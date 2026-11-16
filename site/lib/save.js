const widths = { u8: 1, u16: 2, u32: 4, i16: 2, i32: 4, f32: 4 };
const getters = { u8: 'getUint8', u16: 'getUint16', u32: 'getUint32', i16: 'getInt16', i32: 'getInt32', f32: 'getFloat32' };
const payloadByVersion = { 2: 132, 3: 132, 4: 132, 5: 136, 6: 152, 7: 160, 8: 236, 9: 248, 10: 260, 11:260, 12:268, 13:268, 14:268, 15:860,16:3096,17:3096,18:3100,19:3100,20:3100,21:3100 };
export const lifeLayout={offset:860,size:2236,minVersion:16,visitor_day:0,season_highwater:4,
  progress:{offset:8,count:3},cooldown:11,daynight:12,weather:13,retired_count:14,light_gesture_disabled:{offset:15,minVersion:17},
  retired:{offset:16,count:3,stride:740,core:{offset:0,count:120},daily:{offset:120,count:24},
    companion:{offset:144,size:592},progress:{offset:736,count:3},reserved:739}};
const companionLayout = {
  offset:268,size:592,minVersion:15,request_day:0,seen_achievements:4,
  requests:{offset:8,count:2,stride:4,fields:{kind:0,target:1,progress:2,state:3}},
  treat_counts:{offset:16,count:12},gift_counts:{offset:28,count:36},play_counts:{offset:64,count:3},
  favorite_treat:67,favorite_gift:68,favorite_play:69,diary_count:70,clock_mode:71,initialized:72,
  bond_level:73,last_stage:74,was_sick:75,reserved:{offset:76,count:2},padding:{offset:78,count:2},
  diary:{offset:80,count:64,stride:8,fields:{day:0,kind:4,value:5,pinned:6,calendar:7}},
};
const sameLayout = (a,b) => a && typeof a==='object' && Object.keys(a).length===Object.keys(b).length &&
  Object.keys(b).every(key=>typeof b[key]==='object'?sameLayout(a[key],b[key]):a[key]===b[key]);
const fieldWidth = field => field.type === 'string' ? field.length :
  field.type === 'decor[12]' && field.length === 72 ? 72 : widths[field.type];

export function fromHex(hex) {
  if (typeof hex !== 'string' || !hex.length || hex.length % 2 || !/^[\da-f]+$/i.test(hex)) throw new Error('Invalid save hex');
  return Uint8Array.from(hex.match(/../g), pair => parseInt(pair, 16));
}
export const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

export function validateSchema(schema) {
  if (!schema || !Number.isInteger(schema.magic) || schema.magic < 0 || schema.magic > 0xffffffff ||
      ![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,16,17,18,19,20,21].includes(schema.version) || schema.headerSize !== 12 || !Array.isArray(schema.payloadSizes) ||
      !schema.payloadSizes.length || schema.payloadSizes.some(n => !Number.isInteger(n) || n < 1 || n > 3100) ||
      !Array.isArray(schema.fields)) throw new Error('Unsupported save schema');
  if(schema.version>=15 && (!sameLayout(schema.companion,companionLayout) ||
      schema.payloadSizes.length!==2 || schema.payloadSizes[0]!==120 || schema.payloadSizes[1]!== (schema.version>=18?3100:schema.version>=16?3096:860) ||
      !schema.fields.some(f=>f.name==='usb_sounds'&&f.type==='u8'&&f.offset===267&&f.minVersion===15)))
    throw new Error('Unsupported companion layout');
  if(schema.version>=16){
    const expected={...lifeLayout};
    if(schema.version===16){delete expected.light_gesture_disabled;expected.reserved=15;}
    if(!sameLayout(schema.life,expected))throw new Error('Unsupported life layout');
  }
  const names = new Set();
  for (const field of schema.fields) {
    const width = fieldWidth(field);
    if (!field.name || names.has(field.name) || !Number.isInteger(field.offset) || field.offset < 0 ||
        !Number.isInteger(width) || width < 1 || field.offset + width > Math.max(...schema.payloadSizes)) {
      throw new Error('Invalid save schema field');
    }
    if(schema.version>=10 && (schema.maxPayload!==(schema.version>=18?23308:schema.version>=16?23304:schema.version>=15?21072:20480) || schema.decorTail?.offset!==(schema.version>=18?3100:schema.version>=16?3096:schema.version>=15?860:schema.version>=12?268:260) ||
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
  const prefix=header.version>=18?3100:header.version>=16?3096:header.version>=15?860:header.version>=12?268:260;
  if (header.version>=10 && (header.size<prefix || header.size>(header.version>=18?23308:header.version>=16?23304:header.version>=15?21072:20480) ||
      header.size!==prefix+view.getUint32(268,true)*6))throw new Error('Variable decoration payload length mismatch');
  if ((header.version===2 && ![120,132].includes(header.size)) ||
      (header.version<10 && !schema.payloadSizes.includes(header.size) && header.size !== payloadByVersion[header.version]) ||
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
    if (field.name === 'usb_sounds' && header.version < 15) { fields.usb_sounds = 1; continue; }
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
        (field.name === 'music_track' && fields.music_track > (header.version>=14?9:2))) throw new Error('Invalid music preference in save');
    if(['music_enabled','lights_dimmed','usb_sounds','decor_locked','auto_clean'].includes(field.name)&&fields[field.name]>1)
      throw new Error(`Invalid ${field.name} in save`);
    const powerLimits = { power_mode: 2, screen_timeout: 4, deep_sleep_after: 4, rtc_wake: 2 };
    if (field.name in powerLimits && fields[field.name] > powerLimits[field.name])
      throw new Error(`Invalid ${field.name} in save`);
  }
  if(header.version>=8){
    const kinds=header.version>=12?68:header.version>=10?34:16;
    for(const item of fields.decor){
      if((item.kind!==255&&item.kind>=kinds)||item.blocking>(header.version>=13?2:1)||
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
      if((item.kind!==255&&item.kind>=(header.version>=12?68:34))||item.blocking>(header.version>=13?2:1)||
          item.x<0||item.x>480||item.y<0||item.y>480)
        throw new Error('Invalid extended decor record');
      fields.decor.push(item);
    }
  }
  if(schema.version>=15) {
    fields.companion=companionDefaults();
    if(header.version>=15) {
      const start=12+schema.companion.offset,c=fields.companion;
      c.request_day=view.getUint32(start,true);c.seen_achievements=view.getUint32(start+4,true);
      c.requests=Array.from({length:2},(_,i)=>Object.fromEntries(
        ['kind','target','progress','state'].map((key,j)=>[key,view.getUint8(start+8+i*4+j)])));
      for(const key of ['treat_counts','gift_counts','play_counts'])
        c[key]=Array.from(bytes.subarray(start+companionLayout[key].offset,
          start+companionLayout[key].offset+companionLayout[key].count));
      for(const key of ['favorite_treat','favorite_gift','favorite_play','diary_count','clock_mode',
        'initialized','bond_level','last_stage','was_sick'])c[key]=view.getUint8(start+companionLayout[key]);
      c.reserved=Array.from(bytes.subarray(start+76,start+78));
      c.diary=Array.from({length:64},(_,i)=>({day:view.getUint32(start+80+i*8,true),
        kind:bytes[start+84+i*8],value:bytes[start+85+i*8],pinned:bytes[start+86+i*8],calendar:bytes[start+87+i*8]}));
      if(c.diary_count>64)throw new Error('Invalid companion diary count');
      c.diary.length=c.diary_count;
      if(!companionValid(c))throw new Error('Invalid companion fields');
    }
  }
  if(schema.version>=16){
    fields.life=lifeDefaults();
    if(header.version>=16){
      const start=872,l=fields.life;
      l.visitor_day=view.getUint32(start,true);l.season_highwater=view.getUint32(start+4,true);
      l.progress=Array.from(bytes.subarray(start+8,start+11));
      for(const [key,offset] of Object.entries({cooldown:11,daynight:12,weather:13,retired_count:14,light_gesture_disabled:15}))
        l[key]=bytes[start+offset];
      if(header.version===16 && l.light_gesture_disabled!==0)throw new Error('Invalid legacy life reserved byte');
      if(l.retired_count>3)throw new Error('Invalid life retired pet count');
      l.retired=Array.from({length:l.retired_count},(_,i)=>{
        const o=start+16+i*740;
        return {core:Array.from(bytes.subarray(o,o+120)),daily:Array.from(bytes.subarray(o+120,o+144)),
          companion:readCompanion(bytes,o+144),progress:Array.from(bytes.subarray(o+736,o+739)),reserved:bytes[o+739]};
      });
      if(!lifeValid(l))throw new Error('Invalid life fields');
    }
  }
  return { header, fields };
}

// A template retains native-only prefix bytes that the human-readable schema omits.
export function encodeSave(fields, schema, template) {
  validateSchema(schema);
  if(schema.version!==21)throw new Error('Encoding requires save v21');
  const previous=template?decodeSave(template,schema):null;
  const defaults={silk:20,gifts_owned:0,brightness:50,auto_feed:0,auto_hunt:0,volume:70,last_seen:0,
    daily_day:0xffffffff,daily_base_day:0xffffffff,daily_task_base:0,daily_streak:0,daily_task_done:0,
    pet_xp_day:0xffffffff,pet_xp_today:0,decor_owned:0,
    decor:Array.from({length:12},()=>({kind:255,blocking:0,x:0,y:0})),
    backgrounds_owned:1,background_id:0,power_mode:0,screen_timeout:3,deep_sleep_after:0,rtc_wake:0,
    catalogue_owned_high:0,backgrounds_owned_extra:0,borders_owned:0,borders_owned_extra:0,border_id:255,
    decor_owned_extra:0,music_enabled:1,lights_dimmed:0,music_volume:100,music_track:0,usb_sounds:1,
    decor_extra_count:0,decor_locked:0,auto_clean:0,gift_path:0,climb_owned:0};
  if(previous?.header.size===120 && template[52] && template[50]<36)
    defaults.gifts_owned=2**template[50];
  const supplied=fields;
  fields={...defaults,...previous?.fields,...fields};
  for(const [key,value] of Object.entries(defaults))
    if(typeof fields[key]==='string')fields[key]=value;
  if(supplied.decor_extra_count===undefined && Array.isArray(supplied.decor))
    fields.decor_extra_count=Math.max(0,supplied.decor.length-12);
  const count=fields.decor_extra_count??Math.max(0,(fields.decor?.length??12)-12);
  if(!Number.isInteger(count)||count<0||3100+count*6>schema.maxPayload)throw new Error('Invalid decor count');
  const bytes=new Uint8Array(12+3100+count*6),view=new DataView(bytes.buffer);
  if(template) {
    const oldPrefix=payloadByVersion[previous.header.version]??previous.header.size;
    bytes.set(template.subarray(12,12+Math.min(oldPrefix,3100)),12);
  }
  view.setUint32(0,schema.magic,true);view.setUint16(4,schema.version,true);view.setUint32(8,bytes.length-12,true);
  for(const field of schema.fields) {
    const value=fields[field.name],offset=12+field.offset;
    if(value===undefined||typeof value==='string'&&field.type!=='string')continue;
    if(field.type==='decor[12]')continue;
    if(field.type==='string') {
      if(previous?.fields[field.name]===value)continue;
      const encoded=new TextEncoder().encode(value);
      if(encoded.length>=field.length)throw new Error('Save name too long');
      bytes.fill(0,offset,offset+field.length);bytes.set(encoded,offset);
    } else {
      if(!Number.isFinite(value)||(!field.type.startsWith('f')&&(!Number.isInteger(value)||
          value<0||value>2**(widths[field.type]*8)-1)))throw new Error(`Invalid ${field.name}`);
      view[getters[field.type].replace('get','set')](offset,value,true);
    }
  }
  view.setUint32(268,count,true);
  if(fields.decor) {
    if(fields.decor.length!==12+count)throw new Error('Invalid decor count');
    fields.decor.forEach((item,i)=>{
      const offset=12+(i<12?164+i*6:3100+(i-12)*6);
      for(const key of ['kind','blocking','x','y'])
        if(!Number.isInteger(item[key])||item[key]<0||item[key]>(['x','y'].includes(key)?480:255))
          throw new Error('Invalid decor record');
      bytes[offset]=item.kind;bytes[offset+1]=item.blocking;
      view.setInt16(offset+2,item.x,true);view.setInt16(offset+4,item.y,true);
    });
  }
  const c=fields.companion??companionDefaults(),start=280;
  if(!companionValid(c))throw new Error('Invalid companion fields');
  bytes.fill(0,start,start+592);
  view.setUint32(start,c.request_day,true);view.setUint32(start+4,c.seen_achievements,true);
  c.requests.forEach((r,i)=>['kind','target','progress','state'].forEach((key,j)=>bytes[start+8+i*4+j]=r[key]));
  for(const key of ['treat_counts','gift_counts','play_counts'])bytes.set(c[key],start+companionLayout[key].offset);
  for(const key of ['favorite_treat','favorite_gift','favorite_play','diary_count','clock_mode',
    'initialized','bond_level','last_stage','was_sick'])bytes[start+companionLayout[key]]=c[key];
  if(c.reserved)bytes.set(c.reserved,start+76);
  c.diary.forEach((memory,i)=>{
    const offset=start+80+i*8;view.setUint32(offset,memory.day,true);
    ['kind','value','pinned','calendar'].forEach((key,j)=>bytes[offset+4+j]=memory[key]);
  });
  const l=fields.life??lifeDefaults(),o=872;
  if(!lifeValid(l))throw new Error('Invalid life fields');
  bytes.fill(0,o,o+2236);
  view.setUint32(o,l.visitor_day,true);view.setUint32(o+4,l.season_highwater,true);
  bytes.set(l.progress,o+8);
  for(const [key,offset] of Object.entries({cooldown:11,daynight:12,weather:13,retired_count:14,light_gesture_disabled:15}))
    bytes[o+offset]=l[key]??0;
  l.retired.forEach((pet,i)=>{
    const start=o+16+i*740;
    bytes.set(pet.core,start);bytes.set(pet.daily,start+120);
    writeCompanion(bytes,start+144,pet.companion);
    bytes.set(pet.progress,start+736);bytes[start+739]=pet.reserved??0;
  });
  decodeSave(bytes,schema);
  return bytes;
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
import { companionDefaults, companionValid } from './companion.js?v=1.1.31';
import { lifeDefaults,lifeValid } from './life.js?v=1.1.31';

function readCompanion(bytes,o){
  const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),c=companionDefaults();
  c.request_day=v.getUint32(o,true);c.seen_achievements=v.getUint32(o+4,true);
  c.requests=Array.from({length:2},(_,i)=>Object.fromEntries(['kind','target','progress','state'].map((k,j)=>[k,bytes[o+8+i*4+j]])));
  for(const key of ['treat_counts','gift_counts','play_counts'])
    c[key]=Array.from(bytes.subarray(o+companionLayout[key].offset,o+companionLayout[key].offset+companionLayout[key].count));
  for(const key of ['favorite_treat','favorite_gift','favorite_play','diary_count','clock_mode','initialized','bond_level','last_stage','was_sick'])
    c[key]=bytes[o+companionLayout[key]];
  c.reserved=Array.from(bytes.subarray(o+76,o+78));
  if(c.diary_count>64)throw new Error('Invalid archived diary');
  c.diary=Array.from({length:c.diary_count},(_,i)=>({day:v.getUint32(o+80+i*8,true),
    kind:bytes[o+84+i*8],value:bytes[o+85+i*8],pinned:bytes[o+86+i*8],calendar:bytes[o+87+i*8]}));
  if(!companionValid(c))throw new Error('Invalid archived companion');
  return c;
}
function writeCompanion(bytes,o,c){
  if(!companionValid(c))throw new Error('Invalid archived companion');
  const v=new DataView(bytes.buffer);
  v.setUint32(o,c.request_day,true);v.setUint32(o+4,c.seen_achievements,true);
  c.requests.forEach((r,i)=>['kind','target','progress','state'].forEach((key,j)=>bytes[o+8+i*4+j]=r[key]));
  for(const key of ['treat_counts','gift_counts','play_counts'])bytes.set(c[key],o+companionLayout[key].offset);
  for(const key of ['favorite_treat','favorite_gift','favorite_play','diary_count','clock_mode','initialized','bond_level','last_stage','was_sick'])
    bytes[o+companionLayout[key]]=c[key];
  if(c.reserved)bytes.set(c.reserved,o+76);
  c.diary.forEach((m,i)=>{v.setUint32(o+80+i*8,m.day,true);
    ['kind','value','pinned','calendar'].forEach((key,j)=>bytes[o+84+i*8+j]=m[key]);});
}
