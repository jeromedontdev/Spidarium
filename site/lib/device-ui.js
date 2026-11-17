import * as E from './sim.js?v=1.1.31';
import { renderSpider, BODY_COLOURS, ACCENT_COLOURS } from './spider-renderer.js';
import { blinkAt, glanceAt, breathAt, motesAt, dustColour, lightOpaAt,
  sporesAt, strandsAt, webColour, particlesAt, emitForDecorKey, emitForGift,
  EMIT } from './ambient.js';
import { LocalProgress } from './local-progress.js';
import { Minigame, GAME_NAMES, COLOR_NAMES, COLOR_RGB } from './minigames.js';
import { DeviceAudio, MUSIC_TRACKS } from './device-audio.js?v=1.1.31';
import { HELP_TOPICS } from './help.js?v=1.1.31';

// Carry the UI revision to its artwork so returning players cannot mix releases.
const assetRevision = new URL(import.meta.url).search;
const asset = name => new URL(`../../assets/${name}.png${assetRevision}`, import.meta.url).href;
// Splat colours per prey species, matching the native dead-sprite tints.
const SPLAT_COLOURS = ['#8a6a3a', '#4f6b2f', '#8a7a5a', '#3f3a2a', '#5a3a2a', '#a08a5a', '#6a5a3a', '#8a5a4a'];
const title = name => name.split(/[ _]+/).map(word => word[0].toUpperCase() + word.slice(1)).join(' ');
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
const MENU_ICONS = {
  Daily:'today', 'Check In':'checkin', Treats:'treats', Backgrounds:'backgrounds', Achievements:'achievements',
  'Prey Species':'prey', Decor:'decor', 'Placed Items':'decor',
  Settings:'settings', WiFi:'wifi', 'SD Card':'sd', About:'about',
  Battery:'battery', Runtime:'daily', Display:'display', Brightness:'energy',
  Sound:'sound', Volume:'volume', 'Background Music':'music',
  'Auto Feed':'auto_feed', 'Wipe Spider':'reset',
  'Catch 3 Prey':'feed', 'Clean 2 Times':'clean', 'Play 3 Times':'play', 'Pet 4 Times':'mood',
  Move:'decor', 'Move Gift':'decor', 'Put Away':'back', 'Put Gift Away':'back', Blocking:'lock',
  'Power Mode':'energy', 'Screen Timeout':'display', 'Deep Sleep After':'sleep', 'RTC Wake Interval':'daily',
  'Deep Sleep Now':'lock',
  'Help Guide':'about', 'Screen Rotation':'display', Mute:'sound', Soundtrack:'soundtrack',
  'Music Volume':'music_volume', 'Back Up Save':'sd', 'Restore Save':'back', 'SD Status':'status',
  'Firmware Update':'wifi', 'Browser Edition':'about',
  'Hardware':'settings', 'License':'xp',
  'Your Spider':'personality', Today:'today', Diary:'diary',
  Temperament:'personality', Bond:'bond', 'Favorite Treat':'favorite_treat',
  'Favorite Gift':'favorite_place', 'Favorite Play':'favorite_game',
  'USB Chimes':'usb_chimes',
  Training:'training', 'Photo Album':'album', Calendar:'calendar', Generations:'generations',
  'Photo Storage':'album',
  'Day and Night':'daylight', Weather:'weather', Visitor:'visitor', 'Season Reward':'season_reward',
  Practice:'practice', Perform:'perform', 'Retire Spider':'retire', Resume:'resume',
  'Capture Photo':'camera',
  'Delete Photo':'delete_photo', 'Double-Tap Lights':'light_gesture', 'Dim Enclosure':'enclosure_light',
};
const POWER_MODES = ['Balanced','Battery Saver','RTC Sleep'];
const SCREEN_TIMEOUTS = ['30 Seconds','1 Minute','2 Minutes','5 Minutes','Never'];
const SLEEP_DELAYS = ['Never','5 Minutes Idle','15 Minutes Idle','30 Minutes Idle','1 Hour Idle'];
const RTC_WAKES = ['15 Minutes','1 Hour','6 Hours'];
const SOUNDTRACKS = MUSIC_TRACKS;
const TRACK_ART = ['background_leaf_litter_0', 'background_night_0', 'background_woodland_0',
  'background_crystal_0','background_moss_0','background_desert_0','background_observatory_0',
  'background_bamboo_grove_0','background_circuit_garden_0','background_pond_0'];
function element(tag, className, text) {
  const obj = document.createElement(tag); obj.className = className;
  if (text !== undefined) obj.textContent = text;
  return obj;
}
function box(obj, x, y, w, h) {
  Object.assign(obj.style, { left: `${x}px`, top: `${y}px`, width: `${w}px`, height: `${h}px` }); return obj;
}
export class DeviceGame {
  constructor(root) {
    this.root = root; this.screen = root.querySelector('.game-screen');
    this.controls = root.querySelector('#device-controls'); this.canvas = root.querySelector('#tank');
    this.ctx = this.canvas.getContext('2d'); this.ctx.imageSmoothingEnabled = false;
    this.spiderCanvas = document.createElement('canvas'); this.spiderCanvas.width = this.spiderCanvas.height = 96;
    this.spiderCtx = this.spiderCanvas.getContext('2d'); this.spiderPixels = this.spiderCtx.createImageData(96, 96);
    this.images = new Map(); this.hud = false; this.modal = null; this.game = null; this.giftKind = null; this.decorKind = null; this.decorMoveSlot = null; this.decorSlot = null; this.aiming = false; this.lastGame = -1;
    this.error = false;
    this.placement = null; this.placementPointer = null;
    this.audio = new DeviceAudio((message, error) => this.report(message, error));
    let storage;
    try { storage = window.localStorage; }
    catch (error) { this.report(`Local storage unavailable: ${error.message}`, true); storage = { getItem() { throw error; }, setItem() { throw error; } }; }
    this.progress = new LocalProgress(storage, value => E.restoreSim(value), message => this.report(message, true));
    this.state = this.progress.load();
    this.album = new E.PhotoAlbum();
    this.started = this.state.setup_complete ?? Boolean(this.state.createdAt);
    this.buildHUD();
    if (!this.started) this.setup();
    const resize = () => { this.screen.style.transform = `scale(${root.querySelector('.game-viewport').clientWidth / 480})`; };
    new ResizeObserver(resize).observe(root.querySelector('.game-viewport')); resize();
    root.querySelector('#fullscreen').onclick = () => this.fullscreen();
    document.addEventListener('fullscreenchange', () => {
      root.querySelector('#fullscreen').textContent = document.fullscreenElement === root ? 'Exit Fullscreen' : 'Fullscreen'; resize();
    });
    this.canvas.addEventListener('pointerdown', event => this.pointerDown(event));
    this.canvas.addEventListener('pointermove', event => this.pointerMove(event));
    this.canvas.addEventListener('pointerup', event => this.pointerUp(event));
    this.canvas.addEventListener('pointercancel', event => this.pointerCancel(event));
    this.canvas.addEventListener('pointerleave', () => this.cancelLongPress());
    this.screen.addEventListener('keydown', event => this.key(event));
    root.querySelector('#browser-save-file').onchange = () => this.importProgress();
    root.querySelector('#browser-save-file').oncancel = () =>
      this.report('Browser restore cancelled. Progress unchanged.');
    window.addEventListener('pagehide', () => this.persist());
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) this.persist();
      this.audio.settings(this.state.sound && !document.hidden, this.state.volume);
      this.last = performance.now();
    });
    this.last = performance.now(); this.accumulator = 0; this.lastSave = this.last;
    requestAnimationFrame(now => this.frame(now));
  }
  report(message, error = false) {
    if (this.progress?.blocked && !error) return;
    this.error = error; const status = this.root.querySelector('#sim-message');
    status.textContent = message; status.classList.toggle('error', error);
    if (!this.notice) {
      this.notice = element('div', 'd-notice');
      this.notice.setAttribute('role', 'status');
      this.screen.append(this.notice);
    }
    this.notice.textContent = message;
    this.notice.style.bottom = this.placement ? '112px' : '';
    this.notice.classList.toggle('error', error);
    this.notice.hidden = false;
    clearTimeout(this.noticeTimer);
    this.noticeTimer = setTimeout(() => { this.notice.hidden = true; }, error ? 10000 : 6000);
  }
  persist() { return this.started ? this.progress.save(this.state) : false; }
  lifeAction(action, message) {
    if(this.progress.blocked) {
      this.report('Progress storage is blocked. Download a backup and restore storage before changing life progress.',true);
      return false;
    }
    const before=JSON.parse(JSON.stringify(this.state));
    if(!action(this.state)) {
      this.report(this.state.lastEvent?.reason??'That action is unavailable.',true);return false;
    }
    if(!this.persist()) {
      this.state=E.restoreSim(before);
      this.report('Life action was not saved and has been undone. Download a backup and check site storage.',true);
      return false;
    }
    if(message)this.report(message);
    this.refreshHUD();return true;
  }
  async fullscreen() {
    try {
      if (document.fullscreenElement === this.root) await document.exitFullscreen();
      else if (this.root.requestFullscreen) await this.root.requestFullscreen();
      else throw new Error('Fullscreen is unavailable in this browser. Use the browser’s own fullscreen controls.');
    } catch (error) { this.report(`Fullscreen failed: ${error.message}`, true); }
  }
  image(name) {
    if (!this.images.has(name)) {
      const img = new Image();
      img.onerror = () => this.report(`Required artwork could not be loaded: ${name}`, true);
      img.src = asset(name); this.images.set(name, img);
    }
    return this.images.get(name);
  }
  button(parent, text, x, y, w, h, action, icon) {
    const button = box(element('button', 'd-button'), x, y, w, h);
    button.type = 'button'; button.onclick = event => {
      event.stopPropagation(); this.screen.focus({ preventScroll: true });
      void this.audio.unlock(this.state.sound);
      if (this.state.sound) this.audio.play('tap');
      action();
    };
    if (icon) { const image = element('img', ''); image.src = asset(icon); image.alt = ''; button.append(image); }
    button.append(element('span', '', text)); parent.append(button); return button;
  }
  buildHUD() {
    this.controls.replaceChildren();
    this.hudLayer = element('div', ''); this.controls.append(this.hudLayer);
    this.stats = ['hunger', 'mood', 'energy', 'hygiene'].map((key, index) => {
      const row = box(element('div', 'd-stat'), 81 + index * 81, 8, 75, 44);
      const bar = element('div', 'd-stat-bar'), value = element('span', 'd-stat-value');
      bar.append(element('span', ''));
      row.append(element('span', 'd-stat-name', ['Food', 'Mood', 'Energy', 'Clean'][index]), value, bar);
      this.hudLayer.append(row); return { key, bar, value };
    });
    this.identity = element('div', 'd-identity');
    this.identity.append(element('div', ''), element('div', ''), element('div', '')); this.hudLayer.append(this.identity);
    this.careButtons = {};
    ['Feed', 'Clean', 'Play', 'Sleep', 'Gift', 'Menu'].forEach((name, i) => {
      this.careButtons[name] = this.button(this.hudLayer, name, 40 + i % 3 * 134, 304 + Math.floor(i / 3) * 72, 126, 64, () => this.action(name), `ui_${['feed', 'clean', 'play', 'sleep', 'decor', 'status'][i]}`);
      this.careButtons[name].classList.add('d-care');
    });
    this.ear = element('button', 'd-ear'); this.ear.type = 'button'; this.ear.setAttribute('aria-label', 'Show or Hide Care HUD');
    this.ear.title = 'Care and Menu (M)';
    this.ear.setAttribute('aria-keyshortcuts', 'M');
    this.earMark = element('span', ''); this.ear.append(this.earMark);
    this.ear.onclick = () => {
      this.cancelTap();
      if (this.placement) { this.cancelPlacement(); return; }
      else { E.cancelAim(this.state); this.hud = !this.hud; }
      this.refreshHUD();
    };
    this.controls.append(this.ear);
    this.placementControls=element('div','d-placement-controls');
    this.controls.append(this.placementControls);
    this.placementHint=box(element('div','d-placement-hint','Slide to position; Done saves'),80,340,320,36);
    this.placementControls.append(this.placementHint);
    this.placementAway=this.button(this.placementControls,'Put Away',176,338,128,48,()=>this.putAwayPlacement());
    this.placementPaths=E.DECOR_PATH_NAMES.map((name,path)=>
      this.button(this.placementControls,name,8+path*118,232,112,48,()=>this.setPlacementPath(path)));
    this.placementBack=this.button(this.placementControls,'‹ Back',80,286,96,48,()=>this.changePlacementLayer(-1));
    this.placementFront=this.button(this.placementControls,'Front ›',304,286,96,48,()=>this.changePlacementLayer(1));
    for(const button of [...this.placementPaths,this.placementBack,this.placementFront])
      button.classList.add('d-placement-small');
    this.placementLayer=box(element('div','d-placement-hint d-placement-layer'),176,292,128,40);
    this.placementControls.append(this.placementLayer);
    this.button(this.placementControls,'Cancel',80,388,148,64,()=>this.cancelPlacement(),'ui_back');
    this.button(this.placementControls,'Done',252,388,148,64,()=>this.commitPlacement(),'ui_decor');
    const battery = this.button(this.hudLayer, '', 412, 8, 52, 44, () => this.open('Battery'));
    battery.classList.add('d-battery'); battery.setAttribute('aria-label', 'Battery: Hardware Telemetry Unavailable');
    this.refreshHUD();
  }
  refreshHUD() {
    this.hudLayer.hidden = !this.hud;
    this.ear.hidden = Boolean(this.modal || this.game || !this.started || this.placement);
    const placing = this.giftKind !== null || this.decorKind !== null || this.decorMoveSlot !== null;
    this.placementControls.hidden = !this.placement;
    const editing=this.placement&&(this.placement.slot!==null||this.giftMoving);
    this.placementAway.hidden=!editing;
    const decor=!!this.placement&&!this.placement.gift,p=this.placement;
    /* A gift gets the layer control but NOT the path control: it is always
     * walked over. It has no siblings, so the Back/Front layer arrows and the
     * layer counter stay decor-only. */
    const pathRow=!!this.placement&&!p.gift;
    this.placementPaths.forEach((button,path)=>{
      button.hidden=!pathRow;
      button.setAttribute('aria-pressed',String(pathRow&&p.blocking===path));
      button.style.background=pathRow&&p.blocking===path?'#2b6a5f':'';
    });
    for(const control of [this.placementBack,this.placementFront,this.placementLayer])control.hidden=!decor;
    if(pathRow){
      this.placementHint.textContent=['Spider crosses above this item','Spider steers around this item','Spider passes underneath this item','Spider climbs onto this item'][p.blocking];
    }
    else this.placementHint.textContent='Slide to position; Done saves';
    if(decor){
      const count=E.decorLayerSlots(this.state,p.blocking,p.slot??-1).length+1;
      this.placementBack.disabled=p.layer===0;this.placementFront.disabled=p.layer===count-1;
      this.placementLayer.textContent=`${E.decorCanopy(p.blocking)?'Canopy':'Floor'} Layer\n${p.layer+1} / ${count}`;
    }
    this.placementHint.style.top=pathRow?'196px':'272px';
    this.earMark.textContent = '';
    this.ear.setAttribute('aria-label', 'Show or Hide Care HUD');
    this.ear.setAttribute('aria-expanded', String(this.hud));
    this.stats.forEach(({ key, bar, value }, i) => {
      const number = Math.round(this.state[key]); value.textContent = number;
      bar.firstChild.style.width = `${number}%`;
      bar.firstChild.style.background = value.style.color = ['#f5c86b', '#76d6b1', '#99b4ff', '#68d5df'][i];
    });
    const live = this.state.prey.filter(bug => bug.alive).length;
    this.careButtons.Feed.disabled = live >= 3;
    this.careButtons.Sleep.lastChild.textContent = this.state.state === E.STATES.SLEEP ? 'Wake' : 'Sleep';
    const s = this.state;
    const next = [...E.GIFTS, ...E.SPECIES].filter(item => item.unlock_level > s.level).sort((a, b) => a.unlock_level - b.unlock_level)[0];
    this.identity.children[0].textContent = `${s.name || 'Spider'} / Lv ${s.level} / ${s.xp % E.CONFIG.XP_PER_LEVEL}/${E.CONFIG.XP_PER_LEVEL} XP`;
    this.identity.children[1].textContent = next ? `Next: ${title(next.name)} at Level ${next.unlock_level}` : 'All Gifts and Prey Unlocked';
    this.identity.children[2].textContent = `${s.silk} Silk`;
  }
  action(name) {
    if (name === 'Menu') this.open('Menu');
    else if (name === 'Gift') this.open('Gifts');
    else if (name === 'Play') this.play();
    else {
      const result = name === 'Feed' ? E.feed(this.state) : E.care(this.state, name.toLowerCase());
      if (!result) {
        // Clean is never disabled; it simply has nothing to do on a spotless
        // tank, so say that rather than calling the button unavailable.
        this.report(name === 'Clean' ? 'Already Clean' : `${name} is unavailable in the current state.`, true);
      }
      this.persist(); this.refreshHUD();
    }
  }
  panel(heading, back) {
    const overlay = element('div', 'd-overlay'), panel = element('div', 'd-panel');
    overlay.append(panel); this.controls.append(overlay);
    this.button(panel, 'Back', 12, 12, 96, 56, back); panel.append(element('h3', 'd-heading', heading));
    return { overlay, panel };
  }
  close() {
    if(this.modal?.name && Number.isInteger(this.modal.page)) {
      this.menuPages??=new Map();
      this.menuPages.set(this.modal.name,this.modal.page);
    }
    if(this.photoURL){URL.revokeObjectURL(this.photoURL);this.photoURL=null;}
    this.modal?.overlay.remove(); this.modal = null;
    if (this.game) { this.game.engine.cancel(); this.game.overlay.remove(); this.game = null; }
    this.refreshHUD();
  }
  open(name, page = null, slot = null) {
    if (name === 'Daily') name = 'Today';
    this.cancelTap();
    this.close();
    page??=this.menuPages?.get(name)??0;
    const heading=name==='Gifts'?`Gifts / ${this.state.silk} Silk`:name.startsWith('Trick ')?`${E.TRICKS[Number(name.slice(-1))]} / Training`:name;
    this.modal = this.panel(heading, () => this.back(name)); this.modal.name = name; this.modal.page = page;
    if (slot !== null) this.decorSlot = slot;
    this.refreshHUD();
    const toggle = field => {
      this.state[field] = !this.state[field];
      // Auto Feed is one switch: wild prey and unattended hunting move together.
      if (field === 'autoFeed') this.state.autoHunt = this.state.autoFeed;
      this.persist(); this.open(name, page);
    };
    const row = (label, click, detail = '', extra = {}) => ({
      label, click, detail, icon:`ui_${MENU_ICONS[label] ?? 'status'}`, ...extra });
    let rows;
    switch (name) {
      case 'Menu': rows = [
        row('Your Spider', () => this.open('Your Spider'), 'Personality and Favorites', { submenu: true }),
        row('Today', () => this.open('Today'), `${this.dailyDetail()} / One Optional Request`, { submenu: true }),
        row('Training', () => this.open('Training'), 'Three Tricks / No Silk Rewards', {submenu:true}),
        row('Calendar', () => this.open('Calendar'), 'Activity, Visitors and Seasons', {submenu:true}),
        row('Treats', () => this.open('Treats'), 'Spend Silk', { submenu: true }),
        row('Achievements', () => this.open('Achievements'), this.achievementDetail(), { submenu: true }),
        row('Prey Species', () => this.open('Prey Species'), this.speciesDetail(), { submenu: true }),
        row('Diary', () => this.open('Diary'), 'Memories and Pinned Moments', { submenu: true }),
        row('Photo Album', () => this.open('Photo Album'), 'Eight Persistent Browser Photos', {submenu:true}),
        row('Generations', () => this.open('Generations'), 'Optional / Keep Every Spider', {submenu:true}),
        row('Decor', () => this.open('Decor'), `${E.decorPlacedCount(this.state)} Placed`,
          { submenu: true }),
        row('Backgrounds', () => this.open('Backgrounds'), 'Enclosure Themes', { submenu: true }),
        row('Borders', () => this.open('Borders'), 'Mix and Match Frames',
          { submenu: true,icon:`border_${this.state.background}_0` }),
        /* Settings is always the last row: it is the least-used entry. */
        row('Help Guide',()=>this.open('Help Guide'),'All Features and Controls'),
        row('Settings', () => this.open('Settings'), 'Sound, Display, WiFi', { submenu: true }),
      ]; break;
      case 'Your Spider': {
        E.companionTick(this.state);
        const c=this.state.companion;
        const favorite=(id,items)=>id===255?'Still discovering':items[id].name;
        rows=[
          row('Temperament',null,`${title(E.TEMPERAMENTS[this.state.temperament].name)} / ${['Quick, confident hunter','Patient, cautious hunter','Hearty appetite','Unhurried explorer'][this.state.temperament]}`),
          row('Bond',null,`${this.state.bond}/100 / ${E.lifeContext(this.state).expression}`),
          row('Favorite Treat',null,favorite(c.favorite_treat,E.TREATS)),
          row('Favorite Gift',null,favorite(c.favorite_gift,E.GIFTS)),
          row('Favorite Play',null,c.favorite_play===255?'Still discovering':GAME_NAMES[c.favorite_play]),
        ];
        break;
      }
      case 'Training':
        rows=E.TRICKS.map((trick,id)=>row(trick,()=>this.open(`Trick ${id}`),
          `${this.state.life.progress[id]}/5${this.state.life.progress[id]===5?' / Mastered':''}`,
          {icon:`ui_trick_${trick.toLowerCase()}`,submenu:true}));
        rows.push(row('Practice Cooldown',null,`${this.state.life.cooldown}s of active play remaining`,{icon:'ui_practice'}));
        break;
      case 'Trick 0': case 'Trick 1': case 'Trick 2': {
        const id=Number(name.slice(-1)), ready=E.lifeTrainReady(this.state,id);
        const canPerform=this.state.life.progress[id]===5 && !this.state.sick &&
          ![E.STATES.SLEEP,E.STATES.MOLT].includes(this.state.state) && this.state.energy>=20;
        rows=[
          row(E.TRICKS[id],null,`${this.state.life.progress[id]}/5 / Healthy, awake, 20+ energy`,{icon:`ui_trick_${E.TRICKS[id].toLowerCase()}`}),
          row('Practice',()=>{
            if(this.lifeAction(s=>E.lifeTrain(s,id),'Practice saved. Play in the enclosure to recharge.'))this.open(name);
          },`−2 Energy / +1 Mood / ${this.state.life.cooldown}s cooldown`,{enabled:ready}),
          row('Perform',()=>{
            if(this.lifeAction(s=>E.lifePerform(s,id),`${E.TRICKS[id]}! No Silk or XP awarded.`)){this.close();this.hud=false;this.refreshHUD();}
          },'Mastered Trick / No Rewards',{enabled:canPerform}),
        ];break;
      }
      case 'Calendar': {
        const c=E.lifeContext(this.state);
        rows=[
          row('Calendar',null,c.reliable?`${c.phase} / ${c.season}`:'Calendar unavailable / No invented season'),
          row('Activity',null,c.activity==='None'?'Quiet time':c.activity,{icon:'ui_activity'}),
          row('Visitor',()=>{
            if(this.lifeAction(s=>E.lifeVisitorClaim(s),'Visitor thanked you: +3 Silk saved.'))this.open(name);
          },c.reliable?'Every third day / 12–18 / +3 Silk once':'Needs a reliable calendar',{enabled:E.lifeVisitorAvailable(this.state)}),
          row('Season Reward',()=>{
            if(this.lifeAction(s=>E.lifeSeasonClaim(s),'Season reward: +10 Silk saved.'))this.open(name);
          },'10 Silk / Once per calendar quarter',{enabled:E.lifeSeasonAvailable(this.state)}),
          ...[['Day and Night','daynight'],['Weather','weather']].map(([label,key])=>row(label,()=>{
            if(this.lifeAction(s=>{s.life[key]^=1;return true;}))this.open(name,page);
          },key==='weather'?'Optional Tiny Edge Particles':'Subtle Background Tint',
          {checkbox:true,selected:!!this.state.life[key]})),
        ];break;
      }
      case 'Generations':
        rows=[
          row('Retire Spider',()=>this.generationConfirm('retire'),
            `${this.state.life.retired_count}/3 Archived / Healthy Adult Only`,
            {enabled:this.state.stage===2&&!this.state.sick&&this.state.life.retired_count<3}),
          ...this.state.life.retired.map((pet,index)=>{
            const archived=E.decodeLifePet(pet);
            return row(`Resume ${archived.name||'Spider'}`,()=>this.generationConfirm('resume',index),
              'Swap Pets / Nothing Discarded',{icon:'ui_resume'});
          }),
        ];break;
      case 'Photo Album':
      case 'Photo Storage':
        this.albumMenu();return;
      case 'Diary': {
        E.companionTick(this.state);
        rows=this.state.companion.diary.map((memory,index)=>{
          const text=this.memoryText(memory);
          return row(text.title,()=>this.memoryDetail(index,page),text.detail,{submenu:true,icon:text.icon});
        }).reverse();
        if(!rows.length)rows=[row('A new chapter',null,'New memories will appear as you care together.',{icon:'ui_diary'})];
        break;
      }
      case 'Today': {
        E.companionTick(this.state);
        const today = E.dailyToday(this.state);
        const available = E.dailyAvailable(this.state);
        const next = available
          ? (this.state.daily_day !== E.CONFIG.DAILY_NEVER && this.state.daily_day + 1 === today &&
             this.state.daily_streak > 0
            ? Math.min(this.state.daily_streak + 1, E.CONFIG.DAILY_STREAK_MAX) : 1)
          : this.state.daily_streak;
        const step = Math.max(0, next - 1);
        const claimDetail = available
          ? `Day ${next} / +${E.CONFIG.DAILY_XP_BASE + step * E.CONFIG.DAILY_XP_PER_DAY} XP ` +
            `+${E.CONFIG.DAILY_SILK_BASE + step * E.CONFIG.DAILY_SILK_PER_DAY} Silk`
          : `Claimed / Streak ${this.state.daily_streak}`;
        const task = E.dailyTask(this.state);
        const taskDetail = this.state.daily_task_done
          ? `Done / +${E.CONFIG.DAILY_TASK_XP} XP +${E.CONFIG.DAILY_TASK_SILK} Silk`
          : E.dailyTaskReady(this.state) ? 'Complete / Tap to Claim'
            : `${E.dailyTaskProgress(this.state)} of ${E.dailyTaskGoal(this.state)}`;
        rows = [
          row('Check In', () => { E.dailyClaim(this.state); this.persist(); this.open('Today'); },
            claimDetail, { enabled: available }),
          row(E.dailyTaskName(task),
            () => { E.dailyTaskClaim(this.state); this.persist(); this.open('Today'); },
            taskDetail, { enabled: E.dailyTaskReady(this.state), icon:'ui_daily_task' }),
          /* One request row, no submenu. Two dismissible rows plus a "Not Today"
           * confirmation made Today read as a chore list; the rotating daily
           * task is a single tap, so the request is too. The first request that
           * has not been dismissed is the one shown, so a completed request
           * stays visible with its "Complete" line. */
          (() => {
            const slot = this.state.companion.requests.findIndex(r => r.state !== 2);
            if (slot < 0) return row('Requests Done', null, 'New Ones Tomorrow', { icon:'ui_dismiss' });
            const request = this.state.companion.requests[slot];
            const text = E.companionRequestText(this.state, slot, {species:E.SPECIES,gifts:E.GIFTS});
            return row(text.title, () => {
              if (!request.state) E.companionDismiss(this.state, slot);
              this.persist(); this.open('Today');
            }, text.detail, { icon:text.icon });
          })(),
        ];
        break;
      }
      case 'Treats': rows = [
        ...E.TREATS.map((item, id) => row(item.name,
          () => { E.buyTreat(this.state, id); this.persist(); this.open('Treats',page); },
          `${item.cost} Silk${this.state.silk < item.cost ? ' / Need More' : ''}`,
          { enabled: this.state.silk >= item.cost,
            icon:item.asset })),
      ]; break;
      case 'Decor': {
        rows = E.DECOR_KINDS.map((item, id) => {
          const owned = E.decorOwned(this.state, id);
          const unlocked = E.decorUnlocked(this.state, id);
          /* Level-locked. Showing the Silk cost here read as "you need 52
           * Silk", so a player with plenty of Silk could not tell why the row
           * was grey. Name the actual blocker. */
          const detail = !unlocked ? `Level ${item.level} Needed`
            : owned ? `Owned / ${item.climb ? 'Climbable' : item.blocking ? 'Blocks' : 'Walk Over'}`
            : `${item.cost} Silk / ${item.climb ? 'Climbable' : item.blocking ? 'Blocks' : 'Walk Over'}`;
          /* `selected` is deliberately NOT "owned": the panel jumps to the page
           * of the first selected row, so marking owned kinds selected yanked
           * the player to page 2 the moment they bought anything past the third
           * row. The owned state is already in the detail line. */
          return row(item.name, () => {
            if (!unlocked) return;
            if (!owned && !E.buyDecor(this.state, id)) return;
            this.persist();
            this.beginDecorPlacement(id);
          }, detail, { icon: E.decorSprite(id), enabled: unlocked && (owned || this.state.silk >= item.cost) });
        });
        break;
      }
      case 'Decor Item': this.beginDecorMove(this.decorSlot); return;
      case 'Settings': rows = [
        row('WiFi', () => this.open('WiFi'), 'Networks and Time Sync', { submenu: true }),
        row('SD Card', () => this.open('SD Card'), 'Back Up and Restore', { submenu: true }),
        row('About', () => this.open('About'), 'Version and Hardware', { submenu: true }),
        row('Battery', () => this.open('Battery'), 'Charge and Runtime', { submenu: true }),
        row('Display', () => this.open('Display'), 'Brightness and Rotation', { submenu: true }),
        row('Sound', () => this.open('Sound'), 'Volume and Mute', { submenu: true }),
        row('Auto Feed', () => toggle('autoFeed'), 'Wild Prey and Unattended Hunting', { checkbox: true, selected: this.state.autoFeed }),
        row('Auto Clean', () => toggle('auto_clean'), 'Sweep Droppings Automatically', { checkbox: true, selected: !!this.state.auto_clean }),
        row('Lock Decor', () => toggle('decor_locked'), 'Placed Items Cannot Be Moved', { checkbox: true, selected: !!this.state.decor_locked }),
        row('Wipe Spider', () => this.newTank(), 'Erases This Spider', { destructive: true }),
      ]; break;
      case 'Sound': rows = [
        row('Volume', null, `${this.state.volume}%`, { slider: { value: this.state.volume, min: 0, max: 100, step: 5, change: value => { this.state.volume = value; this.persist(); } } }),
        row('Sound', () => { this.state.sound = !this.state.sound; void this.audio.unlock(this.state.sound); this.persist(); this.open(name); },
          this.state.sound ? 'On / Effects and Music' : 'Off / All Audio Muted',
          { checkbox:true, selected:this.state.sound }),
        row('Background Music', () => { this.state.music_enabled = this.state.music_enabled ? 0 : 1; this.persist(); this.open(name); },
          'Gentle Ambient Score', { checkbox:true, selected:!!this.state.music_enabled }),
        row('Music Volume', null, `${this.state.music_volume}%`, {
          slider:{value:this.state.music_volume,min:0,max:100,step:5,change:value=>{
            this.state.music_volume=value;this.persist();
          }} }),
        row('Soundtrack', () => this.open('Soundtrack'), SOUNDTRACKS[this.state.music_track],
          {submenu:true}),
        row('USB Chimes', () => { this.state.usb_sounds=this.state.usb_sounds?0:1; this.persist(); this.open(name,page); },
          `${this.state.usb_sounds?'On':'Off'} / Device Only`, {checkbox:true,selected:!!this.state.usb_sounds}),
      ]; break;
      case 'Soundtrack': rows = SOUNDTRACKS.map((label,id)=>row(label,()=>{
        this.state.music_track=id;this.persist();this.open(name,page);
      },id===this.state.music_track?'Selected / Original Ambient Score':'Choose This Score',
      {selected:id===this.state.music_track,icon:TRACK_ART[id]})); break;
      case 'Backgrounds': rows = E.BACKGROUNDS.map((key,id) => row(title(key), () => {
        if (!E.buyBackground(this.state,id)) { this.report('Not enough Silk for that background.',true); return; }
        this.persist(); this.open(name, page);
      }, this.state.theme === key ? 'In Use' : E.backgroundOwned(this.state,id) ? 'Owned / Apply Free' : `${E.BACKGROUND_COSTS[id]} Silk / Buy`,
      { selected: this.state.theme === key, enabled:E.backgroundOwned(this.state,id) || this.state.silk>=E.BACKGROUND_COSTS[id],
        icon: `background_${key}_0` })); break;
      case 'Borders': rows = [
        row('No Border',()=>{E.buyBorder(this.state,255);this.persist();this.open('Borders');},
          'Remove Frame / Free',{selected:this.state.border_id===255,icon:'border_minimal_0'}),
        ...E.BACKGROUNDS.map((key,id)=>row(title(key),()=>{
          if(!E.buyBorder(this.state,id)){this.report('Not enough Silk for this frame.',true);return;}
          this.persist();this.open('Borders',Math.floor((id+1)/6));
        },this.state.border_id===id?'In Use':E.borderOwned(this.state,id)?'Owned / Apply Free':
          `${E.borderCost(id)} Silk / Buy Frame`,
        {selected:this.state.border_id===id,enabled:E.borderOwned(this.state,id)||this.state.silk>=E.borderCost(id),icon:`border_${key}_0`}))
      ];break;
      case 'Achievements': rows = E.ACHIEVEMENTS.map(item => row(item.name,
        () => {this.close();this.modal=this.panel(item.name,()=>this.open('Achievements',page));
          this.modal.name='Achievement';this.copy(`${item.goal}.\n\nReward: +${item.xp} XP and +${item.silk} Silk, paid automatically once.\n\n${this.state.achievements&item.bit?'Earned. Existing earned achievements are not paid twice.':'Keep caring for your spider to unlock this milestone.'}`,
            ()=>this.open('Achievements',page),'Back to Achievements');},
        `${this.state.achievements & item.bit ? 'Earned' : 'Goal'} / +${item.xp} XP +${item.silk} Silk`,
        { selected: Boolean(this.state.achievements & item.bit), icon: item.asset })); break;
      case 'Prey Species': rows = E.SPECIES.map(item => row(title(item.name), null,
        `${this.state.level >= item.unlock_level ? 'Available' : 'Locked'} / Level ${item.unlock_level} / Food x${item.hunger_mult.toFixed(1)}`,
        { icon: `prey_${item.key}_walk_0` })); break;
      case 'Gifts': rows = E.GIFTS.map(item => {
        const owned = E.giftOwned(this.state, item.id);
        const current = this.state.gift.used && this.state.gift.kind === item.id;
        const available = this.state.level >= item.level && (owned || this.state.silk >= item.cost);
        const status = this.state.level < item.level ? `Level ${item.level} / ${item.cost} Silk`
          : current ? 'In Tank / Move Free' : owned ? 'Owned / Place Free'
            : `${item.cost} Silk / ${this.state.silk >= item.cost ? 'Buy' : 'Earn More Silk'}`;
        return row(item.name, () => this.previewGift(item.id, status, page), `${status}\n${item.effect}`, { enabled: available, selected: current, icon: item.asset });
      }); break;
      case 'Gift Item': this.beginGiftMove(); return;
      case 'WiFi': this.copy('WiFi, time sync and OTA use the board’s radio.\n\nThis browser has no ESP32 radio. Use Your Device → WiFi Setup below to set real credentials over local USB. The game never sends WiFi credentials or makes a simulated connection.', () => { this.close(); document.querySelector('[data-hardware="wifi"]').click(); document.querySelector('#device').scrollIntoView(); }, 'Open Device Tools'); return;
      case 'SD Card': rows = [
        row('Back Up Save', () => this.download(), 'Download Browser JSON / Not Device SD'),
        row('Restore Save', () => {
          this.report('Choose a browser JSON backup. Your device SD card is not accessed.');
          this.root.querySelector('#browser-save-file').click();
        }, 'Choose Browser JSON / Not Device SD'),
        row('Storage', null, 'Browser Files / Device SD Requires Hardware'),
      ]; break;
      case 'Battery': rows = [
        // Two groups, in the order a player actually thinks about them: what
        // the battery is doing now, then how the screen behaves. Deep sleep has
        // its own submenu because it is a separate mode that stops the pet, not
        // another display preference. Mirrors MENU_POWER in game.c.
        row('Battery', null, 'Hardware Telemetry Unavailable'),
        row('Runtime', null, this.runtimeText()),
        ...[
          ['Power Mode','power_mode',POWER_MODES],
          ['Screen Timeout','screen_timeout',SCREEN_TIMEOUTS],
        ].map(([label,key,values]) => row(label, () => {
          this.state[key]=(this.state[key]+1)%values.length;this.persist();this.open('Battery',page);
        }, `${values[this.state[key]]} / Device Preference`)),
        row('Deep Sleep', () => this.open('Deep Sleep',0), 'Sleep After Idle and Wake Interval', { submenu: true }),
      ]; break;
      case 'Deep Sleep': rows = [
        ...[
          ['Sleep After Idle','deep_sleep_after',SLEEP_DELAYS],
          ['Wake Interval','rtc_wake',RTC_WAKES],
        ].map(([label,key,values]) => row(label, () => {
          this.state[key]=(this.state[key]+1)%values.length;this.persist();this.open('Deep Sleep',page);
        }, `${values[this.state[key]]} / Device Preference`)),
        row('Sleep Now', null, 'Device Only / Right Button Wakes'),
        row('Pet Ages While Asleep', null, 'RTC Catch-Up on Wake / No Radio'),
      ]; break;
      case 'Help Guide': rows=HELP_TOPICS.map((topic,index)=>row(topic.name,()=>{
        this.close();this.modal=this.panel(topic.name,()=>this.open('Help Guide',Math.floor(index/4)));
        this.modal.name='Help Topic';this.copy(topic.text,()=>this.open('Help Guide',Math.floor(index/4)),'Back to Help');
      },'Tap to Read',{icon:`ui_${topic.icon}`}));break;
      case 'Firmware Update':this.copy('Browser progress updates with this website.\n\nTo update a real board, use Your Device below. Choose Update to preserve the pet, and export a backup first.\n\nOn the device: Settings > About > Firmware Update checks GitHub Latest. Failed downloads do not select an incomplete application.',()=>{
        this.close();document.querySelector('[data-hardware="flash"]').click();document.querySelector('#device').scrollIntoView();
      },'Open USB Updater');return;
      case 'Display': rows = [
        row('Brightness', null, 'Controlled by Your Screen'),
        row('Screen Rotation', () => this.rotate(), this.rotationName(), { submenu: true }),
        row('Double-Tap Lights',()=>{
          this.cancelTap();
          this.lifeAction(s=>{s.life.light_gesture_disabled=s.life.light_gesture_disabled?0:1;return true;});
          this.open('Display');
        },'Allow Enclosure Light Gesture',{checkbox:true,selected:!this.state.life.light_gesture_disabled}),
        row('Dim Enclosure',()=>{
          this.lifeAction(s=>{s.lights_dimmed=s.lights_dimmed?0:1;return true;});
          this.open('Display');
        },'Manual Light Control / Always Available',{checkbox:true,selected:!!this.state.lights_dimmed}),
        row('Day and Night',()=>this.open('Calendar',1),'Subtle Background Tint / Calendar Settings'),
        row('Weather',()=>this.open('Calendar',1),'Optional Tiny Edge Particles'),
      ]; break;
      case 'About': rows=[
        row('Browser Edition',null,'Spidarium / Local Progress'),
        row('Hardware',null,'Waveshare ESP32-S3 / 480x480'),
        row('License',null,'Personal, Non-Commercial'),
        row('Firmware Update',()=>this.open('Firmware Update'),'USB Updater / Real Device'),
      ];break;
      default: throw new Error(`Unknown menu: ${name}`);
    }
    this.list(rows, page, ['Treats', 'Backgrounds', 'Borders', 'Decor', 'Gifts', 'Achievements', 'Prey Species'].includes(name) ? 3 : 2);
  }
  previewGift(id, status, page) {
    const item = E.GIFTS[id];
    this.close();
    this.modal = this.panel(item.name, () => this.open('Gifts', page));
    this.modal.name = 'Gift Preview';
    this.refreshHUD();
    this.list([{label:item.name, detail:`${status}\n${item.effect}`, icon:item.asset,
      click:()=>this.chooseGift(id)}], 0);
  }
  back(name) {
    if (name === 'Menu' || name === 'Gifts') this.close();
    else if (name === 'Soundtrack') this.open('Sound');
    else if (name === 'Firmware Update') this.open('About');
    else if (name === 'Help Guide') this.open('Menu');
    else if (name === 'Photo Storage') this.open('Photo Album');
    else if (name === 'Generations') this.open('Menu');
    else if (['Daily','Treats','Your Spider','Today','Diary','Training','Photo Album','Calendar'].includes(name)) this.open('Menu');
    else if (name.startsWith('Trick ')) this.open('Training');
    else if (name === 'Decor Item') this.open('Decor');
    else if (name === 'Deep Sleep') this.open('Battery');
    else this.open(['Backgrounds', 'Borders', 'Achievements', 'Prey Species', 'Decor', 'Settings'].includes(name) ? 'Menu' : 'Settings');
  }
  generationConfirm(kind, index=0, step=1) {
    const retire=kind==='retire';
    this.close();this.modal=this.panel(retire?'Retire Spider?':'Resume Spider?',()=>this.open('Generations'));
    this.modal.name='Generation Confirmation';
    this.copy(retire
      ? 'Optional: archive this healthy adult, its training and companion memories. Start a hatchling with the same color presets. Shared Silk, inventory and habitat stay unchanged. Only three archives; none are evicted.'
      : 'Swap the current spider, training and companion memories with this archive. Both spiders are kept. Shared Silk, inventory and habitat stay unchanged.',
    ()=>{
      if(step===1){this.generationConfirm(kind,index,2);return;}
      const done=this.lifeAction(s=>retire?E.lifeRetire(s,true):E.lifeResume(s,index,true),
        retire?'Adult archived and hatchling saved. No shared items lost.':'Spider swap saved. Both spiders are kept.');
      if(done)this.open('Generations');
    },step===1?'Continue':retire?'Confirm Retirement':'Confirm Swap',
    {icon:retire?'ui_retire':'ui_resume',actionIcon:step===1?undefined:retire?'ui_retire':'ui_resume'});
    this.button(this.modal.panel,'Cancel',112,382,176,48,()=>this.open('Generations'),'ui_confirm_cancel');
  }
  async albumMenu() {
    const modal=this.modal;
    const storage=modal.name==='Photo Storage';
    const page=modal.page??0;
    const albumRows=(status,ready=false)=>[
      {label:'Take Photo',detail:'Save Playfield PNG / Eight Slots',icon:'ui_camera',
        enabled:ready&&!this.albumBusy,click:()=>this.capturePhoto()},
      {label:'Storage Status',detail:status,icon:'ui_status'},
      {label:'Photo Storage',detail:'View and Delete Saved Photos',icon:'ui_album',
        click:()=>this.open('Photo Storage')},
    ];
    this.list(storage?[{label:'Loading Photos',detail:'Browser IndexedDB',icon:'ui_photo'}]:
      albumRows('Loading Storage...'),0,2);
    modal.page=page;
    try {
      const photos=await this.album.list();
      if(this.modal!==modal)return;
      modal.overlay.remove();this.modal=this.panel(storage?`Photo Storage / ${photos.length}/8`:'Photo Album',
        ()=>this.open(storage?'Photo Album':'Menu'));
      this.modal.name=storage?'Photo Storage':'Photo Album';
      const rows=storage?photos.map(photo=>({label:photo.name||'Spider Photo',detail:new Date(photo.created).toLocaleString(),
          icon:'ui_photo',click:()=>this.viewPhoto(photo)}))
        :albumRows(`${photos.length}/8 Photos / Browser Storage`,photos.length<8);
      if(!rows.length)rows.push({label:'No Saved Photos',detail:'Take Photo in Photo Album',icon:'ui_photo'});
      this.list(rows,Math.min(page,Math.floor((rows.length-1)/4)),2);
    } catch(error) {
      if(this.modal!==modal)return;
      this.report(`Album could not be loaded: ${error.message}`,true);
      modal.overlay.remove();this.modal=this.panel(storage?'Photo Storage':'Photo Album',
        ()=>this.open(storage?'Photo Album':'Menu'));this.modal.name=storage?'Photo Storage':'Photo Album';
      this.list(storage?[{label:'Photos Unavailable',detail:'No Photos Changed / See Album Status',icon:'ui_photo'}]:
        albumRows(`Storage Unavailable: ${error.message}`),0,2);
    }
  }
  async capturePhoto() {
    if(this.albumBusy)return;
    this.albumBusy=true;
    try {
      this.draw();
      await this.album.capture(this.canvas,this.state.name||'Spider');
      this.report('Photo saved to the browser album. Device SD card was not changed.');
    } catch(error) {this.report(`Photo was not saved: ${error.message}`,true);}
    finally {
      this.albumBusy=false;
      if(this.modal?.name==='Photo Album')this.open('Photo Album');
    }
  }
  viewPhoto(photo) {
    this.close();this.modal=this.panel('Browser Photo',()=>this.open('Photo Storage'));this.modal.name='Photo View';
    const modal=this.modal;
    this.photoURL=URL.createObjectURL(photo.blob);
    const image=box(element('img',''),58,78,284,284);image.src=this.photoURL;
    image.alt=`${photo.name||'Spider'} / ${new Date(photo.created).toLocaleString()}`;
    image.onerror=()=>{if(this.modal===modal)this.report('Stored photo could not be displayed.',true);};
    this.modal.panel.append(image);
    this.button(this.modal.panel,'Delete Photo',100,374,200,48,()=>this.confirmDeletePhoto(photo),'ui_delete_photo');
  }
  confirmDeletePhoto(photo) {
    this.close();this.modal=this.panel('Delete Photo?',()=>this.viewPhoto(photo));this.modal.name='Photo Delete';
    const modal=this.modal;
    const text=box(element('p','copy-text'),24,94,352,180);
    text.textContent='Delete this photo permanently? Only this image is removed. Your spider, pet backup and other photos stay safe.';
    this.modal.panel.append(text);
    let deleting=false;
    this.button(this.modal.panel,'Confirm Delete',70,290,260,52,async()=>{
      if(deleting)return;
      deleting=true;
      try {
        await this.album.remove(photo.id);
        this.report('Photo deleted. Album slot available.');
        if(this.modal===modal)this.open('Photo Storage');
      } catch(error) {
        deleting=false;this.report(`Photo deletion failed: ${error.message}`,true);
      }
    },'ui_delete_photo');
    this.button(this.modal.panel,'Cancel',112,370,176,48,()=>this.viewPhoto(photo),'ui_confirm_cancel');
  }
  requestDetail(slot) {
    const request=this.state.companion.requests[slot];
    const text=E.companionRequestText(this.state,slot,{species:E.SPECIES,gifts:E.GIFTS});
    this.close();this.modal=this.panel('Optional Request',()=>this.open('Today'));this.modal.name='Request Detail';
    const instructions=request.kind===4?'Finish a visit to the placed gift. Buying or placing does not count.':
      request.kind===3?'Catch this prey with your own pounce, not unattended hunting.':
      request.kind===6?'Clean a real mess. Spotless cleaning does not count.':
      request.kind===2?'Finish any minigame. Starting or cancelling does not count.':
      'Pet three times, with a little pause between taps.';
    this.copy(`${text.title}\n${text.detail}\n\n${instructions}\n\nNo deadline pressure, penalties or claim button.`,
      request.state===0?()=>{
        E.companionDismiss(this.state,slot);this.persist();this.open('Today');
      }:()=>this.open('Today'),request.state===0?'Not Today':'Back to Today',
      {icon:text.icon,actionIcon:request.state===0?'ui_dismiss':'ui_today'});
    this.refreshHUD();
  }
  memoryText(memory) {
    return E.companionMemoryText(memory,{treats:E.TREATS,gifts:E.GIFTS,achievements:E.ACHIEVEMENTS});
  }
  memoryDetail(index,page=0) {
    const memory=this.state.companion.diary[index];
    if(!memory)return;
    const text=this.memoryText(memory);
    this.close();this.modal=this.panel('Diary Memory',()=>this.open('Diary',page));this.modal.name='Diary Detail';
    this.copy(`${text.title}\n\n${text.detail}\n\nKeep up to eight pinned memories. The diary keeps 64 memories, replacing the oldest unpinned one when full.`,()=>{
      if(!E.companionPin(this.state,index)) {this.report('Eight memories are already pinned. Unpin one first.',true);return;}
      this.persist();this.memoryDetail(index,page);
    },memory.pinned?'Unpin':'Pin',{icon:text.icon,actionIcon:memory.pinned?'ui_unpin':'ui_pin'});
    this.refreshHUD();
  }
  /** Today's subtitle: what is waiting for the player right now. */
  dailyDetail() {
    if (E.dailyAvailable(this.state)) return 'Check In Ready';
    if (E.dailyTaskReady(this.state)) return 'Task Complete';
    return `Check In Claimed / Streak ${this.state.daily_streak}`;
  }
  achievementDetail() {
    const earned = E.ACHIEVEMENTS.filter(item => this.state.achievements & item.bit).length;
    return `${earned} of ${E.ACHIEVEMENTS.length} Earned`;
  }
  speciesDetail() {
    const unlocked = E.SPECIES.filter(item => this.state.level >= item.unlock_level).length;
    return `${unlocked} of ${E.SPECIES.length} Unlocked`;
  }
  copy(text, action, label = 'Done', {icon,actionIcon} = {}) {
    const content=element('div', 'd-copy');
    if(icon) {
      const image=element('img','');image.src=asset(icon);image.alt='';
      Object.assign(image.style,{width:'36px',height:'36px',float:'left',marginRight:'8px',imageRendering:'pixelated'});
      content.append(image);
    }
    content.append(element('span','',text));
    this.modal.panel.append(content);
    this.button(this.modal.panel, label, 60, 308, 280, 64, action ?? (() => this.back(this.modal.name)),actionIcon);
  }
  list(rows, page, columns = 0) {
    const grid = columns !== 0, size = grid ? columns * 2 : 3;
    const width = grid ? (376 - (columns - 1) * 8) / columns : 376;
    const pages = Math.max(1, Math.ceil(rows.length / size));
    page=clamp(page,0,pages-1);
    this.modal.page=page;
    rows.slice(page * size, page * size + size).forEach((item, i) => {
      const button = this.button(this.modal.panel, '', grid ? 12 + i % columns * (width + 8) : 12,
        grid ? 76 + Math.floor(i / columns) * 124 : 76 + i * 84,
        width, grid ? 116 : 76, item.click ?? (() => {}));
      button.replaceChildren(); button.classList.add('d-row');
      if (grid) {
        button.classList.add(columns === 3 ? 'd-shop-tile' : 'd-menu-tile');
        button.setAttribute('aria-label', `${item.label}${item.detail ? `: ${item.detail}` : ''}`);
      }
      button.disabled = item.enabled === false || (!item.click && !item.slider);
      if (item.selected) button.classList.add('selected');
      if (item.destructive) button.classList.add('destructive');
      if (item.detail) button.classList.add('has-detail');
      if (item.detail?.includes('\n')) button.classList.add('multi');
      if (item.icon) { button.classList.add('has-icon'); const img = element('img', ''); img.src = asset(item.icon); img.alt = ''; button.append(img); }
      button.append(element('span', 'row-label', item.label));
      if (item.detail) button.append(element('span', 'row-detail', columns === 3 ? item.detail.split('\n')[0] : item.detail));
      if (item.checkbox) {
        const check = element('input', ''); check.type = 'checkbox'; check.checked = item.selected; check.tabIndex = -1; check.setAttribute('aria-hidden', 'true');
        button.setAttribute('role', 'checkbox'); button.setAttribute('aria-checked', String(item.selected)); button.append(check);
      } else if (item.slider) {
        button.classList.add('has-slider');
        const { value, min, max, step, change } = item.slider;
        const input = element('input', 'd-slider'); input.type = 'range';
        input.min = String(min); input.max = String(max); input.step = String(step); input.value = String(value);
        input.setAttribute('aria-label', item.label);
        input.addEventListener('input', () => {
          change(Number(input.value));
          const detail = button.querySelector('.row-detail');
          if (detail) detail.textContent = `${input.value}%`;
          if (grid) button.setAttribute('aria-label', `${item.label}: ${input.value}%`);
        });
        const drag = event => {
          const rect = input.getBoundingClientRect(), rotation = (this.rotation ?? 0) & 3;
          const vertical = rotation % 2 !== 0;
          const length = vertical ? rect.height : rect.width;
          const scale = length / input.offsetWidth;
          const inset = 8 * scale;
          let position = vertical ? event.clientY - rect.top : event.clientX - rect.left;
          if (rotation === 2 || rotation === 3) position = length - position;
          const fraction = clamp((position - inset) / (length - 2 * inset), 0, 1);
          const next = clamp(min + Math.round(fraction * (max - min) / step) * step, min, max);
          if (Number(input.value) !== next) {
            input.value = String(next);
            input.dispatchEvent(new Event('input', { bubbles: true }));
          }
        };
        input.addEventListener('pointerdown', event => {
          if (!event.isPrimary || event.button !== 0) return;
          event.preventDefault(); event.stopPropagation();
          input.focus({ preventScroll: true });
          input.setPointerCapture(event.pointerId);
          drag(event);
        });
        input.addEventListener('pointermove', event => {
          if (input.hasPointerCapture(event.pointerId)) drag(event);
        });
        input.addEventListener('pointerup', event => {
          if (!input.hasPointerCapture(event.pointerId)) return;
          drag(event); input.releasePointerCapture(event.pointerId);
        });
        input.addEventListener('pointercancel', event => {
          if (input.hasPointerCapture(event.pointerId)) input.releasePointerCapture(event.pointerId);
        });
        input.addEventListener('click', event => event.stopPropagation());
        button.append(input);
        button.disabled = false;
      } else if (item.selected) button.append(element('span', 'row-marker', '✓'));
    });
    const name = this.modal.name;
    // Arrow buttons, matching the corner Back control. Text labels made the
    // pager read as content rather than chrome.
    this.button(this.modal.panel, '‹', 12, 332, 96, 56, () => this.open(name, page - 1)).disabled = page === 0;
    this.button(this.modal.panel, '›', 292, 332, 96, 56, () => this.open(name, page + 1)).disabled = page === pages - 1;
    this.modal.panel.append(element('span', 'd-pages', `${page + 1}/${pages}`));
  }
  // The browser has no PMIC, so runtime is stated as unavailable rather than
  // invented. The device reports a real estimate from its battery gauge.
  runtimeText() { return 'Needs Device Battery Gauge'; }
  rotationName() { return ['Upright', 'Sideways Left', 'Upside Down', 'Sideways Right'][this.rotation ?? 0]; }
  rotate() {
    this.rotation = ((this.rotation ?? 0) + 1) & 3;
    this.controls.style.setProperty('--d-rotation', `${this.rotation * 90}deg`);
    this.open('Display');
  }
  newTank() {
    if (!confirm('Start a new browser spider? Download a backup first. The physical board is not touched.')) return;
    const state = E.freshState();
    if (!this.progress.reset(state)) return;
    this.state = state; this.started = false; this.setup();
  }
  setup() {
    this.close(); this.started = false;
    const overlay = element('div', 'd-overlay d-setup'); this.controls.append(overlay);
    this.modal = { overlay, name: 'Setup' }; this.refreshHUD();
    overlay.append(element('h2', '', 'A New Spider'));
    this.button(overlay, this.state.name || 'Tap to Name Your Spider', 40, 102, 264, 56, () => this.nameInput());
    const preview = element('canvas', 'd-preview'); preview.width = preview.height = 96; overlay.append(preview);
    const pixels = preview.getContext('2d').createImageData(96, 96);
    renderSpider(pixels.data, this.spiderOptions()); preview.getContext('2d').putImageData(pixels, 0, 0);
    [['Body', 'bodyColour', BODY_COLOURS], ['Eyes', 'eyeColour', ACCENT_COLOURS], ['Legs', 'legColour', ACCENT_COLOURS], ['Fangs', 'fangColour', ACCENT_COLOURS]].forEach(([label, field, colors], i) => {
      overlay.append(box(element('div', 'd-palette-label', label), 40, 205 + i * 42, 60, 24));
      colors.forEach((color, index) => {
        const swatch = this.button(overlay, '', 100 + index * 38, 196 + i * 42, 32, 32, () => { this.state[field] = index; this.setup(); });
        swatch.classList.add('d-swatch'); swatch.style.background = `#${color.toString(16).padStart(6, '0')}`;
        swatch.setAttribute('aria-label', `${label} Color ${index + 1}`);
        if (this.state[field] === index) swatch.classList.add('selected');
      });
    });
    this.button(overlay, 'Start', 40, 374, 400, 56, () => {
      this.started = true; this.state.name ||= 'Spider'; this.state.setup_complete = true; this.close(); this.persist(); this.refreshHUD();
    });
  }
  nameInput() {
    this.close(); this.modal = this.panel('Spider Name', () => this.setup());
    const input = element('textarea', 'd-input'); input.maxLength = 15; input.value = this.state.name ?? ''; this.modal.panel.append(input);
    const keyboard = element('div', 'd-keyboard'); this.modal.panel.append(keyboard);
    let upper = false;
    const redraw = () => {
      keyboard.replaceChildren();
      ['qwertyuiop', 'asdfghjkl', 'zxcvbnm'].forEach(text => {
        const row = element('div', 'd-keyboard-row'); keyboard.append(row);
        [...text].forEach(char => { const key = element('button', 'd-key', upper ? char.toUpperCase() : char); key.type = 'button'; key.onclick = () => {
          input.setRangeText(upper ? char.toUpperCase() : char, input.selectionStart, input.selectionEnd, 'end');
          input.value = input.value.slice(0, 15); input.focus();
        }; row.append(key); });
      });
      const row = element('div', 'd-keyboard-row'); keyboard.append(row);
      for (const [name, action] of [['Shift', () => { upper = !upper; redraw(); }], ['Space', () => input.setRangeText(' ', input.selectionStart, input.selectionEnd, 'end')], ['⌫', () => input.setRangeText('', Math.max(0, input.selectionStart - (input.selectionStart === input.selectionEnd ? 1 : 0)), input.selectionEnd, 'end')]]) {
        const key = element('button', 'd-key', name); key.type = 'button'; key.style.width = '116px'; key.onclick = action; row.append(key);
      }
    }; redraw();
    this.button(this.modal.panel, 'Save Name', 60, 352, 280, 40, () => {
      const text = input.value.trim();
      if (!text || new TextEncoder().encode(text).length > 15) { this.report('Name must be 1–15 UTF-8 bytes.', true); return; }
      this.state.name = text; this.setup();
    });
  }
  chooseGift(id) {
    const result = E.purchaseGift(this.state, id);
    if (!result) { this.report('Gift purchase is unavailable: check its level and Silk cost.', true); return; }
    this.close(); this.giftMoving = false; this.giftKind = id;
    this.startPlacement(true,id,null,240,240);this.persist();
  }
  beginGiftMove() {
    if (!this.state.gift.used) { this.report('No gift is placed.',true); return; }
    this.close(); this.giftKind = this.state.gift.kind; this.giftMoving = true;
    this.startPlacement(true,this.giftKind,null,this.state.gift.x,this.state.gift.y);
  }
  beginDecorPlacement(id) {
    this.close(); this.decorKind = id; this.decorMoveSlot = null;
    this.startPlacement(false,id,null,240,240);
  }
  beginDecorMove(slot) {
    const item=this.state.decor[slot];
    if (!item || item.kind === E.DECOR_EMPTY) {this.report('That decor is no longer placed.',true);return;}
    this.close(); this.decorKind = null; this.decorMoveSlot = slot;
    this.startPlacement(false,item.kind,slot,item.x,item.y);
  }
  startPlacement(gift,kind,slot,x,y){
    this.cancelLongPress();clearTimeout(this.petTapTimer);this.petTapTimer=null;
    this.aiming=false;E.cancelAim(this.state);this.lastTap=null;this.hud=false;
    this.placement={gift,kind,slot,x,y};this.placementPointer=null;
    if(!gift){
      this.placement.blocking=slot!==null?this.state.decor[slot].blocking:E.DECOR_KINDS[kind].blocking;
      this.placement.layer=slot!==null?E.decorLayerIndex(this.state,slot):
        E.decorLayerSlots(this.state,this.placement.blocking).length;
    } else {
      /* The gift is always walked over, so there is no path to seed. */
      this.placement.blocking=0;
      this.placement.layer=0;
    }
    this.refreshHUD();this.report('Slide to position. Done saves; Cancel leaves placement unchanged.');
    if(this.notice)this.notice.hidden=true;
  }
  positionPlacement(x,y){
    const p=this.placement;
    const radius=p.gift?0:E.DECOR_KINDS[p.kind].radius;
    p.x=Math.round(clamp(x,34+radius,446-radius));
    p.y=Math.round(clamp(y,34+radius,446-radius));
  }
  releasePlacementPointer(id){
    if(this.placementPointer!==id)return;
    if(this.canvas.hasPointerCapture(id))this.canvas.releasePointerCapture(id);
    this.placementPointer=null;
  }
  cancelPlacement(notify=true){
    if(this.placementPointer!==null)this.releasePlacementPointer(this.placementPointer);
    this.placement=null;this.giftKind=null;this.giftMoving=false;this.decorKind=null;this.decorMoveSlot=null;
    this.refreshHUD();
    if(notify)this.report('Placement Cancelled.');
  }
  commitPlacement(){
    const p=this.placement;if(!p)return;
    let slot=p.slot;
    const ok=p.gift?(this.giftMoving?E.moveGift(this.state,p.x,p.y):E.placeGift(this.state,p.kind,p.x,p.y)):
      slot!==null?E.moveDecor(this.state,slot,p.x,p.y):(slot=E.placeDecor(this.state,p.kind,p.x,p.y))>=0;
    if(!ok){this.report('Placement could not be saved. Check ownership and available space.',true);return;}
    if(p.gift){
      /* The gift is always walked over; it has no path control any more. */
      this.state.gift_path=0;
    } else {
      this.state.decor[slot].blocking=p.blocking;
      if(!E.setDecorLayer(this.state,slot,p.layer)){this.report('Layer could not be saved.',true);return;}
    }
    this.cancelPlacement(false);this.persist();this.report('Placement Saved.');
  }
  togglePlacementBlocking(){
    const p=this.placement;
    if(!p)return;
    this.setPlacementPath((p.blocking+1)%3);
  }
  setPlacementPath(path){
    const p=this.placement;
    if(!p)return;
    if(!Number.isInteger(path)||path<0||path>2){this.report('Invalid path mode.',true);return;}
    if(path===p.blocking)return;
    if(!p.gift&&E.decorCanopy(p.blocking)!==E.decorCanopy(path))
      p.layer=E.decorLayerSlots(this.state,path,p.slot??-1).length;
    p.blocking=path;this.refreshHUD();
  }
  changePlacementLayer(step){
    const p=this.placement;if(!p||p.gift)return;
    const last=E.decorLayerSlots(this.state,p.blocking,p.slot??-1).length;
    p.layer=clamp(p.layer+step,0,last);this.refreshHUD();
  }
  putAwayPlacement(){
    const p=this.placement;
    if(!p||(!this.giftMoving&&p.slot===null))return;
    const ok=p.gift?E.removeGift(this.state):E.removeDecor(this.state,p.slot);
    if(!ok){this.report('Item could not be put away.',true);return;}
    this.cancelPlacement(false);this.persist();this.report('Item Put Away / Ownership Kept.');
  }
  play() {
    this.close(); let kind = Math.floor(Math.random() * 3);
    if (kind === this.lastGame) kind = (kind + 1 + Math.floor(Math.random() * 2)) % 3;
    this.lastGame = kind;
    const panel = this.panel(GAME_NAMES[kind], () => this.close());
    this.game = { ...panel, engine: new Minigame(kind, performance.now()), paid: false };
    this.game.prompt = element('div', 'd-game-prompt'); panel.panel.append(this.game.prompt);
    if (kind === 0) {
      this.game.target = box(element('img', 'd-game-target'), 12, 80, 48, 48);
      this.game.target.alt = ''; panel.panel.append(this.game.target); this.game.prompt.style.left = '72px';
    }
    this.game.bar = element('div', 'd-time'); this.game.bar.append(element('span', '')); panel.panel.append(this.game.bar);
    this.game.tiles = Array.from({ length: kind === 2 ? 1 : 4 }, (_, i) => {
      const button = this.button(panel.panel, '', kind === 2 ? 12 : 12 + i % 2 * 194, kind === 2 ? 148 : 148 + Math.floor(i / 2) * 112, kind === 2 ? 376 : 182, kind === 2 ? 212 : 100, () => this.game?.engine.answerAt(i, performance.now()));
      button.classList.add('d-tile'); return button;
    });
    this.refreshHUD();
  }
  gameFrame(now) {
    const game = this.game; game.engine.tick(now); const g = game.engine;
    if (g.finished) {
      if (game.paid) return; game.paid = true;
      const reward = E.minigameReward(this.state, g.kind, { score: g.score, rounds: 8 });
      this.persist(); game.overlay.remove(); this.game = null;
      this.modal = this.panel('Play Complete', () => this.close()); this.modal.name = 'Play Complete';
      this.copy(`Score ${g.score}/8\n\n+${reward.xp} XP / +${reward.silk} Silk\nMood +${E.CONFIG.PLAY_MOOD_GAIN} / Energy -${E.CONFIG.PLAY_ENERGY_COST}`, () => this.close());
      this.modal.panel.querySelector('.d-copy').classList.add('d-result'); return;
    }
    game.prompt.textContent = g.prompt(E.SPECIES.map(item => title(item.name)));
    if (game.target) {
      const src = asset(`prey_${E.SPECIES[g.bug].key}_walk_0`);
      if (game.target.src !== src) game.target.src = src;
    }
    game.bar.firstChild.style.width = `${g.remaining(now) * 100}%`;
    game.tiles.forEach((button, i) => {
      button.disabled = g.pending || g.preview;
      if (g.kind === 0) {
        const item = E.SPECIES[g.bugs[i]];
        const key = `${item.key}_${g.round}`; if (button.dataset.bug !== key) {
          button.dataset.bug = key; button.replaceChildren();
          const image = element('img', ''); image.src = asset(`prey_${item.key}_walk_0`); image.alt = title(item.name);
          button.append(image);
        }
        button.style.background = '#24405a';
      } else if (g.kind === 1) {
        button.style.background = COLOR_RGB[g.colors[i]];
        button.lastChild.textContent = g.preview ? '...' : COLOR_NAMES[g.colors[i]];
      } else {
        button.style.background = g.ready ? '#296e55' : g.feints >= 2 ? '#b94646' : g.feints ? '#846421' : '#24405a';
        button.lastChild.textContent = g.ready ? 'Tap!' : g.feints >= 2 ? 'NOT YET' : g.feints ? 'HOLD' : 'WAIT';
      }
    });
  }
  point(event) {
    const rect = this.canvas.getBoundingClientRect(); return { x: (event.clientX - rect.left) * 480 / rect.width, y: (event.clientY - rect.top) * 480 / rect.height };
  }
  pointerDown(event) {
    this.screen.focus({ preventScroll: true });
    if (!this.started || this.modal || this.game || this.hud) {
      this.tapPointer=null;this.pointerOrigin=null;this.cancelLongPress();this.cancelTap();return;
    }
    if(!this.placement&&this.tapPointer!=null&&this.tapPointer!==event.pointerId)return;
    const p = this.point(event);
    this.pointerOrigin = p; this.pointerConsumed = false;
    void this.audio.unlock(this.state.sound);
    if(this.placement){
      if(this.placementPointer!==null)return;
      this.pointerConsumed=true;this.placementPointer=event.pointerId;
      this.cancelTap();
      const preview=this.placement,reach=preview.gift?40:E.decorIsClimbStructure(preview.kind)
      ?E.DECOR_KINDS[preview.kind].radius:E.DECOR_KINDS[preview.kind].radius*2+12;
      const grabbed=Math.abs(p.x-preview.x)<=reach&&Math.abs(p.y-preview.y)<=reach;
      this.placementOffset={x:grabbed?preview.x-p.x:0,y:grabbed?preview.y-p.y:0};
      this.positionPlacement(p.x+this.placementOffset.x,p.y+this.placementOffset.y);
      this.canvas.setPointerCapture(event.pointerId);
      return;
    }
    this.tapPointer=event.pointerId;this.tapStarted=performance.now();this.tapMoved=false;
    this.canvas.setPointerCapture(event.pointerId);
    /* Hold furniture to edit directly over the playfield. */
    if (this.decorKind === null && this.decorMoveSlot === null && this.giftKind === null) {
      const slot = E.decorHitTest(this.state, p.x, p.y);
      const gift = E.giftHitTest(this.state,p.x,p.y);
      if (slot >= 0 || gift) {
        this.longPress = setTimeout(() => {
          this.longPress = null; this.aiming = false; E.cancelAim(this.state);
          this.cancelTap();this.tapPointer=null;
          this.pointerConsumed = true;
          if(gift&&!(slot>=0&&E.decorCanopy(this.state.decor[slot].blocking)))this.beginGiftMove();
          else this.beginDecorMove(slot);
          this.placementPointer=event.pointerId;
          this.placementOffset={x:this.placement.x-p.x,y:this.placement.y-p.y};
          this.canvas.setPointerCapture(event.pointerId);
        }, 550);
      }
    }
    this.aiming = E.beginAim(this.state, p.x, p.y) !== false;
    if (this.aiming) this.canvas.setPointerCapture(event.pointerId);
  }
  cancelLongPress() {
    if (this.longPress) { clearTimeout(this.longPress); this.longPress = null; }
  }
  cancelTap() {
    clearTimeout(this.petTapTimer);this.petTapTimer=null;this.lastTap=null;
  }
  pointerCancel(event) {
    this.releasePlacementPointer(event.pointerId);
    this.aiming=false;this.tapPointer=null;this.pointerOrigin=null;this.tapMoved=true;
    this.cancelLongPress();this.cancelTap();E.cancelAim(this.state);
  }
  pointerMove(event) {
    const p = this.point(event);
    if(this.placement){
      if(this.placementPointer===event.pointerId)
        this.positionPlacement(p.x+this.placementOffset.x,p.y+this.placementOffset.y);
      return;
    }
    if(this.tapPointer!=null&&event.pointerId!=null&&event.pointerId!==this.tapPointer)return;
    if (this.pointerOrigin && Math.hypot(p.x-this.pointerOrigin.x,p.y-this.pointerOrigin.y) > (this.aiming?17:24)) {
      this.cancelLongPress();this.tapMoved=true;this.cancelTap();
    }
    if (this.aiming) { const p = this.point(event); E.moveAim(this.state, p.x, p.y); }
  }
  pointerUp(event) {
    if(!this.placement&&this.tapPointer!=null&&event.pointerId!==this.tapPointer)return;
    this.cancelLongPress();
    if(this.placement){
      this.pointerMove(event);
      this.releasePlacementPointer(event.pointerId);
      return;
    }
    if (this.pointerConsumed || this.modal || this.game || this.hud) {
      this.tapPointer=null;this.pointerOrigin=null;this.cancelTap();return;
    }
    const point = this.point(event), now = performance.now();
    this.tapPointer=null;
    const tapping = this.pointerOrigin && !this.tapMoved && now-this.tapStarted<550 &&
      Math.hypot(point.x-this.pointerOrigin.x,point.y-this.pointerOrigin.y) <= (this.aiming?17:24);
    this.pointerOrigin=null;
    if(tapping){
      if(!this.state.life.light_gesture_disabled && this.lastTap && now-this.lastTap.time<=450 &&
          Math.hypot(point.x-this.lastTap.x,point.y-this.lastTap.y)<=48){
        this.cancelTap();
        this.aiming=false;E.cancelAim(this.state);
        this.state.lights_dimmed=this.state.lights_dimmed?0:1;
        this.persist();this.report(this.state.lights_dimmed?'Enclosure Lights Dimmed.':'Enclosure Lights Restored.');
        return;
      }
      this.lastTap={...point,time:now};
    }else this.cancelTap();
    if (!this.aiming) {
      /* A tap that did not start an aim is a pet if it landed on the spider.
       * The engine enforces the hit radius and the cooldown. */
      const p = this.point(event);
      if(!tapping)return;
      clearTimeout(this.petTapTimer);
      this.petTapTimer=setTimeout(()=>{
        this.petTapTimer=null;
        if(!this.modal&&!this.game&&!this.hud&&E.pet(this.state,p.x,p.y)){
          this.persist();this.report('Spider Petted.');
        }
      },475);
      return;
    }
    this.aiming = false;
    const p = this.point(event); E.endAim(this.state, p.x, p.y); this.persist();
  }
  key(event) {
    if (event.target instanceof HTMLTextAreaElement || event.target instanceof HTMLInputElement) return;
    if(this.placement){
      const step={ArrowLeft:[-10,0],ArrowRight:[10,0],ArrowUp:[0,-10],ArrowDown:[0,10]}[event.key];
      if(step){event.preventDefault();this.positionPlacement(this.placement.x+step[0],this.placement.y+step[1]);}
      else if(event.key==='Enter'){event.preventDefault();this.commitPlacement();}
      else if(event.key==='Escape'){event.preventDefault();this.cancelPlacement();}
      return;
    }
    if (event.key.toLowerCase() === 'm' && this.started && !this.modal && !this.game) { event.preventDefault(); this.cancelTap(); this.hud = !this.hud; this.refreshHUD(); return; }
    if (this.modal || this.game || !this.started || this.hud) return;
    const s = this.state;
    const directions = { ArrowLeft: [-30, 0], ArrowRight: [30, 0], ArrowUp: [0, -30], ArrowDown: [0, 30] };
    if (directions[event.key]) {
      event.preventDefault(); this.keyboardAim ??= { x: s.spider_x, y: s.spider_y };
      const [x, y] = directions[event.key]; this.keyboardAim.x = clamp(this.keyboardAim.x + x, 40, 440); this.keyboardAim.y = clamp(this.keyboardAim.y + y, 40, 440);
      E.beginAim(s, s.spider_x, s.spider_y); E.moveAim(s, this.keyboardAim.x, this.keyboardAim.y);
    } else if (event.code === 'Space') {
      event.preventDefault(); const p = this.keyboardAim ?? { x: s.spider_x + 80, y: s.spider_y };
      E.endAim(s, p.x, p.y); this.keyboardAim = null; this.persist();
    }
  }
  spiderOptions() {
    const s = this.state;
    let state = E.STATE_NAMES[s.state];
    // The petting reaction is a timed overlay, not a state: it forces a happy
    // wave for a moment without interrupting whatever the spider is doing.
    const reacting = (s.now_ms ?? 0) < (s._engine?.pet_react_until ?? 0);
    const performance=s._engine?.life_performance;
    const performing=performance && s.now_ms-performance.start<performance.duration;
    if(performing&&performance.trick===2)state='aim';
    // Idle life: the device's ambient.c gives the spider a breath, a blink and
    // a glance so it is never perfectly still. Same three here, from the clock.
    const nowMs = s.now_ms ?? 0;
    return { state, emote: reacting || performing ? 'happy' : s.gift_visit ? 'happy' : state === 'sleep' ? 'sleepy' : ['aim', 'stalk'].includes(state) ? 'alert' : state === 'sick' || s.mood < 18 ? 'sad' : state === 'eat' || state === 'play' || s.mood>=80 || s.bond>=75 ? 'happy' : 'none',
      heading: s.heading, phase: s.gait_phase, now: nowMs,
      walking: s.spider.walking, colour: s.bodyColour, pupilColour: s.eyeColour,
      legColour: s.legColour, fangColour: s.fangColour, sick: s.sick !== 0,
      blink: blinkAt(nowMs), glance: glanceAt(nowMs), breath: breathAt(nowMs) };
  }
  draw() {
    const s = this.state, ctx = this.ctx;
    ctx.fillStyle = '#101c2c'; ctx.fillRect(0, 0, 480, 480);
    const bg = this.image(`background_${s.theme}_0`); if (bg.complete && bg.naturalWidth) ctx.drawImage(bg, 0, 0, 480, 480);
    this.drawCalendar();
    // Dust motes: the cheapest way to make a still image read as a place with
    // air in it. Drawn above the background and below the decor, so the spider
    // and the furniture sit in front of them. Tinted from the theme's own dust
    // colour, which the device also uses.
    {
      const dust = dustColour(E.BACKGROUNDS.indexOf(s.theme));
      ctx.fillStyle = `rgba(${(dust >> 16) & 255},${(dust >> 8) & 255},${dust & 255},.42)`;
      for (const m of motesAt(s.now_ms ?? 0, { left: 8, top: 8, right: 472, bottom: 472 }))
        ctx.fillRect(m.x, m.y, m.size, m.size);
    }
    // Spores: a second, slower air layer that crosses the tank on both axes.
    // Round rather than square, and paler, so the two layers read as depth
    // instead of as one noisy field.
    {
      const web = webColour(E.BACKGROUNDS.indexOf(s.theme));
      ctx.fillStyle = `rgba(${(web >> 16) & 255},${(web >> 8) & 255},${web & 255},.30)`;
      for (const p of sporesAt(s.now_ms ?? 0, { left: 8, top: 8, right: 472, bottom: 472 })) {
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.size / 2, 0, Math.PI * 2);
        ctx.fill();
      }
    }
    // Web strands: anchored at the top corners and pivoting slowly. A strand
    // that translated would read as a sliding stick; pivoting reads as
    // something hanging and catching the air.
    {
      const web = webColour(E.BACKGROUNDS.indexOf(s.theme));
      ctx.strokeStyle = `rgba(${(web >> 16) & 255},${(web >> 8) & 255},${web & 255},.20)`;
      ctx.lineWidth = 1;
      for (const t of strandsAt(s.now_ms ?? 0, { left: 8, top: 8, right: 472, bottom: 472 })) {
        ctx.beginPath();
        ctx.moveTo(t.ax, t.ay);
        ctx.lineTo(t.ax + Math.cos(t.angle) * t.length, t.ay + Math.sin(t.angle) * t.length);
        ctx.stroke();
      }
    }
    if(s.border_id!==255){
      const border=this.image(`border_${E.BACKGROUNDS[s.border_id]}_0`);
      if(border.complete&&border.naturalWidth)ctx.drawImage(border,0,0,480,480);
    }
    this.drawDecor(false);
    /* The gift is always walked over, so it is always drawn in the floor pass. */
    if (s.gift?.used && !this.placement?.gift) {
      const gift = E.GIFTS[s.gift.kind], image = this.image(gift.asset);
      if (image.complete && image.naturalWidth) ctx.drawImage(image, s.gift.x - 32, s.gift.y - 32, 64, 64);
    }
    if(this.placement?.gift){
      const p=this.placement,size=p.gift?64:E.decorExtent(p.kind)*2;
      const image=this.image(p.gift?E.GIFTS[p.kind].asset:E.decorSprite(p.kind));
      if(image.complete&&image.naturalWidth)ctx.drawImage(image,p.x-size/2,p.y-size/2,size,size);
      ctx.strokeStyle='#f5c86b';ctx.lineWidth=2;ctx.strokeRect(p.x-size/2-2,p.y-size/2-2,size+4,size+4);
    }
    // Decor and gift particles: a fixed pool reassigned to whichever items are
    // on screen, so the cost is constant whether the tank holds one item or
    // thirteen. The style comes from the item's own kind, so nothing has to be
    // authored per decor entry and no artwork is needed -- a particle is a small
    // tinted dot. Drawn above the decor and below the spider, so the effect
    // belongs to the object rather than floating over the creature.
    {
      const emitters = [];
      for (const item of s.decor ?? []) {
        if (!item || item.kind === E.DECOR_EMPTY) continue;
        const def = E.DECOR_KINDS[item.kind];
        if (!def) continue;
        emitters.push({ x: item.x, y: item.y, style: emitForDecorKey(def.key) });
      }
      if (s.gift?.used) emitters.push({ x: s.gift.x, y: s.gift.y, style: emitForGift() });
      if (emitters.length) {
        const dust = dustColour(E.BACKGROUNDS.indexOf(s.theme));
        const rgb = `${(dust >> 16) & 255},${(dust >> 8) & 255},${dust & 255}`;
        for (const p of particlesAt(s.now_ms ?? 0, emitters)) {
          ctx.fillStyle = `rgba(${rgb},${(p.alpha * 0.5).toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.size / 2, 0, Math.PI * 2);
          ctx.fill();
        }
      }
    }
    for (const poop of s.poop ?? []) {
      if (poop.bug) {
        // Frass: smaller and cooler than a dropping, matching the device's
        // 7x4 pale speckle (0x8d8a7a / 0xb5b2a2 / 0xd8d5c6).
        ctx.fillStyle = '#8d8a7a'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y + 1, 3.5, 2, 0, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#b5b2a2'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y - .5, 2.5, 1.5, 0, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#d8d5c6'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y - 1.5, 1.5, 1, 0, 0, Math.PI * 2); ctx.fill();
      } else {
        ctx.fillStyle = '#4a3520'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y + 2, 6, 3.5, 0, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#6b4a2a'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y - 1, 4.5, 3, 0, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#7d5a33'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y - 3, 3, 2.5, 0, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#8f6a3d'; ctx.beginPath(); ctx.ellipse(poop.x, poop.y - 4, 1.5, 1.5, 0, 0, Math.PI * 2); ctx.fill();
      }
    }
    // Flies: a dropping left alone long enough attracts one, and it orbits the
    // mess that drew it. This is the neglect cycle made visible -- hygiene stops
    // being a number in a menu. Drawn above the droppings so a fly is never
    // hidden behind the mess that attracted it.
    {
      const now = s.now_ms ?? 0;
      for (const f of E.fliesAt(s)) {
        const a = now / 1000 * 1.7 * Math.PI * 2 + f.x * 0.01;
        const fx = f.x + Math.cos(a) * 9, fy = f.y - 6 + Math.sin(a * 1.7) * 3;
        ctx.fillStyle = '#2b2620'; ctx.beginPath(); ctx.ellipse(fx, fy, 2.5, 2, 0, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = '#9aa3a8'; ctx.beginPath(); ctx.ellipse(fx + 1, fy - 1.5, 1.5, 1, 0, 0, Math.PI * 2); ctx.fill();
      }
    }
    renderSpider(this.spiderPixels.data, this.spiderOptions()); this.spiderCtx.putImageData(this.spiderPixels, 0, 0);    // Life-stage size: a spiderling reads as small, an adult as full size.
    const stageScale = s.stage === 0 ? .75 : s.stage === 1 ? .875 : 1;
    const half = 48 * stageScale;
    // Petting bounce: one smooth rise and fall, matching the device's
    // PET_REACT_PX / PET_REACT_MS overlay.
    const reactLeft = (s._engine?.pet_react_until ?? 0) - (s.now_ms ?? 0);
    const lift = reactLeft > 0 ? -Math.round(9 * Math.sin((1 - reactLeft / E.CONFIG.PET_REACT_MS) * Math.PI)) : 0;
    const trickPerformance=s._engine?.life_performance;
    const t=trickPerformance?clamp((s.now_ms-trickPerformance.start)/trickPerformance.duration,0,1):1;
    // Climbing (2.5D): a climbable item lifts the spider's DRAW position only,
    // so steering and hit-testing stay in flat floor coordinates.
    const climb = E.decorClimbAt(s, s.spider_x, s.spider_y);
    const climbLift = -climb;
    if (climb > 0) {
      // The contact shadow FOLLOWS the body. Pinning it to the floor point put
      // it up to 84 px below the spider at full lift, which read as floating
      // above the structure rather than standing on it. A contact shadow
      // belongs where the object touches its surface, and while climbing that
      // surface is the top of the structure, directly under the feet.
      //
      // The +6 offset mirrors SHADOW_DROP in cspider.c: the shadow sits under
      // the abdomen rather than out at the leg tips, which left a visible gap
      // and read as the spider hovering.
      const shrink = climb / 3;
      ctx.save();
      ctx.globalAlpha = Math.max(.24, (150 - climb * 4) / 255);
      ctx.fillStyle = '#000';
      ctx.beginPath();
      ctx.ellipse(s.spider_x, s.spider_y + 6 + climbLift, Math.max(10, 26 - shrink), Math.max(5, 13 - shrink / 2), 0, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }
    if(trickPerformance&&t<1){
      ctx.save();ctx.translate(s.spider_x,s.spider_y+lift+climbLift-(trickPerformance.trick===0?Math.sin(t*Math.PI)*9:0));
      if(trickPerformance.trick===1)ctx.rotate(t*Math.PI*2);
      ctx.drawImage(this.spiderCanvas,-half,-half,half*2,half*2);
      ctx.restore();
    }else if (s.state === E.STATES.PERCH) {
      // Perched on a small prop: drawn from a lower angle, so the body's
      // forward axis compresses. The lift alone reads as a floating sprite;
      // the foreshortening plus the outward heading is what sells the height.
      const fs = E.PERCH_FORESHORTEN;
      ctx.save();
      ctx.translate(s.spider_x, s.spider_y + lift + climbLift);
      ctx.rotate(s.spider_heading);
      ctx.scale(fs, 1);
      ctx.rotate(-s.spider_heading);
      ctx.drawImage(this.spiderCanvas, -half, -half, half * 2, half * 2);
      ctx.restore();
    }else ctx.drawImage(this.spiderCanvas, s.spider_x - half, s.spider_y - half + lift + climbLift, half * 2, half * 2);
    if (s.state === E.STATES.AIM) {
      ctx.strokeStyle = '#f5c86b'; ctx.lineWidth = 2; ctx.setLineDash([6, 4]); ctx.beginPath();
      ctx.moveTo(s.spider_x, s.spider_y); ctx.lineTo(s.aim_x, s.aim_y); ctx.stroke(); ctx.setLineDash([]);
      ctx.beginPath(); ctx.arc(s.aim_x, s.aim_y, 15, 0, Math.PI * 2); ctx.stroke();
    }
    for (const prey of s.prey) {
      if (!prey.alive && !prey.dead) continue;
      const species = E.SPECIES[prey.species], frame = prey.frame ?? Math.floor(performance.now() / 200) % 2;
      const image = this.image(`prey_${species.key}_${prey.dead ? 'dead_0' : (prey.fleeing ? 'flee_' : 'walk_') + frame}`);
      if (image.complete && image.naturalWidth) {
        ctx.save();
        ctx.translate(prey.x, prey.y);
        ctx.rotate(prey.dead ? 0 : Math.round((prey.heading ?? 0) * 4 / Math.PI) * Math.PI / 4);
        ctx.drawImage(image, -24, -24, 48, 48);
        ctx.restore();
      }
      // Splat: a short burst of the species colour where the bug was caught.
      if (prey.dead && prey.dead_at) {
        const age = 2200 - Math.max(0, prey.dead_at - performance.now());
        if (age >= 0 && age < 700) {
          const t = age / 700, radius = 4 + t * 14, alpha = 1 - t;
          ctx.save();
          ctx.globalAlpha = alpha * .85;
          ctx.fillStyle = SPLAT_COLOURS[prey.species] || '#7a2f2f';
          for (let i = 0; i < 6; i++) {
            const a = (i / 6) * Math.PI * 2 + t * 1.2;
            const d = radius * (.55 + .45 * ((i * 37) % 11) / 11);
            ctx.beginPath();
            ctx.ellipse(prey.x + Math.cos(a) * d, prey.y + Math.sin(a) * d,
                        2.6 * (1 - t) + .8, 2.2 * (1 - t) + .6, a, 0, Math.PI * 2);
            ctx.fill();
          }
          ctx.restore();
        }
      }
    }
    this.drawDecor(true);
    if(s.lights_dimmed){
      // The overlay drifts a few percent so the tank reads as a room with a
      // light source rather than a photograph with a filter on it. Same drift
      // as the device's ambient_light_opa().
      ctx.fillStyle=`rgba(7,12,32,${(lightOpaAt(s.now_ms ?? 0)/255).toFixed(3)})`;
      ctx.fillRect(0,0,480,480);
    }
  }
  drawCalendar() {
    const s=this.state,c=E.lifeCalendar(),ctx=this.ctx;
    if(!c.reliable || !s.life)return;
    if(s.life.daynight) {
      // The tint changes COLOUR as well as opacity, so the tank reads as a
      // different time of day rather than as a slightly dirty screen. Mirrors
      // k_light_tints in game.c.
      ctx.fillStyle=({Morning:'rgba(255,217,160,0.063)',Afternoon:'rgba(0,0,0,0)',
        Evening:'rgba(255,154,77,0.118)',Night:'rgba(26,47,107,0.204)'})[c.phase];
      ctx.fillRect(0,0,480,480);
    }
    if(!s.life.weather)return;
    const time=(s.now_ms??0)/1000;
    ctx.fillStyle=c.season==='Winter'?'rgba(224,235,242,0.45)':'rgba(174,199,177,0.38)';
    for(let i=0;i<6;i++){
      const x=i%2?462+(i*3)%14:4+(i*3)%14,y=20+(i*59+time*8)%440;
      ctx.fillRect(Math.floor(x),Math.floor(y),2,c.season==='Winter'?2:4);
    }
  }
  drawDecor(canopy){
    const p=this.placement&&!this.placement.gift?this.placement:null,ctx=this.ctx;
    /* A walk-behind item may only cover the spider with its TOP half. The
     * sprite is centred on the item, so its centre line is the halfway point:
     * while the spider is above that line it is genuinely behind the item, and
     * once it is level with or below it the spider is standing in front of the
     * base and the item must drop behind it. Drawing the whole sprite in the
     * canopy layer made the spider vanish over the item's footprint, which
     * broke the perspective. */
    const spiderY=this.state.spider_y;
    const slots=E.decorLayerSlots(this.state,canopy?2:0,p?.slot??-1)
      .filter(slot=>!canopy||this.state.decor[slot].y>spiderY);
    const draw=item=>{
      const image=this.image(E.decorSprite(item.kind));
      // Large climb structures are authored at 192x192 and drawn 1:1; props are
      // 64x64 and drawn at their footprint size.
      const size=E.decorExtent(item.kind)*2;
      if(image.complete&&image.naturalWidth)ctx.drawImage(image,item.x-size/2,item.y-size/2,size,size);
    };
    for(let i=0;i<=slots.length;i++){
      if(p&&E.decorCanopy(p.blocking)===canopy&&i===p.layer)draw(p);
      if(i<slots.length)draw(this.state.decor[slots[i]]);
    }
    if(p&&canopy){
      const size=E.decorExtent(p.kind)*2;
      ctx.strokeStyle='#f5c86b';ctx.lineWidth=2;ctx.strokeRect(p.x-size/2-2,p.y-size/2-2,size+4,size+4);
    }
  }
  frame(now) {
    const elapsed = Math.min(.15, Math.max(0, (now - this.last) / 1000)); this.last = now;
    if (!document.hidden && this.started && !this.hud && !this.modal && !this.game &&
        this.giftKind === null && this.decorKind === null && this.decorMoveSlot === null) {
      this.accumulator += elapsed; while (this.accumulator + 1e-8 >= .05) { E.tick(this.state, .05); this.accumulator = Math.max(0,this.accumulator-.05); }
      this.companionTip();
    } else this.accumulator = 0;
    if (this.game) this.gameFrame(now);
    if (this.started) this.audio.tick(E.STATE_NAMES[this.state.state], now, this.state.sound && !document.hidden,
      this.state.volume, !this.hud && !this.modal && !this.game, !!this.state.music_enabled,
      this.state.music_volume, this.state.music_track);
    this.draw(); this.refreshHUD();
    if (now - this.lastSave >= 5000) { this.persist(); this.lastSave = now; }
    requestAnimationFrame(time => this.frame(time));
  }
  companionTip() {
    const c=this.state.companion;
    if(!c.initialized||this.placement||this.modal||this.game||this.hud||this.error)return;
    if(this.companionTipMode===2&&c.clock_mode===1) {
      this.companionTipMode=1;this.companionTipDay=c.request_day;return;
    }
    if(this.companionTipMode===c.clock_mode&&this.companionTipDay===c.request_day)return;
    if(this.notice&&!this.notice.hidden)return;
    this.companionTipMode=c.clock_mode;this.companionTipDay=c.request_day;
    this.report('One optional request / Menu > Today');
  }
  download() {
    try {
      const url = URL.createObjectURL(new Blob([JSON.stringify(this.state, null, 2)], { type: 'application/json' }));
      const link = element('a', ''); link.href = url; link.download = 'spidarium-browser-save.json';
      this.root.append(link);
      try { link.click(); }
      finally { link.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000); }
      this.report('Browser backup download requested. Check Downloads; this does not write the device SD card.');
    } catch (error) { this.report(`Browser backup failed: ${error.message}`, true); }
  }
  async screenshot() {
    try {
      this.draw();
      const blob = await new Promise((resolve, reject) => this.canvas.toBlob(value =>
        value ? resolve(value) : reject(new Error('PNG encoding failed')), 'image/png'));
      const url = URL.createObjectURL(blob);
      const link = element('a', '');
      link.href = url;
      link.download = `spidarium-${Date.now()}.png`;
      this.root.append(link);
      try { link.click(); }
      finally { link.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000); }
      this.report('Playfield PNG download requested. Check Downloads. Menus and HUD are excluded.');
    } catch (error) { this.report(`Screenshot failed: ${error.message}`, true); }
  }
  async importProgress() {
    try {
      const file = this.root.querySelector('#browser-save-file').files[0];
      if (!file) { this.report('Browser restore cancelled. Progress unchanged.'); return; }
      if (file.size > 100000) throw new Error('Browser backup is too large');
      const state = E.restoreSim(JSON.parse(await file.text()));
      if (!confirm('Replace browser progress with this backup? Your physical spider is not touched.')) {
        this.report('Browser restore cancelled. Progress unchanged.'); return;
      }
      if (!this.progress.reset(state)) return;
      this.state = state; this.started = state.setup_complete; this.close();
      if (!this.started) this.setup(); this.report('Browser progress restored.');
    } catch (error) { this.report(`Browser restore failed: ${error.message}`, true); }
    finally { this.root.querySelector('#browser-save-file').value = ''; }
  }
}
