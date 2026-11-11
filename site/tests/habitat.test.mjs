import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import { DeviceGame } from '../lib/device-ui.js';

test('all background prices match native; buying once persists free reapplication', async () => {
  const native = await readFile(new URL('../../main/theme.c', import.meta.url), 'utf8');
  const prices = native.match(/k_costs\[THEME_COUNT\] = \{([^}]+)\}/)[1].match(/\d+/g).map(Number);
  assert.deepEqual(E.BACKGROUND_COSTS, prices);
  for (let id=0; id<E.BACKGROUNDS.length; id++) {
    const state=E.freshState();state.silk=100;
    assert.equal(E.buyBackground(state,id),true);
    assert.equal(state.silk,100-E.BACKGROUND_COSTS[id]);
    const restored=E.restoreSim(JSON.parse(JSON.stringify(state)));
    assert.equal(E.backgroundOwned(restored,id),true);
    assert.equal(E.buyBackground(restored,0),true);
    assert.equal(E.buyBackground(restored,id),true);
    assert.equal(restored.silk,state.silk);
  }
  const state=E.freshState();state.silk=0;
  assert.equal(E.buyBackground(state,15),false);
  assert.equal(state.background,'cork');assert.equal(state.backgrounds_owned,1);
});

test('v8 migration keeps current background free, decor and pet without granting the catalogue', () => {
  const old=JSON.parse(JSON.stringify(E.freshState()));
  old.version=8;old.background='crystal';old.silk=37;delete old.backgrounds_owned;
  old.decor_owned=1;old.decor[0]={kind:0,blocking:1,x:110,y:120};
  const state=E.restoreSim(old);
  assert.equal(state.background,'crystal');assert.equal(state.silk,37);
  assert.equal(state.backgrounds_owned,1|(1<<14));
  assert.deepEqual(state.decor[0],old.decor[0]);
  assert.equal(state.deep_sleep_after,0);assert.equal(state.screen_timeout,3);
});

test('gift movement costs nothing and preserves variant, ownership and effective bonus', () => {
  const state=E.freshState();E.placeGift(state,0,240,240);state.gift.variant=2;
  const before={silk:state.silk,owned:state.gifts_owned,bonus:E.giftBonus(state)};
  assert.equal(E.moveGift(state,0,999),true);
  assert.deepEqual(state.gift,{kind:0,variant:2,used:1,x:34,y:446});
  assert.equal(state.silk,before.silk);assert.equal(state.gifts_owned,before.owned);
  assert.deepEqual(E.giftBonus(state),before.bonus);
  assert.equal(E.giftHitTest(state,40,440),true);
});

test('power preferences validate, persist and reject invalid ranges', () => {
  const state=E.freshState();
  Object.assign(state,{power_mode:2,screen_timeout:4,deep_sleep_after:3,rtc_wake:2});
  const restored=E.restoreSim(JSON.parse(JSON.stringify(state)));
  for (const key of ['power_mode','screen_timeout','deep_sleep_after','rtc_wake'])
    assert.equal(restored[key],state[key]);
  for (const [key,value] of [['power_mode',3],['screen_timeout',5],['deep_sleep_after',5],['rtc_wake',3]])
    assert.throws(()=>E.restoreSim({...state,[key]:value}),new RegExp(key));
});

test('hold survives small pointer jitter; a real drag cancels it', t => {
  t.mock.timers.enable({apis:['setTimeout']});
  const state=E.freshState();state.decor_owned=1;E.placeDecor(state,0,100,100);
  const opened=[];
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,started:true,modal:null,game:null,hud:false,decorKind:null,decorMoveSlot:null,giftKind:null,
    screen:{focus(){}},canvas:{setPointerCapture(){}},audio:{unlock(){}},
    point:event=>event,open:(...args)=>opened.push(args),
  });
  ui.pointerDown({x:100,y:100,pointerId:1});
  ui.pointerMove({x:103,y:102});
  t.mock.timers.tick(550);
  assert.deepEqual(opened,[['Decor Item',0,0]]);
  assert.equal(ui.pointerConsumed,true);
  ui.pointerDown({x:100,y:100,pointerId:1});
  ui.pointerMove({x:125,y:100});t.mock.timers.tick(550);
  assert.equal(opened.length,1);
});

test('a placement release cannot also pet or pounce', () => {
  const state=E.freshState();state.decor_owned=1;E.placeDecor(state,0,100,100);
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,started:true,modal:null,game:null,hud:false,decorMoveSlot:0,decorKind:null,giftKind:null,
    screen:{focus(){}},audio:{unlock(){}},point:event=>event,
    refreshHUD(){},persist(){},report(){},
  });
  ui.pointerDown({x:state.spider_x,y:state.spider_y});
  ui.pointerUp({x:state.spider_x,y:state.spider_y});
  assert.equal(state.pets,0);
  assert.equal(state.decor[0].x,state.spider_x);
  assert.equal(ui.decorMoveSlot,null);
});

test('ground prey gait follows travel and a resting cricket plants its feet', () => {
  const state=E.freshState({random:()=>.5});
  state.level=20;state.spider_x=400;state.spider_y=400;state.state=E.STATES.SLEEP;
  assert.equal(E.spawnPrey(state,3),true);
  const beetle=state.prey.at(-1);
  Object.assign(beetle,{x:100,y:100,heading:0,wander_dir:0,retarget_at:1e9});
  E.tick(state,.5);
  const distance=beetle.x-100;
  assert.ok(distance>6);
  assert.equal(beetle.frame,Math.floor(distance/6)%2);
  assert.equal(E.spawnPrey(state,1),true);
  const cricket=state.prey.at(-1);
  Object.assign(cricket,{x:100,y:150,heading:0,wander_dir:0,retarget_at:1e9,frame:1});
  E.tick(state,.05);
  assert.equal(cricket.x,100);
  assert.equal(cricket.frame,0);
});
