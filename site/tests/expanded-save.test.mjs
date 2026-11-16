import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { decodeSave, encodeSave, validateSchema } from '../lib/save.js';
import { companionDefaults } from '../lib/companion.js';
import { lifeDefaults,encodeLifePet } from '../lib/life.js';

const schema=JSON.parse(await readFile(new URL('../save-schema.json',import.meta.url)));

test('v17 life, all three full archives and extra decor roundtrip without cloning shared fields',()=>{
  const life=lifeDefaults();
  life.progress=[5,3,1];life.cooldown=30;life.visitor_day=20000;life.season_highwater=8105;life.light_gesture_disabled=1;
  const pet={hunger:80,mood:70,energy:60,hygiene:90,stage:2,temperament:1,bond:55,
    name:'Ancestor',colour:2,pupil_colour:3,leg_colour:4,fang_colour:5,
    spider_x:240,spider_y:200,companion:companionDefaults(),life};
  life.retired=Array.from({length:3},()=>encodeLifePet(pet));life.retired_count=3;
  const fields={...current(),silk:12345,life},bytes=encodeSave(fields,schema);
  const decoded=decodeSave(bytes,schema);
  assert.deepEqual(decoded.fields.life,life);
  assert.equal(decoded.fields.silk,12345);
  assert.deepEqual(encodeSave(decoded.fields,schema,bytes),bytes);
  assert.equal(decoded.header.version,19);
  for(const [offset,value] of [[8,6],[11,31],[12,2],[13,2],[14,4],[15,2],[16,101],
    [16+46,3],[16+96,4],[16+736,6],[16+739,1]]){
    const invalid=bytes.slice();invalid[872+offset]=value;
    assert.throws(()=>decodeSave(invalid,schema),/life/,`life offset ${offset}`);
  }
  const bad=structuredClone(schema);bad.life.retired.stride=739;
  assert.throws(()=>validateSchema(bad),/life layout/);
});

test('v15 full prefix and maximum tail migrate to v19 with fresh optional life state',()=>{
  const currentBytes=encodeSave(current(),schema),old=new Uint8Array(12+860+6);
  old.set(currentBytes.subarray(0,872));old.set(currentBytes.subarray(3112),872);
  const v=new DataView(old.buffer);v.setUint16(4,15,true);v.setUint32(8,old.length-12,true);
  const decoded=decodeSave(old,schema);
  assert.deepEqual(decoded.fields.life,lifeDefaults());
  const migrated=encodeSave(decoded.fields,schema,old);
  assert.deepEqual(migrated.subarray(0,4),old.subarray(0,4));
  assert.deepEqual(migrated.subarray(12,872),old.subarray(12,872));
  assert.deepEqual(migrated.subarray(3112),old.subarray(872));
});

test('v16 maximum payload migrates with gesture enabled and rejects nonzero legacy reserved byte',()=>{
  const count=3368,fields={...current(),decor_extra_count:count,
    decor:Array.from({length:12+count},()=>({kind:1,blocking:0,x:100,y:200}))};
  /* A genuine v16 payload: the v18 prefix is four bytes longer, so splice those
   * out before relabelling the header. */
  const encoded=encodeSave(fields,schema);
  const old=new Uint8Array(encoded.length-4);
  old.set(encoded.subarray(0,12+3096));old.set(encoded.subarray(12+3100),12+3096);
  const view=new DataView(old.buffer);
  view.setUint16(4,16,true);view.setUint32(8,old.length-12,true);
  assert.equal(old.length,12+23304);
  const decoded=decodeSave(old,schema);
  assert.equal(decoded.fields.life.light_gesture_disabled,0);
  const legacySchema=structuredClone(schema);legacySchema.version=16;
  legacySchema.payloadSizes=[120,3096];legacySchema.maxPayload=23304;legacySchema.decorTail.offset=3096;
  legacySchema.fields=legacySchema.fields.filter(f=>f.minVersion<18);
  delete legacySchema.life.light_gesture_disabled;legacySchema.life.reserved=15;
  assert.deepEqual(decodeSave(old,legacySchema).fields.life,decoded.fields.life);
  const migrated=encodeSave(decoded.fields,schema,old);
  assert.equal(new DataView(migrated.buffer).getUint16(4,true),19);
  assert.deepEqual(migrated.subarray(12+3100),old.subarray(12+3096));
  for(const value of [1,2,255]){
    old[887]=value;
    assert.throws(()=>decodeSave(old,schema),/life/);
  }
});

test('new life flag is a strict 0 or 1 and schema records its introduction',()=>{
  assert.deepEqual(schema.life.light_gesture_disabled,{offset:15,minVersion:17});
  for(const value of [-1,2,255,true,null,undefined,'1']){
    const life={...lifeDefaults(),light_gesture_disabled:value};
    assert.throws(()=>encodeSave({life},schema),/life/);
  }
});

test('v14 preserves all ten music IDs while old headers reject new scores',()=>{
  const bytes=new Uint8Array(280),view=new DataView(bytes.buffer);
  view.setUint32(0,schema.magic,true);view.setUint16(4,14,true);view.setUint32(8,268,true);
  for(let i=0;i<12;i++)bytes[176+i*6]=255;
  view.setUint32(248,1,true);bytes[278]=255;
  for(let track=0;track<10;track++){
    bytes[267]=track;
    assert.equal(decodeSave(bytes,schema).fields.music_track,track);
  }
  bytes[267]=10;assert.throws(()=>decodeSave(bytes,schema),/music preference/);
  bytes[267]=9;view.setUint16(4,13,true);
  assert.throws(()=>decodeSave(bytes,schema),/music preference/);
  bytes[267]=2;assert.equal(decodeSave(bytes,schema).fields.music_track,2);
});

test('compiled v12 schema still reads v10 variable save with eighty extra placements',()=>{
  assert.equal(schema.version,19);
  assert.deepEqual(schema.payloadSizes,[120,3100]);
  const count=80,bytes=new Uint8Array(272+count*6),view=new DataView(bytes.buffer);
  view.setUint32(0,schema.magic,true);view.setUint16(4,10,true);view.setUint32(8,bytes.length-12,true);
  view.setUint32(12+236,1|(1<<23),true);view.setUint8(12+240,23);
  view.setUint32(12+248,3,true);view.setUint8(12+252,1);view.setUint8(12+253,1);
  view.setUint32(12+256,count,true);
  for(let i=0;i<12;i++)view.setUint8(12+164+i*6,255);
  for(let i=0;i<count;i++){
    const offset=272+i*6;
    view.setUint8(offset,i%34);view.setUint8(offset+1,i%2);
    view.setInt16(offset+2,100+i,true);view.setInt16(offset+4,200+i,true);
  }
  const result=decodeSave(bytes,schema);
  assert.equal(result.fields.decor.length,92);
  assert.equal(result.fields.decor_extra_count,80);
  assert.equal(result.fields.decor_owned_extra,3);
  assert.equal(result.fields.background_id,23);
  assert.deepEqual(result.fields.decor[91],{kind:79%34,blocking:1,x:179,y:279});
  view.setUint32(12+256,81,true);
  assert.throws(()=>decodeSave(bytes,schema),/length mismatch/);
  view.setUint32(12+256,80,true);view.setUint8(272,34);
  assert.throws(()=>decodeSave(bytes,schema),/Invalid extended decor/);
  view.setUint8(272,0);view.setUint8(12+252,2);
  assert.throws(()=>decodeSave(bytes,schema),/music_enabled/);
});

function current() {
  const c=companionDefaults();
  c.clock_mode=2;c.initialized=1;c.request_day=123;c.seen_achievements=0xfffff;
  c.requests=[{kind:1,target:255,progress:2,state:0},{kind:4,target:35,progress:1,state:1}];
  c.treat_counts[11]=255;c.gift_counts[35]=8;c.play_counts[2]=9;
  c.favorite_treat=11;c.favorite_gift=35;c.favorite_play=2;
  c.diary=[{day:123,kind:2,value:35,pinned:1,calendar:0}];c.diary_count=1;
  return {backgrounds_owned:1,background_id:0,border_id:255,brightness:50,companion:c,
    decor:Array.from({length:13},(_,i)=>({kind:i,blocking:i%3,x:100+i,y:200+i})),decor_extra_count:1};
}

test('v15 companion preferences requests pins and v15 decor tail roundtrip exactly',()=>{
  const fields=current(),bytes=encodeSave(fields,schema),result=decodeSave(bytes,schema);
  assert.equal(bytes.length,3118);
  assert.deepEqual(result.fields.companion,fields.companion);
  assert.deepEqual(result.fields.decor,fields.decor);
  assert.deepEqual(encodeSave(result.fields,schema,bytes),bytes);
  const wrapped=new Uint8Array(bytes.length+20);wrapped.set(bytes,8);
  assert.deepEqual(decodeSave(wrapped.subarray(8,8+bytes.length),schema).fields.companion,fields.companion);
  const native=bytes.slice(),name=schema.fields.find(f=>f.name==='name').offset;
  native[12+name+1]=255;
  new DataView(native.buffer).setUint32(24,0xdeadbeef,true);
  const modified=encodeSave({companion:fields.companion},schema,native);
  assert.deepEqual(modified.subarray(12,280),native.subarray(12,280));
});

test('USB chime preference roundtrips both values and old v14 padding always migrates enabled',()=>{
  assert.equal(schema.fields.length,55);
  for(const enabled of [0,1]) {
    const bytes=encodeSave({...current(),usb_sounds:enabled},schema);
    assert.equal(bytes[279],enabled);
    assert.equal(decodeSave(bytes,schema).fields.usb_sounds,enabled);
    assert.deepEqual(encodeSave(decodeSave(bytes,schema).fields,schema,bytes),bytes);
    bytes[279]=2;
    assert.throws(()=>decodeSave(bytes,schema),/usb_sounds/);
  }
  assert.throws(()=>encodeSave({...current(),usb_sounds:2},schema),/usb_sounds/);
  for(const padding of [0,1,2,255]) {
    const bytes=encodeSave(current(),schema).slice(0,280),view=new DataView(bytes.buffer);
    view.setUint16(4,14,true);view.setUint32(8,268,true);view.setUint32(268,0,true);
    bytes[279]=padding;
    const old=decodeSave(bytes,schema);
    assert.equal(old.fields.usb_sounds,1);
    assert.equal(encodeSave(old.fields,schema,bytes)[279],1);
  }
  const invalid=structuredClone(schema);
  invalid.fields=invalid.fields.filter(f=>f.name!=='usb_sounds');
  assert.throws(()=>validateSchema(invalid),/companion layout/);
});

test('maximum v14 enclosure migrates without losing any of its 3368 extra placements',()=>{
  const count=3368,bytes=new Uint8Array(12+268+count*6),view=new DataView(bytes.buffer);
  view.setUint32(0,schema.magic,true);view.setUint16(4,14,true);
  view.setUint32(8,bytes.length-12,true);view.setUint32(248,1,true);
  bytes[278]=255;bytes[140]=50;view.setUint32(268,count,true);
  for(let i=0;i<12;i++)bytes[176+i*6]=255;
  for(let i=0;i<count;i++) {
    const offset=280+i*6;bytes[offset]=i%68;bytes[offset+1]=i%3;
    view.setUint16(offset+2,i%481,true);view.setUint16(offset+4,(i*7)%481,true);
  }
  const old=decodeSave(bytes,schema),migrated=encodeSave(old.fields,schema,bytes);
  assert.equal(schema.maxPayload,23308);
  assert.equal(migrated.length,12+3100+count*6);
  const current=decodeSave(migrated,schema);
  assert.deepEqual(current.fields.decor,old.fields.decor);
  assert.deepEqual(migrated.subarray(3112),bytes.subarray(280));
  assert.deepEqual(encodeSave(current.fields,schema,migrated),migrated);
  const oversized=new Uint8Array(migrated.length+6);oversized.set(migrated);
  const oversizedView=new DataView(oversized.buffer);
  oversizedView.setUint32(8,oversized.length-12,true);oversizedView.setUint32(268,count+1,true);
  assert.throws(()=>decodeSave(oversized,schema));
  assert.throws(()=>encodeSave({...current.fields,decor_extra_count:count+1},schema),/decor count/);
  const legacyTooLarge=new Uint8Array(bytes.length+6);legacyTooLarge.set(bytes);
  const legacyView=new DataView(legacyTooLarge.buffer);
  legacyView.setUint32(8,legacyTooLarge.length-12,true);legacyView.setUint32(268,count+1,true);
  assert.throws(()=>decodeSave(legacyTooLarge,schema),/length mismatch/);
});

test('v15 rejects companion enums counts and invalid pins, ignoring reserved and unused slots',()=>{
  const bytes=encodeSave(current(),schema);
  for(const [offset,value] of [[4,0x10],[8,5],[9,0],[10,3],[11,1],[13,36],[14,2],[15,3],
    [27,2],[63,2],[66,2],
    [67,12],[68,36],[69,3],[70,65],[71,3],[72,2],[73,5],[74,3],[75,2],
    [84,9],[85,36],[86,2],[87,2]]) {
    const invalid=bytes.slice();invalid[280+offset]=value;
    if(offset===4)invalid[287]=0x10;
    assert.throws(()=>decodeSave(invalid,schema),/companion/,`offset ${offset}`);
  }
  const c=current();c.companion.diary=Array.from({length:9},()=>({day:1,kind:1,value:0,pinned:1,calendar:0}));
  c.companion.diary_count=9;assert.throws(()=>encodeSave(c,schema),/companion/);
  const bad=structuredClone(schema);bad.companion.diary.offset=79;
  assert.throws(()=>validateSchema(bad),/companion/);
  for(const offset of [76,77,78,79,88,591]) {
    const ignored=bytes.slice();ignored[280+offset]=255;
    assert.equal(decodeSave(ignored,schema).fields.companion.diary.length,1);
  }
});

test('oldest v2 prefix encodes current defaults without losing the native gift',()=>{
  const bytes=new Uint8Array(132),view=new DataView(bytes.buffer);
  view.setUint32(0,schema.magic,true);view.setUint16(4,2,true);view.setUint32(8,120,true);
  bytes[50]=17;bytes[52]=1;
  const migrated=encodeSave(decodeSave(bytes,schema).fields,schema,bytes);
  const fields=decodeSave(migrated,schema).fields;
  assert.equal(fields.silk,20);assert.equal(fields.brightness,50);assert.equal(fields.gifts_owned,1<<17);
  assert.equal(fields.music_enabled,1);assert.equal(fields.screen_timeout,3);
  assert.equal(migrated[50],17);assert.equal(migrated[52],1);
  assert.deepEqual(fields.companion,companionDefaults());
});

test('all v2-v14 migrations default companion and retain historical prefix/tail bounds',()=>{
  for(const version of [2,3,4,5,6,7,8,9,10,11,12,13,14]) {
    const size=({2:132,3:132,4:132,5:136,6:152,7:160,8:236,9:248,10:260,11:260,12:268,13:268,14:268})[version];
    const count=version>=10?1:0,bytes=new Uint8Array(12+size+count*6),view=new DataView(bytes.buffer);
    view.setUint32(0,schema.magic,true);view.setUint16(4,version,true);view.setUint32(8,bytes.length-12,true);
    view.setUint32(24,0xdeadbeef,true);
    if(version>=9)bytes[248]=1;
    if(version>=12)bytes[278]=255;
    if(version>=10) {
      view.setUint32(268,count,true);bytes[12+size]=1;bytes[12+size+2]=123;bytes[12+size+4]=234;
    }
    const old=decodeSave(bytes,schema);
    assert.equal(old.fields.usb_sounds,1);
    assert.deepEqual(old.fields.companion,companionDefaults());
    const migrated=encodeSave(old.fields,schema,bytes);
    assert.equal(new DataView(migrated.buffer).getUint32(24,true),0xdeadbeef);
    assert.deepEqual(decodeSave(migrated,schema).fields.companion,companionDefaults());
    if(version>=10) {
      const expected=bytes.slice(12,12+size);
      if(version===10)expected[254]=100;
      if(version>=12)expected[267]=1;
      assert.deepEqual(migrated.subarray(12,12+size),expected);
      assert.equal(decodeSave(migrated,schema).fields.decor[12].x,123);
    }
  }
});

test('music preferences occupy old padding without moving the variable decor tail',()=>{
  const bytes=new Uint8Array(272),view=new DataView(bytes.buffer);
  view.setUint32(0,schema.magic,true);view.setUint16(4,11,true);view.setUint32(8,260,true);
  bytes[248]=1;bytes[266]=35;bytes[267]=2;
  const saved=decodeSave(bytes,schema);
  assert.equal(saved.fields.music_volume,35);assert.equal(saved.fields.music_track,2);
  bytes[266]=101;assert.throws(()=>decodeSave(bytes,schema),/music preference/);
  bytes[266]=35;bytes[267]=3;assert.throws(()=>decodeSave(bytes,schema),/music preference/);
  view.setUint16(4,10,true);bytes[266]=255;bytes[267]=255;
  const migrated=decodeSave(bytes,schema);
  assert.equal(migrated.fields.music_volume,100);assert.equal(migrated.fields.music_track,0);
});
