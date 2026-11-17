/** Persistent, deterministic companion rules shared by browser engine and backups.
 * Times passed to these helpers are milliseconds; diary/request days are UTC days. */
export const COMPANION_UNKNOWN = 255;
export const COMPANION_EVENTS = Object.freeze({PET:1,PLAY:2,HUNT:3,VISIT:4,TREAT:5,CLEAN:6});
const U32 = 0xffffffff, CALENDAR_START = 1704067200000;
const PREY_LEVELS = [1,2,4,6,10,13,16,20];
const validInt = (v, min, max) => Number.isInteger(v) && v >= min && v <= max;
function arrayOf(value,size,valid) {
  if(!Array.isArray(value)||value.length!==size)return false;
  for(let i=0;i<size;i++)if(!Object.hasOwn(value,i)||!valid(value[i]))return false;
  return true;
}
export function companionDefaults() {
  return {request_day:0,seen_achievements:0,
    requests:Array.from({length:2},()=>({kind:0,target:0,progress:0,state:0})),
    treat_counts:Array(12).fill(0),gift_counts:Array(36).fill(0),play_counts:Array(5).fill(0),
    favorite_treat:255,favorite_gift:255,favorite_play:255,diary_count:0,
    clock_mode:0,initialized:0,bond_level:0,last_stage:0,was_sick:0,reserved:[0,0],diary:[]};
}
export function companionGoal(request) { return request.kind === 1 ? 3 : 1; }
function validRequest(r) {
  if (!r || !validInt(r.kind,0,6) || !validInt(r.state,0,2) || !validInt(r.progress,0,companionGoal(r))) return false;
  if (!r.kind) return r.target === 0 && r.state === 0 && r.progress === 0;
  const targets = {1:0,2:3,3:8,4:36,6:0};
  if (!Object.hasOwn(targets,r.kind)) return false;
  if (r.target !== 255 && !validInt(r.target,0,targets[r.kind]-1)) return false;
  return r.state === 1 ? r.progress === companionGoal(r) : r.state === 2 || r.progress < companionGoal(r);
}
function validMemory(m) {
  if (!m || !validInt(m.day,0,U32) || !validInt(m.pinned,0,1) || !validInt(m.calendar,0,1)) return false;
  const bounds = {1:[0,11],2:[0,35],3:[0,2],4:[1,4],5:[1,2],6:[0,0],7:[0,19],8:[1,2]};
  return Object.hasOwn(bounds,m.kind) && validInt(m.value,...bounds[m.kind]);
}
export function companionValid(c) {
  if (!c || !validInt(c.request_day,0,U32) || !validInt(c.seen_achievements,0,0xfffff) ||
      !validInt(c.clock_mode,0,2) || !validInt(c.initialized,0,1) || !validInt(c.bond_level,0,4) ||
      !validInt(c.last_stage,0,2) || !validInt(c.was_sick,0,1)) return false;
  for (const [key,size,favorite] of [['treat_counts',12,'favorite_treat'],['gift_counts',36,'favorite_gift'],['play_counts',5,'favorite_play']]) {
    if (!arrayOf(c[key],size,v=>validInt(v,0,255)) ||
        !(c[favorite] === 255 || (validInt(c[favorite],0,size-1)&&c[key][c[favorite]]>=3))) return false;
  }
  /* The two bytes that used to be `reserved` now hold play_counts[3..4], so
   * there is no reserved array to validate any more. */
  const valid=arrayOf(c.requests,2,validRequest) &&
    Array.isArray(c.diary) && c.diary.length <= 64 && c.diary_count === c.diary.length &&
    arrayOf(c.diary,c.diary_count,validMemory) && c.diary.filter(m=>m.pinned).length <= 8;
  if (!valid) return false;
  if (c.initialized) return c.clock_mode !== 0 && c.requests.every(r=>r.kind !== 0);
  return c.clock_mode === 0;
}
export function companionAddMemory(s,kind,value) {
  const c=s.companion;
  if (c.diary.length === 64) {
    const index=c.diary.findIndex(m=>!m.pinned);
    if (index < 0) return false;
    c.diary.splice(index,1);
  }
  c.diary.push({day:c.request_day,kind,value,pinned:0,calendar:c.clock_mode === 1 ? 1 : 0});
  c.diary_count=c.diary.length;
  return true;
}
function generate(s) {
  const c=s.companion, mix=((s.seed>>>0)^Math.imul(c.request_day,2654435761))>>>0;
  const first={kind:mix&1?1:2,target:255,progress:0,state:0};
  let kind=1,target=255;
  if (s.gift?.used && !s.sick && s.energy>=20 && s.hunger>=60) {kind=4;target=s.gift.kind;}
  else if (s.hygiene<90) kind=6;
  else if (!s.sick && s.energy>=30 && s.hunger<90) {
    const unlocked=PREY_LEVELS.reduce((n,level)=>n+(s.level>=level?1:0),0);
    if (unlocked) {kind=3;target=(mix>>>8)%unlocked;}
  }
  c.requests=[first,{kind,target,progress:0,state:0}];
  if(first.kind === kind) first.kind=2;
}
export function companionTick(s, now=Date.now()) {
  const c=s.companion ?? (s.companion=companionDefaults());
  const calendar=Number.isFinite(now)&&now>=CALENDAR_START;
  const day=Math.min(U32,Math.max(0,Math.floor(calendar?now/86400000:s.age_ticks/2880)));
  if (!c.initialized) {
    c.initialized=1;c.clock_mode=calendar?1:2;c.request_day=day;
    c.seen_achievements=s.achievements>>>0;c.bond_level=Math.floor(s.bond/25);
    c.last_stage=s.stage;c.was_sick=s.sick?1:0;
    generate(s);
    return c;
  }
  if (c.clock_mode === 2 && calendar) {
    // Calendar arrival relabels this set; never grants a second set in one playday.
    c.clock_mode=1;c.request_day=day;
  } else if ((c.clock_mode === 1 ? calendar : !calendar) && day>c.request_day) {
    c.request_day=day;generate(s);
  }
  const bond=Math.floor(s.bond/25);
  for(let level=c.bond_level+1;level<=bond;level++) companionAddMemory(s,4,level);
  c.bond_level=Math.max(c.bond_level,bond);
  if(s.stage>c.last_stage) companionAddMemory(s,5,s.stage);
  c.last_stage=s.stage;
  if(c.was_sick&&!s.sick) companionAddMemory(s,6,0);
  c.was_sick=s.sick?1:0;
  const achievements=(s.achievements>>>0)&~c.seen_achievements;
  for(let bit=0;bit<20;bit++) if(achievements&(1<<bit)) companionAddMemory(s,7,bit);
  c.seen_achievements=(c.seen_achievements|s.achievements)>>>0;
  return c;
}
export function companionEvent(s,kind,value=255,now=Date.now()) {
  const c=companionTick(s,now);
  if (!validInt(kind,1,6) ||
      ([2,3,4,5].includes(kind)&&!validInt(value,0,({2:3,3:8,4:36,5:12})[kind]-1))) return false;
  if (kind === 2 || kind === 4 || kind === 5) {
    const [counts,favorite,memory]=({2:['play_counts','favorite_play',3],4:['gift_counts','favorite_gift',2],5:['treat_counts','favorite_treat',1]})[kind];
    c[counts][value]=Math.min(255,c[counts][value]+1);
    if(c[favorite] === 255 && c[counts][value]>=3) {
      c[favorite]=value;companionAddMemory(s,memory,value);
    }
    if(c[favorite] === value) {
      s.mood=Math.min(100,s.mood+2);
      if(s._engine) s._engine.pet_react_until=s._engine.now_ms+520;
    }
  }
  for(const request of c.requests) {
    if(request.state || request.kind !== kind || (request.target !== 255 && request.target !== value)) continue;
    request.progress=Math.min(companionGoal(request),request.progress+1);
    if(request.progress === companionGoal(request)) {
      request.state=1;s.silk=Math.min(U32,s.silk+5);
      const summary=c.diary.find(m=>m.kind === 8 && m.day === c.request_day && m.calendar === (c.clock_mode === 1 ? 1 : 0));
      if(summary) summary.value=Math.min(2,summary.value+1);
      else companionAddMemory(s,8,1);
    }
  }
  return true;
}
export function companionDismiss(s,slot) {
  if (!validInt(slot,0,1) || !s.companion?.initialized || s.companion.requests[slot].state !== 0) return false;
  s.companion.requests[slot].state=2;return true;
}
export function companionPin(s,index) {
  const c=s.companion;
  if(!validInt(index,0,(c?.diary.length??0)-1)) return false;
  const memory=c.diary[index];
  if(!memory.pinned && c.diary.filter(m=>m.pinned).length>=8) return false;
  memory.pinned=memory.pinned?0:1;return true;
}
const PLAY_NAMES=['Bug Hunt','Color Match','Quick Tap','Web Weave','Pounce'];
const REQUEST_ICONS={1:'request_pet',2:'request_play',3:'request_hunt',4:'request_visit',6:'request_clean'};
const MEMORY_ICONS={1:'memory_treat',2:'memory_place',3:'memory_game',4:'memory_bond',
  5:'memory_growth',6:'memory_recovery',7:'memory_achievement',8:'memory_requests'};
export function companionRequestText(s,slot,names={}) {
  const r=s.companion.requests[slot];
  const name=({1:'Pet your spider',2:'Finish a minigame',3:`Hunt ${names.species?.[r.target]?.name??'prey'} manually`,
    4:`Visit ${names.gifts?.[r.target]?.name??'the placed gift'}`,6:'Freshen the enclosure'})[r.kind]??'Rest today';
  return {title:name,detail:`${r.progress}/${companionGoal(r)} / ${['Optional / +5 Silk automatically','Complete / +5 Silk paid','Not Today'][r.state]}`,
    icon:`ui_${REQUEST_ICONS[r.kind]??'today'}`};
}
export function companionMemoryText(memory,names={}) {
  const value=memory.value;
  const title=({1:`Favorite treat: ${names.treats?.[value]?.name??value}`,2:`Favorite gift: ${names.gifts?.[value]?.name??value}`,
    3:`Favorite play: ${PLAY_NAMES[value]}`,4:`Bond reached ${value*25}`,5:`Grew into ${value === 1?'Juvenile':'Adult'}`,
    6:'Feeling better again',7:`Achievement: ${names.achievements?.[value]?.name??value}`,
    8:`${value} optional request${value === 1?'':'s'} completed`})[memory.kind];
  let date=`Play day ${memory.day+1}`;
  if(memory.calendar) {
    const time=new Date(memory.day*86400000);
    date=Number.isFinite(time.getTime())?time.toISOString().slice(0,10):`Calendar day ${memory.day}`;
  }
  return {title,detail:`${date}${memory.pinned?' / Pinned':''}`,icon:`ui_${MEMORY_ICONS[memory.kind]??'diary'}`};
}
export { PLAY_NAMES as COMPANION_PLAY_NAMES };
