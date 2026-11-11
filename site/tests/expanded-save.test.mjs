import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { decodeSave } from '../lib/save.js';

const schema=JSON.parse(await readFile(new URL('../save-schema.json',import.meta.url)));

test('compiled v10 variable save preserves eighty extra placements and high catalogue ownership',()=>{
  assert.equal(schema.version,10);
  assert.deepEqual(schema.payloadSizes,[120,260]);
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
