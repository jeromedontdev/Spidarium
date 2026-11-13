import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';
import {decodeSave} from '../lib/save.js';

function fixture(){
  const state=E.freshState();state.decor_owned=0xffff;state.level=40;
  for(let i=0;i<15;i++)E.placeDecor(state,i%16,240,240);
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state,decorMoveSlot:null,decorKind:null,giftKind:null,close(){this.modal=null;},
    refreshHUD(){},persist(){this.saves=(this.saves??0)+1;},report(){},
  });
  return {state,ui};
}

test('all three path modes distinguish steering from canopy draw/hit order',()=>{
  const {state}=fixture();
  for(const item of state.decor)item.blocking=0;
  assert.equal(E.decorBlocksPoint(state,240,240),false);
  state.decor[0].blocking=2;
  assert.equal(E.decorBlocksPoint(state,240,240),false);
  assert.equal(E.decorNearestBlocking(state,240,240,100),null);
  assert.equal(E.decorHitTest(state,240,240),0,'canopy wins above later floor slots');
  state.decor[1].blocking=1;
  assert.equal(E.decorBlocksPoint(state,240,240),true);
  assert.ok(E.decorNearestBlocking(state,240,240,100));
  assert.equal(E.decorHitTest(state,240,240),0);
  state.decor[0].blocking=0;
  assert.equal(E.decorHitTest(state,240,240),14);
});

test('overlapping canopy cannot change the steering footprint of a solid item',()=>{
  const states=[true,false].map(canopy=>{
    const s=E.freshState({random:()=>.5});
    Object.assign(s,{state:E.STATES.WANDER,spider_x:202,spider_y:240,spider_heading:0});
    Object.assign(s._engine,{px:202,py:240,retarget_at:1e9,wander_dir:0});
    s.decor=[...(canopy?[{kind:3,blocking:2,x:240,y:240}]:[]),
      {kind:0,blocking:1,x:240,y:240}];
    E.tick(s,.05);return s;
  });
  assert.equal(states[0].spider_heading,states[1].spider_heading);
  assert.equal(states[0].spider_x,states[1].spider_x);
  assert.equal(states[0].spider_y,states[1].spider_y);
});

test('layer reordering crosses the inline/tail boundary and preserves all item records',()=>{
  const {state}=fixture();
  state.decor[5].blocking=2;
  const original=structuredClone(state.decor),moving=state.decor[14];
  assert.equal(E.setDecorLayer(state,14,0),true);
  assert.equal(state.decor[0],moving);
  assert.deepEqual(state.decor[5],original[5],'canopy keeps its own band/position');
  assert.deepEqual([...state.decor].sort((a,b)=>a.kind-b.kind),original.sort((a,b)=>a.kind-b.kind));
  const before=structuredClone(state.decor);
  assert.equal(E.setDecorLayer(state,0,99),false);
  assert.deepEqual(state.decor,before);
});

test('moving stages path, layer and position; Cancel is exact and Done saves once',()=>{
  const {state,ui}=fixture(),before=structuredClone(state.decor);
  ui.beginDecorMove(0);ui.setPlacementPath(2);ui.positionPlacement(180,180);
  assert.equal(ui.placement.blocking,2);assert.deepEqual(state.decor,before);
  ui.cancelPlacement();assert.deepEqual(state.decor,before);assert.equal(ui.saves,undefined);
  ui.beginDecorMove(0);ui.changePlacementLayer(1);ui.changePlacementLayer(1);
  assert.equal(ui.placement.layer,2);assert.deepEqual(state.decor,before);
  ui.setPlacementPath(1);assert.equal(ui.placement.layer,2,'Solid keeps its existing Floor layer');
  ui.setPlacementPath(0);assert.equal(ui.placement.layer,2,'Walk Over keeps its existing Floor layer');
  ui.commitPlacement();assert.equal(state.decor[2].kind,0);assert.equal(ui.saves,1);
  ui.beginDecorMove(2);ui.setPlacementPath(2);ui.commitPlacement();
  assert.equal(state.decor[2].blocking,2);assert.equal(ui.saves,2);
  const restored=E.restoreSim(JSON.parse(JSON.stringify(state)));
  assert.deepEqual(restored.decor,state.decor,'browser round trip retains mode and layer');
});

test('new placement has path/layer controls, clamped arrows, and no new charge until the existing shop purchase',()=>{
  const {state,ui}=fixture(),silk=state.silk;
  ui.beginDecorPlacement(0);ui.setPlacementPath(2);
  ui.changePlacementLayer(-1);assert.equal(ui.placement.layer,0);
  ui.changePlacementLayer(1);assert.equal(ui.placement.layer,0);
  ui.commitPlacement();
  assert.equal(state.decor.at(-1).blocking,2);
  assert.equal(state.silk,silk);
  assert.equal(ui.saves,1);
});

test('preview respects staged layer ordering without changing saved furniture',()=>{
  const {state,ui}=fixture(),calls=[];
  state.decor=state.decor.slice(0,3);state.decor.forEach(item=>{item.blocking=0;});
  ui.ctx={drawImage(image){calls.push(image.name);},strokeRect(){}};ui.image=name=>({name,complete:true,naturalWidth:64});
  ui.beginDecorMove(2);ui.changePlacementLayer(-1);
  ui.drawDecor(false);assert.deepEqual(calls,state.decor.map((item,i)=>E.decorSprite([0,2,1][i])));
  calls.length=0;ui.setPlacementPath(2);ui.drawDecor(false);ui.drawDecor(true);
  assert.deepEqual(calls,[0,1,2].map(E.decorSprite));
  assert.equal(state.decor[2].blocking,0);
});

test('v13 raw inline/tail modes survive decode; v12 rejects new mode2 and v13 rejects3',async()=>{
  const schema=JSON.parse(await readFile(new URL('../save-schema.json',import.meta.url),'utf8'));
  const bytes=new Uint8Array(12+268+6),v=new DataView(bytes.buffer);
  v.setUint32(0,schema.magic,true);v.setUint16(4,13,true);v.setUint32(8,274,true);
  for(let i=0;i<12;i++)bytes[12+164+i*6]=255;
  bytes[12+164]=0;bytes[12+165]=2;bytes[12+268]=1;bytes[12+269]=2;
  v.setUint32(12+236,1,true);bytes[12+266]=255;v.setUint32(12+256,1,true);
  const decoded=decodeSave(bytes,schema);
  assert.equal(decoded.fields.decor[0].blocking,2);assert.equal(decoded.fields.decor[12].blocking,2);
  v.setUint16(4,12,true);assert.throws(()=>decodeSave(bytes,schema),/decor/i);
  v.setUint16(4,13,true);bytes[12+165]=3;assert.throws(()=>decodeSave(bytes,schema),/decor/i);
});

test('canopy renders above the entire creature layer and below one lighting overlay',()=>{
  const {state,ui}=fixture(),calls=[];
  state.prey[0]={alive:true,dead:false,species:0,x:240,y:240};
  state.lights_dimmed=true;
  ui.spiderPixels={data:new Uint8ClampedArray(96*96*4)};
  ui.spiderCanvas={name:'spider'};ui.spiderCtx={putImageData(){}};
  ui.ctx=new Proxy({}, {get(target,key){
    if(key==='drawImage')return image=>calls.push(image.name);
    if(key==='fillRect')return ()=>{if(target.fillStyle==='rgba(7,12,32,0.43)')calls.push('dim');};
    return target[key]??(()=>{});
  }});
  ui.image=name=>({name,complete:true,naturalWidth:64});
  ui.drawDecor=canopy=>calls.push(canopy?'canopy':'floor');
  ui.draw();
  const prey=calls.findIndex(name=>name.startsWith('prey_'));
  assert.ok(calls.indexOf('floor')<calls.indexOf('spider'));
  assert.ok(calls.indexOf('spider')<prey);
  assert.ok(prey<calls.indexOf('canopy'));
  assert.ok(calls.indexOf('canopy')<calls.indexOf('dim'));
  assert.equal(calls.filter(name=>name==='dim').length,1);
});
