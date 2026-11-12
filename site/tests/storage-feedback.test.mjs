import test from 'node:test';
import assert from 'node:assert/strict';
import { DeviceGame } from '../lib/device-ui.js';
import { freshState, restoreSim } from '../lib/device-engine.js';

test('storage feedback appears inside the game and survives menu overlays', t => {
  t.mock.timers.enable({ apis:['setTimeout'] });
  const notice={setAttribute(){},classList:{toggle(){}},style:{},hidden:true};
  globalThis.document={createElement:()=>notice};
  t.after(()=>delete globalThis.document);
  const status={classList:{toggle(){}}},children=[];
  const ui={root:{querySelector:()=>status},screen:{append:node=>children.push(node)}};
  DeviceGame.prototype.report.call(ui,'Backup Saved and Verified');
  assert.equal(children.length,1);assert.equal(notice.hidden,false);
  assert.equal(notice.textContent,status.textContent);
  assert.equal(notice.style.bottom,'');
  t.mock.timers.tick(6000);assert.equal(notice.hidden,true);
  DeviceGame.prototype.report.call(ui,'Card write failed',true);
  assert.equal(children.length,1);assert.equal(notice.hidden,false);
  t.mock.timers.tick(6000);assert.equal(notice.hidden,false);
  t.mock.timers.tick(4000);assert.equal(notice.hidden,true);
  ui.placement={};
  DeviceGame.prototype.report.call(ui,'Placement failed',true);
  assert.equal(notice.style.bottom,'112px','keep errors clear of Done/Cancel');
});

test('browser backup visibly reports download request and click failure', t => {
  const notices=[],link={click(){},remove(){this.removed=true;}};
  globalThis.document={createElement:()=>link};
  t.after(()=>delete globalThis.document);
  const ui={state:freshState(),root:{append(){}},report:(...args)=>notices.push(args)};
  DeviceGame.prototype.download.call(ui);
  assert.equal(link.removed,true);
  assert.match(notices[0][0],/download requested/);
  link.click=()=>{throw new Error('Downloads blocked');};
  DeviceGame.prototype.download.call(ui);
  assert.deepEqual(notices[1],['Browser backup failed: Downloads blocked',true]);
});

test('restore cancellation and invalid files report explicit outcomes',async()=>{
  const notices=[],input={files:[],value:'old'};
  const ui={root:{querySelector:()=>input},report:(...args)=>notices.push(args)};
  await DeviceGame.prototype.importProgress.call(ui);
  assert.match(notices[0][0],/cancelled/);assert.equal(input.value,'');
  input.files=[{size:2,text:async()=>'{!'}];
  await DeviceGame.prototype.importProgress.call(ui);
  assert.match(notices[1][0],/Browser restore failed/);assert.equal(notices[1][1],true);
});

test('music track and independent gain persist while old saves migrate audibly',()=>{
  const state=freshState();state.music_volume=25;state.music_track=2;
  const restored=restoreSim(JSON.parse(JSON.stringify(state)));
  assert.equal(restored.music_volume,25);assert.equal(restored.music_track,2);
  for(const [key,value] of [['music_volume',101],['music_track',3]])
    assert.throws(()=>restoreSim({...state,[key]:value}),new RegExp(key));
  const old=restoreSim({...state,version:10,music_volume:0,music_track:0});
  assert.equal(old.music_volume,100);assert.equal(old.music_track,0);
});
