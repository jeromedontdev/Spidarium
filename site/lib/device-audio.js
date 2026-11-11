const EFFECTS = {
  eat: [880, 520, 140, .16], pounce: [0, 0, 90, .12],
  molt: [440, 1320, 260, .12], tap: [0, 0, 60, .08],
  alert: [660, 440, 180, .18], aim: [520, 760, 90, .08],
  perfect: [880, 1760, 160, .18], chirp: [1800, 1800, 160, .025],
};
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
    if (!enabled) for (const source of this.sources) this.fade(source);
  }
  play(effect) {
    if (!this.enabled || !this.context || this.context.state !== 'running') return;
    const ambient = isAmbient(effect);
    if (ambient && (this.ambient || performance.now() < this.until)) return;
    if (!ambient && this.ambient) { this.fade(this.ambient); this.ambient = null; }
    const samples = soundSamples(effect, ambient ? Math.floor(Math.random() * 0x100000000) : undefined);
    const buffer = this.context.createBuffer(1, samples.length, 16000);
    buffer.copyToChannel(samples, 0); const source = this.context.createBufferSource();
    source.buffer = buffer; source.level = this.context.createGain();
    source.connect(source.level); source.level.connect(this.master);
    source.onended = () => {
      this.sources.delete(source); source.disconnect(); source.level.disconnect();
      if (this.ambient === source) this.ambient = null;
    };
    this.sources.add(source); source.start();
    if (ambient) this.ambient = source;
    else this.until = performance.now() + samples.length / 16;
  }
  tick(state, now, enabled, volume = 70, active = true) {
    this.settings(enabled, volume);
    if (enabled && active && state !== this.previous && ['eat', 'pounce', 'molt', 'aim'].includes(state)) this.play(state);
    this.previous = state;
  }
}
