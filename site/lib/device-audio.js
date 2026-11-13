const EFFECTS = {
  eat: [880, 520, 140, .16], pounce: [0, 0, 90, .12],
  molt: [440, 1320, 260, .12], tap: [392, 330, 60, .04],
  alert: [660, 440, 180, .18], aim: [520, 760, 90, .08],
  perfect: [880, 1760, 160, .18],
};

/* Integer-only synthesis shared sample for sample with main/audio_music.h. */
export const MUSIC_RATE = 16000, MUSIC_CHORD = 128000, MUSIC_OVERLAP = 32000;
export const MUSIC_NOTE = 32000, MUSIC_LOOP = 4 * MUSIC_CHORD;
export const MUSIC_PAD_GAIN = 400, MUSIC_MELODY_GAIN = 1100, MUSIC_FADE_IN = 24000;
export const MUSIC_TRACKS = Object.freeze(['Silk Garden', 'Moonlit Hollow', 'Rainy Canopy',
  'Crystal Echoes', 'Mosslight Waltz', 'Ember Hearth', 'Starlit Web',
  'Bamboo Rain', 'Clockwork Fern', 'Dewdrop Lullaby']);
const increment = hz => hz ? Math.round(hz * 4294967296 / MUSIC_RATE) : 0;
export const MUSIC_SINE = Int16Array.from({ length: 256 }, (_, i) => Math.round(32767 * Math.sin(2 * Math.PI * i / 256)));
/* D, Bm, G, A: slow consonant triads, each note a slightly detuned pair. */
export const MUSIC_PAD = Uint32Array.from([[293.66, 369.99, 440], [246.94, 293.66, 369.99],
  [196, 246.94, 293.66], [220, 277.18, 329.63]].flat().flatMap(hz => [increment(hz - .12), increment(hz + .12)]));
export const MUSIC_MELODY = Uint32Array.from([739.99, 0, 659.26, 587.33, 493.88, 0, 587.33, 0,
  587.33, 493.88, 0, 392, 440, 0, 554.37, 659.26].map(increment));
const padTable = (chords, detune) => Uint32Array.from(chords.flat()
  .flatMap(hz => [increment(hz - detune), increment(hz + detune)]));
export const MUSIC_MOON_PAD = padTable([[220, 261.63, 329.63], [174.61, 220, 261.63],
  [130.81, 164.81, 196], [196, 246.94, 293.66]], .08);
export const MUSIC_MOON_MELODY = Uint32Array.from([440, 0, 392, 329.63, 349.23, 0, 440, 0,
  329.63, 293.66, 0, 261.63, 293.66, 0, 392, 0].map(increment));
export const MUSIC_RAIN_PAD = padTable([[174.61, 220, 261.63], [164.81, 196, 261.63],
  [146.83, 174.61, 220], [146.83, 174.61, 233.08]], .16);
export const MUSIC_RAIN_MELODY = Uint32Array.from([523.25, 0, 440, 0, 392, 0, 329.63, 392,
  349.23, 0, 440, 0, 466.16, 349.23, 0, 293.66].map(increment));
export const MUSIC_EXTRA_TRACKS = Object.freeze([
  { pad: MUSIC_MOON_PAD, melody: MUSIC_MOON_MELODY, chord: 160000, overlap: 40000,
    note: 40000, attack: 3200, padGain: 360, melodyGain: 850 },
  { pad: MUSIC_RAIN_PAD, melody: MUSIC_RAIN_MELODY, chord: 96000, overlap: 24000,
    note: 24000, attack: 2400, padGain: 380, melodyGain: 950 },
]);
export const MUSIC_PITCH = Uint32Array.from({length:37},(_,i)=>
  increment(440*Math.pow(2,(i+48-69)/12)));
export const MUSIC_SCORE_CHORDS = Uint8Array.from([
  50,54,57,59,62,66,55,59,62,57,61,64,
  55,59,62,52,55,59,48,52,55,50,54,57,
  53,57,60,50,53,57,58,62,65,48,52,55,
  48,52,55,55,59,62,57,60,64,53,57,60,
  50,57,62,55,62,69,57,64,69,50,57,64,
  52,55,59,48,52,55,55,59,62,50,54,57,
  48,52,55,53,57,60,50,53,57,55,59,62,
]);
export const MUSIC_SCORE_MELODIES = Uint8Array.from([
  78,0,81,0,74,0,73,0,71,0,74,0,76,0,73,0,
  71,74,79,74,71,67,0,67,72,76,79,76,74,69,66,0,
  65,0,69,72,69,65,62,0,65,0,70,74,67,64,60,0,
  76,0,0,79,74,0,0,71,72,0,0,76,77,0,76,0,
  74,76,0,81,79,0,76,74,81,0,83,81,78,76,74,0,
  76,71,79,71,72,67,76,67,74,71,79,71,78,69,74,0,
  72,0,67,0,69,0,65,0,65,0,62,0,67,0,71,0,
]);
/* chord, beat, attack, pad/bass/lead/arpeggio gains, rhythm mask, sustained. */
export const MUSIC_SCORES = Object.freeze([
  [128000,16000,640,150,850,1250,300,0x5555,0],
  [96000,16000,480,100,900,1200,600,0x6db6,0],
  [128000,16000,160,100,1500,1050,350,0x2929,0],
  [192000,24000,3200,140,450,1250,250,0x1111,1],
  [96000,12000,128,140,800,1000,400,0xadad,0],
  [128000,8000,192,85,1100,1150,900,0xffff,0],
  [160000,20000,2400,140,500,1100,300,0x5555,1],
]);
function validateTrack(track) {
  if (!Number.isInteger(track) || track < 0 || track >= MUSIC_TRACKS.length)
    throw new RangeError(`Invalid music track: ${track}`);
}
export function musicTrackLoop(track = 0) {
  validateTrack(track);
  if(track>=3)return 4*MUSIC_SCORES[track-3][0];
  return track === 0 ? MUSIC_LOOP : 4 * MUSIC_EXTRA_TRACKS[track - 1].chord;
}
export const musicGain = (sample, volume) => Math.trunc(sample * volume / 100) || 0;
export const PET_NOTES = Uint32Array.from([392, 587.33].map(increment));
export const PET_SAMPLES = 2560, PET_SECOND = 640;

export function sine(phase) {
  const index = phase >>> 24, frac = (phase >>> 8) & 65535;
  const a = MUSIC_SINE[index], b = MUSIC_SINE[(index + 1) & 255];
  return a + (((b - a) * frac) >> 16);
}
export function smoothQ15(x) {
  const square = (x * x) >> 15;
  return (square * (49152 - x)) >> 14;
}
/* Soft attack, cubic decay to exactly zero at length, quieter octave partial. */
export function bell(inc, local, length, attack, gain) {
  if (!inc || local >= length) return 0;
  let env;
  if (local < attack) env = Math.floor(local * 32768 / attack);
  else {
    const r = Math.floor((length - local) * 32768 / (length - attack));
    env = (((r * r) >> 15) * r) >> 15;
  }
  const phase = Math.imul(inc, local) >>> 0, upper = (env * env) >> 15;
  return ((((sine(phase) * gain) >> 15) * env) >> 15) +
    ((((sine((phase << 1) >>> 0) * (gain >> 2)) >> 15) * upper) >> 15);
}
function pad(chord, local) {
  let sum = 0;
  for (let voice = 0; voice < 6; voice++) sum += sine(Math.imul(MUSIC_PAD[chord * 6 + voice], local) >>> 0);
  let env = 32768;
  if (local < MUSIC_OVERLAP) env = smoothQ15(Math.floor(local * 32768 / MUSIC_OVERLAP));
  else if (local >= MUSIC_CHORD) env = 32768 - smoothQ15(Math.floor((local - MUSIC_CHORD) * 32768 / MUSIC_OVERLAP));
  return (((sum * MUSIC_PAD_GAIN) >> 15) * env) >> 15;
}
/* Loop position -> int16 sample. Periodic, so a looping stream is seamless. */
export function musicSample(position, track = 0) {
  validateTrack(track);
  if(track>=3)return scoreSample(track-3,position);
  if (track !== 0) return extraMusicSample(position, track);
  const p = position % MUSIC_LOOP, chord = Math.floor(p / MUSIC_CHORD), local = p - chord * MUSIC_CHORD;
  let value = pad(chord, local);
  if (local < MUSIC_OVERLAP) value += pad((chord + 3) & 3, local + MUSIC_CHORD);
  const note = Math.floor(p / MUSIC_NOTE);
  return value + bell(MUSIC_MELODY[note], p - note * MUSIC_NOTE, MUSIC_NOTE, 1280, MUSIC_MELODY_GAIN);
}
const pitch = note=>note?MUSIC_PITCH[note-48]:0;
function scorePad(score,chord,local){
  const [length,,,gain]=MUSIC_SCORES[score];let sum=0;
  for(let voice=0;voice<3;voice++){
    const inc=pitch(MUSIC_SCORE_CHORDS[score*12+chord*3+voice]);
    sum+=sine(Math.imul(inc-2684,local)>>>0)+sine(Math.imul(inc+2684,local)>>>0);
  }
  const overlap=length/4;let env=32768;
  if(local<overlap)env=smoothQ15(Math.floor(local*32768/overlap));
  else if(local>=length)env=32768-smoothQ15(Math.floor((local-length)*32768/overlap));
  return (((sum*gain)>>15)*env)>>15;
}
function scoreSample(score,position){
  const [length,beatLength,attack,,bass,leadGain,arpGain,rhythm,sustained]=MUSIC_SCORES[score];
  const p=position%(4*length),chord=Math.floor(p/length),local=p%length;
  let value=scorePad(score,chord,local);
  if(local<length/4)value+=scorePad(score,(chord+3)&3,local+length);
  const beat=Math.floor(p/beatLength),at=p%beatLength;
  const root=pitch(MUSIC_SCORE_CHORDS[score*12+chord*3]);
  value+=bell(root>>>1,local%(2*beatLength),2*beatLength,640,bass);
  const lead=pitch(MUSIC_SCORE_MELODIES[score*16+beat%16]);
  if(sustained&&lead){
    let env=smoothQ15(Math.floor(at*32768/beatLength));
    env=(env*(32768-env))>>13;
    value+=(((sine(Math.imul(lead,at)>>>0)*leadGain)>>15)*env)>>15;
  }else value+=bell(lead,at,beatLength,attack,leadGain);
  if(rhythm&(1<<(beat%16))){
    const arp=pitch(MUSIC_SCORE_CHORDS[score*12+chord*3+beat%3]);
    value+=bell(arp*2,at,beatLength,attack,arpGain);
  }
  return value;
}
function extraPad(track, id, chord, local) {
  let sum = 0;
  for (let voice = 0; voice < 6; voice++) {
    const inc = track.pad[chord * 6 + voice];
    sum += sine(Math.imul(inc, local) >>> 0);
    sum += sine(id === 1 ? Math.imul(inc >>> 1, local) >>> 0 : (Math.imul(inc, local) << 1) >>> 0) >> 3;
  }
  let env = 32768;
  if (local < track.overlap) env = smoothQ15(Math.floor(local * 32768 / track.overlap));
  else if (local >= track.chord) env = 32768 - smoothQ15(Math.floor((local - track.chord) * 32768 / track.overlap));
  return (((sum * track.padGain) >> 15) * env) >> 15;
}
function extraMusicSample(position, id) {
  const track = MUSIC_EXTRA_TRACKS[id - 1], p = position % (4 * track.chord);
  const chord = Math.floor(p / track.chord), local = p - chord * track.chord;
  let value = extraPad(track, id, chord, local);
  if (local < track.overlap) value += extraPad(track, id, (chord + 3) & 3, local + track.chord);
  value=Math.trunc(value/4);
  if(id===1){
    const beat=local%20000;
    return value+bell(track.pad[chord*6]>>>1,beat,20000,240,1850)+
      bell(track.melody[Math.floor(p/20000)%16],beat,16000,160,1500);
  }
  const beat=local%6000,voice=Math.floor(local/6000)%3;
  return value+bell(track.pad[chord*6+voice*2]*2,beat,6000,96,2100)+
    bell(track.melody[Math.floor(p/24000)%16],local%24000,18000,320,700);
}
export function petSample(i) {
  return bell(PET_NOTES[0], i, PET_SAMPLES, 160, 1500) +
    (i >= PET_SECOND ? bell(PET_NOTES[1], i - PET_SECOND, PET_SAMPLES - PET_SECOND, 160, 1300) : 0);
}
let musicLoop = null, musicLoopTrack = -1;
export function musicSamples(track = 0) {
  validateTrack(track);
  if (!musicLoop || track !== musicLoopTrack) {
    musicLoop = new Float32Array(musicTrackLoop(track));
    for (let i = 0; i < musicLoop.length; i++) musicLoop[i] = musicSample(i, track) / 32768;
    musicLoopTrack = track;
  }
  return musicLoop;
}
const isAmbient = effect => effect === 'rustle' || effect === 'drop';
const smooth = value => value * value * (3 - 2 * value);

function ambientSamples(leafy, seed) {
  const length = 16000 * (leafy ? 10 : 8), samples = new Float32Array(length);
  let air = 0, floor = 0;
  for (let i = 0; i < length; i++) {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    const noise = (seed >>> 16) - 32768;
    air += Math.trunc((noise - air) / (leafy ? 12 : 32));
    floor += Math.trunc((noise - floor) / 512);
    const envelope = Math.min(1, i / 32000, (length - 1 - i) / 48000);
    const cycle = (i + (leafy ? 17000 : 0)) % 112000;
    const swell = cycle < 56000 ? cycle / 56000 : (112000 - cycle) / 56000;
    const gain = (leafy ? .095 : .14) * (.65 + .35 * smooth(swell));
    samples[i] = Math.trunc((air - floor) * gain * smooth(envelope)) / 32768;
  }
  return samples;
}

export function soundSamples(effect, seed = 0x12345678) {
  if (isAmbient(effect)) return ambientSamples(effect === 'drop', seed);
  if (effect === 'pet' || effect === 'chirp') return Float32Array.from({ length: PET_SAMPLES }, (_, i) => petSample(i) / 32768);
  if (!EFFECTS[effect]) throw new Error(`Unknown sound effect: ${effect}`);
  const [f0, f1, ms, gain] = EFFECTS[effect], length = 16000 * ms / 1000;
  const samples = new Float32Array(length); let phase = 0;
  for (let i = 0; i < length; i++) {
    const u = i / length;
    const attack = f0 ? .08 : .02, release = f0 ? .45 : .7;
    const envelope = u < attack ? u / attack : u > 1 - release ? (1 - u) / release : 1;
    let signal;
    if (f0) {
      phase += 2 * Math.PI * (f0 + (f1 - f0) * u) / 16000;
      if (phase > 2 * Math.PI) phase -= 2 * Math.PI;
      signal = Math.sin(phase);
    } else {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
      signal = ((seed >>> 16) & 65535) / 32768 - 1;
    }
    samples[i] = Math.trunc(clamp(signal * envelope * gain, -1, 1) * 32000) / 32768;
  }
  if (effect === 'pounce') for (let i = 1; i < length; i++) samples[i] = Math.trunc((samples[i] + 3 * samples[i - 1]) * 32768 / 4) / 32768;
  return samples;
}
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
export class DeviceAudio {
  constructor(report) {
    this.report = report; this.context = null; this.master = null;
    this.until = 0; this.nextAmbient = 12000; this.previous = null;
    this.ambient = null; this.sources = new Set(); this.enabled = true; this.volume = .7;
    this.music = null; this.musicBuffer = null; this.musicEnabled = false; this.quietUntil = 0;
    this.musicVolume = 1; this.musicTrack = 0; this.musicBufferTrack = -1; this.musicGain = null;
  }
  async unlock(enabled) {
    if (!enabled) return;
    try {
      const Context = globalThis.AudioContext ?? globalThis.webkitAudioContext;
      if (!Context) throw new Error('Web Audio is unavailable');
      this.context ??= new Context();
      if (!this.master) {
        this.master = this.context.createGain();
        this.master.gain.value = this.enabled ? this.volume : 0;
        this.master.connect(this.context.destination);
      }
      if (this.context.state === 'suspended') await this.context.resume();
      this.syncMusic();
    } catch (error) { this.report(`Sound unavailable: ${error.message}`, true); }
  }
  fade(source) {
    if (source.stopping) return;
    source.stopping = true;
    const now = this.context.currentTime;
    source.level.gain.cancelScheduledValues(now);
    source.level.gain.setValueAtTime(source.level.gain.value, now);
    source.level.gain.linearRampToValueAtTime(0, now + .02);
    source.stop(now + .02);
  }
  settings(enabled, volume) {
    const level = clamp(volume, 0, 100) / 100;
    const changed = enabled !== this.enabled || level !== this.volume;
    this.enabled = enabled; this.volume = level;
    if (!changed || !this.context || !this.master) return;
    this.master.gain.setTargetAtTime(enabled ? this.volume : 0, this.context.currentTime, .01);
    if (!enabled) { for (const source of this.sources) this.fade(source); this.music = null; }
  }
  source(samples, output = this.master) {
    const buffer = samples instanceof Float32Array ? this.context.createBuffer(1, samples.length, 16000) : samples;
    if (buffer !== samples) buffer.copyToChannel(samples, 0);
    const source = this.context.createBufferSource();
    source.buffer = buffer; source.level = this.context.createGain();
    source.connect(source.level); source.level.connect(output);
    source.onended = () => {
      this.sources.delete(source); source.disconnect(); source.level.disconnect();
      if (this.ambient === source) this.ambient = null;
      if (this.music === source) this.music = null;
    };
    this.sources.add(source);
    return source;
  }
  /* One seamless looping buffer; independent of effects, under master mute/volume. */
  syncMusic() {
    const want = this.enabled && this.musicEnabled && this.musicVolume > 0 &&
      this.context?.state === 'running' && !!this.master;
    if (!want) {
      if (this.music) { this.fade(this.music); this.music = null; }
      return;
    }
    if (this.music) return;
    if (!this.musicGain) {
      this.musicGain = this.context.createGain();
      this.musicGain.gain.value = this.musicVolume;
      this.musicGain.connect(this.master);
    }
    if (!this.musicBuffer || this.musicBufferTrack !== this.musicTrack) {
      const samples = musicSamples(this.musicTrack);
      this.musicBuffer = this.context.createBuffer(1, samples.length, 16000);
      this.musicBuffer.copyToChannel(samples, 0);
      this.musicBufferTrack = this.musicTrack;
    }
    const music = this.source(this.musicBuffer, this.musicGain), start = Math.max(this.context.currentTime, this.quietUntil);
    music.loop = true;
    music.level.gain.value = 0;
    music.level.gain.setValueAtTime(0, start);
    music.level.gain.linearRampToValueAtTime(1, start + MUSIC_FADE_IN / MUSIC_RATE);
    music.start();
    this.music = music;
  }
  /* Like the native worker: 20 ms fade out, effect alone, then a slow fade in. */
  duck(seconds) {
    const now = this.context.currentTime;
    this.quietUntil = Math.max(this.quietUntil, now + seconds);
    if (!this.music) return;
    const gain = this.music.level.gain;
    gain.cancelScheduledValues(now);
    gain.setValueAtTime(gain.value, now);
    gain.linearRampToValueAtTime(0, now + .02);
    gain.setValueAtTime(0, this.quietUntil);
    gain.linearRampToValueAtTime(1, this.quietUntil + MUSIC_FADE_IN / MUSIC_RATE);
  }
  play(effect) {
    if (!this.enabled || !this.context || this.context.state !== 'running') return;
    const ambient = isAmbient(effect);
    if (ambient && (this.ambient || performance.now() < this.until)) return;
    if (!ambient && this.ambient) { this.fade(this.ambient); this.ambient = null; }
    const samples = soundSamples(effect, ambient ? Math.floor(Math.random() * 0x100000000) : undefined);
    const source = this.source(samples);
    source.start();
    this.duck(.02 + samples.length / 16000);
    if (ambient) this.ambient = source;
    else this.until = performance.now() + samples.length / 16;
  }
  musicSettings(volume, track) {
    if (!Number.isInteger(volume) || volume < 0 || volume > 100)
      this.report(`Invalid music volume: ${volume}`, true);
    else if (this.musicVolume !== volume / 100) {
      this.musicVolume = volume / 100;
      if (this.musicGain) this.musicGain.gain.setTargetAtTime(this.musicVolume, this.context.currentTime, .01);
    }
    if (!Number.isInteger(track) || track < 0 || track >= MUSIC_TRACKS.length)
      this.report(`Invalid music track: ${track}`, true);
    else if (this.musicTrack !== track) {
      this.musicTrack = track;
      if (this.music) { this.fade(this.music); this.music = null; }
    }
  }
  tick(state, now, enabled, volume = 70, active = true, musicEnabled = false, musicVolume = 100, musicTrack = 0) {
    this.settings(enabled, volume);
    this.musicSettings(musicVolume, musicTrack);
    this.musicEnabled = !!musicEnabled;
    this.syncMusic();
    if (enabled && active && state !== this.previous && ['eat', 'pounce', 'molt', 'aim'].includes(state)) this.play(state);
    this.previous = state;
  }
}
