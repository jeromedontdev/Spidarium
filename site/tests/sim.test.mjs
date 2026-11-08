import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,access} from 'node:fs/promises';
import {
  CONFIG,STATES,SPECIES,PREY,GIFTS,BACKGROUNDS,ACHIEVEMENTS,TEMPERAMENTS,
  freshState,restoreSim,tick,feed,care,pounce,beginAim,moveAim,endAim,aimInfo,
  aimAvailable,aimWindow,preyCount,spawnPrey,speciesUnlocked,nextSpecies,preySprite,
  purchaseGift,placeGift,removeGift,visitGift,giftBonus,award,awardAchievement,
  minigameReward,resolvePounce,recomputeMood,seekTo,
} from '../lib/sim.js';
const native=path=>readFile(new URL(`../../main/${path}`,import.meta.url),'utf8');
const state=options=>freshState({random:()=>.5,temperament:0,...options});
const bug=(s,{id=0,x=280,y=240,wild=false}={})=>{
  assert.equal(spawnPrey(s,id,wild),true);
  Object.assign(s.prey.at(-1),{x,y,heading:0,wander_dir:0,retarget_at:Infinity});
  return s.prey.at(-1);
};
const near=(a,b,eps=1e-6)=>assert.ok(Math.abs(a-b)<eps,`${a} != ${b}`);

test('browser constants are the native defines, not approximation rates',async()=>{
  const config=await native('game_config.h');
  let checked=0;
  for(const [key,value] of Object.entries(CONFIG)){
    const match=config.match(new RegExp(`^#define\\s+${key}\\s+([\\d.]+)[fu]?\\s*(?:/.*)?$`,'m'));
    if(match){assert.equal(value,Number(match[1]),key);checked++;}
  }
  assert.ok(checked>=80,`only checked ${checked} constants`);
  assert.match(await native('cspider.c'),/#define STRIDE_PX 24\.0f/);
  assert.equal(CONFIG.STRIDE_PX,24);
});
test('native defaults and flat rendering/control aliases',()=>{
  const s=state();
  assert.deepEqual([s.hunger,s.mood,s.energy,s.hygiene,s.silk,s.level,s.stage],[65,60,80,80,20,1,0]);
  assert.equal(s.autoFeed,false);assert.equal(s.autoHunt,false);
  assert.equal(s.auto_feed,0);assert.equal(s.auto_hunt,0);
  s.autoFeed=true;s.autoHunt=true;
  assert.equal(s.auto_feed,1);assert.equal(s.auto_hunt,1);
  s.x=100;s.y=120;s.heading=1;
  assert.equal(s.spider_x,100);assert.equal(s.spider_y,120);
  near(s.spider_heading,1);near(s.spider.x,100);near(s.spider.y,120);
  assert.equal('battery' in s,false);assert.equal('health' in s,false);assert.equal('coins' in s,false);
});
test('8 species values and eligibility match native catalogue',async()=>{
  const source=await native('species.c');
  const rows=[...source.matchAll(/\{\s*"([^"]+)",\s*(\d+),\s*MOVE_(\w+),\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f/g)];
  assert.equal(rows.length,8);
  rows.forEach((m,i)=>{
    const sp=SPECIES[i];
    assert.equal(sp.name,m[1]);assert.equal(sp.unlock_level,+m[2]);assert.equal(sp.move,m[3].toLowerCase());
    ['speed_mult','flee_speed_mult','flee_radius_mult','perfect_mult'].forEach((key,n)=>assert.equal(sp[key],+m[n+4]));
  });
  const s=state();
  assert.equal(nextSpecies(s).key,'cricket');
  assert.equal(feed(s,'beetle'),false);assert.equal(preyCount(s),0);
  for(const sp of SPECIES){s.level=sp.unlock_level-1;assert.equal(speciesUnlocked(s,sp.id),false);s.level++;assert.equal(speciesUnlocked(s,sp.key),true);}
  s.level=20;assert.equal(nextSpecies(s),null);
});
test('manual feed only spawns live prey, works asleep, and caps physical slots at 3',()=>{
  const s=state();care(s,'sleep');
  for(let i=0;i<3;i++)assert.equal(feed(s,'fly'),true);
  assert.equal(feed(s,'fly'),false);assert.equal(s.lastEvent.reason,'maximum 3 prey');
  assert.equal(s.hunger,65);assert.equal(s.silk,20);assert.equal(s.xp,0);assert.equal(s.meals,0);
  assert.equal(preyCount(s),3);
});
test('autoFeed and autoHunt default OFF independently; manual aim still works',()=>{
  const s=state();tick(s,60);assert.equal(preyCount(s),0);
  s.autoFeed=true;tick(s);assert.equal(preyCount(s),1);assert.equal(s.prey[0].wild,true);
  tick(s,5);assert.ok(![2,3,4].includes(s.state));assert.equal(s.meals,0);
  s.autoFeed=false;s.autoHunt=true;s.hunger=65;s.state=0;s._engine.idle_until=0;
  tick(s);assert.equal(s.state,STATES.STALK);
  s.autoHunt=false;tick(s);assert.ok(![2,3].includes(s.state));
  assert.equal(beginAim(s,s.x,s.y),true);
  const before=[s.x,s.y,s.prey[0].x,s.prey[0].y];
  tick(s,5);assert.deepEqual([s.x,s.y,s.prey[0].x,s.prey[0].y],before);
});
test('wild timing/flower/daily visitors honor auto_feed rather than manual feed',()=>{
  const s=state();s.autoFeed=true;tick(s);assert.equal(preyCount(s),1);
  care(s,'clean');
  // Runtime scheduling is directly controllable for focused long-period tests.
  s._engine.last_wild=s._engine.now_ms-1199950;
  tick(s);assert.equal(preyCount(s),1);
  care(s,'clean');s.level=40;s.silk=1000;placeGift(s,6);
  s._engine.last_wild=s._engine.now_ms-599950;
  tick(s);assert.equal(preyCount(s),1);
  s.days_lived=1;tick(s);assert.equal(preyCount(s),2);assert.equal(s.prey[1].species,7);
  const xp=s.xp;tick(s);assert.equal(s.xp,xp); // VISITOR_XP is unused on device.
  care(s,'clean');s.autoFeed=false;s.days_lived=2;tick(s);assert.equal(preyCount(s),0);
});
test('native manual-aim gates: full, sleep, eat, molt, pounce; not sick or low energy',()=>{
  const s=state();bug(s);
  s.hunger=95;assert.equal(aimAvailable(s),false);
  s.hunger=94;assert.equal(aimAvailable(s),true);
  for(const value of [4,5,6,8]){s.state=value;assert.equal(aimAvailable(s),false);}
  s.state=9;s.sick=1;s.energy=0;assert.equal(aimAvailable(s),true);
  assert.equal(beginAim(s,240,240),true);assert.equal(endAim(s,250,240),false);
  assert.equal(beginAim(s,280,240),true);assert.equal(endAim(s,280,240),false);
  assert.equal(beginAim(s,240,240),true);assert.equal(endAim(s,280,240),true);
});
test('pounce flight reaches target at native speed and reports catch after landing',()=>{
  const s=state(),p=bug(s,{x:360});
  assert.equal(pounce(s,360,240),true);assert.equal(s.state,4);
  assert.equal(s.energy,77);assert.equal(s.meals,0);
  tick(s,.4);assert.equal(s.state,4);assert.ok(s.x>240&&s.x<360);
  tick(s,.1);assert.equal(s.state,5);assert.equal(s.x,360);assert.equal(p.dead,true);
  assert.equal(s.lastEvent.type,'catch');assert.equal(s.lastEvent.quality,'perfect');
  assert.equal(s.hunger,85);assert.equal(s.energy,83);assert.equal(s.hygiene,68);
  assert.equal(s.mood,72);assert.equal(s.silk,25);assert.equal(s.meals,1);assert.equal(s.bond,4);
  assert.equal(s.streak,1);assert.equal(s.xp,63); // 18 streak XP + 3 achievements * 15.
  tick(s,.8);assert.notEqual(s.state,5);
});
test('range is 160 times gift bonus, rectangular bounds not invented circle',()=>{
  const s=state();bug(s);
  assert.equal(pounce(s,1000,1000),true);
  near(Math.hypot(s._engine.pounce.x-240,s._engine.pounce.y-240),Math.hypot(113,113));
  const t=state();t.level=3;t.silk=100;placeGift(t,2);bug(t);
  assert.equal(pounce(t,1000,240),true);assert.equal(t._engine.pounce.x,432);
  t.x=446;t.y=446;t.state=0;t._engine.pounce=null;
  assert.equal(pounce(t,1000,1000),true);assert.equal(t._engine.pounce.x,446);assert.equal(t._engine.pounce.y,446);
});
test('geometry uses landed bug, body edge and species perfect radius, not first prey',()=>{
  const s=state();s.level=6;
  const a=bug(s,{x:100}),b=bug(s,{id:3,x:360});s.x=316;
  s._engine.aim_power=1;s._engine.aim_ideal=0;
  assert.equal(resolvePounce(s,true),true);assert.equal(a.dead,false);assert.equal(b.dead,true);
  assert.equal(s.lastEvent.quality,'good'); // 44 centre -22 body =22, outside the beetle's 15px perfect radius.
  const t=state({random:()=>.999});t.level=6;bug(t,{id:3,x:360});t.x=315;
  t._engine.aim_power=1;t._engine.aim_ideal=0;
  assert.equal(resolvePounce(t,true),false);assert.equal(t.xp,1);assert.equal(t.silk,20);
});
test('reach gate forbids remotely disappearing bugs and resets streak',()=>{
  const s=state();const p=bug(s,{x:400});s.x=200;s.streak=6;
  assert.equal(resolvePounce(s),false);assert.equal(p.dead,false);assert.equal(s.streak,0);
  assert.equal(s.xp,1);assert.equal(s.lastEvent.reason,'out of reach');
});
test('good catches use exact probability multipliers and only perfect advances streak',()=>{
  const s=state({random:()=>0});bug(s,{x:320});s.x=260;s.energy=50;s.stage=1;s.sick=1;s.streak=2;
  s._engine.aim_power=1;s._engine.aim_ideal=0;
  assert.equal(resolvePounce(s,false),true);
  near(s.lastEvent.probability,clampProbability((.55-38*.004+.5*.2+.5*.1)*.7*.55*1.1));
  assert.equal(s.lastEvent.quality,'good');assert.equal(s.streak,2);
  assert.equal(s.lastEvent.xp,11);assert.equal(s.lastEvent.silk,3);assert.equal(s.bond,2);
});
function clampProbability(n){return Math.min(.95,Math.max(.2,n));}
test('dead PNGs remain visible 2200ms, then free native prey slots',async()=>{
  for(const sp of SPECIES){
    const s=state();s.level=20;s.xp=950;const p=bug(s,{id:sp.id,x:240});
    assert.equal(resolvePounce(s),true);assert.equal(preyCount(s),0);
    assert.equal(p.alive,true);assert.equal(p.dead,true);
    await access(new URL(`../../assets/${preySprite(sp.key,true)}.png`,import.meta.url));
    tick(s,2.15);assert.equal(p.alive,true);
    tick(s,.05);assert.equal(p.alive,false);
    assert.equal(feed(s,sp.id),true);assert.equal(s.prey.length,1);
  }
});
test('clean/play care rewards are exact native XP and do not pay invented Silk',()=>{
  const s=state();bug(s);s.poop.push({expires:10000});care(s,'clean');
  assert.equal(s.hygiene,100);assert.equal(s.cleans,1);assert.equal(s.xp,18);
  assert.equal(preyCount(s),0);assert.equal(s.poop.length,0);assert.equal(s.silk,20);
  care(s,'clean');assert.equal(s.xp,21);assert.equal(s.cleans,2);
  care(s,'play');assert.equal(s.mood,70);assert.equal(s.energy,75);assert.equal(s.plays,1);
  assert.equal(s.xp,40);assert.equal(s.silk,20);assert.equal(s.state,7);
  assert.throws(()=>care(s,'pet')); // Defined on device, but not currently wired.
});
test('native integer decay runs every 3s, empty tank regenerates hygiene',()=>{
  const s=state();tick(s,2.95);
  assert.equal(s.hunger,65);assert.equal(s.mood,60);
  tick(s,.05);assert.equal(s.mood,63);assert.equal(s.hunger,65);
  tick(s,117);assert.equal(s.hunger,59);assert.equal(s.hygiene,84);
  // Float accumulator in needs.c intentionally carries the small residue.
  assert.equal(s.energy,80);tick(s,3);assert.equal(s.energy,79);
});
test('hunger temperament and gift decay values use native fractional carries',()=>{
  for(const [id,loss] of [[0,6],[1,5],[2,7],[3,5]]){
    const s=state({temperament:id});tick(s,120);assert.equal(s.hunger,65-loss);
  }
  const s=state();s.level=40;s.silk=1000;placeGift(s,17);tick(s,120);
  assert.equal(s.hunger,61);
  near(giftBonus(s).all_stats_decay_mult,1/1.3);
});
test('sleep is held 6000ms even at full energy and regenerates 6/min (gift 1.5x)',()=>{
  const s=state();s.energy=100;care(s,'sleep');tick(s,5.95);assert.equal(s.state,6);
  tick(s,.05);assert.equal(s.state,0);
  care(s,'sleep');care(s,'sleep');assert.equal(s.state,0);
  const t=state();t.energy=10;placeGift(t,0);care(t,'sleep');tick(t,6);
  assert.equal(t.energy,10);assert.equal(t.state,0);
  tick(t,3);assert.equal(t.energy,11);
});
test('mood truncates toward zero, low-stat penalty halves while sick',()=>{
  const s=state();s.hunger=24;s.energy=24;s.hygiene=80;s.mood=50;
  recomputeMood(s);assert.equal(s.mood,42);
  s.sick=1;s.mood=50;recomputeMood(s);assert.equal(s.mood,45);
});
test('native sickness thresholds/recovery/bond loss and no death even at zero',()=>{
  const s=state();s.hunger=9;s.mood=50;tick(s,3);assert.equal(s.sick,1);assert.equal(s.state,9);
  s.bond=8;s.hunger=26;s.mood=60;tick(s,3);assert.equal(s.sick,0);assert.equal(s.bond,7);
  assert.ok(s.achievements&ACHIEVEMENTS.find(a=>a.id==='survived_sick').bit);
  s.hunger=0;s.mood=0;s.energy=0;s.hygiene=0;tick(s,60);
  assert.equal(s.sick,1);assert.notEqual(s.state,'dead');assert.equal('dead' in s,false);
  const t=state();t.hygiene=0;t.hunger=80;t.energy=80;t.mood=60;tick(t,3);assert.equal(t.sick,0);
});
test('aging is exactly 30s ticks /2880 day and stage/molt rewards match device',()=>{
  const s=state();tick(s,29.95);assert.equal(s.age_ticks,0);tick(s,.05);assert.equal(s.age_ticks,1);
  s.age_ticks=2*2880-1;s.days_lived=1;s._engine.last_age=s._engine.now_ms-29950;
  tick(s);assert.equal(s.age_ticks,5760);assert.equal(s.stage,1);assert.equal(s.state,8);
  assert.equal(s.xp,25);assert.equal(s.last_molt_day,2);
  // Native MOLT has no exit; keep parity rather than invent a life-cycle rule.
  tick(s,1);assert.equal(s.state,8);
  s.state=0;s.age_ticks=4*2880-1;s.days_lived=3;s._engine.last_age=s._engine.now_ms-29950;
  tick(s);assert.equal(s.xp,43);assert.equal(s.last_molt_day,4);
  s.state=0;s.age_ticks=5*2880-1;s.days_lived=4;s._engine.last_age=s._engine.now_ms-29950;
  tick(s);assert.equal(s.stage,2);assert.equal(s.xp,68);assert.equal(s.last_molt_day,5);
});
test('achievement bits reward once, recursion-safe level5 grants exactly15XP',async()=>{
  const s=state();award(s,195);assert.equal(s.level,4);
  award(s,5);assert.equal(s.xp,215);assert.equal(s.level,5);
  assert.equal(awardAchievement(s,'level_5'),false);assert.equal(s.xp,215);
  const source=await native('game.h');
  for(const a of ACHIEVEMENTS){
    const name=`ACH_${a.id.toUpperCase()}`;
    assert.match(source,new RegExp(`#define ${name}\\s+\\(1u << ${Math.log2(a.bit)}\\)`));
    await access(new URL(`../../assets/${a.asset}.png`,import.meta.url));
  }
});
test('18 gift shop levels/prices/native effective bonuses and neutral removal',async()=>{
  const source=await native('gift.c');
  const levels=source.match(/levels\[GIFT_KINDS\]\s*=\s*\{([^}]+)\}/)[1].trim().split(',').map(Number);
  assert.equal(GIFTS.length,18);assert.deepEqual(GIFTS.map(g=>g.level),levels);
  const s=state();
  assert.equal(purchaseGift(s,17),false);assert.equal(purchaseGift(s,0),true);assert.equal(s.silk,4);
  assert.equal(s.gift.used,0);assert.equal(purchaseGift(s,0),true);assert.equal(s.silk,4);
  assert.equal(placeGift(s,0,10,999),true);assert.deepEqual([s.gift.x,s.gift.y],[34,446]);
  near(giftBonus(s).energy_regen_mult,1.5);
  s.level=40;s.silk=10000;
  for(const g of GIFTS){
    assert.equal(g.cost,12+g.level*4);assert.equal(placeGift(s,g.id),true);
    for(const [key,value] of Object.entries(g.bonuses))if(key!=='bonus_synergy_mult')near(giftBonus(s)[key],value);
  }
  near(giftBonus(s).pounce_range_mult,1.3);near(giftBonus(s).fear_mult,1/1.3);
  placeGift(s,16);near(giftBonus(s).pounce_range_mult,1.2);
  removeGift(s);for(const [key,value] of Object.entries(giftBonus(s)))assert.equal(value,key==='wild_spawn_bonus'?0:1);
});
test('gift visits walk, hold 3s, reward only mood+4 energy+4, then cooldown60s',()=>{
  const s=state();placeGift(s,0,245,240);s.hunger=80;s.energy=50;s.mood=50;
  visitGift(s);tick(s);assert.equal(s.spider.gift_visiting,true);
  assert.equal(s._engine.gift_hold_until,s._engine.now_ms+3000);
  const before=[s.xp,s.silk];tick(s,3);
  assert.equal(s.visits,1);assert.equal(s.spider.gift_visiting,false);
  assert.equal(s.energy,54);assert.deepEqual([s.xp,s.silk],before);
  assert.equal(s._engine.gift_visit_at,s._engine.now_ms+60000);
});
test('bond gift is +2 Silk at native120-240s and only without placed gift',()=>{
  const s=state({random:()=>0});s.bond=75;tick(s);
  assert.equal(s._engine.next_bond_gift,s._engine.now_ms+120000);
  s._engine.next_bond_gift=s._engine.now_ms+50;tick(s);assert.equal(s.silk,22);
  placeGift(s,0);s._engine.next_bond_gift=s._engine.now_ms+50;tick(s);assert.equal(s.silk,6);
});
test('mind reevaluates4s; weights/native strict thresholds and tie order',()=>{
  const s=state();s.hunger=24;s.energy=10;tick(s);assert.equal(s.want,'food');assert.equal(s.mind_rule,'starving');
  s.hunger=25;tick(s,3.95);assert.equal(s.want,'food');
  tick(s);assert.equal(s.want,'rest');assert.equal(s.mind_rule,'exhausted');
});
test('automatic FSM stalk/crouch/window/pounce obeys flags and temperament',()=>{
  const s=state();bug(s,{x:320});s.autoHunt=true;
  tick(s);assert.equal(s.state,2);tick(s);assert.equal(s.state,3);
  assert.equal(aimWindow(s),Math.trunc(2580*.85));
  const start=s._engine.aim_start;s._engine.now_ms=start+aimWindow(s)-50;
  tick(s);assert.equal(s.state,4);assert.equal(s._engine.pounce.player,false);
  const t=state({temperament:1});t.bond=50;
  assert.equal(aimWindow(t),Math.trunc(Math.trunc(2580*1.25)*1.15));
  t.level=100;assert.equal(aimWindow(t),2300);
});
test('movement is subpixel stage/temperament-scaled, gait is distance-driven',()=>{
  const s=state();seekTo(s,400,240);tick(s);
  near(s.x,240+55*.75*1.1*.05);const initial=s.gait_phase;
  tick(s);near(s.gait_phase-initial,55*.75*1.1*.05*2*Math.PI/24);
  const phase=s.gait_phase;s.paused=true;tick(s,2);assert.equal(s.gait_phase,phase);
  s.paused=false;s.state=0;s._engine.idle_until=s._engine.now_ms+1000;tick(s,.1);
  assert.equal(s.gait_phase,phase);assert.equal(s.spider.walking,false);
});
test('species hop pauses after200ms and flee bursts stop300..1300ms; drag freezes all',()=>{
  const s=state();s.level=2;s.x=100;s.y=100;s._engine.idle_until=10000;
  const p=bug(s,{id:1,x:400,y:400});tick(s,.15);assert.ok(p.x>400);
  const before=p.x;tick(s,.05);assert.equal(p.x,before);
  s.x=390;s.y=400;tick(s,.15);assert.equal(p.fleeing,true);const fleeing=p.x;
  tick(s,.15);assert.equal(p.x,fleeing);
  beginAim(s,s.x,s.y);const held=[p.x,p.y,s.x,s.y];tick(s,1);
  assert.deepEqual([p.x,p.y,s.x,s.y],held);
});
test('restore migrates old progress/ownership and resets runtime, no offline decay',()=>{
  const s=restoreSim({version:2,name:'My spider',hunger:-10,mood:60,hygiene:80,energy:999,xp:85,silk:20,
    owned:[0,0],gift:0,cleanCount:3,games:2,prey:[],battery:78,
    autoFeed:true,autoHunt:true},{random:()=>.5,now:()=>999999999});
  assert.equal(s.name,'My spider');assert.equal(s.hunger,0);assert.equal(s.energy,100);
  assert.equal(s.xp,106);assert.equal(s.level,3);assert.equal(s.gifts_owned,1);
  assert.equal(s.gift.kind,0);assert.equal(s.cleans,3);assert.equal(s.plays,2);
  assert.equal(s.prey.length,0);assert.equal(s.state,0);assert.equal(s.autoFeed,false);assert.equal(s.autoHunt,false);
  const before=s.hunger;tick(s,2.95);assert.equal(s.hunger,before);assert.equal(s.age_ticks,0);
  const restored=restoreSim(JSON.parse(JSON.stringify({...state(),auto_feed:1,auto_hunt:1})),{random:()=>.5});
  assert.equal(restored.autoFeed,true);assert.equal(restored.autoHunt,true);
  assert.equal(BACKGROUNDS.length,8);
});
test('null restore creates fresh state but corrupt saves throw descriptive errors',()=>{
  assert.equal(restoreSim(null,{random:()=>.5}).version,4);
  const good=()=>JSON.parse(JSON.stringify(state()));
  for(const value of [[],new Date(),42,'{}',{}, {version:99}, {version:4}]){
    assert.throws(()=>restoreSim(value),/Invalid browser progress:/);
  }
  for(const [key,value] of [
    ['xp',-1],['silk','20'],['hunger',101],['energy',NaN],['mood',Infinity],
    ['auto_feed',2],['auto_hunt',true],['autoFeed','yes'],['colour',6],
    ['fang_colour',9],['name',false],['level',0],['gifts_owned',1<<18],
    ['achievements',1<<11],['state',99],['background','fake'],['setup_complete',1],
    ['createdAt','not a timestamp'],['gift',{kind:99,used:1,x:240,y:240,variant:0}],
    ['_engine',null],['spider',{x:'x',y:240,heading:0,gait_phase:0}],
  ]){
    const raw=good();raw[key]=value;
    assert.throws(()=>restoreSim(raw),/Invalid browser progress:/,key);
  }
  const absent=good();delete absent.silk;
  assert.throws(()=>restoreSim(absent),/missing silk/);
  const metadata=good();metadata._engine.now_ms=NaN;
  assert.throws(()=>restoreSim(metadata),/_engine.now_ms/);
});
test('both valid legacy versions retain earned progress and clamp legacy need ranges',()=>{
  for(const version of [1,2]){
    const raw={version,hunger:-5,mood:102,energy:999,hygiene:70.5,xp:80,silk:24,
      name:'Legacy spider',owned:[0,1],gift:1,background:'fern',autoFeed:true,autoHunt:true};
    const s=restoreSim(raw,{random:()=>.5});
    assert.equal(s.version,4);assert.equal(s.level,3);assert.equal(s.xp,100);
    assert.deepEqual([s.hunger,s.mood,s.energy,s.hygiene],[0,100,100,70]);
    assert.equal(s.silk,24);assert.equal(s.gifts_owned,3);assert.equal(s.gift.kind,1);
    assert.equal(s.name,'Legacy spider');assert.equal(s.auto_feed,0);assert.equal(s.auto_hunt,0);
    assert.throws(()=>restoreSim({...raw,xp:NaN}),/xp/);
    assert.throws(()=>restoreSim({...raw,owned:[99]}),/owned/);
  }
});
test('20Hz batching and injected random/time produce deterministic JSON-safe play',()=>{
  const a=state(),b=state();
  feed(a,'fly');feed(b,'fly');tick(a,5);
  for(let i=0;i<100;i++)tick(b,.05);
  assert.deepEqual(JSON.parse(JSON.stringify(a)),JSON.parse(JSON.stringify(b)));
  const paused=a._engine.now_ms;a.paused=true;tick(a,30);assert.equal(a._engine.now_ms,paused);
  assert.throws(()=>tick(a,NaN));assert.throws(()=>tick(a,-1));
});
test('native minigame callback includes needs_play +2+score*4XP, +score*3Silk',()=>{
  for(let score=0;score<=8;score++){
    const s=state(),result=minigameReward(s,'hunt',score);
    assert.deepEqual(result,{score,xp:21+score*4,silk:score*3});
    assert.equal(s.mood,70);assert.equal(s.energy,75);assert.equal(s.plays,1);
    const second=minigameReward(s,score);assert.equal(second.xp,6+score*4);
  }
});
