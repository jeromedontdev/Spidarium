import test from 'node:test';
import assert from 'node:assert/strict';
import { soundSamples, DeviceAudio } from '../lib/device-audio.js';

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
    state: 'running', currentTime: 3,
    createBuffer: () => ({ copyToChannel() {} }),
    createGain: () => ({ gain: new Gain(), connect() {}, disconnect() {} }),
    createBufferSource: () => ({ connect() {}, disconnect() {}, start() {},
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

test('automatic ambience never schedules pet chirps or foreground jump sounds', () => {
  const sound = audio(), effects = [];
  sound.play = effect => effects.push(effect);
  for (let i = 1; i <= 20; i++) sound.tick('idle', i * 21000, true);
  assert.equal(effects.length, 20);
  assert.ok(effects.every(effect => ['rustle', 'drop'].includes(effect)));
  sound.tick('pounce', 500000, true);
  assert.equal(effects.at(-1), 'pounce');
  assert.equal(effects.length, 21, 'no ambient trigger during a pounce');
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
