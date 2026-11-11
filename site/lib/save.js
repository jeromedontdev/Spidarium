const widths = { u8: 1, u16: 2, u32: 4, i16: 2, i32: 4, f32: 4 };
const getters = { u8: 'getUint8', u16: 'getUint16', u32: 'getUint32', i16: 'getInt16', i32: 'getInt32', f32: 'getFloat32' };
const payloadByVersion = { 2: 132, 3: 132, 4: 132, 5: 136, 6: 152, 7: 160, 8: 236, 9: 248, 10: 260, 11:260, 12:268 };
const fieldWidth = field => field.type === 'string' ? field.length :
  field.type === 'decor[12]' && field.length === 72 ? 72 : widths[field.type];

export function fromHex(hex) {
  if (typeof hex !== 'string' || !hex.length || hex.length % 2 || !/^[\da-f]+$/i.test(hex)) throw new Error('Invalid save hex');
  return Uint8Array.from(hex.match(/../g), pair => parseInt(pair, 16));
}
export const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

export function validateSchema(schema) {
  if (!schema || !Number.isInteger(schema.magic) || schema.magic < 0 || schema.magic > 0xffffffff ||
      ![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12].includes(schema.version) || schema.headerSize !== 12 || !Array.isArray(schema.payloadSizes) ||
      !schema.payloadSizes.length || schema.payloadSizes.some(n => !Number.isInteger(n) || n < 1 || n > 268) ||
      !Array.isArray(schema.fields)) throw new Error('Unsupported save schema');
  const names = new Set();
  for (const field of schema.fields) {
    const width = fieldWidth(field);
    if (!field.name || names.has(field.name) || !Number.isInteger(field.offset) || field.offset < 0 ||
        !Number.isInteger(width) || width < 1 || field.offset + width > Math.max(...schema.payloadSizes)) {
      throw new Error('Invalid save schema field');
    }
    if(schema.version>=10 && (schema.maxPayload!==20480 || schema.decorTail?.offset!==(schema.version>=12?268:260) ||
        schema.decorTail?.stride!==6 || schema.decorTail?.countField!=='decor_extra_count' ||
        !schema.fields.some(field=>field.name==='decor_extra_count'&&field.type==='u32'&&field.offset===256)))
      throw new Error('Unsupported variable decoration layout');
    names.add(field.name);
  }
  return schema;
}

export function decodeSave(bytes, schema) {
  validateSchema(schema);
  if (!(bytes instanceof Uint8Array) || bytes.length < 12 ||
      bytes.length > 12 + (schema.maxPayload??Math.max(...schema.payloadSizes))) throw new Error('Save must be a raw header and supported payload');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const header = { magic: view.getUint32(0, true), version: view.getUint16(4, true), reserved: view.getUint16(6, true), size: view.getUint32(8, true) };
  if (header.magic !== schema.magic) throw new Error('Save magic does not match Spidarium');
  if (!(header.version === schema.version || header.version >= 2 && header.version < schema.version) || header.reserved !== 0) throw new Error('Unsupported save version or reserved header');
  if(bytes.length!==header.size+12)throw new Error('Save payload length mismatch');
  if (header.version >= 3 && header.version < 10 && header.size !== payloadByVersion[header.version]) throw new Error('Version 3+ requires its full save payload');
  const prefix=header.version>=12?268:260;
  if (header.version>=10 && (header.size<prefix || header.size>schema.maxPayload ||
      header.size!==prefix+view.getUint32(268,true)*6))throw new Error('Variable decoration payload length mismatch');
  if ((header.version<10 && !schema.payloadSizes.includes(header.size) && header.size !== payloadByVersion[header.version]) ||
      bytes.length !== header.size + 12) throw new Error('Save payload length mismatch');
  const fields = {};
  for (const field of schema.fields) {
    const offset = 12 + field.offset;
    const width = fieldWidth(field);
    if (['auto_feed', 'auto_hunt'].includes(field.name) && header.version < 3) {
      fields[field.name] = 0; continue;
    }
    if (field.name === 'volume' && header.version < 4) { fields.volume = 70; continue; }
    if (field.name === 'music_volume' && header.version < 11) { fields.music_volume = 100; continue; }
    if (field.name === 'music_track' && header.version < 11) { fields.music_track = 0; continue; }
    if (header.version<12&&['catalogue_owned_high','backgrounds_owned_extra','borders_owned','borders_owned_extra','border_id'].includes(field.name)){
      fields[field.name]=field.name==='border_id'?255:0;continue;
    }
    if (field.name === 'last_seen' && header.version < 5) { fields.last_seen = 0; continue; }
    if (field.offset + width > header.size || header.version < (field.minVersion ?? 0)) {
      fields[field.name] = '(not present in older save)'; continue;
    }
    fields[field.name] = field.type === 'decor[12]'
      ? Array.from({ length: 12 }, (_, slot) => ({
        kind: view.getUint8(offset + slot * 6),
        blocking: view.getUint8(offset + slot * 6 + 1),
        x: view.getUint16(offset + slot * 6 + 2, true),
        y: view.getUint16(offset + slot * 6 + 4, true),
      })) : field.type === 'string'
      ? new TextDecoder().decode(bytes.subarray(offset, offset + width)).split('\0')[0]
      : view[getters[field.type]](offset, true);
    if (typeof fields[field.name] === 'number' && !Number.isFinite(fields[field.name])) throw new Error(`Invalid number in ${field.name}`);
    if (['auto_feed', 'auto_hunt'].includes(field.name) && fields[field.name] > 1) throw new Error(`Invalid checkbox in ${field.name}`);
    if (field.name === 'volume' && fields.volume > 100) throw new Error('Invalid volume in save');
    if ((field.name === 'music_volume' && fields.music_volume > 100) ||
        (field.name === 'music_track' && fields.music_track > 2)) throw new Error('Invalid music preference in save');
    if(['music_enabled','lights_dimmed'].includes(field.name)&&fields[field.name]>1)
      throw new Error(`Invalid ${field.name} in save`);
    const powerLimits = { power_mode: 2, screen_timeout: 4, deep_sleep_after: 4, rtc_wake: 2 };
    if (field.name in powerLimits && fields[field.name] > powerLimits[field.name])
      throw new Error(`Invalid ${field.name} in save`);
  }
  if(header.version>=8){
    const kinds=header.version>=12?68:header.version>=10?34:16;
    for(const item of fields.decor){
      if((item.kind!==255&&item.kind>=kinds)||item.blocking>1||
          item.x<0||item.x>480||item.y<0||item.y>480)
        throw new Error('Invalid inline decor record');
    }
  }
  if (header.version >= 9 &&
      (fields.background_id >= (header.version>=12?48:24) || !(fields.backgrounds_owned & 1) ||
       (header.version<12&&fields.backgrounds_owned > 0xffffff) ||
       !((fields.background_id<32?fields.backgrounds_owned:fields.backgrounds_owned_extra)&(1<<(fields.background_id%32)))))
    throw new Error('Invalid background ownership in save');
  if(header.version>=12&&fields.border_id!==255&&
      (fields.border_id>=48||!((fields.border_id<32?fields.borders_owned:fields.borders_owned_extra)&(1<<(fields.border_id%32)))))
    throw new Error('Invalid border ownership in save');
  if(header.version>=10){
    if(header.version<12&&fields.decor_owned_extra>3)throw new Error('Invalid extended decor ownership');
    for(let i=0;i<fields.decor_extra_count;i++){
      const offset=12+prefix+i*6;
      const item={kind:view.getUint8(offset),blocking:view.getUint8(offset+1),
        x:view.getInt16(offset+2,true),y:view.getInt16(offset+4,true)};
      if((item.kind!==255&&item.kind>=(header.version>=12?68:34))||item.blocking>1||
          item.x<0||item.x>480||item.y<0||item.y>480)
        throw new Error('Invalid extended decor record');
      fields.decor.push(item);
    }
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
