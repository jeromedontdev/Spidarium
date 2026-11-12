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
    point:event=>event,open:(...args)=>opened.push(args),close(){},refreshHUD(){},report(){},
  });
  ui.pointerDown({x:100,y:100,pointerId:1});
  ui.pointerMove({x:103,y:102});
  t.mock.timers.tick(550);
  assert.deepEqual(opened,[]);
  assert.equal(ui.placement.slot,0);assert.equal(ui.modal,null);
  assert.equal(ui.pointerConsumed,true);
  ui.releasePlacementPointer=()=>{ui.placementPointer=null;};
  ui.cancelPlacement();
  ui.pointerDown({x:100,y:100,pointerId:1});
  ui.pointerMove({x:125,y:100});t.mock.timers.tick(550);
  assert.equal(opened.length,0);assert.equal(ui.placement,null);
});

test('held placement drag and release stage only; Done commits once without petting or pouncing', () => {
  const state=E.freshState();state.decor_owned=1;E.placeDecor(state,0,100,100);
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,started:true,modal:null,game:null,hud:false,decorMoveSlot:0,decorKind:null,giftKind:null,
    screen:{focus(){}},audio:{unlock(){}},point:event=>event,
    close(){},canvas:{setPointerCapture(){},hasPointerCapture(){return true;},releasePointerCapture(){}},
    refreshHUD(){},persist(){this.saves=(this.saves??0)+1;},report(){},
  });
  ui.beginDecorMove(0);
  ui.pointerDown({x:100,y:100,pointerId:1});
  ui.pointerMove({x:state.spider_x,y:state.spider_y,pointerId:1});
  assert.equal(state.decor[0].x,100);
  assert.equal(ui.placement.x,state.spider_x);
  ui.pointerUp({x:state.spider_x,y:state.spider_y,pointerId:1});
  assert.equal(state.decor[0].x,100);
  assert.equal(ui.decorMoveSlot,0);
  assert.equal(state.pets,0);
  ui.commitPlacement();ui.commitPlacement();
  assert.equal(state.decor[0].x,state.spider_x);
  assert.equal(ui.decorMoveSlot,null);
  assert.equal(ui.saves,1);
});

test('new and moved decor/gifts preview their art kind and cancel without changing save data',()=>{
  for(const mode of ['decor','move decor','gift','move gift']){
    const state=E.freshState();state.decor_owned=1;state.gifts_owned=1;
    E.placeDecor(state,0,100,150);E.placeGift(state,0,210,220);state.gift.variant=2;
    const ui=Object.assign(Object.create(DeviceGame.prototype),{
      state,started:true,modal:null,game:null,hud:false,decorMoveSlot:null,decorKind:null,giftKind:null,
      screen:{focus(){}},canvas:{setPointerCapture(){},hasPointerCapture(){return true;},releasePointerCapture(){}},
      audio:{unlock(){}},point:event=>event,close(){},refreshHUD(){},persist(){},report(){},
    });
    const before=JSON.stringify(state);
    if(mode==='decor')ui.beginDecorPlacement(0);
    if(mode==='move decor')ui.beginDecorMove(0);
    if(mode==='gift')ui.chooseGift(0);
    if(mode==='move gift')ui.beginGiftMove();
    assert.equal(ui.placement.kind,0);
    assert.equal(ui.placement.gift,mode.includes('gift'));
    const initial={...ui.placement};
    ui.pointerDown({x:initial.x+5,y:initial.y+4,pointerId:7});
    ui.pointerMove({x:300,y:310,pointerId:8});
    assert.equal(ui.placement.x,initial.x,'ignore other pointers');
    ui.pointerMove({x:300,y:310,pointerId:7});
    assert.equal(ui.placement.x,295,'preserve grab offset');
    ui.pointerUp({x:300,y:310,pointerId:7});
    assert.equal(JSON.stringify(state),before);
    ui.cancelPlacement();
    assert.equal(JSON.stringify(state),before);
    assert.equal(ui.placement,null);
    if(mode==='decor')ui.beginDecorPlacement(0);
    if(mode==='move decor')ui.beginDecorMove(0);
    if(mode==='gift')ui.chooseGift(0);
    if(mode==='move gift')ui.beginGiftMove();
    ui.positionPlacement(-100,1000);
    const radius=mode.includes('gift')?0:E.DECOR_KINDS[0].radius;
    assert.equal(ui.placement.x,34+radius);assert.equal(ui.placement.y,446-radius);
    const silk=state.silk;
    ui.commitPlacement();
    const item=mode.includes('gift')?state.gift:state.decor[mode==='decor'?1:0];
    assert.equal(item.x,34+radius);assert.equal(item.y,446-radius);
    assert.equal(state.silk,silk);
    if(mode==='move gift')assert.equal(item.variant,2);
  }
});

test('cancelled pointer releases capture but keeps editing; failed Done retains preview and reports error',()=>{
  const state=E.freshState();state.decor_owned=1;
  const errors=[],released=[];
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,started:true,modal:null,game:null,hud:false,decorMoveSlot:null,decorKind:null,giftKind:null,
    screen:{focus(){}},canvas:{setPointerCapture(){},hasPointerCapture(){return true;},releasePointerCapture:id=>released.push(id)},
    audio:{unlock(){}},point:event=>event,close(){},refreshHUD(){},persist(){assert.fail('failed commit must not persist');},
    report:(text,error)=>{if(error)errors.push(text);},
  });
  ui.beginDecorPlacement(0);ui.pointerDown({x:240,y:240,pointerId:4});
  ui.releasePlacementPointer(4);
  assert.deepEqual(released,[4]);
  ui.pointerMove({x:350,y:300,pointerId:4});
  assert.equal(ui.placement.x,240);
  state.decor_owned=0;ui.commitPlacement();
  assert.ok(ui.placement);assert.equal(errors.length,1);
  ui.cancelPlacement();assert.equal(ui.placement,null);
});

test('on-screen editing stages blocking until Done and Put Away stays in the enclosure',()=>{
  const state=E.freshState();state.decor_owned=1;state.gifts_owned=1;
  E.placeDecor(state,0,180,180);E.placeGift(state,0,240,240);state.gift.variant=2;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,decorMoveSlot:null,decorKind:null,giftKind:null,close(){this.modal=null;},refreshHUD(){},persist(){},report(){},
  });
  const silk=state.silk;
  ui.beginDecorMove(0);ui.togglePlacementBlocking();
  assert.equal(state.decor[0].blocking,0);
  assert.equal(ui.placement.blocking,true);
  ui.cancelPlacement();assert.equal(state.decor[0].blocking,0);
  ui.beginDecorMove(0);ui.togglePlacementBlocking();ui.commitPlacement();
  assert.equal(state.decor[0].blocking,1);
  ui.beginDecorMove(0);ui.putAwayPlacement();
  assert.equal(state.decor[0].kind,E.DECOR_EMPTY);
  assert.equal(ui.modal,null);assert.equal(ui.placement,null);assert.ok(E.decorOwned(state,0));
  ui.beginGiftMove();ui.togglePlacementBlocking();assert.equal(ui.placement.blocking,undefined);
  ui.putAwayPlacement();assert.equal(state.gift.used,0);assert.equal(state.gift.variant,2);
  assert.equal(state.gifts_owned,1);assert.equal(state.silk,silk);assert.equal(ui.modal,null);
});

test('placement keyboard arrows stage, Enter confirms and Escape cancels without hunting',t=>{
  globalThis.HTMLInputElement=class {};globalThis.HTMLTextAreaElement=class {};
  t.after(()=>{delete globalThis.HTMLInputElement;delete globalThis.HTMLTextAreaElement;});
  const state=E.freshState();state.decor_owned=1;E.placeDecor(state,0,100,150);
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,decorMoveSlot:null,decorKind:null,giftKind:null,close(){},refreshHUD(){},persist(){},report(){},
  });
  const key=name=>ui.key({key:name,target:{},preventDefault(){}});
  ui.beginDecorMove(0);key('ArrowRight');key('ArrowDown');
  assert.equal(state.decor[0].x,100);assert.equal(ui.placement.x,110);assert.equal(ui.placement.y,160);
  key('Enter');assert.equal(state.decor[0].x,110);assert.equal(state.decor[0].y,160);
  ui.beginDecorMove(0);key('ArrowLeft');key('Escape');
  assert.equal(state.decor[0].x,110);assert.equal(ui.placement,null);
  assert.equal(state.pets,0);assert.equal(state._engine.drag,null);assert.equal(ui.aiming,false);
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
