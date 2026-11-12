import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { soundSamples, DeviceAudio, musicSamples, musicSample, petSample, MUSIC_LOOP, MUSIC_SINE,
  MUSIC_PAD, MUSIC_MELODY, PET_NOTES, PET_SAMPLES } from '../lib/device-audio.js';
import { MUSIC_TRACKS, MUSIC_EXTRA_TRACKS, MUSIC_MOON_PAD, MUSIC_MOON_MELODY,
  MUSIC_RAIN_PAD, MUSIC_RAIN_MELODY, musicTrackLoop, musicGain } from '../lib/device-audio.js';

function rms(samples, start = 0, end = samples.length) {
  let power = 0;
  for (let i = start; i < end; i++) power += samples[i] ** 2;
  return Math.sqrt(power / (end - start));
}

test('ambience is sustained, quiet, slowly faded air rather than a short pitch sweep', () => {
  for (const [effect, seconds] of [['rustle', 8], ['drop', 10]]) {
    const samples = soundSamples(effect);
    assert.equal(samples.length, seconds * 16000);
    assert.equal(Math.abs(samples[0]), 0);
    assert.equal(Math.abs(samples.at(-1)), 0);
    const body = rms(samples, 32000, samples.length - 48000);
    assert.ok(body > .002 && body < .012, `quiet body RMS: ${body}`);
    assert.ok(rms(samples, 0, 4000) < body / 20, 'slow attack');
    assert.ok(rms(samples, samples.length - 4000) < body / 20, 'slow release');
    let peak = 0, change = 0;
    for (let i = 1; i < samples.length; i++) {
      peak = Math.max(peak, Math.abs(samples[i]));
      change = Math.max(change, Math.abs(samples[i] - samples[i - 1]));
    }
    assert.ok(peak < .06, `ample headroom: ${peak}`);
    assert.ok(change < .012, `no impulsive jumps: ${change}`);
    assert.deepEqual(samples, soundSamples(effect), 'reproducible seeded PCM');
    assert.notDeepEqual(samples, soundSamples(effect, 9), 'variation without changing character');
  }
});

test('browser PCM matches the native on-device self-test sample for sample', () => {
  for (const [effect, expected] of [['rustle', 0x02c0ba3f], ['drop', 0x096a6969]]) {
    let hash = 2166136261;
    for (const value of soundSamples(effect)) {
      const bits = Math.round(value * 32768) & 65535;
      hash = Math.imul(hash ^ (bits & 255), 16777619) >>> 0;
      hash = Math.imul(hash ^ (bits >>> 8), 16777619) >>> 0;
    }
    assert.equal(hash, expected, effect);
  }
});

test('pounce is a softer filtered air transient, not the former rising laser tone', () => {
  const samples = soundSamples('pounce');
  assert.equal(samples.length, 1440);
  assert.ok(rms(samples) < .03);
  assert.ok(rms(soundSamples('aim')) < .06);
  assert.ok(rms(soundSamples('tap')) < .05);
});

class Gain {
  constructor() { this.value = 1; this.events = []; }
  cancelScheduledValues(time) { this.events.push(['cancel', time]); }
  setValueAtTime(value, time) { this.events.push(['set', value, time]); }
  linearRampToValueAtTime(value, time) { this.events.push(['ramp', value, time]); }
  setTargetAtTime(value, time, duration) { this.events.push(['target', value, time, duration]); }
}
function audio() {
  const sound = new DeviceAudio(() => assert.fail('Unexpected Web Audio error'));
  sound.context = {
    state: 'running', currentTime: 3, buffers: 0,
    createBuffer() { this.buffers++; return { copyToChannel() {} }; },
    createGain: () => ({ gain: new Gain(), connect(target) { this.output = target; }, disconnect() {} }),
    createBufferSource: () => ({ connect(target) { this.output = target; }, disconnect() {}, start() { this.started = true; },
      stop(time) { this.stoppedAt = time; } }),
  };
  sound.master = sound.context.createGain();
  return sound;
}

test('foreground immediately fades ambience and does not wait eight seconds', () => {
  const sound = audio();
  sound.play('rustle');
  const ambience = sound.ambient;
  assert.ok(ambience);
  sound.play('eat');
  assert.equal(sound.ambient, null);
  assert.equal(ambience.stoppedAt, 3.02);
  assert.ok(ambience.level.gain.events.some(event => event[0] === 'ramp' && event[1] === 0));
  assert.equal(sound.sources.size, 2);
  ambience.onended();
  assert.equal(sound.sources.size, 1);
});

test('mute and volume apply during menus and to already playing sources', () => {
  const sound = audio();
  sound.play('drop');
  const ambience = sound.ambient;
  sound.tick('idle', 1000, true, 25, false);
  assert.deepEqual(sound.master.gain.events.at(-1), ['target', .25, 3, .01]);
  sound.tick('idle', 1050, false, 25, false);
  assert.equal(ambience.stoppedAt, 3.02);
  assert.deepEqual(sound.master.gain.events.at(-1), ['target', 0, 3, .01]);
  const count = sound.sources.size;
  sound.play('pounce');
  assert.equal(sound.sources.size, count, 'mute prevents new playback');
});

test('automatic ambience stays silent after the rejected noise textures', () => {
  const sound = audio(), effects = [];
  sound.play = effect => effects.push(effect);
  for (let i = 1; i <= 20; i++) sound.tick('idle', i * 21000, true);
  assert.equal(effects.length, 0);
  sound.tick('pounce', 500000, true);
  assert.equal(effects.at(-1), 'pounce');
  assert.equal(effects.length, 1, 'foreground effects still work without automatic ambience');
});

test('unchanged per-frame settings do not accumulate gain automation events', () => {
  const sound = audio();
  sound.settings(true, 25);
  const count = sound.master.gain.events.length;
  for (let frame = 0; frame < 3600; frame++) sound.tick('idle', frame * 16, true, 25, false);
  assert.equal(sound.master.gain.events.length, count);
  sound.settings(false, 25);
  assert.equal(sound.master.gain.events.length, count + 1);
});

const hz = inc => inc * 16000 / 4294967296;
function fnv(count, sample) {
  let hash = 2166136261;
  for (let i = 0; i < count; i++) {
    const bits = sample(i) & 65535;
    hash = Math.imul(hash ^ (bits & 255), 16777619) >>> 0;
    hash = Math.imul(hash ^ (bits >>> 8), 16777619) >>> 0;
  }
  return hash;
}

test('background music is a quiet, smooth, seamlessly looping consonant stream', () => {
  const samples = musicSamples();
  assert.equal(samples.length, MUSIC_LOOP);
  assert.equal(MUSIC_LOOP / 16000, 32, 'long-lived 32 second loop');
  let peak = 0, step = 0;
  for (let i = 0; i < samples.length; i++) {
    peak = Math.max(peak, Math.abs(samples[i]));
    step = Math.max(step, Math.abs(samples[i] - samples[(i + samples.length - 1) % samples.length]));
  }
  const body = rms(samples), seam = Math.abs(samples[0] - samples.at(-1));
  assert.ok(peak < .12, `headroom below effects: ${peak}`);
  assert.ok(body > .012 && body < .035, `gentle RMS: ${body}`);
  assert.ok(step < .025, `no clicks or jumps including the loop seam: ${step}`);
  assert.ok(seam < .025, `seamless loop: ${seam}`);
  assert.equal(musicSample(MUSIC_LOOP + 1234), musicSample(1234), 'periodic stream');
  for (let second = 0; second < 32; second++) {
    const level = rms(samples, second * 16000, (second + 1) * 16000);
    assert.ok(level > .006 && level < .04, `continuous, never silent or surging at ${second}s: ${level}`);
  }
  // Every pad and melody pitch is in D major; each chord is a major or minor triad.
  const major = new Set([0, 2, 4, 5, 7, 9, 11]);
  for (const inc of [...MUSIC_PAD, ...MUSIC_MELODY].filter(Boolean)) {
    const semitone = Math.round(12 * Math.log2(hz(inc) / 293.66));
    assert.ok(major.has(((semitone % 12) + 12) % 12), `${hz(inc).toFixed(2)} Hz is in key`);
  }
  for (let chord = 0; chord < 4; chord++) {
    const notes = [0, 2, 4].map(v => Math.round(12 * Math.log2(hz(MUSIC_PAD[chord * 6 + v]) / hz(MUSIC_PAD[chord * 6]))));
    assert.ok([[0, 4, 7], [0, 3, 7]].some(triad => triad.join() === notes.join()), `triad ${notes}`);
    for (let v = 0; v < 6; v += 2) {
      const beat = hz(MUSIC_PAD[chord * 6 + v + 1]) - hz(MUSIC_PAD[chord * 6 + v]);
      assert.ok(beat > .1 && beat < .5, `slow chorus detune, not a sweep: ${beat}`);
    }
  }
  assert.ok(MUSIC_MELODY.filter(Boolean).length <= 12, 'sparse melody with rests');
});

test('pet sound is a short soft rising two-note bell, not the former harsh chirp', () => {
  const samples = soundSamples('pet');
  assert.deepEqual(samples, soundSamples('chirp'), 'native SFX id 8 name kept as an alias');
  assert.equal(samples.length, PET_SAMPLES);
  assert.equal(samples[0], 0);
  assert.equal(samples.at(-1), 0);
  let peak = 0, step = 0;
  for (let i = 1; i < samples.length; i++) {
    peak = Math.max(peak, Math.abs(samples[i]));
    step = Math.max(step, Math.abs(samples[i] - samples[i - 1]));
  }
  assert.ok(peak < .07, `soft: ${peak}`);
  assert.ok(step < .02, `no click: ${step}`);
  assert.ok(rms(samples) > .008 && rms(samples) < .03, `audible but gentle: ${rms(samples)}`);
  const [low, high] = PET_NOTES.map(hz);
  assert.ok(Math.abs(12 * Math.log2(high / low) - 7) < .05, `rising equal-tempered fifth: ${low} -> ${high}`);
  assert.ok(low > 300 && high < 700, 'mid register, well below the old 1800 Hz chirp');
  const crossings = (start, end) => { let count = 0; for (let i = start + 1; i < end; i++) if ((samples[i - 1] < 0) !== (samples[i] < 0)) count++; return count; };
  assert.ok(crossings(1280, 2560) >= crossings(0, 640), 'pitch rises; no downward sweep');
});

test('native header tables and self-test hashes match the browser synthesis', () => {
  const header = readFileSync(new URL('../../main/audio_music.h', import.meta.url), 'utf8');
  const table = name => header.match(new RegExp(`${name}\\[\\d+\\] = \\{([^}]*)\\}`))[1]
    .split(',').map(item => item.trim()).filter(Boolean).map(item => Number(item.replace(/u$/, '')));
  assert.deepEqual(table('MUSIC_SINE'), [...MUSIC_SINE]);
  assert.deepEqual(table('MUSIC_PAD'), [...MUSIC_PAD]);
  assert.deepEqual(table('MUSIC_MELODY'), [...MUSIC_MELODY]);
  assert.deepEqual(table('MUSIC_MOON_PAD'), [...MUSIC_MOON_PAD]);
  assert.deepEqual(table('MUSIC_MOON_MELODY'), [...MUSIC_MOON_MELODY]);
  assert.deepEqual(table('MUSIC_RAIN_PAD'), [...MUSIC_RAIN_PAD]);
  assert.deepEqual(table('MUSIC_RAIN_MELODY'), [...MUSIC_RAIN_MELODY]);
  assert.deepEqual(table('PET_NOTES'), [...PET_NOTES]);
  const define = name => Number(header.match(new RegExp(`#define ${name} (0x[0-9a-f]+)u`))[1]);
  assert.equal(fnv(MUSIC_LOOP, musicSample), define('MUSIC_TEST_HASH'));
  assert.equal(fnv(musicTrackLoop(1), i => musicSample(i, 1)), define('MUSIC_MOON_TEST_HASH'));
  assert.equal(fnv(musicTrackLoop(2), i => musicSample(i, 2)), define('MUSIC_RAIN_TEST_HASH'));
  assert.equal(fnv(PET_SAMPLES, petSample), define('PET_TEST_HASH'));
  const descriptors = [...header.matchAll(/\{MUSIC_(?:MOON|RAIN)_PAD, MUSIC_(?:MOON|RAIN)_MELODY, ([^}]+)\}/g)]
    .map(match => match[1].split(',').map(value => Number(value.trim().replace(/u$/, ''))));
  assert.deepEqual(descriptors, MUSIC_EXTRA_TRACKS.map(track => [
    track.chord, track.overlap, track.note, track.attack, track.padGain, track.melodyGain,
  ]), 'native and browser tempo, envelope and voice levels match');
});

test('music starts after enable with a fade in, loops, and keeps playing in menus', () => {
  const sound = audio();
  sound.tick('idle', 0, true, 70, true);
  assert.equal(sound.music, null, 'music stays off without the music setting');
  sound.tick('idle', 16, true, 70, false, true);
  const music = sound.music;
  assert.ok(music?.started);
  assert.equal(music.loop, true);
  assert.equal(music.level.gain.value, 0);
  assert.deepEqual(music.level.gain.events.at(-1), ['ramp', 1, 4.5]);
  for (let frame = 2; frame < 600; frame++) sound.tick('idle', frame * 16, true, 70, frame % 2 === 0, true);
  assert.equal(sound.music, music, 'one long-lived source across menu/gameplay frames');
  assert.equal(music.level.gain.events.length, 2, 'steady ticks schedule nothing');
  assert.equal(sound.context.buffers, 1);
});

test('music toggle, master mute and hidden page fade promptly; volume is shared', () => {
  const sound = audio();
  sound.tick('idle', 0, true, 70, true, true);
  let music = sound.music;
  sound.tick('idle', 16, true, 30, true, true);
  assert.deepEqual(sound.master.gain.events.at(-1), ['target', .3, 3, .01]);
  assert.equal(sound.music, music, 'volume does not restart music');
  sound.tick('idle', 32, true, 30, true, false);
  assert.equal(music.stoppedAt, 3.02, 'music toggle off fades within 20 ms');
  assert.equal(sound.music, null);
  sound.tick('idle', 48, true, 30, true, true);
  music = sound.music;
  assert.ok(music);
  sound.tick('idle', 64, false, 30, true, true);
  assert.equal(music.stoppedAt, 3.02, 'master mute / hidden page fades music');
  assert.equal(sound.music, null);
  sound.tick('idle', 80, false, 30, true, true);
  assert.equal(sound.music, null, 'no restart while muted');
  sound.tick('idle', 96, true, 30, true, true);
  assert.ok(sound.music && sound.music !== music, 'unmute restarts the stream');
  assert.equal(sound.context.buffers, 1, 'loop buffer is generated once and reused');
});

test('effects fade the music out, then music fades back in after the effect', () => {
  const sound = audio();
  sound.tick('idle', 0, true, 70, true, true);
  const music = sound.music;
  sound.play('pet');
  const events = music.level.gain.events.slice(-5);
  assert.deepEqual(events[0], ['cancel', 3]);
  assert.deepEqual(events[1].slice(0, 1), ['set']);
  assert.deepEqual(events[2], ['ramp', 0, 3.02]);
  assert.deepEqual(events[3], ['set', 0, 3 + .02 + PET_SAMPLES / 16000]);
  assert.deepEqual(events[4], ['ramp', 1, 3 + .02 + PET_SAMPLES / 16000 + 1.5]);
  assert.equal(sound.music, music, 'the stream is ducked, not restarted');
  assert.ok(sound.sources.size === 2);
});

test('three original tracks have distinct PCM, tempo and voices with continuous quiet loops', () => {
  assert.deepEqual(MUSIC_TRACKS, ['Silk Garden', 'Moonlit Hollow', 'Rainy Canopy']);
  const hashes = new Set(), lengths = new Set();
  for (let track = 0; track < MUSIC_TRACKS.length; track++) {
    const samples = musicSamples(track), length = musicTrackLoop(track);
    lengths.add(length);
    assert.equal(samples.length, length);
    assert.equal(musicSamples(track), samples, 'reuse the current deterministic loop');
    let peak = 0, step = 0;
    for (let i = 0; i < length; i++) {
      assert.equal(samples[i] * 32768, musicSample(i, track), `exact streamed PCM track=${track} sample=${i}`);
      peak = Math.max(peak, Math.abs(samples[i]));
      step = Math.max(step, Math.abs(samples[i] - samples[(i + length - 1) % length]));
    }
    assert.ok(peak < .12 && rms(samples) > .012 && rms(samples) < .035, `gentle track ${track}`);
    assert.ok(step < .025, `continuous across every chunk/chord/note/loop boundary: ${step}`);
    assert.equal(musicSample(length + 1234, track), musicSample(1234, track), 'periodic');
    for (let second = 0; second < length / 16000; second++)
      assert.ok(rms(samples, second * 16000, (second + 1) * 16000) > .004, 'no dead seconds');
    hashes.add(fnv(length, i => musicSample(i, track)));
  }
  assert.equal(hashes.size, 3, 'all tracks are genuinely different recordings');
  assert.equal(lengths.size, 3, 'different harmonic tempos');
});

test('music-only gain has exact silence/full scale and symmetric integer scaling', () => {
  for (let track = 0; track < MUSIC_TRACKS.length; track++) {
    for (let i = 0; i < musicTrackLoop(track); i += 17) {
      const sample = musicSample(i, track);
      assert.equal(musicGain(sample, 100), sample);
      assert.equal(musicGain(sample, 0), 0);
      assert.equal(musicGain(sample, 50), Math.trunc(sample / 2) || 0);
      assert.equal(musicGain(sample, 25), Math.trunc(sample / 4) || 0);
    }
  }
});

test('scores differ measurably in rhythm and register, not just transposed pad notes',()=>{
  const features=[];
  for(let track=0;track<3;track++){
    const s=musicSamples(track), envelopes=[];
    let power=0,delta=0;
    for(let i=1;i<s.length;i++){power+=s[i]**2;delta+=(s[i]-s[i-1])**2;}
    for(let i=0;i<s.length;i+=1600)envelopes.push(rms(s,i,Math.min(i+1600,s.length)));
    const mean=envelopes.reduce((a,b)=>a+b)/envelopes.length;
    const cv=Math.sqrt(envelopes.reduce((a,b)=>a+(b-mean)**2,0)/envelopes.length)/mean;
    features.push({cv,register:Math.sqrt(delta/power)});
  }
  assert.ok(features[1].cv>features[0].cv*1.8,'Moon has distinct plucked beats, not a held garden pad');
  assert.ok(features[2].cv>features[0].cv*1.3,'Rain has distinct fast mallet attacks');
  assert.ok(features[2].register>features[1].register*1.8,'Rain mallets occupy a much brighter register than Moon bass');
  assert.ok(features[0].register>features[1].register*1.5,'Garden has a different register from Moon');
});

test('music volume controls only the music bus; master volume and mute still win', () => {
  const sound = audio();
  sound.tick('idle', 0, true, 80, true, true, 25, 1);
  const music = sound.music;
  assert.equal(music.level.output, sound.musicGain);
  assert.equal(sound.musicGain.output, sound.master);
  assert.equal(sound.musicGain.gain.value, .25);
  sound.play('pet');
  const effect = [...sound.sources].find(source => source !== music);
  assert.equal(effect.level.output, sound.master, 'foreground bypasses music volume');
  const masterEvents = sound.master.gain.events.length;
  sound.tick('idle', 16, true, 80, true, true, 50, 1);
  assert.equal(sound.music, music, 'music gain does not restart playback');
  assert.deepEqual(sound.musicGain.gain.events.at(-1), ['target', .5, 3, .01]);
  assert.equal(sound.master.gain.events.length, masterEvents, 'master unaffected');
  assert.equal(effect.stoppedAt, undefined, 'foreground unaffected');
  sound.tick('idle', 32, true, 80, true, true, 0, 1);
  assert.equal(sound.music, null);
  assert.equal(music.stoppedAt, 3.02, 'music-only silence fades within 20 ms');
  assert.equal(effect.stoppedAt, undefined, 'music mute does not mute effects');
  sound.tick('idle', 48, true, 80, true, true, 50, 1);
  assert.ok(sound.music);
  const resumed = sound.music;
  sound.tick('idle', 64, true, 0, true, true, 100, 1);
  assert.deepEqual(sound.master.gain.events.at(-1), ['target', 0, 3, .01], 'zero master always wins');
  sound.tick('idle', 80, false, 80, true, true, 100, 1);
  assert.equal(resumed.stoppedAt, 3.02);
  assert.equal(effect.stoppedAt, 3.02, 'master mute also stops foreground');
});

test('track changes fade old music and restart the selected loop without touching effects', () => {
  const sound = audio();
  sound.tick('idle', 0, true, 70, true, true);
  const old = sound.music, oldBuffer = old.buffer;
  sound.play('pet');
  const effect = [...sound.sources].find(source => source !== old);
  sound.tick('idle', 16, true, 70, false, true, 35, 2);
  const selected = sound.music;
  assert.equal(old.stoppedAt, 3.02);
  assert.notEqual(selected, old);
  assert.notEqual(selected.buffer, oldBuffer);
  assert.equal(sound.musicBufferTrack, 2);
  assert.equal(selected.loop, true);
  assert.equal(selected.level.gain.value, 0, 'new track fades in from silence');
  assert.equal(effect.stoppedAt, undefined);
  old.onended();
  assert.equal(sound.music, selected, 'old completion cannot clear the new track');
  const events = sound.musicGain.gain.events.length, buffers = sound.context.buffers;
  for (let i = 0; i < 120; i++) sound.tick('idle', i * 16, true, 70, false, true, 35, 2);
  assert.equal(sound.music, selected);
  assert.equal(sound.context.buffers, buffers, 'no per-frame PCM allocation');
  assert.equal(sound.musicGain.gain.events.length, events, 'no redundant gain automation');
});

test('invalid music inputs report rejection and retain the prior valid settings', () => {
  const sound = audio(), errors = [];
  sound.report = (...args) => errors.push(args);
  sound.tick('idle', 0, true, 70, true, true, 40, 1);
  const source = sound.music;
  for (const [volume, track] of [[101, 3], [-1, -1], [NaN, NaN], [1.5, 1.5]]) {
    sound.tick('idle', 16, true, 70, true, true, volume, track);
    assert.equal(sound.musicVolume, .4);
    assert.equal(sound.musicTrack, 1);
    assert.equal(sound.music, source);
  }
  assert.equal(errors.length, 8);
  assert.ok(errors.every(([message, error]) => /^Invalid music (volume|track):/.test(message) && error));
  assert.throws(() => musicSamples(3), RangeError);
  assert.throws(() => musicSample(0, -1), RangeError);
  assert.throws(() => musicTrackLoop(NaN), RangeError);
});

test('native music requests publish atomics only and the worker owns fade/restart and PCM reuse', () => {
  const source = readFileSync(new URL('../../main/audio.c', import.meta.url), 'utf8');
  const header = readFileSync(new URL('../../main/audio.h', import.meta.url), 'utf8');
  assert.match(header, /#define AUDIO_MUSIC_TRACK_COUNT 3/);
  assert.match(source, /_Static_assert\(AUDIO_MUSIC_TRACK_COUNT == MUSIC_TRACK_COUNT/);
  for (const [name, maximum] of [['track', 'AUDIO_MUSIC_TRACK_COUNT'], ['volume', '100']]) {
    const setter = source.match(new RegExp(`void audio_set_music_${name}\\(uint8_t ${name}\\)\\s*\\{([\\s\\S]*?)\\n\\}`))[1];
    assert.ok(setter.includes(maximum));
    assert.match(setter, /ESP_LOGW/);
    assert.match(setter, /return;/);
    assert.ok(setter.includes(`atomic_store(&s_music_${name}, ${name})`));
    assert.doesNotMatch(setter, /lv_|esp_codec|write_pcm|vTaskDelay|heap_caps|music_stop/);
  }
  assert.match(source, /music_stop\("track change"\);\s*s_music_active_track = track;\s*s_music_pos = 0;\s*s_music_fade = 0;/);
  assert.match(source, /s_buf\[i\] = music_gain\(value,/);
  assert.equal((source.match(/heap_caps_malloc\(/g) ?? []).length, 1, 'existing 12,800-byte PSRAM PCM reused');
  assert.match(source, /MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT/);
  const effects = source.slice(source.indexOf('static void play_sfx(sfx_t sfx)\n{'));
  assert.doesNotMatch(effects, /s_music_volume|music_gain/, 'foreground is not attenuated by music volume');
});
