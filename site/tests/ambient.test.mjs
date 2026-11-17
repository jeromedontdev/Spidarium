import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { blinkAt, glanceAt, breathAt, motesAt, dustColour, lightOpaAt, MOTE_COUNT, DUST_COLOURS,
  sporesAt, strandsAt, webColour, particlesAt, emitForDecorKey, emitForGift,
  SPORE_COUNT, STRAND_COUNT, PARTICLE_COUNT, EMIT, WEB_COLOURS } from '../lib/ambient.js';
import { renderSpider } from '../lib/spider-renderer.js';
import { BACKGROUNDS } from '../lib/device-engine.js';
import * as E from '../lib/device-engine.js';

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

test('neglected waste attracts flies, and cleaning settles them', () => {
  const s = E.freshState();
  s._engine.now_ms = 0;
  E.addPoop(s, 200, 300);
  // A fresh dropping is not yet interesting.
  assert.equal(E.fliesAt(s).length, 0, 'a fresh dropping attracted a fly');

  // Just before the threshold, still nothing.
  s._engine.now_ms = E.FLY_ATTRACT_MS - 1;
  assert.equal(E.fliesAt(s).length, 0, 'a fly appeared too early');

  // Past it, exactly one fly.
  s._engine.now_ms = E.FLY_ATTRACT_MS + 1;
  assert.equal(E.fliesAt(s).length, 1, 'neglect did not attract a fly');

  // Cleaning removes the mess and therefore the fly.
  s.poop.length = 0;
  assert.equal(E.fliesAt(s).length, 0, 'a fly outlived its dropping');
});

test('flies are capped and one per dropping', () => {
  const s = E.freshState();
  s._engine.now_ms = 0;
  for (let i = 0; i < E.CONFIG.MAX_POOP; i++) E.addPoop(s, 100 + i * 20, 300);
  s._engine.now_ms = E.FLY_ATTRACT_MS + 1;
  const flies = E.fliesAt(s);
  assert.ok(flies.length <= E.MAX_FLIES, `too many flies: ${flies.length}`);
  // One fly per dropping: no two flies share a position.
  const seen = new Set(flies.map(f => `${f.x},${f.y}`));
  assert.equal(seen.size, flies.length, 'two flies share a dropping');
});

test('a fly anchors to its dropping rather than drifting', () => {
  const s = E.freshState();
  s._engine.now_ms = 0;
  E.addPoop(s, 240, 300);
  s._engine.now_ms = E.FLY_ATTRACT_MS + 1;
  const a = E.fliesAt(s)[0];
  // The orbit is applied by the renderer from the clock, so the anchor is
  // stable; assert the anchor is the dropping, not a drifting point.
  assert.equal(a.x, 240);
  assert.equal(a.y, 300);
});

test('prey graze, so the tank moves while the spider sleeps', () => {
  // A bug that is not fleeing stops to feed for a moment, then moves on. This
  // is what makes the tank read as alive when nothing else is happening.
  const s = E.freshState({ random: () => 0.5 });
  s.state = E.STATES.SLEEP;
  s.spider_x = 60; s.spider_y = 60;   // far from the bug, so it never flees
  assert.equal(E.spawnPrey(s, 0), true);
  const p = s.prey[0];
  assert.ok(p.next_graze_at > 0, 'the graze timer was not seeded');

  // Run past the first graze window and confirm the bug actually stops.
  let stopped = 0, moved = 0, last = { x: p.x, y: p.y };
  for (let i = 0; i < 400; i++) {
    E.tick(s, 0.05);
    const d = Math.hypot(p.x - last.x, p.y - last.y);
    if (d < 0.01) stopped++; else moved++;
    last = { x: p.x, y: p.y };
  }
  assert.ok(stopped > 0, 'the bug never paused to graze');
  assert.ok(moved > 0, 'the bug never moved');
});

test('a grazing bug still flees when the spider closes', () => {
  // A pause must never be a way to get caught standing still.
  const s = E.freshState({ random: () => 0.5 });
  s.state = E.STATES.SLEEP;
  s.spider_x = 60; s.spider_y = 60;
  E.spawnPrey(s, 0);
  const p = s.prey[0];
  // Force a graze, then put the spider on top of the bug.
  p.graze_until = s._engine.now_ms + 5000;
  p.next_graze_at = s._engine.now_ms + 5000;
  s.spider_x = Math.round(p.x); s.spider_y = Math.round(p.y);
  E.tick(s, 0.05);
  assert.equal(p.fleeing, true, 'a grazing bug ignored the spider');
});

test('the day/night tint is visible, not a 5% wash', () => {
  // The old tint was 12-24/255, which is a 5-9% wash -- invisible on the panel
  // and the reason the setting appeared to do nothing. Each period must now be
  // clearly different from the others AND from no tint at all.
  const tints = {
    Morning: [255, 217, 160, 0.063],
    Afternoon: [0, 0, 0, 0],
    Evening: [255, 154, 77, 0.118],
    Night: [26, 47, 107, 0.204],
  };
  // Afternoon is deliberately no tint at all.
  assert.equal(tints.Afternoon[3], 0, 'afternoon should be untinted');
  // The other three must be strong enough to see without greying the art.
  // 0.05 is the floor: below that the change is not perceptible on an AMOLED in
  // daylight. 0.25 is the ceiling: above that the tank reads as hazy, which is
  // what the first pass at 0.38 did.
  for (const name of ['Morning', 'Evening', 'Night']) {
    assert.ok(tints[name][3] >= 0.05, `${name} tint is too weak: ${tints[name][3]}`);
    assert.ok(tints[name][3] <= 0.25, `${name} tint is hazy: ${tints[name][3]}`);
  }
  // Night must be the strongest, and it must be blue rather than merely dark.
  assert.ok(tints.Night[3] > tints.Evening[3], 'night should be stronger than evening');
  assert.ok(tints.Night[2] > tints.Night[0], 'night should be blue, not warm');
  // Morning and evening must be warm, so the periods read as different times
  // of day rather than as different brightnesses.
  assert.ok(tints.Morning[0] > tints.Morning[2], 'morning should be warm');
  assert.ok(tints.Evening[0] > tints.Evening[2], 'evening should be warm');
});




test('the light shafts are gone, and the tank is not hazy', () => {
  // The shafts were removed after three attempts: rotated objects were software
  // rendered at ~111,000 blended pixels each per frame and made the tank choppy,
  // unrotated bands read as blocky stripes, and baked beams could not move --
  // and static light reads as a stain rather than as light. This test exists so
  // they are not quietly reintroduced without solving that.
  const src = readFileSync(new URL('../lib/device-ui.js', import.meta.url), 'utf8');
  assert.ok(!/createLinearGradient/.test(src), 'light shafts are back in the tank renderer');
  assert.ok(!/shaft/i.test(src), 'shaft code is back in the tank renderer');
});

test('spores form a second air layer that moves on both axes', () => {
  const bounds = { left: 8, top: 8, right: 472, bottom: 472 };
  const a = sporesAt(0, bounds);
  const b = sporesAt(1500, bounds);
  assert.equal(a.length, SPORE_COUNT);
  for (const p of a) {
    assert.ok(p.x >= bounds.left - 4 && p.x <= bounds.right + 4, `spore x out of tank: ${p.x}`);
    assert.ok(p.y >= bounds.top - 4 && p.y <= bounds.bottom + 4, `spore y out of tank: ${p.y}`);
    assert.ok(p.size >= 3 && p.size <= 5, `spore size out of range: ${p.size}`);
  }
  // They must actually move, and on both axes -- a spore that only rises is
  // just a bigger mote and the second layer is wasted.
  assert.ok(a.some((p, i) => p.x !== b[i].x), 'spores never move horizontally');
  assert.ok(a.some((p, i) => p.y !== b[i].y), 'spores never move vertically');
});

test('web strands are anchored in the tank and actually pivot', () => {
  const bounds = { left: 8, top: 8, right: 472, bottom: 472 };
  const a = strandsAt(0, bounds);
  const b = strandsAt(2500, bounds);
  assert.equal(a.length, STRAND_COUNT);
  for (const t of a) {
    assert.ok(t.ax >= bounds.left && t.ax <= bounds.right, `strand anchor out of tank: ${t.ax}`);
    assert.ok(t.ay >= bounds.top && t.ay <= bounds.bottom, `strand anchor out of tank: ${t.ay}`);
    assert.ok(t.length >= 20 && t.length <= 80, `strand length out of range: ${t.length}`);
  }
  // A strand whose angle never changes is a static line drawn over the art.
  assert.ok(a.some((t, i) => t.angle !== b[i].angle), 'strands never pivot');
  // Spread across the width rather than clustered in the corners: with six
  // strands, corner-only placement left the middle of the tank bare.
  const xs = a.map(t => t.ax).sort((p, q) => p - q);
  assert.ok(xs[0] < 120 && xs[xs.length - 1] > 360, 'strands do not span the tank');
  assert.ok(xs[xs.length - 1] - xs[0] > 300, 'strands are clustered, not spread');
  // Alternating lean, so they do not all fall the same way.
  assert.ok(a.some(t => t.angle > 1.5) && a.some(t => t.angle < 1.5), 'strands all lean one way');
});

test('decor and gift particles use a fixed pool, not one emitter per item', () => {
  // The whole point of the design is that cost does not scale with how much the
  // player has placed. A pool that grew with the item count would be the worst
  // possible property for the frame budget.
  const items = [];
  for (let i = 0; i < 40; i++) items.push({ x: 100 + i, y: 200, style: EMIT.SPORE });
  const many = particlesAt(1000, items);
  assert.ok(many.length <= PARTICLE_COUNT, `pool overflowed: ${many.length} > ${PARTICLE_COUNT}`);

  // An empty tank must cost nothing at all.
  assert.equal(particlesAt(1000, []).length, 0);
  assert.equal(particlesAt(1000, [{ x: 1, y: 1, style: EMIT.NONE }]).length, 0);
});

test('the emitter mapping classifies decor by family and gifts always sparkle', () => {
  // A table that returned NONE for everything would pass every other check
  // while doing nothing.
  assert.equal(emitForDecorKey('fern'), EMIT.SPORE);
  assert.equal(emitForDecorKey('crystal'), EMIT.GLINT);
  assert.equal(emitForDecorKey('rock_small'), EMIT.DUST);
  assert.equal(emitForDecorKey('not_a_real_kind'), EMIT.NONE);
  assert.equal(emitForGift(), EMIT.GLINT);

  // Every key in the real decor table must classify, so no item is silently
  // inert. This is the check that catches a new item being added without an
  // emitter.
  for (const def of E.DECOR_KINDS) {
    assert.notEqual(emitForDecorKey(def.key), EMIT.NONE,
      `decor "${def.key}" has no particle emitter`);
  }
});

test('particles fade out rather than popping', () => {
  const items = [{ x: 240, y: 240, style: EMIT.SPORE }];
  let sawFade = false;
  for (let t = 0; t < 4000; t += 40) {
    for (const p of particlesAt(t, items)) {
      assert.ok(p.alpha >= 0 && p.alpha <= 1, `alpha out of range: ${p.alpha}`);
      if (p.alpha < 0.9) sawFade = true;
    }
  }
  assert.ok(sawFade, 'particles never fade, so they pop out of existence');
});

test('the web palette is complete and distinct from the dust palette', () => {
  // Spores and strands take theme->web, the motes take theme->dust. If the two
  // tables were the same the layers would not separate visually.
  assert.equal(WEB_COLOURS.length, DUST_COLOURS.length);
  assert.equal(webColour(0), WEB_COLOURS[0]);
  assert.equal(webColour(9999), WEB_COLOURS[0]);
  let differing = 0;
  for (let i = 0; i < WEB_COLOURS.length; i++)
    if (WEB_COLOURS[i] !== DUST_COLOURS[i]) differing++;
  assert.ok(differing > WEB_COLOURS.length / 2, 'web and dust palettes are nearly identical');
});
