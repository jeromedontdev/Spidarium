import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';
import {HELP_TOPICS} from '../lib/help.js';

const source=name=>readFile(new URL(`../../main/${name}`,import.meta.url),'utf8');
const fresh=()=>E.freshState({random:()=>.5,temperament:0});
function catchMeal(s){
  E.care(s,'clean');
  s.hunger=40;s.state=0;
  assert.equal(E.spawnPrey(s,0),true);
  Object.assign(s.prey.find(p=>p.alive&&!p.dead),{x:s.spider_x,y:s.spider_y});
  assert.equal(E.resolvePounce(s,true),true);
  E.tick(s);
}
function resources(s){s.level=40;s.xp=1950;s.silk=10000;}

test('all twenty achievements are reachable through their actual gameplay event paths',()=>{
  const earned=new Set();
  const exercise=(ids,action)=>{
    const s=fresh();action(s);E.tick(s);
    for(const id of ids){
      assert.ok(s.achievements&E.ACHIEVEMENTS.find(a=>a.id===id).bit,id);
      earned.add(id);
    }
  };
  exercise(['first_meal','first_hunt','first_perfect','ten_meals','twenty_five_meals'],s=>{
    for(let i=0;i<25;i++)catchMeal(s);
  });
  exercise(['first_pet','bond_50'],s=>{s.bond=49;assert.ok(E.pet(s,s.spider_x,s.spider_y));});
  exercise(['first_clean','ten_cleans'],s=>{
    for(let i=0;i<10;i++){s.hygiene=50;E.care(s,'clean');E.tick(s);}
  });
  exercise(['first_play','ten_plays'],s=>{
    for(let i=0;i<10;i++){E.minigameReward(s,0,{score:8,rounds:8});E.tick(s);}
  });
  exercise(['first_molt','adult'],s=>{
    for(const day of [2,5]){
      s.age_ticks=day*2880-1;s.days_lived=day-1;
      s._engine.last_age=s._engine.now_ms-29950;
      E.tick(s);assert.equal(s.state,E.STATES.MOLT);
      E.tick(s,.8);assert.notEqual(s.state,E.STATES.MOLT);
    }
  });
  exercise(['level_5'],s=>E.addXP(s,200));
  exercise(['survived_sick'],s=>{s.sick=1;s.hunger=30;s.mood=60;E.tick(s,3);assert.equal(s.sick,0);});
  exercise(['streak_3'],s=>{
    for(let day=1;day<=3;day++){s.daily_override=day;assert.ok(E.dailyClaim(s));E.tick(s);}
  });
  exercise(['decor_3'],s=>{
    resources(s);assert.ok(E.buyDecor(s,0));
    for(const x of [100,220,340])assert.ok(E.placeDecor(s,0,x,150)>=0);
  });
  exercise(['backgrounds_4'],s=>{resources(s);for(let id=0;id<4;id++)assert.ok(E.buyBackground(s,id));});
  exercise(['first_border'],s=>{resources(s);assert.ok(E.buyBorder(s,0));});
  exercise(['first_gift'],s=>assert.ok(E.placeGift(s,0)));
  assert.deepEqual([...earned].sort(),E.ACHIEVEMENTS.map(a=>a.id).sort());
});

test('achievement goals/bits match firmware; rewards pay once including level-up recursion and reload',async()=>{
  const header=await source('achievements.h'), bits=await source('game.h');
  for(const a of E.ACHIEVEMENTS){
    assert.ok(header.includes(`"${a.name}", "${a.goal}"`),a.id);
    assert.match(bits,new RegExp(`#define ACH_${a.id.toUpperCase()}\\s+\\(1u << ${Math.log2(a.bit)}\\)`));
    assert.equal(a.xp,15);assert.equal(a.silk,20);
  }
  const s=fresh();
  for(const a of E.ACHIEVEMENTS)E.awardAchievement(s,a.id);
  assert.equal(s.xp,300);assert.equal(s.silk,420);
  const restored=E.restoreSim(JSON.parse(JSON.stringify(s)));
  for(const a of E.ACHIEVEMENTS)assert.equal(E.awardAchievement(restored,a.id),false);
  E.checkAchievements(restored);
  assert.equal(restored.xp,300);assert.equal(restored.silk,420);
});

test('native, browser and website expose the same complete, readable help topics',async()=>{
  const header=await source('help_content.h');
  const native=[...header.matchAll(/\{"([^"]+)","([^"]+)","([^"]+)"\}/g)]
    .map(([,name,icon,text])=>({name,icon,text}));
  assert.deepEqual(native,HELP_TOPICS);
  assert.equal(native.length,17);
  for(const t of native){
    assert.ok(t.text.length<430,t.name);
    await readFile(new URL(`../../assets/ui_${t.icon}.png`,import.meta.url));
  }
  const app=await readFile(new URL('../app.js',import.meta.url),'utf8');
  const html=await readFile(new URL('../index.html',import.meta.url),'utf8');
  assert.match(app,/for \(const topic of HELP_TOPICS\)/);
  assert.match(html,/href="#help"/);
  assert.match(html,/id="help-topics"/);
  assert.doesNotMatch(html,/release-files/);
  assert.doesNotMatch(app,/firmware\.manifest\.files\.forEach/);
});

test('shop affordability disables unaffordable tiles, keeps owned choices free and preserves treat page',()=>{
  let actual;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:fresh(),close(){},panel(){return {};},refreshHUD(){},persist(){},
    list(rows,page){actual={rows,page};},report(){},
  });
  resources(ui.state);
  E.buyDecor(ui.state,0);E.buyBackground(ui.state,1);E.buyBorder(ui.state,2);
  ui.state.silk=0;
  for(const [menu,owned,unowned] of [['Decor',0,1],['Backgrounds',1,2],['Borders',3,4]]){
    ui.open(menu,1);
    assert.notEqual(actual.rows[owned].enabled,false,`${menu}: owned applies free`);
    assert.equal(actual.rows[unowned].enabled,false,`${menu}: unaffordable disabled`);
  }
  ui.open('Treats',1);assert.ok(actual.rows.every(row=>row.enabled===false));
  ui.state.silk=1000;ui.open('Treats',1);
  actual.rows[8].click();assert.equal(actual.page,1);
  ui.open('Achievements');
  assert.equal(actual.rows.length,20);
  assert.ok(actual.rows.every(row=>typeof row.click==='function'&&/\+15 XP \+20 Silk/.test(row.detail)));
});

test('updates live in About; settings tiles use distinct descriptive artwork',()=>{
  let rows;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:fresh(),close(){},panel(){return {};},refreshHUD(){},list(items){rows=items;},
  });
  ui.open('About');
  assert.ok(rows.some(row=>row.label==='Firmware Update'&&typeof row.click==='function'));
  assert.ok(rows.every(row=>row.label!=='Help Guide'));
  ui.open('Settings');assert.ok(rows.every(row=>row.label!=='Help Guide'));
  ui.open('Menu');
  assert.equal(rows.filter(row=>row.label==='Help Guide').length,1);
  rows.find(row=>row.label==='Help Guide').click();
  assert.equal(ui.modal.name,'Help Guide');
  ui.back('Help Guide');assert.equal(ui.modal.name,'Menu');
  ui.open('Sound');
  assert.equal(new Set(rows.map(row=>row.icon)).size,rows.length);
  ui.open('Help Guide');assert.equal(rows.length,17);
});
