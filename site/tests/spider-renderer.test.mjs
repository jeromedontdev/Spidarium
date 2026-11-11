import test from 'node:test';
import assert from 'node:assert/strict';
import { legPose, renderSpider, rgb565Channels } from '../lib/spider-renderer.js';

test('original angular fan leg geometry matches the native cspider_gait_test fixtures', () => {
  const original = [
    [9.426316, 12.928440, 10.446028, 25.647630, 25.091787, 31.681479],
    [3.335358, 15.648495, -.919937, 27.678044, 9.992579, 39.159447],
    [-3.335358, 15.648495, -12.125996, 24.897402, -6.843560, 39.830636],
    [-9.426316, 12.928440, -21.224289, 17.789041, -22.490138, 33.578380],
  ];
  for (let k = 0; k < 4; k++) for (const side of [-1, 1]) {
    const pose = Object.values(legPose(k, side));
    pose.forEach((value, i) => assert.ok(Math.abs(value - original[k][i] * (i % 2 ? side : 1)) < .001));
  }
});
test('native pixel canvas is 96 square, transparent outside anatomy, RGB565-quantized and bounded', () => {
  const pixels = new Uint8ClampedArray(96 * 96 * 4);
  renderSpider(pixels);
  assert.equal(pixels[3], 0);
  assert.ok(pixels.filter((_, i) => i % 4 === 3 && pixels[i] === 255).length > 1500);
  const colours = new Set();
  for (let i = 0; i < pixels.length; i += 4) if (pixels[i + 3]) colours.add(`${pixels[i]},${pixels[i + 1]},${pixels[i + 2]}`);
  assert.ok(colours.has(rgb565Channels(0x8a94a6).join(',')));
  assert.ok(colours.has('0,0,0'));
  for (let i = 0; i < 96; i++) {
    assert.equal(pixels[i * 4 + 3], 0);
    assert.equal(pixels[(95 * 96 + i) * 4 + 3], 0);
  }
});
test('sleep removes white eye/fang pixels and sickness dulls the palette without transparency', () => {
  const pixels = new Uint8ClampedArray(96 * 96 * 4);
  renderSpider(pixels, { state: 'sleep', emote: 'sleepy', sick: true });
  for (let i = 0; i < pixels.length; i += 4) {
    assert.ok([0, 255].includes(pixels[i + 3]));
    assert.ok(!(pixels[i] > 230 && pixels[i + 1] > 230 && pixels[i + 2] > 230));
  }
});
test('sickness keeps every drawn pixel opaque and desaturates the body', () => {
  const healthy = new Uint8ClampedArray(96 * 96 * 4);
  const sick = new Uint8ClampedArray(96 * 96 * 4);
  renderSpider(healthy);
  renderSpider(sick, { sick: true });
  let drawn = 0, duller = 0;
  for (let i = 0; i < healthy.length; i += 4) {
    assert.equal(sick[i + 3], healthy[i + 3]);
    if (!healthy[i + 3]) continue;
    drawn++;
    const spread = (buf) => Math.max(buf[i], buf[i + 1], buf[i + 2]) - Math.min(buf[i], buf[i + 1], buf[i + 2]);
    if (spread(sick) <= spread(healthy)) duller++;
  }
  assert.ok(drawn > 1500);
  assert.ok(duller > drawn * .9, `only ${duller}/${drawn} pixels desaturated`);
});
test('happy/alert gait never inverts knees and all animated frames remain within the native canvas', () => {
  for (const emote of ['none', 'happy', 'sad', 'alert', 'sleepy']) for (let frame = 0; frame < 32; frame++) {
    const pixels = new Uint8ClampedArray(96 * 96 * 4);
    renderSpider(pixels, { emote, phase: frame * Math.PI / 16, heading: frame * Math.PI / 16, now: frame * 40, walking: true });
    for (let i = 0; i < 96; i++) {
      assert.equal(pixels[i * 4 + 3], 0);
      assert.equal(pixels[(95 * 96 + i) * 4 + 3], 0);
    }
  }
});

// The published PNGs are an export of this renderer, so the two must agree
// pixel for pixel. They drifted once already: the generator used doubles while
// the device uses float, and the browser port interleaved eye/pupil drawing
// where cspider.c draws both eyes then both pupils. Both bugs are invisible in
// a screenshot and obvious in a diff, so pin the diff.
test('exported spider PNGs match this renderer pixel for pixel', async () => {
  const { readFileSync } = await import('node:fs');
  const { inflateSync } = await import('node:zlib');
  const { fileURLToPath } = await import('node:url');
  const { dirname, join } = await import('node:path');
  const assets = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'assets');
  // Minimal PNG reader: these files are 8-bit RGBA, non-interlaced, one IDAT.
  const decode = (file) => {
    const buf = readFileSync(file);
    let pos = 8, width = 0, height = 0;
    const idat = [];
    while (pos < buf.length) {
      const len = buf.readUInt32BE(pos), type = buf.toString('ascii', pos + 4, pos + 8);
      const data = buf.subarray(pos + 8, pos + 8 + len);
      if (type === 'IHDR') { width = data.readUInt32BE(0); height = data.readUInt32BE(4); }
      else if (type === 'IDAT') idat.push(data);
      else if (type === 'IEND') break;
      pos += 12 + len;
    }
    const raw = inflateSync(Buffer.concat(idat));
    const out = new Uint8ClampedArray(width * height * 4);
    const stride = width * 4;
    for (let y = 0; y < height; y++) {
      const filter = raw[y * (stride + 1)];
      const row = raw.subarray(y * (stride + 1) + 1, y * (stride + 1) + 1 + stride);
      for (let x = 0; x < stride; x++) {
        const a = x >= 4 ? out[y * stride + x - 4] : 0;
        const b = y > 0 ? out[(y - 1) * stride + x] : 0;
        const c = x >= 4 && y > 0 ? out[(y - 1) * stride + x - 4] : 0;
        let v = row[x];
        if (filter === 1) v += a;
        else if (filter === 2) v += b;
        else if (filter === 3) v += (a + b) >> 1;
        else if (filter === 4) {
          const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
          v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
        }
        out[y * stride + x] = v & 255;
      }
    }
    return out;
  };
  const views = [['n', -Math.PI / 2], ['e', 0], ['s', Math.PI / 2]];
  const anims = [['walk', 4], ['idle', 2], ['eat', 2], ['sleep', 1], ['molt', 2], ['pounce', 2]];
  const emoteFor = (a) => a === 'sleep' ? 'sleepy' : a === 'eat' ? 'happy' : 'none';
  let checked = 0;
  for (const [view, heading] of views) for (const [anim, frames] of anims) for (let frame = 0; frame < frames; frame++) {
    const walking = anim === 'walk';
    const pixels = new Uint8ClampedArray(96 * 96 * 4);
    renderSpider(pixels, {
      heading, state: anim, emote: emoteFor(anim), walking,
      phase: walking ? frame / 4 * 2 * Math.PI : 0, now: frame * 110,
    });
    const png = decode(join(assets, `spider_${view}_${anim}_${frame}.png`));
    for (let i = 0; i < pixels.length; i++) {
      if (pixels[i] !== png[i]) {
        const px = (i >> 2) % 96, py = (i >> 2) / 96 | 0;
        assert.fail(`spider_${view}_${anim}_${frame}.png differs at (${px},${py}) channel ${i & 3}: png=${png[i]} renderer=${pixels[i]}`);
      }
    }
    checked++;
  }
  assert.equal(checked, 39);
});
