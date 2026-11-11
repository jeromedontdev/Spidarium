// Pixel port of main/cspider.c: identical 96×96 geometry and RGB565 colours.
export const BODY_COLOURS = [0x2b3138, 0x45454b, 0x4d2c1e, 0x27402c, 0x272e4d, 0x402740];
export const ACCENT_COLOURS = [0x000000, 0xf7f9ff, 0x8a94a6, 0xf5b342, 0xe06c6c, 0x58d68d, 0x7c9cff, 0x4fd1c5, 0xb07cff];
const PI = Math.PI;
const trunc = Math.trunc;
export function legPose(k, side, phase = 0, walking = false, emote = 'none', now = 0) {
  const base = side * (PI * .5 + (k - 1.5) * .42);
  const gait = phase + k * (PI * .5) + (side < 0 ? 0 : PI);
  // The front pair leads the walk, matching main/cspider.c leg_pose().
  const lead = k === 0 ? 1.55 : k === 1 ? 1.15 : 1;
  let swing = walking ? .42 * lead * Math.sin(gait) : 0;
  let bend = .55 + (walking ? .30 * lead * Math.sin(gait + PI * .5) : 0);
  const wave = Math.sin((now % 1200) * (2 * PI / 1200));
  if (emote === 'happy' && k === 0) { swing += 1.10 * wave; bend -= .55 + .35 * wave; }
  else if (emote === 'sad') { bend += .60; swing *= .35; }
  else if (emote === 'alert') { bend -= .45 + .10 * wave; swing *= .25; }
  else if (emote === 'sleepy') { bend -= .30 + .05 * wave; swing *= .15; }
  const angle = Math.max(.15, Math.min(1.25, swing + bend));
  const dir1 = base + side * angle, dir2 = base - side * angle;
  const hf = 16 * Math.cos(base), hr = 16 * Math.sin(base);
  const kf = hf + 22 * .58 * Math.cos(dir1), kr = hr + 22 * .58 * Math.sin(dir1);
  return { hf, hr, kf, kr, ff: kf + 22 * .72 * Math.cos(dir2), fr: kr + 22 * .72 * Math.sin(dir2) };
}
export function rgb565Channels(rgb) {
  const r = (rgb >> 19) & 31, g = (rgb >> 10) & 63, b = (rgb >> 3) & 31;
  return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
}
export function renderSpider(pixels, { heading = PI * .5, state = 'idle', emote = 'none', now = 0, phase = 0, walking = false, colour = 0, pupilColour = 0, legColour = 2, fangColour = 1, sick = false } = {}) {
  if (!(pixels instanceof Uint8ClampedArray) || pixels.length !== 96 * 96 * 4) throw new Error('Spider needs a 96×96 RGBA buffer');
  pixels.fill(0);
  let scaleF = 1, scaleR = 1;
  if (state === 'pounce') { scaleF = 1.17; scaleR = .82; }
  else if (state === 'aim') { scaleF = 1.11; scaleR = .84; }
  else if (state === 'sleep') { scaleF = 1.05; scaleR = .95; }
  const base = BODY_COLOURS[colour % BODY_COLOURS.length];
  const lighten = (rgb, divisor) => {
    let r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    r += trunc((255 - r) / divisor); g += trunc((255 - g) / divisor); b += trunc((255 - b) / divisor);
    return (r << 16) | (g << 8) | b;
  };
  // Sickness dulls the palette instead of making the sprite translucent. The
  // device used to apply LV_OPA_60 to the whole canvas, which let the pale
  // ground textures show through and read as a transparency bug. The grey
  // target is the pixel's own luma so near-white pixels desaturate too.
  const dull = (rgb) => {
    if (!sick) return rgb;
    const r0 = (rgb >> 16) & 255, g0 = (rgb >> 8) & 255, b0 = rgb & 255;
    const luma = trunc((r0 * 30 + g0 * 59 + b0 * 11) / 100);
    const mix = (c) => trunc(trunc((c * 55 + luma * 45) / 100) * 88 / 100);
    return (mix(r0) << 16) | (mix(g0) << 8) | mix(b0);
  };
  const mid = dull(lighten(base, 4)), hi = dull(lighten(lighten(base, 4), 3)), body = dull(base);
  const pupil = dull(ACCENT_COLOURS[pupilColour % 9]), leg = dull(ACCENT_COLOURS[legColour % 9]), fang = dull(ACCENT_COLOURS[fangColour % 9]);
  const toCanvas = (fwd, right, sf = scaleF, sr = scaleR) => [
    48 + trunc(Math.cos(heading) * fwd * sf - Math.sin(heading) * right * sr),
    48 + trunc(Math.sin(heading) * fwd * sf + Math.cos(heading) * right * sr),
  ];
  const put = (x, y, channels) => {
    if (x < 0 || y < 0 || x >= 96 || y >= 96) return;
    const i = (y * 96 + x) * 4;
    pixels[i] = channels[0]; pixels[i + 1] = channels[1]; pixels[i + 2] = channels[2]; pixels[i + 3] = 255;
  };
  const circle = (cx, cy, radius, rgb) => {
    const channels = rgb565Channels(rgb);
    for (let dy = -radius; dy <= radius; dy++) {
      const width = trunc(Math.sqrt(radius * radius - dy * dy));
      for (let x = cx - width; x <= cx + width; x++) put(x, cy + dy, channels);
    }
  };
  const line = (start, end, width, rgb) => {
    const channels = rgb565Channels(rgb), dx = end[0] - start[0], dy = end[1] - start[1];
    const steps = Math.max(Math.abs(dx), Math.abs(dy)), half = trunc(width / 2);
    if (!steps) { put(...start, channels); return; }
    for (let i = 0; i <= steps; i++) {
      const x = start[0] + trunc(dx * i / steps), y = start[1] + trunc(dy * i / steps);
      for (let oy = -half; oy <= half; oy++) for (let ox = -half; ox <= half; ox++) {
        if (ox * ox + oy * oy <= half * half + half) put(x + ox, y + oy, channels);
      }
    }
  };
  const triangle = (points, rgb) => {
    const channels = rgb565Channels(rgb);
    const min = Math.max(0, Math.min(...points.map(p => p[1]))), max = Math.min(95, Math.max(...points.map(p => p[1])));
    for (let y = min; y <= max; y++) {
      let lo = 96, hi = -1;
      for (let edge = 0; edge < 3; edge++) {
        const [ax, ay] = points[edge], [bx, by] = points[(edge + 1) % 3];
        if (ay === by) { if (y === ay) { lo = Math.min(lo, ax, bx); hi = Math.max(hi, ax, bx); } continue; }
        if ((y < ay && y < by) || (y > ay && y > by)) continue;
        const x = ax + trunc((bx - ax) * (y - ay) / (by - ay));
        lo = Math.min(lo, x); hi = Math.max(hi, x);
      }
      for (let x = Math.max(0, lo); x <= Math.min(95, hi); x++) put(x, y, channels);
    }
  };
  for (let i = 0; i < 8; i++) {
    const pose = legPose(i & 3, i < 4 ? -1 : 1, phase, walking, emote, now);
    const hip = toCanvas(pose.hf, pose.hr, 1, 1), knee = toCanvas(pose.kf, pose.kr, 1, 1), foot = toCanvas(pose.ff, pose.fr, 1, 1);
    line(hip, knee, 3, leg); line(knee, foot, 3, leg);
  }
  for (const [forward, radius] of [[-13, 13], [8, 11]]) {
    circle(...toCanvas(forward, 0), radius, body);
    circle(...toCanvas(forward - 1.5, -1.5), radius - 3, mid);
    circle(...toCanvas(forward - 3, -3), radius - (radius === 13 ? 7 : 6), hi);
    for (let i = 0; i < 14; i++) {
      const angle = i * 2 * PI / 14;
      circle(...toCanvas(forward + Math.cos(angle) * (radius + 2), Math.sin(angle) * (radius + 2)), 3 - (i % 2), mid);
    }
  }
  for (const [forward, right] of [[4, -8], [4, 8], [0, -4], [0, 4]]) circle(...toCanvas(forward, right), 2, dull(0x6b7280));
  const asleep = state === 'sleep';
  for (const side of [-1, 1]) {
    circle(...toCanvas(14, side * 5.5), 7, asleep ? 0 : dull(0xf7f9ff));
    if (!asleep) {
      circle(...toCanvas(15, side * 5.5), 4, pupil);
      circle(...toCanvas(16, side * 5.5 - 2), 1, dull(0xffffff));
      triangle([toCanvas(8 + 15.5 - 4.5 * .5, side * 4 - 3),
        toCanvas(8 + 15.5 - 4.5 * .5, side * 4 + 3), toCanvas(8 + 15.5 + 4.5 * .5, side * 4)], fang);
    }
  }
  return pixels;
}
