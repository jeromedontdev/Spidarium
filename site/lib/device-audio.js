const EFFECTS = {
  eat: [880, 520, 140, .35], pounce: [320, 1100, 90, .40],
  molt: [440, 1320, 260, .30], tap: [0, 0, 60, .25],
  alert: [660, 440, 180, .35], aim: [520, 760, 90, .22],
  perfect: [880, 1760, 160, .30], rustle: [0, 0, 280, .035],
  chirp: [1600, 2200, 160, .045], drop: [900, 280, 220, .055],
};
// PCM synthesis is the same sweep/envelope/LCG as main/audio.c.
export function soundSamples(effect) {
  if (!EFFECTS[effect]) throw new Error(`Unknown sound effect: ${effect}`);
  const [f0, f1, ms, gain] = EFFECTS[effect], length = 16000 * ms / 1000;
  const samples = new Float32Array(length); let phase = 0, seed = 0x12345678;
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
  if (effect === 'rustle') for (let i = 1; i < length; i++) samples[i] = Math.trunc((samples[i] + 3 * samples[i - 1]) * 32768 / 4) / 32768;
  return samples;
}
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
export class DeviceAudio {
  constructor(report) { this.report = report; this.context = null; this.until = 0; this.nextAmbient = 12000; this.previous = null; }
  async unlock(enabled) {
    if (!enabled) return;
    try {
      const Context = globalThis.AudioContext ?? globalThis.webkitAudioContext;
      if (!Context) throw new Error('Web Audio is unavailable');
      this.context ??= new Context();
      if (this.context.state === 'suspended') await this.context.resume();
    } catch (error) { this.report(`Sound unavailable: ${error.message}`, true); }
  }
  play(effect) {
    if (!this.context || this.context.state !== 'running') return;
    const samples = soundSamples(effect), buffer = this.context.createBuffer(1, samples.length, 16000);
    buffer.copyToChannel(samples, 0); const source = this.context.createBufferSource();
    source.buffer = buffer; source.connect(this.context.destination); source.start();
    this.until = performance.now() + samples.length / 16;
  }
  tick(state, now, enabled) {
    if (enabled && state !== this.previous && ['eat', 'pounce', 'molt', 'aim'].includes(state)) this.play(state);
    this.previous = state;
    if (now < this.nextAmbient) return;
    this.nextAmbient = now + 12000 + Math.floor(Math.random() * 13000);
    if (enabled && now >= this.until && !['aim', 'pounce', 'eat'].includes(state))
      this.play(['rustle', 'chirp', 'drop'][Math.floor(Math.random() * 3)]);
  }
}
