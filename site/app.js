import { SerialConsole } from './lib/protocol.js';
import { decodeSave, fromHex, toHex, validateSchema, validateWifi } from './lib/save.js';
import { loadFirmware, flashFirmware } from './lib/flasher.js';
import { PREY, BACKGROUNDS, GIFTS, freshState, restoreSim, preySprite, feed, care, pounce, purchaseGift, visitGift, tick, startGame, answerGame } from './lib/sim.js';
import { renderSpider } from './lib/spider-renderer.js';

const $ = selector => document.querySelector(selector);
const storageKey = 'spidarium.browser-habitat.v1';
let state = freshState();
try { state = restoreSim(JSON.parse(localStorage.getItem(storageKey))); } catch {}
const persist = () => { try { localStorage.setItem(storageKey, JSON.stringify(state)); } catch {} };
const message = text => { $('#sim-message').textContent = text; };
const assetURL = name => new URL(`../assets/${name}.png`, import.meta.url).href;
const images = new Map();
function image(name) {
  if (!images.has(name)) {
    const img = new Image(); img.src = assetURL(name); images.set(name, img);
  }
  return images.get(name);
}
let audio;
function chirp() {
  if (state.muted) return;
  try {
    audio ??= new (globalThis.AudioContext || globalThis.webkitAudioContext)();
    void audio.resume();
    const oscillator = audio.createOscillator(), gain = audio.createGain();
    oscillator.type = 'sine'; oscillator.frequency.setValueAtTime(640, audio.currentTime);
    oscillator.frequency.exponentialRampToValueAtTime(1050, audio.currentTime + .08);
    gain.gain.setValueAtTime(.035, audio.currentTime); gain.gain.exponentialRampToValueAtTime(.001, audio.currentTime + .14);
    oscillator.connect(gain); gain.connect(audio.destination); oscillator.start(); oscillator.stop(audio.currentTime + .15);
  } catch {}
}
const giftNames = ['pebble', 'leaf', 'twig', 'acorn', 'moss', 'sap', 'flower', 'crystal', 'amber', 'coral', 'silk', 'bone', 'pearl', 'ruby', 'sapphire', 'emerald', 'diamond', 'crown'];
const stats = [['hunger', 'Food'], ['mood', 'Mood'], ['energy', 'Energy'], ['hygiene', 'Hygiene']];
$('#stats').innerHTML = stats.map(([key, label]) => `<div class="stat-${key}"><div class="stat-label">${label}<span id="stat-${key}"></span></div><progress id="bar-${key}" max="100" value="0" aria-label="${label}"></progress></div>`).join('');
function updateUI() {
  for (const [key] of stats) {
    $(`#stat-${key}`).textContent = `${Math.round(state[key])}%`;
    $(`#bar-${key}`).value = state[key];
  }
  $('#pet-title').textContent = `${state.name || 'Spider'}.`;
  $('#tank-name').textContent = (state.name || 'Spider').toUpperCase();
  $('#tank-level').textContent = `LV. ${state.level}`;
  $('#tank-state').textContent = state.state === 'sleep' ? 'DREAMING IN SILK' : state.hunger < 20 ? 'A LITTLE HUNGRY' : state.energy < 15 ? 'TIME FOR A LITTLE REST' : 'CURIOUS & CONTENT';
  $('#silk').textContent = state.silk;
  $('#xp-label').textContent = `Level ${state.level} · ${state.xp % 40} / 40 XP`;
  $('#xp').value = state.xp % 40;
  $('#sleep-button').innerHTML = `<span>☾</span>${state.state === 'sleep' ? 'Wake' : 'Sleep'}`;
  $('#battery-label').textContent = `${state.battery}% ${state.charging ? '⚡' : '◇'} · PREVIEW`;
  $('#tank').style.filter = `brightness(${.4 + state.brightness * .006})`;
}
let panel = 'games';
function menuItem(title, detail, label, action, disabled = false) {
  const row = document.createElement('div'); row.className = 'menu-item';
  const copy = document.createElement('div'), small = document.createElement('small');
  copy.textContent = title; small.textContent = detail; copy.append(small);
  const button = document.createElement('button'); button.textContent = label; button.disabled = disabled; button.onclick = action;
  row.append(copy, button); return row;
}
function renderMenu() {
  const menu = $('#sim-menu'); menu.replaceChildren();
  for (const button of document.querySelectorAll('[data-panel]')) button.setAttribute('aria-selected', String(button.dataset.panel === panel));
  if (panel === 'games') {
    menu.append(
      menuItem('Silk & seek', 'Remember the glowing leaf · 8 rounds', 'Play →', () => openGame('memory')),
      menuItem('Quick little legs', 'Tap only after the green signal · 8 rounds', 'Play →', () => openGame('reflex')),
      menuItem('Bug spotter', 'Find the named prey among four species · 8 rounds', 'Play →', () => openGame('hunt')),
    );
  } else if (panel === 'feed') {
    const intro = document.createElement('p'); intro.className = 'menu-intro'; intro.textContent = 'Choose a bug. Three prey at a time; no overcrowding.'; menu.append(intro);
    const choices = document.createElement('div'); choices.className = 'feed-selector';
    PREY.forEach(species => {
      const button = document.createElement('button'), img = document.createElement('img');
      img.src = assetURL(`prey_${species}_walk_0`); img.alt = ''; button.append(img, species);
      button.onclick = () => { message(feed(state, species)); persist(); updateUI(); chirp(); };
      choices.append(button);
    });
    menu.append(choices);
  } else if (panel === 'gifts') {
    const list = document.createElement('div'); list.className = 'gift-scroll';
    for (const gift of GIFTS) {
      const owned = state.owned.includes(gift.id), locked = state.level < gift.level && !owned;
      list.append(menuItem(gift.name, `${gift.effect} · ${owned ? 'Owned' : locked ? `Level ${gift.level}` : `${gift.cost} Silk`}`,
        state.gift === gift.id ? 'Visit' : owned ? 'Place' : locked ? 'Locked' : 'Give',
        () => { message(state.gift === gift.id ? visitGift(state) : purchaseGift(state, gift.id)); persist(); updateUI(); renderMenu(); chirp(); }, locked));
    }
    menu.append(list);
  } else if (panel === 'journal') {
    const grid = document.createElement('div'); grid.className = 'journal-grid';
    for (const [label, value] of [['Meals', state.meals], ['Catches', state.catches], ['Games', state.games], ['Cleanups', state.cleanCount], ['Gift visits', state.visits]]) {
      const cell = document.createElement('div'), count = document.createElement('b'); count.textContent = value;
      cell.append(`${label} · `, count); grid.append(cell);
    }
    menu.append(grid);
    const note = document.createElement('p'); note.className = 'menu-intro'; note.textContent = `Browser progress only · ${state.level < 3 ? 'Spiderling' : state.level < 8 ? 'Juvenile' : 'Adult'} · ${state.owned.length} gifts owned`;
    menu.append(note);
    const badges = document.createElement('div'); badges.className = 'achievement-row';
    for (const [name, title, earned] of [
      ['first_meal', 'First meal', state.meals > 0],
      ['first_clean', 'First cleanup', state.cleanCount > 0], ['first_play', 'First game', state.games > 0],
      ['first_hunt', 'First hunt', state.catches > 0], ['ten_meals', 'Ten meals', state.meals >= 10],
      ['level_5', 'Level five', state.level >= 5], ['adult', 'Browser adult', state.level >= 8],
      ['first_perfect', 'Perfect game', state.bestGame === 8],
    ]) {
      if (!earned) continue;
      const badge = document.createElement('img'); badge.src = assetURL(`achievement_${name}`); badge.alt = title; badge.title = title; badges.append(badge);
    }
    menu.append(badges);
  } else {
    const rows = [
      ['Name', 'name', 'text'], ['Habitat', 'background', 'select'], ['Sound', 'muted', 'checkbox'],
      ['Brightness', 'brightness', 'range'], ['Battery preview', 'battery', 'range'], ['Charging preview', 'charging', 'checkbox'],
    ];
    for (const [label, key, type] of rows) {
      const row = document.createElement('label'); row.className = 'setting-row'; row.textContent = label;
      const input = document.createElement(type === 'select' ? 'select' : 'input');
      if (type === 'select') BACKGROUNDS.forEach(name => { const option = document.createElement('option'); option.value = name; option.textContent = name.replace('_', ' '); input.append(option); });
      else input.type = type;
      if (type === 'checkbox') input.checked = key === 'muted' ? !state.muted : state[key];
      else input.value = state[key];
      if (type === 'text') { input.maxLength = 16; input.placeholder = 'Spider'; input.className = 'setting-input'; input.style.width = '130px'; }
      if (type === 'range') { input.min = key === 'brightness' ? '15' : '0'; input.max = '100'; }
      input.oninput = () => {
        state[key] = type === 'checkbox' ? (key === 'muted' ? !input.checked : input.checked) : type === 'range' ? Number(input.value) : input.value;
        persist(); updateUI(); if (key === 'muted') chirp();
      };
      row.append(input); menu.append(row);
    }
    const reset = document.createElement('button'); reset.className = 'text-button'; reset.textContent = 'Reset browser pet…';
    reset.onclick = () => { if (confirm('Reset only this browser habitat? Your device save is not affected.')) { state = freshState(); persist(); updateUI(); renderMenu(); message('A fresh little beginning.'); } };
    menu.append(reset);
  }
}
document.querySelectorAll('[data-panel]').forEach(button => { button.onclick = () => { panel = button.dataset.panel; renderMenu(); }; });
document.querySelectorAll('[data-care]').forEach(button => {
  button.onclick = () => {
    if (button.dataset.care === 'feed') { panel = 'feed'; renderMenu(); }
    else if (button.dataset.care === 'play') openGame(['memory', 'reflex', 'hunt'][Math.floor(Math.random() * 3)]);
    else { message(care(state, button.dataset.care)); persist(); updateUI(); chirp(); }
  };
});
const canvas = $('#tank'), context = canvas.getContext('2d');
context.imageSmoothingEnabled = false;
const nativeCanvas = document.createElement('canvas'); nativeCanvas.width = nativeCanvas.height = 96;
const nativeContext = nativeCanvas.getContext('2d'), nativeImage = nativeContext.createImageData(96, 96);
function drawNative(target, options = {}) {
  const ctx = target.getContext('2d'), frame = ctx.createImageData(96, 96);
  renderSpider(frame.data, options); ctx.putImageData(frame, 0, 0);
}
drawNative($('#hero-spider'));
drawNative($('#sleeping-spider'), { state: 'sleep', emote: 'sleepy' });
let drag = null, pointerTarget = null, motionUntil = 0, lastTime = 0, lastPersist = 0, heading = Math.PI / 2;
function coordinate(event) {
  const rect = canvas.getBoundingClientRect();
  return { x: (event.clientX - rect.left) / rect.width * 480, y: (event.clientY - rect.top) / rect.height * 480 };
}
canvas.onpointerdown = event => {
  const point = coordinate(event);
  if (state.gift !== null && Math.hypot(point.x - 330, point.y - 355) < 45) {
    message(visitGift(state)); persist(); updateUI(); return;
  }
  if (Math.hypot(point.x - state.x, point.y - state.y) < 75) {
    drag = point; pointerTarget = point; canvas.setPointerCapture(event.pointerId);
  } else { pointerTarget = point; message('Start your drag on your spider, or use the arrow keys and Space.'); }
};
canvas.onpointermove = event => { if (drag) pointerTarget = coordinate(event); };
canvas.onpointerup = event => {
  if (!drag) return;
  const target = coordinate(event);
  if (Math.hypot(target.x - drag.x, target.y - drag.y) < 10) message('Drag from your spider to aim a jump. Use Play for a minigame.');
  else {
    heading = Math.atan2(target.y - state.y, target.x - state.x);
    message(pounce(state, target.x, target.y).message); motionUntil = performance.now() + 850;
  }
  drag = null; pointerTarget = null; persist(); updateUI(); chirp();
};
canvas.onpointercancel = () => { drag = null; pointerTarget = null; };
let keyAim = { x: 240, y: 257 };
canvas.onkeydown = event => {
  const keys = { ArrowLeft: [-15, 0], ArrowRight: [15, 0], ArrowUp: [0, -15], ArrowDown: [0, 15] };
  if (keys[event.key]) {
    event.preventDefault(); keyAim.x = Math.max(45, Math.min(435, keyAim.x + keys[event.key][0])); keyAim.y = Math.max(45, Math.min(435, keyAim.y + keys[event.key][1]));
    pointerTarget = { ...keyAim };
  } else if (event.code === 'Space') {
    event.preventDefault(); heading = Math.atan2(keyAim.y - state.y, keyAim.x - state.x); message(pounce(state, keyAim.x, keyAim.y).message); motionUntil = performance.now() + 850; pointerTarget = null; persist(); updateUI();
  } else if (event.key === 'Enter') { event.preventDefault(); openGame(['memory', 'reflex', 'hunt'][Math.floor(Math.random() * 3)]); }
};
function drawSprite(name, x, y, size, rotation = 0) {
  const img = image(name);
  if (!img.complete || !img.naturalWidth) return;
  context.save(); context.translate(x, y); context.rotate(rotation); context.drawImage(img, -size / 2, -size / 2, size, size); context.restore();
}
function draw(now) {
  const delta = lastTime ? Math.min((now - lastTime) / 1000, .1) : 0; lastTime = now;
  tick(state, delta);
  if (state.state !== 'sleep' && now > motionUntil) state.state = 'idle';
  context.fillStyle = '#404e35'; context.fillRect(0, 0, 480, 480);
  const background = image(`background_${state.background}_0`);
  if (background.complete && background.naturalWidth) context.drawImage(background, 0, 0, 480, 480);
  context.fillStyle = '#111b1630'; context.fillRect(0, 0, 480, 480);
  if (state.gift !== null) drawSprite(`gift_${giftNames[state.gift]}`, 330, 355, 65);
  for (const splatter of state.splatters) {
    drawSprite(preySprite(splatter.species, true), splatter.x, splatter.y, 46);
  }
  for (const bug of state.prey) {
    if (!drag && state.state !== 'sleep') {
      bug.phase += delta * 1.3; bug.x += Math.cos(bug.phase) * delta * 7; bug.y += Math.sin(bug.phase * 1.1) * delta * 7;
      const r = Math.hypot(bug.x - 240, bug.y - 240);
      if (r > 175) { bug.x = 240 + (bug.x - 240) / r * 170; bug.y = 240 + (bug.y - 240) / r * 170; }
    }
    drawSprite(preySprite(bug.species, false, Math.floor(now / 250)), bug.x, bug.y, 46);
  }
  const emote = state.state === 'sleep' ? 'sleepy' : state.mood < 20 ? 'sad' : drag ? 'alert' : state.state === 'eat' ? 'happy' : 'none';
  renderSpider(nativeImage.data, { heading, state: drag ? 'aim' : state.state, emote, now });
  nativeContext.putImageData(nativeImage, 0, 0);
  context.drawImage(nativeCanvas, state.x - 48, state.y - 48);
  if (pointerTarget) {
    context.strokeStyle = '#e3f0af'; context.lineWidth = 2; context.setLineDash([5, 6]);
    context.beginPath(); context.moveTo(state.x, state.y); context.lineTo(pointerTarget.x, pointerTarget.y); context.stroke(); context.setLineDash([]);
    context.beginPath(); context.arc(pointerTarget.x, pointerTarget.y, 19, 0, Math.PI * 2); context.stroke();
  }
  if (now - lastPersist > 2000) { updateUI(); persist(); lastPersist = now; }
  requestAnimationFrame(draw);
}
document.addEventListener('visibilitychange', () => { lastTime = 0; persist(); });
updateUI(); renderMenu(); requestAnimationFrame(draw);

let game = null, gameTimer = null, roundStarted = 0, gameChoices = [];
function openGame(kind) {
  if (state.state === 'sleep') { message('Wake your spider before playing.'); return; }
  game = startGame(kind); $('#game-title').textContent = { memory: 'Silk & seek', reflex: 'Quick little legs', hunt: 'Bug spotter' }[kind];
  $('#game-dialog').showModal(); presentRound();
}
function presentRound() {
  clearTimeout(gameTimer); $('#next-round').hidden = true; $('#game-feedback').textContent = '';
  $('#game-round').textContent = `Round ${game.round + 1} / 8`; $('#game-score').textContent = `${game.correct} correct`;
  const board = $('#game-board'); board.className = 'game-board'; board.replaceChildren(); roundStarted = 0;
  gameChoices = Array.from({ length: 4 }, (_, index) => PREY[(game.round + index) % PREY.length]);
  $('#game-instruction').textContent = game.kind === 'memory' ? 'Remember the bright leaf. When it fades, choose its hiding place.' : game.kind === 'reflex' ? 'Wait for a tile to turn green. Then tap it within 900 ms. Early taps lose the round.' : `Find the ${gameChoices[game.target]}.`;
  for (let index = 0; index < 4; index++) {
    const tile = document.createElement('button'); tile.className = 'game-tile'; tile.setAttribute('aria-label', game.kind === 'hunt' ? gameChoices[index] : `Leaf ${index + 1}`);
    if (game.kind === 'hunt') { const img = document.createElement('img'); img.alt = ''; img.src = assetURL(`prey_${gameChoices[index]}_walk_0`); tile.append(img); }
    else tile.textContent = '♧';
    if (game.kind === 'memory') { tile.disabled = true; if (index === game.target) tile.classList.add('target'); }
    tile.onclick = () => resolveRound(index);
    board.append(tile);
  }
  if (game.kind === 'memory') {
    gameTimer = setTimeout(() => {
      board.querySelectorAll('button').forEach(tile => { tile.classList.remove('target'); tile.textContent = '?'; tile.disabled = false; });
      $('#game-instruction').textContent = 'Where was the glowing leaf?';
    }, Math.max(450, 1050 - game.round * 70));
  } else if (game.kind === 'reflex') {
    gameTimer = setTimeout(() => {
      board.children[game.target].classList.add('target'); roundStarted = performance.now();
      $('#game-instruction').textContent = 'Now! Tap the green tile.';
      gameTimer = setTimeout(() => resolveRound(-1), 900);
    }, 900 + Math.random() * 1500);
  }
}
function resolveRound(index) {
  clearTimeout(gameTimer);
  if (game.kind === 'reflex' && (!roundStarted || performance.now() - roundStarted > 900)) index = -1;
  const hit = answerGame(state, game, index);
  $('#game-board').querySelectorAll('button').forEach(button => { button.disabled = true; });
  $('#game-score').textContent = `${game.correct} correct`;
  $('#game-feedback').textContent = hit ? 'A lovely little win.' : 'Not this time. The next round is a fresh start.';
  chirp();
  if (game.finished) {
    $('#game-round').textContent = '8 / 8 · Complete'; $('#next-round').textContent = 'Back to the terrarium'; $('#next-round').hidden = false;
    const silk = game.correct * 2 + (game.correct === 8 ? 8 : 0);
    $('#game-feedback').textContent = `${game.correct}/8 correct · +${silk} Silk · +${game.correct * 3} XP${game.correct === 8 ? ' · Perfect round bonus!' : ''}`;
    persist(); updateUI();
  } else { $('#next-round').textContent = 'Next round →'; $('#next-round').hidden = false; }
}
$('#next-round').onclick = () => { if (game.finished) $('#game-dialog').close(); else presentRound(); };
$('#close-game').onclick = () => $('#game-dialog').close();
$('#game-dialog').addEventListener('close', () => { clearTimeout(gameTimer); game = null; });

const serialSupported = Boolean(globalThis.isSecureContext && navigator.serial);
$('#serial-support').textContent = serialSupported
  ? 'Web Serial is available. Device actions require your permission. The simulator never connects to a board.'
  : 'Play freely here. Device tools need desktop Chrome/Edge in a secure context (HTTPS or localhost); Web Serial is unavailable in this browser.';
const log = text => {
  const lines = $('#serial-log').textContent.split('\n').filter(Boolean);
  lines.push(String(text)); $('#serial-log').textContent = lines.slice(-150).join('\n'); $('#serial-log').scrollTop = $('#serial-log').scrollHeight;
};
const consoleSession = new SerialConsole({ log });
let selectedPort = null, busy = false, firmware = null, loadedSave = null, schema = null, deviceSchemaVerified = false;
let pendingRestore = false;
function hardwareTab(name) {
  document.querySelectorAll('[data-hardware]').forEach(button => button.setAttribute('aria-selected', String(button.dataset.hardware === name)));
  ['flash', 'save', 'wifi'].forEach(panel => { $(`#hardware-${panel}`).hidden = panel !== name; });
}
document.querySelectorAll('[data-hardware]').forEach(button => { button.onclick = () => hardwareTab(button.dataset.hardware); });
$('#wifi-console-link').onclick = event => { event.preventDefault(); hardwareTab('save'); };
function deviceUI() {
  const connected = consoleSession.connected;
  $('#connection-label').textContent = busy ? 'Working…' : connected ? (consoleSession.quiet ? 'Connected · log suppressed' : 'Connected') : 'Disconnected';
  $('#connect').disabled = !serialSupported || busy || connected;
  $('#disconnect').disabled = busy || !connected;
  for (const id of ['export-save', 'read-schema', 'reboot']) $(`#${id}`).disabled = busy || !connected;
  $('#restore-save').disabled = busy || !connected || !loadedSave || !deviceSchemaVerified || !$('#restore-consent').checked || pendingRestore;
  $('#wifi-submit').disabled = busy || !connected || pendingRestore;
  $('#flash').disabled = busy || !serialSupported || !firmware || !$('#flash-consent').checked || pendingRestore;
  $('#check-release').disabled = busy;
}
async function operation(work, status = $('#save-summary')) {
  if (busy) return;
  busy = true; deviceUI();
  try { await work(); }
  catch (error) { status.textContent = error.message; log(error.message); }
  finally { busy = false; deviceUI(); }
}
async function bundledSchema() {
  if (schema) return schema;
  const response = await fetch(new URL('./save-schema.json', import.meta.url));
  if (!response.ok) throw new Error('Bundled save-schema.json missing; release tooling must supply the compiled schema');
  schema = validateSchema(await response.json()); return schema;
}
async function readDeviceSchema() {
  const result = await consoleSession.command('save schema', {
    dataPrefix: 'SAVE SCHEMA ', accept: line => /^OK (?:save )?schema(?:\s|$)/.test(line),
  });
  schema = validateSchema(JSON.parse(result.data)); deviceSchemaVerified = true;
  if (loadedSave) showSave(loadedSave);
  return schema;
}
function showSave(bytes) {
  const parsed = decodeSave(bytes, schema);
  loadedSave = bytes;
  $('#save-summary').textContent = `Valid Spidarium v${parsed.header.version} save · ${bytes.length} bytes · ${parsed.header.size}-byte payload. ${deviceSchemaVerified ? 'Checked against this device schema.' : 'Offline view; connect and read device schema before restoring.'}`;
  const table = document.createElement('table');
  for (const [name, value] of Object.entries(parsed.fields)) {
    const row = document.createElement('tr'), key = document.createElement('td'), cell = document.createElement('td'); key.textContent = name; cell.textContent = String(value); row.append(key, cell); table.append(row);
  }
  $('#save-view').replaceChildren(table); deviceUI();
}
$('#connect').onclick = () => operation(async () => {
  selectedPort = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
  await consoleSession.connect(selectedPort); pendingRestore = false; deviceSchemaVerified = false;
  $('#save-summary').textContent = 'Console connected. Export your pet or read its schema.';
});
$('#disconnect').onclick = () => operation(async () => { await consoleSession.close(); deviceSchemaVerified = false; });
$('#read-schema').onclick = () => operation(async () => { await readDeviceSchema(); $('#save-summary').textContent = 'Read and validated the device’s compiled save schema.'; });
$('#export-save').onclick = () => operation(async () => {
  await readDeviceSchema();
  const result = await consoleSession.command('save export', { dataPrefix: 'SAVE HEX ', accept: line => /^OK save export(?:\s|$)/.test(line), timeout: 30000 });
  const bytes = fromHex(result.data); showSave(bytes);
  const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
  const link = document.createElement('a'); link.href = url; link.download = `spidarium-save-${new Date().toISOString().slice(0, 10)}.bin`; link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
});
$('#save-file').onchange = () => operation(async () => {
  loadedSave = null; $('#save-view').replaceChildren(); $('#restore-consent').checked = false;
  const file = $('#save-file').files[0]; if (!file) return;
  if (file.size > 4096) throw new Error('Backup file is too large');
  await bundledSchema();
  const bytes = /\.(txt|hex)$/i.test(file.name) ? fromHex((await file.text()).trim().replace(/^SAVE HEX /, '')) : new Uint8Array(await file.arrayBuffer());
  showSave(bytes);
});
$('#restore-consent').onchange = deviceUI;
$('#restore-save').onclick = () => operation(async () => {
  decodeSave(loadedSave, schema);
  if (!confirm('Replace the device pet with this exact backup? Keep your original backup. The device will remain paused until you explicitly reboot.')) return;
  const result = await consoleSession.command(`save restore ${toHex(loadedSave)}`, {
    accept: line => line === 'OK save restore; reboot required', timeout: 30000,
  });
  pendingRestore = true; $('#restore-consent').checked = false;
  $('#save-summary').textContent = `${result.line}. Device pet is paused. Use Reboot device when ready.`;
});
$('#reboot').onclick = () => operation(async () => {
  if (!confirm('Reboot the connected device now? This disconnects the console and loads the restored save, if any.')) return;
  await consoleSession.command('reboot', { accept: line => /^OK rebooting(?:\s|$)/.test(line) });
  await consoleSession.close(); pendingRestore = false; deviceSchemaVerified = false;
  $('#save-summary').textContent = 'Reboot acknowledged. Reconnect after the board starts.';
});
$('#wifi-form').onsubmit = event => {
  event.preventDefault();
  operation(async () => {
    let command;
    try { command = validateWifi($('#wifi-ssid').value, $('#wifi-password').value); }
    finally { $('#wifi-ssid').value = ''; $('#wifi-password').value = ''; }
    $('#wifi-status').textContent = 'Joining over local USB… Credentials cleared from the form; logs suppressed.';
    try {
      await consoleSession.command(command, { accept: line => /^OK wifi(?:\s|$)/.test(line), timeout: 180000, sensitive: true });
      $('#wifi-status').textContent = 'Device confirmed WiFi setup. Credentials stayed on the local USB connection.';
    } finally { command = null; }
  }, $('#wifi-status'));
};
$('#check-release').onclick = () => operation(async () => {
  firmware = null;
  firmware = await loadFirmware(new URL('./firmware/manifest.json', import.meta.url));
  $('#release-info').textContent = `Spidarium ${firmware.manifest.version} · ESP32-S3 · all five SHA-256 hashes verified · NVS preserved.`;
  $('#release-files').replaceChildren();
  firmware.manifest.files.forEach(file => {
    const row = document.createElement('li'), hash = document.createElement('code');
    row.textContent = `${file.path} · 0x${file.offset.toString(16)} · ${file.size.toLocaleString()} bytes`; hash.textContent = `SHA-256 ${file.sha256}`; row.append(hash); $('#release-files').append(row);
  });
  $('#flash-status').textContent = 'Release verified. No firmware written yet.';
}, $('#flash-status'));
$('#flash-consent').onchange = deviceUI;
$('#flash').onclick = () => operation(async () => {
  if (!confirm(`Flash Spidarium ${firmware.manifest.version} onto your ESP32-S3? Do not unplug until complete. NVS will not be erased.`)) return;
  // Get permission in the click gesture, before network/module work.
  const port = consoleSession.connected ? selectedPort : await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
  let tools;
  try { tools = await import('./vendor/esptool.js'); }
  catch { throw new Error('Local esptool bundle missing. Run npm ci and npm run build, then serve the built site.'); }
  $('#flash-status').textContent = 'Entering bootloader. If needed, hold BOOT and tap RESET.';
  const verified = await loadFirmware(new URL('./firmware/manifest.json', import.meta.url));
  if (JSON.stringify(verified.manifest) !== JSON.stringify(firmware.manifest)) throw new Error('Release changed since confirmation; check release again');
  await flashFirmware({
    port, consoleSession, firmware: verified, tools, log,
    progress: (index, written, total) => {
      $('#flash-progress').value = (index + written / total) / verified.images.length * 100;
      $('#flash-status').textContent = `Writing image ${index + 1} / ${verified.images.length} · ${Math.round(written / total * 100)}%`;
    },
  });
  $('#flash-progress').value = 100;
  $('#flash-status').textContent = 'All images written and device MD5 checks passed. Press RESET when ready. NVS was not erased.';
  deviceSchemaVerified = false;
}, $('#flash-status'));
$('#clear-log').onclick = () => { $('#serial-log').textContent = ''; };
navigator.serial?.addEventListener('disconnect', () => {
  if (!consoleSession.connected) return;
  void consoleSession.close().then(() => { deviceSchemaVerified = false; deviceUI(); });
});
deviceUI();
