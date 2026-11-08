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
test('sleep removes white eye/fang pixels and sickness applies native 60% opacity', () => {
  const pixels = new Uint8ClampedArray(96 * 96 * 4);
  renderSpider(pixels, { state: 'sleep', emote: 'sleepy', sick: true });
  for (let i = 0; i < pixels.length; i += 4) {
    assert.ok([0, 153].includes(pixels[i + 3]));
    assert.ok(!(pixels[i] > 230 && pixels[i + 1] > 230 && pixels[i + 2] > 230));
  }
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
