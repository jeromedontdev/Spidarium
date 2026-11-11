import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { decodeSave, fromHex, toHex, validateSchema, validateWifi } from '../lib/save.js';

const schema = {
  magic: 0x53504944, version: 2, headerSize: 12, payloadSizes: [120, 132],
  fields: [{ name: 'hunger', type: 'u8', offset: 0 }, { name: 'heading', type: 'i16', offset: 36 },
    { name: 'name', type: 'string', offset: 100, length: 16 }, { name: 'silk', type: 'u32', offset: 120 },
    { name: 'radians', type: 'f32', offset: 88 }],
};
function blob(size = 132) {
  const bytes = new Uint8Array(size + 12), view = new DataView(bytes.buffer);
  view.setUint32(0, schema.magic, true); view.setUint16(4, 2, true); view.setUint32(8, size, true);
  bytes[12] = 95; view.setInt16(48, -45, true); view.setFloat32(100, .5, true);
  bytes.set(new TextEncoder().encode('Spider'), 112);
  if (size === 132) view.setUint32(132, 65538, true);
  return bytes;
}
test('raw 144-byte save roundtrips exact hex and decodes little-endian signed fields', () => {
  const bytes = blob(), parsed = decodeSave(fromHex(toHex(bytes)), schema);
  assert.equal(toHex(bytes).length, 288); assert.equal(parsed.header.size, 132);
  assert.equal(parsed.fields.hunger, 95); assert.equal(parsed.fields.heading, -45);
  assert.equal(parsed.fields.name, 'Spider'); assert.equal(parsed.fields.silk, 65538); assert.equal(parsed.fields.radians, .5);
});
test('legacy 132-byte raw save omits appended fields without out-of-bounds reads', () => {
  const parsed = decodeSave(blob(120), schema);
  assert.equal(parsed.header.size, 120); assert.match(parsed.fields.silk, /older save/);
});
test('subarray DataView respects its offset', () => {
  const wrapped = new Uint8Array(160); wrapped.set(blob(), 8);
  assert.equal(decodeSave(wrapped.subarray(8, 152), schema).fields.name, 'Spider');
});
test('reject malformed hex, magic, reserved/version and truncated/oversized payloads', () => {
  for (const hex of ['', 'a', 'xy', 'ab cd', '00\n00']) assert.throws(() => fromHex(hex));
  for (const index of [0, 4, 6, 8]) { const bytes = blob(); bytes[index] ^= 1; assert.throws(() => decodeSave(bytes, schema)); }
  assert.throws(() => decodeSave(blob().subarray(0, 143), schema));
  assert.throws(() => decodeSave(new Uint8Array(145), schema));
});
test('schema rejects unsafe offsets, unsupported types and duplicate names', () => {
  assert.throws(() => validateSchema({ ...schema, fields: [{ name: 'bad', type: 'u32', offset: 131 }] }));
  assert.throws(() => validateSchema({ ...schema, fields: [{ name: 'bad', type: 'bool', offset: 0 }] }));
  assert.throws(() => validateSchema({ ...schema, fields: [schema.fields[0], schema.fields[0]] }));
});
test('WiFi validation enforces byte lengths, open networks and protocol injection boundary', () => {
  assert.equal(validateWifi('home', 'abcdefgh'), 'wifi add home|abcdefgh');
  assert.equal(validateWifi('Open Network', ''), 'wifi add Open Network|');
  for (const [ssid, pass] of [['', ''], ['x'.repeat(33), ''], ['é'.repeat(17), ''], ['home|evil', 'abcdefgh'],
    ['home', 'abc\n12345'], ['home', 'abcdefg'], ['home', 'x'.repeat(64)], [' home', 'abcdefgh'], ['home', 'abcdefgh '],
    ['home', 'abc\0defgh']]) assert.throws(() => validateWifi(ssid, pass));
});
test('bundled hardware-compiled schema decodes both actual ABI payload sizes', async () => {
  const actual = validateSchema(JSON.parse(await readFile(new URL('../save-schema.json', import.meta.url), 'utf8')));
  assert.equal(actual.version, 12);
  assert.equal(actual.fields.length, 52);
  for (const size of actual.payloadSizes) {
    const bytes = new Uint8Array(12 + size), view = new DataView(bytes.buffer);
    view.setUint32(0, actual.magic, true); view.setUint16(4, size === 120 ? 2 : actual.version, true); view.setUint32(8, size, true);
    bytes[12] = 83;
    if (size === 268) { bytes[12 + 236] = 1; bytes[12 + 266] = 255; }
    const name = actual.fields.find(field => field.name === 'name');
    bytes.set(new TextEncoder().encode('Schema Test'), 12 + name.offset);
    const silk = actual.fields.find(field => field.name === 'silk');
    if (silk.offset + 4 <= size) view.setUint32(12 + silk.offset, 12345, true);
    const parsed = decodeSave(fromHex(toHex(bytes)), actual);
    assert.equal(parsed.fields.name, 'Schema Test'); assert.equal(parsed.fields.hunger, 83);
    if (size === 268) assert.equal(parsed.fields.silk, 12345);
    else assert.match(parsed.fields.silk, /older save/);
    assert.equal(parsed.fields.volume, size === 268 ? 0 : 70);
    if (size === 268) assert.equal(parsed.fields.decor.length, 12);
  }
});
test('current compiled schema reads v5-v9 prefixes and all twelve little-endian decor slots', async () => {
  const actual = validateSchema(JSON.parse(await readFile(new URL('../save-schema.json', import.meta.url), 'utf8')));
  for (const [version, size] of [[2, 132], [5, 136], [6, 152], [7, 160], [8, 236], [9, 248]]) {
    const bytes = new Uint8Array(12 + size), view = new DataView(bytes.buffer);
    view.setUint32(0, actual.magic, true); view.setUint16(4, version, true); view.setUint32(8, size, true);
    if (version >= 8) {
      if (version === 9) bytes[12 + 236] = 1;
      const start = 12 + actual.fields.find(field => field.name === 'decor').offset;
      for (let slot = 0; slot < 12; slot++) {
        bytes[start + slot * 6] = slot;
        bytes[start + slot * 6 + 1] = slot % 2;
        view.setUint16(start + slot * 6 + 2, 280 + slot, true);
        view.setUint16(start + slot * 6 + 4, 340 + slot, true);
      }
    }
    const parsed = decodeSave(fromHex(toHex(bytes)), actual);
    assert.equal(toHex(bytes).length, (12 + size) * 2);
    if (version < 8) assert.match(parsed.fields.decor, /older save/);
    else for (let slot = 0; slot < 12; slot++) assert.deepEqual(parsed.fields.decor[slot],
      { kind: slot, blocking: slot % 2, x: 280 + slot, y: 340 + slot });
    const wrongSize = bytes.slice(); new DataView(wrongSize.buffer).setUint32(8, size - 1, true);
    assert.throws(() => decodeSave(wrongSize, actual), /payload/);
  }
  const badDecor = structuredClone(actual);
  badDecor.fields.find(field => field.name === 'decor').length = 71;
  assert.throws(() => validateSchema(badDecor), /schema field/);
});
test('v3 checkbox bytes validate while v2 padding always means off', () => {
  const actual = { ...schema, version: 3, fields: [
    { name: 'auto_feed', offset: 129, type: 'u8', minVersion: 3 },
    { name: 'auto_hunt', offset: 130, type: 'u8', minVersion: 3 },
  ] };
  const bytes = blob(); bytes[141] = bytes[142] = 255;
  assert.deepEqual(decodeSave(bytes, actual).fields, { auto_feed: 0, auto_hunt: 0 });
  new DataView(bytes.buffer).setUint16(4, 3, true);
  assert.throws(() => decodeSave(bytes, actual), /checkbox/);
  bytes[141] = 1; bytes[142] = 0;
  assert.deepEqual(decodeSave(bytes, actual).fields, { auto_feed: 1, auto_hunt: 0 });
  assert.throws(() => decodeSave(bytes, schema), /version/);
  const truncated = blob(120); new DataView(truncated.buffer).setUint16(4, 3, true);
  assert.throws(() => decodeSave(truncated, actual), /full save payload/);
});
