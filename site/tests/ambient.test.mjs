import test from 'node:test';
import assert from 'node:assert/strict';
import { blinkAt, glanceAt, breathAt, motesAt, dustColour, lightOpaAt, MOTE_COUNT, DUST_COLOURS } from '../lib/ambient.js';
import { renderSpider } from '../lib/spider-renderer.js';
import { BACKGROUNDS } from '../lib/device-engine.js';

const BOUNDS = { left: 8, top: 8, right: 472, bottom: 472 };

test('the spider blinks, and the blink is a short window', () => {
  // A blink must actually happen: sample a full minute and require several.
  let shut = 0;
  for (let t = 0; t < 60000; t += 16) if (blinkAt(t) > 0.55) shut++;
  assert.ok(shut > 0, 'the spider never blinks');
  // ...but it must be brief. More than a fifth of the time shut reads as a
  // broken renderer, not a blink.
  assert.ok(shut < 60000 / 16 * 0.2, `eyes shut too long: ${shut} frames`);
});

test('blink is always in range and returns to open', () => {
  for (let t = 0; t < 20000; t += 7) {
    const b = blinkAt(t);
    assert.ok(b >= 0 && b <= 1, `blink out of range at ${t}: ${b}`);
  }
  // Every blink closes fully and then opens fully.
  assert.equal(blinkAt(0), 1);
  assert.equal(blinkAt(1000), 0);
});

test('the spider glances, and only to one of three positions', () => {
  const seen = new Set();
  for (let t = 0; t < 60000; t += 16) {
    const g = glanceAt(t);
    assert.ok(g === -1 || g === 0 || g === 1, `bad glance ${g}`);
    seen.add(g);
  }
  assert.ok(seen.has(0), 'the eyes never rest');
  assert.ok(seen.has(1) || seen.has(-1), 'the eyes never glance');
});

test('breathing stays within a range that moves a pixel', () => {
  for (let t = 0; t < 20000; t += 13) {
    const b = breathAt(t);
    assert.ok(b >= 0.91 && b <= 1.09, `breath out of range at ${t}: ${b}`);
  }
  // It must actually vary, or the abdomen is static and the effect is dead.
  assert.notEqual(breathAt(0), breathAt(1000));
});

test('motes stay inside the tank and actually drift', () => {
  const a = motesAt(0, BOUNDS);
  assert.equal(a.length, MOTE_COUNT);
  for (const m of a) {
    assert.ok(m.x >= BOUNDS.left - 8 && m.x <= BOUNDS.right + 8, `mote x out of tank: ${m.x}`);
    assert.ok(m.y >= BOUNDS.top - 1 && m.y <= BOUNDS.bottom + 1, `mote y out of tank: ${m.y}`);
    assert.ok(m.size >= 2 && m.size <= 4, `mote size out of range: ${m.size}`);
  }
  // A field that never advances is the failure mode that looks fine in a
  // screenshot, so require real movement.
  const b = motesAt(500, BOUNDS);
  assert.ok(a.some((m, i) => m.y !== b[i].y), 'motes never move');
});

test('motes wrap rather than escaping the tank over a long run', () => {
  for (let t = 0; t < 120000; t += 250) {
    for (const m of motesAt(t, BOUNDS)) {
      assert.ok(m.y >= BOUNDS.top - 1 && m.y <= BOUNDS.bottom + 1, `mote escaped at ${t}: ${m.y}`);
    }
  }
});

test('the enclosure light drifts but never flickers', () => {
  let lo = 255, hi = 0;
  for (let t = 0; t < 60000; t += 50) {
    const v = lightOpaAt(t);
    assert.ok(v >= 0 && v <= 255, `light out of range: ${v}`);
    lo = Math.min(lo, v); hi = Math.max(hi, v);
  }
  assert.ok(hi > lo, 'the enclosure light never drifts');
  assert.ok(hi - lo <= 20, `the enclosure light flickers: ${lo}..${hi}`);
});

test('every theme has a dust colour', () => {
  assert.equal(DUST_COLOURS.length, BACKGROUNDS.length);
  for (let i = 0; i < BACKGROUNDS.length; i++) {
    const c = dustColour(i);
    assert.ok(Number.isInteger(c) && c >= 0 && c <= 0xffffff, `bad dust for ${BACKGROUNDS[i]}`);
  }
  // An unknown theme must fall back rather than return undefined.
  assert.equal(dustColour(9999), DUST_COLOURS[0]);
});

test('a blink closes the eyes and a glance moves the pupils', () => {
  const pixels = new Uint8ClampedArray(96 * 96 * 4);
  const base = { heading: Math.PI * .5, state: 'idle', now: 0 };

  renderSpider(pixels, { ...base, blink: 0, glance: 0 });
  const open = pixels.slice();

  renderSpider(pixels, { ...base, blink: 1, glance: 0 });
  const shut = pixels.slice();
  assert.notDeepEqual(open, shut, 'a blink did not change the render');

  renderSpider(pixels, { ...base, blink: 0, glance: 1 });
  const glanced = pixels.slice();
  assert.notDeepEqual(open, glanced, 'a glance did not change the render');

  // A glance must not move the body: only the pupils shift. Compare the
  // abdomen, which is well behind the eyes.
  const abdomen = (buf) => {
    let sum = 0;
    for (let y = 30; y < 60; y++) for (let x = 30; x < 60; x++) sum += buf[(y * 96 + x) * 4 + 3];
    return sum;
  };
  assert.equal(abdomen(open), abdomen(glanced), 'the glance moved the body');
});

test('breathing changes the abdomen but not the legs', () => {
  const pixels = new Uint8ClampedArray(96 * 96 * 4);
  const base = { heading: Math.PI * .5, state: 'idle', now: 0, blink: 0, glance: 0 };
  renderSpider(pixels, { ...base, breath: 0.92 });
  const small = pixels.slice();
  renderSpider(pixels, { ...base, breath: 1.08 });
  const large = pixels.slice();
  assert.notDeepEqual(small, large, 'breathing did not change the render');
});

test('a sleeping spider does not blink or breathe', () => {
  const pixels = new Uint8ClampedArray(96 * 96 * 4);
  const base = { heading: Math.PI * .5, state: 'sleep', now: 0 };
  renderSpider(pixels, { ...base, blink: 0, glance: 0, breath: 1 });
  const a = pixels.slice();
  renderSpider(pixels, { ...base, blink: 1, glance: 1, breath: 1.03 });
  const b = pixels.slice();
  assert.deepEqual(a, b, 'a sleeping spider moved');
});
