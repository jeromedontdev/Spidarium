import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SPECIES,GIFTS,TEMPERAMENTS,CONFIG,freshState,tick,spawnPrey,placeGift,giftBonus,aimWindow,restoreSim,minigameReward,
  DECOR_KINDS,DECOR_SLOTS,DECOR_EMPTY,decorSprite,decorUnlocked,decorOwned,buyDecor,decorPlacedCount,placeDecor,
  removeDecor,toggleDecorBlocking,decorHitTest,decorBlocksPoint,decorNearestBlocking,moveDecor,care} from '../lib/sim.js';
const native=name=>readFile(new URL(`../../main/${name}`,import.meta.url),'utf8');
const neutral=()=>freshState({random:()=>.5,temperament:0});
const keys=['energy_regen_mult','hygiene_decay_mult','pounce_range_mult','aim_window_mult','wild_spawn_bonus',
  'pounce_success_mult','movement_efficiency_mult','mood_mult','fear_mult','prey_flee_penalty',
  'catch_success_mult','prey_detection_range_mult','all_stats_decay_mult','bonus_synergy_mult'];
const close=(a,b)=>assert.ok(Math.abs(a-b)<1e-6,`${a} != ${b}`);

test('every multiplier in all18 native gift definitions is faithfully ported',async()=>{
  const text=(await native('species.c')).split('const gift_def_t k_gifts')[1];
  const effectSource=(await native('gift.c')).split('static const char *const effects[GIFT_KINDS] = {')[1].split('};')[0];
  const effects=[...effectSource.matchAll(/"([^"]+)"/g)].map(row=>row[1]);
  assert.equal(effects.length,18);
  const rows=[...text.matchAll(/\{\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),\s*0x[\da-f]+,\s*([^}]+)\}/g)];
  assert.equal(rows.length,18);
  rows.forEach((row,id)=>{
    assert.equal(GIFTS[id].key,row[1]);assert.equal(GIFTS[id].effect,effects[id]);
    const values=row[4].split(',').map(v=>Number(v.trim().replace(/f$/,'')));
    assert.equal(values.length,keys.length);
    keys.forEach((key,n)=>assert.equal(GIFTS[id].bonuses[key]??(key==='wild_spawn_bonus'?0:1),values[n],`${id}:${key}`));
    const s=neutral();s.level=40;s.silk=10000;assert.equal(placeGift(s,id),true);
    const b=giftBonus(s),synergy=id===16?1.2:id===17?1.3:1;
    keys.forEach((key,n)=>{
      if(key==='bonus_synergy_mult'||key==='wild_spawn_bonus')close(b[key],values[n]);
      else close(b[key],values[n]*(['hygiene_decay_mult','fear_mult','prey_flee_penalty','all_stats_decay_mult'].includes(key)?1/synergy:synergy));
    });
  });
});
test('native temperament rows and window integer truncation match at every level',async()=>{
  const source=await native('game.c');
  const rows=[...source.matchAll(/\{\s*"(bold|timid|greedy|lazy)",\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f,\s*(\d+)\s*\}/g)];
  assert.equal(rows.length,4);
  rows.forEach((row,id)=>{
    const t=TEMPERAMENTS[id];
    for(const [n,key] of ['aim_window_mult','catch_mult','speed_mult','hunger_decay_mult','sleep_threshold'].entries())
      assert.equal(t[key],+row[n+2]);
    const s=neutral();s.temperament=id;
    for(let level=1;level<=100;level++){
      s.level=level;assert.equal(aimWindow(s),Math.trunc(Math.max(1600,2600-level*20)*t.aim_window_mult));
    }
  });
});
test('all8 species movement formulas match native styles and scaling',()=>{
  for(const sp of SPECIES){
    const s=neutral();s.level=sp.unlock_level;s.x=100;s.y=100;s.state=0;s._engine.idle_until=10000;
    assert.equal(spawnPrey(s,sp.id),true);
    const p=s.prey[0];Object.assign(p,{x:300,y:300,heading:0,wander_dir:0,retarget_at:10000});
    tick(s);
    const mult=Math.min(1.8,1+s.level*.06);
    const step=30*mult*sp.speed_mult*.05*(sp.move==='hop'?2.2:1);
    close(p.x,300+step);
    close(p.y,300+(sp.move==='flutter'?Math.sin(50*.006)*14*.05:0));
    assert.equal(p.fleeing,false);
  }
});
test('level/wild/gift flee modifiers use native radius and burst rhythm',()=>{
  const s=neutral();s.level=40;s.silk=10000;s.x=200;s.y=240;
  placeGift(s,4,400,400);spawnPrey(s,4,true);
  const p=s.prey[0];Object.assign(p,{x:210,y:240,heading:0,wander_dir:0,retarget_at:10000});
  s._engine.idle_until=10000;tick(s);
  assert.equal(p.fleeing,true);
  close(p.x,210+70*1.8*1.25*1.15*.85*.05);
  s._engine.now_ms=300;const before=p.x;tick(s);assert.equal(p.x,before);
});
test('browser object-score callback is compatible and preserves setup/theme/colors',()=>{
  const s=neutral();
  const result=minigameReward(s,1,{score:8,rounds:8});
  assert.deepEqual(result,{xp:53,silk:24,score:8});
  s.setup_complete=true;s.theme='cork';s.bodyColour=3;s.eyeColour=4;s.legColour=5;s.fangColour=8;s.sound=false;
  s.autoFeed=true;s.autoHunt=true;
  const loaded=restoreSim(JSON.parse(JSON.stringify(s)),{random:()=>.5});
  assert.equal(loaded.setup_complete,true);assert.equal(loaded.theme,'cork');
  assert.equal(loaded.bodyColour,3);assert.equal(loaded.eyeColour,4);
  assert.equal(loaded.legColour,5);assert.equal(loaded.fangColour,8);assert.equal(loaded.sound,false);
  assert.equal(loaded.autoFeed,true);assert.equal(loaded.autoHunt,true);
});
test('native automation has padding-stable ABI, v2 migration, and gated entries',async()=>{
  const [header,config,save,behaviour,prey]=await Promise.all(['game.h','game_config.h','save.c','behaviour.c','prey.c'].map(native));
  assert.match(header,/_Static_assert\(sizeof\(game_state_t\) == 260/);
  assert.match(header,/offsetof\(game_state_t, auto_feed\) == 129/);
  assert.match(header,/offsetof\(game_state_t, auto_hunt\) == 130/);
  assert.match(header,/offsetof\(game_state_t, volume\) == 131/);
  assert.match(header,/offsetof\(game_state_t, last_seen\) == 132/);
  assert.match(header,/offsetof\(game_state_t, daily_day\) == 136/);
  assert.match(header,/offsetof\(game_state_t, decor_owned\) == 160/);
  assert.match(header,/offsetof\(game_state_t, decor\) == 164/);
  assert.match(config,/#define SAVE_VERSION\s+11/);
  assert.match(save,/version == 2\)[\s\S]*?state->auto_feed = 0;[\s\S]*?state->auto_hunt = 0;/);
  assert.match(save,/migrate_automation\(&state, header\.version\)/);
  assert.match(save,/state\.auto_feed = 0xA5;[\s\S]*?state\.auto_hunt = 0x5A;[\s\S]*?migrate_automation\(&state, header\.version\)/);
  assert.match(save,/hdr\.version >= 3 && \(state\.auto_feed > 1 \|\| state\.auto_hunt > 1\)/);
  assert.match(behaviour,/if \(!g\.auto_hunt && \(g\.state == SPIDER_STALK \|\| g\.state == SPIDER_AIM\)\)/);
  assert.equal((behaviour.match(/if \(g\.auto_hunt && prey_count\(\) > 0/g)??[]).length,3);
  assert.match(behaviour,/else if \(g\.auto_hunt && prey_count\(\) > 0/);
  assert.match(prey,/if \(!g\.auto_feed\) \{[\s\S]*?wild_seeded = false;/);
  assert.match(prey,/WILD_SPAWN_MS \/ \(1u \+ gift->wild_spawn_bonus\)/);
  assert.match(prey,/auto feed=%u \(manual feed unchanged\)/);
  assert.match(behaviour,/auto hunt=%u \(manual aim unchanged\)/);
  assert.equal(CONFIG.LOGIC_HZ,20);
});
test('serial schema producer emits both automation offsets and minVersion3',async()=>{
  const console=await native('test_console.c');
  assert.match(console,/\{"auto_feed", "u8", offsetof\(game_state_t, auto_feed\), 0, 3\}/);
  assert.match(console,/\{"auto_hunt", "u8", offsetof\(game_state_t, auto_hunt\), 0, 3\}/);
  assert.match(console,/\{"volume", "u8", offsetof\(game_state_t, volume\), 0, 4\}/);
  assert.match(console,/minVersion\\":%u/);
  assert.match(console,/SCHEMA_CAP = 5120/);
  assert.match(console,/\{"decor_owned", "u32", offsetof\(game_state_t, decor_owned\), 0, 8\}/);
  assert.match(console,/\{"decor", "decor\[12\]", offsetof\(game_state_t, decor\), sizeof g\.decor, 8\}/);
});

test('decor catalogue matches the native table and every kind is placeable',async()=>{
  const source=(await native('decor.c')).split('k_decor_kinds[DECOR_COUNT] = {')[1].split('};')[0];
  const rows=[...source.matchAll(/\{\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*"([^"]+)"\s*\}/g)];
  assert.equal(rows.length,DECOR_KINDS.length);
  rows.forEach((row,id)=>{
    const d=DECOR_KINDS[id];
    assert.equal(d.name,row[1],`name ${id}`);
    assert.equal(d.cost,Number(row[3]),`cost ${id}`);
    assert.equal(d.level,Number(row[4]),`level ${id}`);
    assert.equal(d.radius,Number(row[5]),`radius ${id}`);
    assert.equal(d.blocking,Number(row[6]),`blocking ${id}`);
    assert.equal(d.key,row[7].replace('decor_',''),`key ${id}`);
    assert.equal(decorSprite(id),`decoritem_${d.key}`,`sprite ${id}`);
  });
});

test('decor purchase, placement, blocking and hit testing behave like the device',()=>{
  const s=neutral();
  s.level=1;s.silk=0;
  assert.equal(decorUnlocked(s,12),false);
  assert.equal(buyDecor(s,0),false);
  assert.equal(decorOwned(s,0),false);

  s.silk=DECOR_KINDS[0].cost;
  assert.equal(buyDecor(s,0),true);
  assert.equal(decorOwned(s,0),true);
  assert.equal(s.silk,0);

  // An unowned kind cannot be placed.
  assert.equal(placeDecor(s,3,100,100),-1);

  const slot=placeDecor(s,0,100,100);
  assert.ok(slot>=0);
  assert.equal(decorPlacedCount(s),1);
  assert.equal(s.decor[slot].blocking,0);
  assert.equal(decorBlocksPoint(s,100,100),false);

  assert.equal(toggleDecorBlocking(s,slot),true);
  assert.equal(s.decor[slot].blocking,1);
  assert.equal(decorBlocksPoint(s,100,100),true);
  assert.equal(decorBlocksPoint(s,100+DECOR_KINDS[0].radius+4,100),false);

  const near=decorNearestBlocking(s,110,100,60);
  assert.ok(near);assert.equal(near.x,100);assert.equal(near.y,100);
  assert.equal(decorNearestBlocking(s,110,100,2),null);

  assert.equal(decorHitTest(s,100,100),slot);
  assert.equal(decorHitTest(s,127,100),slot);
  assert.equal(decorHitTest(s,129,100),-1);
  assert.equal(removeDecor(s,slot),true);
  assert.equal(decorPlacedCount(s),0);
  assert.equal(decorBlocksPoint(s,100,100),false);

  // A blocking kind defaults to blocking, and placement is clamped inside.
  s.level=9;s.silk=DECOR_KINDS[12].cost;
  assert.equal(buyDecor(s,12),true);
  const crystal=placeDecor(s,12,200,200);
  assert.ok(crystal>=0);assert.equal(s.decor[crystal].blocking,1);
  const clamped=placeDecor(s,0,-500,-500);
  assert.ok(clamped>=0);
  assert.ok(s.decor[clamped].x>=34&&s.decor[clamped].y>=34);
});

test('decor slots are empty by default and survive a save round trip',()=>{
  const s=neutral();
  assert.equal(s.decor.length,DECOR_SLOTS);
  assert.ok(s.decor.every(item=>item.kind===DECOR_EMPTY));
  assert.equal(decorPlacedCount(s),0);

  s.level=9;s.silk=1000;
  assert.equal(buyDecor(s,3),true);
  const slot=placeDecor(s,3,150,200);
  assert.ok(slot>=0);
  const loaded=restoreSim(JSON.parse(JSON.stringify(s)),{random:()=>.5});
  assert.equal(decorPlacedCount(loaded),1);
  assert.equal(loaded.decor[slot].kind,3);
  assert.equal(loaded.decor[slot].blocking,1);
  assert.equal(loaded.decor[slot].x,150);
  assert.equal(loaded.decor[slot].y,200);
  assert.equal(decorOwned(loaded,3),true);
});

test('a blocking item stops the spider walking through it',()=>{
  const s=neutral();
  s.level=9;s.silk=1000;
  assert.equal(buyDecor(s,1),true);
  const slot=placeDecor(s,1,240,240);
  assert.ok(slot>=0);
  assert.equal(s.decor[slot].blocking,1);
  // Drive the spider straight at the rock from the left.
  s.spider_x=140;s.spider_y=240;s.spider_heading=0;
  s._engine.px=140;s._engine.py=240;
  for(let i=0;i<200;i++)tick(s,.05);
  assert.equal(decorBlocksPoint(s,s.spider_x,s.spider_y),false,
    'the spider must never end a tick inside a blocking item');
});

test('decor can be moved without losing its slot, kind or blocking flag',()=>{
  const s=neutral();
  s.level=9;s.silk=1000;
  assert.equal(buyDecor(s,1),true);
  const slot=placeDecor(s,1,150,200);
  assert.ok(slot>=0);
  assert.equal(s.decor[slot].blocking,1);
  assert.equal(moveDecor(s,slot,300,120),true);
  assert.equal(s.decor[slot].x,300);
  assert.equal(s.decor[slot].y,120);
  assert.equal(s.decor[slot].kind,1);
  assert.equal(s.decor[slot].blocking,1);
  assert.equal(decorHitTest(s,300,120),slot);
  assert.equal(decorHitTest(s,150,200),-1);
  // Clamped inside the playfield.
  assert.equal(moveDecor(s,slot,-900,-900),true);
  assert.ok(s.decor[slot].x>=34&&s.decor[slot].y>=34);
  assert.equal(moveDecor(s,DECOR_SLOTS,100,100),false);
});

test('a player-initiated sleep holds until energy is full, not at 95',()=>{
  const s=neutral();
  s.energy=80;
  care(s,'sleep');
  assert.equal(s.state,6);
  // Six seconds is the floor, but 80 energy is not rested.
  tick(s,6);
  assert.equal(s.state,6,'a forced sleep must not end at 95 energy');
  // Sleep regenerates 6/min, so 80 -> 100 takes about 3.3 minutes.
  for(let i=0;i<80;i++)tick(s,3);
  assert.equal(s.energy,100);
  assert.notEqual(s.state,6,'a forced sleep ends once energy is full');
});
