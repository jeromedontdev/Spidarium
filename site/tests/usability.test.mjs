import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';

function menu() {
  return Object.assign(Object.create(DeviceGame.prototype),{
    state:E.freshState(),cancelTap(){},close(){},panel(){return {};},
    album:{async list(){return [];}},
    refreshHUD(){},list(rows){this.rows=rows;},persist(){return true;},report(){},progress:{blocked:false},
  });
}

test('device and browser menus share gameplay-first ordering and Settings has no screenshot action',async()=>{
  const ui=menu();ui.open('Menu');
  const order=['Decor','Treats','Backgrounds','Borders','Your Spider','Today','Training',
    'Calendar','Achievements','Prey Species','Diary','Generations','Photo Album','Help Guide','Settings'];
  assert.deepEqual(ui.rows.map(row=>row.label),order);
  const native=await readFile(new URL('../../main/game.c',import.meta.url),'utf8');
  const root=native.slice(native.indexOf('case MENU_MAIN: {'),native.indexOf('case MENU_ALBUM: {'));
  assert.deepEqual([...root.matchAll(/items\[\d+\] = \(ui_menu_item_t\)\{ "([^"]+)"/g)].map(m=>m[1]),order);
  ui.open('Settings');assert.ok(ui.rows.every(row=>row.label!=='Save Screenshot'));
});

test('Display controls gesture separately from manual dimming with dedicated icons',()=>{
  const ui=menu();ui.state.lights_dimmed=1;ui.open('Display');
  const gesture=ui.rows.find(row=>row.label==='Double-Tap Lights');
  assert.equal(gesture.selected,true);gesture.click();
  assert.equal(ui.state.life.light_gesture_disabled,1);
  assert.equal(ui.state.lights_dimmed,1);
  ui.rows.find(row=>row.label==='Dim Enclosure').click();
  assert.equal(ui.state.lights_dimmed,0);
  assert.equal(ui.state.life.light_gesture_disabled,1);
  assert.equal(new Set(ui.rows.map(row=>row.icon)).size,ui.rows.length);
});

test('photo deletion requires confirmation, Cancel keeps the image, and failures stay explicit',async t=>{
  globalThis.document={createElement(){return {style:{}};}};
  t.after(()=>{delete globalThis.document;});
  const photo={id:4},calls=[],ui=menu();
  Object.assign(ui,{
    panel(){return {panel:{append(){}}};},
    button(parent,label,x,y,w,h,action){this.actions??={};this.actions[label]=action;},
    viewPhoto(value){calls.push(['view',value.id]);},
    report(message,error){calls.push(['report',message,error]);},
    album:{async remove(){throw new Error('transaction rejected');}},
  });
  ui.confirmDeletePhoto(photo);ui.actions.Cancel();
  assert.deepEqual(calls,[['view',4]]);
  calls.length=0;ui.confirmDeletePhoto(photo);
  await ui.actions['Confirm Delete']();
  assert.deepEqual(calls,[['report','Photo deletion failed: transaction rejected',true]]);
  assert.equal(ui.modal.name,'Photo Delete');
  let commit;
  ui.album.remove=id=>{calls.push(['remove',id]);return new Promise(resolve=>{commit=resolve;});};
  const pending=ui.actions['Confirm Delete']();
  await ui.actions['Confirm Delete']();
  assert.deepEqual(calls.at(-1),['remove',4]);
  assert.equal(calls.filter(call=>call[0]==='remove').length,1);
  ui.open=name=>calls.push(['open',name]);
  commit(4);await pending;
  assert.deepEqual(calls.slice(-2),[['report','Photo deleted. Album slot available.',undefined],['open','Photo Storage']]);
});

test('completed deletion does not replace a newer photo confirmation',async t=>{
  globalThis.document={createElement(){return {style:{}};}};
  t.after(()=>{delete globalThis.document;});
  const ui=menu();let commit,confirm,navigated=false;
  Object.assign(ui,{
    panel(){return {panel:{append(){}}};},
    button(parent,label,x,y,w,h,action){if(label==='Confirm Delete')confirm=action;},
    album:{remove(){return new Promise(resolve=>{commit=resolve;});}},
  });
  ui.confirmDeletePhoto({id:1});
  const pending=confirm();
  ui.confirmDeletePhoto({id:2});const current=ui.modal;
  ui.open=()=>{navigated=true;};
  commit();await pending;
  assert.equal(ui.modal,current);assert.equal(navigated,false);
});

test('closing a photo suppresses stale image errors without hiding active decode failures',t=>{
  globalThis.document={createElement(){return {style:{}};}};
  t.after(()=>{delete globalThis.document;});
  const ui=menu(),images=[],errors=[];
  Object.assign(ui,{
    panel(){return {panel:{append(image){images.push(image);}}};},
    button(){},report(message,error){errors.push([message,error]);},
    close(){if(this.photoURL)URL.revokeObjectURL(this.photoURL);this.photoURL=null;this.modal=null;},
  });
  const photo={blob:new Blob(['png']),created:0};
  ui.viewPhoto(photo);const stale=images.at(-1);
  ui.viewPhoto(photo);const current=images.at(-1);
  stale.onerror();assert.deepEqual(errors,[]);
  current.onerror();assert.deepEqual(errors,[['Stored photo could not be displayed.',true]]);
  ui.close();
});

test('Back retains root, Settings, catalogue and nested menu pages',()=>{
  const ui=menu();
  ui.close=DeviceGame.prototype.close;
  ui.panel=()=>({overlay:{remove(){}}});
  for(const [parent,page,child] of [
    ['Menu',3,'Settings'],['Settings',1,'Sound'],['Sound',1,'Soundtrack'],
    ['Menu',2,'Photo Album'],['Menu',3,'Help Guide'],['Menu',1,'Decor'],
  ]){
    ui.open(parent,page);ui.open(child);ui.back(child);
    assert.equal(ui.modal.name,parent);
    assert.equal(ui.modal.page,page,`${parent} page`);
  }
  ui.open('Calendar',1);ui.open('Calendar');
  assert.equal(ui.modal.page,1);
  ui.open('Photo Album');ui.open('Generations');ui.back('Generations');
  assert.equal(ui.modal.name,'Menu');
  ui.open('Menu',2);ui.open('Generations');ui.back('Generations');
  assert.equal(ui.modal.name,'Menu');assert.equal(ui.modal.page,2);
});

test('Photo Album contains controls; Photo Storage contains only photos and Generations stays separate',async()=>{
  const ui=menu();
  ui.album={async list(){return [{name:'Photo',created:0}];}};
  ui.panel=()=>({overlay:{remove(){}}});
  ui.open('Photo Album');await new Promise(resolve=>setImmediate(resolve));
  assert.deepEqual(ui.rows.map(row=>row.label),['Take Photo','Storage Status','Photo Storage']);
  assert.equal(new Set(ui.rows.map(row=>row.icon)).size,3);
  ui.rows[2].click();await new Promise(resolve=>setImmediate(resolve));
  assert.equal(ui.modal.name,'Photo Storage');
  assert.deepEqual(ui.rows.map(row=>row.label),['Photo']);
  ui.back('Photo Storage');
  assert.equal(ui.modal.name,'Photo Album');
});

test('asynchronous photo loading retains the requested page and clamps after deletion',async()=>{
  const ui=menu();
  ui.panel=()=>({overlay:{remove(){}}});
  ui.list=function(rows,page){this.rows=rows;this.modal.page=page;};
  const photos=Array.from({length:8},(_,i)=>({name:`Photo ${i+1}`,created:0}));
  ui.album={async list(){return photos;}};
  ui.open('Photo Storage',1);await new Promise(resolve=>setImmediate(resolve));
  assert.equal(ui.modal.page,1);
  assert.equal(ui.rows.at(-1).label,'Photo 8');
  photos.splice(3);
  ui.open('Photo Storage',2);await new Promise(resolve=>setImmediate(resolve));
  assert.equal(ui.modal.page,0);
});

test('leaving a loading photo list does not replace its remembered page with the spinner page',async()=>{
  const ui=menu();const pending=[];
  ui.close=DeviceGame.prototype.close;
  ui.panel=()=>({overlay:{remove(){}}});
  ui.list=function(rows,page){this.rows=rows;this.modal.page=page;};
  ui.album={list(){return new Promise(done=>{pending.push(done);});}};
  ui.open('Photo Storage',2);ui.back('Photo Storage');
  assert.equal(ui.menuPages.get('Photo Storage'),2);
  pending[0]([]);await new Promise(done=>setImmediate(done));
  assert.equal(ui.modal.name,'Photo Album');
  assert.deepEqual(ui.rows.map(row=>row.label),['Take Photo','Storage Status','Photo Storage']);
  pending[1]([]);await new Promise(done=>setImmediate(done));
});

test('Take Photo returns to the album controls after actual capture completion',async()=>{
  const ui=menu();ui.panel=()=>({overlay:{remove(){}}});
  const photos=[];ui.draw=()=>{};ui.report=()=>{};
  ui.album={async list(){return photos;},async capture(){photos.push({name:'Photo',created:0});}};
  ui.open('Photo Album');await new Promise(done=>setImmediate(done));
  await ui.rows[0].click();await new Promise(done=>setImmediate(done));
  assert.equal(ui.modal.name,'Photo Album');
  assert.equal(ui.rows[0].enabled,true);
  assert.match(ui.rows[1].detail,/1\/8/);
  assert.deepEqual(ui.rows.map(row=>row.label),['Take Photo','Storage Status','Photo Storage']);
});

test('album controls handle empty, full and unavailable storage without placing controls in the photo list',async()=>{
  const ui=menu();ui.panel=()=>({overlay:{remove(){}}});
  let photos=[];
  ui.album={async list(){return photos;}};
  ui.open('Photo Album');await new Promise(done=>setImmediate(done));
  assert.equal(ui.rows[0].enabled,true);
  assert.match(ui.rows[1].detail,/0\/8/);
  ui.open('Photo Storage');await new Promise(done=>setImmediate(done));
  assert.deepEqual(ui.rows.map(row=>row.label),['No Saved Photos']);
  photos=Array.from({length:8},(_,i)=>({name:`Photo ${i}`,created:0}));
  ui.open('Photo Album');await new Promise(done=>setImmediate(done));
  assert.equal(ui.rows[0].enabled,false);
  assert.match(ui.rows[1].detail,/8\/8/);
  ui.album.list=async()=>{throw Error('blocked');};
  let failure;ui.report=(message,error)=>{failure={message,error};};
  ui.open('Photo Album');await new Promise(done=>setImmediate(done));
  assert.equal(ui.rows[0].enabled,false);
  assert.match(ui.rows[1].detail,/Storage Unavailable: blocked/);
  assert.equal(failure.error,true);
});
