import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {DeviceGame} from '../lib/device-ui.js';
import * as E from '../lib/device-engine.js';

test('all catalogues use six tiles and navigation/settings use four-tile grids',()=>{
  let actual;
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:E.freshState(),close(){},panel(){return {};},refreshHUD(){},persist(){},
    list(rows,page,grid){actual={rows,page,grid};},report(){},
  });
  for(const name of ['Treats','Backgrounds','Borders','Decor','Gifts','Achievements','Prey Species']){
    ui.open(name);assert.equal(actual.grid,3,name);
  }
  for(const name of ['Menu','Settings','Sound','Battery']){
    ui.open(name);assert.equal(actual.grid,2,name);
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
  return {tag,children:[],attributes:{},listeners:{},classList:{values:[],add(...v){this.values.push(...v);}},
    append(...values){this.children.push(...values);},replaceChildren(){this.children=[];},
    addEventListener(name,callback){this.listeners[name]=callback;},
    querySelector(selector){return this.children.find(child=>child.className===selector.slice(1));},
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
    DeviceGame.prototype.list.call(ui,rows,0,3);
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
    DeviceGame.prototype.list.call(ui,rows,last,3);
    const final=calls.filter(c=>c.button.classList.values.includes('d-shop-tile'));
    assert.equal(final.length,count-last*6);
    assert.equal(final.at(-1).button.children.find(child=>child.className==='row-marker').textContent,'✓');
    final.at(-1).click();assert.deepEqual(picked,[count-1]);
    assert.equal(calls.at(-1).button.disabled,true);
  }
});

test('four-tile settings keep sliders, switches and final-page routing usable',t=>{
  globalThis.document={createElement:node};t.after(()=>{delete globalThis.document;});
  const calls=[];let gain;
  const ui={modal:{name:'Sound',panel:node()},open(name,page){this.page=page;},
    button(parent,label,x,y,w,h,click){
      const button=node('button');parent.append(button);
      calls.push({label,x,y,w,h,click,button});return button;
    }};
  const rows=[
    {label:'Volume',detail:'70%',icon:'ui_volume',slider:{value:70,min:0,max:100,step:5,change:value=>{gain=value;}}},
    {label:'Sound',detail:'On',checkbox:true,selected:true,click(){}},
    {label:'Background Music',detail:'On',checkbox:true,selected:true,click(){}},
    {label:'Music Volume',detail:'35%',icon:'ui_volume',slider:{value:35,min:0,max:100,step:5,change(){}}},
    {label:'Soundtrack',detail:'Moonlit Hollow',submenu:true,click(){}},
  ];
  DeviceGame.prototype.list.call(ui,rows,0,2);
  const tiles=calls.filter(c=>c.button.classList.values.includes('d-menu-tile'));
  assert.equal(tiles.length,4);
  for(let i=0;i<4;i++)assert.deepEqual([tiles[i].x,tiles[i].y,tiles[i].w,tiles[i].h],
    [12+i%2*192,76+Math.floor(i/2)*124,184,116]);
  const input=tiles[0].button.children.find(child=>child.type==='range');
  input.value='25';input.listeners.input();
  assert.equal(gain,25);
  assert.equal(tiles[0].button.attributes['aria-label'],'Volume: 25%');
  assert.equal(tiles[1].button.attributes['role'],'checkbox');
  assert.equal(tiles[1].button.attributes['aria-checked'],'true');
  calls.length=0;DeviceGame.prototype.list.call(ui,rows,1,2);
  assert.equal(calls.filter(c=>c.button.classList.values.includes('d-menu-tile')).length,1);
  assert.equal(calls[0].button.children.some(child=>child.className==='row-marker'),false);
  assert.equal(calls.at(-1).button.disabled,true);
});

test('sliders capture held drags, respect rotation, and release on cancellation',t=>{
  globalThis.document={createElement:node};t.after(()=>{delete globalThis.document;});
  for(const rotation of [0,1,2,3]){
    const panel=node(),values=[];
    const ui={rotation,modal:{name:'Sound',panel},open(){},
      button(parent){const button=node('button');parent.append(button);return button;}};
    DeviceGame.prototype.list.call(ui,[{label:'Volume',detail:'50%',
      slider:{value:50,min:0,max:100,step:5,change:value=>values.push(value)}}],0,2);
    const input=panel.children[0].children.find(child=>child.type==='range');
    let captured=null;
    input.offsetWidth=156;
    input.getBoundingClientRect=()=>({left:10,top:20,width:rotation%2?20:156,height:rotation%2?156:20});
    input.focus=()=>{};
    input.setPointerCapture=id=>{captured=id;};
    input.hasPointerCapture=id=>captured===id;
    input.releasePointerCapture=()=>{captured=null;};
    input.dispatchEvent=event=>input.listeners[event.type](event);
    const point=fraction=>{
      let position=8+140*fraction;
      if(rotation===2||rotation===3)position=156-position;
      return {pointerId:1,isPrimary:true,button:0,clientX:10+(rotation%2?10:position),
        clientY:20+(rotation%2?position:10),preventDefault(){},stopPropagation(){}};
    };
    input.listeners.pointerdown({...point(.1),button:2});
    input.listeners.pointerdown({...point(.1),isPrimary:false});
    assert.equal(captured,null);assert.deepEqual(values,[]);
    input.listeners.pointerdown(point(.2));assert.equal(captured,1);
    input.listeners.pointermove(point(.8));
    assert.deepEqual(values,[20,80]);
    assert.equal(panel.children[0].attributes['aria-label'],'Volume: 80%');
    input.listeners.pointerup(point(.65));assert.equal(captured,null);
    assert.equal(values.at(-1),65);
    input.listeners.pointermove(point(.1));assert.equal(values.at(-1),65);
    input.listeners.pointerdown(point(.4));input.listeners.pointercancel(point(.4));
    assert.equal(captured,null);
    input.value='30';input.listeners.input();assert.equal(values.at(-1),30);
  }
});

test('fold is inside the rounded screen; hero preserves spider with one small bug',async()=>{
  const css=await readFile(new URL('../device.css',import.meta.url),'utf8');
  const html=await readFile(new URL('../index.html',import.meta.url),'utf8');
  const app=await readFile(new URL('../app.js',import.meta.url),'utf8');
  assert.match(css,/\.game-screen \.d-ear \{[^}]*left:12px; top:12px; width:56px; height:56px/);
  assert.match(css,/\.d-ear::after \{[^}]*polygon\(0 100%,100% 0,36% 36%\)/);
  assert.match(html,/class="hero-prey"[^>]+prey_beetle_walk_0\.png/);
  assert.match(html,/id="hero-spider"/);
  assert.match(app,/\['hero-spider'/);
  assert.doesNotMatch(html,/class="hero-beetle"/);
  const style=await readFile(new URL('../style.css',import.meta.url),'utf8');
  assert.match(style,/\.hero-moss \{ background:#354a39 url\("\.\.\/assets\/background_moss_0\.png"\)/);
});
