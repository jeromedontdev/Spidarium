import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {DeviceGame} from '../lib/device-ui.js';
import * as E from '../lib/device-engine.js';

test('all five shops request six-tile grids while navigation/settings remain lists',()=>{
  let actual;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:E.freshState(),close(){},panel(){return {};},refreshHUD(){},persist(){},
    list(rows,page,grid){actual={rows,page,grid};},report(){},
  });
  for(const name of ['Treats','Backgrounds','Borders','Decor','Gifts']){
    ui.open(name);assert.equal(actual.grid,true,name);
  }
  for(const name of ['Menu','Settings','Sound','Battery']){
    ui.open(name);assert.equal(actual.grid,false,name);
  }
  ui.state.silk=10000;ui.open('Borders',8);
  actual.rows.at(-1).click();
  assert.equal(actual.page,8);
  assert.equal(ui.state.border_id,47);
});

test('gift tile previews the full effect before any purchase or placement',()=>{
  const s=E.freshState();s.level=100;s.silk=10000;
  let rows,chosen=null,back;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:s,close(){},panel(title,callback){back=callback;return {};},
    refreshHUD(){},persist(){},list(items){rows=items;},
    chooseGift(id){chosen=id;},
  });
  ui.open('Gifts',5);
  rows.at(-1).click();
  assert.equal(s.silk,10000);assert.equal(chosen,null);
  assert.equal(ui.modal.name,'Gift Preview');
  assert.match(rows[0].detail,/balanced bonuses \+15%/);
  rows[0].click();assert.equal(chosen,35);
  back();assert.equal(ui.modal.name,'Gifts');assert.equal(ui.modal.page,5);
});

function node(tag='div'){
  return {tag,children:[],attributes:{},classList:{values:[],add(...v){this.values.push(...v);}},
    append(...values){this.children.push(...values);},replaceChildren(){this.children=[];},
    setAttribute(k,v){this.attributes[k]=v;}};
}

test('grid pagination fits six non-overlapping targets and maps final IDs exactly',t=>{
  globalThis.document={createElement:node};t.after(()=>{delete globalThis.document;});
  for(const count of [12,36,48,49,68]){
    const calls=[],picked=[];
    const ui={modal:{name:'Shop',panel:node()},open(name,page){this.page=page;},
      button(parent,label,x,y,w,h,click){
        const button=node('button');parent.append(button);
        calls.push({label,x,y,w,h,click,button});return button;
      }};
    const rows=Array.from({length:count},(_,id)=>({
      label:`Item ${id}`,detail:'80 Silk / Buy\nComplete effect description',
      click:()=>picked.push(id),icon:'ui_decor',selected:id===count-1,
    }));
    DeviceGame.prototype.list.call(ui,rows,0,true);
    const tiles=calls.filter(c=>c.button.classList.values.includes('d-shop-tile'));
    assert.equal(tiles.length,6);
    for(let i=0;i<6;i++){
      assert.deepEqual([tiles[i].x,tiles[i].y,tiles[i].w,tiles[i].h],
        [12+i%3*128,76+Math.floor(i/3)*124,120,116]);
      assert.ok(tiles[i].x+tiles[i].w<=388);
      assert.ok(tiles[i].y+tiles[i].h<332);
      assert.match(tiles[i].button.attributes['aria-label'],/Complete effect/);
    }
    assert.equal(calls.at(-2).button.disabled,true);
    calls.at(-1).click();assert.equal(ui.page,1);
    calls.length=0;
    const last=Math.ceil(count/6)-1;
    DeviceGame.prototype.list.call(ui,rows,last,true);
    const final=calls.filter(c=>c.button.classList.values.includes('d-shop-tile'));
    assert.equal(final.length,count-last*6);
    final.at(-1).click();assert.deepEqual(picked,[count-1]);
    assert.equal(calls.at(-1).button.disabled,true);
  }
});

test('fold is inside the rounded screen with a generous transparent target; hero is a beetle',async()=>{
  const css=await readFile(new URL('../device.css',import.meta.url),'utf8');
  const html=await readFile(new URL('../index.html',import.meta.url),'utf8');
  const app=await readFile(new URL('../app.js',import.meta.url),'utf8');
  assert.match(css,/\.game-screen \.d-ear \{[^}]*left:12px; top:12px; width:56px; height:56px/);
  assert.match(css,/\.d-ear::after \{[^}]*polygon\(0 100%,100% 0,36% 36%\)/);
  assert.match(html,/class="hero-beetle"[^>]+prey_beetle_walk_0\.png/);
  assert.doesNotMatch(html,/id="hero-spider"/);
  assert.doesNotMatch(app,/\['hero-spider'/);
});
