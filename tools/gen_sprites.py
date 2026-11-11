#!/usr/bin/env python3
"""
Spidarium sprite generator.

Draws every sprite procedurally with Pillow and writes RGBA PNGs into assets/.
Deterministic geometry and fixed per-theme seeds produce identical bytes.
Background and decor materials are defined alongside their drawing helpers.

Usage:  python tools/gen_sprites.py
"""

import colorsys
import io
import math
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("ERROR: Pillow is required. Install it with: pip install Pillow")


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
]

DECOR_NAMES = ["rock_0", "rock_1", "plant_0", "plant_1", "wood_log"]

# Placeable decor. These are the props the player buys and arranges, so they
# carry the 16-bit SNES-era look: a dark outline, a mid body, a lit top-left
# face and a shadowed bottom-right face, at 32x32 so they read at panel scale.
DECOR_ITEM_NAMES = [
    "rock_small", "rock_large", "pebbles", "log", "bark", "fern", "moss_clump",
    "succulent", "flower_white", "flower_pink", "mushroom", "shell", "crystal",
    "leaf_pile", "twig", "bone",
]
BACKGROUND_NAMES = [
    "cork", "moss", "leaf_litter", "sand", "fern", "orchid", "woodland", "minimal",
    "mushroom", "gothic", "seashell", "succulent", "willow", "desert", "crystal",
    "cemetery",
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
        light = tuple(min(255, v + (255 - v) // 3) for v in material[:3]) + (255,)
        dark = tuple(v * 3 // 4 for v in material[:3]) + (255,)
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
    return _enlarge(_shade_materials(img, (body, dark, highlight)))


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
    return _enlarge(_shade_materials(img, (body, dark, wing)))


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
    return _enlarge(img)


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
    return _enlarge(_shade_materials(img, (accent, shade, CREAM)))


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
    else:
        _octagon(d, (7, 3, 17, 14), shade, 3)
        d.rectangle((10, 6, 14, 12), fill=(0, 0, 0, 0))
        d.rectangle((5, 10, 19, 21), fill=accent, outline=INK)
        d.line([(7, 12), (17, 12)], fill=CREAM)
        d.rectangle((11, 14, 13, 16), fill=INK)
        d.line([(12, 16), (12, 19)], fill=INK)
    return _enlarge(_shade_materials(img, (accent, shade, CREAM)))


def draw_gift(name):
    """All eighteen furnishings share the terrain/decor material language."""
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
    return _enlarge(img)


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
    else:  # bone
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
    return _enlarge(img)


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
}


BACKGROUND_SEEDS = {
    "cork": 11, "moss": 23, "leaf_litter": 37, "sand": 41,
    "fern": 53, "orchid": 67, "woodland": 71, "minimal": 83,
    "mushroom": 97, "gothic": 101, "seashell": 113, "succulent": 127,
    "willow": 131, "desert": 149, "crystal": 157, "cemetery": 163,
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
}


def draw_background(name):
    """Compose one top-down ground texture at the panel's native 240x240."""
    p = BACKGROUND_PALETTES[name]
    img = Image.new("RGBA", (BG_SIZE, BG_SIZE), _rgba(p["ground"][3]))
    d = ImageDraw.Draw(img)
    rng = _rng(BACKGROUND_SEEDS[name])
    _BACKGROUND_SCENES[name](d, p, rng)
    return img


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

    for gift in GIFT_NAMES:
        save(draw_gift(gift), "gift_%s.png" % gift)

    for decor in DECOR_NAMES:
        save(draw_decor(decor), "decor_%s.png" % decor)

    for item in DECOR_ITEM_NAMES:
        save(draw_decor_item(item), "decoritem_%s.png" % item)

    for background in BACKGROUND_NAMES:
        save(draw_background(background), "background_%s_0.png" % background)

    print("total: %d files" % count)


if __name__ == "__main__":
    main()
