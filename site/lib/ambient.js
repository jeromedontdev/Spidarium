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

/* Dust motes. Sixteen small squares drifting upward with a sideways sway. The
 * device moves real LVGL objects; here they are just positions, drawn by the
 * caller. `bounds` is the playfield in tank pixels. */
export const MOTE_COUNT = 16;

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

/* The theme's `web` column, in BACKGROUNDS order. This is the near-silhouette
 * tone -- paler and cooler than `dust` -- so spores and strands separate from
 * the dust motes instead of reading as one noisy field. */
export const WEB_COLOURS = [
  0xb5b9a0, 0xb3bf9d, 0xb6b69b, 0xc0c3a8, 0xb6c3a1, 0xbdc4a5, 0xb7bea0, 0xc4c9aa,
  0xb8bda0, 0xb0b0ba, 0xc6c4ac, 0xb6c0a8, 0xb8c2a2, 0xc2bfa2, 0xb4bcc6, 0xb2b8a4,
  0xc4a16d, 0x9bacc3, 0xa2b4d2, 0xc49b69, 0xc5d5d9, 0x9ebd9d, 0xb7aa8a, 0xa2ad88,
  0xc5c69c, 0xb9c4d2, 0xc4b3c9, 0xc9a8b1, 0xcbb18a, 0xb1c3d6, 0xd7c3a2, 0xd3b6c0,
  0xb0bd93, 0xc4b1d1, 0xb7bd87, 0xd5b47c, 0xb2c5d2, 0x9fcaba, 0xc4ccda, 0xb6c9d8,
  0xc7c098, 0xc5aa85, 0xc2c3a1, 0xa2c8bf, 0xd3ceb0, 0xc4cc9d, 0xd7bac9, 0xd0d8e6,
];

export function webColour(themeId) {
  return WEB_COLOURS[themeId] ?? WEB_COLOURS[0];
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

/* Spores: a second, slower air layer that crosses the tank on both axes. The
 * device moves real LVGL objects; here they are positions. Larger and rounder
 * than the motes so the two layers are distinguishable at a glance rather than
 * reading as one noisy field. */
export const SPORE_COUNT = 6;

export function sporesAt(nowMs, bounds) {
  const { left, top, right, bottom } = bounds;
  const w = Math.max(1, right - left), h = Math.max(1, bottom - top);
  const out = [];
  for (let i = 0; i < SPORE_COUNT; i++) {
    const seed = i * 131 + 11;
    const vy = 2 + hash(seed + 1) * 4;                       /* px/s upward */
    const vx = (hash(seed + 2) * 2 - 1) * 5;                 /* px/s sideways */
    const size = 3 + Math.floor(hash(seed + 3) * 3);
    const wobbleRate = 0.3 + hash(seed + 4) * 0.5;
    /* Wrap on both axes so the motion looks continuous rather than resetting. */
    const x = left + (((hash(seed + 5) * w + (nowMs / 1000) * vx) % w) + w) % w;
    const y = bottom - (((hash(seed + 6) * h + (nowMs / 1000) * vy) % h) + h) % h;
    const wobble = Math.sin((nowMs / 1000) * wobbleRate + hash(seed + 7) * 6.28) * 3;
    out.push({ x: Math.round(x + wobble), y: Math.round(y), size });
  }
  return out;
}

/* Web strands: a thin line anchored at one end, pivoting slowly. A strand that
 * translated would read as a sliding stick; pivoting reads as something hanging
 * and catching the air. Returns the anchor and the current angle in radians.
 *
 * Placement is CLUSTERED around shared anchors. Even spacing is a pattern, not a
 * web; jittering each strand independently produces scattered lines rather than
 * a web. What actually reads as a web is several strands leaving the SAME point
 * at different angles, so the anchors are chosen first and each strand is
 * assigned to one of them exactly.
 *
 * Strands are also DYNAMIC: each one is spun, hangs for a while, fades out, and
 * is replaced by a new strand at the same anchor. The web therefore builds up
 * and breaks down over a session instead of being a fixed decoration. The anchor
 * is deliberately NOT re-randomised on respawn -- that would dissolve the
 * clusters back into scattered lines. */
export const STRAND_COUNT = 6;

/* Anchor zones, as fractions of the playfield. The corners get more strands than
 * the middle, which is how a web actually accumulates. */
const WEB_ZONES = [
  [0.07, 0.06],   /* top-left corner */
  [0.93, 0.05],   /* top-right corner */
  [0.34, 0.03],   /* upper left-of-centre */
];

/* Fade envelope, in seconds. Matches the firmware: 1.5 s in, hold, 2.5 s out. */
const STRAND_FADE_IN = 1.5;
const STRAND_FADE_OUT = 2.5;

export function strandsAt(nowMs, bounds) {
  const { left, top, right, bottom } = bounds;
  const out = [];
  const spanX = Math.max(1, right - left);
  const spanY = Math.max(1, bottom - top);

  /* One anchor per zone, jittered once so the clusters are not at fixed
   * positions between loads. These are FIXED for the life of the page: a strand
   * respawns into the same anchor, so the web keeps its shape as individual
   * strands come and go. */
  const anchors = WEB_ZONES.map((z, i) => {
    const seed = i * 401 + 7;
    let ax = left + 20 + Math.round(z[0] * (spanX - 40)) + Math.round(hash(seed) * 30) - 15;
    let ay = top + 12 + Math.round(z[1] * (spanY - 24)) + Math.round(hash(seed + 1) * 22) - 11;
    ax = Math.max(left + 12, Math.min(right - 12, ax));
    ay = Math.max(top + 8, ay);
    return [ax, ay];
  });

  const t = nowMs / 1000;

  for (let i = 0; i < STRAND_COUNT; i++) {
    const seed = i * 173 + 23;
    /* Round-robin across the anchors, so every cluster gets at least one. */
    const [ax, ay] = anchors[i % anchors.length];

    /* Each strand runs its own cycle: a life of 20-70 s, offset so the whole web
     * never vanishes at once. The cycle index is derived from the clock, which
     * keeps this a pure function of time -- no state to carry between frames. */
    const lifeMax = 20 + hash(seed + 9) * 50;
    const phase = hash(seed + 10) * lifeMax;
    const cyclePos = (t + phase) % lifeMax;
    const cycle = Math.floor((t + phase) / lifeMax);

    /* Per-cycle random values, so a respawned strand is a different strand. */
    const cs = seed + cycle * 977;
    const length = 18 + hash(cs + 3) * 62;
    const baseAngle = 0.35 + hash(cs + 4) * 2.44;
    /* Sway rates spread wide so the strands never move in step. */
    const swayRate = 0.12 + hash(cs + 5) * 0.55;
    const swayAmp = 0.05 + hash(cs + 6) * 0.16;
    const angle = baseAngle + Math.sin(t * swayRate + hash(cs + 7) * 6.28) * swayAmp;
    /* Opacity varies per strand: a web is denser where more silk crosses. */
    const peak = (40 + Math.floor(hash(cs + 8) * 60)) / 255;

    /* Fade in at the start of the cycle, out at the end, so a strand never pops
     * into or out of existence. */
    let fade = 1;
    if (cyclePos < STRAND_FADE_IN) fade = cyclePos / STRAND_FADE_IN;
    else if (lifeMax - cyclePos < STRAND_FADE_OUT) fade = (lifeMax - cyclePos) / STRAND_FADE_OUT;
    fade = Math.max(0, Math.min(1, fade));

    out.push({ ax, ay, length, angle, alpha: peak * fade });
  }
  return out;
}

/* Decor and gift particles.
 *
 * A FIXED POOL, not one emitter per item: the tank can hold twelve decor slots
 * plus a gift, and per-item emitters would make the cost scale with how much the
 * player has bought. The pool is reassigned to whichever items are on screen, so
 * the cost is constant whether the tank is empty or full.
 *
 * The style is derived from the item's own kind, so nothing has to be authored
 * per decor entry and no artwork is needed -- a particle is a small tinted dot.
 * That is what makes this cheap in both compute and tokens.
 */
export const PARTICLE_COUNT = 48;

export const EMIT = { NONE: 0, SPORE: 1, GLINT: 2, DUST: 3 };

/* Mirrors ambient_emit_for_decor() in ambient.c. Keyed on the DECOR_KINDS `key`
 * rather than the display name, so it cannot drift when a name is reworded. */
const SPORE_KINDS = new Set([
  'fern', 'moss_clump', 'succulent', 'flower_white', 'flower_pink', 'mushroom',
  'fern_pot', 'mushroom_pair', 'flower_planter', 'moss_stone', 'leaf_umbrella',
  'fairy_ring', 'foxglove_lantern', 'cycad', 'bonsai', 'bamboo_fountain',
  'climb_vine', 'climb_ivy',
  /* Organic odds and ends that would also shed something. */
  'leaf_pile', 'seed_pod', 'rose_urn', 'dinosaur_egg', 'fairy_bridge',
]);
const GLINT_KINDS = new Set([
  'crystal', 'crystal_cluster', 'dew_pool', 'shell', 'shell_cove', 'lantern_cap',
  'globe', 'tea_cup', 'thimble', 'potion_rack', 'amber_fossil', 'neon_sign',
  'holo_terminal', 'solar_array', 'sun_obelisk', 'stone_lantern',
  /* Anything with a surface that would catch a highlight. */
  'web_hammock', 'silk_tent', 'pixie_door', 'spool', 'cable_coil',
  'moon_rover', 'oxygen_pod', 'treasure_chest',
]);
const DUST_KINDS = new Set([
  'rock_small', 'rock_large', 'pebbles', 'log', 'bark', 'twig', 'bone',
  'driftwood_arch', 'twig_ladder', 'bone_arch', 'fossil', 'stone_bridge',
  'gargoyle', 'iron_gate', 'book_stack', 'broken_column', 'glyph_tablet',
  'climb_branch', 'climb_root', 'climb_boulder', 'climb_driftwood',
  /* Heavy, dusty, or worked-stone objects. */
  'acorn_hide', 'quill_desk', 'cauldron', 'broom', 'wizard_tower',
  'dragon_statue',
]);

export function emitForDecorKey(key) {
  if (SPORE_KINDS.has(key)) return EMIT.SPORE;
  if (GLINT_KINDS.has(key)) return EMIT.GLINT;
  if (DUST_KINDS.has(key)) return EMIT.DUST;
  return EMIT.NONE;
}

/* Gifts are the reward objects, so they all sparkle. */
export function emitForGift() {
  return EMIT.GLINT;
}

/* Live particles for the current frame. `items` is [{x, y, style}]. The pool is
 * deterministic from the clock, so the browser and the device read the same way
 * and a test can assert on it without a random source. */
export function particlesAt(nowMs, items) {
  const emitters = items.filter(it => it.style !== EMIT.NONE);
  if (!emitters.length) return [];
  const out = [];
  for (let i = 0; i < PARTICLE_COUNT; i++) {
    /* Each pool slot runs its own cycle, offset by its own phase, so spawns are
     * spread out rather than arriving in a burst. The cycle is the particle's
     * own life plus a gap, so a slot is idle for a while between particles --
     * which is what makes the effect read as occasional rather than as a
     * fountain. */
    const seed = i * 211 + 37;
    const life = 2.0 + hash(seed + 1) * 1.6;          /* seconds */
    const gap = 0.15 + hash(seed + 2) * 0.35;         /* seconds idle */
    const period = life + gap;
    const phase = hash(seed) * period;
    const cycle = Math.floor((nowMs / 1000 + phase) / period);
    const sinceSpawn = (nowMs / 1000 + phase) % period;
    if (sinceSpawn >= life) continue;                 /* idle between particles */
    const t = sinceSpawn / life;                      /* 0..1 through its life */
    const emitter = emitters[Math.floor(hash(seed + cycle) * emitters.length)];
    if (!emitter) continue;
    const jitterX = (hash(seed + cycle + 2) - 0.5) * 18;
    const jitterY = (hash(seed + cycle + 3) - 0.5) * 10;
    const rise = emitter.style === EMIT.DUST ? 1 : -1;
    const travel = t * life * (emitter.style === EMIT.GLINT ? 1.5 : 4.5);
    out.push({
      x: Math.round(emitter.x + jitterX + (hash(seed + cycle + 4) - 0.5) * 6 * t),
      y: Math.round(emitter.y + jitterY + rise * travel),
      size: emitter.style === EMIT.DUST ? 2 : 3,
      /* Fade over the last third of the life, so a particle never pops out of
       * existence. Mirrors the device's opacity ramp. */
      alpha: t > 0.66 ? (1 - t) / 0.34 : 1,
      style: emitter.style,
    });
  }
  return out;
}
