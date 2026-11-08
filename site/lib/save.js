const widths = { u8: 1, u16: 2, u32: 4, i16: 2, i32: 4, f32: 4 };
const getters = { u8: 'getUint8', u16: 'getUint16', u32: 'getUint32', i16: 'getInt16', i32: 'getInt32', f32: 'getFloat32' };

export function fromHex(hex) {
  if (typeof hex !== 'string' || !hex.length || hex.length % 2 || !/^[\da-f]+$/i.test(hex)) throw new Error('Invalid save hex');
  return Uint8Array.from(hex.match(/../g), pair => parseInt(pair, 16));
}
export const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

export function validateSchema(schema) {
  if (!schema || !Number.isInteger(schema.magic) || schema.magic < 0 || schema.magic > 0xffffffff ||
      ![2, 3].includes(schema.version) || schema.headerSize !== 12 || !Array.isArray(schema.payloadSizes) ||
      !schema.payloadSizes.length || schema.payloadSizes.some(n => !Number.isInteger(n) || n < 1 || n > 132) ||
      !Array.isArray(schema.fields)) throw new Error('Unsupported save schema');
  const names = new Set();
  for (const field of schema.fields) {
    const width = field.type === 'string' ? field.length : widths[field.type];
    if (!field.name || names.has(field.name) || !Number.isInteger(field.offset) || field.offset < 0 ||
        !Number.isInteger(width) || width < 1 || field.offset + width > Math.max(...schema.payloadSizes)) {
      throw new Error('Invalid save schema field');
    }
    names.add(field.name);
  }
  return schema;
}

export function decodeSave(bytes, schema) {
  validateSchema(schema);
  if (!(bytes instanceof Uint8Array) || bytes.length < 12 || bytes.length > 144) throw new Error('Save must be a raw 12-byte header and 120/132-byte payload');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const header = { magic: view.getUint32(0, true), version: view.getUint16(4, true), reserved: view.getUint16(6, true), size: view.getUint32(8, true) };
  if (header.magic !== schema.magic) throw new Error('Save magic does not match Spidarium');
  if (!(header.version === schema.version || schema.version === 3 && header.version === 2) || header.reserved !== 0) throw new Error('Unsupported save version or reserved header');
  if (header.version === 3 && header.size !== 132) throw new Error('Version 3 requires the full save payload');
  if (!schema.payloadSizes.includes(header.size) || bytes.length !== header.size + 12) throw new Error('Save payload length mismatch');
  const fields = {};
  for (const field of schema.fields) {
    const offset = 12 + field.offset;
    const width = field.type === 'string' ? field.length : widths[field.type];
    if (['auto_feed', 'auto_hunt'].includes(field.name) && header.version < 3) {
      fields[field.name] = 0; continue;
    }
    if (field.offset + width > header.size) { fields[field.name] = '(not present in older save)'; continue; }
    fields[field.name] = field.type === 'string'
      ? new TextDecoder().decode(bytes.subarray(offset, offset + width)).split('\0')[0]
      : view[getters[field.type]](offset, true);
    if (typeof fields[field.name] === 'number' && !Number.isFinite(fields[field.name])) throw new Error(`Invalid number in ${field.name}`);
    if (['auto_feed', 'auto_hunt'].includes(field.name) && fields[field.name] > 1) throw new Error(`Invalid checkbox in ${field.name}`);
  }
  return { header, fields };
}

export function validateWifi(ssid, password) {
  const bytes = value => new TextEncoder().encode(value).length;
  if (typeof ssid !== 'string' || typeof password !== 'string' || /[|\r\n\0]/.test(ssid + password)) throw new Error('SSID/password cannot contain |, line breaks or NUL');
  if (ssid !== ssid.trim()) throw new Error('SSID cannot start/end with whitespace (console protocol trims commands)');
  if (bytes(ssid) < 1 || bytes(ssid) > 32) throw new Error('SSID must be 1–32 UTF-8 bytes');
  if (password && (bytes(password) < 8 || bytes(password) > 63)) throw new Error('Password must be empty (open network) or 8–63 UTF-8 bytes');
  if (password && password !== password.trimEnd()) throw new Error('Password cannot end in whitespace (console protocol trims commands)');
  return `wifi add ${ssid}|${password}`;
}
