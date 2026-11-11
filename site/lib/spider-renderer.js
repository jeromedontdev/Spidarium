// Pixel port of main/cspider.c: identical 96×96 geometry and RGB565 colours.
export const BODY_COLOURS = [0x2b3138, 0x45454b, 0x4d2c1e, 0x27402c, 0x272e4d, 0x402740];
export const ACCENT_COLOURS = [0x000000, 0xf7f9ff, 0x8a94a6, 0xf5b342, 0xe06c6c, 0x58d68d, 0x7c9cff, 0x4fd1c5, 0xb07cff];
const PI = Math.PI;
const trunc = Math.trunc;
// cspider.c computes in `float`, not `double`. The difference is not academic:
// cosf(PI_F / 2) is -4.37e-8 while Math.cos(Math.PI / 2) is +6.12e-17, so a
// coordinate landing exactly on an integer truncates one pixel apart. Rounding
// every intermediate through Math.fround keeps this port pixel-identical to the
// panel instead of merely close.
const f = Math.fround;
const PI_F = f(3.14159265);
const fsin = (x) => f(Math.sin(f(x)));
const fcos = (x) => f(Math.cos(f(x)));
export function legPose(k, side, phase = 0, walking = false, emote = 'none', now = 0) {
  const base = f(side * f(f(PI_F * f(.5)) + f(f(k - f(1.5)) * f(.42))));
  const gait = f(f(phase + f(k * f(PI_F * f(.5)))) + (side < 0 ? f(0) : PI_F));
  // The front pair leads the walk, matching main/cspider.c leg_pose().
  const lead = k === 0 ? f(1.55) : k === 1 ? f(1.15) : f(1);
  let swing = walking ? f(f(f(.42) * lead) * fsin(gait)) : f(0);
  let bend = f(f(.55) + (walking ? f(f(f(.30) * lead) * fsin(f(gait + f(PI_F * f(.5))))) : f(0)));
  const wave = fsin(f(f(now % 1200) * f(f(2 * PI_F) / f(1200))));
  if (emote === 'happy' && k === 0) { swing = f(swing + f(f(1.10) * wave)); bend = f(bend - f(f(.55) + f(f(.35) * wave))); }
  else if (emote === 'sad') { bend = f(bend + f(.60)); swing = f(swing * f(.35)); }
  else if (emote === 'alert') { bend = f(bend - f(f(.45) + f(f(.10) * wave))); swing = f(swing * f(.25)); }
  else if (emote === 'sleepy') { bend = f(bend - f(f(.30) + f(f(.05) * wave))); swing = f(swing * f(.15)); }
  const angle = Math.max(f(.15), Math.min(f(1.25), f(swing + bend)));
  const dir1 = f(base + f(side * angle)), dir2 = f(base - f(side * angle));
  const hf = f(16 * fcos(base)), hr = f(16 * fsin(base));
  const kf = f(hf + f(f(22 * f(.58)) * fcos(dir1))), kr = f(hr + f(f(22 * f(.58)) * fsin(dir1)));
  return { hf, hr, kf, kr, ff: f(kf + f(f(22 * f(.72)) * fcos(dir2))), fr: f(kr + f(f(22 * f(.72)) * fsin(dir2))) };
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
  const toCanvas = (fwd, right, sf = scaleF, sr = scaleR) => {
    const fx = fcos(heading), fy = fsin(heading), rx = f(-fy), ry = fx;
    const fw = f(f(fwd) * f(sf)), rt = f(f(right) * f(sr));
    return [
      48 + trunc(f(f(fx * fw) + f(rx * rt))),
      48 + trunc(f(f(fy * fw) + f(ry * rt))),
    ];
  };
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
  // C integer division truncates toward zero; JS `/` then trunc() matches, but
  // the products must stay exact, so keep them in integer arithmetic.
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
    circle(...toCanvas(f(forward - 1.5), f(-1.5)), radius - 3, mid);
    circle(...toCanvas(f(forward - 3), f(-3)), radius - (radius === 13 ? 7 : 6), hi);
    for (let i = 0; i < 14; i++) {
      const angle = f(f(f(i) * f(2 * PI_F)) / f(14));
      circle(...toCanvas(f(forward + f(fcos(angle) * f(radius + 2))),
        f(fsin(angle) * f(radius + 2))), 3 - (i % 2), mid);
    }
  }
  for (const [forward, right] of [[4, -8], [4, 8], [0, -4], [0, 4]]) circle(...toCanvas(forward, right), 2, dull(0x6b7280));
  const asleep = state === 'sleep';
  // Both eyes first, then both pupils, then both fangs -- the same order as
  // cspider_render(). Interleaving them lets the second eye overwrite the first
  // pupil's inner edge, which is a visible notch at this size.
  for (const side of [-1, 1]) circle(...toCanvas(14, f(side * 5.5)), 7, asleep ? 0 : dull(0xf7f9ff));
  if (!asleep) {
    for (const side of [-1, 1]) circle(...toCanvas(15, f(side * 5.5)), 4, pupil);
    for (const side of [-1, 1]) circle(...toCanvas(16, f(f(side * 5.5) - 2)), 1, dull(0xffffff));
    for (const side of [-1, 1]) {
      triangle([toCanvas(f(8 + 15.5 - f(4.5 * .5)), f(f(side * 4) - 3)),
        toCanvas(f(8 + 15.5 - f(4.5 * .5)), f(f(side * 4) + 3)),
        toCanvas(f(8 + 15.5 + f(4.5 * .5)), f(side * 4))], fang);
    }
  }
  return pixels;
}
