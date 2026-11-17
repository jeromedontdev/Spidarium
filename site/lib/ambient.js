/*
 * Spidarium — ambient life, browser mirror.
 *
 * The device's ambient.c drifts dust motes and gives the spider a breath, a
 * blink and a glance. The browser has no LVGL objects to move, so the motes are
 * drawn straight onto the tank canvas; this module supplies the shared timing
 * so the two stay in step.
 *
 * Everything here is a pure function of the clock. The device uses esp_random()
 * for its intervals, which cannot be reproduced, so the browser derives the
 * same shape from a hash of the time bucket instead. The result reads the same
 * and is stable under test, which is what matters: a test that asserts "the
 * spider blinks" must not be flaky.
 */

const PI = Math.PI;

/* A cheap deterministic hash, so each blink and glance lands on its own
 * interval without a random source. */
function hash(n) {
  let x = n | 0;
  x = (x ^ 61) ^ (x >>> 16);
  x = (x + (x << 3)) | 0;
  x = x ^ (x >>> 4);
  x = Math.imul(x, 0x27d4eb2d);
  x = x ^ (x >>> 15);
  return (x >>> 0) / 4294967296;
}

/* Blink: a short close-open every few seconds. Returns 0..1 where 1 is shut.
 * The window is deliberately short so the eyes are closed for a few frames
 * rather than a visible pause. */
export function blinkAt(nowMs) {
  const period = 2200 + Math.floor(hash(Math.floor(nowMs / 6000)) * 3800);
  const phase = nowMs % period;
  const closeMs = 140;
  if (phase >= closeMs) return 0;
  return 1 - phase / closeMs;
}

/* Glance: the eyes drift to one side and back. Returns -1, 0 or 1. */
export function glanceAt(nowMs) {
  const period = 3000 + Math.floor(hash(Math.floor(nowMs / 8000) + 7) * 5000);
  const phase = nowMs % period;
  const holdMs = 900 + Math.floor(hash(Math.floor(nowMs / 8000) + 13) * 900);
  if (phase >= holdMs) return 0;
  return hash(Math.floor(nowMs / period) + 31) < 0.5 ? -1 : 1;
}

/* Breath: a slow sine, about one breath every four seconds. The amplitude is
 * 8%, not 3%: the abdomen is 13 px across, so a 3% breath is a third of a pixel
 * and rounds away entirely. 8% is the smallest value that actually moves a
 * pixel; more than that reads as a pulse. */
export function breathAt(nowMs) {
  return 1 + Math.sin((nowMs / 4000) * 2 * PI) * 0.08;
}

/* Opacity for the dimmed-enclosure overlay, 0..255. The device used a fixed
 * 110, which reads as a filter laid over a photograph; a slow drift reads as a
 * room whose light is coming from somewhere. The drift is small -- a few
 * percent -- so it never looks like the screen is flickering. */
export function lightOpaAt(nowMs) {
  const v = 110 + Math.round(Math.sin((nowMs / 8000) * 2 * PI) * 8);
  return Math.max(0, Math.min(255, v));
}

/* Dust motes. Eight small squares drifting upward with a sideways sway. The
 * device moves real LVGL objects; here they are just positions, drawn by the
 * caller. `bounds` is the playfield in tank pixels. */
export const MOTE_COUNT = 8;

/* The theme's `dust` column, in BACKGROUNDS order. The device tints its motes
 * from theme->dust, which was defined in the theme table and never used
 * anywhere until now -- so the motes cost no new palette work. */
export const DUST_COLOURS = [
  0xa68150, 0x708052, 0xa17947, 0x9d8052, 0x829564, 0x817955, 0x7f8050, 0x87916d,
  0x8a7a52, 0x6e6e78, 0x9c8a6c, 0x7e8e6c, 0x7c8e5e, 0x9a8452, 0x74808c, 0x76806a,
  0x9a734a, 0x558b98, 0xbaa66a, 0xb2703e, 0xa9d0df, 0x5e9f91, 0x9a8057, 0x6d8661,
  0xb497c1, 0xb9a2c2, 0x9b6b85, 0x9e657d, 0x9d855c, 0xb99c66, 0xae977a, 0xad8a9b,
  0xa28bba, 0xab86bd, 0x9b9257, 0xb18a43, 0xb46ca4, 0x74a393, 0x95acba, 0x6fa5ae,
  0xa58bb6, 0xa0745b, 0x9ca16a, 0x78a4a0, 0xa8a085, 0x9ca776, 0xb78ba6, 0xb1bdd3,
];

export function dustColour(themeId) {
  return DUST_COLOURS[themeId] ?? DUST_COLOURS[0];
}

export function motesAt(nowMs, bounds) {
  const { left, top, right, bottom } = bounds;
  const w = Math.max(1, right - left), h = Math.max(1, bottom - top);
  const out = [];
  for (let i = 0; i < MOTE_COUNT; i++) {
    const seed = i * 97;
    const speed = 4 + hash(seed + 1) * 8;          /* px/s upward */
    const swayRate = 0.4 + hash(seed + 2) * 0.8;   /* rad/s */
    const swayAmp = 2 + hash(seed + 3) * 4;        /* px */
    const size = 2 + Math.floor(hash(seed + 4) * 3);
    const x0 = left + hash(seed + 5) * w;
    /* Wrap at the top so the field never thins out. */
    const travelled = (nowMs / 1000) * speed + hash(seed + 6) * h;
    const y = bottom - (travelled % h);
    const x = x0 + Math.sin((nowMs / 1000) * swayRate + hash(seed + 7) * 6.28) * swayAmp;
    out.push({ x: Math.round(x), y: Math.round(y), size });
  }
  return out;
}
