import * as E from './sim.js';
import { renderSpider, BODY_COLOURS, ACCENT_COLOURS } from './spider-renderer.js';
import { LocalProgress } from './local-progress.js';
import { Minigame, GAME_NAMES, COLOR_NAMES, COLOR_RGB } from './minigames.js';
import { DeviceAudio } from './device-audio.js';

const asset = name => new URL(`../../assets/${name}.png`, import.meta.url).href;
// Splat colours per prey species, matching the native dead-sprite tints.
const SPLAT_COLOURS = ['#8a6a3a', '#4f6b2f', '#8a7a5a', '#3f3a2a', '#5a3a2a', '#a08a5a', '#6a5a3a', '#8a5a4a'];
const title = name => name.split(/[ _]+/).map(word => word[0].toUpperCase() + word.slice(1)).join(' ');
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
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
    this.images = new Map(); this.hud = false; this.modal = null; this.game = null; this.giftKind = null; this.decorKind = null; this.aiming = false; this.lastGame = -1;
    this.error = false;
    this.audio = new DeviceAudio((message, error) => this.report(message, error));
    let storage;
    try { storage = window.localStorage; }
    catch (error) { this.report(`Local storage unavailable: ${error.message}`, true); storage = { getItem() { throw error; }, setItem() { throw error; } }; }
    this.progress = new LocalProgress(storage, value => E.restoreSim(value), message => this.report(message, true));
    this.state = this.progress.load();
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
    this.canvas.addEventListener('pointercancel', () => { this.aiming = false; E.cancelAim(this.state); });
    this.screen.addEventListener('keydown', event => this.key(event));
    root.querySelector('#browser-save-file').onchange = () => this.importProgress();
    window.addEventListener('pagehide', () => this.persist());
    document.addEventListener('visibilitychange', () => { if (document.hidden) this.persist(); this.last = performance.now(); });
    this.last = performance.now(); this.accumulator = 0; this.lastSave = this.last;
    requestAnimationFrame(now => this.frame(now));
  }
  report(message, error = false) {
    if (this.progress?.blocked && !error) return;
    this.error = error; const status = this.root.querySelector('#sim-message');
    status.textContent = message; status.classList.toggle('error', error);
  }
  persist() { if (this.started) this.progress.save(this.state); }
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
    this.earMark = element('span', ''); this.ear.append(this.earMark);
    this.ear.onclick = () => {
      if (this.decorKind !== null) { this.decorKind = null; this.report('Decor Placement Cancelled.'); }
      else if (this.giftKind !== null) { this.giftKind = null; this.report('Gift Placement Cancelled.'); }
      else { E.cancelAim(this.state); this.hud = !this.hud; }
      this.refreshHUD();
    };
    this.controls.append(this.ear);
    const battery = this.button(this.hudLayer, '', 412, 8, 52, 44, () => this.open('Battery'));
    battery.classList.add('d-battery'); battery.setAttribute('aria-label', 'Battery: Hardware Telemetry Unavailable');
    this.refreshHUD();
  }
  refreshHUD() {
    this.hudLayer.hidden = !this.hud;
    this.ear.hidden = Boolean(this.modal || this.game || !this.started);
    const placing = this.giftKind !== null || this.decorKind !== null;
    this.earMark.textContent = placing ? '×' : '';
    this.ear.setAttribute('aria-label', placing ? 'Cancel Placement' : 'Show or Hide Care HUD');
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
    this.modal?.overlay.remove(); this.modal = null;
    if (this.game) { this.game.engine.cancel(); this.game.overlay.remove(); this.game = null; }
    this.refreshHUD();
  }
  open(name, page = 0) {
    this.close(); this.modal = this.panel(name === 'Gifts' ? `Gifts / ${this.state.silk} Silk` : name, () => this.back(name)); this.modal.name = name; this.modal.page = page;
    this.refreshHUD();
    const toggle = field => {
      this.state[field] = !this.state[field];
      // Auto Feed is one switch: wild prey and unattended hunting move together.
      if (field === 'autoFeed') this.state.autoHunt = this.state.autoFeed;
      this.persist(); this.open(name, page);
    };
    const row = (label, click, detail = '', extra = {}) => ({ label, click, detail, ...extra });
    let rows;
    switch (name) {
      case 'Menu': rows = [
        row('Daily', () => this.open('Daily'), this.dailyDetail(), { submenu: true }),
        row('Treats', () => this.open('Treats'), 'Spend Silk', { submenu: true }),
        row('Backgrounds', () => this.open('Backgrounds'), 'Enclosure Themes', { submenu: true }),
        row('Achievements', () => this.open('Achievements'), this.achievementDetail(), { submenu: true }),
        row('Prey Species', () => this.open('Prey Species'), this.speciesDetail(), { submenu: true }),
        row('Decor', () => this.open('Decor'), `${E.decorPlacedCount(this.state)} of ${E.DECOR_SLOTS} Placed`,
          { submenu: true }),
        /* Settings is always the last row: it is the least-used entry. */
        row('Settings', () => this.open('Settings'), 'Sound, Display, WiFi', { submenu: true }),
      ]; break;
      case 'Daily': {
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
          row('Check In', () => { E.dailyClaim(this.state); this.persist(); this.open('Daily'); },
            claimDetail, { enabled: available }),
          row(E.dailyTaskName(task),
            () => { E.dailyTaskClaim(this.state); this.persist(); this.open('Daily'); },
            taskDetail, { enabled: E.dailyTaskReady(this.state) }),
        ];
        break;
      }
      case 'Treats': rows = [
        ...E.TREATS.map((item, id) => row(item.name,
          () => { E.buyTreat(this.state, id); this.persist(); this.open('Treats'); },
          `${item.cost} Silk${this.state.silk < item.cost ? ' / Need More' : ''}`,
          { enabled: this.state.silk >= item.cost })),
      ]; break;
      case 'Decor': {
        rows = E.DECOR_KINDS.map((item, id) => {
          const owned = E.decorOwned(this.state, id);
          const unlocked = E.decorUnlocked(this.state, id);
          const detail = !unlocked ? `Level ${item.level} / ${item.cost} Silk`
            : owned ? `Owned / ${item.blocking ? 'Blocks' : 'Walk Over'}`
            : `${item.cost} Silk / ${item.blocking ? 'Blocks' : 'Walk Over'}`;
          return row(item.name, () => {
            if (!unlocked) return;
            if (!owned && !E.buyDecor(this.state, id)) return;
            this.persist();
            this.beginDecorPlacement(id);
          }, detail, { selected: owned, enabled: unlocked });
        });
        rows.push(row('Placed Items', () => this.open('Placed Decor'),
          `${E.decorPlacedCount(this.state)} of ${E.DECOR_SLOTS} Placed`, { submenu: true }));
        break;
      }
      case 'Placed Decor': {
        const placed = this.state.decor
          .map((item, slot) => ({ item, slot }))
          .filter(({ item }) => item.kind !== E.DECOR_EMPTY);
        rows = placed.length ? placed.map(({ item, slot }) => row(
          E.DECOR_KINDS[item.kind].name,
          () => { E.toggleDecorBlocking(this.state, slot); this.persist(); this.open('Placed Decor'); },
          `${item.blocking ? 'Blocks' : 'Walk Over'} / Tap to Change`,
          { selected: !!item.blocking }))
          : [row('Nothing Placed', null, 'Pick an Item First', { enabled: false })];
        break;
      }
      case 'Settings': rows = [
        row('WiFi', () => this.open('WiFi'), 'Networks and Time Sync', { submenu: true }),
        row('SD Card', () => this.open('SD Card'), 'Back Up and Restore', { submenu: true }),
        row('About', () => this.open('About'), 'Version and Hardware', { submenu: true }),
        row('Battery', () => this.open('Battery'), 'Charge and Runtime', { submenu: true }),
        row('Display', () => this.open('Display'), 'Brightness and Rotation', { submenu: true }),
        row('Sound', () => this.open('Sound'), 'Volume and Mute', { submenu: true }),
        row('Auto Feed', () => toggle('autoFeed'), 'Wild Prey and Unattended Hunting', { checkbox: true, selected: this.state.autoFeed }),
        row('Save Screenshot', () => this.screenshot(), 'Download Playfield PNG'),
        row('Wipe Spider', () => this.newTank(), 'Erases This Spider', { destructive: true }),
      ]; break;
      case 'Sound': rows = [
        row('Volume', null, `${this.state.volume}%`, { slider: { value: this.state.volume, min: 0, max: 100, step: 5, change: value => { this.state.volume = value; this.persist(); } } }),
        row('Sound On', () => { this.state.sound = true; void this.audio.unlock(true); this.persist(); this.open(name); }, 'Play Effects', { selected: this.state.sound }),
        row('Sound Off', () => { this.state.sound = false; void this.audio.unlock(false); this.persist(); this.open(name); }, 'Silence Effects', { selected: !this.state.sound }),
      ]; break;
      case 'Backgrounds': rows = E.BACKGROUNDS.map(key => row(title(key), () => {
        this.state.theme = key; this.persist(); this.open(name, page);
      }, this.state.theme === key ? 'In Use' : 'Tap to Apply', { selected: this.state.theme === key })); break;
      case 'Achievements': rows = E.ACHIEVEMENTS.filter(item => item.id !== 'first_pet').map(item => row(item.name, null,
        this.state.achievements & item.bit ? 'Earned' : 'Not Yet', { selected: Boolean(this.state.achievements & item.bit), icon: item.asset })); break;
      case 'Prey Species': rows = E.SPECIES.map(item => row(title(item.name), null,
        `${this.state.level >= item.unlock_level ? 'Available' : 'Locked'} / Level ${item.unlock_level} / Food x${item.hunger_mult.toFixed(1)}`,
        { icon: `prey_${item.key}_walk_0` })); break;
      case 'Gifts': rows = E.GIFTS.map(item => {
        const owned = Boolean(this.state.gifts_owned & (1 << item.id));
        const current = this.state.gift.used && this.state.gift.kind === item.id;
        const available = this.state.level >= item.level && (owned || this.state.silk >= item.cost);
        const status = this.state.level < item.level ? `Level ${item.level} / ${item.cost} Silk`
          : current ? 'In Tank / Move Free' : owned ? 'Owned / Place Free'
            : `${item.cost} Silk / ${this.state.silk >= item.cost ? 'Buy' : 'Earn More Silk'}`;
        return row(item.name, () => this.chooseGift(item.id), `${status}\n${item.effect}`, { enabled: available, selected: current });
      }); break;
      case 'WiFi': this.copy('WiFi, time sync and OTA use the board’s radio.\n\nThis browser has no ESP32 radio. Use Your Device → WiFi Setup below to set real credentials over local USB. The game never sends WiFi credentials or makes a simulated connection.', () => { this.close(); document.querySelector('[data-hardware="wifi"]').click(); document.querySelector('#device').scrollIntoView(); }, 'Open Device Tools'); return;
      case 'SD Card': rows = [row('Back Up Save', () => this.download()), row('Restore Save', () => this.root.querySelector('#browser-save-file').click())]; break;
      case 'Battery': rows = [
        row('Battery', null, 'Hardware Telemetry Unavailable'),
        row('Runtime', null, this.runtimeText()),
        row('Automatic Power Saving', null, 'Device Only'),
      ]; break;
      case 'Display': rows = [
        row('Brightness', null, 'Controlled by Your Screen'),
        row('Screen Rotation', () => this.rotate(), this.rotationName(), { submenu: true }),
      ]; break;      case 'About': this.copy('Spidarium / Browser Edition\n\nDevice-coordinate UI and native game rules.\n480 × 480, scaled without changing targets.\nProgress stays in this browser.\n\nHardware: Waveshare ESP32-S3\nESP-IDF 5.5.6 / LVGL 9.6\n\nPersonal, Non-Commercial License.'); return;
      default: throw new Error(`Unknown menu: ${name}`);
    }
    this.list(rows, page);
  }
  back(name) {
    if (name === 'Menu' || name === 'Gifts') this.close();
    else if (name === 'Daily' || name === 'Treats') this.open('Menu');
    else this.open(['Backgrounds', 'Achievements', 'Prey Species'].includes(name) ? 'Menu' : 'Settings');
  }
  /** The Daily row's subtitle: what is waiting for the player right now. */
  dailyDetail() {
    if (E.dailyAvailable(this.state)) return 'Check In Ready';
    if (E.dailyTaskReady(this.state)) return 'Task Complete';
    return `Streak ${this.state.daily_streak}`;
  }
  achievementDetail() {
    const earned = E.ACHIEVEMENTS.filter(item => item.id !== 'first_pet' &&
      (this.state.achievements & item.bit)).length;
    return `${earned} of ${E.ACHIEVEMENTS.filter(item => item.id !== 'first_pet').length} Earned`;
  }
  speciesDetail() {
    const unlocked = E.SPECIES.filter(item => this.state.level >= item.unlock_level).length;
    return `${unlocked} of ${E.SPECIES.length} Unlocked`;
  }
  copy(text, action, label = 'Done') {
    this.modal.panel.append(element('div', 'd-copy', text));
    this.button(this.modal.panel, label, 60, 308, 280, 64, action ?? (() => this.back(this.modal.name)));
  }
  list(rows, page) {
    const pages = Math.max(1, Math.ceil(rows.length / 3));
    rows.slice(page * 3, page * 3 + 3).forEach((item, i) => {
      const button = this.button(this.modal.panel, '', 12, 76 + i * 84, 376, 76, item.click ?? (() => {}));
      button.replaceChildren(); button.classList.add('d-row');
      button.disabled = item.enabled === false || (!item.click && !item.slider);
      if (item.selected) button.classList.add('selected');
      if (item.destructive) button.classList.add('destructive');
      if (item.detail) button.classList.add('has-detail');
      if (item.detail?.includes('\n')) button.classList.add('multi');
      if (item.icon) { button.classList.add('has-icon'); const img = element('img', ''); img.src = asset(item.icon); img.alt = ''; button.append(img); }
      button.append(element('span', 'row-label', item.label));
      if (item.detail) button.append(element('span', 'row-detail', item.detail));
      if (item.checkbox) {
        const check = element('input', ''); check.type = 'checkbox'; check.checked = item.selected; check.tabIndex = -1; check.setAttribute('aria-hidden', 'true');
        button.setAttribute('role', 'checkbox'); button.setAttribute('aria-checked', String(item.selected)); button.append(check);
      } else if (item.slider) {
        const { value, min, max, step, change } = item.slider;
        const input = element('input', 'd-slider'); input.type = 'range';
        input.min = String(min); input.max = String(max); input.step = String(step); input.value = String(value);
        input.setAttribute('aria-label', item.label);
        input.addEventListener('input', () => { change(Number(input.value)); });
        input.addEventListener('change', () => { this.open(this.modal.name, page); });
        button.append(input);
        button.disabled = false;
      } else if (item.selected || item.submenu) button.append(element('span', 'row-marker', item.selected ? '✓' : '›'));
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
    this.close(); this.giftKind = id; this.hud = false; this.refreshHUD();
    this.report('Tap the tank to place the gift. X cancels placement.'); this.persist();
  }
  beginDecorPlacement(id) {
    this.close(); this.decorKind = id; this.hud = false; this.refreshHUD();
    this.report('Tap the tank to place the decor. X cancels placement.');
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
        button.style.background = '#253a4c';
      } else if (g.kind === 1) {
        button.style.background = COLOR_RGB[g.colors[i]];
        button.lastChild.textContent = g.preview ? '...' : COLOR_NAMES[g.colors[i]];
      } else {
        button.style.background = g.ready ? '#296e55' : g.feints >= 2 ? '#b94646' : g.feints ? '#846421' : '#1c3044';
        button.lastChild.textContent = g.ready ? 'Tap!' : g.feints >= 2 ? 'NOT YET' : g.feints ? 'HOLD' : 'WAIT';
      }
    });
  }
  point(event) {
    const rect = this.canvas.getBoundingClientRect(); return { x: (event.clientX - rect.left) * 480 / rect.width, y: (event.clientY - rect.top) * 480 / rect.height };
  }
  pointerDown(event) {
    this.screen.focus({ preventScroll: true });
    if (!this.started || this.modal || this.game || this.hud) return;
    const p = this.point(event);
    void this.audio.unlock(this.state.sound);
    if (this.decorKind !== null) {
      const slot = E.placeDecor(this.state, this.decorKind, p.x, p.y);
      if (slot < 0) { this.report('No room left for decor.', true); return; }
      this.decorKind = null; this.refreshHUD(); this.persist(); this.report('Decor Placed.'); return;
    }
    if (this.giftKind !== null) {
      const result = E.placeGift(this.state, this.giftKind, p.x, p.y);
      if (!result) { this.report('Gift cannot be placed here.', true); return; }
      this.giftKind = null; this.refreshHUD(); this.persist(); this.report('Gift Placed.'); return;
    }
    this.aiming = E.beginAim(this.state, p.x, p.y) !== false;
    if (this.aiming) this.canvas.setPointerCapture(event.pointerId);
  }
  pointerMove(event) { if (this.aiming) { const p = this.point(event); E.moveAim(this.state, p.x, p.y); } }
  pointerUp(event) {
    if (!this.aiming) {
      /* A tap that did not start an aim is a pet if it landed on the spider.
       * The engine enforces the hit radius and the cooldown. */
      const p = this.point(event);
      if (E.pet(this.state, p.x, p.y)) { this.persist(); this.report('Spider Petted.'); }
      return;
    }
    this.aiming = false;
    const p = this.point(event); E.endAim(this.state, p.x, p.y); this.persist();
  }
  key(event) {
    if (event.target instanceof HTMLTextAreaElement || event.target instanceof HTMLInputElement) return;
    if (event.key.toLowerCase() === 'm' && this.started && !this.modal && !this.game) { event.preventDefault(); this.hud = !this.hud; this.refreshHUD(); return; }
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
    const state = E.STATE_NAMES[s.state];
    // The petting reaction is a timed overlay, not a state: it forces a happy
    // wave for a moment without interrupting whatever the spider is doing.
    const reacting = (s.now_ms ?? 0) < (s._engine?.pet_react_until ?? 0);
    return { state, emote: reacting ? 'happy' : s.gift_visit ? 'happy' : state === 'sleep' ? 'sleepy' : ['aim', 'stalk'].includes(state) ? 'alert' : state === 'sick' || s.mood < 18 ? 'sad' : state === 'eat' || state === 'play' ? 'happy' : 'none',
      heading: s.heading, phase: s.gait_phase, now: s.now_ms ?? 0,
      walking: s.spider.walking, colour: s.bodyColour, pupilColour: s.eyeColour,
      legColour: s.legColour, fangColour: s.fangColour, sick: s.sick !== 0 };
  }
  draw() {
    const s = this.state, ctx = this.ctx;
    ctx.fillStyle = '#101c2c'; ctx.fillRect(0, 0, 480, 480);
    const bg = this.image(`background_${s.theme}_0`); if (bg.complete && bg.naturalWidth) ctx.drawImage(bg, 0, 0, 480, 480);
    // Decor is part of the floor, so it draws under the gift and the droppings.
    // The source is 64x64 and the footprint radius is in playfield pixels, so
    // the drawn size is radius*4 -- the same ratio the device uses.
    for (const item of s.decor ?? []) {
      if (item.kind === E.DECOR_EMPTY) continue;
      const def = E.DECOR_KINDS[item.kind];
      if (!def) continue;
      const image = this.image(E.decorSprite(item.kind));
      if (!image.complete || !image.naturalWidth) continue;
      const size = def.radius * 4;
      ctx.drawImage(image, item.x - size / 2, item.y - size / 2, size, size);
    }
    if (s.gift?.used) {
      const gift = E.GIFTS[s.gift.kind], image = this.image(gift.asset);
      if (image.complete && image.naturalWidth) ctx.drawImage(image, s.gift.x - 32, s.gift.y - 32, 64, 64);
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
    renderSpider(this.spiderPixels.data, this.spiderOptions()); this.spiderCtx.putImageData(this.spiderPixels, 0, 0);
    // Life-stage size: a spiderling reads as small, an adult as full size.
    const stageScale = s.stage === 0 ? .75 : s.stage === 1 ? .875 : 1;
    const half = 48 * stageScale;
    // Petting bounce: one smooth rise and fall, matching the device's
    // PET_REACT_PX / PET_REACT_MS overlay.
    const reactLeft = (s._engine?.pet_react_until ?? 0) - (s.now_ms ?? 0);
    const lift = reactLeft > 0 ? -Math.round(9 * Math.sin((1 - reactLeft / E.CONFIG.PET_REACT_MS) * Math.PI)) : 0;
    ctx.drawImage(this.spiderCanvas, s.spider_x - half, s.spider_y - half + lift, half * 2, half * 2);
    if (s.state === E.STATES.AIM) {
      ctx.strokeStyle = '#f5c86b'; ctx.lineWidth = 2; ctx.setLineDash([6, 4]); ctx.beginPath();
      ctx.moveTo(s.spider_x, s.spider_y); ctx.lineTo(s.aim_x, s.aim_y); ctx.stroke(); ctx.setLineDash([]);
      ctx.beginPath(); ctx.arc(s.aim_x, s.aim_y, 15, 0, Math.PI * 2); ctx.stroke();
    }
    for (const prey of s.prey) {
      if (!prey.alive && !prey.dead) continue;
      const species = E.SPECIES[prey.species], frame = prey.frame ?? Math.floor(performance.now() / 200) % 2;
      const image = this.image(`prey_${species.key}_${prey.alive ? (prey.fleeing ? 'flee_' : 'walk_') + frame : 'dead_0'}`);
      if (image.complete && image.naturalWidth) ctx.drawImage(image, prey.x - 12, prey.y - 12, 24, 24);
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
  }
  frame(now) {
    const elapsed = Math.min(.15, Math.max(0, (now - this.last) / 1000)); this.last = now;
    if (!document.hidden && this.started && !this.hud && !this.modal && !this.game &&
        this.giftKind === null && this.decorKind === null) {
      this.accumulator += elapsed; while (this.accumulator >= .05) { E.tick(this.state, .05); this.accumulator -= .05; }
    } else this.accumulator = 0;
    if (this.game) this.gameFrame(now);
    if (this.started && !this.hud && !this.modal && !this.game) this.audio.tick(E.STATE_NAMES[this.state.state], now, this.state.sound);
    this.draw(); this.refreshHUD();
    if (now - this.lastSave >= 5000) { this.persist(); this.lastSave = now; }
    requestAnimationFrame(time => this.frame(time));
  }
  download() {
    const url = URL.createObjectURL(new Blob([JSON.stringify(this.state, null, 2)], { type: 'application/json' }));
    const link = element('a', ''); link.href = url; link.download = 'spidarium-browser-save.json'; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000); this.report('Browser save downloaded. Device binary backups use the USB tools below.');
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
      link.click();
      link.remove();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      this.report('Playfield screenshot downloaded. Menus and HUD are excluded.');
    } catch (error) { this.report(`Screenshot failed: ${error.message}`, true); }
  }
  async importProgress() {
    try {
      const file = this.root.querySelector('#browser-save-file').files[0]; if (!file) return;
      if (file.size > 100000) throw new Error('Browser backup is too large');
      const state = E.restoreSim(JSON.parse(await file.text()));
      if (!confirm('Replace browser progress with this backup? Your physical spider is not touched.')) return;
      if (!this.progress.reset(state)) return;
      this.state = state; this.started = state.setup_complete; this.close();
      if (!this.started) this.setup(); this.report('Browser progress restored.');
    } catch (error) { this.report(`Browser restore failed: ${error.message}`, true); }
    finally { this.root.querySelector('#browser-save-file').value = ''; }
  }
}
