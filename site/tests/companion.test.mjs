import test from 'node:test';
import assert from 'node:assert/strict';
import * as E from '../lib/device-engine.js';
import {DeviceGame} from '../lib/device-ui.js';

const DAY=86400000, START=1704067200000;
const fresh=(now=START)=>E.freshState({random:()=>.5,now:()=>now,temperament:0});
const copy=value=>JSON.parse(JSON.stringify(value));
const init=(s,now=START)=>E.companionTick(s,now);
const request=(kind,target=255)=>({kind,target,progress:0,state:0});
const note=(s,kind,value=255,now=START)=>E.companionEvent(s,kind,value,now);

test('fresh companion matches zeroed C ABI and arrays are independent',()=>{
  const c=E.companionDefaults();
  assert.equal(E.companionValid(c),true);
  assert.equal(c.initialized,0);assert.equal(c.clock_mode,0);assert.equal(c.request_day,0);
  assert.deepEqual(c.requests,[{kind:0,target:0,progress:0,state:0},{kind:0,target:0,progress:0,state:0}]);
  assert.deepEqual([c.favorite_treat,c.favorite_gift,c.favorite_play],[255,255,255]);
  c.requests[0].kind=1;assert.equal(c.requests[1].kind,0);
  c.treat_counts[0]=1;assert.equal(E.companionDefaults().treat_counts[0],0);
});

test('old saves 1 through14 migrate without retroactive milestones or aliasing',()=>{
  for(let version=1;version<=14;version++){
    const raw=copy(fresh());raw.version=version;delete raw.companion;
    if(version<3)delete raw.state;
    if(version<12)raw.gifts_owned=0;
    const s=E.restoreSim(raw,{now:()=>START,random:()=>.5});
    assert.equal(s.companion.initialized,0,`v${version}`);
    s.bond=100;s.stage=2;s.achievements=0xfffff;s.sick=1;
    init(s);
    assert.equal(s.companion.diary_count,0);
    assert.equal(s.companion.bond_level,4);
    assert.equal(s.companion.last_stage,2);
    assert.equal(s.companion.seen_achievements,0xfffff);
  }
  const original=fresh();init(original);const raw=copy(original);
  const restored=E.restoreSim(raw);restored.companion.treat_counts[0]=2;
  assert.equal(raw.companion.treat_counts[0],0);
});

test('strict validation rejects malformed nested requests, memories, counts and flags',()=>{
  const s=fresh();init(s);
  const invalid=[
    c=>{c.request_day=-1;},c=>{c.request_day=2**32;},
    c=>{c.seen_achievements=1<<20;},c=>{c.clock_mode=3;},c=>{c.initialized=2;},
    c=>{c.requests=[];},c=>{c.requests[0].kind=5;},c=>{c.requests[0].target=254;},
    c=>{c.requests[0].state=3;},c=>{c.requests[0].progress=4;},
    c=>{c.requests[0]={kind:1,target:255,progress:2,state:1};},
    c=>{c.requests[0]={kind:1,target:255,progress:3,state:0};},
    c=>{c.treat_counts[0]=256;},c=>{c.play_counts[0]=-1;},c=>{c.gift_counts.pop();},
    c=>{c.favorite_treat=12;},c=>{c.favorite_gift=36;},c=>{c.favorite_play=3;},
    c=>{c.bond_level=5;},c=>{c.last_stage=3;},c=>{c.was_sick=2;},
    c=>{c.play_counts[0]=256;},c=>{c.diary_count=1;},
    c=>{c.favorite_treat=0;c.treat_counts[0]=2;},
    c=>{delete c.requests[0].kind;},c=>{delete c.play_counts[0];},
    ...[{kind:0,value:0},{kind:1,value:12},{kind:2,value:36},{kind:3,value:3},
      {kind:4,value:0},{kind:4,value:5},{kind:5,value:0},{kind:5,value:3},
      {kind:6,value:1},{kind:7,value:20},{kind:8,value:0},{kind:8,value:3},
      {kind:6,value:0,pinned:2},{kind:6,value:0,calendar:2},{kind:6,value:0,day:-1}]
      .map(fields=>c=>{c.diary=[{day:1,pinned:0,calendar:1,...fields}];c.diary_count=1;}),
  ];
  for(const mutate of invalid){
    const raw=copy(s);mutate(raw.companion);
    assert.equal(E.companionValid(raw.companion),false,mutate.toString());
    assert.throws(()=>E.restoreSim(raw),/companion/,mutate.toString());
  }
  const uninitialized=E.companionDefaults();uninitialized.clock_mode=1;
  assert.equal(E.companionValid(uninitialized),false);
  /* The two bytes that used to be `reserved` now hold play_counts[3..4], so a
   * full five-entry play_counts array is what has to validate. */
  const counts=E.companionDefaults();counts.play_counts=[1,2,3,4,5];
  assert.equal(E.companionValid(counts),true);
  assert.throws(()=>E.restoreSim({...copy(s),companion:null}),/companion/);
});

test('request generation is deterministic and uses exact eligibility boundaries',()=>{
  for(const [changes,kind,target] of [
    [{gift:{used:1,kind:35},hunger:60,energy:20,sick:0},4,35],
    [{gift:{used:1,kind:0},hunger:59,hygiene:89},6,255],
    [{hygiene:90,energy:30,hunger:89,sick:0,level:1},3,0],
    [{hygiene:90,energy:29,hunger:89},1,255],
    [{hygiene:90,energy:30,hunger:90},1,255],
    [{hygiene:89,sick:1},6,255],
    [{hygiene:90,sick:1},1,255],
  ]){
    const s=Object.assign(fresh(),changes);init(s);
    assert.equal(s.companion.requests[1].kind,kind);
    assert.equal(s.companion.requests[1].target,target);
    assert.notEqual(s.companion.requests[0].kind,s.companion.requests[1].kind);
    const duplicate=Object.assign(fresh(),changes);init(duplicate);
    assert.deepEqual(duplicate.companion.requests,s.companion.requests);
  }
  for(let level=1;level<=20;level++){
    const s=Object.assign(fresh(),{hygiene:100,hunger:50,energy:80,level});init(s);
    assert.ok(E.speciesUnlocked(s,s.companion.requests[1].target));
  }
});

test('clock reliability boundary, playday rollover, backclock, calendar arrival and reboot',()=>{
  const s=fresh(0);s.age_ticks=2879;init(s,START-1);
  assert.equal(s.companion.clock_mode,2);assert.equal(s.companion.request_day,0);
  s.age_ticks=2880;init(s,0);assert.equal(s.companion.request_day,1);
  s.companion.requests=[request(1),request(6)];note(s,1,255,0);
  const before=copy(s.companion.requests);
  s.age_ticks=0;init(s,0);assert.deepEqual(s.companion.requests,before);
  init(s,START);
  assert.equal(s.companion.clock_mode,1);
  assert.equal(s.companion.request_day,Math.floor(START/DAY));
  assert.deepEqual(s.companion.requests,before);
  init(s,START-1);assert.deepEqual(s.companion.requests,before);
  init(s,START-DAY);assert.deepEqual(s.companion.requests,before);
  const restored=E.restoreSim(copy(s),{now:()=>START});
  init(restored,START);assert.deepEqual(restored.companion.requests,before);
  init(restored,START+DAY-1);assert.deepEqual(restored.companion.requests,before);
  init(restored,START+DAY);assert.ok(restored.companion.requests.every(r=>r.progress===0&&r.state===0));
});

test('requests save partial progress, pay five Silk once automatically, group pinned summary',()=>{
  const s=fresh();init(s);s.companion.requests=[request(1),request(6)];
  note(s,1);note(s,1);assert.equal(s.companion.requests[0].progress,2);
  const restored=E.restoreSim(copy(s),{now:()=>START}),balance=restored.silk;
  note(restored,1);assert.equal(restored.silk,balance+5);
  assert.deepEqual(restored.companion.requests[0],{kind:1,target:255,progress:3,state:1});
  assert.equal(E.companionPin(restored,0),true);
  note(restored,6);assert.equal(restored.silk,balance+10);
  assert.equal(restored.companion.diary.length,1);
  assert.equal(restored.companion.diary[0].value,2);
  assert.equal(restored.companion.diary[0].pinned,1);
  for(let i=0;i<5;i++){note(restored,1);note(restored,6);}
  assert.equal(restored.silk,balance+10);
  const again=E.restoreSim(copy(restored),{now:()=>START});note(again,1);note(again,6);
  assert.equal(again.silk,balance+10);
});

test('dismissal is explicit, persistent and penalty-free; targets match exactly',()=>{
  const s=fresh();init(s);s.companion.requests=[request(3,2),request(4,35)];
  const silk=s.silk;
  note(s,3,1);note(s,4,34);assert.ok(s.companion.requests.every(r=>r.progress===0));
  assert.equal(E.companionDismiss(s,1),true);assert.equal(E.companionDismiss(s,1),false);
  assert.equal(E.companionDismiss(s,-1),false);
  note(s,4,35);assert.equal(s.silk,silk);
  const restored=E.restoreSim(copy(s));assert.equal(restored.companion.requests[1].state,2);
  note(s,3,2);assert.equal(s.silk,silk+5);
});

test('preferences discover on third positive exposure, stay stable, saturate and use happy art',()=>{
  for(const [event,count,favorite,memory,max] of [
    [5,'treat_counts','favorite_treat',1,12],[4,'gift_counts','favorite_gift',2,36],[2,'play_counts','favorite_play',3,3],
  ]){
    const s=fresh();init(s);s.companion.requests=[request(1),request(6)];
    note(s,event,0);note(s,event,0);assert.equal(s.companion[favorite],255);
    s.mood=70;note(s,event,0);assert.equal(s.companion[favorite],0);assert.equal(s.mood,72);
    assert.ok(s._engine.pet_react_until>s._engine.now_ms);
    assert.equal(s.companion.diary.filter(m=>m.kind===memory).length,1);
    for(let i=0;i<6;i++)note(s,event,max-1);
    assert.equal(s.companion[favorite],0);
    s.companion[count][0]=255;s.mood=99;note(s,event,0);
    assert.equal(s.companion[count][0],255);assert.equal(s.mood,100);
    for(const invalid of [-1,max,255,.5])assert.equal(note(s,event,invalid),false);
    assert.equal(E.companionValid(s.companion),true);
  }
});

test('bond milestones, stage, recovery and all twenty achievement memories are first-only',()=>{
  const s=fresh();init(s);
  s.bond=24;init(s);assert.equal(s.companion.diary.length,0);
  s.bond=25;init(s);s.bond=100;init(s);
  assert.deepEqual(s.companion.diary.map(m=>m.value),[1,2,3,4]);
  s.bond=0;init(s);s.bond=100;init(s);
  assert.equal(s.companion.diary.length,4);
  s.stage=1;init(s);s.stage=2;init(s);init(s);
  assert.deepEqual(s.companion.diary.filter(m=>m.kind===5).map(m=>m.value),[1,2]);
  s.sick=1;init(s);s.sick=0;init(s);init(s);
  assert.equal(s.companion.diary.filter(m=>m.kind===6).length,1);
  s.achievements=0xfffff;init(s);init(s);
  assert.deepEqual(s.companion.diary.filter(m=>m.kind===7).map(m=>m.value),Array.from({length:20},(_,i)=>i));
});

test('diary cap64 evicts oldest unpinned; pin cap8 rejects ninth and preserves entries',()=>{
  const s=fresh();init(s);
  for(let i=0;i<64;i++){s.companion.request_day=i;E.companionAddMemory(s,6,0);}
  for(let i=0;i<8;i++)assert.equal(E.companionPin(s,i),true);
  assert.equal(E.companionPin(s,8),false);
  assert.equal(s.companion.diary[8].pinned,0);
  E.companionAddMemory(s,6,0);
  assert.equal(s.companion.diary_count,64);
  assert.deepEqual(s.companion.diary.slice(0,9).map(m=>m.day),[0,1,2,3,4,5,6,7,9]);
  assert.equal(E.companionPin(s,0),true);assert.equal(E.companionPin(s,8),true);
  assert.equal(E.companionValid(s.companion),true);
  const bad=copy(s.companion);bad.diary[9].pinned=1;assert.equal(E.companionValid(bad),false);
});

test('engine integration counts actual clean, successful manual catch and completed minigames only',()=>{
  const s=fresh();init(s);s.companion.requests=[request(2),request(3,0)];
  E.care(s,'play');assert.equal(s.companion.play_counts[0],0);
  E.minigameReward(s,255,8);assert.equal(s.companion.requests[0].state,0);
  E.minigameReward(s,0,{score:0,rounds:8});
  assert.equal(s.companion.play_counts[0],1);assert.equal(s.companion.requests[0].state,1);
  s.hunger=40;s.state=0;E.spawnPrey(s,0);
  Object.assign(s.prey.find(p=>p.alive),{x:s.spider_x,y:s.spider_y});
  assert.equal(E.resolvePounce(s,false),true);assert.equal(s.companion.requests[1].state,0);
  s.hunger=40;s.state=0;E.spawnPrey(s,0);
  Object.assign(s.prey.find(p=>p.alive&&!p.dead),{x:s.spider_x,y:s.spider_y});
  assert.equal(E.resolvePounce(s,true),true);assert.equal(s.companion.requests[1].state,1);
  s.companion.requests=[request(1),request(6)];E.care(s,'clean');
  assert.equal(s.companion.requests[1].state,1);
  s.companion.requests[1]=request(6);assert.equal(E.care(s,'clean'),false);
  assert.equal(s.companion.requests[1].progress,0);
});

test('engine treat preferences ignore fully capped treats and gift purchases/start do not count visits',()=>{
  const s=fresh();init(s);s.silk=10000;s.hunger=s.mood=s.energy=s.hygiene=100;
  E.buyTreat(s,0);assert.equal(s.companion.treat_counts[0],0);
  for(let i=0;i<3;i++){s.hunger=50;assert.ok(E.buyTreat(s,0));}
  assert.equal(s.companion.favorite_treat,0);
  assert.ok(E.placeGift(s,0,s.spider_x,s.spider_y));
  assert.equal(s.companion.gift_counts[0],0);
  s.state=0;s.hunger=80;assert.ok(E.visitGift(s));
  assert.equal(s.companion.gift_counts[0],0);
  E.tick(s,3.2);
  assert.equal(s.companion.gift_counts[0],1);
});

function uiFixture(){
  const ui=Object.assign(Object.create(DeviceGame.prototype),{
    state:fresh(),close(){},panel(heading,back){return {heading,back};},refreshHUD(){},persist(){this.saved=(this.saved??0)+1;},
    list(rows,page){this.rows=rows;this.page=page;},
    copy(text,action,label,icons){this.detail={text,action,label,...icons};},
    report(message,error){this.notice={message,error};},cancelTap(){},
  });
  return ui;
}
test('menus combine check-in and three claimable tasks in Today; Daily aliases Today',()=>{
  const ui=uiFixture();ui.open('Menu');
  for(const [name,icon] of [['Your Spider','personality'],['Today','today'],['Diary','diary']])
    assert.equal(ui.rows.find(r=>r.label===name).icon,`ui_${icon}`);
  assert.ok(!ui.rows.some(r=>r.label==='Daily Check-In'||r.label==='Daily'));
  ui.open('Daily');assert.equal(ui.modal.name,'Today');assert.equal(ui.modal.heading,'Today');
  ui.open('Your Spider');assert.equal(ui.rows.length,5);
  assert.deepEqual(ui.rows.map(r=>r.icon),['personality','bond','favorite_treat','favorite_place','favorite_game'].map(i=>`ui_${i}`));
  assert.equal(ui.rows[2].detail,'Still discovering');
  for(const menu of ['Your Spider','Today','Diary']){ui.back(menu);assert.equal(ui.modal.name,'Menu');}
  /* Every Today row is claimable: the check-in plus three rotating tasks. The
   * dismissible request row is gone -- requests still pay out passively when
   * their event happens, but they are no longer a chore on this screen. */
  ui.open('Today');assert.equal(ui.rows.length,4);
  assert.deepEqual(ui.rows.map(r=>r.icon),
    ['ui_checkin','ui_daily_task','ui_daily_task','ui_daily_task']);
  assert.equal(new Set(ui.rows.slice(1).map(r=>r.label)).size,3);
  E.companionAddMemory(ui.state,6,0);E.companionAddMemory(ui.state,4,1);
  ui.open('Diary');assert.match(ui.rows[0].label,/Bond reached 25/);
  ui.rows[0].click();assert.equal(ui.detail.label,'Pin');ui.detail.action();
  assert.equal(ui.state.companion.diary[1].pinned,1);assert.equal(ui.detail.label,'Unpin');
  assert.equal(ui.detail.icon,'ui_memory_bond');assert.equal(ui.detail.actionIcon,'ui_unpin');
  ui.detail.action();assert.equal(ui.state.companion.diary[1].pinned,0);
  assert.equal(ui.detail.actionIcon,'ui_pin');
});

test('Today claims preserve streak and both legacy rewards without replay',()=>{
  const ui=uiFixture(),s=ui.state;
  s.daily_override=100;s.daily_day=99;s.daily_streak=2;
  E.dailyEnsureBase(s);s.meals+=3; // Day 100 uses the catch-prey task.
  ui.open('Today');
  const claim=ui.rows[0].click,task=ui.rows[1].click;
  assert.equal(ui.rows[0].enabled,true);assert.match(ui.rows[0].detail,/Day 3/);
  assert.equal(ui.rows[1].enabled,true);
  const silk=s.silk,xp=s.xp;
  claim();assert.equal(s.daily_day,100);assert.equal(s.daily_streak,3);
  assert.equal(s.silk,silk+E.CONFIG.DAILY_SILK_BASE+2*E.CONFIG.DAILY_SILK_PER_DAY);
  assert.equal(s.xp,xp+E.CONFIG.DAILY_XP_BASE+2*E.CONFIG.DAILY_XP_PER_DAY);
  assert.match(ui.rows[0].detail,/Claimed \/ Streak 3/);assert.equal(ui.rows[0].enabled,false);
  task();assert.equal(s.daily_task_done,1);assert.equal(ui.rows[1].enabled,false);
  assert.match(ui.rows[1].detail,/Done/);
  assert.equal(s.silk,silk+E.CONFIG.DAILY_SILK_BASE+2*E.CONFIG.DAILY_SILK_PER_DAY+E.CONFIG.DAILY_TASK_SILK);
  const paid=copy(s);
  claim();task();ui.open('Daily');ui.rows[0].click();ui.rows[1].click();
  assert.equal(s.silk,paid.silk);assert.equal(s.xp,paid.xp);
  ui.state=E.restoreSim(paid,{now:()=>START});ui.state.daily_override=100;
  ui.open('Today');ui.rows[0].click();ui.rows[1].click();
  assert.equal(ui.state.silk,paid.silk);assert.equal(ui.state.xp,paid.xp);
  ui.state.daily_override=101;ui.open('Daily');
  assert.equal(ui.rows[0].enabled,true);assert.match(ui.rows[0].detail,/Day 4/);
  ui.rows[0].click();assert.equal(ui.state.daily_streak,4);
  ui.open('Menu');assert.match(ui.rows.find(r=>r.label==='Today').detail,/Claimed/);
});

test('each Today task slot claims independently and only once',()=>{
  const ui=uiFixture(),s=ui.state;
  s.daily_override=100;s.daily_day=99;s.daily_streak=2;
  E.dailyEnsureBase(s);
  /* Slot 0 is catch-prey, slot 1 clean, slot 2 play on day 100. */
  s.meals+=3;s.cleans+=2;s.plays+=3;
  ui.open('Today');
  assert.equal(ui.rows.length,4);
  for(let i=1;i<4;i++)assert.equal(ui.rows[i].enabled,true,`slot ${i-1} not ready`);
  const silk=s.silk;
  ui.rows[1].click();
  assert.equal(s.silk,silk+E.CONFIG.DAILY_TASK_SILK);
  assert.equal(s.daily_task_done,1);
  assert.equal(s.daily_task_done_extra,0);
  ui.rows[2].click();
  assert.equal(s.silk,silk+2*E.CONFIG.DAILY_TASK_SILK);
  assert.equal(s.daily_task_done_extra,1);
  ui.rows[3].click();
  assert.equal(s.silk,silk+3*E.CONFIG.DAILY_TASK_SILK);
  assert.equal(s.daily_task_done_extra,3);
  /* A second tap on any slot pays nothing. */
  ui.rows[1].click();ui.rows[2].click();ui.rows[3].click();
  assert.equal(s.silk,silk+3*E.CONFIG.DAILY_TASK_SILK);
  /* The day rollover re-snapshots all three slots. */
  s.daily_override=101;E.dailyEnsureBase(s);
  assert.equal(s.daily_task_done,0);assert.equal(s.daily_task_done_extra,0);
  assert.equal(E.dailyAnyTaskReady(s),false);
});

test('request and memory kinds keep dedicated icons in lists and details without visible UTC',()=>{
  const ui=uiFixture();init(ui.state);
  /* open('Today') re-runs companionTick() with the real clock, so pin the
   * request day to today or the day rollover regenerates the requests. */
  ui.state.companion.request_day=Math.floor(Date.now()/DAY);
  /* Requests are no longer a Today row, so their icons are checked through the
   * text helper that still renders them in the diary and the HUD. */
  for(const [kind,icon] of [[1,'pet'],[2,'play'],[3,'hunt'],[4,'visit'],[6,'clean']]){
    ui.state.companion.requests=[request(kind,kind===3||kind===4?0:255),request(6)];
    const text=E.companionRequestText(ui.state,0,{species:E.SPECIES,gifts:E.GIFTS});
    assert.equal(text.icon,`ui_request_${icon}`);
    assert.doesNotMatch(text.detail,/\bUTC\b/i);
  }
  const kinds=['treat','place','game','bond','growth','recovery','achievement','requests'];
  for(let i=0;i<kinds.length;i++){
    const memory={kind:i+1,value:[0,0,0,1,1,0,0,1][i],day:Math.floor(START/DAY),calendar:1,pinned:0};
    ui.state.companion.diary=[memory];ui.state.companion.diary_count=1;
    ui.open('Diary');assert.equal(ui.rows[0].icon,`ui_memory_${kinds[i]}`);
    assert.match(ui.rows[0].detail,/2024-01-01/);assert.doesNotMatch(ui.rows[0].detail,/\bUTC\b/i);
    ui.rows[0].click();assert.equal(ui.detail.icon,`ui_memory_${kinds[i]}`);
    assert.doesNotMatch(ui.detail.text,/\bUTC\b/i);
  }
  // Calendar formatting remains based on UTC day numbers, with no timezone label.
  assert.equal(E.companionMemoryText({kind:6,value:0,day:0,calendar:1,pinned:0}).detail,'1970-01-01');
});

test('detail copy renders its kind icon and action icon while retaining ordinary copy behavior',t=>{
  const previous=globalThis.document;
  globalThis.document={createElement:tag=>({tag,style:{},children:[],append(...children){this.children.push(...children);}})};
  t.after(()=>{if(previous===undefined)delete globalThis.document;else globalThis.document=previous;});
  const panel={children:[],append(child){this.children.push(child);}};
  const ui=uiFixture();ui.modal={panel,name:'Diary Detail'};
  let buttonArgs;
  ui.button=(...args)=>{buttonArgs=args;};
  const action=()=>{};
  DeviceGame.prototype.copy.call(ui,'A memory',action,'Pin',{icon:'ui_memory_treat',actionIcon:'ui_pin'});
  const content=panel.children[0];
  assert.equal(content.className,'d-copy');assert.equal(content.children[0].tag,'img');
  assert.match(content.children[0].src,/ui_memory_treat\.png/);
  assert.equal(content.children[0].alt,'');assert.equal(content.children[1].textContent,'A memory');
  assert.equal(buttonArgs[6],action);assert.equal(buttonArgs[7],'ui_pin');
  DeviceGame.prototype.copy.call(ui,'Ordinary help',action);
  assert.equal(panel.children[1].children.length,1);
  assert.equal(panel.children[1].children[0].textContent,'Ordinary help');
  assert.equal(buttonArgs[1],'Done');assert.equal(buttonArgs[7],undefined);
});

test('menu pin failure is explicit; diary pagination and completed request detail require no claim',()=>{
  const ui=uiFixture();init(ui.state);
  for(let i=0;i<10;i++)E.companionAddMemory(ui.state,4,1);
  for(let i=0;i<8;i++)E.companionPin(ui.state,i);
  ui.open('Diary',2);assert.equal(ui.page,2);
  ui.memoryDetail(9,2);ui.detail.action();assert.equal(ui.notice.error,true);
  assert.match(ui.notice.message,/Eight memories/);assert.equal(ui.state.companion.diary[9].pinned,0);
  ui.state.companion.requests[0]={kind:2,target:255,progress:1,state:1};
  ui.requestDetail(0);assert.equal(ui.detail.label,'Back to Today');
  assert.match(ui.detail.text,/No deadline pressure, penalties or claim button/);
  assert.match(E.companionMemoryText({day:0,kind:6,value:0,pinned:0,calendar:0}).detail,/Play day 1/);
  assert.match(E.companionMemoryText({day:0xffffffff,kind:6,value:0,pinned:0,calendar:1}).detail,/Calendar day 4294967295/);
});

test('gentle tip is once per day, hidden during other UI and not repeated on calendar arrival',()=>{
  const ui=uiFixture();init(ui.state,0);
  const messages=[];ui.report=text=>messages.push(text);
  for(const field of ['placement','modal','game','hud']){
    ui[field]={};ui.companionTip();assert.equal(messages.length,0);ui[field]=null;
  }
  ui.companionTip();ui.companionTip();assert.equal(messages.length,1);
  assert.equal(messages[0],'One optional request / Menu > Today');
  init(ui.state,START);ui.companionTip();assert.equal(messages.length,1);
  init(ui.state,START+DAY);ui.companionTip();assert.equal(messages.length,2);
});

test('USB Chimes defaults on, strictly validates v15, and ignores legacy padding values',()=>{
  assert.equal(fresh().usb_sounds,1);
  for(const flag of [0,1]){
    const raw=copy(fresh());raw.usb_sounds=flag;
    assert.equal(E.restoreSim(raw).usb_sounds,flag);
  }
  for(const flag of [undefined,null,true,false,-1,2,.5,'1']){
    const raw=copy(fresh());
    if(flag===undefined)delete raw.usb_sounds;else raw.usb_sounds=flag;
    assert.throws(()=>E.restoreSim(raw),/usb_sounds/);
  }
  for(let version=1;version<=14;version++){
    const raw=copy(fresh());raw.version=version;raw.usb_sounds=0;
    if(version<3)delete raw.state;
    assert.equal(E.restoreSim(raw).usb_sounds,1,`v${version} old padding0`);
    delete raw.usb_sounds;
    assert.equal(E.restoreSim(raw).usb_sounds,1,`v${version} absent flag`);
  }
});

test('Sound USB Chimes is sixth checkbox, toggles without USB audio and roundtrips',()=>{
  const ui=uiFixture();
  ui.open('Sound',1);assert.equal(ui.rows.length,6);
  assert.deepEqual(ui.rows.map(r=>r.icon),['volume','sound','music','music_volume','soundtrack','usb_chimes'].map(i=>`ui_${i}`));
  let chimes=ui.rows.at(-1);
  assert.equal(chimes.label,'USB Chimes');assert.equal(chimes.checkbox,true);
  assert.equal(chimes.selected,true);assert.equal(chimes.detail,'On / Device Only');
  chimes.click();
  assert.equal(ui.page,1);assert.equal(ui.saved,1);assert.equal(ui.state.usb_sounds,0);
  assert.equal(ui.rows.at(-1).detail,'Off / Device Only');
  assert.equal(E.restoreSim(copy(ui.state)).usb_sounds,0);
  ui.rows.at(-1).click();
  assert.equal(ui.saved,2);assert.equal(ui.state.usb_sounds,1);
  assert.equal(E.restoreSim(copy(ui.state)).usb_sounds,1);
});
