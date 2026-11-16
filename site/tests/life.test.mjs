import test from 'node:test';
import assert from 'node:assert/strict';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';
import {readFile} from 'node:fs/promises';
import {encodeSave,decodeSave} from '../lib/save.js';

const START=1704067200000, DAY=86400000;
const fresh=(now=START)=>E.freshState({now:()=>now,random:()=>.5,temperament:0});
const copy=value=>JSON.parse(JSON.stringify(value));

test('scheduled activity uses its dedicated Calendar icon',()=>{
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:fresh(),cancelTap(){},close(){},panel(){return {};},refreshHUD(){},
    list(rows){this.rows=rows;},
  });
  ui.open('Calendar');
  assert.equal(ui.rows.find(row=>row.label==='Activity').icon,'ui_activity');
  assert.equal(ui.rows.find(row=>row.label==='Calendar').icon,'ui_calendar');
});

test('life defaults match ABI, restore old versions without invented progress, and reject malformed nested data',()=>{
  assert.deepEqual(E.lifeDefaults(),{visitor_day:0xffffffff,season_highwater:0,progress:[0,0,0],
    cooldown:0,daynight:1,weather:0,retired_count:0,light_gesture_disabled:0,retired:[]});
  for(let version=1;version<=15;version++){
    const raw=copy(fresh());raw.version=version;delete raw.life;
    if(version<3)delete raw.state;
    assert.deepEqual(E.restoreSim(raw).life,E.lifeDefaults());
  }
  for(const mutate of [
    l=>{l.visitor_day=0;},l=>{l.season_highwater=8096;},l=>{l.cooldown=31;},
    l=>{l.daynight=2;},l=>{l.weather=true;},l=>{l.progress[0]=6;},
    l=>{delete l.progress[1];},l=>{l.retired_count=1;},l=>{l.light_gesture_disabled=2;},
    l=>{l.retired=[{}];l.retired_count=1;},
  ]){
    const raw=copy(fresh());mutate(raw.life);
    assert.throws(()=>E.restoreSim(raw),/life/);
  }
  const raw=copy(fresh());delete raw.life;assert.throws(()=>E.restoreSim(raw),/life/);
});

test('legacy v16 JSON reserved zero becomes enabled gesture; invalid old/new flags fail',()=>{
  const raw=copy(fresh());raw.version=16;
  delete raw.life.light_gesture_disabled;raw.life.reserved=0;
  const original=copy(raw);
  const restored=E.restoreSim(raw);
  assert.equal(restored.life.light_gesture_disabled,0);
  assert.equal(Object.hasOwn(restored.life,'reserved'),false);
  assert.deepEqual(raw,original);
  const missing=copy(raw);delete missing.life.reserved;
  assert.throws(()=>E.restoreSim(missing),/life/);
  raw.life.reserved=1;assert.throws(()=>E.restoreSim(raw),/life/);
  for(const value of [2,-1,true,null,'1']){
    const invalid=copy(fresh());invalid.life.light_gesture_disabled=value;
    assert.throws(()=>E.restoreSim(invalid),/life/);
  }
});

test('tricks require five paid practices, bounded active cooldown and healthy awake energy gate; no Silk or XP grind',()=>{
  const s=fresh(), silk=s.silk,xp=s.xp;
  for(let id=0;id<3;id++){
    for(let n=0;n<5;n++){
      Object.assign(s,{energy:80,mood:50,state:0,sick:0});s.life.cooldown=0;
      assert.equal(E.lifeTrain(s,id),true);
      assert.equal(s.life.progress[id],n+1);assert.equal(s.energy,78);assert.equal(s.mood,51);
      assert.equal(s.life.cooldown,30);
      assert.equal(E.lifeTrain(s,id),false);
    }
    s.life.cooldown=0;
    assert.equal(E.lifeTrain(s,id),false);assert.equal(E.lifePerform(s,id),true);
    assert.equal(s._engine.life_performance.trick,id);
  }
  assert.equal(s.silk,silk);assert.equal(s.xp,xp);
  for(const changes of [{sick:1},{state:6},{state:8},{energy:19}]){
    const bad=Object.assign(fresh(),changes);
    assert.equal(E.lifeTrain(bad,0),false);
    bad.life.progress[0]=5;assert.equal(E.lifePerform(bad,0),false);
  }
  assert.equal(E.lifeTrain(fresh(),-1),false);assert.equal(E.lifePerform(fresh(),3),false);
  const clock=fresh();clock.life.cooldown=30;clock.paused=true;E.tick(clock,100);
  assert.equal(clock.life.cooldown,30);clock.paused=false;
  E.tick(clock,.95);assert.equal(clock.life.cooldown,30);
  E.tick(clock,.05);assert.equal(clock.life.cooldown,29);
  const reboot=E.restoreSim(copy(clock));assert.equal(reboot.life.cooldown,29);
  E.tick(reboot,29);assert.equal(reboot.life.cooldown,0);
  const idle=fresh();E.tick(idle,.95);E.lifeTrain(idle,0);E.tick(idle,.05);
  assert.equal(idle.life.cooldown,30);
});

test('calendar phase, quarter, activity, unreliable RTC and backward reward guards are deterministic',()=>{
  for(const [hour,phase,activity] of [[0,'Night','None'],[5,'Night','None'],[6,'Morning','Stretch'],
    [11,'Morning','Stretch'],[12,'Afternoon','Explore'],[17,'Afternoon','Explore'],
    [18,'Evening','Quiet'],[21,'Evening','Quiet'],[22,'Night','None'],[23,'Night','None']]){
    const c=E.lifeCalendar(START+hour*3600000);
    assert.equal(c.phase,phase);assert.equal(c.activity,activity);
  }
  for(const [month,season] of [[0,'Winter'],[3,'Spring'],[6,'Summer'],[9,'Autumn']]){
    const c=E.lifeCalendar(Date.UTC(2026,month,1));assert.equal(c.season,season);
    assert.equal(c.season_key,2026*4+month/3+1);
  }
  const s=fresh(0);
  for(const now of [0,START-1,NaN,Infinity]){
    assert.equal(E.lifeCalendar(now).reliable,false);assert.equal(E.lifeCalendar(now).season,null);
    assert.equal(E.lifeSeasonClaim(s,now),false);assert.equal(E.lifeVisitorClaim(s,now),false);
  }
  assert.equal(E.lifeSeasonClaim(s,START),true);assert.equal(s.silk,30);
  assert.equal(E.lifeSeasonClaim(s,START),false);
  assert.equal(E.lifeSeasonClaim(s,Date.UTC(2024,3,1)),true);
  assert.equal(E.lifeSeasonClaim(s,START),false);
  const day=Math.ceil(Math.floor(START/DAY)/3)*3, visit=day*DAY+12*3600000;
  assert.equal(E.lifeVisitorAvailable(s,visit-1),false);
  assert.equal(E.lifeVisitorClaim(s,visit),true);assert.equal(s.silk,43);
  assert.equal(E.lifeVisitorClaim(s,visit),false);
  assert.equal(E.lifeVisitorClaim(s,visit-3*DAY),false);
  assert.equal(E.lifeVisitorAvailable(s,day*DAY+18*3600000),false);
  const reboot=E.restoreSim(copy(s));
  assert.equal(E.lifeVisitorClaim(reboot,visit),false);
  assert.equal(E.lifeVisitorClaim(reboot,visit+3*DAY),true);
  assert.equal(E.lifeContext({...s,sick:1},visit).activity,'Rest');
  s.silk=0xffffffff;
  const highwaters=[s.life.visitor_day,s.life.season_highwater];
  assert.equal(E.lifeVisitorClaim(s,visit+3*DAY),false);
  assert.equal(E.lifeSeasonClaim(s,Date.UTC(2025,0,1)),false);
  assert.deepEqual([s.life.visitor_day,s.life.season_highwater],highwaters);
});

test('generation archive ABI swaps only pets, training and companion; cap is strict and confirmation mandatory',()=>{
  const s=fresh();Object.assign(s,{name:'Parent',stage:2,bond:80,colour:4,pupil_colour:3,leg_colour:7,fang_colour:2,
    silk:700,gifts_owned:123,background:'cork',volume:25,muted:1});
  s.gift={used:1,kind:0,variant:0,x:150,y:170};
  s.life.progress=[5,3,1];s.life.light_gesture_disabled=1;E.companionTick(s,START);s.companion.treat_counts[2]=2;
  const shared=()=>copy({silk:s.silk,owned:s.gifts_owned,gift:s.gift,volume:s.volume,muted:s.muted,decor:s.decor,
    backgrounds:s.backgrounds_owned,weather:s.life.weather,daynight:s.life.daynight,
    gesture:s.life.light_gesture_disabled});
  const original=shared();
  assert.equal(E.lifeRetire(s),false);assert.equal(s.life.retired_count,0);
  s.sick=1;assert.equal(E.lifeRetire(s,true),false);s.sick=0;
  assert.equal(E.lifeRetire(s,true),true);
  assert.equal(E.lifeValid(s.life),true);
  assert.equal(s.life.retired[0].core.length,120);assert.equal(s.life.retired[0].daily.length,24);
  assert.equal(s.stage,0);assert.equal(s.name,'');assert.equal(s.bond,0);
  assert.deepEqual([s.hunger,s.mood,s.energy,s.hygiene],[80,80,80,100]);
  assert.deepEqual([s.colour,s.pupil_colour,s.leg_colour,s.fang_colour],[4,3,7,2]);
  assert.deepEqual(s.life.progress,[0,0,0]);assert.equal(s.life.cooldown,30);
  assert.deepEqual(s.companion,E.companionDefaults());assert.deepEqual(shared(),original);
  s.name='Child';s.life.progress[2]=2;assert.equal(E.lifeResume(s,0),false);
  assert.equal(E.lifeResume(s,0,true),true);
  assert.equal(s.name,'Parent');assert.equal(s.bond,80);assert.deepEqual(s.life.progress,[5,3,1]);
  assert.equal(s.companion.treat_counts[2],2);assert.equal(s.life.cooldown,30);assert.deepEqual(shared(),original);
  assert.equal(E.lifeResume(s,0,true),true);assert.equal(s.name,'Child');assert.deepEqual(s.life.progress,[0,0,2]);
  for(let i=1;i<3;i++){s.stage=2;assert.equal(E.lifeRetire(s,true),true);}
  s.stage=2;const archives=copy(s.life.retired);
  assert.equal(E.lifeRetire(s,true),false);assert.deepEqual(s.life.retired,archives);
  assert.equal(E.lifeResume(s,3,true),false);
  const restored=E.restoreSim(copy(s));assert.deepEqual(restored.life,s.life);
  restored.life.retired[0].core[0]=0;assert.notEqual(s.life.retired[0].core[0],0);
  s.name='🕷🕷🕷🕷';s.life.retired_count=0;s.life.retired=[];s.stage=2;
  assert.equal(E.lifeRetire(s,true),false);assert.equal(s.life.retired_count,0);
});

test('archive validator rejects corrupt raw bytes, unterminated identity and invalid pet states',()=>{
  const s=fresh();s.stage=2;E.lifeRetire(s,true);
  for(const mutate of [
    p=>{p.core.pop();},p=>{p.core[0]=101;},p=>{p.core[68]=11;},
    p=>{p.core.fill(65,100,116);},p=>{p.core[116]=6;},
    p=>{p.daily[12]=8;},p=>{p.progress[0]=6;},p=>{p.companion=null;},
  ]){
    const life=copy(s.life);mutate(life.retired[0]);assert.equal(E.lifeValid(life),false);
  }
});

test('browser archives round-trip the exact native v19 raw life ABI',async()=>{
  const schema=JSON.parse(await readFile(new URL('../save-schema.json',import.meta.url)));
  const s=fresh();s.stage=2;s.name='Archive';s.life.progress=[5,4,3];
  E.companionTick(s,START);s.companion.treat_counts[1]=2;
  assert.equal(E.lifeRetire(s,true),true);
  const bytes=encodeSave(s,schema), decoded=decodeSave(bytes,schema);
  assert.equal(decoded.header.version,21);assert.equal(decoded.header.size,3100);
  assert.deepEqual(decoded.fields.life,s.life);
  const offset=12+860+16;
  assert.deepEqual(Array.from(bytes.slice(offset,offset+120)),s.life.retired[0].core);
  assert.deepEqual(Array.from(bytes.slice(offset+120,offset+144)),s.life.retired[0].daily);
  assert.deepEqual(Array.from(bytes.slice(offset+736,offset+739)),[5,4,3]);
});

test('UI life mutations roll back on persistence failure and never report success',()=>{
  const s=fresh(),messages=[];
  const game={state:s,progress:{blocked:false},persist:()=>false,report:(...args)=>messages.push(args),refreshHUD(){}};
  assert.equal(DeviceGame.prototype.lifeAction.call(game,state=>E.lifeTrain(state,0),'Saved!'),false);
  assert.equal(game.state.life.progress[0],0);assert.equal(game.state.energy,80);
  assert.equal(messages.some(([text])=>text==='Saved!'),false);
  game.progress.blocked=true;
  assert.equal(DeviceGame.prototype.lifeAction.call(game,()=>{throw new Error('must not run');}),false);
});

test('photo capture failures surface instead of claiming fallback success',async()=>{
  const messages=[],game={albumBusy:false,draw(){},canvas:{},
    album:{capture:async()=>{throw new Error('quota exceeded');}},state:{name:'Spider'},
    report:(...args)=>messages.push(args)};
  await DeviceGame.prototype.capturePhoto.call(game);
  assert.deepEqual(messages,[['Photo was not saved: quota exceeded',true]]);
  assert.equal(game.albumBusy,false);
  await assert.rejects(new E.PhotoAlbum(null).list(),/IndexedDB/);
  await assert.rejects(E.canvasPNG({toBlob:callback=>callback(null)}),/PNG encoding failed/);
});

test('UI active-time animation and practice cooldown pause in menus and hidden tabs',t=>{
  globalThis.document={hidden:false};globalThis.requestAnimationFrame=()=>{};
  t.after(()=>{delete globalThis.document;delete globalThis.requestAnimationFrame;});
  const s=fresh();s.life.cooldown=30;
  const game={state:s,last:0,accumulator:0,lastSave:0,started:true,hud:false,modal:null,game:null,
    giftKind:null,decorKind:null,decorMoveSlot:null,audio:{tick(){}},
    companionTip(){},gameFrame(){},draw(){},refreshHUD(){},persist(){}};
  for(const key of ['hidden','hud','modal','game']){
    if(key==='hidden')document.hidden=true;else game[key]=true;
    for(let i=0;i<20;i++)DeviceGame.prototype.frame.call(game,game.last+150);
    if(key==='hidden')document.hidden=false;else game[key]=key==='hud'?false:null;
    assert.equal(s.life.cooldown,30,key);assert.equal(s.now_ms,START,key);
  }
  for(let i=0;i<20;i++)DeviceGame.prototype.frame.call(game,game.last+150);
  assert.equal(s.life.cooldown,27);assert.equal(s.now_ms,START+3000);
});

// Minimal transactional IDB fixture: committed records survive a new album instance.
function fakeIndexedDB({failCommit=false}={}) {
  const records=[];let next=1;
  return {records,open(){
    const request={};
    queueMicrotask(()=>{
      request.result={close(){},transaction(){
        const tx={error:null}, pending=[];let aborted=false;
        const schedule=(work)=>{
          const result={};pending.push(1);
          queueMicrotask(()=>{
            if(aborted)return;
            result.result=work();result.onsuccess?.();
            pending.pop();
            if(!pending.length)queueMicrotask(()=>{
              if(aborted)return;
              if(failCommit){tx.error=new Error('quota exceeded');tx.onabort?.();}
              else tx.oncomplete?.();
            });
          });
          return result;
        };
        tx.objectStore=()=>({
          count:()=>schedule(()=>records.length),
          getAll:()=>schedule(()=>records.map(p=>({...p}))),
          get:id=>schedule(()=>records.find(p=>p.id===id)),
          delete:id=>schedule(()=>{
            const index=records.findIndex(p=>p.id===id);
            if(!failCommit && index>=0)records.splice(index,1);
          }),
          add:record=>schedule(()=>{
            const id=next++;if(!failCommit)records.push({...record,id});return id;
          }),
        });
        tx.abort=()=>{aborted=true;queueMicrotask(()=>tx.onabort?.());};return tx;
      }};
      request.onsuccess();
    });
    return request;
  }};
}
test('album stores actual PNG blobs persistently, bounds eight photos and never overwrites',async()=>{
  const db=fakeIndexedDB(),album=new E.PhotoAlbum(db);
  const png=new Blob([Uint8Array.of(137,80,78,71)],{type:'image/png'});
  const canvas={toBlob(callback,type){assert.equal(type,'image/png');callback(png);}};
  for(let i=0;i<8;i++)assert.equal(await album.capture(canvas,'Spider',START+i),i+1);
  const reloaded=await new E.PhotoAlbum(db).list();
  assert.equal(reloaded.length,8);assert.equal(reloaded[0].blob,png);
  await assert.rejects(album.capture(canvas,'Overwrite'),/full/);
  assert.deepEqual((await album.list()).map(p=>p.id),[1,2,3,4,5,6,7,8]);
  await assert.rejects(new E.PhotoAlbum(fakeIndexedDB({failCommit:true})).capture(canvas),/quota/);
});

test('album removes only a selected existing ID, awaits commit, and frees a full slot',async()=>{
  const db=fakeIndexedDB(),album=new E.PhotoAlbum(db);
  const canvas={toBlob(callback){callback(new Blob(['png'],{type:'image/png'}));}};
  for(let i=0;i<8;i++)await album.capture(canvas,'Spider',START+i);
  for(const id of [0,-1,1.5,'1',Number.MAX_SAFE_INTEGER+1])
    await assert.rejects(album.remove(id),/Invalid photo ID/);
  await assert.rejects(album.remove(99),/does not exist/);
  assert.equal((await album.list()).length,8);
  assert.equal(await album.remove(4),4);
  assert.deepEqual((await new E.PhotoAlbum(db).list()).map(p=>p.id),[1,2,3,5,6,7,8]);
  assert.equal(await album.capture(canvas),9);
  await assert.rejects(album.remove(4),/does not exist/);
  const failing=fakeIndexedDB({failCommit:true});
  failing.records.push({id:1});
  await assert.rejects(new E.PhotoAlbum(failing).remove(1),/quota exceeded/);
  assert.deepEqual(failing.records,[{id:1}]);
});
