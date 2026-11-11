import test from 'node:test';
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';
import { validateManifest, validatePartitions, loadFirmware, flashFirmware } from '../lib/flasher.js';

function table() {
  const bytes = new Uint8Array(160).fill(255), view = new DataView(bytes.buffer);
  const parts = [['nvs', 1, 2, 0x9000, 0x6000], ['otadata', 1, 0, 0x10000, 0x2000], ['factory', 0, 0, 0x20000, 0x300000], ['storage', 1, 0x82, 0x920000, 0x600000]];
  parts.forEach(([label, type, subtype, offset, size], index) => {
    const at = index * 32; bytes.fill(0, at, at + 32); view.setUint16(at, 0x50aa, true);
    bytes[at + 2] = type; bytes[at + 3] = subtype; view.setUint32(at + 4, offset, true); view.setUint32(at + 8, size, true);
    bytes.set(new TextEncoder().encode(label), at + 12);
  });
  return bytes;
}
async function release() {
  const files = [['bootloader.bin', 0], ['partition-table.bin', 0x8000], ['ota_data_initial.bin', 0x10000], ['spidarium.bin', 0x20000], ['storage.bin', 0x920000]];
  const data = new Map(files.map(([name]) => [name, name === 'partition-table.bin' ? table() : new Uint8Array([1, 2, 3, 4])]));
  const manifest = { version: 'test-1', chip: 'ESP32-S3', baud: 460800, files: await Promise.all(files.map(async ([path, offset]) => {
    const bytes = data.get(path), digest = await webcrypto.subtle.digest('SHA-256', bytes);
    return { path, offset, size: bytes.length, sha256: Buffer.from(digest).toString('hex') };
  })) };
  return { manifest, data };
}
test('manifest accepts actual release offsets and rejects NVS, overlap, traversal and wrong chip', async () => {
  const { manifest } = await release(); assert.equal(validateManifest(manifest), manifest);
  for (const mutate of [m => { m.chip = 'ESP32'; }, m => { m.files[4].path = '../secret'; }, m => { m.files[2].offset = 0x9000; },
    m => { m.files[3].offset = 0x10000; }, m => { m.files[4].size = 0xffffff; }, m => { m.files[0].sha256 = 'bad'; }]) {
    const copy = structuredClone(manifest); mutate(copy); assert.throws(() => validateManifest(copy));
  }
});
test('partition table must protect NVS and match the image addresses/capacities', async () => {
  const { manifest } = await release(); assert.equal(validatePartitions(manifest, table()).length, 4);
  const bad = table(); new DataView(bad.buffer).setUint32(8, 0x7000, true);
  assert.throws(() => validatePartitions(manifest, bad), /NVS/);
  const moved = structuredClone(manifest); moved.files[4].offset = 0x930000;
  assert.throws(() => validatePartitions(moved, table()), /partition/);
});
test('firmware fetch verifies every size and SHA-256 at subpath-relative URLs', async () => {
  const { manifest, data } = await release(); const urls = [];
  const fetcher = async url => {
    urls.push(String(url)); const name = new URL(url).pathname.split('/').at(-1);
    return name === 'manifest.json' ? new Response(JSON.stringify(manifest)) : new Response(data.get(name));
  };
  const loaded = await loadFirmware(new URL('https://example.test/Spidarium/site/firmware/manifest.json'), fetcher, webcrypto);
  assert.equal(loaded.images.length, 5); assert.ok(loaded.images[0].data instanceof Uint8Array);
  assert.ok(urls.every(url => url.startsWith('https://example.test/Spidarium/site/firmware/')));
  data.set('storage.bin', new Uint8Array([9, 9, 9, 9]));
  await assert.rejects(loadFirmware(new URL(urls[0]), fetcher, webcrypto), /SHA-256/);
  data.set('storage.bin', new Uint8Array([1]));
  await assert.rejects(loadFirmware(new URL(urls[0]), fetcher, webcrypto), /Size/);
});
test('flash closes console first, uses real writeFlash API with eraseAll false and disconnects', async () => {
  const { manifest, data } = await release(), order = [];
  const firmware = { manifest, images: manifest.files.map(file => ({ data: data.get(file.path), address: file.offset })) };
  const tools = {
    md5: () => 'md5',
    Transport: class { constructor(_port, tracing) { assert.equal(tracing, false); order.push('transport'); } async disconnect() { order.push('disconnect'); } },
    ESPLoader: class {
      constructor() { this.chip = { CHIP_NAME: 'ESP32-S3' }; }
      async main() { order.push('main'); }
      async writeFlash(options) {
        order.push('write'); assert.equal(options.eraseAll, false); assert.equal(options.flashMode, 'keep');
        assert.equal(options.calculateMD5Hash, tools.md5); assert.equal(options.fileArray.length, 5);
      }
    },
  };
  await flashFirmware({ port: {}, consoleSession: { close: async () => order.push('close-console') }, firmware, tools, progress() {}, log() {} });
  assert.deepEqual(order, ['close-console', 'transport', 'main', 'write', 'disconnect']);
  tools.ESPLoader = class { constructor() { this.chip = { CHIP_NAME: 'ESP32-C3' }; } async main() {} async writeFlash() { assert.fail('wrong chip was written'); } };
  await assert.rejects(flashFirmware({ port: {}, consoleSession: { close: async () => {} }, firmware, tools, progress() {}, log() {} }), /Wrong chip/);
  tools.ESPLoader = class { constructor() { this.chip = { CHIP_NAME: 'ESP32-S3' }; } async main() {} async writeFlash() { throw new Error('USB write failed'); } };
  await assert.rejects(flashFirmware({ port: {}, consoleSession: { close: async () => {} }, firmware, tools, progress() {}, log() {} }), /USB write failed/);
  assert.equal(order.at(-1), 'disconnect');
});

test('updater preserves bootloader/table, selects factory last and verifies complete NVS bytes', async () => {
  const {manifest,data}=await release();
  const firmware={manifest,images:manifest.files.map(file=>({data:data.get(file.path),address:file.offset}))};
  let written=false,corrupt=false,badTable=false,reads=[];
  const tools={
    md5:()=> 'md5',
    Transport:class {async disconnect(){}},
    ESPLoader:class {
      constructor(){this.chip={CHIP_NAME:'ESP32-S3'};}
      async main(){}
      async readFlash(address,length){
        reads.push([address,length]);
        if(address===0x8000){const bytes=table();if(badTable)bytes[20]^=1;return bytes;}
        const bytes=new Uint8Array(length).fill(42);
        if(corrupt&&written)bytes[0]^=1;
        return bytes;
      }
      async writeFlash(options){
        written=true;
        assert.equal(options.eraseAll,false);
        assert.deepEqual(options.fileArray.map(file=>file.address),[0x20000,0x920000,0x10000]);
      }
    },
  };
  const args={mode:'update',port:{},consoleSession:{async close(){}},firmware,tools,progress(){},log(){}};
  await flashFirmware(args);
  assert.deepEqual(reads,[[0x8000,table().length],[0x9000,0x6000],[0x9000,0x6000]]);
  written=false;corrupt=true;
  await assert.rejects(flashFirmware(args),/Save-region verification failed/);
  written=false;badTable=true;
  await assert.rejects(flashFirmware(args),/partition layout differs/);
  assert.equal(written,false);
  await assert.rejects(flashFirmware({...args,mode:'erase'}),/install or update/);
});
