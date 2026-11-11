import test from 'node:test';
import assert from 'node:assert/strict';
import * as E from '../lib/device-engine.js';
import { DeviceGame } from '../lib/device-ui.js';

test('expanded catalogue has six treats, thirty-four decor kinds and twenty-four scenes',()=>{
  assert.equal(E.TREATS.length,6);
  assert.equal(E.DECOR_KINDS.length,34);
  assert.equal(E.BACKGROUNDS.length,24);
  assert.equal(new Set(E.DECOR_KINDS.map(item=>item.key)).size,34);
  assert.equal(new Set(E.BACKGROUNDS).size,24);
});

test('decor grows past twelve, preserves high ownership bits, moves and restores all records',()=>{
  const state=E.freshState();state.level=100;state.silk=100000;
  for(let kind=0;kind<E.DECOR_KINDS.length;kind++){
    assert.equal(E.buyDecor(state,kind),true);
    assert.equal(E.decorOwned(state,kind),true,`owned ${kind}`);
  }
  for(let i=0;i<80;i++)assert.equal(E.placeDecor(state,i%34,100+i,150+i),i);
  assert.equal(E.decorPlacedCount(state),80);
  assert.equal(state.decor_owned,0xffffffff);
  assert.equal(state.decor_owned_extra,3);
  assert.equal(E.moveDecor(state,79,340,350),true);
  assert.equal(E.decorHitTest(state,340,350),79);
  assert.equal(E.toggleDecorBlocking(state,79),true);
  const restored=E.restoreSim(JSON.parse(JSON.stringify(state)));
  assert.deepEqual(restored.decor,state.decor);
  assert.equal(restored.decor_owned,0xffffffff);
  assert.equal(restored.decor_owned_extra,3);
  assert.equal(E.removeDecor(restored,79),true);
  assert.equal(E.placeDecor(restored,33,360,360),79);
  assert.equal(E.decorPlacedCount(restored),80);
});

test('three new treats have distinct paired benefits with exact Silk costs',()=>{
  for(const [kind,cost,gains] of [
    [3,14,{hunger:30,mood:12}],
    [4,12,{hygiene:25,energy:12}],
    [5,18,{energy:28,mood:20}],
  ]){
    const state=E.freshState();
    Object.assign(state,{hunger:40,mood:40,hygiene:40,energy:40,silk:cost});
    assert.equal(E.buyTreat(state,kind),true);
    assert.equal(state.silk,0);
    for(const [field,gain] of Object.entries(gains))assert.equal(state[field],40+gain);
    assert.equal(E.buyTreat(state,kind),false);
  }
});

test('music and enclosure lighting persist independently of master sound and brightness',()=>{
  const state=E.freshState();
  assert.equal(state.music_enabled,1);
  Object.assign(state,{music_enabled:0,lights_dimmed:1,brightness:80,muted:0});
  const restored=E.restoreSim(JSON.parse(JSON.stringify(state)));
  assert.equal(restored.music_enabled,0);assert.equal(restored.lights_dimmed,1);
  assert.equal(restored.brightness,80);assert.equal(restored.muted,0);
  for(const field of ['music_enabled','lights_dimmed'])
    assert.throws(()=>E.restoreSim({...state,[field]:2}),new RegExp(field));
  const legacy={...state,version:9};delete legacy.music_enabled;delete legacy.lights_dimmed;
  assert.equal(E.restoreSim(legacy).music_enabled,1);
  assert.equal(E.restoreSim(legacy).lights_dimmed,0);
});

test('double tap toggles enclosure lights without petting, spending or changing brightness',t=>{
  t.mock.timers.enable({apis:['setTimeout']});
  const state=E.freshState(),pets=state.pets,silk=state.silk,brightness=state.brightness;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,started:true,modal:null,game:null,hud:false,decorKind:null,decorMoveSlot:null,giftKind:null,
    screen:{focus(){}},canvas:{setPointerCapture(){}},audio:{unlock(){}},
    point:event=>event,persist(){},report(){},
  });

  const tap=()=>{const p={x:state.spider_x,y:state.spider_y,pointerId:1};ui.pointerDown(p);ui.pointerUp(p);};
  tap();tap();t.mock.timers.tick(500);
  assert.equal(state.lights_dimmed,1);assert.equal(state.pets,pets);
  assert.equal(state.silk,silk);assert.equal(state.brightness,brightness);
  tap();tap();t.mock.timers.tick(500);
  assert.equal(state.lights_dimmed,0);assert.equal(state.pets,pets);
});

test('Sound is one reversible master checkbox beside independent music and volume',()=>{
    const state=E.freshState(),unlocked=[];
    let rows=[];
    const ui=Object.assign(Object.create(DeviceGame.prototype),{
      state,close(){},panel(){return {};},refreshHUD(){},persist(){},
      audio:{unlock:enabled=>unlocked.push(enabled)},list:items=>{rows=items;},
    });
    ui.open('Sound');
    assert.deepEqual(rows.map(row=>row.label),['Volume','Sound','Background Music','Music Volume','Soundtrack']);
    assert.equal(new Set(rows.slice(0,3).map(row=>row.icon)).size,3);
    assert.equal(rows[1].checkbox,true);assert.equal(rows[1].selected,true);
    rows[1].click();
    assert.equal(state.sound,false);assert.equal(state.music_enabled,1);
    assert.equal(rows[1].selected,false);
    rows[1].click();
    assert.equal(state.sound,true);assert.equal(state.music_enabled,1);
    assert.deepEqual(unlocked,[false,true]);
    rows[2].click();
    assert.equal(state.music_enabled,0);assert.equal(state.sound,true);
    rows[3].slider.change(35);
    assert.equal(state.music_volume,35);assert.equal(state.volume,70);
    rows[4].click();
    assert.deepEqual(rows.map(row=>row.label),['Silk Garden','Moonlit Hollow','Rainy Canopy']);
    rows[2].click();
    assert.equal(state.music_track,2);assert.equal(state.music_volume,35);
  });

test('root and species icons are distinct and redundant movement entries are absent',()=>{
    let rows=[];
    const ui=Object.assign(Object.create(DeviceGame.prototype),{
      state:E.freshState(),close(){},panel(){return {};},refreshHUD(){},
      list:items=>{rows=items;},
    });
    ui.open('Menu');
    assert.equal(rows.length,7);
    assert.equal(new Set(rows.map(row=>row.icon)).size,7);
    assert.equal(rows.some(row=>['Arrange Decor','Placed Gift','Placed Items'].includes(row.label)),false);
    ui.open('Prey Species');
    assert.deepEqual(rows.map(row=>row.icon),E.SPECIES.map(item=>`prey_${item.key}_walk_0`));
    ui.open('Treats');
    assert.equal(rows.length,6);assert.equal(new Set(rows.map(row=>row.icon)).size,6);
  });
