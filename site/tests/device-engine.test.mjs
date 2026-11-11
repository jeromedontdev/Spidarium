import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SPECIES,GIFTS,TEMPERAMENTS,CONFIG,freshState,tick,spawnPrey,placeGift,giftBonus,aimWindow,restoreSim,minigameReward} from '../lib/sim.js';
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
  assert.match(header,/_Static_assert\(sizeof\(game_state_t\) == 160/);
  assert.match(header,/offsetof\(game_state_t, auto_feed\) == 129/);
  assert.match(header,/offsetof\(game_state_t, auto_hunt\) == 130/);
  assert.match(header,/offsetof\(game_state_t, volume\) == 131/);
  assert.match(header,/offsetof\(game_state_t, last_seen\) == 132/);
  assert.match(header,/offsetof\(game_state_t, daily_day\) == 136/);
  assert.match(config,/#define SAVE_VERSION\s+7/);
  assert.match(save,/version == 2\)[\s\S]*?state->auto_feed = 0;[\s\S]*?state->auto_hunt = 0;/);
  assert.match(save,/migrate_automation\(state, hdr\.version\)/);
  assert.match(save,/state\.auto_feed = 0xA5;[\s\S]*?state\.auto_hunt = 0x5A;[\s\S]*?migrate_automation\(&state, header\.version\)/);
  assert.match(save,/hdr\.version == SAVE_VERSION && \(state\.auto_feed > 1 \|\| state\.auto_hunt > 1\)/);
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
  assert.match(console,/SCHEMA_CAP = 3328/);
});
