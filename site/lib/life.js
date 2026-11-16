import { companionDefaults, companionValid } from './companion.js?v=1.1.31';

export const TRICKS = Object.freeze(['Wave', 'Spin', 'Bow']);
export const LIFE_NEVER = 0xffffffff;
const START = 1704067200000, DAY = 86400000;
const int = (v, min, max) => Number.isInteger(v) && v >= min && v <= max;
const copy = value => JSON.parse(JSON.stringify(value));
const array = (v, n, valid) => Array.isArray(v) && v.length === n &&
  Array.from({length:n}, (_, i) => Object.hasOwn(v, i) && valid(v[i])).every(Boolean);

export function lifeDefaults() {
  return {visitor_day:LIFE_NEVER,season_highwater:0,progress:[0,0,0],cooldown:0,
    daynight:1,weather:0,retired_count:0,light_gesture_disabled:0,retired:[]};
}

// Original game_state_t pet prefix and daily block. Shared habitat/settings never swap.
const CORE = [
  ['hunger',0,'u8'],['mood',1,'u8'],['energy',2,'u8'],['hygiene',3,'u8'],
  ['age_ticks',4,'u32'],['meals',8,'u32'],['pets',12,'u32'],['cleans',16,'u32'],
  ['plays',20,'u32'],['xp',24,'u32'],['seed',28,'u32'],
  ['spider_x',32,'i16'],['spider_y',34,'i16'],['heading_deg',36,'i16'],
  ['stage',46,'u8'],['sick',47,'u8'],['level',52,'u32'],['achievements',56,'u32'],
  ['days_lived',60,'u32'],['last_molt_day',64,'u32'],['state',68,'u8'],
  ['taps',72,'u32'],['eat_until_ms',76,'i32'],['blink_window',80,'u32'],
  ['blink_until',84,'u32'],['spider_heading',88,'f32'],['streak',92,'u16'],
  ['best_streak',94,'u16'],['temperament',96,'u8'],['bond',98,'u16'],
  ['colour',116,'u8'],['pupil_colour',117,'u8'],['leg_colour',118,'u8'],['fang_colour',119,'u8'],
];
const DAILY = [['daily_day',0,'u32'],['daily_base_day',4,'u32'],['daily_task_base',8,'u32'],
  ['daily_streak',12,'u8'],['daily_task_done',13,'u8'],['pet_xp_day',16,'u32'],['pet_xp_today',20,'u8']];
const TYPES = {u8:'Uint8',u16:'Uint16',u32:'Uint32',i16:'Int16',i32:'Int32',f32:'Float32'};
function encodeBlock(s, size, fields) {
  const bytes = new Uint8Array(size), view = new DataView(bytes.buffer);
  for (const [key, offset, type] of fields) view[`set${TYPES[type]}`](offset, s[key] ?? 0, true);
  return bytes;
}
function decodeBlock(bytes, fields) {
  const view = new DataView(Uint8Array.from(bytes).buffer);
  return Object.fromEntries(fields.map(([key, offset, type]) => [key,view[`get${TYPES[type]}`](offset,true)]));
}
export function encodeLifePet(s) {
  const name=new TextEncoder().encode(s.name ?? '');
  if(name.length>15 || name.includes(0))throw new Error('Spider name must fit 15 UTF-8 bytes before archiving');
  const core = encodeBlock(s,120,CORE);
  core.set(name,100);
  return {core:Array.from(core),daily:Array.from(encodeBlock(s,24,DAILY)),
    companion:copy(s.companion),progress:[...s.life.progress],reserved:0};
}
export function decodeLifePet(pet) {
  return {...decodeBlock(pet.core,CORE),...decodeBlock(pet.daily,DAILY),
    name:new TextDecoder().decode(Uint8Array.from(pet.core.slice(100,116))).split('\0')[0],
    companion:copy(pet.companion)};
}
function validPet(pet) {
  if (!pet || !array(pet.core,120,v=>int(v,0,255)) || !array(pet.daily,24,v=>int(v,0,255)) ||
      !companionValid(pet.companion) || !array(pet.progress,3,v=>int(v,0,5)) || pet.reserved !== 0) return false;
  const s = decodeLifePet(pet);
  return ['hunger','mood','energy','hygiene'].every(k=>s[k]<=100) && s.stage<=2 && s.sick<=1 &&
    (s.temperament<4 || s.temperament===255) && s.bond<=100 &&
    s.state<=9 && pet.core.slice(100,116).includes(0) && (s.colour<6 || s.colour===255) &&
    ['pupil_colour','leg_colour','fang_colour'].every(k=>s[k]<9 || s[k]===255) &&
    int(s.spider_x,0,480) && int(s.spider_y,0,480) && Number.isFinite(s.spider_heading) &&
    s.daily_streak<=7 && s.daily_task_done<=1 && s.pet_xp_today<=10;
}
export function lifeValid(life) {
  return !!life && (life.visitor_day===LIFE_NEVER || int(life.visitor_day,19723,LIFE_NEVER-1)) &&
    (life.season_highwater===0 || int(life.season_highwater,8097,65535)) &&
    array(life.progress,3,v=>int(v,0,5)) && int(life.cooldown,0,30) &&
    int(life.daynight,0,1) && int(life.weather,0,1) && int(life.light_gesture_disabled,0,1) &&
    int(life.retired_count,0,3) && array(life.retired,life.retired_count,validPet);
}
export function lifeCalendar(now=Date.now()) {
  if (!Number.isFinite(now) || now<START || now>LIFE_NEVER*1000)
    return {reliable:false,day:null,hour:null,phase:null,season:null,season_key:0,activity:'None'};
  const date = new Date(now), hour = date.getUTCHours(), quarter = Math.floor(date.getUTCMonth()/3);
  const phase = hour<6 || hour>=22 ? 'Night' : hour<12 ? 'Morning' : hour<18 ? 'Afternoon' : 'Evening';
  return {reliable:true,day:Math.floor(now/DAY),hour,phase,
    season:['Winter','Spring','Summer','Autumn'][quarter],
    season_key:date.getUTCFullYear()*4+quarter+1,
    activity:({Morning:'Stretch',Afternoon:'Explore',Evening:'Quiet',Night:'None'})[phase]};
}
export function lifeContext(s, now=Date.now()) {
  const calendar = lifeCalendar(now);
  const expression = s.sick ? 'Needs gentle care' : s.state===6 ? 'Resting peacefully' :
    s.hunger<25 ? 'Looking for a meal' : s.energy<20 ? 'Ready for a rest' :
    s.mood>=80 ? 'Feeling playful' : s.bond>=75 ? 'Happy to see you' : 'Curious and watching';
  return {...calendar,expression,activity:s.sick || s.state===6 || s.state===8 ? 'Rest' : calendar.activity};
}
function reject(s, reason) { s.lastEvent={type:'rejected',reason}; return false; }
export function lifeTrainReady(s, trick) {
  return int(trick,0,2) && !s.sick && ![6,8].includes(s.state) && s.energy>=20 &&
    s.life.progress[trick]<5 && s.life.cooldown===0;
}
export function lifeTrain(s, trick) {
  if (!lifeTrainReady(s,trick)) return reject(s,'Practice needs a healthy awake spider, 20 energy and no cooldown.');
  s.energy-=2;s.mood=Math.min(100,s.mood+1);s.life.progress[trick]++;s.life.cooldown=30;
  s.lastEvent={type:'practice',trick,mastered:s.life.progress[trick]===5};
  return true;
}
export function lifePerform(s, trick) {
  if (!int(trick,0,2) || s.life.progress[trick]!==5 || s.sick || [6,8].includes(s.state) || s.energy<20)
    return reject(s,'Perform needs a mastered trick and a healthy awake spider with 20 energy.');
  if (s._engine) s._engine.life_performance={trick,start:s._engine.now_ms,duration:1800};
  s.lastEvent={type:'perform',trick};
  return true;
}
export function lifeTick(s, seconds=.05) {
  const r=s._engine;
  if(r.life_performance && r.now_ms-r.life_performance.start>=r.life_performance.duration) r.life_performance=null;
  if(!s.life.cooldown){r.life_seconds=0;return;}
  r.life_seconds=(r.life_seconds??0)+seconds;
  const whole=Math.floor(r.life_seconds+1e-8);
  if (whole) { r.life_seconds-=whole;s.life.cooldown=Math.max(0,s.life.cooldown-whole); }
}
export function lifeVisitorAvailable(s, now=Date.now()) {
  const c=lifeCalendar(now);
  return c.reliable && c.day%3===0 && c.hour>=12 && c.hour<18 &&
    s.silk<=LIFE_NEVER-3 && (s.life.visitor_day===LIFE_NEVER || c.day>s.life.visitor_day);
}
export function lifeVisitorClaim(s, now=Date.now()) {
  if(!lifeVisitorAvailable(s,now))return reject(s,'No unclaimed visitor is here (every third calendar day, 12–18).');
  s.life.visitor_day=lifeCalendar(now).day;s.silk+=3;return true;
}
export function lifeSeasonAvailable(s, now=Date.now()) {
  const c=lifeCalendar(now);
  return c.reliable && s.silk<=LIFE_NEVER-10 && c.season_key>s.life.season_highwater;
}
export function lifeSeasonClaim(s, now=Date.now()) {
  if(!lifeSeasonAvailable(s,now))return reject(s,'No unclaimed seasonal reward; a reliable calendar is required.');
  s.life.season_highwater=lifeCalendar(now).season_key;s.silk+=10;return true;
}
function applyPet(s, pet) {
  Object.assign(s,decodeLifePet(pet));s.life.progress=[...pet.progress];s.life.cooldown=30;
  if(s.temperament===255)s.temperament=s.seed%4;
  for(const [key,value] of [['colour',0],['pupil_colour',0],['leg_colour',2],['fang_colour',1]])
    if(s[key]===255)s[key]=value;
  s.state=s.sick?9:0;s.eat_until_ms=0;s.blink_window=0;s.blink_until=0;
  s.prey=[];s.poop=[];s.visits=0;s.thought='';s.mind_rule=null;s.lastEvent=null;
}
export function lifeRetire(s, confirmed=false, hatchling=null) {
  if(new TextEncoder().encode(s.name??'').length>15)return reject(s,'Choose a shorter spider name (15 UTF-8 bytes) before archiving; nothing was changed.');
  if(!confirmed || s.stage!==2 || s.sick || s.life.retired_count>=3 || !hatchling)
    return reject(s,'Retirement needs a confirmed healthy adult and a free archive slot.');
  s.life.retired.push(encodeLifePet(s));s.life.retired_count++;
  const colours=Object.fromEntries(['colour','pupil_colour','leg_colour','fang_colour'].map(k=>[k,s[k]]));
  const seed=(Math.imul(s.seed,1664525)+1013904223)>>>0;
  const pet=encodeLifePet({...hatchling,...colours,hunger:80,mood:80,energy:80,hygiene:100,
    seed,temperament:seed%4,companion:companionDefaults(),life:lifeDefaults()});
  applyPet(s,pet);
  return true;
}
export function lifeResume(s, index, confirmed=false) {
  if(new TextEncoder().encode(s.name??'').length>15)return reject(s,'Choose a shorter spider name (15 UTF-8 bytes) before swapping; nothing was changed.');
  if(!confirmed || !int(index,0,s.life.retired_count-1))return reject(s,'Choose and confirm an archived spider to swap.');
  const current=encodeLifePet(s), archived=s.life.retired[index];
  applyPet(s,archived);s.life.retired[index]=current;
  return true;
}

export async function canvasPNG(canvas) {
  return new Promise((resolve,reject)=>canvas.toBlob(blob=>
    blob && blob.type==='image/png' && blob.size>0 ? resolve(blob) : reject(new Error('PNG encoding failed')), 'image/png'));
}
export class PhotoAlbum {
  constructor(indexedDB=globalThis.indexedDB) { this.indexedDB=indexedDB; }
  async open() {
    if(!this.indexedDB)throw new Error('IndexedDB is unavailable; no photo was saved.');
    return new Promise((resolve,reject)=>{
      const request=this.indexedDB.open('spidarium-photo-album',1);
      let blocked=false;
      request.onupgradeneeded=()=>request.result.createObjectStore('photos',{keyPath:'id',autoIncrement:true});
      request.onerror=()=>reject(request.error??new Error('Album could not be opened'));
      request.onblocked=()=>{blocked=true;reject(new Error('Album is blocked by another browser tab'));};
      request.onsuccess=()=>{
        const db=request.result;
        if(blocked){db.close();return;}
        db.onversionchange=()=>db.close();resolve(db);
      };
    });
  }
  async transaction(mode, action) {
    const db=await this.open();
    return new Promise((resolve,reject)=>{
      let result, failure;
      try {
        const tx=db.transaction('photos',mode), store=tx.objectStore('photos');
        tx.oncomplete=()=>{db.close();resolve(result);};
        tx.onabort=tx.onerror=()=>{db.close();reject(failure??tx.error??new Error('Album storage failed'));};
        action(store, value=>{result=value;}, error=>{failure=error;tx.abort();});
      } catch(error) {db.close();reject(error);}
    });
  }
  async list() {
    return this.transaction('readonly',(store,done)=>{
      const request=store.getAll();request.onsuccess=()=>done(request.result);
    });
  }
  async remove(id) {
    if(!Number.isSafeInteger(id) || id<=0)throw new Error('Invalid photo ID');
    return this.transaction('readwrite',(store,done,fail)=>{
      const request=store.get(id);
      request.onsuccess=()=>{
        if(!request.result){fail(new Error('Photo does not exist'));return;}
        const deletion=store.delete(id);
        deletion.onsuccess=()=>done(id);
      };
    });
  }
  async capture(canvas, name='', now=Date.now()) {
    const blob=await canvasPNG(canvas);
    if(blob.size>2*1024*1024)throw new Error('Photo exceeds the 2 MB album limit');
    return this.transaction('readwrite',(store,done,fail)=>{
      const count=store.count();
      count.onsuccess=()=>{
        if(count.result>=8){fail(new Error('Album is full (8 photos); existing photos were not overwritten'));return;}
        const request=store.add({blob,name:String(name).slice(0,15),created:now});
        request.onsuccess=()=>done(request.result);
      };
    });
  }
}
