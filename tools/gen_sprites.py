#!/usr/bin/env python3
"""
Spidarium sprite generator.

Draws sprites, themed terrain and imports the approved moss/Crystal pairs.
Fixed geometry/seeds and checked source images produce identical pixels.
Background and decor materials are defined alongside their drawing helpers.

Usage:  python tools/gen_sprites.py
"""

import colorsys
import hashlib
import io
import math
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("ERROR: Pillow is required. Install it with: pip install Pillow")

from asset_build import APPROVED_CRYSTAL_PIXEL_SHA256, APPROVED_CRYSTAL_BORDER_SOURCE_SHA256
import terrain_art
import core_decor_art
import furnishing_art
import small_art

# ---------------------------------------------------------------------------
# Tunables — retune the art here without touching the drawing code.
# ---------------------------------------------------------------------------
PALETTE = {
    "prey_body":  (0x6B, 0x5A, 0x48, 255),
    "prey_dark":  (0x3F, 0x35, 0x2A, 255),
    "prey_wing":  (0x8A, 0x84, 0x78, 210),
    "prey_leg":   (0x2E, 0x27, 0x1F, 255),
    "hunger":     (0xF5, 0xB3, 0x42, 255),
    "mood":       (0x58, 0xD6, 0x8D, 255),
    "energy":     (0x7C, 0x9C, 0xFF, 255),
    "hygiene":    (0x4F, 0xD1, 0xC5, 255),
    "white":      (0xFF, 0xFF, 0xFF, 255),
    "dim":        (0x8B, 0x93, 0xA7, 255),
}

GEOMETRY = {
    "prey_canvas":   48,
    "prey_centre":   (24.0, 24.0),
    "ui_canvas":     48,
}

# Facing direction in screen radians (+x right, +y down).
SPIDER_VIEWS = [("n", -math.pi / 2.0), ("e", 0.0), ("s", math.pi / 2.0)]

SPIDER_ANIMS = [
    ("walk",   4),
    ("idle",   2),
    ("eat",    2),
    ("sleep",  1),
    ("pounce", 2),
    ("molt",   2),
]

PREY_SPECIES = ["fly", "cricket", "moth", "beetle",
                "roach", "mealworm", "fruitfly", "waxworm"]
PREY_ANIMS = [("walk", 2), ("flee", 2), ("dead", 1)]

UI_ICONS = [
    "feed", "clean", "play", "sleep", "decor", "status", "back",
    "hunger", "mood", "energy", "hygiene", "xp", "lock",
    "daily", "treats", "backgrounds", "achievements", "prey", "settings",
    "wifi", "sd", "about", "battery", "display", "sound", "auto_feed",
    "screenshot", "reset", "volume", "music",
]

ACHIEVEMENT_NAMES = [
    "first_meal", "first_pet", "first_clean", "first_play", "first_hunt",
    "first_molt", "ten_meals", "adult", "level_5", "survived_sick",
    "first_perfect",
]
ACHIEVEMENT_COLORS = {
    "first_meal": 0xE9A44F, "first_pet": 0xE781A5, "first_clean": 0x68C8C0,
    "first_play": 0x72C58B, "first_hunt": 0xD5634D, "first_molt": 0xC6B9DE,
    "ten_meals": 0xD8AE43, "adult": 0xA8C6C7, "level_5": 0x7C9CFF,
    "survived_sick": 0x58D68D, "first_perfect": 0xA7D8E0,
}

GIFT_NAMES = [
    "pebble", "leaf", "twig", "acorn", "moss", "sap", "flower", "crystal",
    "amber", "coral", "silk", "bone", "pearl", "ruby", "sapphire", "emerald",
    "diamond", "crown",
    "fairy_cottage", "moon_bed", "reading_nook", "teapot_hide", "witch_hat",
    "fern_nest", "neon_den", "star_capsule", "dragon_roost", "sun_temple",
    "zen_pavilion", "petal_swing", "velvet_throne", "scholar_perch",
    "potion_hide", "fossil_cradle", "holo_garden", "torii_retreat",
]

DECOR_NAMES = ["rock_0", "rock_1", "plant_0", "plant_1", "wood_log"]

# Placeable decor. These are the props the player buys and arranges, so they
# carry the 16-bit SNES-era look: a dark outline, a mid body, a lit top-left
# face and a shadowed bottom-right face, at 32x32 so they read at panel scale.
DECOR_ITEM_NAMES = [
    "rock_small", "rock_large", "pebbles", "log", "bark", "fern", "moss_clump",
    "succulent", "flower_white", "flower_pink", "mushroom", "shell", "crystal",
    "leaf_pile", "twig", "bone",
    "fern_pot", "acorn_hide", "driftwood_arch", "web_hammock", "dew_pool",
    "mushroom_pair", "flower_planter", "crystal_cluster", "shell_cove",
    "twig_ladder", "moss_stone", "seed_pod", "silk_tent", "leaf_umbrella",
    "bone_arch", "fossil", "lantern_cap", "stone_bridge",
    "fairy_ring", "foxglove_lantern", "pixie_door", "gargoyle", "iron_gate",
    "rose_urn", "book_stack", "globe", "quill_desk", "tea_cup", "spool",
    "thimble", "cauldron", "broom", "potion_rack", "amber_fossil", "cycad",
    "dinosaur_egg", "neon_sign", "holo_terminal", "cable_coil", "moon_rover",
    "solar_array", "oxygen_pod", "wizard_tower", "dragon_statue",
    "treasure_chest", "sun_obelisk", "glyph_tablet", "broken_column",
    "stone_lantern", "bamboo_fountain", "bonsai", "fairy_bridge",
]
BACKGROUND_NAMES = [
    "cork", "moss", "leaf_litter", "sand", "fern", "orchid", "woodland", "minimal",
    "mushroom", "gothic", "seashell", "succulent", "willow", "desert", "crystal",
    "cemetery",
    "house", "dark_cave", "night", "autumn", "snow", "pond", "ruins", "marsh",
    "fairy_glade", "moonflower_grove", "cathedral", "rose_courtyard",
    "library", "observatory", "kitchen", "sewing_room", "witch_garden",
    "moon_altar", "primeval_jungle", "amber_basin", "neon_alley",
    "circuit_garden", "lunar_base", "starship", "enchanted_court",
    "dragon_heath", "lost_city", "sunken_temple", "zen_garden",
    "bamboo_grove", "sakura_courtyard", "cloud_sanctuary",
]

TREAT_NAMES = [
    "hunger", "mood", "energy", "dew", "pollen", "silk",
    "moon", "amber", "neon", "star", "lotus", "rune",
]

GIFT_COLORS = {
    "pebble": 0x8FA6A0, "leaf": 0x78A875, "twig": 0xA8794E,
    "acorn": 0xC59A58, "moss": 0x708C45, "sap": 0xD5A34F,
    "flower": 0xE781A5, "crystal": 0x60BBC1, "amber": 0xE9983E,
    "coral": 0xE87579, "silk": 0xC6B9DE, "bone": 0xD8CFB0,
    "pearl": 0xEBCED7, "ruby": 0xC8435C, "sapphire": 0x467FC1,
    "emerald": 0x4D9A69, "diamond": 0xA7D8E0, "crown": 0xD8AE43,
}

PREY_COLORS = {
    "fly": (0xC78B48, 0x623D2D, 0xE8D7B3),
    "cricket": (0xA9A05A, 0x575032, 0xD8C98A),
    "moth": (0xB6A08E, 0x62544B, 0xE1D2BB),
    "beetle": (0x416D68, 0x263F40, 0x8DB8A0),
    "roach": (0x98613D, 0x513423, 0xC59558),
    "mealworm": (0xCBA866, 0x795338, 0xF0D399),
    "fruitfly": (0xD2AA70, 0x67533F, 0xDDDCC6),
    "waxworm": (0xEADCB2, 0xAB9471, 0xFFF0D4),
}

# ---------------------------------------------------------------------------
# Spider — 96x96, body centre pinned at (48, 48)
# ---------------------------------------------------------------------------
# The device does not blit a spider sprite sheet: main/cspider.c renders the
# creature procedurally into a 96x96 RGB565A8 canvas every frame. These PNGs are
# a port of that renderer, so the published artwork shows the spider the panel
# actually draws. Geometry, palette, draw order and RGB565 quantisation below
# are copied from cspider.c -- keep the two in step.
SPIDER_CANVAS = 96
SPIDER_CENTRE = 48

ABD_FWD, ABD_R = -13.0, 13
CEPH_FWD, CEPH_R = 8.0, 11
EYE_FWD, EYE_DX, EYE_R, PUPIL_R, EYE_SMALL_R = 6.0, 5.5, 7, 4, 2
EYE_MID_FWD, EYE_MID_DX = -4.0, 8.0
EYE_REAR_FWD, EYE_REAR_DX = -8.0, 4.0
FANG_LEN, FANG_HALF_W, FANG_FWD, FANG_DX = 4.5, 3.0, 15.5, 4.0
FUZZ_COUNT, FUZZ_OUT, FUZZ_R = 14, 2.0, 3
LEG_LEN, LEG_SEG1, LEG_SEG2, BODY_R, LEG_WIDTH = 22.0, 0.58, 0.72, 16, 3
NUM_LEGS = 8

# A fresh save uses the charcoal body with black pupils, grey legs and white
# fangs, so that is the palette exported here.
COL_BASE = 0x2B3138
COL_PUPIL = 0x000000
COL_LEG = 0x8A94A6
COL_FANG = 0xF7F9FF
COL_SECONDARY = 0x6B7280
COL_EYE = 0xF7F9FF
COL_GLINT = 0xFFFFFF


def _rgb565(rgb):
    """Quantise to RGB565 and expand back, matching the panel's colour depth."""
    r = (rgb >> 19) & 0x1F
    g = (rgb >> 10) & 0x3F
    b = (rgb >> 3) & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _f32(x):
    """Round to IEEE-754 binary32.

    cspider.c computes the spider in `float`, not `double`. The difference is
    not academic: cosf(PI_F / 2) is -4.37e-8 while cos(pi / 2) is +6.12e-17, so
    a coordinate that lands exactly on an integer truncates one pixel apart
    between the two. Emulating binary32 keeps this export pixel-identical to
    the panel instead of merely close.
    """
    return struct.unpack("f", struct.pack("f", float(x)))[0]


def _f32_sin(x):
    return _f32(math.sin(_f32(x)))


def _f32_cos(x):
    return _f32(math.cos(_f32(x)))


# cspider.c uses PI_F, a binary32 approximation of pi, not math.pi.
PI_F = _f32(3.14159265)


def _lighten(rgb, divisor):
    r, g, b = (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF
    r += (255 - r) // divisor
    g += (255 - g) // divisor
    b += (255 - b) // divisor
    return (r << 16) | (g << 8) | b


def _leg_pose(k, side, phase, walking, emote, now):
    """Port of leg_pose() in cspider.c: (hf, hr, kf, kr, ff, fr).

    Every intermediate is rounded to binary32 because the device computes in
    `float`. See _f32() for why that changes pixels.
    """
    base = _f32(side * _f32(_f32(PI_F * _f32(0.5)) + _f32(_f32(k - _f32(1.5)) * _f32(0.42))))
    gait = _f32(_f32(phase + _f32(k * _f32(PI_F * _f32(0.5)))) +
                (_f32(0.0) if side < 0 else PI_F))
    lead = _f32(1.55) if k == 0 else _f32(1.15) if k == 1 else _f32(1.0)
    swing = _f32(_f32(_f32(0.42) * lead) * _f32_sin(gait)) if walking else _f32(0.0)
    bend = _f32(_f32(0.55) + (_f32(_f32(_f32(0.30) * lead) *
                                  _f32_sin(_f32(gait + _f32(PI_F * _f32(0.5)))))
                             if walking else _f32(0.0)))
    wave = _f32_sin(_f32(_f32(now % 1200) * _f32(_f32(2 * PI_F) / _f32(1200))))
    if emote == "happy":
        if k == 0:
            swing = _f32(swing + _f32(_f32(1.10) * wave))
            bend = _f32(bend - _f32(_f32(0.55) + _f32(_f32(0.35) * wave)))
    elif emote == "sad":
        bend = _f32(bend + _f32(0.60))
        swing = _f32(swing * _f32(0.35))
    elif emote == "alert":
        bend = _f32(bend - _f32(_f32(0.45) + _f32(_f32(0.10) * wave)))
        swing = _f32(swing * _f32(0.25))
    elif emote == "sleepy":
        bend = _f32(bend - _f32(_f32(0.30) + _f32(_f32(0.05) * wave)))
        swing = _f32(swing * _f32(0.15))
    angle = max(_f32(0.15), min(_f32(1.25), _f32(swing + bend)))
    dir1 = _f32(base + _f32(side * angle))
    dir2 = _f32(base - _f32(side * angle))
    hf = _f32(BODY_R * _f32_cos(base))
    hr = _f32(BODY_R * _f32_sin(base))
    kf = _f32(hf + _f32(_f32(LEG_LEN * LEG_SEG1) * _f32_cos(dir1)))
    kr = _f32(hr + _f32(_f32(LEG_LEN * LEG_SEG1) * _f32_sin(dir1)))
    ff = _f32(kf + _f32(_f32(LEG_LEN * LEG_SEG2) * _f32_cos(dir2)))
    fr = _f32(kr + _f32(_f32(LEG_LEN * LEG_SEG2) * _f32_sin(dir2)))
    return hf, hr, kf, kr, ff, fr


def _idiv(a, b):
    """C integer division: truncates toward zero, unlike Python's //."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


class _Canvas:
    """The 96x96 buffer cspider.c writes into, with the same primitives."""

    def __init__(self):
        self.pixels = [[None] * SPIDER_CANVAS for _ in range(SPIDER_CANVAS)]

    def _set(self, x, y, colour):
        if 0 <= x < SPIDER_CANVAS and 0 <= y < SPIDER_CANVAS:
            self.pixels[y][x] = colour

    def circle(self, cx, cy, r, rgb):
        if r <= 0:
            return
        colour = _rgb565(rgb)
        r2 = r * r
        for dy in range(-r, r + 1):
            y = cy + dy
            if not 0 <= y < SPIDER_CANVAS:
                continue
            w = int(math.sqrt(r2 - dy * dy))
            for x in range(max(0, cx - w), min(SPIDER_CANVAS - 1, cx + w) + 1):
                self.pixels[y][x] = colour

    def line(self, x1, y1, x2, y2, width, rgb):
        colour = _rgb565(rgb)
        dx, dy = x2 - x1, y2 - y1
        steps = max(abs(dx), abs(dy))
        if steps <= 0:
            self._set(x1, y1, colour)
            return
        half = width // 2
        for i in range(steps + 1):
            x = x1 + _idiv(dx * i, steps)
            y = y1 + _idiv(dy * i, steps)
            for oy in range(-half, half + 1):
                for ox in range(-half, half + 1):
                    if ox * ox + oy * oy <= half * half + half:
                        self._set(x + ox, y + oy, colour)

    def triangle(self, x0, y0, x1, y1, x2, y2, rgb):
        colour = _rgb565(rgb)
        ex, ey = (x0, x1, x2), (y0, y1, y2)
        miny = max(0, min(ey))
        maxy = min(SPIDER_CANVAS - 1, max(ey))
        for y in range(miny, maxy + 1):
            lo, hi = SPIDER_CANVAS, -1
            for e in range(3):
                ax, ay = ex[e], ey[e]
                bx, by = ex[(e + 1) % 3], ey[(e + 1) % 3]
                if ay == by:
                    if y == ay:
                        lo, hi = min(lo, ax, bx), max(hi, ax, bx)
                    continue
                if (y < ay and y < by) or (y > ay and y > by):
                    continue
                x = ax + _idiv((bx - ax) * (y - ay), by - ay)
                lo, hi = min(lo, x), max(hi, x)
            if hi < lo:
                continue
            for x in range(max(0, lo), min(SPIDER_CANVAS - 1, hi) + 1):
                self._set(x, y, colour)


def _spider_emote(anim):
    """The emote ui_hud.c would pick for this state."""
    if anim == "sleep":
        return "sleepy"
    if anim == "eat":
        return "happy"
    return "none"


def draw_spider(theta, anim, frame):
    canvas = _Canvas()
    heading = _f32(theta)

    sx, sy = _f32(1.0), _f32(1.0)
    if anim == "pounce":
        sx, sy = _f32(1.17), _f32(0.82)
    elif anim == "sleep":
        sx, sy = _f32(1.05), _f32(0.95)

    asleep = anim == "sleep"
    emote = _spider_emote(anim)
    walking = anim == "walk"
    phase = _f32(_f32(frame / 4.0) * _f32(2.0 * PI_F)) if walking else _f32(0.0)
    now = frame * 110

    col_base = COL_BASE
    col_mid = _lighten(col_base, 4)
    col_hi = _lighten(col_mid, 3)
    col_fuzz = col_mid

    def to_canvas(fwd, right, sf=1.0, sr=1.0):
        """Port of to_canvas() in cspider.c, in binary32."""
        fx, fy = _f32_cos(heading), _f32_sin(heading)
        rx, ry = _f32(-fy), fx
        f = _f32(_f32(fwd) * _f32(sf))
        r = _f32(_f32(right) * _f32(sr))
        return (SPIDER_CENTRE + int(_f32(_f32(fx * f) + _f32(rx * r))),
                SPIDER_CENTRE + int(_f32(_f32(fy * f) + _f32(ry * r))))

    # --- legs, drawn first so the body covers the hips --------------------
    for i in range(NUM_LEGS):
        side = -1 if i < 4 else 1
        hf, hr, kf, kr, ff, fr = _leg_pose(i & 3, side, phase, walking, emote, now)
        hx, hy = to_canvas(hf, hr)
        kx, ky = to_canvas(kf, kr)
        tx, ty = to_canvas(ff, fr)
        canvas.line(hx, hy, kx, ky, LEG_WIDTH, COL_LEG)
        canvas.line(kx, ky, tx, ty, LEG_WIDTH, COL_LEG)

    # --- abdomen ----------------------------------------------------------
    canvas.circle(*to_canvas(ABD_FWD, 0.0, sx, sy), ABD_R, col_base)
    canvas.circle(*to_canvas(ABD_FWD - 1.5, -1.5, sx, sy), ABD_R - 3, col_mid)
    canvas.circle(*to_canvas(ABD_FWD - 3.0, -3.0, sx, sy), ABD_R - 7, col_hi)
    for i in range(FUZZ_COUNT):
        a = _f32(_f32(_f32(i) * _f32(2.0 * PI_F)) / _f32(FUZZ_COUNT))
        canvas.circle(*to_canvas(_f32(ABD_FWD + _f32(_f32_cos(a) * _f32(ABD_R + FUZZ_OUT))),
                                 _f32(_f32_sin(a) * _f32(ABD_R + FUZZ_OUT)), sx, sy),
                      FUZZ_R - (i % 2), col_fuzz)

    # --- cephalothorax ----------------------------------------------------
    canvas.circle(*to_canvas(CEPH_FWD, 0.0, sx, sy), CEPH_R, col_base)
    canvas.circle(*to_canvas(CEPH_FWD - 1.5, -1.5, sx, sy), CEPH_R - 3, col_mid)
    canvas.circle(*to_canvas(CEPH_FWD - 3.0, -3.0, sx, sy), CEPH_R - 6, col_hi)
    for i in range(FUZZ_COUNT):
        a = _f32(_f32(_f32(i) * _f32(2.0 * PI_F)) / _f32(FUZZ_COUNT))
        canvas.circle(*to_canvas(_f32(CEPH_FWD + _f32(_f32_cos(a) * _f32(CEPH_R + FUZZ_OUT))),
                                 _f32(_f32_sin(a) * _f32(CEPH_R + FUZZ_OUT)), sx, sy),
                      FUZZ_R - (i % 2), col_fuzz)

    # --- secondary eyes ---------------------------------------------------
    for fwd, dx in ((EYE_MID_FWD, EYE_MID_DX), (EYE_REAR_FWD, EYE_REAR_DX)):
        for side in (-1, 1):
            canvas.circle(*to_canvas(_f32(CEPH_FWD + fwd), _f32(side * dx), sx, sy),
                          EYE_SMALL_R, COL_SECONDARY)

    # --- primary eyes -----------------------------------------------------
    col_eye = 0x000000 if asleep else COL_EYE
    for side in (-1, 1):
        canvas.circle(*to_canvas(_f32(CEPH_FWD + EYE_FWD), _f32(side * EYE_DX), sx, sy),
                      EYE_R, col_eye)

    if not asleep:
        for side in (-1, 1):
            canvas.circle(*to_canvas(_f32(CEPH_FWD + EYE_FWD + 1.0),
                                     _f32(side * EYE_DX), sx, sy),
                          PUPIL_R, COL_PUPIL)
            canvas.circle(*to_canvas(_f32(CEPH_FWD + EYE_FWD + 2.0),
                                     _f32(_f32(side * EYE_DX) - 2.0), sx, sy),
                          1, COL_GLINT)
        for side in (-1.0, 1.0):
            bx0, by0 = to_canvas(_f32(CEPH_FWD + FANG_FWD - _f32(FANG_LEN * 0.5)),
                                 _f32(_f32(side * FANG_DX) - FANG_HALF_W), sx, sy)
            bx1, by1 = to_canvas(_f32(CEPH_FWD + FANG_FWD - _f32(FANG_LEN * 0.5)),
                                 _f32(_f32(side * FANG_DX) + FANG_HALF_W), sx, sy)
            ax, ay = to_canvas(_f32(CEPH_FWD + FANG_FWD + _f32(FANG_LEN * 0.5)),
                               _f32(side * FANG_DX), sx, sy)
            canvas.triangle(bx0, by0, bx1, by1, ax, ay, COL_FANG)

    img = Image.new("RGBA", (SPIDER_CANVAS, SPIDER_CANVAS), (0, 0, 0, 0))
    pixels = img.load()
    for y in range(SPIDER_CANVAS):
        for x in range(SPIDER_CANVAS):
            colour = canvas.pixels[y][x]
            if colour is not None:
                pixels[x, y] = colour + (255,)
    return img


# ---------------------------------------------------------------------------
# Non-spider art — authored on integer half-resolution grids, never filtered.
# ---------------------------------------------------------------------------
INK = (30, 35, 36, 255)
CREAM = (255, 239, 193, 255)
GREEN = (98, 161, 85, 255)


def _rgba(value):
    return tuple((value >> shift) & 255 for shift in (16, 8, 0)) + (255,)


def _pixel_canvas(width, height):
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    return image, ImageDraw.Draw(image)


def _enlarge(image):
    return image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST)


def _shape(draw, points, color):
    draw.polygon(points, fill=color, outline=INK, width=1)


def _octagon(draw, box, color, cut=2):
    x0, y0, x1, y1 = box
    _shape(draw, [(x0 + cut, y0), (x1 - cut, y0), (x1, y0 + cut),
                  (x1, y1 - cut), (x1 - cut, y1), (x0 + cut, y1),
                  (x0, y1 - cut), (x0, y0 + cut)], color)


def _shade_materials(image, materials):
    """Shade existing pixel clusters without changing their silhouette or ink."""
    source = image.copy()
    pixels = image.load()
    for material in dict.fromkeys(materials):
        points = [(x, y) for y in range(image.height) for x in range(image.width)
                  if source.getpixel((x, y)) == material]
        if not points:
            continue
        first = min(x + y for x, y in points)
        span = max(1, max(x + y for x, y in points) - first)
        light = tuple(min(255, v + (255 - v) // 3 + shift)
                      for v, shift in zip(material[:3], (5, 3, 0))) + (255,)
        dark = tuple(min(255, v * 3 // 4 + shift)
                     for v, shift in zip(material[:3], (0, 2, 8))) + (255,)
        for x, y in points:
            position = (x + y - first) * 10
            if position <= span * 2:
                pixels[x, y] = light
            elif position >= span * 8:
                pixels[x, y] = dark
    return image


def _draw_larva(species, anim, frame):
    """Fixed head/thorax with a small abdominal wave, not a zigzagging body."""
    img, d = _pixel_canvas(24, 24)
    body, dark, highlight = map(_rgba, PREY_COLORS[species])
    plump = species == "waxworm"
    radius = 3 if plump else 2
    phase = 1 if frame else -1
    for x in (14, 16, 18):
        for side in (-1, 1):
            d.line([(x, 12 + side * radius),
                    (x + phase, 12 + side * (radius + 2))], fill=INK)
    if plump:
        for x in (5, 8, 11):
            for side in (-1, 1):
                d.rectangle((x, 12 + side * 4, x + 1, 12 + side * 4), fill=dark)
    for x in range(4, 18, 2):
        y = 12 + (phase if x < 10 else 0)
        segment_radius = 2 if x < 8 else radius
        _octagon(d, (x - 1, y - segment_radius, x + 2, y + segment_radius), body, 1)
        d.line([(x + 1, y - segment_radius + 1), (x + 1, y + segment_radius - 1)], fill=dark)
        d.point((x, y - segment_radius + 1), fill=highlight)
    _octagon(d, (17, 10, 20, 14), dark, 1)
    d.point((18, 10), fill=body)
    d.point((19, 11), fill=CREAM)
    return _enlarge(small_art.finish(_shade_materials(img, (body, dark, highlight)), species, "prey"))


def prey_leg_paths(species, frame):
    """Six thoracic legs; alternating left/right tripod feet, fixed roots."""
    paths = []
    phase = 1 if frame else -1
    for side in (-1, 1):
        for pair, root in enumerate((12, 14, 16)):
            stride = phase * (1 if (pair + (side > 0)) % 2 else -1)
            knee_x = root + (-3, -1, 2)[pair]
            foot_x = root + (-5, -1, 4)[pair] + stride
            if species == "cricket" and pair == 0:
                knee_x, foot_x = 6, 4 + stride
            paths.append([(root, 12 + side),
                          (knee_x, 12 + side * (6 if species == "cricket" and pair == 0 else 4)),
                          (foot_x, 12 + side * 7)])
    return paths


def draw_prey(species, anim, frame):
    """Thorax-attached legs, stable bodies and species-specific wing poses."""
    if anim == "dead":
        return draw_splatter(species)
    if species in ("mealworm", "waxworm"):
        return _draw_larva(species, anim, frame)
    img, d = _pixel_canvas(24, 24)
    body, dark, wing = map(_rgba, PREY_COLORS[species])
    for index, path in enumerate(prey_leg_paths(species, frame)):
        d.line(path, fill=INK)
        if species == "cricket" and index % 3 == 0:
            d.line(path[:2], fill=dark, width=2)
    if species in ("fly", "fruitfly", "moth"):
        spread = (1 if frame else 0) + (1 if anim == "flee" else 0)
        for side in (-1, 1):
            extent = (7 if species == "moth" else 4) + spread
            y = 12 + side * extent
            _shape(d, [(14, 12), (9, y), (5 if species == "moth" else 8, y),
                       (6, 12 + side * 3), (11, 12 + side)], wing)
            d.line([(9, y), (12, 12 + side * 2)], fill=dark)
            d.point((9, y - side), fill=CREAM)
    if species in ("beetle", "roach"):
        _octagon(d, (4, 8, 14, 16), body, 2)
        d.line([(5, 12), (13, 12)], fill=dark)
        d.line([(6, 9), (11, 9)], fill=wing)
        d.line([(6, 15), (11, 15)], fill=dark)
    else:
        _octagon(d, (6, 10, 13, 14), body, 1)
        d.line([(7, 11), (12, 11)], fill=wing)
        d.line([(8, 13), (12, 13)], fill=dark)
    _octagon(d, (12, 10, 16, 14), dark, 1)
    d.line([(13, 10), (15, 10)], fill=wing)
    _octagon(d, (16, 10, 19, 14), body, 1)
    d.point((18, 11), fill=CREAM)
    if species in ("fly", "fruitfly"):
        d.point((18, 10), fill=_rgba(0xD5634D))
        d.point((18, 14), fill=_rgba(0xD5634D))
    for side in (-1, 1):
        reach = 4 if species in ("roach", "cricket") else 2
        d.line([(18, 12 + side), (21, 12 + side * reach),
                (22, 12 + side * (reach + 1))], fill=INK)
    return _enlarge(small_art.finish(_shade_materials(img, (body, dark, wing)), species, "prey"))


def draw_splatter(species):
    """An asymmetric puddle, scattered droplets and tiny species-colored shards."""
    img, d = _pixel_canvas(24, 24)
    body, dark, highlight = map(_rgba, PREY_COLORS[species])
    variant = PREY_SPECIES.index(species)
    d.ellipse((5, 10, 18, 16), fill=body, outline=dark)
    d.ellipse((3, 12, 10, 17 + variant % 2), fill=body)
    d.ellipse((10, 8 - variant % 2, 15 + variant % 3, 15), fill=body)
    d.polygon([(6, 13), (4, 10 - variant % 3), (8, 11), (10, 10),
               (15, 12), (19 + variant % 2, 13), (17, 15),
               (15, 18), (12, 16), (8, 17)], fill=body)
    d.line([(6, 15), (8, 16), (10, 15)], fill=dark)
    d.line([(13, 14), (15, 14)], fill=dark)
    d.line([(8, 12), (10, 12)], fill=highlight)
    d.point((11, 10), fill=highlight)
    d.rectangle((2 + variant % 2, 5, 3 + variant % 2, 6), fill=body)
    d.rectangle((20, 8 + variant % 3, 21, 9 + variant % 3), fill=body)
    d.point((18 - variant % 3, 20), fill=body)
    d.point((6 + variant % 4, 20), fill=dark)
    if species in ("fly", "moth", "fruitfly"):
        d.polygon([(5, 8), (7 + variant % 2, 7), (8, 9)], fill=highlight)
    elif species in ("beetle", "roach", "cricket"):
        d.line([(16, 6), (18, 5), (19, 6)], fill=dark)
        d.point((17, 6), fill=highlight)
    else:
        d.rectangle((6, 7, 8, 8), fill=highlight)
        d.point((8, 8), fill=dark)
    body_mid = tuple((body[i] * 2 + dark[i]) // 3 for i in range(3)) + (255,)
    rim = tuple((body[i] * 2 + highlight[i]) // 3 for i in range(3)) + (255,)
    glint = tuple(min(255, v + (255 - v) // 3) for v in highlight[:3]) + (255,)
    d.line([(6, 14), (8, 15), (10, 14)], fill=body_mid)
    d.line([(12, 9), (14, 9), (15, 10)], fill=rim)
    d.line([(7, 11), (8, 10), (10, 10)], fill=rim)
    d.point((9, 10), fill=glint)
    return _enlarge(small_art.finish(img, species, "splatter"))


def draw_achievement(name):
    """Eleven dedicated badge motifs, never recycled HUD or gift sprites."""
    img, d = _pixel_canvas(24, 24)
    accent = _rgba(ACHIEVEMENT_COLORS[name])
    shade = tuple(max(0, value - 48) for value in accent[:3]) + (255,)
    if name == "first_meal":
        _shape(d, [(3, 13), (21, 13), (19, 19), (16, 21), (8, 21), (5, 19)], accent)
        d.line([(6, 15), (18, 15)], fill=CREAM)
        d.line([(8, 19), (16, 19)], fill=shade)
        _octagon(d, (9, 5, 15, 11), shade, 1)
        d.line([(12, 6), (12, 9)], fill=CREAM)
        d.line([(8, 7), (6, 5)], fill=INK)
        d.line([(16, 7), (18, 5)], fill=INK)
    elif name == "first_pet":
        _shape(d, [(3, 15), (6, 12), (9, 14), (13, 14), (16, 12),
                   (19, 13), (16, 18), (11, 21), (6, 21)], accent)
        d.line([(7, 16), (12, 17), (15, 15)], fill=CREAM)
        _shape(d, [(11, 4), (13, 2), (16, 2), (18, 4), (20, 2),
                   (22, 4), (22, 7), (18, 11), (12, 7)], shade)
        d.line([(14, 4), (15, 4)], fill=CREAM)
    elif name == "first_clean":
        _shape(d, [(10, 3), (13, 7), (16, 13), (16, 17), (13, 21),
                   (7, 21), (4, 17), (4, 13), (7, 7)], accent)
        d.line([(7, 13), (7, 16), (9, 18)], fill=CREAM, width=2)
        d.line([(14, 14), (14, 17), (12, 19)], fill=shade)
        d.line([(19, 4), (19, 10)], fill=CREAM)
        d.line([(16, 7), (22, 7)], fill=CREAM)
    elif name == "first_play":
        _shape(d, [(11, 2), (19, 8), (11, 16), (4, 8)], accent)
        d.line([(11, 3), (11, 15)], fill=CREAM)
        d.line([(5, 8), (18, 8)], fill=shade)
        d.line([(11, 16), (13, 19), (9, 22)], fill=INK)
        d.polygon([(12, 17), (16, 16), (16, 19)], fill=shade)
        d.polygon([(10, 20), (6, 19), (6, 22)], fill=accent)
    elif name == "first_hunt":
        _octagon(d, (3, 5, 17, 19), shade, 3)
        _octagon(d, (5, 7, 15, 17), accent, 2)
        d.rectangle((8, 10, 12, 14), fill=INK)
        d.point((10, 12), fill=CREAM)
        d.line([(10, 2), (10, 7)], fill=CREAM)
        d.line([(1, 12), (6, 12)], fill=CREAM)
        d.line([(13, 11), (21, 3)], fill=INK, width=3)
        d.line([(13, 11), (21, 3)], fill=CREAM)
        d.polygon([(21, 2), (21, 7), (17, 3)], fill=accent)
    elif name == "first_molt":
        _shape(d, [(3, 9), (7, 6), (10, 9), (9, 12), (10, 16),
                   (7, 20), (3, 16), (4, 12)], shade)
        _shape(d, [(15, 8), (18, 5), (21, 8), (20, 12), (21, 16),
                   (18, 20), (14, 16), (15, 12)], accent)
        for x, sign in ((4, -1), (20, 1)):
            for y in (10, 13, 16):
                d.line([(x, y), (x + sign * 2, y - 2)], fill=INK)
        d.line([(17, 14), (17, 16)], fill=CREAM)
        d.line([(10, 3), (14, 3)], fill=CREAM)
        d.polygon([(14, 1), (17, 3), (14, 5)], fill=accent)
    elif name == "ten_meals":
        _shape(d, [(2, 15), (22, 15), (19, 21), (5, 21)], accent)
        for x in (6, 12, 18):
            _octagon(d, (x - 2, 11, x + 2, 16), shade, 1)
            d.point((x - 1, 12), fill=CREAM)
        d.line([(6, 4), (8, 2), (8, 8)], fill=CREAM, width=2)
        d.line([(6, 9), (10, 9)], fill=CREAM)
        d.rectangle((13, 2, 18, 9), outline=CREAM, width=2)
        d.line([(6, 19), (18, 19)], fill=CREAM)
    elif name == "adult":
        _shape(d, [(4, 2), (12, 5), (20, 2), (20, 14), (17, 19),
                   (12, 22), (7, 19), (4, 14)], shade)
        for side in (-1, 1):
            for y in (9, 12, 15):
                d.line([(12 + side * 2, y), (12 + side * 6, y - 2),
                        (12 + side * 6, y + 1)], fill=CREAM)
        _octagon(d, (9, 12, 15, 19), accent, 2)
        _octagon(d, (9, 7, 15, 12), accent, 1)
        d.point((10, 9), fill=INK)
        d.point((14, 9), fill=INK)
        d.line([(11, 14), (11, 16)], fill=CREAM)
    elif name == "level_5":
        _shape(d, [(2, 17), (7, 17), (7, 13), (12, 13), (12, 9),
                   (17, 9), (17, 5), (21, 5), (21, 21), (2, 21)], accent)
        d.line([(4, 19), (19, 19)], fill=shade)
        d.line([(3, 4), (8, 4), (8, 7), (3, 7), (3, 10), (8, 10)],
               fill=CREAM, width=2)
        d.polygon([(15, 4), (19, 1), (22, 4)], fill=CREAM)
    elif name == "survived_sick":
        _shape(d, [(7, 2), (16, 2), (16, 7), (21, 7), (21, 16),
                   (16, 16), (16, 21), (7, 21), (7, 16), (2, 16),
                   (2, 7), (7, 7)], shade)
        _shape(d, [(5, 12), (9, 7), (17, 6), (16, 14), (11, 17)], accent)
        d.line([(7, 14), (14, 9)], fill=CREAM)
        d.line([(10, 12), (10, 9)], fill=CREAM)
        d.line([(12, 10), (15, 10)], fill=CREAM)
    elif name == "first_perfect":
        _shape(d, [(9, 3), (15, 3), (18, 7), (12, 15), (6, 7)], accent)
        d.line([(7, 7), (17, 7)], fill=CREAM)
        d.line([(10, 4), (12, 13), (14, 4)], fill=CREAM)
        for side in (-1, 1):
            d.line([(12, 21), (12 + side * 7, 17), (12 + side * 9, 9)], fill=shade)
            for x, y in ((7, 12), (6, 16), (3, 19)):
                xx = 12 + side * x
                d.polygon([(xx, y), (xx - side * 3, y - 2),
                           (xx - side * 2, y + 2)], fill=accent)
        d.rectangle((10, 19, 14, 21), fill=CREAM)
    else:
        raise ValueError(f"Unknown achievement: {name}")
    return _enlarge(small_art.finish(_shade_materials(img, (accent, shade, CREAM)), name, "badge"))


def _pixel_star(d, color):
    _shape(d, [(12, 3), (14, 9), (20, 9), (16, 13), (18, 20),
               (12, 16), (6, 20), (8, 13), (4, 9), (10, 9)], color)
    d.line([(12, 6), (11, 10), (8, 10)], fill=CREAM)


def draw_icon(name):
    """Native 48 px controls, with no translucent badge or runtime downscale."""
    img, d = _pixel_canvas(24, 24)
    accent = _rgba({
        "feed": 0xE9A44F, "clean": 0x68C8C0, "play": 0x72C58B,
        "sleep": 0x91A7E8, "decor": 0x83B985, "status": 0xA8C6C7,
        "back": 0xDDE8E1, "hunger": 0xF5B342, "mood": 0x58D68D,
        "energy": 0x7C9CFF, "hygiene": 0x4FD1C5, "xp": 0xF5B342,
        "lock": 0x8B93A7,
        "daily": 0xE9A44F, "treats": 0xE781A5, "backgrounds": 0x72C58B,
        "achievements": 0xD8AE43, "prey": 0xC59558, "settings": 0xA8C6C7,
        "wifi": 0x68C8C0, "sd": 0x7C9CFF, "about": 0xA7D8E0,
        "battery": 0x72C58B, "display": 0x91A7E8, "sound": 0xC6B9DE,
        "auto_feed": 0xE9A44F, "screenshot": 0x68C8C0, "reset": 0xD5634D,
        "volume": 0x68C8C0, "music": 0xC6B9DE,
    }[name])
    shade = tuple(max(0, v - 48) for v in accent[:3]) + (255,)
    if name == "feed":
        _shape(d, [(4, 12), (20, 12), (18, 19), (16, 21), (8, 21), (6, 19)], accent)
        d.rectangle((5, 12, 19, 14), fill=shade, outline=INK)
        d.line([(8, 17), (9, 19), (15, 19)], fill=CREAM)
        for x, y in ((7, 8), (12, 6), (17, 9)):
            _octagon(d, (x - 1, y - 1, x + 1, y + 1), CREAM, 1)
    elif name == "clean":
        d.line([(16, 3), (8, 13)], fill=INK, width=3)
        d.line([(16, 4), (9, 13)], fill=CREAM)
        _shape(d, [(7, 11), (13, 15), (11, 20), (3, 17)], accent)
        for x in (5, 7, 9):
            d.line([(x, 14), (x - 1, 17)], fill=shade)
        d.line([(19, 13), (19, 17)], fill=CREAM)
        d.line([(17, 15), (21, 15)], fill=CREAM)
    elif name == "play":
        _octagon(d, (4, 4, 20, 20), accent, 4)
        _shape(d, [(6, 6), (10, 5), (10, 10), (5, 10)], CREAM)
        _shape(d, [(14, 14), (19, 14), (18, 18), (14, 19)], CREAM)
        d.line([(5, 15), (9, 15), (15, 9), (15, 5)], fill=shade)
    elif name == "sleep":
        _shape(d, [(12, 3), (8, 3), (5, 6), (3, 10), (3, 15), (6, 19),
                   (10, 21), (15, 21), (19, 18), (13, 18), (9, 15),
                   (8, 11), (9, 7)], accent)
        d.line([(5, 10), (5, 15), (8, 18)], fill=CREAM)
        d.line([(18, 4), (18, 8)], fill=CREAM)
        d.line([(16, 6), (20, 6)], fill=CREAM)
    elif name == "decor":
        d.rectangle((11, 5, 12, 18), fill=shade)
        _shape(d, [(11, 10), (4, 8), (3, 4), (7, 4), (11, 7)], accent)
        _shape(d, [(12, 13), (18, 11), (21, 6), (16, 6), (12, 9)], accent)
        _shape(d, [(6, 17), (18, 17), (16, 22), (8, 22)], shade)
        d.line([(7, 18), (17, 18)], fill=CREAM)
    elif name == "status":
        d.rectangle((5, 5, 19, 21), fill=accent, outline=INK)
        d.rectangle((9, 3, 15, 6), fill=shade, outline=INK)
        d.line([(8, 13), (11, 16), (17, 10)], fill=INK, width=2)
        d.line([(8, 19), (16, 19)], fill=CREAM)
    elif name == "back":
        _shape(d, [(10, 4), (3, 11), (3, 13), (10, 20), (13, 17),
                   (10, 14), (21, 14), (21, 10), (10, 10), (13, 7)], accent)
        d.line([(6, 11), (10, 7)], fill=CREAM)
    elif name == "hunger":
        d.rectangle((5, 4, 9, 10), fill=accent)
        for x in (6, 8):
            d.line([(x, 4), (x, 7)], fill=INK)
        d.rectangle((6, 10, 8, 20), fill=accent, outline=INK)
        _shape(d, [(15, 4), (18, 4), (18, 20), (16, 20),
                   (16, 12), (13, 12), (13, 8)], accent)
        d.line([(16, 5), (16, 10)], fill=CREAM)
    elif name == "mood":
        _octagon(d, (3, 3, 21, 21), accent, 5)
        d.rectangle((7, 8, 8, 10), fill=INK)
        d.rectangle((16, 8, 17, 10), fill=INK)
        d.line([(7, 14), (9, 17), (15, 17), (17, 14)], fill=INK, width=2)
        d.line([(7, 5), (11, 5)], fill=CREAM)
    elif name == "energy":
        _shape(d, [(13, 2), (5, 13), (11, 13), (9, 22),
                   (20, 9), (13, 9), (16, 2)], accent)
        d.line([(13, 4), (9, 10)], fill=CREAM)
    elif name == "hygiene":
        _shape(d, [(12, 2), (10, 6), (5, 12), (4, 16), (6, 20),
                   (9, 22), (15, 22), (18, 20), (20, 16), (19, 12), (14, 6)], accent)
        d.line([(8, 13), (7, 16), (9, 18)], fill=CREAM, width=2)
        d.line([(12, 20), (16, 19)], fill=shade)
    elif name == "xp":
        _pixel_star(d, accent)
    elif name == "daily":
        d.rectangle((4, 5, 20, 20), fill=accent, outline=INK)
        d.rectangle((6, 8, 18, 18), fill=CREAM)
        d.rectangle((7, 3, 9, 8), fill=shade)
        d.rectangle((15, 3, 17, 8), fill=shade)
        d.line([(8, 13), (10, 15), (15, 11)], fill=accent, width=2)
        d.line([(6, 9), (18, 9)], fill=shade)
    elif name == "treats":
        _shape(d, [(4, 8), (7, 5), (10, 8), (14, 8), (17, 5), (20, 8),
                   (20, 16), (17, 19), (14, 16), (10, 16), (7, 19), (4, 16)], accent)
        d.rectangle((8, 9, 16, 15), fill=shade)
        d.line([(8, 8), (11, 11), (13, 11), (16, 8)], fill=CREAM)
        d.rectangle((10, 12, 14, 14), fill=CREAM)
    elif name == "backgrounds":
        d.rectangle((3, 5, 21, 19), fill=shade, outline=INK)
        d.rectangle((5, 7, 19, 17), fill=CREAM)
        _shape(d, [(5, 16), (9, 11), (12, 14), (16, 9), (19, 14), (19, 17), (5, 17)], accent)
        d.ellipse((7, 8, 10, 11), fill=_rgba(0xF5B342))
        d.line([(5, 20), (19, 20)], fill=INK, width=2)
    elif name == "achievements":
        _shape(d, [(6, 4), (18, 4), (18, 11), (16, 15), (14, 17),
                   (14, 20), (18, 20), (18, 22), (6, 22), (6, 20),
                   (10, 20), (10, 17), (8, 15), (6, 11)], accent)
        d.rectangle((8, 6, 16, 10), fill=CREAM)
        d.line([(8, 5), (5, 5), (5, 10), (8, 12)], fill=shade, width=2)
        d.line([(16, 5), (19, 5), (19, 10), (16, 12)], fill=shade, width=2)
        d.line([(11, 13), (13, 13)], fill=CREAM, width=2)
    elif name == "prey":
        _octagon(d, (9, 8, 15, 16), accent, 2)
        d.ellipse((7, 5, 10, 8), fill=shade, outline=INK)
        d.ellipse((14, 5, 17, 8), fill=shade, outline=INK)
        for y in (10, 13, 16):
            d.line([(9, y), (5, y - 2), (3, y - 2)], fill=INK, width=2)
            d.line([(15, y), (19, y - 2), (21, y - 2)], fill=INK, width=2)
            d.line([(9, y), (6, y + 2)], fill=CREAM)
            d.line([(15, y), (18, y + 2)], fill=CREAM)
        d.point((11, 10), fill=CREAM)
        d.point((14, 10), fill=CREAM)
    elif name == "settings":
        _shape(d, [(10, 3), (14, 3), (15, 6), (18, 7), (21, 6), (22, 10),
                   (20, 12), (20, 14), (22, 16), (20, 20), (17, 19),
                   (15, 21), (14, 23), (10, 23), (9, 20), (6, 19),
                   (3, 20), (2, 16), (4, 14), (4, 11), (2, 9), (4, 5),
                   (7, 6), (9, 5)], accent)
        _octagon(d, (7, 7, 17, 17), shade, 3)
        _octagon(d, (10, 10, 14, 14), CREAM, 2)
    elif name == "wifi":
        d.arc((3, 4, 21, 20), 220, 320, fill=INK, width=4)
        d.arc((3, 5, 21, 21), 220, 320, fill=accent, width=2)
        d.arc((7, 8, 17, 19), 220, 320, fill=INK, width=4)
        d.arc((7, 9, 17, 20), 220, 320, fill=CREAM, width=2)
        _octagon(d, (10, 16, 14, 20), shade, 2)
        d.point((11, 17), fill=CREAM)
    elif name == "sd":
        _shape(d, [(6, 3), (15, 3), (20, 8), (20, 21), (4, 21), (4, 5)], accent)
        d.polygon([(15, 3), (15, 8), (20, 8)], fill=CREAM)
        d.rectangle((7, 6, 9, 12), fill=shade)
        d.rectangle((11, 5, 13, 12), fill=CREAM)
        d.rectangle((15, 9, 17, 12), fill=shade)
        d.rectangle((7, 15, 17, 18), fill=_rgba(0x263F5F), outline=INK)
        d.line([(8, 16), (16, 16)], fill=CREAM)
    elif name == "about":
        _octagon(d, (3, 3, 21, 21), accent, 5)
        d.rectangle((11, 10, 13, 17), fill=CREAM)
        d.rectangle((11, 6, 13, 8), fill=shade)
        d.line([(7, 5), (10, 4)], fill=CREAM)
    elif name == "battery":
        d.rectangle((4, 7, 19, 19), fill=accent, outline=INK)
        d.rectangle((19, 10, 21, 16), fill=shade, outline=INK)
        d.rectangle((6, 9, 17, 17), fill=CREAM)
        d.rectangle((7, 10, 11, 16), fill=accent)
        d.rectangle((12, 10, 16, 16), fill=shade)
        d.line([(7, 8), (16, 8)], fill=CREAM)
    elif name == "display":
        d.rectangle((3, 4, 21, 17), fill=accent, outline=INK)
        d.rectangle((5, 6, 19, 15), fill=CREAM)
        _shape(d, [(5, 14), (9, 10), (12, 13), (15, 8), (19, 13), (19, 15), (5, 15)], shade)
        d.rectangle((10, 18, 14, 20), fill=INK)
        d.rectangle((7, 21, 17, 22), fill=shade)
    elif name == "sound":
        _shape(d, [(3, 9), (8, 9), (14, 4), (16, 4), (16, 20),
                   (14, 20), (8, 15), (3, 15)], accent)
        d.line([(5, 10), (8, 10), (13, 6)], fill=CREAM)
        d.arc((11, 7, 20, 17), 300, 60, fill=INK, width=3)
        d.arc((12, 8, 19, 16), 300, 60, fill=CREAM, width=2)
        d.arc((13, 4, 23, 20), 300, 60, fill=shade, width=2)
    elif name == "auto_feed":
        d.rectangle((4, 14, 20, 20), fill=accent, outline=INK)
        d.line([(5, 15), (8, 18), (16, 18), (19, 15)], fill=CREAM)
        d.arc((6, 3, 18, 15), 180, 350, fill=shade, width=3)
        d.polygon([(17, 5), (21, 7), (17, 9)], fill=shade)
        d.ellipse((9, 8, 11, 10), fill=CREAM)
        d.ellipse((13, 7, 15, 9), fill=CREAM)
    elif name == "screenshot":
        d.rectangle((3, 7, 21, 19), fill=accent, outline=INK)
        d.polygon([(7, 7), (9, 4), (15, 4), (17, 7)], fill=shade, outline=INK)
        _octagon(d, (8, 8, 16, 16), INK, 3)
        _octagon(d, (10, 10, 14, 14), CREAM, 2)
        d.rectangle((17, 9, 19, 11), fill=CREAM)
        d.line([(5, 17), (7, 17)], fill=shade)
    elif name == "reset":
        d.arc((4, 4, 20, 20), 35, 315, fill=INK, width=4)
        d.arc((5, 5, 19, 19), 35, 315, fill=accent, width=2)
        d.polygon([(5, 3), (10, 4), (7, 8)], fill=shade)
        d.line([(9, 12), (12, 15), (16, 10)], fill=CREAM, width=2)
    elif name == "volume":
        for x, y in ((5, 8), (12, 15), (19, 6)):
            d.rectangle((x - 1, 3, x + 1, 21), fill=INK)
            d.line([(x, 4), (x, 20)], fill=accent)
            d.rectangle((x - 3, y - 2, x + 3, y + 2), fill=INK)
            d.rectangle((x - 2, y - 1, x + 2, y + 1), fill=shade)
            d.line([(x - 2, y - 1), (x + 2, y - 1)], fill=CREAM)
    elif name == "music":
        d.polygon([(7, 4), (21, 1), (21, 7), (7, 10)], fill=INK)
        d.polygon([(9, 5), (19, 3), (19, 5), (9, 7)], fill=shade)
        for x, y in ((8, 18), (20, 15)):
            d.rectangle((x - 1, 6, x + 1, y), fill=INK)
            d.line([(x, 7), (x, y - 1)], fill=CREAM)
            _octagon(d, (x - 7, y - 2, x + 1, y + 4), accent, 2)
            d.line([(x - 5, y - 1), (x - 2, y - 1)], fill=CREAM)
    else:  # lock
        _octagon(d, (7, 3, 17, 14), shade, 3)
        d.rectangle((10, 6, 14, 12), fill=(0, 0, 0, 0))
        d.rectangle((5, 10, 19, 21), fill=accent, outline=INK)
        d.line([(7, 12), (17, 12)], fill=CREAM)
        d.rectangle((11, 14, 13, 16), fill=INK)
        d.line([(12, 16), (12, 19)], fill=INK)
    return _enlarge(small_art.finish(_shade_materials(img, (accent, shade, CREAM)), name, "icon"))


def draw_gift(name):
    """Furnishings share the terrain/decor material language."""
    if name in GIFT_NAMES[18:]:
        return _draw_catalogue_gift(name)
    img, d = _pixel_canvas(16, 16)
    ink, shadow, grain, wood, cut, light = map(
        _rgba, (0x232332, 0x623C39, 0x875239, 0xAE743D, 0xC99459, 0xEAC58A))
    c = _rgba(GIFT_COLORS[name])
    dark = tuple(v * 3 // 5 for v in c[:3]) + (255,)
    hi = tuple(min(255, v + (255 - v) * 2 // 3) for v in c[:3]) + (255,)
    mid = tuple((c[i] + dark[i]) // 2 for i in range(3)) + (255,)

    def slab(box):
        x0, y0, x1, y1 = box
        d.rectangle(box, fill=ink)
        d.rectangle((x0 + 1, y0 + 1, x1 - 1, y1 - 1), fill=wood)
        d.line([(x0 + 1, y0 + 1), (x1 - 1, y0 + 1)], fill=light)
        d.line([(x0 + 1, y1 - 1), (x1 - 1, y1 - 1)], fill=shadow)
        for x in range(x0 + 3, x1 - 1, 4):
            d.line([(x, y0 + 2), (x - 1, y1 - 2)], fill=grain)
            d.point((x + 1, y0 + 2), fill=cut)

    def gem(cx, top, bottom, width):
        d.polygon([(cx, top), (cx + width, top + 3), (cx + width - 1, bottom),
                   (cx - width + 1, bottom), (cx - width, top + 3)], fill=ink)
        d.polygon([(cx, top + 1), (cx, bottom - 1),
                   (cx - width + 1, top + 3)], fill=c)
        d.polygon([(cx, top + 1), (cx + width - 1, top + 3),
                   (cx + width - 2, bottom - 1), (cx, bottom - 1)], fill=dark)
        d.line([(cx, top + 1), (cx - width + 1, top + 3),
                (cx - 1, top + 4)], fill=hi)
        d.line([(cx + 1, top + 4), (cx + 1, bottom - 1)], fill=mid)

    if name == "pebble":
        _blob(d, 8, 9, 7, 5, c, hi, dark, ink)
        d.polygon([(3, 8), (6, 5), (10, 5), (8, 9)], fill=hi)
        d.polygon([(9, 9), (13, 8), (13, 11), (10, 13)], fill=mid)
        d.line([(4, 7), (6, 6), (9, 6)], fill=light)
        d.point((6, 9), fill=grain)
        d.point((11, 10), fill=shadow)
    elif name in ("leaf", "acorn", "bone", "diamond"):
        body, lit, shade = (c, hi, dark) if name in ("bone", "diamond") else (wood, light, shadow)
        _blob(d, 8, 9, 7, 5, body, lit, shade, ink)
        if name == "leaf":
            for base, tip in (((2, 7), (10, 1)), ((7, 7), (14, 4))):
                _leaf_shape(d, base, tip, 3, c, hi, dark, ink)
        elif name == "acorn":
            _blob(d, 8, 5, 6, 3, grain, cut, shadow, ink)
            for x in (4, 7, 10):
                d.line([(x, 4), (x + 1, 5)], fill=light)
            d.line([(8, 2), (9, 0), (11, 0)], fill=wood)
        elif name == "bone":
            for x in (2, 13):
                d.line([(x, 7), (x, 12)], fill=hi, width=2)
                d.point((x, 9), fill=mid)
        else:
            d.line([(3, 7), (6, 4), (10, 4), (13, 7)], fill=light)
            d.line([(4, 6), (8, 5), (11, 6)], fill=cut)
            d.point((12, 10), fill=wood)
        d.ellipse((4, 7, 11, 14), fill=shadow)
        d.ellipse((5, 8, 10, 14), fill=ink)
        d.line([(5, 14), (10, 14)], fill=grain)
        d.line([(3, 9), (3, 12)], fill=cut)
    elif name == "twig":
        slab((3, 12, 12, 15))
        d.line([(7, 13), (7, 7), (4, 3)], fill=ink, width=4)
        d.line([(8, 9), (12, 5), (13, 2)], fill=ink, width=3)
        d.line([(7, 12), (7, 7), (4, 3)], fill=wood, width=2)
        d.line([(8, 9), (12, 5), (13, 2)], fill=cut)
        d.line([(6, 10), (6, 7), (3, 3)], fill=light)
        d.point((9, 8), fill=grain)
        d.point((12, 3), fill=shadow)
        _leaf_shape(d, (11, 5), (15, 4), 2, c, hi, dark, ink)
    elif name in ("crystal", "sapphire", "crown"):
        slab((2, 12, 13, 15))
        if name == "crystal":
            gem(5, 5, 12, 3)
            gem(9, 1, 12, 4)
            d.line([(4, 11), (11, 11)], fill=light)
        elif name == "sapphire":
            slab((5, 6, 10, 13))
            gem(4, 2, 8, 3)
            gem(11, 1, 7, 3)
            slab((2, 7, 13, 10))
            d.point((8, 11), fill=hi)
        else:
            d.line([(4, 12), (3, 5), (11, 3), (12, 12)], fill=ink, width=3)
            d.line([(4, 11), (3, 5), (11, 3), (12, 11)], fill=cut)
            slab((2, 8, 13, 11))
            for x, y in ((4, 2), (8, 0), (12, 2)):
                d.polygon([(x - 1, y + 3), (x, y), (x + 1, y + 3)], fill=c)
                d.point((x, y + 1), fill=hi)
                d.point((x, y + 3), fill=dark)
    elif name == "moss":
        for x, y, rx, ry in ((5, 10, 4, 4), (11, 10, 4, 4), (8, 6, 4, 3)):
            _blob(d, x, y, rx, ry, c, hi, dark, ink)
        for x, y in ((3, 9), (6, 5), (10, 7), (8, 11), (12, 10)):
            d.line([(x, y + 1), (x, y - 1), (x + 1, y - 2)], fill=light)
            d.point((x + 1, y + 1), fill=mid)
        d.line([(4, 14), (11, 14)], fill=shadow)
    elif name == "sap":
        d.line([(2, 13), (7, 10), (10, 6), (13, 3)], fill=ink, width=5)
        d.line([(2, 12), (7, 9), (10, 5), (13, 2)], fill=wood, width=3)
        d.line([(2, 11), (7, 8), (10, 4), (13, 1)], fill=cut)
        d.point((6, 11), fill=grain)
        d.point((9, 8), fill=shadow)
        for x, y in ((3, 11), (8, 8), (12, 4)):
            _blob(d, x, y, 2, 2, c, hi, dark, ink)
            d.point((x - 1, y - 1), fill=light)
    elif name == "flower":
        green, green_hi = map(_rgba, (0x387344, 0xB5D65C))
        d.line([(8, 8), (8, 15)], fill=ink, width=3)
        d.line([(8, 8), (8, 14)], fill=green)
        _leaf_shape(d, (8, 13), (14, 10), 2, green, green_hi, green, ink)
        for x, y in ((7, 3), (3, 6), (12, 6), (5, 10), (10, 10)):
            _blob(d, x, y, 3, 3, c, hi, dark, ink)
        d.ellipse((6, 5, 10, 9), fill=shadow)
        d.ellipse((6, 5, 9, 8), fill=cut)
        d.point((7, 6), fill=light)
    elif name == "amber":
        slab((1, 10, 14, 15))
        _blob(d, 8, 7, 5, 5, c, hi, dark, ink)
        d.polygon([(6, 4), (10, 5), (11, 8), (8, 10), (6, 8)], fill=mid)
        d.line([(5, 6), (6, 4), (8, 3)], fill=light)
        d.line([(7, 7), (9, 6), (8, 8)], fill=shadow)
    elif name == "coral":
        _blob(d, 8, 13, 6, 2, wood, light, shadow, ink)
        for points in (((8, 13), (8, 4)), ((8, 10), (3, 7), (3, 2)),
                       ((8, 9), (12, 6), (12, 1)), ((4, 7), (1, 5))):
            d.line(points, fill=ink, width=3)
            d.line(points, fill=c, width=2)
            d.line([(x - 1, y) for x, y in points], fill=hi)
        for x, y in ((3, 3), (8, 6), (12, 3), (6, 9)):
            d.point((x, y), fill=dark)
        d.point((9, 12), fill=mid)
    elif name == "silk":
        slab((0, 2, 4, 15))
        slab((11, 2, 15, 15))
        d.polygon([(3, 5), (7, 8), (9, 8), (12, 5), (11, 12), (8, 14),
                   (5, 12)], fill=ink)
        d.polygon([(4, 6), (7, 9), (9, 9), (11, 6), (10, 12), (7, 13),
                   (5, 11)], fill=c)
        d.line([(4, 6), (7, 9), (9, 9), (11, 6)], fill=hi)
        d.line([(5, 10), (7, 12), (10, 11)], fill=dark)
        d.line([(6, 9), (6, 11), (9, 11), (10, 8)], fill=mid)
    elif name == "pearl":
        _blob(d, 8, 8, 6, 6, c, hi, dark, ink)
        d.line([(4, 5), (6, 3), (8, 3)], fill=light)
        d.line([(6, 11), (9, 12), (12, 9)], fill=mid)
        d.line([(6, 14), (11, 14)], fill=shadow)
        d.point((10, 4), fill=cut)
    elif name == "ruby":
        d.polygon([(1, 14), (1, 9), (11, 2), (14, 3), (14, 14)], fill=ink)
        d.polygon([(3, 13), (3, 9), (12, 3), (13, 4), (13, 13)], fill=wood)
        d.line([(3, 9), (12, 3)], fill=hi, width=2)
        d.line([(4, 10), (12, 5)], fill=c)
        d.line([(5, 11), (12, 6)], fill=dark)
        d.line([(6, 12), (12, 8)], fill=grain)
        d.line([(8, 12), (12, 9)], fill=cut)
        d.point((12, 12), fill=shadow)
        d.point((3, 13), fill=light)
        d.point((11, 7), fill=mid)
    elif name == "emerald":
        slab((1, 10, 14, 15))
        d.line([(8, 11), (8, 2)], fill=ink, width=3)
        d.line([(8, 10), (8, 2)], fill=dark)
        for base, tip in (((8, 5), (3, 1)), ((8, 8), (14, 3)),
                          ((8, 10), (2, 6)), ((8, 10), (13, 7))):
            _leaf_shape(d, base, tip, 2, c, hi, dark, ink)
    else:
        raise ValueError("Unknown gift: " + name)
    return furnishing_art.existing(_enlarge(img), name, "gift")


def draw_decor(name):
    """Keep legacy prop sizes, using the same materials as placeable decor."""
    width, height = {
        "rock_0": (24, 16), "rock_1": (20, 14),
        "plant_0": (24, 32), "plant_1": (26, 28), "wood_log": (48, 18),
    }[name]
    item = {"rock_0": "rock_large", "rock_1": "rock_small",
            "plant_0": "fern", "plant_1": "succulent", "wood_log": "log"}[name]
    source = draw_decor_item(item).resize((32, 32), Image.Resampling.NEAREST)
    source = source.crop(source.getbbox())
    return _enlarge(source.resize((width, height), Image.Resampling.NEAREST))


def _decor_canvas():
    """A 32x32 canvas for a placeable decor item."""
    return _pixel_canvas(32, 32)


def _outline(d, points, color, width=1):
    """Draw a closed polygon outline, so every prop reads against the floor."""
    d.line(list(points) + [points[0]], fill=color, width=width)


def _blob(d, cx, cy, rx, ry, body, light, dark, ink=INK):
    """Clipped six-tone volume; no offset ellipse can leak past the ink rim."""
    mid = tuple((body[i] + dark[i]) // 2 for i in range(3)) + (255,)
    bounce = tuple((body[i] * 2 + light[i]) // 3 for i in range(3)) + (255,)
    mask = Image.new("1", (rx * 2 + 1, ry * 2 + 1))
    ImageDraw.Draw(mask).ellipse((0, 0, rx * 2, ry * 2), fill=1)
    for y in range(ry * 2 + 1):
        for x in range(rx * 2 + 1):
            if not mask.getpixel((x, y)):
                continue
            edge = (x == 0 or y == 0 or x == rx * 2 or y == ry * 2
                    or not all(mask.getpixel((x + dx, y + dy))
                               for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))))
            value = x * ry + y * rx
            color = (light if value < rx * ry else body if value < rx * ry * 2
                     else mid if value < rx * ry * 3 else dark)
            if edge:
                color = ink
            elif y == ry * 2 - 2 and x > rx:
                color = bounce
            d.point((cx - rx + x, cy - ry + y), fill=color)


def _leaf_shape(d, base, tip, breadth, body, light, dark, ink=INK):
    """A pointed leaf with an ink outline, a lit half and a midrib."""
    bx, by = base
    tx, ty = tip
    dx, dy = tx - bx, ty - by
    length = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / length, dx / length
    left, right = [], []
    for step in range(1, 9):
        t = step / 9.0
        width = breadth * math.sin(math.pi * t) ** 0.7
        px, py = bx + dx * t, by + dy * t
        left.append((px + nx * width, py + ny * width))
        right.append((px - nx * width, py - ny * width))
    points = [(bx, by)] + left + [(tx, ty)] + right[::-1]
    points = [(round(px), round(py)) for px, py in points]
    left = [(round(px), round(py)) for px, py in left]
    right = [(round(px), round(py)) for px, py in right]
    d.polygon(points, fill=body)
    d.polygon([base] + left + [tip], fill=light)
    d.polygon([base] + right[:4] + [tip], fill=dark)
    _outline(d, points, ink)
    vein = tuple((body[i] + light[i]) // 2 for i in range(3)) + (255,)
    d.line([base, tip], fill=vein)
    for step in (3, 5, 7):
        px, py = bx + dx * step // 9, by + dy * step // 9
        d.line([(px, py), left[step - 1]], fill=body)


def _frond(d, base, tip, curve, body, light, dark, ink=INK):
    """A fern frond: a curved rachis with paired pinnae along it.

    A fern is not a leaf. Drawing it as one pointed blade is what made the
    earlier attempts read as grass, so this walks the rachis and stamps a
    short leaflet on each side at every step, shrinking toward the tip.
    """
    bx, by = base
    tx, ty = tip
    dx, dy = tx - bx, ty - by
    length = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / length, dx / length
    steps = 7
    spine = []
    for step in range(steps + 1):
        t = step / steps
        # Bend the rachis sideways so the frond arcs instead of standing rigid.
        bend = curve * math.sin(math.pi * t)
        spine.append((round(bx + dx * t + nx * bend),
                      round(by + dy * t + ny * bend)))
    d.line(spine, fill=ink, width=2)
    d.line(spine, fill=dark, width=1)
    for step in range(1, steps):
        t = step / steps
        px, py = spine[step]
        span = 5.0 * (1 - t) + 1
        if span < 1.2:
            continue
        for side in (1, -1):
            ex = px + nx * span * side + dx / length * 1.6
            ey = py + ny * span * side + dy / length * 1.6
            _leaf_shape(d, (px, py), (round(ex), round(ey)), 1.4,
                        body, light, dark, ink)


def draw_decor_item(name):
    """One placeable decor item at 32x32, doubled to 64x64."""
    if name in core_decor_art.NAMES:
        return core_decor_art.draw(name)
    if name in DECOR_ITEM_NAMES[34:]:
        return _draw_catalogue_decor(name)
    img, d = _decor_canvas()
    stone, stone_hi, stone_dark = map(_rgba, (0x909188, 0xD6CEAC, 0x505568))
    leaf, leaf_hi, leaf_dark = map(_rgba, (0x52963A, 0xB5D65C, 0x245B49))
    bark, bark_hi, bark_dark = map(_rgba, (0xAE743D, 0xEAC58A, 0x623C39))
    white, white_hi, white_dark = map(_rgba, (0xF2EEDC, 0xFFFFFF, 0xC8C2AC))
    pink, pink_hi, pink_dark = map(_rgba, (0xE87AB0, 0xFFB8D8, 0xB84E80))
    red, red_hi, red_dark = map(_rgba, (0xD83A2E, 0xFF7A5A, 0x8A1E18))
    cyan, cyan_hi, cyan_dark = map(_rgba, (0x62C8D8, 0xC8F4FF, 0x2E7A8A))
    bone, bone_hi, bone_dark = map(_rgba, (0xE8E2CC, 0xFFFDF0, 0xA8A088))
    shell, shell_hi, shell_dark = map(_rgba, (0xF2A08E, 0xFFD8C8, 0xC06A5A))
    stone_mid, stone_glint = map(_rgba, (0x73788A, 0xF0E8C9))
    grain, cut = map(_rgba, (0x875239, 0xC99459))

    if name == "rock_small":
        _blob(d, 16, 19, 10, 7, stone, stone_hi, stone_dark)
        d.polygon([(8, 19), (12, 14), (19, 14), (17, 18)], fill=stone_hi)
        d.polygon([(18, 19), (24, 17), (25, 21), (20, 24)], fill=stone_mid)
        d.line([(10, 17), (12, 15), (16, 15)], fill=stone_glint)
        d.line([(11, 16), (15, 14), (20, 16)], fill=stone_hi)
        d.line([(13, 21), (18, 22)], fill=stone_dark)
    elif name == "rock_large":
        _blob(d, 16, 17, 14, 11, stone, stone_hi, stone_dark)
        d.polygon([(5, 16), (12, 8), (20, 8), (17, 16)], fill=stone_hi)
        d.polygon([(19, 14), (28, 16), (25, 23), (20, 25)], fill=stone_mid)
        d.line([(8, 13), (13, 9), (18, 9)], fill=stone_glint)
        d.line([(7, 14), (13, 9), (21, 10)], fill=stone_hi, width=2)
        d.line([(10, 21), (17, 24), (23, 21)], fill=stone_dark, width=2)
        d.line([(19, 13), (22, 17)], fill=stone_dark)
    elif name == "pebbles":
        for cx, cy, rx, ry in ((10, 22, 5, 4), (20, 24, 4, 3), (16, 15, 6, 5),
                               (24, 16, 3, 3), (7, 14, 3, 2)):
            _blob(d, cx, cy, rx, ry, stone, stone_hi, stone_dark)
            d.line([(cx - 1, cy - ry + 1), (cx + 1, cy - ry + 1)],
                   fill=stone_glint)
            d.point((cx + rx - 1, cy), fill=stone_mid)
    elif name == "log":
        d.polygon([(7, 8), (26, 6), (30, 11), (30, 21),
                   (25, 25), (7, 27), (2, 23), (1, 15)], fill=INK)
        d.polygon([(8, 9), (25, 7), (29, 11), (29, 20),
                   (25, 24), (8, 26)], fill=bark_dark)
        d.polygon([(8, 9), (25, 7), (28, 10), (27, 15),
                   (8, 18)], fill=bark)
        for y in (10, 14, 19, 23):
            d.line([(10, y), (16, y - 1), (20, y), (27, y - 2)], fill=grain)
            d.line([(11, y - 1), (16, y - 2), (20, y - 1)], fill=cut)
        d.line([(11, 9), (23, 8), (26, 9)], fill=bark_hi)
        d.line([(18, 11), (17, 14), (19, 17), (18, 22)], fill=INK)
        d.line([(19, 12), (18, 14), (20, 17)], fill=cut)
        _blob(d, 7, 18, 6, 9, cut, bark_hi, grain)
        d.ellipse((3, 12, 10, 24), fill=bark_dark)
        d.ellipse((4, 13, 9, 23), fill=INK)
        d.line([(4, 23), (7, 24), (9, 21)], fill=grain)
    elif name == "bark":
        d.polygon([(4, 20), (8, 10), (18, 7), (27, 12), (26, 22), (14, 25)],
                  fill=INK)
        d.polygon([(5, 19), (9, 11), (18, 8), (26, 13), (25, 21), (14, 24)],
                  fill=bark)
        d.polygon([(5, 19), (9, 11), (18, 8), (20, 12), (12, 20)], fill=bark_hi)
        for x, y in ((14, 11), (19, 14), (11, 14), (18, 19)):
            d.line([(x, y), (x - 3, y + 4), (x - 2, y + 7)], fill=bark_dark)
            d.line([(x + 1, y), (x - 2, y + 4)], fill=cut)
            d.point((x + 2, y - 1), fill=grain)
        d.line([(8, 17), (12, 12), (16, 10)], fill=_rgba(0xF3DAA2))
        d.line([(16, 23), (23, 21)], fill=_rgba(0xBF8951))
    elif name == "fern":
        # A rosette of arcing fronds, the way a real fern grows from a crown.
        for base, tip, curve in (((16, 27), (5, 8), 2.5),
                                 ((16, 27), (16, 2), 0.0),
                                 ((16, 27), (27, 8), -2.5),
                                 ((16, 27), (2, 19), 3.0),
                                 ((16, 27), (30, 19), -3.0)):
            _frond(d, base, tip, curve, leaf, leaf_hi, leaf_dark)
        _blob(d, 16, 27, 3, 3, bark, bark_hi, bark_dark)
    elif name == "moss_clump":
        for cx, cy, rx, ry in ((12, 20, 8, 6), (21, 21, 7, 5), (16, 14, 7, 5),
                               (9, 15, 5, 4), (24, 14, 4, 3)):
            _blob(d, cx, cy, rx, ry, leaf, leaf_hi, leaf_dark)
        for x, y in ((8, 13), (13, 11), (19, 12), (22, 18), (12, 19),
                     (8, 22), (17, 22), (25, 13)):
            d.line([(x, y + 1), (x, y - 1), (x + 2, y - 2)],
                   fill=_rgba(0xD5E77B))
            d.point((x + 2, y + 1), fill=_rgba(0x387344))
            d.point((x - 1, y + 2), fill=_rgba(0x718E32))
    elif name == "succulent":
        for radius, breadth, body, hi, dk in (
            (14, 4, _rgba(0x388D79), _rgba(0x82C68B), _rgba(0x24535B)),
            (9, 3, _rgba(0x68AE80), _rgba(0xC2DD9A), _rgba(0x3B796E)),
        ):
            for dx, dy in ((0, -10), (7, -7), (10, 0), (7, 7), (0, 10),
                           (-7, 7), (-10, 0), (-7, -7)):
                _leaf_shape(d, (16, 17), (16 + dx * radius // 10,
                            17 + dy * radius // 12), breadth, body, hi, dk)
        d.line([(16, 18), (16, 13), (18, 15)], fill=_rgba(0xE6E9AB))
    elif name == "flower_white":
        for step in range(5):
            angle = step * 2 * math.pi / 5 - math.pi / 2
            cx = int(16 + math.cos(angle) * 7)
            cy = int(16 + math.sin(angle) * 7)
            _blob(d, cx, cy, 5, 5, white, white_hi, white_dark)
        _blob(d, 16, 16, 4, 4, _rgba(0xE8C070), white_hi, _rgba(0xB89040))
    elif name == "flower_pink":
        for step in range(5):
            angle = step * 2 * math.pi / 5 - math.pi / 2
            cx = int(16 + math.cos(angle) * 7)
            cy = int(16 + math.sin(angle) * 7)
            _blob(d, cx, cy, 5, 5, pink, pink_hi, pink_dark)
        _blob(d, 16, 16, 4, 4, white, white_hi, white_dark)
    elif name == "mushroom":
        d.rectangle((13, 17, 19, 27), fill=INK)
        d.rectangle((14, 18, 18, 26), fill=white)
        d.rectangle((14, 18, 15, 26), fill=white_hi)
        _blob(d, 16, 15, 12, 9, red, red_hi, red_dark)
        for cx, cy in ((11, 12), (20, 13), (16, 18), (23, 17)):
            d.ellipse((cx - 2, cy - 2, cx + 2, cy + 2), fill=white_hi)
    elif name == "shell":
        d.polygon([(16, 28), (4, 12), (8, 6), (16, 4), (24, 6), (28, 12)],
                  fill=INK)
        d.polygon([(16, 27), (5, 12), (9, 7), (16, 5), (23, 7), (27, 12)],
                  fill=shell)
        for step in range(-2, 3):
            tx = 16 + step * 4
            ty = 6 + abs(step) * 2
            d.line([(16, 26), (tx, ty)], fill=shell_dark, width=2)
            d.line([(16, 24), (tx - 1, ty)], fill=shell_hi)
        d.line([(7, 12), (10, 8), (16, 6), (21, 8)], fill=_rgba(0xFFF0D5))
        d.line([(8, 16), (16, 26), (24, 16)], fill=_rgba(0xDB8279))
        d.line([(10, 18), (16, 24), (22, 18)], fill=_rgba(0xF5BAA1))
        d.ellipse((12, 24, 20, 29), fill=shell_hi)
        d.line([(13, 28), (19, 28)], fill=_rgba(0xA75468))
        d.line([(14, 25), (18, 25)], fill=_rgba(0xFFEAC3))
    elif name == "crystal":
        for x, y, w, h in ((7, 15, 5, 12), (24, 12, 5, 15), (16, 2, 7, 26)):
            d.polygon([(x, y), (x + w, y + 6), (x + w - 1, y + h),
                       (x - w + 1, y + h), (x - w, y + 6)], fill=INK)
            d.polygon([(x, y + 1), (x, y + h - 1), (x - w + 2, y + h - 1),
                       (x - w + 1, y + 6)], fill=cyan)
            d.polygon([(x, y + 1), (x + w - 1, y + 6),
                       (x + w - 2, y + h - 1), (x, y + h - 1)], fill=cyan_dark)
            d.polygon([(x, y + 1), (x + w - 1, y + 6), (x, y + 9),
                       (x - w + 1, y + 6)], fill=cyan_hi)
            d.line([(x - 1, y + 4), (x - 1, y + h - 3)], fill=_rgba(0xE9FCFF))
            d.line([(x + 2, y + 10), (x + 2, y + h - 2)], fill=_rgba(0x4278BA))
        d.line([(11, 28), (19, 29), (23, 27)], fill=_rgba(0x4B4A88))
        d.point((8, 28), fill=_rgba(0x82A8E8))
        d.point((25, 28), fill=_rgba(0xA7E7EE))
    elif name == "leaf_pile":
        for base, tip, body, hi, dk in (
            ((8, 26), (4, 12), bark, bark_hi, bark_dark),
            ((14, 27), (12, 8), leaf, leaf_hi, leaf_dark),
            ((20, 27), (24, 10), bark, bark_hi, bark_dark),
            ((25, 26), (28, 14), leaf, leaf_hi, leaf_dark),
            ((17, 24), (18, 14), leaf, leaf_hi, leaf_dark),
        ):
            _leaf_shape(d, base, tip, 4, body, hi, dk)
    elif name == "twig":
        d.line([(4, 26), (14, 16), (22, 12), (28, 5)], fill=INK, width=4)
        d.line([(4, 25), (14, 15), (22, 11), (28, 4)], fill=bark, width=2)
        d.line([(14, 16), (10, 8)], fill=INK, width=3)
        d.line([(14, 15), (10, 8)], fill=bark_hi, width=1)
        d.line([(22, 12), (26, 18)], fill=INK, width=3)
        d.line([(22, 11), (26, 18)], fill=bark_hi, width=1)
        for x, y in ((7, 23), (16, 15), (24, 9)):
            d.line([(x, y), (x + 2, y - 2)], fill=grain)
            d.point((x, y - 1), fill=cut)
        d.ellipse((3, 24, 6, 27), fill=cut)
        d.point((4, 25), fill=_rgba(0xF3DAA2))
        d.line([(25, 16), (26, 17)], fill=_rgba(0xBF8951))
        d.line([(12, 12), (13, 14)], fill=_rgba(0xD6A36B))
    elif name == "fern_pot":
        d.polygon([(7, 18), (25, 18), (22, 29), (10, 29)], fill=INK)
        d.polygon([(9, 19), (23, 19), (20, 27), (11, 27)], fill=red)
        d.line([(10, 20), (22, 20)], fill=red_hi, width=2)
        d.line([(12, 23), (20, 23)], fill=red_dark)
        for base, tip, curve in (((16, 20), (6, 6), 1.0), ((16, 20), (12, 3), 0.5),
                                 ((16, 20), (23, 4), -0.5), ((16, 20), (29, 9), -1.0)):
            _frond(d, base, tip, curve, leaf, leaf_hi, leaf_dark)
        _blob(d, 16, 20, 5, 2, bark_dark, grain, INK)
    elif name == "acorn_hide":
        d.polygon([(4, 15), (7, 8), (13, 5), (21, 6), (27, 11),
                   (29, 20), (25, 27), (7, 27), (3, 22)], fill=INK)
        d.polygon([(6, 15), (9, 9), (14, 7), (21, 8), (25, 12),
                   (27, 20), (23, 24), (8, 24), (5, 21)], fill=bark)
        d.polygon([(5, 14), (8, 9), (14, 7), (20, 8), (24, 12), (20, 15),
                   (13, 14), (8, 17)], fill=bark_hi)
        d.ellipse((9, 15, 23, 25), fill=INK)
        d.ellipse((11, 17, 21, 24), fill=bark_dark)
        d.arc((12, 18, 20, 24), 190, 350, fill=grain, width=2)
        for x in range(9, 24, 4):
            d.line([(x, 9), (x + 2, 12)], fill=grain)
    elif name == "driftwood_arch":
        d.polygon([(3, 27), (5, 10), (9, 5), (14, 6), (13, 10),
                   (10, 12), (9, 25), (23, 25), (22, 12), (18, 10),
                   (17, 6), (23, 5), (27, 11), (29, 28)], fill=INK)
        d.polygon([(6, 25), (8, 11), (11, 8), (13, 9), (12, 12),
                   (11, 14), (11, 27), (21, 27), (21, 14), (19, 12),
                   (19, 9), (22, 8), (25, 12), (26, 25)], fill=bark)
        d.line([(8, 12), (10, 9), (12, 9)], fill=bark_hi, width=2)
        d.line([(22, 9), (24, 12), (24, 16)], fill=grain, width=2)
        d.line([(9, 19), (10, 22)], fill=bark_dark)
        d.line([(22, 19), (23, 23)], fill=cut)
        d.line([(11, 26), (20, 26)], fill=grain)
    elif name == "web_hammock":
        d.line([(3, 7), (8, 8), (12, 16), (16, 20), (20, 16), (24, 8), (29, 7)],
               fill=INK, width=4)
        d.line([(3, 6), (8, 7), (12, 15), (16, 19), (20, 15), (24, 7), (29, 6)],
               fill=stone_hi, width=2)
        d.line([(7, 8), (11, 14), (16, 18), (21, 14), (25, 8)], fill=cyan)
        for offset in range(1, 5):
            x = 8 + offset * 3
            y = 8 + min(offset, 5 - offset) * 2
            d.line([(x, y), (32 - x, y)], fill=cyan_dark)
        d.line([(4, 5), (2, 26)], fill=bark, width=3)
        d.line([(28, 5), (30, 26)], fill=bark, width=3)
        d.line([(3, 8), (4, 6)], fill=bark_hi)
        d.line([(28, 7), (29, 9)], fill=bark_hi)
        _blob(d, 16, 20, 4, 2, cyan, cyan_hi, cyan_dark)
    elif name == "dew_pool":
        d.ellipse((3, 13, 29, 27), fill=INK)
        d.ellipse((5, 13, 27, 24), fill=cyan_dark)
        d.ellipse((7, 12, 25, 22), fill=cyan)
        d.polygon([(16, 14), (10, 17), (8, 20), (14, 21), (20, 19), (24, 16)],
                  fill=cyan_hi)
        d.line([(8, 17), (12, 14), (18, 14)], fill=white_hi)
        d.line([(7, 22), (12, 24), (20, 23)], fill=cyan_dark)
        _leaf_shape(d, (20, 16), (27, 12), 3, leaf, leaf_hi, leaf_dark)
        _blob(d, 20, 16, 2, 1, _rgba(0xE9A44F), _rgba(0xFFF0C0), bark_dark)
    elif name == "mushroom_pair":
        d.rectangle((8, 16, 12, 26), fill=INK)
        d.rectangle((9, 17, 11, 25), fill=white)
        _blob(d, 10, 14, 8, 6, red, red_hi, red_dark)
        for x, y in ((7, 12), (12, 14), (9, 17)):
            d.point((x, y), fill=white_hi)
        d.rectangle((20, 20, 23, 28), fill=INK)
        d.rectangle((21, 21, 22, 27), fill=white)
        _blob(d, 22, 19, 6, 4, red, red_hi, red_dark)
        d.point((21, 18), fill=white_hi)
        d.line([(8, 27), (25, 27)], fill=leaf_dark)
        d.point((6, 24), fill=leaf_hi)
    elif name == "flower_planter":
        d.polygon([(4, 19), (28, 19), (25, 28), (7, 28)], fill=INK)
        d.polygon([(6, 20), (26, 20), (23, 26), (9, 26)], fill=bark)
        d.line([(7, 20), (25, 20)], fill=bark_hi, width=2)
        for x, y in ((10, 16), (17, 13), (24, 16)):
            d.line([(x, 19), (x, y + 2)], fill=leaf_dark, width=2)
            for dx, dy in ((-3, 0), (0, -3), (3, 0), (0, 3)):
                _blob(d, x + dx, y + dy, 3, 3, pink, pink_hi, pink_dark)
            d.ellipse((x - 1, y - 1, x + 1, y + 1), fill=white_hi)
        d.point((12, 23), fill=grain)
        d.point((21, 24), fill=cut)
    elif name == "crystal_cluster":
        for x, y, width, height, body, light, dark in (
            (7, 15, 5, 13, cyan, cyan_hi, cyan_dark),
            (15, 3, 7, 25, pink, pink_hi, pink_dark),
            (25, 12, 5, 16, cyan, cyan_hi, cyan_dark),
            (12, 18, 4, 10, stone_mid, stone_glint, stone_dark),
        ):
            d.polygon([(x, y + 5), (x + width, y), (x + width - 1, y + height),
                       (x - width + 1, y + height), (x - width, y + 5)], fill=INK)
            d.polygon([(x, y + 1), (x - width + 1, y + 5), (x - 1, y + height - 1)],
                      fill=body)
            d.polygon([(x, y + 1), (x + width - 1, y + 5),
                       (x + width - 2, y + height - 1), (x, y + height - 1)], fill=dark)
            d.line([(x - 1, y + 4), (x, y + 2), (x + 1, y + 5)], fill=light)
        d.polygon([(4, 28), (10, 26), (17, 28), (25, 26), (29, 29)], fill=stone_dark)
        d.line([(6, 29), (26, 29)], fill=stone_glint)
    elif name == "shell_cove":
        d.polygon([(3, 24), (4, 15), (8, 7), (15, 4), (23, 7),
                   (28, 15), (29, 24), (24, 28), (8, 28)], fill=INK)
        d.polygon([(5, 23), (6, 15), (9, 9), (15, 6), (22, 9),
                   (26, 15), (27, 23), (23, 26), (9, 26)], fill=shell)
        d.ellipse((9, 14, 23, 25), fill=shell_dark)
        d.ellipse((11, 15, 21, 24), fill=INK)
        d.polygon([(11, 19), (15, 16), (21, 18), (19, 23), (13, 23)], fill=cyan_dark)
        d.line([(6, 16), (9, 10), (15, 7), (21, 10)], fill=shell_hi)
        for x in (8, 11, 14, 17, 20, 23):
            d.line([(16, 23), (x, 10 + abs(16 - x) // 2)], fill=shell_hi)
        d.point((16, 19), fill=cyan_hi)
    elif name == "twig_ladder":
        d.line([(7, 4), (11, 27)], fill=INK, width=5)
        d.line([(23, 4), (19, 27)], fill=INK, width=5)
        d.line([(7, 5), (11, 26)], fill=bark, width=2)
        d.line([(23, 5), (19, 26)], fill=bark_hi, width=2)
        for y in (8, 13, 18, 23):
            d.line([(9, y), (21, y)], fill=INK, width=4)
            d.line([(10, y - 1), (20, y - 1)], fill=cut, width=2)
            d.point((12, y), fill=grain)
        d.line([(6, 7), (7, 5)], fill=bark_hi)
        d.line([(20, 26), (21, 28)], fill=grain)
    elif name == "moss_stone":
        _blob(d, 16, 20, 13, 8, stone, stone_hi, stone_dark)
        d.polygon([(5, 18), (8, 14), (14, 12), (21, 13), (27, 17),
                   (24, 20), (15, 19), (9, 21)], fill=leaf_dark)
        for cx, cy, rx, ry in ((10, 15, 5, 3), (17, 14, 6, 4), (23, 16, 4, 3)):
            _blob(d, cx, cy, rx, ry, leaf, leaf_hi, leaf_dark)
        for x, y in ((8, 14), (13, 12), (20, 13), (24, 15)):
            d.point((x, y), fill=leaf_hi)
        d.line([(10, 23), (15, 25), (21, 23)], fill=stone_dark)
        d.line([(7, 19), (10, 18)], fill=stone_glint)
    elif name == "seed_pod":
        d.polygon([(7, 24), (6, 18), (9, 12), (16, 8), (23, 9),
                   (27, 14), (25, 21), (19, 26), (12, 27)], fill=INK)
        d.polygon([(9, 23), (8, 18), (11, 13), (16, 10), (22, 11),
                   (25, 15), (23, 20), (18, 24), (12, 25)], fill=leaf)
        d.polygon([(9, 21), (10, 15), (15, 11), (19, 12), (16, 18)], fill=leaf_hi)
        d.line([(10, 22), (16, 19), (22, 13)], fill=leaf_dark, width=2)
        for x, y in ((12, 19), (16, 16), (19, 14)):
            _blob(d, x, y, 2, 2, red, red_hi, red_dark)
        d.line([(16, 10), (17, 5), (20, 3)], fill=bark, width=2)
        d.line([(17, 10), (18, 6)], fill=bark_hi)
    elif name == "silk_tent":
        d.polygon([(3, 27), (8, 8), (16, 3), (24, 8), (29, 27),
                   (22, 27), (16, 17), (10, 27)], fill=INK)
        d.polygon([(6, 25), (10, 9), (16, 5), (22, 9), (26, 25),
                   (21, 25), (16, 15), (11, 25)], fill=white)
        d.polygon([(13, 24), (16, 16), (20, 25)], fill=stone_dark)
        d.line([(10, 9), (16, 5), (22, 9)], fill=white_hi, width=2)
        for y in (12, 16, 20):
            d.line([(11, y), (16, y + 2), (21, y)], fill=cyan)
        d.line([(5, 26), (27, 26)], fill=bark, width=2)
        d.point((16, 8), fill=cyan_hi)
    elif name == "leaf_umbrella":
        d.line([(16, 13), (16, 28)], fill=INK, width=4)
        d.line([(16, 14), (16, 27)], fill=bark, width=2)
        d.line([(16, 25), (18, 28), (21, 28)], fill=bark_hi)
        d.polygon([(2, 14), (5, 8), (12, 4), (20, 4), (27, 8),
                   (30, 14), (26, 18), (22, 13), (17, 17), (12, 13),
                   (7, 18)], fill=INK)
        d.polygon([(4, 13), (7, 9), (13, 6), (20, 6), (26, 9),
                   (28, 13), (25, 15), (22, 11), (17, 15), (12, 11),
                   (7, 15)], fill=leaf)
        d.polygon([(4, 13), (7, 9), (13, 6), (16, 7), (16, 13),
                   (12, 11), (7, 15)], fill=leaf_hi)
        d.line([(5, 12), (16, 13), (27, 12)], fill=leaf_dark, width=2)
        d.line([(16, 13), (16, 8)], fill=leaf_hi)
    elif name == "bone_arch":
        d.line([(6, 28), (6, 13), (8, 7), (12, 5), (15, 9)], fill=INK, width=6)
        d.line([(26, 28), (26, 13), (24, 7), (20, 5), (17, 9)], fill=INK, width=6)
        d.line([(6, 27), (6, 13), (8, 8), (12, 6), (15, 10)], fill=bone, width=3)
        d.line([(26, 27), (26, 13), (24, 8), (20, 6), (17, 10)], fill=bone_hi, width=3)
        d.ellipse((3, 5, 10, 12), fill=bone)
        d.ellipse((22, 5, 29, 12), fill=bone_hi)
        d.ellipse((5, 7, 8, 10), fill=bone_dark)
        d.ellipse((24, 7, 27, 10), fill=bone_dark)
        d.line([(8, 27), (24, 27)], fill=stone_dark)
        d.point((8, 13), fill=white_hi)
        d.point((24, 13), fill=white_hi)
    elif name == "fossil":
        _blob(d, 16, 20, 13, 7, stone_dark, stone, INK)
        d.ellipse((7, 9, 25, 25), fill=INK)
        d.ellipse((9, 11, 23, 23), fill=stone)
        d.arc((11, 13, 21, 23), 190, 350, fill=stone_hi, width=3)
        d.arc((13, 15, 19, 21), 40, 320, fill=stone_dark, width=2)
        d.ellipse((15, 17, 18, 20), fill=stone_glint)
        d.line([(9, 12), (12, 10), (16, 10)], fill=stone_glint)
        d.line([(19, 24), (23, 22)], fill=stone_dark)
        d.point((7, 21), fill=stone_mid)
    elif name == "lantern_cap":
        d.line([(15, 9), (15, 3), (19, 2)], fill=bark, width=2)
        d.line([(16, 9), (16, 4)], fill=bark_hi)
        d.rectangle((13, 18, 19, 27), fill=INK)
        d.rectangle((14, 19, 18, 26), fill=white)
        d.rectangle((14, 19, 16, 25), fill=white_hi)
        _blob(d, 16, 16, 11, 8, _rgba(0xD8AE43), _rgba(0xFFF0B0), red_dark)
        d.arc((7, 8, 25, 22), 190, 350, fill=red, width=2)
        for x, y in ((11, 14), (17, 12), (21, 16)):
            d.point((x, y), fill=white_hi)
        d.line([(9, 16), (12, 13), (16, 12)], fill=white_hi)
        d.line([(15, 27), (18, 27)], fill=leaf_dark)
    elif name == "stone_bridge":
        d.polygon([(2, 10), (7, 6), (25, 6), (30, 10), (28, 18),
                   (24, 14), (20, 12), (12, 12), (8, 15), (4, 19)], fill=INK)
        d.polygon([(4, 10), (8, 8), (24, 8), (28, 10), (26, 15),
                   (22, 12), (19, 11), (13, 11), (9, 13), (5, 16)], fill=stone)
        d.polygon([(8, 17), (11, 14), (14, 13), (18, 13), (21, 14), (24, 17),
                   (23, 25), (9, 25)], fill=stone_dark)
        d.rectangle((11, 18, 21, 24), fill=INK)
        d.line([(7, 10), (12, 9), (21, 9)], fill=stone_hi, width=2)
        d.line([(6, 14), (9, 12)], fill=stone_mid)
        d.line([(24, 12), (26, 14)], fill=stone_glint)
        d.line([(4, 26), (28, 26)], fill=leaf_dark)
    elif name == "bone":
        d.rectangle((10, 12, 22, 20), fill=INK)
        d.rectangle((11, 13, 21, 19), fill=bone)
        d.rectangle((11, 13, 21, 15), fill=bone_hi)
        for cx, cy in ((9, 11), (9, 21), (23, 11), (23, 21)):
            d.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=INK)
            d.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=bone)
            d.ellipse((cx - 3, cy - 3, cx + 1, cy), fill=bone_hi)
        d.line([(13, 17), (19, 17)], fill=bone_dark)
        d.line([(11, 18), (20, 18)], fill=_rgba(0xC8B99A))
        d.line([(11, 16), (20, 16)], fill=_rgba(0xE1D2B0))
        d.line([(7, 23), (10, 24)], fill=_rgba(0xB7A58B))
        d.point((22, 10), fill=_rgba(0xF5EACF))
    else:
        raise ValueError(f"Unknown decor item: {name}")
    # Broken surface highlights and small material marks, clipped to the prop.
    texture = {
        "rock_small": stone_mid, "rock_large": stone_mid, "pebbles": stone_mid,
        "bark": grain, "moss_clump": _rgba(0x387344), "bone": bone_dark,
        "mushroom": red_dark, "shell": shell_dark,
    }.get(name)
    if texture is not None:
        pixels = img.load()
        candidates = {stone, stone_hi} if name.startswith("rock") or name == "pebbles" else {
            "bark": {bark, bark_hi}, "moss_clump": {leaf, leaf_hi},
            "bone": {bone, bone_hi}, "mushroom": {red, red_hi},
            "shell": {shell, shell_hi},
        }.get(name, set())
        for y in range(3, 29):
            for x in range(3, 29):
                if (x * 37 + y * 61) % 29 == 0 and pixels[x, y] in candidates:
                    d.point((x, y), fill=texture)
    material_shading = {
        "acorn_hide": (bark, bark_hi),
        "driftwood_arch": (bark, bark_hi, grain),
        "shell_cove": (shell, shell_hi),
        "twig_ladder": (bark, cut),
        "silk_tent": (white, cyan),
        "leaf_umbrella": (leaf, bark),
        "bone_arch": (bone, bone_hi),
        "stone_bridge": (stone,),
    }.get(name, ())
    return furnishing_art.existing(_enlarge(_shade_materials(img, material_shading)), name, "decor")


# ---------------------------------------------------------------------------
# Native-resolution terrain: discrete colour clusters, quiet central substrate,
# and low-profile edge materials. Furniture remains player-placed decor.
# ---------------------------------------------------------------------------
def _hls(hue, light, sat):
    """HLS triple to a packed RGB int, clamped to the 0-255 cube."""
    r, g, b = colorsys.hls_to_rgb((hue % 360) / 360.0,
                                  max(0.0, min(1.0, light)),
                                  max(0.0, min(1.0, sat)))
    return (int(r * 255) << 16) | (int(g * 255) << 8) | int(b * 255)


def _ramp(hue, light, sat, count, spread):
    """Finite hue-shifted ramp, ordered from shadow to highlight."""
    return [_hls(hue + 16 * (0.5 - i / (count - 1)),
                 light + spread * (i / (count - 1) - 0.5), sat)
            for i in range(count)]


def _palette(ground, foliage, stone, accents):
    """Limited material ramps; never interpolate colours between entries."""
    gh, gl, gs = ground
    fh, fl, fs = foliage
    sh, sl, ss = stone
    return {
        "ground": _ramp(gh, gl, gs, 7, 0.14),
        "stone": _ramp(sh, sl, ss, 6, 0.36),
        "leaf": _ramp(fh, fl, fs, 6, 0.40),
        "accent": [_ramp(spec[0], spec[1], spec[2], 5, 0.28)
                   for spec in accents],
    }


BACKGROUND_PALETTES = {
    # cork — warm ochre bark mulch
    "cork": _palette((30, 0.40, 0.58), (96, 0.50, 0.72), (40, 0.62, 0.10),
                     ((52, 0.94, 0.16), (18, 0.52, 0.72), (44, 0.78, 0.62))),
    # moss — green clearing
    "moss": _palette((75, 0.42, 0.55), (96, 0.50, 0.72), (40, 0.60, 0.10),
                     ((52, 0.95, 0.14), (28, 0.50, 0.66), (72, 0.80, 0.66))),
    # leaf_litter — rust drift
    "leaf_litter": _palette((25, 0.43, 0.57), (92, 0.50, 0.70), (40, 0.60, 0.10),
                            ((52, 0.75, 0.44), (8, 0.48, 0.68), (38, 0.58, 0.72))),
    # sand — pale gold dunes
    "sand": _palette((40, 0.62, 0.62), (110, 0.34, 0.62), (40, 0.66, 0.08),
                     ((52, 0.96, 0.12), (24, 0.54, 0.68), (44, 0.80, 0.62))),
    # fern — dark forest floor
    "fern": _palette((45, 0.39, 0.42), (96, 0.54, 0.80), (40, 0.58, 0.10),
                     ((52, 0.95, 0.12), (28, 0.48, 0.64), (76, 0.82, 0.68))),
    # orchid — deep plum floor
    "orchid": _palette((315, 0.42, 0.32), (100, 0.44, 0.62), (40, 0.58, 0.10),
                       ((330, 0.72, 0.72), (312, 0.56, 0.62), (300, 0.90, 0.86))),
    # woodland — timber and moss
    "woodland": _palette((32, 0.43, 0.49), (100, 0.46, 0.66), (40, 0.60, 0.10),
                         ((52, 0.94, 0.14), (20, 0.50, 0.62), (44, 0.80, 0.68))),
    # minimal — bare pale gravel
    "minimal": _palette((42, 0.64, 0.30), (110, 0.36, 0.56), (40, 0.62, 0.06),
                        ((52, 0.96, 0.10), (36, 0.30, 0.62), (0, 0.00, 0.98))),
    # mushroom — grassy glade
    "mushroom": _palette((80, 0.43, 0.50), (100, 0.44, 0.64), (40, 0.60, 0.10),
                         ((52, 0.94, 0.14), (2, 0.72, 0.62), (16, 0.80, 0.66))),
    # gothic — violet flagstones
    "gothic": _palette((262, 0.44, 0.28), (100, 0.40, 0.58), (250, 0.44, 0.10),
                       ((52, 0.92, 0.12), (272, 0.44, 0.62), (286, 0.60, 0.76))),
    # seashell — shell sand
    "seashell": _palette((34, 0.65, 0.56), (150, 0.34, 0.56), (40, 0.66, 0.08),
                         ((52, 0.96, 0.10), (8, 0.72, 0.72), (350, 0.66, 0.66))),    # succulent — cool teal floor
    "succulent": _palette((155, 0.43, 0.31), (150, 0.52, 0.66), (40, 0.58, 0.10),
                          ((52, 0.94, 0.12), (160, 0.34, 0.78), (140, 0.44, 0.86))),
    # willow — dark bank under yellow-green
    "willow": _palette((66, 0.43, 0.48), (66, 0.56, 0.76), (40, 0.58, 0.10),
                       ((52, 0.94, 0.12), (28, 0.50, 0.62), (70, 0.82, 0.70))),
    # desert — terracotta sand
    "desert": _palette((22, 0.50, 0.62), (120, 0.34, 0.56), (40, 0.64, 0.08),
                       ((52, 0.96, 0.10), (36, 0.72, 0.72), (130, 0.50, 0.50))),
    # crystal — dark geode rock
    "crystal": _palette((258, 0.40, 0.37), (186, 0.52, 0.66), (250, 0.40, 0.10),
                        ((186, 0.90, 0.86), (176, 0.72, 0.72), (322, 0.66, 0.66))),
    # cemetery — cold earth
    "cemetery": _palette((200, 0.43, 0.24), (110, 0.34, 0.54), (210, 0.47, 0.16),
                         ((52, 0.90, 0.10), (206, 0.24, 0.72), (200, 0.30, 0.86))),
    # house — warm timber floor and woven rugs
    "house": _palette((28, 0.47, 0.50), (100, 0.48, 0.62), (34, 0.58, 0.10),
                      ((42, 0.88, 0.16), (18, 0.50, 0.68), (350, 0.70, 0.55))),
    # dark cave — cool slate in a deep geode
    "dark_cave": _palette((248, 0.29, 0.38), (178, 0.40, 0.52), (235, 0.42, 0.18),
                          ((190, 0.78, 0.78), (274, 0.60, 0.68), (47, 0.74, 0.72))),
    # night — blue-black earth with distant firefly lights
    "night": _palette((222, 0.31, 0.38), (126, 0.42, 0.50), (218, 0.44, 0.16),
                      ((49, 0.86, 0.75), (195, 0.72, 0.68), (278, 0.62, 0.60))),
    # autumn — russet loam and copper leaf banks
    "autumn": _palette((21, 0.43, 0.57), (83, 0.40, 0.58), (34, 0.55, 0.16),
                       ((29, 0.78, 0.68), (3, 0.54, 0.68), (48, 0.72, 0.68))),
    # snow — cold blue-white ground and ice
    "snow": _palette((205, 0.70, 0.22), (148, 0.44, 0.42), (212, 0.60, 0.14),
                     ((194, 0.84, 0.58), (204, 0.52, 0.62), (224, 0.82, 0.30))),
    # pond — green bank beside a quiet water edge
    "pond": _palette((92, 0.39, 0.42), (112, 0.48, 0.66), (38, 0.55, 0.12),
                     ((188, 0.55, 0.68), (171, 0.40, 0.58), (56, 0.76, 0.68))),
    # ruins — warm gray flagstone with broken masonry
    "ruins": _palette((37, 0.40, 0.34), (93, 0.39, 0.46), (30, 0.51, 0.14),
                      ((44, 0.78, 0.24), (16, 0.43, 0.58), (71, 0.68, 0.48))),
    # marsh — olive mud with reed-lined pools
    "marsh": _palette((76, 0.37, 0.48), (108, 0.43, 0.56), (39, 0.52, 0.14),
                      ((169, 0.40, 0.54), (42, 0.54, 0.68), (90, 0.72, 0.52))),
}


BACKGROUND_SEEDS = {
    "cork": 11, "moss": 23, "leaf_litter": 37, "sand": 41,
    "fern": 53, "orchid": 67, "woodland": 71, "minimal": 83,
    "mushroom": 97, "gothic": 101, "seashell": 113, "succulent": 127,
    "willow": 131, "desert": 149, "crystal": 157, "cemetery": 163,
    "house": 179, "dark_cave": 191, "night": 199, "autumn": 211,
    "snow": 223, "pond": 227, "ruins": 233, "marsh": 239,
}

BG_SIZE = 240


def _rng(seed):
    """Deterministic little LCG so every scene is byte-reproducible."""
    state = seed & 0x7FFFFFFF

    def nxt(n):
        nonlocal state
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        return (state >> 8) % n

    return nxt


def _terrain_stone(d, x, y, r, ramp):
    """Low, chipped terrain stone with three planes and a broken lit rim."""
    colors = list(map(_rgba, ramp))
    points = [(x - r, y), (x - r + 2, y - r // 2),
              (x, y - r), (x + r - 2, y - r + 1),
              (x + r, y), (x + r - 1, y + r // 2),
              (x - r + 2, y + r // 2 + 1)]
    d.polygon(points, fill=colors[0])
    d.polygon([(x - r + 1, y), (x, y - r + 1),
               (x + r - 2, y - r + 2), (x + r - 1, y),
               (x, y + r // 2)], fill=colors[3])
    d.polygon([(x, y), (x + r - 2, y - r + 3),
               (x + r - 1, y), (x + r - 2, y + r // 2),
               (x, y + r // 2)], fill=colors[1])
    d.line([(x - r + 2, y - 1), (x, y - r + 2),
            (x + r - 3, y - r + 2)], fill=colors[5])
    d.line([(x - r + 3, y + 1), (x - 1, y + r // 2 - 1)], fill=colors[2])
    d.point((x + 1, y - r + 3), fill=colors[4])


def _moss_bank(d, x, y, r, ramp, rng):
    colors = list(map(_rgba, ramp))
    for _ in range(26):
        cx, cy = x + rng(r * 2 + 1) - r, y + rng(r + 1) - r // 2
        w, h = 3 + rng(4), 2 + rng(3)
        _blob(d, cx, cy, w, h, colors[2], colors[4], colors[1], colors[0])
        d.line([(cx - 1, cy), (cx, cy - 2), (cx + 1, cy - 1)], fill=colors[5])
        d.point((cx + 1, cy + 1), fill=colors[3])


def _ground(d, p, rng, material="soil", fringe="green"):
    """Clustered substrate with a protected, low-contrast 160x160 center."""
    g = list(map(_rgba, p["ground"]))
    d.rectangle((0, 0, 239, 239), fill=g[3])
    # Small, flat clusters replace both smooth noise and isolated bright grit.
    for _ in range(320 if material != "minimal" else 70):
        x, y = rng(240), rng(240)
        w, h = 2 + rng(4), 1 + rng(3)
        if material in ("sand", "shell", "desert", "minimal"):
            d.line([(x, y), (x + 1, y)], fill=g[2])
            d.point((x + 1, y - 1), fill=g[4])
        elif material == "moss":
            d.line([(x, y), (x + 1, y - h), (x + 2, y)], fill=g[2])
            d.line([(x + 2, y), (x + 3, y - h), (x + 4, y)], fill=g[4])
        else:
            d.line([(x, y), (x + w - 1, y), (x + w, y - 1)], fill=g[2])
            d.line([(x + 1, y + 1), (x + w - 1, y + 1)], fill=g[4])
            d.point((x - 1, y + 2), fill=g[2])
    if material != "minimal":
        for _ in range(300):
            side, along, depth = rng(4), rng(240), rng(26)
            x, y = ((along, depth) if side == 0 else (239 - depth, along)
                    if side == 1 else (along, 239 - depth) if side == 2
                    else (depth, along))
            w, h = 2 + rng(6), 1 + rng(3)
            d.polygon([(x - w, y), (x - 2, y - h), (x + w, y - h),
                       (x + w + 1, y), (x + 2, y + h), (x - w, y + h)],
                      fill=g[1 if depth < 12 else 2])
            d.line([(x - w + 1, y), (x - 1, y - h + 1), (x + 2, y - h + 1)],
                   fill=g[5 if depth < 12 else 4])
            if depth < 10:
                d.point((x + 1, y - h), fill=g[6])
    if material in ("sand", "shell", "desert"):
        for y in range(17, 240, 31):
            x = rng(45) - 25
            while x < 240:
                w = 14 + rng(20)
                d.line([(x, y + 2), (x + w // 3, y),
                        (x + w, y), (x + w + 4, y - 2)], fill=g[4])
                x += w + 12 + rng(24)
    if material in ("slate", "ruins"):
        for row in range(-1, 7):
            y = row * 43
            for col in range(-1, 6):
                x = col * 53 + (row % 2) * 25
                d.line([(x, y + 5), (x + 5, y), (x + 45, y),
                        (x + 51, y + 6), (x + 49, y + 35),
                        (x + 43, y + 40), (x + 5, y + 40),
                        (x, y + 34), (x, y + 5)], fill=g[2])
                d.line([(x + 6, y + 1), (x + 43, y + 1)], fill=g[4])
    for _ in range(58 if material != "minimal" else 16):
        side, along, depth = rng(4), rng(240), rng(18)
        x, y = ((along, depth) if side == 0 else (239 - depth, along)
                if side == 1 else (along, 239 - depth) if side == 2
                else (depth, along))
        if material in ("cork", "wood"):
            w, h = 4 + rng(7), 3 + rng(4)
            a = list(map(_rgba, p["accent"][1]))
            d.polygon([(x - w, y), (x - w + 3, y - h),
                       (x + w - 2, y - h - 1), (x + w, y),
                       (x + w - 3, y + h), (x - w + 1, y + h + 1)], fill=a[0])
            d.polygon([(x - w + 1, y), (x - w + 3, y - h + 1),
                       (x + w - 2, y - h), (x + w - 1, y),
                       (x + w - 4, y + h - 1)], fill=a[2])
            d.line([(x - w + 3, y - h + 1), (x + w - 3, y - h)], fill=a[4])
            d.line([(x - w + 3, y + 1), (x, y - 1), (x + w - 3, y)], fill=a[1])
            d.point((x + 1, y - h + 2), fill=a[3])
        elif material == "leaves":
            ramp = p["accent"][1 if rng(2) else 2]
            _leaf_shape(d, (x, y), (x + rng(17) - 8, y - 5 - rng(9)),
                        3 + rng(3), _rgba(ramp[2]), _rgba(ramp[4]),
                        _rgba(ramp[0]), g[0])
        else:
            _terrain_stone(d, x, y, 3 + rng(5), p["stone"])
    if material != "minimal":
        for x, y, r in ((10, 10, 12), (228, 230, 14), (237, 61, 8)):
            _terrain_stone(d, x + rng(9) - 4, y + rng(9) - 4, r, p["stone"])
    if fringe != "bare":
        # Asymmetric shallow banks, not a frame of repeated furniture.
        banks = ((9, 20, 18), (30, 3, 23), (228, 213, 21),
                 (213, 237, 24), (3, 174, 14))
        for x, y, r in banks:
            _moss_bank(d, x, y, r, p["leaf"], rng)
        for x, y, dx, dy in ((4, 24, 25, -16), (22, 3, 14, 25),
                             (236, 214, -26, 13), (212, 236, 9, -23)):
            if fringe == "fern":
                _frond(d, (x, y), (x + dx, y + dy), 3,
                       _rgba(p["leaf"][2]), _rgba(p["leaf"][5]),
                       _rgba(p["leaf"][0]), _rgba(p["leaf"][0]))
            else:
                _leaf_shape(d, (x, y), (x + dx, y + dy), 7,
                            _rgba(p["leaf"][2]), _rgba(p["leaf"][4]),
                            _rgba(p["leaf"][1]), _rgba(p["leaf"][0]))
    if material == "mineral":
        for x, y in ((12, 41), (221, 12), (234, 185), (36, 231)):
            a = list(map(_rgba, p["accent"][1]))
            d.polygon([(x - 7, y + 3), (x - 4, y - 5), (x + 5, y - 3),
                       (x + 8, y + 4), (x, y + 7)], fill=a[0])
            d.polygon([(x - 4, y - 4), (x, y + 1), (x + 4, y - 2)], fill=a[4])
            d.polygon([(x, y + 1), (x + 4, y - 2), (x + 6, y + 3)], fill=a[2])


def _bg_cork(d, p, rng):
    """Warm ochre bark mulch."""
    _ground(d, p, rng, "cork")


def _bg_moss(d, p, rng):
    """Dark peaty floor under moss."""
    _ground(d, p, rng, "moss")
    for x, y, r in ((0, 78, 16), (173, 0, 18), (239, 131, 12)):
        _moss_bank(d, x, y, r, p["leaf"], rng)


def _bg_leaf_litter(d, p, rng):
    """Rust autumn drift."""
    _ground(d, p, rng, "leaves", "bare")


def _bg_sand(d, p, rng):
    """Pale gold dunes."""
    _ground(d, p, rng, "sand", "bare")


def _bg_fern(d, p, rng):
    """Dark forest floor."""
    _ground(d, p, rng, "soil", "fern")


def _bg_orchid(d, p, rng):
    """Deep plum floor."""
    _ground(d, p, rng, "soil")
    for x, y in ((8, 24), (223, 224), (17, 178)):
        a = list(map(_rgba, p["accent"][0]))
        for dx, dy in ((-3, 0), (3, 0), (0, -3), (0, 3)):
            _blob(d, x + dx, y + dy, 3, 2, a[2], a[4], a[0])
        d.point((x, y), fill=_rgba(p["accent"][2][4]))


def _bg_woodland(d, p, rng):
    """Timber-brown forest floor."""
    _ground(d, p, rng, "wood")


def _bg_minimal(d, p, rng):
    """Bare pale gravel."""
    _ground(d, p, rng, "minimal", "bare")


def _bg_mushroom(d, p, rng):
    """Warm loam."""
    _ground(d, p, rng, "soil")
    for x, y in ((6, 62), (232, 198), (24, 234)):
        a = list(map(_rgba, p["accent"][1]))
        d.rectangle((x, y, x + 2, y + 4), fill=_rgba(p["accent"][0][3]))
        _blob(d, x + 1, y - 1, 4, 3, a[2], a[4], a[0])
        d.point((x, y - 2), fill=_rgba(p["accent"][0][4]))


def _bg_gothic(d, p, rng):
    """Violet flagstones."""
    _ground(d, p, rng, "slate")


def _bg_seashell(d, p, rng):
    """Shell sand."""
    _ground(d, p, rng, "shell", "bare")
    for x, y in ((4, 36), (230, 180), (21, 229), (181, 6)):
        a = list(map(_rgba, p["accent"][1]))
        d.polygon([(x - 4, y), (x - 2, y - 4), (x + 3, y - 4),
                   (x + 5, y), (x, y + 5)], fill=a[1])
        for dx in (-2, 0, 2):
            d.line([(x, y + 3), (x + dx, y - 3)], fill=a[4])


def _bg_succulent(d, p, rng):
    """Cool teal floor."""
    _ground(d, p, rng, "gravel", "bare")
    for x, y in ((6, 12), (232, 222), (12, 202)):
        for dx, dy in ((-8, -4), (0, -10), (8, -3), (0, 4)):
            _leaf_shape(d, (x, y), (x + dx, y + dy), 3,
                        _rgba(p["leaf"][2]), _rgba(p["leaf"][5]),
                        _rgba(p["leaf"][0]))


def _bg_willow(d, p, rng):
    """Dark bank under willow."""
    _ground(d, p, rng, "moss")
    water = list(map(_rgba, (0x285D63, 0x3D8584, 0x6CB7AF, 0xB5D9C3)))
    d.polygon([(0, 0), (59, 0), (54, 7), (40, 13), (38, 22),
               (24, 27), (18, 39), (0, 46)], fill=_rgba(p["leaf"][0]))
    d.polygon([(0, 0), (54, 0), (47, 7), (35, 11), (32, 20),
               (19, 24), (13, 35), (0, 40)], fill=water[1])
    for x, y in ((3, 8), (20, 5), (7, 23), (29, 13)):
        d.line([(x, y), (x + 7, y), (x + 9, y - 1)], fill=water[2])
        d.line([(x + 1, y + 3), (x + 4, y + 3)], fill=water[0])


def _bg_desert(d, p, rng):
    """Terracotta sand."""
    _ground(d, p, rng, "desert", "bare")
    for x, y in ((3, 16), (233, 226)):
        for dx in (-8, 0, 8):
            _leaf_shape(d, (x, y + 6), (x + dx, y - 9 + abs(dx)), 2,
                        _rgba(p["leaf"][2]), _rgba(p["leaf"][5]),
                        _rgba(p["leaf"][0]))


def _bg_crystal(d, p, rng):
    """Dark geode rock."""
    _ground(d, p, rng, "mineral", "bare")


def _bg_cemetery(d, p, rng):
    """Cold earth."""
    _ground(d, p, rng, "ruins")


def _bg_house(d, p, rng):
    """A snug timber room with an uncluttered playable floor."""
    _ground(d, p, rng, "wood")
    g = list(map(_rgba, p["ground"]))
    wood = list(map(_rgba, p["accent"][1]))
    d.rectangle((0, 0, 239, 8), fill=g[1])
    d.rectangle((0, 231, 239, 239), fill=g[1])
    d.rectangle((0, 0, 8, 239), fill=g[1])
    d.rectangle((231, 0, 239, 239), fill=g[1])
    for y in (3, 235):
        d.line([(0, y), (239, y)], fill=wood[3], width=2)
        d.line([(0, y + 3), (239, y + 3)], fill=wood[0])
    for x in (4, 235):
        d.line([(x, 0), (x, 239)], fill=wood[3], width=2)
    # A small inset hearth and window remain outside the clear central play area.
    d.rectangle((13, 14, 33, 32), fill=wood[0], outline=wood[4])
    d.rectangle((16, 17, 30, 29), fill=g[2], outline=wood[1])
    d.line([(18, 19), (28, 19)], fill=wood[4], width=2)
    d.line([(18, 25), (22, 23), (26, 25)], fill=wood[2])
    for x in (19, 25, 30, 208, 221):
        y = 18 if x < 40 else 222
        d.line([(x, y), (x + 2, y - 5), (x + 4, y)], fill=wood[2])
        d.point((x + 2, y - 6), fill=wood[4])


def _bg_dark_cave(d, p, rng):
    """A deep geode floor with crystalline walls gathered around the rim."""
    _ground(d, p, rng, "mineral", "bare")
    stone = p["stone"]
    for x, y, radius in ((9, 24, 12), (31, 7, 9), (229, 19, 12),
                         (8, 218, 14), (233, 227, 11), (212, 7, 8)):
        _terrain_stone(d, x, y, radius, stone)
    for x, y, color in ((8, 40, p["accent"][0][4]),
                        (225, 39, p["accent"][1][4]),
                        (21, 229, p["accent"][0][3]),
                        (229, 208, p["accent"][1][3])):
        d.polygon([(x, y - 7), (x + 4, y - 2), (x + 2, y + 5),
                   (x - 2, y + 3), (x - 3, y - 3)], fill=_rgba(color))
        d.line([(x, y - 6), (x, y + 2)], fill=_rgba(p["accent"][0][4]))


def _bg_night(d, p, rng):
    """A quiet midnight floor with fireflies and a thin corner moon."""
    _ground(d, p, rng, "slate", "bare")
    moon, star, dim = (list(map(_rgba, ramp)) for ramp in p["accent"])
    d.ellipse((9, 9, 34, 34), fill=dim[0])
    d.ellipse((7, 7, 30, 30), fill=moon[2])
    d.ellipse((11, 3, 32, 24), fill=_rgba(p["ground"][3]))
    d.line([(12, 12), (17, 9), (23, 9)], fill=moon[4], width=2)
    for x, y in ((51, 14), (92, 23), (163, 19), (202, 12),
                 (19, 71), (220, 93), (11, 177), (226, 176), (47, 220), (189, 225)):
        if x < 40 or x >= 200 or y < 40 or y >= 200:
            d.point((x, y), fill=star[4])
            d.line([(x - 2, y), (x + 2, y)], fill=star[2])
            d.line([(x, y - 2), (x, y + 2)], fill=star[2])
            d.point((x + 3, y + 2), fill=dim[4])


def _bg_autumn(d, p, rng):
    """Warm leaf drifts ring a broad, softly shaded russet clearing."""
    _ground(d, p, rng, "leaves", "bare")
    for x, y, index in ((14, 18, 1), (228, 16, 2), (17, 226, 2),
                        (224, 222, 1), (4, 103, 1), (235, 137, 2),
                        (67, 8, 1), (182, 232, 2)):
        ramp = list(map(_rgba, p["accent"][index]))
        for dx, dy, angle in ((-5, 0, -1), (0, 0, 1), (5, 3, -1)):
            _leaf_shape(d, (x + dx, y + dy), (x + dx + 5 * angle, y + dy - 7),
                        3, ramp[2], ramp[4], ramp[0])
    leaf = list(map(_rgba, p["leaf"]))
    for x, y in ((28, 42), (211, 53), (28, 198), (210, 192)):
        d.line([(x - 7, y), (x, y - 2), (x + 8, y)], fill=leaf[2], width=2)
        d.point((x - 3, y - 4), fill=leaf[4])


def _bg_snow(d, p, rng):
    """Pale packed snow with blue ice chips along the enclosure edges."""
    _ground(d, p, rng, "minimal", "bare")
    snow, ice = list(map(_rgba, p["ground"])), list(map(_rgba, p["stone"]))
    for x, y, radius in ((8, 16, 11), (36, 11, 8), (218, 18, 13),
                         (13, 228, 10), (228, 220, 12), (192, 235, 8)):
        _terrain_stone(d, x, y, radius, p["stone"])
    for y, reverse in ((17, False), (223, True)):
        points = [(0, y + 9), (18, y + 4), (32, y + 7), (40, y + 3),
                  (40, y - 2), (20, y - 4), (0, y)]
        if reverse:
            points = [(x, 239 - yy) for x, yy in points]
        d.polygon(points, fill=snow[1])
        d.line(points[:4], fill=snow[5], width=2)
    for x, y in ((4, 48), (235, 64), (12, 187), (226, 177)):
        d.line([(x - 5, y), (x, y - 3), (x + 4, y)], fill=snow[6], width=2)
        d.point((x - 2, y - 5), fill=snow[4])
        d.point((x + 6, y + 2), fill=ice[3])


def _bg_pond(d, p, rng):
    """A green pond bank with ripples confined to the far edge."""
    _ground(d, p, rng, "moss", "bare")
    water = list(map(_rgba, p["accent"][0]))
    d.polygon([(0, 213), (22, 210), (39, 216), (39, 239), (0, 239)], fill=water[1])
    d.polygon([(0, 226), (18, 220), (39, 226), (39, 239), (0, 239)], fill=water[0])
    d.line([(2, 217), (16, 215), (28, 218), (37, 217)], fill=water[4], width=2)
    for x, y in ((8, 224), (23, 231), (13, 236)):
        d.line([(x, y), (x + 8, y)], fill=water[3])
        d.line([(x + 2, y + 3), (x + 6, y + 3)], fill=water[2])
    leaf = list(map(_rgba, p["leaf"]))
    for x, y in ((8, 209), (22, 210), (232, 203), (224, 28), (13, 34)):
        d.line([(x, y + 14), (x + 1, y + 6), (x - 2, y)], fill=leaf[1], width=2)
        _leaf_shape(d, (x - 2, y + 7), (x - 8, y + 2), 3, leaf[2], leaf[5], leaf[0])
        _leaf_shape(d, (x + 1, y + 8), (x + 7, y + 1), 3, leaf[2], leaf[5], leaf[0])


def _bg_ruins(d, p, rng):
    """Weathered sandstone blocks and broken seams at the outer edge."""
    _ground(d, p, rng, "ruins", "bare")
    for x, y, radius in ((7, 23, 12), (231, 13, 10), (12, 225, 13),
                         (232, 228, 12), (4, 125, 8), (236, 107, 9)):
        _terrain_stone(d, x, y, radius, p["stone"])
    stone = list(map(_rgba, p["stone"]))
    for x, y in ((4, 43), (233, 52), (20, 200), (216, 206)):
        d.line([(x, y), (x + 3, y + 4), (x + 1, y + 9), (x + 6, y + 13)],
               fill=stone[0], width=2)
        d.line([(x + 1, y + 1), (x + 4, y + 5)], fill=stone[4])


def _bg_marsh(d, p, rng):
    """Olive peat with pools and cattails kept to the perimeter."""
    _ground(d, p, rng, "moss", "bare")
    water = list(map(_rgba, p["accent"][0]))
    for box in ((0, 18, 28, 33), (213, 203, 239, 221), (2, 225, 30, 239)):
        d.ellipse(box, fill=water[0])
        x0, y0, x1, y1 = box
        d.arc(box, 190, 350, fill=water[4], width=2)
        d.line([(x0 + 5, y1 - 4), (x0 + 11, y1 - 4)], fill=water[2])
    leaf = list(map(_rgba, p["leaf"]))
    for x, y, lean in ((8, 39, -5), (20, 42, 4), (229, 193, -4),
                       (216, 188, 5), (10, 211, 3), (232, 36, -4)):
        d.line([(x, y + 16), (x + lean // 2, y + 7), (x + lean, y)], fill=leaf[1], width=2)
        d.line([(x + lean, y + 3), (x + lean, y - 5)], fill=leaf[0], width=2)
        d.ellipse((x + lean - 2, y - 7, x + lean + 2, y - 3), fill=water[3])
        d.point((x + lean, y - 5), fill=water[4])


_BACKGROUND_SCENES = {
    "cork": _bg_cork,
    "moss": _bg_moss,
    "leaf_litter": _bg_leaf_litter,
    "sand": _bg_sand,
    "fern": _bg_fern,
    "orchid": _bg_orchid,
    "woodland": _bg_woodland,
    "minimal": _bg_minimal,
    "mushroom": _bg_mushroom,
    "gothic": _bg_gothic,
    "seashell": _bg_seashell,
    "succulent": _bg_succulent,
    "willow": _bg_willow,
    "desert": _bg_desert,
    "crystal": _bg_crystal,
    "cemetery": _bg_cemetery,
    "house": _bg_house,
    "dark_cave": _bg_dark_cave,
    "night": _bg_night,
    "autumn": _bg_autumn,
    "snow": _bg_snow,
    "pond": _bg_pond,
    "ruins": _bg_ruins,
    "marsh": _bg_marsh,
}


def draw_background(name):
    if name == "crystal":
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "assets", "sources", "crystal_floor.png")
        with Image.open(path) as source:
            source.load()
            if (source.format != "PNG" or source.mode != "RGBA" or source.size != (240, 240) or
                    hashlib.sha256(source.tobytes()).hexdigest() != APPROVED_CRYSTAL_PIXEL_SHA256):
                raise ValueError("Crystal source differs from the approved 240x240 RGBA floor")
            return source.copy()
    return terrain_art.background(name)


def _draw_procedural_background(name):
    """Legacy terrain retained for the earlier procedural drawing helpers."""
    if name in BACKGROUND_NAMES[24:]:
        return _draw_catalogue_background(name)
    p = BACKGROUND_PALETTES[name]
    img = Image.new("RGBA", (BG_SIZE, BG_SIZE), _rgba(p["ground"][3]))
    d = ImageDraw.Draw(img)
    rng = _rng(BACKGROUND_SEEDS[name])
    _BACKGROUND_SCENES[name](d, p, rng)
    return img


# Appended catalogue art uses three discrete material ramps and a shared ink.
# Legacy terrain helpers below no longer author the active background/frame set.
_CATALOGUE_RAMPS = (
    (0x594156, 0xAD7957, 0xE9C68E),
    (0x493A68, 0xA269AF, 0xF5BADD),
    (0x234D50, 0x579B83, 0xC0D99A),
)

_THEME_MATERIALS = {
    "woodland": ((0x49382E, 0x947049, 0xD4BA7D), (0x68423E, 0xB67B58, 0xF0C694),
                 (0x1B4F38, 0x4C8D42, 0xABCA70)),
    "fairy": ((0x654340, 0xB08467, 0xF0D8A1), (0x713C83, 0xC477BC, 0xFFD4E5),
              (0x245741, 0x68A66A, 0xD0EAA0)),
    "gothic": ((0x303947, 0x697A8A, 0xADBCC6), (0x3B2038, 0x812F4D, 0xD7758C),
               (0x393E56, 0x747A98, 0xC4CADC)),
    "academia": ((0x493025, 0x94603C, 0xD6A36B), (0x65502D, 0xAD8B48, 0xEDD998),
                 (0x233E41, 0x41676A, 0x91B6AC)),
    "household": ((0x6B6472, 0xB3C4CC, 0xF3F4E1), (0x61435E, 0xB77B9E, 0xF2C2D3),
                  (0x354C64, 0x6D9AAE, 0xB8DEE5)),
    "witchy": ((0x211C30, 0x43344F, 0x82718F), (0x48254F, 0x89538F, 0xDB9BD0),
               (0x244435, 0x5E8B43, 0xBFCE74)),
    "prehistoric": ((0x4B3126, 0x8B5933, 0xCFA06A), (0x8B421B, 0xD68B2F, 0xFFE09A),
                    (0x39452B, 0x718244, 0xC3BC75)),
    "cyberpunk": ((0x151F37, 0x344962, 0x819EB5), (0x642657, 0xCF419E, 0xFFADD9),
                  (0x15495A, 0x28BFC8, 0xB2FFFF)),
    "scifi": ((0x28374B, 0x718A9B, 0xD2E0E2), (0x222C53, 0x4F68AA, 0xA7BEF0),
               (0x19566A, 0x42BCCB, 0xC3FFFF)),
    "fantasy": ((0x45495E, 0x9396AE, 0xDFE1EB), (0x47245F, 0x8F50B2, 0xD6A2EF),
                (0x6C4B22, 0xC79C42, 0xFFE7A0)),
    "ancient": ((0x625540, 0xA89970, 0xE2D6A7), (0x315652, 0x528F83, 0xACD1AF),
                (0x74552B, 0xB79243, 0xF0D785)),
    "zen": ((0x434950, 0x929B98, 0xDDE0CA), (0x4C5933, 0x8C9952, 0xDBD69C),
            (0x334E35, 0x6E9360, 0xBBD294)),
    "water": ((0x3D5362, 0x7299AD, 0xC2E5E5), (0x794F61, 0xD39B9F, 0xFFE2CD),
              (0x205851, 0x559D83, 0xB8D5AD)),
    "mineral": ((0x3F4355, 0x79778E, 0xBFC0CE), (0x493F7A, 0x9681C7, 0xE2C8FF),
                (0x266374, 0x62B6BD, 0xBCF4EB)),
    "desert": ((0x78523B, 0xBC9662, 0xF1DDA2), (0x664335, 0xA36E4A, 0xDFAB72),
               (0x43583D, 0x7C9462, 0xC7CB84)),
}

_THEME_KEYS = {
    "woodland": "cork moss leaf_litter fern woodland succulent willow autumn fern_nest",
    "fairy": "fairy_ring foxglove_lantern pixie_door fairy_bridge fairy_cottage petal_swing "
             "fairy_glade moonflower_grove orchid",
    "gothic": "gargoyle iron_gate rose_urn moon_bed velvet_throne gothic cemetery cathedral rose_courtyard",
    "academia": "book_stack globe quill_desk reading_nook scholar_perch library observatory",
    "household": "tea_cup spool thimble teapot_hide house kitchen sewing_room",
    "witchy": "cauldron broom potion_rack witch_hat potion_hide witch_garden moon_altar moon",
    "prehistoric": "amber_fossil cycad dinosaur_egg fossil_cradle primeval_jungle amber_basin amber",
    "cyberpunk": "neon_sign holo_terminal cable_coil neon_den holo_garden neon_alley circuit_garden neon",
    "scifi": "moon_rover solar_array oxygen_pod star_capsule lunar_base starship star",
    "fantasy": "wizard_tower dragon_statue treasure_chest dragon_roost enchanted_court dragon_heath "
               "cloud_sanctuary night",
    "ancient": "sun_obelisk glyph_tablet broken_column sun_temple lost_city sunken_temple ruins rune",
    "zen": "stone_lantern bamboo_fountain bonsai zen_pavilion torii_retreat zen_garden bamboo_grove "
           "sakura_courtyard lotus",
    "water": "seashell pond marsh pollen",
    "mineral": "crystal dark_cave snow",
    "desert": "sand desert minimal",
}


def _material_family(name):
    return next((family for family, keys in _THEME_KEYS.items() if name in keys.split()),
                "woodland")


def _catalogue_canvas(size):
    image = Image.new("RGBA", (size, size))
    return image, ImageDraw.Draw(image), [_rgba(r[1]) for r in _CATALOGUE_RAMPS]


def _catalogue_finish(image, family=None, name=None, kind=None):
    """Shade only material pixels; preserve ink, silhouette and binary alpha."""
    if name is not None:
        return _enlarge(furnishing_art.catalogue(
            image, name, kind, _CATALOGUE_RAMPS, _THEME_MATERIALS[family], INK))
    for ramp in _CATALOGUE_RAMPS:
        body = _rgba(ramp[1])
        points = [(x, y) for y in range(image.height) for x in range(image.width)
                  if image.getpixel((x, y)) == body]
        if not points:
            continue
        x0, y0 = min(x for x, _ in points), min(y for _, y in points)
        x1, y1 = max(x for x, _ in points), max(y for _, y in points)
        span = max(1, x1 - x0 + y1 - y0)
        for x, y in points:
            phase = (x - x0 + y - y0) / span
            tone = 2 if phase < 0.34 else 0 if phase > 0.72 else 1
            # A few broken highlights read as woven grain, stone chips or glaze.
            if (x + 3 * y) % 11 == 0:
                tone = 2
            image.putpixel((x, y), _rgba(ramp[tone]))
    if family:
        translated = {
            _rgba(old): _rgba(new)
            for old_ramp, new_ramp in zip(_CATALOGUE_RAMPS, _THEME_MATERIALS[family])
            for old, new in zip(old_ramp, new_ramp)
        }
        pixels = image.load()
        image.putdata([
            translated.get(pixels[x, y], pixels[x, y])
            for y in range(image.height) for x in range(image.width)
        ])
    return _enlarge(image)


def _panel(d, box, material):
    d.rectangle(box, fill=material, outline=INK)


def _oval(d, box, material):
    d.ellipse(box, fill=material, outline=INK)


def _poly(d, points, material):
    d.polygon(points, fill=material, outline=INK)


def _draw_catalogue_decor(name):
    image, d, (wood, gem, leaf) = _catalogue_canvas(32)
    if name == "fairy_ring":
        d.ellipse((2, 14, 29, 29), outline=leaf, width=3)
        for x, y in ((5, 17), (14, 12), (25, 16), (22, 24), (10, 25)):
            _panel(d, (x, y, x + 2, y + 5), wood)
            _oval(d, (x - 3, y - 3, x + 5, y + 1), gem)
    elif name == "foxglove_lantern":
        d.line((15, 29, 17, 4), fill=leaf, width=3)
        for x, y in ((10, 8), (18, 12), (9, 19)):
            _poly(d, [(x, y), (x + 6, y), (x + 7, y + 7),
                      (x - 1, y + 7)], gem)
            _oval(d, (x, y + 4, x + 6, y + 8), wood)
        _poly(d, [(16, 27), (24, 21), (26, 23), (20, 28)], leaf)
    elif name == "pixie_door":
        _oval(d, (6, 3, 26, 29), leaf)
        _oval(d, (9, 6, 24, 29), wood)
        _panel(d, (9, 17, 24, 29), wood)
        d.line((16, 8, 16, 28), fill=INK)
        _oval(d, (19, 20, 22, 23), gem)
    elif name == "gargoyle":
        _poly(d, [(13, 15), (3, 5), (2, 23), (12, 21)], gem)
        _poly(d, [(20, 15), (29, 5), (30, 23), (20, 21)], gem)
        _panel(d, (7, 26, 26, 30), wood)
        _oval(d, (10, 13, 23, 28), wood)
        _poly(d, [(10, 4), (15, 7), (21, 4), (23, 16), (16, 20), (9, 15)], leaf)
        d.point((13, 12), fill=INK)
        d.point((20, 12), fill=INK)
    elif name == "iron_gate":
        for x in (4, 10, 16, 22, 28):
            _poly(d, [(x, 3), (x - 2, 8), (x - 1, 8), (x - 1, 29),
                      (x + 1, 29), (x + 1, 8), (x + 2, 8)], wood)
        _panel(d, (3, 13, 29, 16), leaf)
        _panel(d, (3, 23, 29, 25), gem)
    elif name == "rose_urn":
        _poly(d, [(7, 15), (25, 15), (22, 24), (18, 26), (21, 29),
                  (11, 29), (14, 26), (10, 24)], wood)
        for x, y in ((8, 8), (17, 4), (23, 9)):
            d.line((16, 17, x, y + 2), fill=leaf, width=3)
            _oval(d, (x - 4, y - 3, x + 3, y + 3), gem)
            d.line((x - 1, y + 1, x + 1, y - 1), fill=INK)
    elif name == "book_stack":
        for box, color in (((4, 23, 27, 29), wood), ((8, 16, 29, 22), leaf),
                           ((3, 8, 24, 15), gem)):
            _panel(d, box, color)
            x, y, right, _ = box
            d.line((x + 3, y + 3, right - 2, y + 3), fill=wood)
            d.line((x + 2, y + 1, x + 2, y + 5), fill=INK)
    elif name == "globe":
        d.arc((4, 2, 27, 25), 230, 70, fill=gem, width=3)
        _oval(d, (7, 4, 24, 21), leaf)
        _poly(d, [(11, 6), (19, 7), (16, 11), (20, 15), (15, 18),
                  (13, 12), (9, 11)], wood)
        _panel(d, (15, 23, 18, 28), gem)
        _oval(d, (8, 27, 25, 30), wood)
    elif name == "quill_desk":
        _panel(d, (3, 17, 29, 22), wood)
        for x in (5, 24):
            _panel(d, (x, 22, x + 3, 30), wood)
        _panel(d, (7, 13, 18, 17), leaf)
        _oval(d, (21, 12, 27, 17), gem)
        _poly(d, [(23, 13), (21, 4), (28, 1), (29, 7)], gem)
        d.line((23, 15, 27, 4), fill=wood)
    elif name == "tea_cup":
        _oval(d, (22, 11, 30, 23), wood)
        _oval(d, (24, 14, 27, 20), leaf)
        _oval(d, (2, 24, 28, 30), gem)
        _poly(d, [(5, 10), (25, 10), (22, 25), (10, 25)], wood)
        _oval(d, (5, 7, 25, 14), leaf)
        d.line((10, 18, 13, 20, 17, 17), fill=gem, width=2)
    elif name == "spool":
        _panel(d, (9, 7, 24, 25), gem)
        for y in (10, 14, 18, 22):
            d.line((10, y, 23, y + 1), fill=leaf)
        _oval(d, (5, 3, 27, 10), wood)
        _oval(d, (5, 23, 27, 30), wood)
        _oval(d, (13, 4, 19, 7), gem)
    elif name == "thimble":
        _poly(d, [(7, 27), (9, 7), (13, 3), (21, 3), (25, 7), (27, 27)], leaf)
        for y in (9, 14, 19):
            for x in (12, 17, 22):
                _panel(d, (x, y, x + 2, y + 2), gem)
        _panel(d, (6, 25, 28, 29), wood)
    elif name == "cauldron":
        _oval(d, (3, 10, 29, 28), gem)
        _oval(d, (4, 8, 28, 15), leaf)
        for x in (7, 23):
            _panel(d, (x, 26, x + 3, 30), wood)
        d.arc((3, 3, 28, 25), 185, 355, fill=wood, width=2)
        for box in ((12, 2, 16, 6), (20, 5, 23, 8)):
            _oval(d, box, leaf)
    elif name == "broom":
        _poly(d, [(25, 1), (28, 3), (14, 22), (11, 20)], wood)
        _poly(d, [(11, 16), (19, 21), (14, 30), (2, 25)], leaf)
        for x in (5, 9, 13):
            d.line((x, 25, 13, 20), fill=wood)
        d.line((9, 19, 17, 24), fill=gem, width=3)
    elif name == "potion_rack":
        _panel(d, (3, 4, 6, 29), wood)
        _panel(d, (26, 4, 29, 29), wood)
        for y in (14, 28):
            _panel(d, (3, y, 29, y + 2), wood)
            for x in (9, 20):
                _oval(d, (x - 2, y - 8, x + 4, y - 1), leaf if x == 9 else gem)
                _panel(d, (x, y - 11, x + 2, y - 7), wood)
    elif name == "amber_fossil":
        _poly(d, [(8, 3), (23, 5), (29, 15), (25, 27), (10, 30), (3, 20)], wood)
        _oval(d, (10, 10, 21, 22), gem)
        d.line((12, 12, 23, 24), fill=leaf, width=2)
        for y in (14, 18, 22):
            d.line((10, y + 2, 16, y, 22, y - 2), fill=INK)
    elif name == "cycad":
        _panel(d, (13, 16, 19, 30), wood)
        for tip in ((2, 9), (7, 2), (17, 1), (28, 5), (30, 14), (3, 20)):
            _poly(d, [(16, 18), tip, (tip[0], tip[1] + 4)], leaf)
            d.line((16, 18, *tip), fill=gem)
            for step in (2, 4, 6):
                x = 16 + (tip[0] - 16) * step // 8
                y = 18 + (tip[1] - 18) * step // 8
                d.line((x - 2, y + 1, x, y, x + 2, y - 2), fill=leaf)
    elif name == "dinosaur_egg":
        _oval(d, (2, 23, 30, 30), leaf)
        _oval(d, (8, 2, 25, 28), wood)
        for box in ((12, 7, 16, 11), (19, 15, 23, 20), (11, 21, 15, 24)):
            _oval(d, box, gem)
        d.line((16, 13, 13, 16, 17, 19), fill=INK)
    elif name == "neon_sign":
        _panel(d, (5, 3, 27, 25), wood)
        _panel(d, (8, 6, 24, 22), gem)
        d.line((11, 19, 11, 10, 16, 16, 21, 9, 21, 19), fill=leaf, width=2)
        _panel(d, (14, 25, 18, 30), leaf)
    elif name == "holo_terminal":
        _poly(d, [(5, 29), (9, 22), (25, 22), (29, 29)], wood)
        _panel(d, (7, 4, 25, 19), leaf)
        _panel(d, (11, 8, 20, 12), gem)
        d.line((11, 16, 21, 16), fill=gem)
        d.line((12, 25, 23, 25), fill=leaf, width=2)
    elif name == "cable_coil":
        for box in ((3, 8, 24, 25), (7, 11, 20, 22), (10, 14, 17, 19)):
            d.ellipse(box, outline=INK, width=4)
            d.ellipse(tuple(v + (1 if i < 2 else -1) for i, v in enumerate(box)),
                      outline=gem, width=2)
        d.line((20, 23, 28, 28, 29, 17), fill=wood, width=3)
        _panel(d, (26, 11, 31, 17), leaf)
    elif name == "moon_rover":
        _poly(d, [(3, 13), (20, 13), (27, 21), (7, 24)], wood)
        for x in (7, 23):
            _oval(d, (x - 4, 22, x + 4, 30), gem)
        _panel(d, (10, 7, 21, 16), leaf)
        d.line((15, 7, 15, 2, 23, 2), fill=wood, width=2)
        _oval(d, (23, 1, 27, 5), leaf)
    elif name == "solar_array":
        _panel(d, (14, 17, 18, 29), wood)
        _panel(d, (8, 28, 24, 30), wood)
        _poly(d, [(3, 4), (25, 4), (30, 20), (8, 20)], leaf)
        for x in (8, 14, 20):
            d.line((x, 6, x + 4, 18), fill=gem, width=2)
        d.line((6, 12, 26, 12), fill=wood)
    elif name == "oxygen_pod":
        _oval(d, (7, 2, 25, 29), leaf)
        _panel(d, (8, 22, 24, 29), wood)
        _oval(d, (11, 6, 21, 20), gem)
        d.line((16, 8, 16, 17), fill=leaf, width=2)
        d.line((13, 12, 19, 12), fill=leaf, width=2)
    elif name == "wizard_tower":
        _panel(d, (9, 13, 24, 29), wood)
        _poly(d, [(5, 14), (16, 1), (28, 14)], gem)
        _panel(d, (14, 21, 19, 29), leaf)
        _oval(d, (13, 14, 20, 19), leaf)
        d.line((18, 2, 25, 4, 20, 6), fill=wood)
    elif name == "dragon_statue":
        _panel(d, (4, 27, 29, 30), wood)
        _poly(d, [(12, 25), (6, 13), (3, 6), (16, 13), (23, 7),
                  (20, 18), (28, 24), (23, 26)], gem)
        _poly(d, [(13, 26), (16, 16), (14, 9), (18, 3), (26, 6),
                  (29, 10), (22, 12), (22, 19), (25, 26)], leaf)
        d.point((22, 7), fill=INK)
    elif name == "treasure_chest":
        _oval(d, (3, 7, 28, 23), wood)
        _panel(d, (3, 16, 28, 29), wood)
        for x in (7, 23):
            _panel(d, (x, 10, x + 2, 28), leaf)
        _panel(d, (13, 15, 19, 22), gem)
        d.point((16, 19), fill=INK)
    elif name == "sun_obelisk":
        _poly(d, [(12, 5), (17, 1), (22, 5), (25, 27), (9, 27)], wood)
        _panel(d, (6, 27, 28, 30), leaf)
        _oval(d, (13, 9, 21, 17), gem)
        d.line((17, 19, 17, 24), fill=gem, width=2)
    elif name == "glyph_tablet":
        _poly(d, [(5, 4), (24, 2), (28, 7), (25, 29), (4, 27)], wood)
        for x, y in ((9, 9), (18, 8), (8, 19), (18, 18)):
            d.line((x, y, x + 4, y + 2, x + 1, y + 6), fill=gem, width=2)
        d.line((5, 24, 10, 25, 12, 28), fill=leaf, width=2)
    elif name == "broken_column":
        _panel(d, (5, 26, 28, 30), leaf)
        _poly(d, [(10, 26), (10, 9), (14, 12), (18, 5), (22, 9),
                  (24, 26)], wood)
        for x in (14, 18, 21):
            d.line((x, 14, x, 24), fill=gem)
        _poly(d, [(2, 22), (6, 19), (9, 24), (7, 27)], gem)
    elif name == "stone_lantern":
        _panel(d, (14, 17, 19, 27), wood)
        _panel(d, (9, 27, 24, 30), leaf)
        _panel(d, (9, 9, 24, 19), wood)
        _panel(d, (13, 11, 20, 17), gem)
        _poly(d, [(4, 10), (11, 5), (16, 2), (22, 5), (29, 10)], leaf)
    elif name == "bamboo_fountain":
        _oval(d, (3, 21, 30, 30), wood)
        _oval(d, (6, 23, 27, 28), gem)
        _panel(d, (21, 3, 26, 24), leaf)
        _poly(d, [(7, 10), (26, 7), (27, 12), (8, 15)], leaf)
        d.line((10, 16, 10, 24), fill=gem, width=2)
        d.line((22, 17, 25, 17), fill=INK)
    elif name == "bonsai":
        _poly(d, [(5, 24), (28, 24), (25, 30), (8, 30)], gem)
        d.line((16, 24, 13, 18, 19, 12, 17, 6), fill=wood, width=4)
        for box in ((2, 10, 16, 17), (14, 4, 29, 12), (7, 2, 22, 9)):
            _oval(d, box, leaf)
    elif name == "fairy_bridge":
        d.arc((2, 12, 30, 38), 185, 355, fill=INK, width=9)
        d.arc((3, 14, 29, 37), 185, 355, fill=wood, width=5)
        for x, y in ((4, 21), (10, 15), (22, 15), (28, 21)):
            _panel(d, (x - 1, y - 7, x + 1, y + 2), leaf)
            _oval(d, (x - 2, y - 9, x + 2, y - 5), gem)
        d.arc((3, 5, 29, 32), 185, 355, fill=leaf, width=2)
    else:
        raise ValueError(f"Unknown appended decor: {name}")
    return _catalogue_finish(image, _material_family(name), name, "decor")


def _draw_catalogue_gift(name):
    image, d, (wood, gem, leaf) = _catalogue_canvas(16)
    if name == "fairy_cottage":
        _panel(d, (3, 7, 13, 14), wood)
        _poly(d, [(1, 8), (7, 1), (15, 8)], leaf)
        _oval(d, (6, 9, 10, 15), gem)
    elif name == "moon_bed":
        _oval(d, (2, 1, 13, 13), gem)
        d.ellipse((6, 0, 15, 10), fill=(0, 0, 0, 0))
        _panel(d, (2, 11, 14, 14), wood)
        _panel(d, (5, 10, 12, 12), leaf)
    elif name == "reading_nook":
        _panel(d, (1, 2, 5, 14), wood)
        for y in (4, 8, 12):
            d.line((2, y, 4, y), fill=leaf)
        _panel(d, (7, 6, 13, 13), gem)
        _panel(d, (6, 11, 15, 14), leaf)
        _panel(d, (10, 1, 12, 5), wood)
    elif name == "teapot_hide":
        _oval(d, (10, 4, 15, 11), leaf)
        _poly(d, [(4, 8), (0, 4), (1, 11), (5, 12)], wood)
        _oval(d, (3, 4, 13, 14), wood)
        _panel(d, (5, 3, 11, 5), leaf)
        _oval(d, (6, 8, 10, 14), gem)
    elif name == "witch_hat":
        _poly(d, [(3, 11), (7, 1), (11, 2), (9, 4), (13, 12)], gem)
        _oval(d, (0, 10, 15, 14), gem)
        _panel(d, (5, 8, 12, 10), leaf)
        _panel(d, (7, 8, 9, 11), wood)
    elif name == "fern_nest":
        _oval(d, (2, 8, 14, 14), wood)
        _oval(d, (4, 9, 12, 12), gem)
        for x, y in ((1, 3), (7, 1), (14, 4)):
            _poly(d, [(7, 10), (x, y), (x + 1, y + 5)], leaf)
    elif name == "neon_den":
        _poly(d, [(1, 14), (1, 5), (5, 1), (14, 1), (14, 14)], wood)
        _panel(d, (4, 4, 11, 14), gem)
        d.line((2, 12, 2, 5, 6, 2, 13, 2), fill=leaf, width=2)
    elif name == "star_capsule":
        _oval(d, (3, 1, 13, 14), leaf)
        _panel(d, (2, 11, 14, 14), wood)
        _oval(d, (5, 4, 11, 11), gem)
        d.line((8, 5, 8, 9), fill=wood)
        d.line((6, 7, 10, 7), fill=wood)
    elif name == "dragon_roost":
        _poly(d, [(1, 8), (1, 1), (7, 6), (14, 1), (14, 9)], leaf)
        _oval(d, (2, 8, 14, 14), wood)
        _oval(d, (5, 9, 11, 12), gem)
        d.line((3, 12, 7, 14, 13, 11), fill=leaf)
    elif name == "sun_temple":
        _panel(d, (1, 12, 14, 15), wood)
        for x in (3, 11):
            _panel(d, (x, 5, x + 2, 12), leaf)
        _poly(d, [(0, 5), (7, 0), (15, 5)], wood)
        _oval(d, (5, 2, 10, 6), gem)
    elif name == "zen_pavilion":
        for x in (3, 12):
            _panel(d, (x, 6, x + 1, 13), wood)
        _poly(d, [(0, 7), (4, 4), (7, 1), (11, 4), (15, 7)], leaf)
        _panel(d, (1, 13, 15, 15), wood)
        _panel(d, (6, 9, 10, 12), gem)
    elif name == "petal_swing":
        d.line((2, 1, 13, 1), fill=wood, width=2)
        for x in (3, 12):
            d.line((x, 2, x, 10), fill=leaf)
        _oval(d, (2, 8, 13, 14), gem)
        _poly(d, [(7, 11), (4, 8), (7, 5), (10, 8)], leaf)
    elif name == "velvet_throne":
        _poly(d, [(3, 11), (3, 1), (7, 3), (12, 1), (12, 11)], wood)
        _panel(d, (5, 5, 10, 11), gem)
        _panel(d, (1, 10, 14, 13), leaf)
        for x in (3, 11):
            _panel(d, (x, 13, x + 1, 15), wood)
    elif name == "scholar_perch":
        _panel(d, (7, 5, 9, 13), wood)
        _panel(d, (2, 12, 14, 15), wood)
        _poly(d, [(1, 3), (7, 1), (8, 3), (14, 1), (14, 7),
                  (8, 8), (2, 7)], leaf)
        d.line((8, 3, 8, 7), fill=gem, width=2)
        d.line((3, 4, 6, 3), fill=gem)
    elif name == "potion_hide":
        _panel(d, (5, 1, 11, 4), wood)
        _oval(d, (1, 5, 15, 15), leaf)
        _panel(d, (5, 9, 11, 15), gem)
        d.line((3, 7, 5, 5, 11, 5), fill=wood)
    elif name == "fossil_cradle":
        _oval(d, (1, 8, 15, 15), wood)
        _oval(d, (4, 7, 12, 12), gem)
        for x, y in ((2, 2), (7, 0), (12, 3)):
            d.arc((x, y, x + 3, 13), 90, 280, fill=leaf, width=2)
    elif name == "holo_garden":
        _poly(d, [(1, 11), (10, 8), (15, 12), (7, 15)], wood)
        _panel(d, (4, 2, 12, 9), gem)
        d.line((8, 9, 8, 3), fill=leaf, width=2)
        d.line((5, 4, 8, 6, 11, 3), fill=leaf, width=2)
    elif name == "torii_retreat":
        for x in (3, 11):
            _panel(d, (x, 4, x + 1, 14), wood)
        _poly(d, [(0, 1), (4, 3), (11, 3), (15, 1), (14, 5), (1, 5)], gem)
        _panel(d, (2, 7, 13, 9), wood)
        _oval(d, (5, 11, 10, 15), leaf)
    else:
        raise ValueError(f"Unknown appended gift: {name}")
    return _catalogue_finish(image, _material_family(name), name, "gift")


def draw_treat(name):
    """Fictional confectionery, never a depiction of real spider nutrition."""
    image, d, (wood, gem, leaf) = _catalogue_canvas(24)
    if name == "hunger":  # Juicy Fly, a fictional game reward.
        for box in ((3, 3, 11, 14), (12, 3, 21, 14)):
            _oval(d, box, leaf)
        for y in (11, 15, 19):
            d.line((3, y + 2, 9, y, 15, y, 21, y + 2), fill=wood, width=2)
        _oval(d, (8, 8, 16, 22), wood)
        _oval(d, (8, 3, 16, 11), gem)
        d.point((10, 6), fill=INK)
        d.point((14, 6), fill=INK)
    elif name == "mood":  # Silk Ribbon
        _poly(d, [(10, 11), (3, 4), (1, 7), (3, 15), (10, 14)], gem)
        _poly(d, [(14, 11), (21, 4), (23, 7), (21, 15), (14, 14)], gem)
        _poly(d, [(10, 13), (4, 22), (9, 20), (12, 22), (13, 14)], leaf)
        _poly(d, [(13, 13), (15, 22), (18, 20), (22, 22), (15, 12)], leaf)
        _oval(d, (9, 9, 15, 16), wood)
    elif name == "energy":  # Warm Pebble
        _oval(d, (3, 10, 21, 21), wood)
        _poly(d, [(6, 13), (10, 10), (16, 11), (12, 15)], leaf)
        _poly(d, [(14, 16), (20, 14), (18, 19), (12, 20)], gem)
        for x in (6, 12, 18):
            d.line((x, 7, x - 1, 5, x + 1, 2), fill=gem, width=2)
    elif name == "dew":  # Honey Moth
        _poly(d, [(10, 8), (3, 2), (1, 9), (5, 14), (2, 20),
                  (10, 18), (12, 12)], wood)
        _poly(d, [(14, 8), (21, 2), (23, 9), (19, 14), (22, 20),
                  (14, 18), (12, 12)], wood)
        for box in ((4, 7, 8, 12), (16, 7, 20, 12)):
            _oval(d, box, gem)
        _oval(d, (10, 7, 14, 21), leaf)
        d.line((11, 7, 8, 2), fill=leaf)
        d.line((13, 7, 16, 2), fill=leaf)
    elif name == "pollen":  # Dew Drop
        _poly(d, [(12, 1), (20, 12), (20, 18), (15, 22), (8, 22),
                  (4, 18), (4, 12)], leaf)
        _oval(d, (7, 11, 12, 17), gem)
        _poly(d, [(13, 17), (18, 15), (17, 20), (13, 21)], wood)
    elif name == "silk":  # Cozy Nest
        _oval(d, (1, 9, 23, 21), wood)
        _oval(d, (4, 10, 20, 17), gem)
        for y in (16, 19):
            d.line((3, y, 8, y + 2, 15, y, 21, y + 1), fill=leaf, width=2)
        _poly(d, [(12, 13), (7, 6), (11, 2), (16, 5), (17, 11)], leaf)
        d.line((10, 5, 14, 11), fill=wood)
    elif name == "moon":
        _oval(d, (3, 16, 21, 22), wood)
        _poly(d, [(7, 18), (5, 12), (7, 8), (11, 7), (11, 4), (16, 8),
                  (18, 17)], gem)
        d.arc((8, 6, 18, 15), 60, 260, fill=leaf, width=3)
    elif name == "amber":
        _poly(d, [(7, 3), (16, 2), (21, 10), (18, 20), (8, 22),
                  (2, 14)], wood)
        _poly(d, [(8, 9), (11, 6), (16, 11), (12, 16), (7, 14)], gem)
        d.line((7, 18, 17, 6), fill=leaf, width=2)
    elif name == "neon":
        _panel(d, (3, 6, 21, 21), gem)
        _poly(d, [(3, 6), (8, 2), (22, 2), (21, 6)], leaf)
        _poly(d, [(13, 7), (8, 14), (13, 14), (11, 19), (18, 11),
                  (13, 11)], wood)
    elif name == "star":
        _poly(d, [(12, 1), (15, 8), (23, 9), (17, 14), (19, 22),
                  (12, 18), (5, 22), (7, 14), (1, 9), (9, 8)], wood)
        _oval(d, (8, 9, 16, 17), gem)
        d.line((9, 12, 12, 15, 15, 11), fill=leaf, width=2)
    elif name == "lotus":
        _panel(d, (8, 15, 16, 22), wood)
        for points in ([(12, 16), (2, 12), (3, 5), (10, 9)],
                       [(12, 16), (22, 12), (21, 5), (14, 9)],
                       [(8, 12), (12, 1), (16, 12), (12, 17)]):
            _poly(d, points, gem)
        _oval(d, (9, 10, 15, 16), leaf)
    elif name == "rune":
        _poly(d, [(5, 3), (18, 3), (22, 8), (21, 19), (16, 22),
                  (4, 20), (2, 9)], wood)
        d.line((8, 17, 8, 7, 16, 11, 8, 13), fill=gem, width=3)
        _oval(d, (15, 15, 19, 19), leaf)
    else:
        raise ValueError(f"Unknown fictional treat: {name}")
    return _catalogue_finish(image, _material_family(name), name, "treat")


def _draw_catalogue_background(name):
    """Architectural/organic edge compositions around a three-tone play floor."""
    index = BACKGROUND_NAMES.index(name) - 24
    grounds = (
        (0x354A42, 0x3D5249, 0x465A50), (0x39394D, 0x414155, 0x49495D),
        (0x42404B, 0x4A4853, 0x52505B), (0x58484C, 0x605054, 0x68585C),
        (0x574636, 0x5F4E3E, 0x675646), (0x343B50, 0x3C4358, 0x444B60),
        (0x6B6050, 0x736858, 0x7B7060), (0x594B59, 0x615361, 0x695B69),
        (0x39463A, 0x414E42, 0x49564A), (0x3C394B, 0x444153, 0x4C495B),
        (0x354335, 0x3D4B3D, 0x455345), (0x63503A, 0x6B5842, 0x73604A),
        (0x303C4D, 0x384455, 0x404C5D), (0x304A48, 0x385250, 0x405A58),
        (0x565866, 0x5E606E, 0x666876), (0x354453, 0x3D4C5B, 0x455463),
        (0x59525E, 0x615A66, 0x69626E), (0x55464B, 0x5D4E53, 0x65565B),
        (0x5F5744, 0x675F4C, 0x6F6754), (0x345054, 0x3C585C, 0x446064),
        (0x6A675B, 0x726F63, 0x7A776B), (0x3E5040, 0x465848, 0x4E6050),
        (0x62545A, 0x6A5C62, 0x72646A), (0x686475, 0x706C7D, 0x787485),
    )
    ground = list(map(_rgba, grounds[index]))
    image = Image.new("RGBA", (240, 240), ground[1])
    d = ImageDraw.Draw(image)
    wood, gem, leaf = (_rgba(r[1]) for r in _CATALOGUE_RAMPS)
    dark, light = _rgba(0x252A3C), _rgba(0xDACCA6)

    def masonry(box, step=18):
        x0, y0, x1, y1 = box
        d.rectangle(box, fill=dark)
        for y in range(y0, y1, 10):
            for x in range(x0 - (step // 2 if (y - y0) // 10 % 2 else 0), x1, step):
                left, right = max(x0, x), min(x1, x + step - 2)
                if left < right:
                    d.rectangle((left, y + 1, right, min(y1, y + 8)), fill=wood)
                    d.line((left, y + 1, right, y + 1), fill=light)

    def flower(x, y, radius=7):
        for dx, dy in ((-radius, 0), (radius, 0), (0, -radius), (0, radius)):
            _oval(d, (x + dx - 5, y + dy - 5, x + dx + 5, y + dy + 5), gem)
        _oval(d, (x - 4, y - 4, x + 4, y + 4), light)

    def frond(x, y, height=28):
        d.line((x, y, x + 2, y - height), fill=wood, width=3)
        for step in range(3, height, 5):
            for side in (-1, 1):
                _poly(d, [(x + 1, y - step), (x + side * (height - step) // 2,
                           y - step - 7), (x + side * 4, y - step + 2)], leaf)

    def arch(x, width=30):
        _oval(d, (x, 3, x + width, 34), wood)
        _oval(d, (x + 5, 7, x + width - 5, 36), gem)
        d.line((x + width // 2, 8, x + width // 2, 35), fill=light, width=2)

    def circuit(box, pitch):
        x0, y0, x1, y1 = box
        for x in range(x0, x1 - 4, pitch):
            d.line((x, y1, x, y0 + 8, x + 5, y0 + 3, x + 5, y0), fill=leaf, width=2)
            _panel(d, (x - 2, y1 - 6, x + 3, y1 - 1), gem)

    if name in ("fairy_glade", "moonflower_grove"):
        for x, y in ((16, 29), (220, 29), (18, 233), (225, 228)):
            frond(x, y, 27)
            flower(x + 2, y - 14, 5)
        if name == "fairy_glade":
            for x in range(45, 202, 27):
                _panel(d, (x, 18, x + 4, 34), wood)
                _oval(d, (x - 6, 10, x + 10, 22), gem)
            d.arc((28, 203, 208, 256), 190, 350, fill=leaf, width=4)
        else:
            d.ellipse((85, 2, 136, 34), fill=light)
            d.ellipse((101, 0, 144, 26), fill=ground[1])
            for x in (52, 163, 187):
                flower(x, 221, 6)
    elif name == "cathedral":
        masonry((0, 0, 239, 37))
        for x in (17, 74, 132, 190):
            arch(x)
        for x in (3, 224):
            _panel(d, (x, 42, x + 11, 198), wood)
            for y in (55, 112, 170):
                _panel(d, (x - 2, y, x + 13, y + 5), light)
    elif name == "rose_courtyard":
        masonry((0, 0, 239, 18))
        masonry((0, 221, 239, 239))
        for x in (17, 222):
            d.line((x, 210, x, 28), fill=leaf, width=4)
            for y in (35, 75, 120, 163, 203):
                flower(x, y, 5)
    elif name == "library":
        for y in (1, 218):
            _panel(d, (0, y, 239, y + 20), wood)
            for x in range(4, 235, 9):
                h = 9 + (x * 3) % 8
                _panel(d, (x, y + 19 - h, x + 6, y + 18), gem if x % 2 else leaf)
                d.line((x + 1, y + 17, x + 5, y + 17), fill=light)
        for y in range(44, 195, 22):
            _panel(d, (7, y, 31, y + 16), wood)
            d.line((10, y + 4, 28, y + 4), fill=light)
    elif name == "observatory":
        d.arc((24, -45, 215, 37), 0, 180, fill=wood, width=7)
        for x, y in ((19, 22), (64, 13), (110, 22), (170, 15), (215, 25)):
            d.line((x - 4, y, x + 4, y), fill=light)
            d.line((x, y - 4, x, y + 4), fill=light)
        _poly(d, [(204, 207), (220, 202), (236, 218), (218, 227)], leaf)
        d.line((220, 220, 209, 239), fill=wood, width=4)
        d.line((220, 220, 232, 239), fill=wood, width=4)
    elif name == "kitchen":
        for x in range(0, 240, 30):
            _panel(d, (x, 0, x + 28, 32), wood)
            _panel(d, (x + 4, 5, x + 24, 24), leaf)
            d.rectangle((x + 18, 13, x + 21, 16), fill=light)
        _panel(d, (0, 211, 100, 239), light)
        for x in (10, 53):
            _oval(d, (x, 215, x + 30, 236), dark)
            d.arc((x + 5, 219, x + 25, 233), 0, 300, fill=gem, width=3)
    elif name == "sewing_room":
        _panel(d, (0, 0, 239, 29), wood)
        for x in (12, 49, 88, 132, 178, 215):
            _panel(d, (x, 7, x + 13, 26), gem if x % 2 else leaf)
            for y in (4, 25):
                _oval(d, (x - 3, y, x + 16, y + 4), light)
        _panel(d, (205, 206, 237, 237), gem)
        for x in range(208, 237, 6):
            d.line((x, 208, x, 234), fill=light)
        for y in range(209, 237, 6):
            d.line((207, y, 235, y), fill=leaf)
    elif name == "witch_garden":
        for x in range(5, 238, 19):
            _poly(d, [(x, 2), (x + 4, 8), (x + 4, 30), (x - 3, 30),
                      (x - 3, 8)], wood)
        d.line((0, 19, 239, 19), fill=leaf, width=3)
        for x, y in ((15, 88), (223, 143), (21, 229), (218, 231)):
            frond(x, y, 25)
            _oval(d, (x - 7, y - 5, x + 8, y + 8), gem)
    elif name == "moon_altar":
        masonry((48, 0, 192, 34))
        d.ellipse((99, 3, 137, 31), fill=light)
        d.ellipse((109, 0, 143, 23), fill=wood)
        for x in (15, 215):
            for y in (27, 214):
                _panel(d, (x, y, x + 8, y + 21), gem)
                _poly(d, [(x + 4, y - 8), (x, y + 2), (x + 8, y + 2)], light)
    elif name == "primeval_jungle":
        for x in (9, 31, 208, 231):
            d.line((x, 0, x - 5, 29, x + 3, 81), fill=wood, width=6)
        for x, y in ((16, 36), (61, 31), (108, 27), (173, 31), (221, 37),
                     (20, 236), (72, 235), (195, 237), (227, 229)):
            frond(x, y, 30)
    elif name == "amber_basin":
        for x, y in ((12, 19), (48, 11), (183, 15), (223, 33),
                     (13, 222), (224, 224)):
            _poly(d, [(x - 9, y + 7), (x - 5, y - 12), (x + 8, y - 8),
                      (x + 12, y + 12)], wood)
            d.line((x - 3, y + 5, x + 3, y - 6), fill=light, width=3)
            d.arc((x - 4, y - 3, x + 7, y + 8), 0, 300, fill=gem, width=2)
        for x in (57, 110, 165):
            d.arc((x, 209, x + 40, 249), 190, 350, fill=leaf, width=3)
    elif name == "neon_alley":
        masonry((0, 0, 239, 35), 26)
        for x in (12, 85, 178):
            _panel(d, (x, 4, x + 48, 29), dark)
            d.line((x + 5, 23, x + 5, 9, x + 20, 20, x + 37, 8,
                    x + 37, 23), fill=gem, width=3)
        circuit((4, 54, 29, 200), 11)
        d.line((214, 239, 214, 214, 239, 214), fill=leaf, width=3)
    elif name == "circuit_garden":
        circuit((0, 1, 235, 31), 19)
        circuit((2, 210, 235, 239), 23)
        for x, y in ((17, 78), (223, 167)):
            _panel(d, (x - 10, y - 15, x + 10, y + 15), dark)
            _panel(d, (x - 6, y - 10, x + 6, y + 10), leaf)
            d.line((x, y - 7, x, y + 7), fill=gem, width=2)
    elif name == "lunar_base":
        d.polygon([(0, 0), (239, 0), (225, 33), (17, 33)], fill=wood)
        for x in (24, 91, 158):
            _oval(d, (x, 3, x + 46, 28), dark)
            _oval(d, (x + 7, 6, x + 40, 24), leaf)
        for x, y in ((14, 125), (220, 200), (30, 226)):
            d.ellipse((x - 12, y - 6, x + 12, y + 6), outline=dark, width=3)
            d.arc((x - 9, y - 4, x + 10, y + 5), 185, 350, fill=light)
    elif name == "starship":
        _poly(d, [(0, 0), (239, 0), (215, 34), (24, 34)], dark)
        for x in range(8, 231, 30):
            _panel(d, (x, 4, x + 22, 23), leaf)
            d.line((x + 3, 10, x + 16, 10), fill=light)
            d.line((x + 3, 17, x + 11, 17), fill=gem)
        for x in (3, 218):
            _panel(d, (x, 57, x + 17, 190), wood)
            for y in (64, 110, 159):
                _panel(d, (x + 4, y, x + 12, y + 22), gem)
    elif name == "enchanted_court":
        for x in (8, 58, 108, 158, 208):
            arch(x, 24)
        for x in (15, 223):
            d.line((x, 207, x, 63), fill=wood, width=4)
            for y in (74, 134, 194):
                _poly(d, [(x, y - 10), (x + 9, y), (x, y + 10),
                          (x - 9, y)], gem)
    elif name == "dragon_heath":
        for x, y in ((14, 33), (225, 35), (24, 235), (219, 230)):
            _poly(d, [(x - 12, y), (x - 3, y - 30), (x + 4, y - 15),
                      (x + 12, y - 23), (x + 10, y + 2)], dark)
            d.line((x - 2, y - 4, x + 2, y - 16), fill=gem, width=3)
        d.line((52, 15, 82, 23, 93, 10, 127, 28, 151, 13, 188, 21),
               fill=wood, width=4)
    elif name == "lost_city":
        masonry((0, 0, 239, 34), 25)
        for x in (14, 75, 143, 205):
            _panel(d, (x, 6, x + 18, 27), leaf)
            d.line((x + 5, 11, x + 13, 11, x + 8, 20, x + 14, 23),
                   fill=light, width=2)
        for x, y in ((15, 216), (220, 206)):
            masonry((x - 10, y, x + 12, 239), 12)
            frond(x, y + 15, 25)
    elif name == "sunken_temple":
        for x in (4, 217):
            masonry((x, 0, x + 18, 239), 18)
        for x in (45, 95, 155):
            d.arc((x, 3, x + 32, 35), 0, 300, fill=leaf, width=4)
        for x, y in ((26, 82), (218, 159), (30, 229), (207, 220)):
            d.line((x, y, x - 5, y - 12, x, y - 23), fill=leaf, width=3)
            for dx, dy in ((0, -29), (7, -35)):
                d.ellipse((x + dx, y + dy, x + dx + 5, y + dy + 5), outline=light)
    elif name == "zen_garden":
        for y in (10, 19, 28, 213, 222, 231):
            d.line((37, y, 204, y), fill=light)
        for x, y in ((16, 25), (218, 219)):
            for radius in (9, 14, 19):
                d.arc((x - radius, y - radius, x + radius, y + radius),
                      0, 350, fill=wood)
            _oval(d, (x - 7, y - 6, x + 7, y + 5), leaf)
    elif name == "bamboo_grove":
        for x in (6, 19, 31, 207, 222, 234):
            _panel(d, (x - 3, 0, x + 3, 239), leaf)
            for y in range(13 + x % 11, 239, 31):
                d.line((x - 3, y, x + 3, y), fill=light, width=2)
                _poly(d, [(x, y), (x + (9 if x < 40 else -9), y - 9),
                          (x + (8 if x < 40 else -8), y - 2)], leaf)
    elif name == "sakura_courtyard":
        masonry((0, 218, 239, 239), 24)
        d.line((0, 28, 60, 8, 120, 19, 188, 5, 239, 25), fill=wood, width=6)
        for x, y in ((17, 22), (48, 12), (88, 14), (127, 24),
                     (168, 11), (214, 22), (229, 30)):
            flower(x, y, 5)
        for x in (15, 222):
            arch(x - 9, 20)
    elif name == "cloud_sanctuary":
        for x, y in ((15, 15), (61, 9), (114, 16), (172, 10), (222, 20),
                     (23, 224), (82, 233), (175, 229), (224, 220)):
            _oval(d, (x - 22, y - 8, x + 22, y + 10), gem)
            _oval(d, (x - 10, y - 13, x + 13, y + 5), light)
        for x in (15, 220):
            _panel(d, (x - 5, 65, x + 5, 179), wood)
            _panel(d, (x - 10, 61, x + 10, 67), light)
    else:
        raise ValueError(f"Unknown appended background: {name}")
    # Replace the entire keeper area, including any border motif overshoot.
    d.rectangle((40, 40, 199, 199), fill=ground[1])
    rng = _rng(0xCA7100 + index * 971)
    for _ in range(180):
        x, y = 42 + rng(151), 42 + rng(153)
        d.line((x, y, x + 3 + rng(4), y), fill=ground[rng(3)])
        if rng(3) == 0:
            d.line((x + 1, y + 1, x + 3, y + 1), fill=ground[rng(3)])
    # Shade perimeter materials without filtering native terrain pixels.
    shaded = _catalogue_finish(image, _material_family(name))
    return shaded.resize((240, 240), Image.Resampling.NEAREST)


def _border_corner(family):
    """28x28 L-shaped ornaments; all painted pixels stay inside an 8px band."""
    strip = Image.new("RGBA", (28, 8))
    d = ImageDraw.Draw(strip)
    wood, gem, leaf = (_rgba(r[1]) for r in _CATALOGUE_RAMPS)
    if family == "woodland":
        d.line((0, 3, 27, 3), fill=wood, width=2)
        for x in (4, 12, 21):
            _poly(d, [(x - 3, 3), (x, 0), (x + 5, 1), (x + 2, 4)], leaf)
            _poly(d, [(x, 4), (x + 4, 7), (x + 7, 6), (x + 4, 3)], leaf)
    elif family == "fairy":
        for x in (5, 19):
            _panel(d, (x, 3, x + 2, 7), wood)
            _oval(d, (x - 4, 0, x + 6, 4), gem)
            d.point((x, 1), fill=leaf)
            d.point((x + 3, 2), fill=leaf)
        d.line((0, 6, 11, 6, 15, 3, 27, 6), fill=leaf)
    elif family == "gothic":
        _poly(d, [(0, 1), (5, 4), (7, 0), (9, 4), (14, 1),
                  (11, 6), (7, 7), (3, 6)], wood)
        d.point((6, 4), fill=gem)
        d.point((9, 4), fill=gem)
        for x in (16, 23):
            _poly(d, [(x, 7), (x, 3), (x + 2, 0), (x + 4, 3),
                      (x + 4, 7)], wood)
            d.line((x + 2, 3, x + 2, 6), fill=gem)
    elif family == "academia":
        for x, h in ((1, 7), (6, 5), (11, 6), (16, 7)):
            _panel(d, (x, 7 - h, x + 3, 7), leaf if x % 2 else gem)
            d.line((x + 1, 6, x + 2, 6), fill=wood)
        _poly(d, [(20, 7), (20, 1), (24, 2), (27, 0), (27, 6), (24, 7)], wood)
        d.line((24, 3, 24, 6), fill=gem)
    elif family == "household":
        _panel(d, (1, 2, 8, 6), gem)
        d.line((0, 0, 9, 0), fill=wood, width=2)
        d.line((0, 7, 9, 7), fill=wood)
        for x in (14, 23):
            _oval(d, (x - 3, 0, x + 3, 7), leaf)
            d.point((x - 1, 3), fill=INK)
            d.point((x + 1, 4), fill=INK)
    elif family == "witchy":
        _panel(d, (3, 0, 6, 2), wood)
        _poly(d, [(3, 2), (1, 4), (1, 7), (8, 7), (8, 4), (6, 2)], leaf)
        _panel(d, (3, 5, 6, 6), gem)
        _oval(d, (13, 0, 20, 7), gem)
        d.ellipse((17, -1, 23, 5), fill=(0, 0, 0, 0))
        _poly(d, [(23, 7), (25, 0), (27, 7)], leaf)
    elif family == "prehistoric":
        d.line((0, 4, 27, 4), fill=wood, width=2)
        for x in (3, 9, 15, 21):
            d.arc((x - 3, 0, x + 3, 7), 45, 315, fill=gem, width=2)
        _poly(d, [(23, 1), (26, 0), (27, 4), (25, 7), (22, 5)], leaf)
    elif family == "cyberpunk":
        for x in (1, 5, 9):
            _panel(d, (x, 0, x + 1, 7), wood)
        _oval(d, (1, 0, 10, 7), gem)
        _oval(d, (4, 2, 7, 5), leaf)
        d.line((11, 2, 17, 2, 20, 6, 27, 6), fill=leaf, width=2)
        _panel(d, (22, 0, 27, 3), wood)
        d.line((23, 1, 26, 1), fill=gem)
    elif family == "scifi":
        _poly(d, [(5, 0), (7, 3), (11, 3), (8, 5), (9, 7),
                  (5, 6), (2, 7), (3, 4), (0, 2), (4, 2)], leaf)
        _panel(d, (14, 0, 27, 7), wood)
        for x in (16, 20, 24):
            _panel(d, (x, 2, x + 1, 5), gem)
        d.line((15, 4, 26, 4), fill=leaf)
    elif family == "fantasy":
        _poly(d, [(0, 7), (0, 0), (3, 0), (3, 3), (6, 3), (6, 0),
                  (9, 0), (9, 3), (12, 3), (12, 0), (15, 0), (15, 7)], wood)
        _panel(d, (6, 5, 9, 7), gem)
        _poly(d, [(18, 7), (18, 1), (22, 3), (24, 0), (27, 2),
                  (25, 5), (27, 7)], leaf)
    elif family == "ancient":
        _poly(d, [(0, 7), (0, 5), (3, 5), (3, 3), (6, 3), (6, 0),
                  (10, 0), (10, 3), (13, 3), (13, 5), (16, 5), (16, 7)], wood)
        _panel(d, (7, 4, 9, 7), gem)
        d.line((20, 7, 20, 0, 26, 2, 21, 4, 26, 7), fill=leaf, width=2)
    elif family == "zen":
        d.line((0, 0, 5, 1, 13, 1, 18, 0), fill=gem, width=2)
        d.line((2, 3, 16, 3), fill=wood)
        for x in (4, 13):
            d.line((x, 2, x, 7), fill=wood, width=2)
        for x in (21, 26):
            d.line((x, 0, x, 7), fill=leaf, width=2)
            d.line((x - 1, 3, x + 1, 3), fill=wood)
    elif family == "water":
        for x in (5, 19):
            _poly(d, [(x, 7), (x - 5, 2), (x - 3, 0), (x + 3, 0),
                      (x + 5, 2)], gem)
            for dx in (-3, 0, 3):
                d.line((x, 6, x + dx, 1), fill=wood)
        d.arc((6, 3, 17, 7), 0, 180, fill=leaf)
    elif family == "mineral":
        for x, y in ((4, 1), (11, 0), (20, 2)):
            _poly(d, [(x - 3, 6), (x - 2, y + 1), (x, y), (x + 3, y + 2),
                      (x + 2, 7)], gem)
            d.line((x, y + 2, x, 6), fill=leaf)
    else:
        _poly(d, [(0, 7), (4, 3), (9, 1), (15, 4), (20, 2), (27, 7)], wood)
        d.line((2, 6, 9, 3, 15, 6), fill=gem)
        _poly(d, [(20, 6), (20, 0), (22, 0), (22, 3), (26, 3),
                  (26, 5), (22, 5), (22, 7)], leaf)
    corner = Image.new("RGBA", (28, 28))
    corner.alpha_composite(strip, (0, 0))
    corner.alpha_composite(strip.transpose(Image.Transpose.ROTATE_270), (0, 0))
    return corner


def _draw_crystal_border():
    source_path = os.path.join(os.path.dirname(__file__), "..", "assets", "sources",
                               "crystal_border.png")
    with Image.open(source_path) as source:
        source.load()
        if (source.format != "PNG" or source.mode != "RGBA" or source.size != (120, 120)
                or hashlib.sha256(source.tobytes()).hexdigest() != APPROVED_CRYSTAL_BORDER_SOURCE_SHA256):
            raise ValueError("Crystal border source differs from the selected PixelLab image")
        if source.getchannel("A").crop((18, 18, 102, 102)).getbbox():
            raise ValueError("Crystal border source paints inside its nine-slice opening")
        # Fit the generated 18px rim to 8px without clipping inward crystal tips.
        source_edges, target_edges = (0, 18, 102, 120), (0, 8, 112, 120)
        image = Image.new("RGBA", (120, 120))
        for y in range(3):
            for x in range(3):
                box = (source_edges[x], source_edges[y],
                       source_edges[x + 1], source_edges[y + 1])
                size = (target_edges[x + 1] - target_edges[x],
                        target_edges[y + 1] - target_edges[y])
                image.paste(source.crop(box).resize(size, Image.Resampling.NEAREST),
                            (target_edges[x], target_edges[y]))
    return image.resize((240, 240), Image.Resampling.NEAREST)


def draw_border(name):
    """A theme-specific 2x pixel frame with a genuinely transparent play opening."""
    if name not in BACKGROUND_NAMES:
        raise ValueError(f"Unknown border theme: {name}")
    if name == "crystal":
        return _draw_crystal_border()
    return terrain_art.border(name)


def _draw_legacy_border(name):
    index = BACKGROUND_NAMES.index(name)
    family = _material_family(name)
    source = _draw_procedural_background(name).resize((120, 120), Image.Resampling.NEAREST)
    image = Image.new("RGBA", (120, 120))
    # Sample the theme's edge materials rather than recoloring one generic frame.
    # The 10-color shared ramp keeps even the old 64-color terrain crisp here.
    palette = [INK] + [_rgba(color) for ramp in _THEME_MATERIALS[family] for color in ramp]
    cache = {}
    for y in range(120):
        for x in range(120):
            if 3 <= x < 117 and 3 <= y < 117:
                continue
            pixel = source.getpixel((x, y))
            if pixel not in cache:
                cache[pixel] = min(palette, key=lambda p: sum(
                    (p[channel] - pixel[channel]) ** 2 for channel in range(3)))
            image.putpixel((x, y), cache[pixel])
    d = ImageDraw.Draw(image)
    wood, gem, leaf = (_rgba(r[1]) for r in _CATALOGUE_RAMPS)
    plants = {
        "moss", "fern", "orchid", "woodland", "succulent", "willow",
        "fairy_glade", "moonflower_grove", "witch_garden", "primeval_jungle",
        "bamboo_grove", "sakura_courtyard",
    }
    masonry = {
        "gothic", "cemetery", "ruins", "cathedral", "rose_courtyard",
        "moon_altar", "enchanted_court", "lost_city", "sunken_temple",
    }
    technology = {
        "neon_alley", "circuit_garden", "lunar_base", "starship", "observatory",
    }
    petals = {"orchid", "fairy_glade", "moonflower_grove", "sakura_courtyard",
              "rose_courtyard"}
    # Author one horizontal edge tile, then rotate it exactly onto each side.
    # Each theme has its own pitch, offsets and motif layout, not just a tint.
    for side in range(4):
        edge = Image.new("RGBA", (120, 8))
        ed = ImageDraw.Draw(edge)
        pitch = 13 + index % 7
        offset = 8 + (index * 5 + side * 3) % 9
        ed.line((0, 1, 119, 1), fill=wood)
        ed.line((0, 2, 119, 2), fill=INK)
        for x in range(offset, 113, pitch):
            if name in plants:
                ed.line((x - 5, 3, x + 5, 4), fill=wood)
                _poly(ed, [(x - 4, 3), (x, 7), (x + 1, 3)], leaf)
                _poly(ed, [(x + 1, 3), (x + 6, 6), (x + 6, 3)], leaf)
            elif name in masonry:
                _panel(ed, (x - 5, 2, x + 5, 6), wood)
                ed.line((x - 4, 3, x + 3, 3), fill=gem)
                ed.line((x, 4, x, 6), fill=INK)
            elif name in technology:
                ed.line((x - 6, 3, x - 2, 3, x + 1, 6, x + 6, 6),
                        fill=leaf)
                _panel(ed, (x - 2, 3, x + 2, 7), gem)
            elif name in ("seashell", "pond", "marsh"):
                ed.arc((x - 5, 0, x + 5, 7), 0, 180, fill=leaf, width=2)
                ed.line((x - 2, 3, x, 6, x + 2, 3), fill=gem)
            elif name in ("crystal", "dark_cave", "amber_basin", "dragon_heath"):
                _poly(ed, [(x - 5, 3), (x - 2, 7), (x + 1, 4),
                           (x + 4, 6), (x + 5, 2)], gem)
                ed.line((x - 2, 3, x - 1, 5), fill=leaf)
            elif name in ("library", "house", "kitchen", "sewing_room"):
                _panel(ed, (x - 4, 2, x + 4, 7), wood)
                _panel(ed, (x - 2, 3, x + 2, 6), gem)
                ed.line((x, 3, x, 6), fill=leaf)
            elif name in ("night", "cloud_sanctuary"):
                _oval(ed, (x - 5, 1, x + 5, 6), gem)
                _oval(ed, (x - 2, 0, x + 3, 5), leaf)
            elif name in ("zen_garden", "sand", "desert", "snow", "minimal"):
                ed.line((x - 5, 4, x - 2, 5, x + 2, 5, x + 5, 4),
                        fill=leaf, width=2)
                _oval(ed, (x - 2, 2, x + 2, 4), wood)
            else:  # Cork, leaf litter, mushrooms and autumn.
                _oval(ed, (x - 4, 2, x + 4, 6), wood)
                ed.line((x - 2, 3, x + 2, 5), fill=leaf)
            if name in petals:
                _oval(ed, (x - 2, 3, x + 2, 7), gem)
                ed.point((x, 5), fill=wood)
        edge = _catalogue_finish(edge, family).resize((120, 8), Image.Resampling.NEAREST)
        if side == 0:
            image.alpha_composite(edge, (0, 0))
        elif side == 1:
            image.alpha_composite(edge.transpose(Image.Transpose.ROTATE_270), (112, 0))
        elif side == 2:
            image.alpha_composite(edge.transpose(Image.Transpose.ROTATE_180), (0, 112))
        else:
            image.alpha_composite(edge.transpose(Image.Transpose.ROTATE_90), (0, 0))
    # Large L-shaped corner ornaments carry family identity, not generic dots.
    corner = _catalogue_finish(_border_corner(family), family)
    corner = corner.resize((28, 28), Image.Resampling.NEAREST)
    for angle, position in ((0, (0, 0)), (270, (92, 0)),
                            (180, (92, 92)), (90, (0, 92))):
        image.alpha_composite(corner.rotate(angle), position)
    # [16,224) is transparent after enlargement; no invisible RGB debris.
    ImageDraw.Draw(image).rectangle((8, 8, 111, 111), fill=(0, 0, 0, 0))
    return _enlarge(image)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def _save_png(image, path):
    if os.path.isfile(path):
        with Image.open(path) as existing:
            if (existing.format == "PNG" and existing.mode == image.mode and
                    existing.size == image.size and existing.tobytes() == image.tobytes()):
                return False
    encoded = io.BytesIO()
    image.save(encoded, "PNG")
    with open(path, "wb") as output:
        output.write(encoded.getvalue())
    return True


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, "assets")
    os.makedirs(out_dir, exist_ok=True)

    count = 0

    def save(img, filename):
        nonlocal count
        path = os.path.join(out_dir, filename)
        changed = _save_png(img, path)
        count += 1
        print("wrote" if changed else "kept", filename)

    for view, theta in SPIDER_VIEWS:
        for anim, frames in SPIDER_ANIMS:
            for f in range(frames):
                save(draw_spider(theta, anim, f), "spider_%s_%s_%d.png" % (view, anim, f))

    for species in PREY_SPECIES:
        for anim, frames in PREY_ANIMS:
            for f in range(frames):
                save(draw_prey(species, anim, f), "prey_%s_%s_%d.png" % (species, anim, f))

    for icon in UI_ICONS:
        save(draw_icon(icon), "ui_%s.png" % icon)

    for achievement in ACHIEVEMENT_NAMES:
        save(draw_achievement(achievement), "achievement_%s.png" % achievement)

    for treat in TREAT_NAMES:
        save(draw_treat(treat), "treat_%s.png" % treat)

    for gift in GIFT_NAMES:
        save(draw_gift(gift), "gift_%s.png" % gift)

    for decor in DECOR_NAMES:
        save(draw_decor(decor), "decor_%s.png" % decor)

    for item in DECOR_ITEM_NAMES:
        save(draw_decor_item(item), "decoritem_%s.png" % item)

    for background in BACKGROUND_NAMES:
        save(draw_background(background), "background_%s_0.png" % background)
        save(draw_border(background), "border_%s_0.png" % background)

    print("total: %d files" % count)


if __name__ == "__main__":
    main()
