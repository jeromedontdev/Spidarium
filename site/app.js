import { SerialConsole } from './lib/protocol.js';
import { decodeSave, fromHex, toHex, validateSchema, validateWifi } from './lib/save.js?v=1.1.24';
import { loadFirmware, flashFirmware } from './lib/flasher.js?v=1.1.24';
import { DeviceGame } from './lib/device-ui.js?v=1.1.24';
import { renderSpider } from './lib/spider-renderer.js';

const $ = selector => document.querySelector(selector);
for (const [id, state, emote] of [['hero-spider', 'idle', 'happy'], ['sleeping-spider', 'sleep', 'sleepy']]) {
  const ctx = $(`#${id}`).getContext('2d'), frame = ctx.createImageData(96, 96);
  renderSpider(frame.data, { state, emote, fangColour: 7 });
  ctx.putImageData(frame, 0, 0);
}
new DeviceGame($('#browser-game'));

const serialSupported = Boolean(globalThis.isSecureContext && navigator.serial);
$('#serial-support').textContent = serialSupported
  ? 'Web Serial is available. Device actions require your permission. The browser game never connects to a board.'
  : 'Play freely here. Device tools need desktop Chrome/Edge over HTTPS or localhost; Web Serial is unavailable.';
const log = text => {
  const lines = $('#serial-log').textContent.split('\n').filter(Boolean);
  lines.push(String(text)); $('#serial-log').textContent = lines.slice(-150).join('\n'); $('#serial-log').scrollTop = $('#serial-log').scrollHeight;
};
const consoleSession = new SerialConsole({ log });
let selectedPort = null, busy = false, firmware = null, loadedSave = null, schema = null, deviceSchemaVerified = false, pendingRestore = false;
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
  const response = await fetch(new URL('./save-schema.json', import.meta.url), {cache:'no-store'});
  if (!response.ok) throw new Error('Bundled compiled save schema is missing');
  schema = validateSchema(await response.json()); return schema;
}
async function readDeviceSchema() {
  const result = await consoleSession.command('save schema', { dataPrefix: 'SAVE SCHEMA ', accept: line => /^OK (?:save )?schema(?:\s|$)/.test(line) });
  schema = validateSchema(JSON.parse(result.data)); deviceSchemaVerified = true;
  if (loadedSave) showSave(loadedSave);
  return schema;
}
function showSave(bytes) {
  const parsed = decodeSave(bytes, schema); loadedSave = bytes;
  $('#save-summary').textContent = `Valid Spidarium v${parsed.header.version} save · ${bytes.length} bytes · ${deviceSchemaVerified ? 'Checked against this device schema.' : 'Offline view; read the device schema before restoring.'}`;
  const table = document.createElement('table');
  for (const [name, value] of Object.entries(parsed.fields)) {
    const row = document.createElement('tr'), key = document.createElement('td'), cell = document.createElement('td');
    key.textContent = name;
    cell.textContent = typeof value === 'object' ? JSON.stringify(value) : String(value);
    row.append(key, cell); table.append(row);
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
  const link = document.createElement('a'); link.href = url; link.download = `spidarium-save-${new Date().toISOString().slice(0, 10)}.bin`; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
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
  if (!confirm('Replace the device pet with this exact backup? Keep your original. The tank stays paused until you reboot.')) return;
  const result = await consoleSession.command(`save restore ${toHex(loadedSave)}`, { accept: line => line === 'OK save restore; reboot required', timeout: 30000 });
  pendingRestore = true; $('#restore-consent').checked = false; $('#save-summary').textContent = `${result.line}. Use Reboot device when ready.`;
});
$('#reboot').onclick = () => operation(async () => {
  if (!confirm('Reboot the connected device now? This disconnects the console and loads the saved pet.')) return;
  await consoleSession.command('reboot', { accept: line => /^OK rebooting(?:\s|$)/.test(line) });
  await consoleSession.close(); pendingRestore = false; deviceSchemaVerified = false; $('#save-summary').textContent = 'Reboot acknowledged. Reconnect after the board starts.';
});
$('#wifi-form').onsubmit = event => {
  event.preventDefault();
  operation(async () => {
    let command;
    try { command = validateWifi($('#wifi-ssid').value, $('#wifi-password').value); }
    finally { $('#wifi-ssid').value = ''; $('#wifi-password').value = ''; }
    $('#wifi-status').textContent = 'Joining over local USB… Credentials cleared; logs suppressed.';
    try {
      await consoleSession.command(command, { accept: line => /^OK wifi(?:\s|$)/.test(line), timeout: 180000, sensitive: true });
      $('#wifi-status').textContent = 'Device confirmed WiFi setup. Credentials stayed on local USB.';
    } finally { command = null; }
  }, $('#wifi-status'));
};
$('#check-release').onclick = () => operation(async () => {
  firmware = null; firmware = await loadFirmware(new URL('./firmware/manifest.json', import.meta.url));
  $('#release-info').textContent = `Spidarium ${firmware.manifest.version} · ESP32-S3 · all five SHA-256 hashes verified · NVS preserved.`;
  $('#release-files').replaceChildren();
  firmware.manifest.files.forEach(file => {
    const row = document.createElement('li'), hash = document.createElement('code');
    row.textContent = `${file.path} · 0x${file.offset.toString(16)} · ${file.size.toLocaleString()} bytes`;
    hash.textContent = `SHA-256 ${file.sha256}`; row.append(hash); $('#release-files').append(row);
  });
  $('#flash-status').textContent = 'Release verified. No firmware written yet.';
}, $('#flash-status'));
$('#flash-consent').onchange = deviceUI;
$('#flash-mode').onchange = () => {
  $('#flash-consent').checked = false;
  $('#flash').textContent = $('#flash-mode').value === 'update' ? 'Choose board & update' : 'Choose board & install';
  deviceUI();
};
$('#flash').onclick = () => operation(async () => {
  const mode = $('#flash-mode').value, count = mode === 'update' ? 3 : 5;
  if (!confirm(`${mode === 'update' ? 'Update' : 'Install'} Spidarium ${firmware.manifest.version} on your ESP32-S3? Do not unplug until complete. NVS will not be erased.`)) return;
  const port = consoleSession.connected ? selectedPort : await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
  let tools;
  try { tools = await import('./vendor/esptool.js'); }
  catch { throw new Error('Local esptool bundle missing. Build and serve the production website.'); }
  $('#flash-status').textContent = 'Entering bootloader. If needed, hold BOOT and tap RESET.';
  const verified = await loadFirmware(new URL('./firmware/manifest.json', import.meta.url));
  if (JSON.stringify(verified.manifest) !== JSON.stringify(firmware.manifest)) throw new Error('Release changed since confirmation; check release again');
  await flashFirmware({ port, consoleSession, firmware: verified, tools, mode, log, progress: (index, written, total) => {
    $('#flash-progress').value = (index + written / total) / count * 100;
    $('#flash-status').textContent = `Writing image ${index + 1} / ${count} · ${Math.round(written / total * 100)}%`;
  } });
  $('#flash-progress').value = 100; $('#flash-status').textContent = mode === 'update'
    ? 'Update written, device MD5 checks passed and all NVS bytes verified unchanged. Press RESET.'
    : 'Images written and device MD5 checks passed. Press RESET. NVS was not erased.';
  deviceSchemaVerified = false;
}, $('#flash-status'));
$('#clear-log').onclick = () => { $('#serial-log').textContent = ''; };
navigator.serial?.addEventListener('disconnect', () => {
  if (!consoleSession.connected) return;
  void consoleSession.close().then(() => { deviceSchemaVerified = false; deviceUI(); }).catch(error => { log(error.message); deviceUI(); });
});
deviceUI();
