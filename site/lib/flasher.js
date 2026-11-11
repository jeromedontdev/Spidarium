const FLASH_SIZE = 16 * 1024 * 1024;
export function validateManifest(manifest) {
  if (!manifest || typeof manifest.version !== 'string' || manifest.chip !== 'ESP32-S3' || manifest.baud !== 460800 ||
      !Array.isArray(manifest.files) || manifest.files.length !== 5) throw new Error('Unsupported firmware manifest');
  const names = new Set();
  const sorted = [...manifest.files].sort((a, b) => a.offset - b.offset);
  const expected = { 'bootloader.bin': 0, 'partition-table.bin': 32768 };
  let end = 0;
  for (const file of sorted) {
    if (!/^(bootloader|partition-table|ota_data_initial|spidarium|storage)\.bin$/.test(file.path) ||
        names.has(file.path) || !/^[a-f\d]{64}$/i.test(file.sha256) || !Number.isInteger(file.size) || file.size < 1 ||
        !Number.isInteger(file.offset) || file.offset % 4096 || file.offset < end || file.offset + file.size > FLASH_SIZE ||
        (file.path in expected && file.offset !== expected[file.path]) ||
        (file.path === 'storage.bin' && file.offset <= 65536) ||
        (file.offset < 0xf000 && file.offset + Math.ceil(file.size / 4096) * 4096 > 0x9000)) throw new Error('Invalid firmware files or attempted NVS overwrite');
    names.add(file.path); end = file.offset + Math.ceil(file.size / 4096) * 4096;
  }
  return manifest;
}
export function validatePartitions(manifest, partitionBytes) {
  const table = new DataView(partitionBytes.buffer, partitionBytes.byteOffset, partitionBytes.byteLength);
  const partitions = [];
  for (let offset = 0; offset + 32 <= partitionBytes.length; offset += 32) {
    if (table.getUint16(offset, true) !== 0x50aa) break;
    partitions.push({
      type: table.getUint8(offset + 2), subtype: table.getUint8(offset + 3),
      offset: table.getUint32(offset + 4, true), size: table.getUint32(offset + 8, true),
      label: new TextDecoder().decode(partitionBytes.subarray(offset + 12, offset + 28)).split('\0')[0],
    });
  }
  const nvs = partitions.find(part => part.type === 1 && part.subtype === 2 && part.label === 'nvs');
  if (!nvs || nvs.offset !== 0x9000 || nvs.size !== 0x6000) throw new Error('Partition table does not preserve the expected NVS region');
  const regions = { 'ota_data_initial.bin': partitions.find(part => part.type === 1 && part.subtype === 0),
    'spidarium.bin': partitions.find(part => part.type === 0 && part.offset === manifest.files.find(file => file.path === 'spidarium.bin').offset),
    'storage.bin': partitions.find(part => part.label === 'storage') };
  for (const file of manifest.files) {
    if (file.path in regions) {
      const region = regions[file.path];
      if (!region || region.offset !== file.offset || file.size > region.size) throw new Error(`Image does not fit partition: ${file.path}`);
    }
    const end = file.offset + Math.ceil(file.size / 4096) * 4096;
    if (file.offset < nvs.offset + nvs.size && end > nvs.offset) throw new Error('Firmware would overwrite NVS');
  }
  return partitions;
}
export async function loadFirmware(manifestURL, fetcher = fetch, cryptoAPI = globalThis.crypto) {
  const response = await fetcher(manifestURL, { cache: 'no-store' });
  if (!response.ok) throw new Error('No published firmware manifest found. Build/release the firmware first.');
  const manifest = validateManifest(await response.json());
  const images = [];
  for (const file of manifest.files) {
    const response = await fetcher(new URL(file.path, manifestURL), { cache: 'no-store' });
    if (!response.ok) throw new Error(`Missing firmware: ${file.path}`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (bytes.length !== file.size) throw new Error(`Size mismatch: ${file.path}`);
    const digest = Array.from(new Uint8Array(await cryptoAPI.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
    if (digest !== file.sha256.toLowerCase()) throw new Error(`SHA-256 mismatch: ${file.path}`);
    images.push({ data: bytes, address: file.offset });
  }
  validatePartitions(manifest, images[manifest.files.findIndex(file => file.path === 'partition-table.bin')].data);
  return { manifest, images };
}
export async function flashFirmware({ port, consoleSession, firmware, progress, log, tools, mode = 'install' }) {
  if (!['install', 'update'].includes(mode)) throw new Error('Choose install or update mode');
  if (typeof tools.ESPLoader !== 'function' || typeof tools.Transport !== 'function' || typeof tools.md5 !== 'function') throw new Error('Incomplete local flasher bundle');
  validateManifest(firmware.manifest);
  validatePartitions(firmware.manifest, firmware.images[firmware.manifest.files.findIndex(file => file.path === 'partition-table.bin')].data);
  await consoleSession.close();
  const transport = new tools.Transport(port, false);
  const terminal = { clean() {}, write: log, writeLine: log };
  const loader = new tools.ESPLoader({ transport, baudrate: firmware.manifest.baud, terminal, debugLogging: false });
  let before;
  try {
    await loader.main();
    if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw new Error('Wrong chip: this release requires ESP32-S3');
    let images = firmware.images;
    if (mode === 'update') {
      const expected = firmware.images[firmware.manifest.files.findIndex(file => file.path === 'partition-table.bin')].data;
      const installed = await loader.readFlash(0x8000, expected.length);
      if (installed.length !== expected.length || !expected.every((byte, i) => byte === installed[i]))
        throw new Error('Installed partition layout differs. Nothing written. Export your pet, then use full installation for this migration.');
      before = await loader.readFlash(0x9000, 0x6000);
      if (before.length !== 0x6000) throw new Error('Could not read the complete save region. Nothing written.');
      // Select the factory application only after both application and art writes.
      images = ['spidarium.bin', 'storage.bin', 'ota_data_initial.bin'].map(name =>
        firmware.images[firmware.manifest.files.findIndex(file => file.path === name)]);
      log('Update: preserving bootloader, partition table and NVS; verifying save-region bytes after writing.');
    }
    await loader.writeFlash({
      fileArray: images, flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep',
      eraseAll: false, compress: true,
      calculateMD5Hash: tools.md5,
      reportProgress: (fileIndex, written, total) => progress(fileIndex, written, total),
    });
    if (before) {
      const after = await loader.readFlash(0x9000, before.length);
      const same = after.length === before.length && before.every((byte, i) => byte === after[i]);
      before.fill(0); after.fill(0);
      if (!same) throw new Error('Save-region verification failed. Do not start a new pet; retain your backup and recover over USB.');
      log('Update verified: all NVS bytes unchanged. No save data was logged or uploaded.');
    }
    log('Flash writes completed. Press RESET on the board when ready; no automatic reboot.');
  } finally {
    before?.fill(0);
    await transport.disconnect();
  }
}
