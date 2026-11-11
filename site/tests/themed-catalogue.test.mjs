import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';
import {decodeSave} from '../lib/save.js';

const source=name=>readFile(new URL(`../../main/${name}`,import.meta.url),'utf8');

test('all 68 native/browser decor entries keep names, prices, levels and footprints in sync',async()=>{
  const text=(await source('decor.c')).split('static const decor_def_t k_decor_kinds')[1].split('};')[0];
  const rows=[...text.matchAll(/\{\s*"([^"]+)",\s*"[^"]+",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*"decor_([^"]+)"\s*\}/g)];
  assert.equal(rows.length,68);
  rows.forEach((row,id)=>{
    const item=E.DECOR_KINDS[id];
    assert.deepEqual([item.name,item.cost,item.level,item.radius,item.blocking,item.key],
      [row[1],...row.slice(2,6).map(Number),row[6]],`decor ${id}`);
  });
  const themes=await source('theme.c');
  const names=[...themes.split('const theme_def_t k_themes')[1].split('};')[0]
    .matchAll(/\{\s*"([^"]+)"/g)].map(row=>row[1].replaceAll(' ','_'));
  assert.deepEqual(names,E.BACKGROUNDS);
  const costs=themes.split('static const uint16_t k_costs')[1].split('{')[1].split('}')[0]
    .split(',').map(v=>Number(v.trim()));
  assert.deepEqual(costs,E.BACKGROUND_COSTS);
});

test('all high ownership IDs are independent, persist and never alias low IDs',()=>{
  const s=E.freshState();s.level=100;s.xp=4950;s.silk=100000;
  for(let id=0;id<36;id++){
    assert.equal(E.giftOwned(s,id),false);
    assert.equal(E.purchaseGift(s,id),true);
    for(let n=0;n<36;n++)assert.equal(E.giftOwned(s,n),n<=id,`${id} gift ${n}`);
  }
  assert.equal(s.gifts_owned,0xffffffff);assert.equal(s.catalogue_owned_high,15);
  for(let id=0;id<68;id++){
    assert.equal(E.buyDecor(s,id),true);
    for(let n=0;n<68;n++)assert.equal(E.decorOwned(s,n),n<=id,`${id} decor ${n}`);
  }
  assert.equal(s.catalogue_owned_high,255);
  for(let id=1;id<48;id++){
    assert.equal(E.buyBackground(s,id),true);
    for(let n=0;n<48;n++)assert.equal(E.backgroundOwned(s,n),n<=id,`${id} background ${n}`);
  }
  assert.equal(s.backgrounds_owned,0xffffffff);assert.equal(s.backgrounds_owned_extra,65535);
  E.placeGift(s,35);E.placeDecor(s,67,240,240);
  const restored=E.restoreSim(JSON.parse(JSON.stringify(s)));
  assert.equal(restored.gift.kind,35);assert.equal(restored.decor[0].kind,67);
  for(let id=0;id<36;id++)assert.equal(E.giftOwned(restored,id),true);
  for(let id=0;id<68;id++)assert.equal(E.decorOwned(restored,id),true);
  assert.equal(E.backgroundOwned(restored,47),true);
  const old=E.freshState();old.version=11;old.catalogue_owned_high=255;old.backgrounds_owned_extra=65535;
  const migrated=E.restoreSim(JSON.parse(JSON.stringify(old)));
  assert.equal(migrated.catalogue_owned_high,0);assert.equal(migrated.backgrounds_owned_extra,0);
  assert.equal(migrated.border_id,255);
});

test('48 theme-matched borders buy once, mix with any floor, remove free and persist',()=>{
  const s=E.freshState();s.silk=100000;
  for(let id=0;id<48;id++){
    const wallet=s.silk;
    assert.equal(E.buyBorder(s,id),true);assert.equal(s.silk,wallet-E.borderCost(id));
    assert.equal(s.border_id,id);assert.equal(s.background,'cork');
    assert.equal(E.borderOwned(s,id),true);
    assert.equal(E.buyBorder(s,id),true);assert.equal(s.silk,wallet-E.borderCost(id));
    for(let n=0;n<48;n++)assert.equal(E.borderOwned(s,n),n<=id,`${id} border ${n}`);
  }
  assert.equal(s.borders_owned,0xffffffff);assert.equal(s.borders_owned_extra,65535);
  const restored=E.restoreSim(JSON.parse(JSON.stringify(s)));
  assert.equal(restored.border_id,47);assert.equal(E.borderOwned(restored,47),true);
  const wallet=s.silk;assert.equal(E.buyBorder(s,255),true);assert.equal(s.silk,wallet);
  assert.equal(s.border_id,255);assert.equal(E.borderOwned(s,47),true);
  const poor=E.freshState();poor.silk=0;
  assert.equal(E.buyBorder(poor,47),false);assert.equal(poor.border_id,255);
  assert.equal(E.buyBorder(poor,48),false);assert.equal(E.borderOwned(poor,15),false);
  assert.throws(()=>E.restoreSim({...poor,border_id:47}),/border must be owned/);
});

test('border shop has None plus every theme thumbnail and Settings remains last',()=>{
  let rows=[];
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:E.freshState(),close(){},panel(){return {};},refreshHUD(){},persist(){},
    list:items=>{rows=items;},report(){},
  });
  ui.open('Menu');assert.equal(rows.at(-1).label,'Settings');
  assert.ok(rows.some(row=>row.label==='Borders'));
  ui.open('Borders');assert.equal(rows.length,49);assert.equal(rows[0].label,'No Border');
  assert.deepEqual(rows.slice(1).map(row=>row.icon),E.BACKGROUNDS.map(key=>`border_${key}_0`));
  ui.state.silk=10000;rows.at(-1).click();
  assert.equal(ui.state.border_id,47);assert.equal(E.borderOwned(ui.state,47),true);
  rows[0].click();assert.equal(ui.state.border_id,255);
  let parent;DeviceGame.prototype.back.call({open:name=>{parent=name;}},'Borders');
  assert.equal(parent,'Menu');
});

test('six new fictional treats apply exact paired gains and never grant permanent ownership',()=>{
  for(const [id,cost,field,gain,secondary,bonus] of [
    [6,16,'mood',28,'energy',14],[7,14,'hunger',32,'energy',8],
    [8,18,'energy',30,'mood',16],[9,20,'hunger',28,'energy',22],
    [10,16,'hygiene',30,'mood',12],[11,22,'hunger',20,'mood',24],
  ]){
    const s=E.freshState();Object.assign(s,{hunger:40,mood:40,energy:40,hygiene:40,silk:cost});
    assert.equal(E.buyTreat(s,id),true);assert.equal(s.silk,0);
    assert.equal(s[field],40+gain);assert.equal(s[secondary],40+bonus);
    assert.equal(s.gifts_owned,0);assert.equal(s.catalogue_owned_high,0);
    assert.equal(E.buyTreat(s,id),false);
  }
  assert.equal(new Set(E.TREATS.map(item=>item.asset)).size,12);
});

test('pre-expansion progress cannot unlock new IDs through historical padding or lists',()=>{
  for(const mutation of [
    s=>{s.background='cloud_sanctuary';},
    s=>{s.owned=[35];},
    s=>{s.gift={kind:35,used:1,variant:0,x:240,y:240};},
    s=>{s.decor[0]={kind:67,x:240,y:240,blocking:0};},
  ]){
    const old=E.freshState();old.version=11;
    old.catalogue_owned_high=255;old.backgrounds_owned_extra=65535;
    mutation(old);assert.throws(()=>E.restoreSim(old),/unknown|invalid/i);
  }
});

test('raw v12 ownership/border fields and shifted tail decode; v11 tail stays at 260',async()=>{
  const schema=JSON.parse(await readFile(new URL('../save-schema.json',import.meta.url)));
  assert.equal(schema.version,12);assert.equal(schema.fields.length,52);
  for(const version of [10,11,12]){
    const prefix=version===12?268:260,bytes=new Uint8Array(12+prefix+6),v=new DataView(bytes.buffer);
    v.setUint32(0,schema.magic,true);v.setUint16(4,version,true);v.setUint32(8,prefix+6,true);
    bytes[12+236]=1;v.setUint32(12+256,1,true);
    if(version===12){
      bytes[12+245]=255;v.setUint16(12+246,65535,true);
      v.setUint32(12+260,0x80000000,true);v.setUint16(12+264,32768,true);bytes[12+266]=47;
      bytes[12+prefix]=67;
    }else{
      bytes[12+245]=255;v.setUint16(12+246,65535,true);bytes[12+prefix]=33;
    }
    const decoded=decodeSave(bytes,schema);
    assert.equal(decoded.fields.decor.at(-1).kind,version===12?67:33);
    assert.equal(decoded.fields.catalogue_owned_high,version===12?255:0);
    assert.equal(decoded.fields.border_id,version===12?47:255);
    if(version===12){
      bytes[12+266]=48;assert.throws(()=>decodeSave(bytes,schema),/border ownership/);
      bytes[12+266]=47;bytes[12+prefix]=68;assert.throws(()=>decodeSave(bytes,schema),/decor record/);
      bytes[12+prefix]=67;bytes[12+schema.fields.find(f=>f.name==='decor').offset]=68;
      assert.throws(()=>decodeSave(bytes,schema),/inline decor record/);
    }
  }
});
