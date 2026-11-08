#!/usr/bin/env python3
"""
Spidarium sprite generator.

Draws every sprite procedurally with Pillow and writes RGBA PNGs into assets/.
Deterministic: no randomness, fixed geometry, so re-running produces identical
bytes. All art tunables live in PALETTE and GEOMETRY at the top.

Usage:  python tools/gen_sprites.py
"""

import io
import math
import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("ERROR: Pillow is required. Install it with: pip install Pillow")


# ---------------------------------------------------------------------------
# Tunables — retune the art here without touching the drawing code.
# ---------------------------------------------------------------------------
PALETTE = {
    "body":       (0x6F, 0x7A, 0x8A, 255),
    "body_hi":    (0x8A, 0x94, 0xA6, 255),
    "body_rim":   (0x9E, 0xA8, 0xBA, 255),
    "body_dull":  (0x55, 0x5E, 0x6C, 255),
    "leg":        (0x4A, 0x52, 0x60, 255),
    "eye":        (0xF5, 0xB3, 0x42, 255),
    "pupil":      (0x05, 0x06, 0x0A, 255),
    "fang":       (0xD8, 0xDD, 0xE8, 255),
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
    "spider_canvas": 96,
    "spider_centre": (48.0, 48.0),
    "ceph_r":        (13.0, 11.0),
    "ceph_offset":   9.0,
    "abd_r":         (15.0, 17.0),
    "abd_offset":    -13.0,
    "leg_fwd":       (9.0, 3.5, -3.5, -9.0),
    "leg_side":      7.0,
    "leg_knee_out":  11.0,
    "leg_foot_out":  22.0,
    "leg_w0":        5.0,
    "leg_w1":        2.6,
    "rim":           1.2,
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
BACKGROUND_NAMES = [
    "cork", "moss", "leaf_litter", "sand", "fern", "orchid", "woodland", "minimal",
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
# Primitives
# ---------------------------------------------------------------------------
def _taper(d, p0, p1, w0, w1, col):
    """A tapered quad from p0 (width w0) to p1 (width w1)."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    length = math.hypot(dx, dy)
    if length < 1e-6:
        return
    nx, ny = -dy / length, dx / length
    d.polygon([
        (p0[0] + nx * w0 * 0.5, p0[1] + ny * w0 * 0.5),
        (p1[0] + nx * w1 * 0.5, p1[1] + ny * w1 * 0.5),
        (p1[0] - nx * w1 * 0.5, p1[1] - ny * w1 * 0.5),
        (p0[0] - nx * w0 * 0.5, p0[1] - ny * w0 * 0.5),
    ], fill=col)


def _ell(d, c, r, col):
    d.ellipse([c[0] - r[0], c[1] - r[1], c[0] + r[0], c[1] + r[1]], fill=col)


def _star(d, c, r_out, r_in, col, points=5):
    pts = []
    for i in range(points * 2):
        ang = -math.pi / 2.0 + i * math.pi / points
        r = r_out if i % 2 == 0 else r_in
        pts.append((c[0] + math.cos(ang) * r, c[1] + math.sin(ang) * r))
    d.polygon(pts, fill=col)


# ---------------------------------------------------------------------------
# Spider — 96x96, body centre pinned at (48, 48)
# ---------------------------------------------------------------------------
def _spider_params(anim, frame):
    """(gait_phase, leg_ext, body_shift, body_col, cracked, fang_spread)."""
    if anim == "walk":
        return (frame / 4.0 * 2.0 * math.pi, 1.0, 0.0, PALETTE["body"], False, 0.0)
    if anim == "idle":
        return (0.0, 1.0, 0.0, PALETTE["body"], False, 0.0)
    if anim == "eat":
        return (0.0, 1.0, 0.0, PALETTE["body"], False, 1.0 if frame else 0.0)
    if anim == "sleep":
        return (0.0, 0.42, 0.0, PALETTE["body"], False, 0.0)
    if anim == "pounce":
        if frame == 0:
            return (0.0, 0.55, -1.5, PALETTE["body"], False, 0.0)
        return (0.0, 1.45, 1.5, PALETTE["body"], False, 0.0)
    if anim == "molt":
        if frame == 0:
            return (0.0, 1.0, 0.0, PALETTE["body_dull"], True, 0.0)
        return (0.0, 1.0, 0.0, PALETTE["body_hi"], False, 0.0)
    return (0.0, 1.0, 0.0, PALETTE["body"], False, 0.0)


def draw_spider(theta, anim, frame):
    size = GEOMETRY["spider_canvas"]
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx, cy = GEOMETRY["spider_centre"]
    fx, fy = math.cos(theta), math.sin(theta)
    rx, ry = -fy, fx                      # "right" of the facing direction

    phase, ext, shift, body_col, cracked, fang_spread = _spider_params(anim, frame)

    def loc(fwd, side):
        """Local (forward, right) -> screen."""
        return (cx + fx * (fwd + shift) + rx * side,
                cy + fy * (fwd + shift) + ry * side)

    # --- legs first, so the body overlaps the hips -------------------------
    for side in (-1, 1):
        for k, fwd in enumerate(GEOMETRY["leg_fwd"]):
            swing = 0.0
            if anim == "walk":
                # Tetrapod gait: legs 1/3 left + 2/4 right move together.
                group = 0.0 if ((k % 2 == 0) == (side < 0)) else math.pi
                swing = 2.2 * math.sin(phase + group)

            hip = loc(fwd, side * GEOMETRY["leg_side"] * 0.72)
            knee = loc(fwd * 1.5 + swing * 0.5,
                       side * (GEOMETRY["leg_side"] + GEOMETRY["leg_knee_out"] * ext))
            foot = loc(fwd * 2.3 + swing - 1.0,
                       side * (GEOMETRY["leg_side"] + GEOMETRY["leg_foot_out"] * ext))

            _taper(d, hip, knee, GEOMETRY["leg_w0"], GEOMETRY["leg_w1"] * 1.4, PALETTE["leg"])
            _taper(d, knee, foot, GEOMETRY["leg_w1"] * 1.4, GEOMETRY["leg_w1"] * 0.7, PALETTE["leg"])

    abd_c = loc(GEOMETRY["abd_offset"], 0.0)
    ceph_c = loc(GEOMETRY["ceph_offset"], 0.0)

    # --- fuzzy rim (1 px lighter silhouette) -------------------------------
    rim = GEOMETRY["rim"]
    _ell(d, abd_c, (GEOMETRY["abd_r"][0] + rim, GEOMETRY["abd_r"][1] + rim), PALETTE["body_rim"])
    _ell(d, ceph_c, (GEOMETRY["ceph_r"][0] + rim, GEOMETRY["ceph_r"][1] + rim), PALETTE["body_rim"])

    # --- abdomen (larger, teardrop) ---------------------------------------
    _ell(d, abd_c, GEOMETRY["abd_r"], body_col)
    _ell(d, (abd_c[0] - 4.0, abd_c[1] - 5.0), (6.0, 6.0), PALETTE["body_hi"])

    # --- cephalothorax (compact, rounded) ---------------------------------
    _ell(d, ceph_c, GEOMETRY["ceph_r"], body_col)
    _ell(d, (ceph_c[0] - 3.0, ceph_c[1] - 3.5), (4.5, 4.0), PALETTE["body_hi"])

    # --- eight eyes: four large anterior, four small ----------------------
    for a, b in ((6.5, -3.2), (6.5, 3.2), (3.0, -5.6), (3.0, 5.6)):
        p = loc(GEOMETRY["ceph_offset"] + a, b)
        _ell(d, p, (2.7, 2.7), PALETTE["eye"])
        _ell(d, (p[0] + fx * 0.8, p[1] + fy * 0.8), (1.2, 1.2), PALETTE["pupil"])
    for a, b in ((1.0, -7.4), (1.0, 7.4), (8.6, -1.0), (8.6, 1.0)):
        _ell(d, loc(GEOMETRY["ceph_offset"] + a, b), (1.5, 1.5), PALETTE["eye"])

    # --- chelicerae / fangs ------------------------------------------------
    spread = 2.2 + fang_spread * 1.2
    for b in (-spread, spread):
        base = loc(GEOMETRY["ceph_offset"] + 11.0, b)
        tip = loc(GEOMETRY["ceph_offset"] + 15.0, b * 0.6)
        _taper(d, base, tip, 3.0, 1.0, PALETTE["fang"])

    # --- molt_0: old exoskeleton splitting --------------------------------
    if cracked:
        pts = []
        for i in range(7):
            a = -14.0 + i * 4.5
            b = 1.6 if i % 2 == 0 else -1.6
            pts.append(loc(a, b))
        d.line(pts, fill=PALETTE["pupil"], width=2)

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


def _draw_added_prey(species, anim, frame):
    """Small fly, antenna-led roach, and two contrasting segmented larvae."""
    img, d = _pixel_canvas(24, 24)
    body, dark, highlight = map(_rgba, PREY_COLORS[species])
    gait = 1 if frame == 0 else -1
    stride = gait * (2 if anim == "flee" else 1)
    if species == "roach":
        for side in (-1, 1):
            for x in (7, 11, 15):
                tip_y = 12 + side * 7
                d.line([(x, 12 + side * 2), (x - 2, 12 + side * 4),
                        (x - stride - 2, tip_y)], fill=INK)
            d.line([(17, 12 + side), (20, 12 + side * (3 + gait)),
                    (23, 12 + side * (7 + gait))], fill=dark)
        _shape(d, [(3, 11), (5, 8), (10, 7), (15, 8), (18, 10),
                   (18, 14), (15, 16), (10, 17), (5, 16), (3, 13)], body)
        d.line([(5, 12), (15, 12)], fill=dark)
        d.line([(6, 9), (11, 9)], fill=highlight)
        d.line([(6, 15), (11, 15)], fill=dark)
        _octagon(d, (14, 9, 18, 15), dark, 1)
        d.line([(15, 10), (16, 10)], fill=highlight)
        eye = (17, 11)
    elif species == "fruitfly":
        spread = 1 if anim == "flee" and frame == 0 else 0
        for side in (-1, 1):
            wing_y = 12 + side * (4 + spread)
            _shape(d, [(10, 12), (8, wing_y), (9, wing_y + side),
                       (12, wing_y + side), (14, 12)], highlight)
            d.point((10, wing_y), fill=CREAM)
            for x in (11, 13):
                d.line([(x, 12 + side), (x - stride, 12 + side * 4)],
                       fill=INK)
        _octagon(d, (9, 10, 13, 14), body, 1)
        d.line([(10, 12), (12, 12)], fill=dark)
        _octagon(d, (13, 10, 16, 14), dark, 1)
        d.point((15, 10), fill=_rgba(0xDB534A))
        d.point((15, 14), fill=_rgba(0xDB534A))
        eye = (16, 12)
    else:
        plump = species == "waxworm"
        radius = 3 if plump else 2
        start = 4 if plump else 3
        centers = [(x, 12 + (stride if x < 9 else -stride if x > 14 else 0))
                   for x in range(start, 18, 3)]
        for index, (x, y) in enumerate(centers):
            if plump or index >= len(centers) - 3:
                for side in (-1, 1):
                    d.line([(x, y + side * radius),
                            (x - gait, y + side * (radius + 1))], fill=dark)
        for x, y in centers:
            _octagon(d, (x - 2, y - radius, x + 2, y + radius), body, 1)
            d.line([(x + 1, y - radius + 1), (x + 1, y + radius - 1)], fill=dark)
            d.line([(x - 1, y - radius + 1), (x, y - radius + 1)], fill=highlight)
        head_y = centers[-1][1]
        _octagon(d, (17, head_y - 2, 20, head_y + 2), dark, 1)
        d.point((18, head_y - 1), fill=body)
        eye = (19, head_y)
    d.point(eye, fill=CREAM)
    return _enlarge(img)


def draw_prey(species, anim, frame):
    """Eight top-down prey; preserve the original four species' drawing path."""
    if anim == "dead":
        return draw_splatter(species)
    if species in ("roach", "mealworm", "fruitfly", "waxworm"):
        return _draw_added_prey(species, anim, frame)
    img, d = _pixel_canvas(24, 24)
    body, dark, wing = map(_rgba, PREY_COLORS[species])
    gait = 1 if frame == 0 else -1
    spread = 2 if anim == "flee" and frame == 0 else 0

    if species == "fly":
        for side in (-1, 1):
            y = 12 + side * (4 + spread)
            _shape(d, [(8, 12), (5, y), (6, y + side * 2),
                       (10, y + side * 2), (14, 12)], wing)
            d.line([(7, y), (11, y)], fill=CREAM)
    elif species == "moth":
        for side in (-1, 1):
            y = 12 + side * (7 + spread)
            _shape(d, [(12, 11), (5, y), (3, y + side), (3, 12 + side * 3),
                       (6, 12 + side), (5, 12), (10, 11)], wing)
            d.line([(4, 12 + side * 4), (8, 12 + side * 4)], fill=dark, width=2)
            d.point((6, 12 + side * 5), fill=CREAM)

    for side in (-1, 1):
        for x in (8, 11, 14):
            tip = 12 + side * 7
            d.line([(x, 12 + side * 2), (x - 1, 12 + side * 4),
                    (x - gait, tip)], fill=INK)

    if species == "cricket":
        for side in (-1, 1):
            y = 12 + side * (8 + gait)
            d.line([(8, 12 + side), (3, 12 + side * 4), (5, y),
                    (1, y)], fill=INK, width=2)
            d.line([(15, 11), (19, 7 if side < 0 else 17),
                    (22, 6 if side < 0 else 18)], fill=dark)
        _shape(d, [(5, 10), (13, 9), (17, 11), (17, 13), (13, 15),
                   (5, 14), (3, 12)], body)
        d.line([(6, 11), (13, 11)], fill=wing)
        d.line([(6, 13), (12, 13)], fill=dark)
        _octagon(d, (14, 10, 18, 14), dark, 1)
        eye = (16, 11)
    elif species == "beetle":
        _octagon(d, (5, 6, 15, 18), body, 3)
        d.line([(10, 7), (10, 17)], fill=dark)
        d.line([(7, 8), (7, 11)], fill=wing)
        d.line([(13, 14), (13, 16)], fill=dark)
        _octagon(d, (14, 9, 18, 15), dark, 1)
        for side in (-1, 1):
            d.line([(17, 11 if side < 0 else 13),
                    (20, 8 if side < 0 else 16)], fill=INK)
        eye = (16, 10)
    elif species == "moth":
        _octagon(d, (7, 10, 18, 14), body, 1)
        d.rectangle((9, 11, 14, 13), fill=dark)
        for x in (9, 11, 13):
            d.point((x, 12), fill=wing)
        for side in (-1, 1):
            d.line([(17, 12), (20, 9 if side < 0 else 15),
                    (21, 8 if side < 0 else 16)], fill=INK)
        eye = (17, 11)
    else:
        _octagon(d, (7, 9, 15, 15), dark, 2)
        d.rectangle((9, 10, 12, 14), fill=body)
        d.line([(9, 12), (12, 12)], fill=dark)
        _octagon(d, (14, 9, 18, 15), body, 1)
        d.rectangle((16, 10, 17, 11), fill=_rgba(0xD5634D))
        d.rectangle((16, 13, 17, 14), fill=_rgba(0xD5634D))
        eye = (16, 10)
    d.point(eye, fill=CREAM)
    return _enlarge(img)


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
    return _enlarge(img)


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
    return _enlarge(img)


def draw_gift(name):
    """Habitat furniture: shelters, perches, resting places and enrichment."""
    img, d = _pixel_canvas(16, 16)
    c = _rgba(GIFT_COLORS[name])
    shade = tuple(max(0, v - 48) for v in c[:3]) + (255,)
    wood, dark, cut = map(_rgba, (0x734D31, 0x523B27, 0xA47346))
    if name in ("twig", "crystal", "sapphire", "crown"):
        d.rectangle((3, 12, 12, 14), fill=dark, outline=INK)
        d.rectangle((7, 5, 9, 12), fill=wood, outline=INK)
        _shape(d, [(2, 4), (11, 2), (14, 4), (12, 7), (3, 8)], cut)
        d.line([(4, 5), (11, 4)], fill=CREAM)
        d.rectangle((4, 6, 6, 7), fill=c)
        if name == "twig":
            d.line([(9, 9), (13, 10), (14, 8)], fill=wood, width=2)
        elif name == "crown":
            d.line([(3, 10), (3, 3), (11, 1)], fill=wood)
            d.point((11, 1), fill=c)
    elif name in ("leaf", "acorn", "bone", "diamond"):
        _shape(d, [(1, 12), (2, 6), (6, 2), (10, 2), (14, 6), (15, 12)], wood)
        _shape(d, [(4, 12), (4, 8), (6, 6), (10, 6), (12, 8), (12, 12)], dark)
        d.rectangle((6, 8, 10, 12), fill=INK)
        d.line([(3, 6), (6, 3), (10, 3)], fill=cut)
        if name == "leaf":
            _shape(d, [(1, 6), (4, 2), (11, 1), (14, 4), (9, 6)], _rgba(0x68975C))
            d.line([(3, 4), (11, 3)], fill=CREAM)
        elif name == "diamond":
            d.line([(3, 7), (5, 4), (10, 4), (13, 7)], fill=CREAM)
            d.line([(4, 6), (11, 7)], fill=c)
        elif name == "bone":
            d.rectangle((2, 5, 4, 11), fill=CREAM)
            d.rectangle((12, 5, 14, 11), fill=c)
        else:
            d.line([(7, 2), (8, 0)], fill=wood)
            d.line([(3, 6), (13, 6)], fill=cut)
    elif name == "pebble":
        _shape(d, [(2, 9), (4, 6), (9, 5), (13, 8), (14, 11),
                   (11, 13), (4, 13), (1, 11)], c)
        d.line([(4, 8), (7, 7), (9, 7)], fill=CREAM)
        d.line([(4, 11), (11, 11), (12, 10)], fill=shade)
    elif name == "moss":
        _shape(d, [(1, 12), (2, 8), (4, 8), (4, 5), (7, 4), (9, 6),
                   (12, 5), (14, 8), (14, 12), (12, 14), (3, 14)], shade)
        for x, y in ((3, 9), (6, 6), (10, 8), (8, 11), (12, 11)):
            d.rectangle((x, y, x + 1, y + 1), fill=c)
        d.point((6, 5), fill=CREAM)
    elif name == "sap":
        _shape(d, [(1, 13), (3, 9), (8, 8), (11, 3), (14, 2), (13, 7),
                   (10, 11), (4, 12)], wood)
        for x, y in ((3, 10), (7, 9), (11, 6), (13, 3)):
            d.rectangle((x, y, x + 1, y + 1), fill=c)
    elif name == "flower":
        d.line([(8, 8), (8, 14)], fill=GREEN, width=2)
        _shape(d, [(8, 12), (11, 10), (13, 10), (11, 13)], GREEN)
        for box in ((6, 1, 9, 5), (2, 4, 6, 7), (10, 4, 14, 7),
                    (4, 8, 7, 11), (9, 8, 12, 11)):
            _octagon(d, box, c, 1)
        _octagon(d, (6, 5, 10, 8), _rgba(0xF5D571), 1)
        d.point((7, 6), fill=CREAM)
    elif name == "amber":
        _octagon(d, (1, 7, 14, 14), dark, 2)
        _octagon(d, (3, 4, 12, 11), c, 2)
        d.rectangle((5, 6, 10, 8), fill=shade)
        d.line([(4, 5), (10, 5)], fill=CREAM)
    elif name == "coral":
        d.line([(8, 14), (8, 5)], fill=INK, width=3)
        d.line([(8, 10), (3, 7), (3, 3)], fill=INK, width=3)
        d.line([(8, 9), (12, 6), (12, 2)], fill=INK, width=3)
        d.line([(8, 13), (8, 5)], fill=c)
        d.line([(8, 10), (3, 7), (3, 3)], fill=c)
        d.line([(8, 9), (12, 6), (12, 2)], fill=c)
        d.point((3, 3), fill=CREAM)
        d.line([(6, 14), (10, 14)], fill=shade)
    elif name == "silk":
        d.rectangle((1, 3, 3, 14), fill=wood, outline=INK)
        d.rectangle((12, 3, 14, 14), fill=wood, outline=INK)
        _shape(d, [(3, 5), (7, 8), (10, 8), (12, 5), (11, 11), (6, 12)], c)
        d.line([(4, 7), (7, 10), (10, 10), (11, 7)], fill=shade)
    elif name == "pearl":
        _octagon(d, (2, 2, 13, 13), c, 3)
        d.line([(3, 8), (5, 11), (10, 11), (12, 8)], fill=shade)
        d.rectangle((5, 4, 7, 5), fill=CREAM)
        d.point((4, 6), fill=CREAM)
    elif name == "ruby":
        _shape(d, [(2, 12), (2, 8), (10, 3), (13, 3), (14, 13)], wood)
        d.line([(3, 8), (10, 4), (13, 4)], fill=c, width=2)
        d.line([(5, 12), (10, 8)], fill=cut)
    elif name == "emerald":
        _octagon(d, (1, 9, 14, 14), wood, 1)
        d.line([(8, 11), (8, 3)], fill=shade, width=2)
        for points in ([(8, 6), (3, 2), (2, 4), (5, 7)],
                       [(8, 8), (12, 4), (14, 5), (11, 9)],
                       [(8, 10), (3, 6), (2, 8), (5, 11)]):
            _shape(d, points, c)
        d.point((8, 2), fill=CREAM)
    else:
        raise ValueError("Unknown gift: " + name)
    return _enlarge(img)


def draw_decor(name):
    """Optional props: two rocks, a fern, a succulent, and a cut bark log."""
    width, height = {
        "rock_0": (24, 16), "rock_1": (20, 14),
        "plant_0": (24, 32), "plant_1": (26, 28), "wood_log": (48, 18),
    }[name]
    img, d = _pixel_canvas(width, height)
    stone, stone_dark, stone_hi = map(_rgba, (0x6B766C, 0x444E46, 0xBBB89F))
    stem, leaf, leaf_hi = map(_rgba, (0x4D663D, 0x68975C, 0xAEC180))
    bark, bark_dark, cut = map(_rgba, (0x734D31, 0x523B27, 0xA47346))
    if name == "rock_0":
        _shape(d, [(2, 12), (4, 7), (9, 3), (16, 4), (21, 9),
                   (20, 13), (10, 15)], stone)
        d.line([(5, 7), (9, 4), (15, 5)], fill=stone_hi)
        _shape(d, [(11, 8), (20, 10), (19, 12), (12, 13)], stone_dark)
    elif name == "rock_1":
        _shape(d, [(1, 11), (5, 5), (11, 3), (18, 8), (18, 12), (9, 13)], cut)
        d.line([(5, 6), (10, 4), (14, 6)], fill=stone_hi)
        d.line([(10, 8), (14, 11), (17, 11)], fill=bark_dark, width=2)
    elif name == "plant_0":
        d.line([(12, 30), (12, 4)], fill=stem, width=2)
        _shape(d, [(12, 10), (9, 6), (12, 1), (15, 6)], leaf)
        for y, length in ((11, 6), (17, 8), (23, 10)):
            for side in (-1, 1):
                x = 12 + side * length
                _shape(d, [(12, y + 4), (x, y + 2), (x - side, y - 2),
                           (12 + side * 3, y)], leaf)
                d.line([(12 + side, y + 3), (x - side, y)], fill=leaf_hi)
        d.line([(8, 30), (16, 30)], fill=stone_dark, width=2)
    elif name == "plant_1":
        _shape(d, [(6, 22), (19, 22), (17, 27), (8, 27)], bark)
        for points in (
            [(12, 23), (4, 20), (1, 12), (7, 14)],
            [(13, 23), (21, 20), (24, 12), (18, 14)],
            [(12, 23), (7, 13), (8, 4), (12, 9)],
            [(13, 23), (18, 13), (17, 4), (13, 9)],
            [(12, 23), (10, 12), (13, 1), (15, 12)],
        ):
            _shape(d, points, leaf)
        d.line([(13, 5), (13, 19)], fill=leaf_hi)
        d.line([(8, 25), (16, 25)], fill=cut)
    else:
        _shape(d, [(8, 4), (43, 4), (46, 7), (46, 12),
                   (43, 15), (8, 15)], bark)
        for x in (17, 25, 34, 41):
            d.line([(x, 6), (x - 2, 10), (x - 1, 13)], fill=bark_dark)
        d.line([(13, 5), (35, 5)], fill=cut)
        _octagon(d, (1, 4, 12, 15), cut, 3)
        _octagon(d, (3, 6, 10, 13), bark_dark, 2)
        _octagon(d, (5, 8, 8, 11), cut, 1)
        d.point((6, 9), fill=CREAM)
    return _enlarge(img)


# ---------------------------------------------------------------------------
# Enclosures — composed at 120x120, doubled without filtering to 240x240.
# ---------------------------------------------------------------------------
BACKGROUND_PALETTES = {
    # Floor/shade, acrylic edge/light, bark/shade/light, foliage/shade/light,
    # silk, and one material accent. Twelve flat colors per keeper box.
    "cork": (0xC6B397, 0xB3A184, 0x6D7771, 0xE0E1CB, 0x916844, 0x684C35,
             0xB6905E, 0x7B9360, 0x566F47, 0xA1B27C, 0xEEE8D3, 0xD0BE9C),
    "moss": (0xA7AD86, 0x969F79, 0x66766A, 0xD5DECA, 0x8F6C48, 0x644F36,
             0xB39466, 0x78905A, 0x536E43, 0xA4B479, 0xEEEAD9, 0xBCC39C),
    "leaf_litter": (0xB8A181, 0xA78F70, 0x73766B, 0xDFDFCA, 0x926344, 0x684831,
                    0xBA8D56, 0x7F8A55, 0x586B44, 0xA9B57C, 0xEDE4CF, 0xCAAA72),
    "sand": (0xD3BF92, 0xC3AC7F, 0x7E8070, 0xE5E3CC, 0xA47750, 0x775338,
             0xC39966, 0x899565, 0x616E48, 0xB1BC87, 0xF1EAD5, 0xDDCCA6),
    "fern": (0xA7AD94, 0x969D85, 0x65766B, 0xDAE0CE, 0x8C6B49, 0x624E36,
             0xB19165, 0x739461, 0x4D724B, 0xA0BA80, 0xECE9D8, 0xBEC6AA),
    "orchid": (0xC4B9A8, 0xB0A591, 0x737B73, 0xE1E2D3, 0x8E6850, 0x674B39,
               0xB6916A, 0x789471, 0x526D54, 0xA2B894, 0xEFE9DD, 0xC89AA7),
    "woodland": (0xB3A68C, 0xA09278, 0x6E776C, 0xDCE0CA, 0x8B6848, 0x604D37,
                 0xB18D60, 0x778A5C, 0x516B45, 0xA4B47C, 0xEAE6D1, 0xC5B99B),
    "minimal": (0xD7D4BD, 0xC5C2AB, 0x7A8178, 0xE5E7D6, 0x9B7752, 0x73583E,
                0xB99A6C, 0x879B72, 0x60774F, 0xB0BF91, 0xF1EFDF, 0xE1DECB),
}


def _keeper_bark(d, p, x, y, width=23, height=69):
    """A cork slab with a broken silhouette and joined, bounded bark fissures."""
    bark, dark, light = p[4:7]
    outline = [(x + 3, y), (x + width - 5, y + 1), (x + width, y + 7),
               (x + width - 2, y + 19), (x + width, y + 31),
               (x + width - 3, y + height - 6), (x + width - 8, y + height),
               (x + 4, y + height - 2), (x, y + height - 11),
               (x + 2, y + 36), (x, y + 22), (x + 1, y + 8)]
    d.polygon(outline, fill=dark)
    d.polygon([(a + 2, b) for a, b in outline[:-2]], fill=bark)
    paths = [
        [(5, 5), (7, 12), (5, 19), (8, 24), (6, 34), (7, 42), (5, 51), (7, 61)],
        [(13, 4), (11, 13), (14, 21), (12, 29), (14, 38), (12, 47), (14, 58)],
        [(19, 9), (17, 18), (19, 27), (17, 36), (19, 45), (17, 56)],
        [(6, 19), (12, 21), (18, 18)],
        [(7, 42), (12, 39), (18, 42)],
        [(5, 53), (11, 55), (17, 52)],
    ]
    for path in paths:
        points = [(x + a * width // 23, y + b * height // 69) for a, b in path]
        d.line(points, fill=dark)
    for a, b, length in ((8, 7, 6), (15, 24, 7), (8, 29, 8),
                          (15, 43, 6), (8, 55, 5)):
        px, py = x + a * width // 23, y + b * height // 69
        d.line([(px, py), (px, py + length * height // 69)], fill=light, width=2)
    d.line([(x + 3, y + 8), (x + 3, y + 19)], fill=light)


def _keeper_tube(d, p, x, y, length=59):
    """Hollow cork hide seen from above; the dark mouth is an actual opening."""
    bark, dark, light = p[4:7]
    d.polygon([(x + 4, y), (x + length - 6, y + 2), (x + length, y + 6),
               (x + length - 1, y + 17), (x + length - 7, y + 20),
               (x + 3, y + 18), (x, y + 13), (x, y + 5)], fill=dark)
    d.polygon([(x + 6, y + 2), (x + length - 6, y + 4),
               (x + length - 4, y + 14), (x + 6, y + 16)], fill=bark)
    for offset in (6, 10, 14):
        d.line([(x + 9, y + offset), (x + 20, y + offset - 1),
                (x + 28, y + offset + 1), (x + length - 8, y + offset)], fill=dark)
    d.line([(x + 10, y + 4), (x + 23, y + 3), (x + 35, y + 5)], fill=light)
    d.ellipse((x, y + 2, x + 11, y + 18), fill=light)
    d.ellipse((x + 2, y + 4, x + 9, y + 16), fill=dark)
    d.line([(x + 5, y + 5), (x + 3, y + 8)], fill=bark)
    d.line([(x + length - 4, y + 6), (x + length - 5, y + 14)], fill=light)


def _keeper_twig(d, p, points):
    d.line(points, fill=p[5], width=5)
    d.line([(x - 1, y - 1) for x, y in points], fill=p[4], width=3)
    d.line([(x - 1, y - 1) for x, y in points[:-1]], fill=p[6])
    for x, y in points[1:-1]:
        d.ellipse((x - 2, y - 2, x + 1, y + 1), fill=p[5])
        d.point((x - 1, y - 1), fill=p[6])


def _keeper_leaf(d, p, base, tip, breadth=6, colors=None):
    """Two clustered tones share a midrib, attached to a visible botanical stem."""
    leaf, dark, light = colors or p[7:10]
    bx, by = base
    tx, ty = tip
    dx, dy = tx - bx, ty - by
    length = max(1, math.hypot(dx, dy))
    nx, ny = int(-dy * breadth / length), int(dx * breadth / length)
    mx, my = (bx + tx) // 2, (by + ty) // 2
    near = ((bx * 3 + tx) // 4, (by * 3 + ty) // 4)
    far = ((bx + tx * 3) // 4, (by + ty * 3) // 4)
    d.polygon([base, (near[0] + nx // 2, near[1] + ny // 2),
               (mx + nx, my + ny), (far[0] + nx // 2, far[1] + ny // 2), tip,
               (far[0] - nx // 2, far[1] - ny // 2),
               (mx - nx, my - ny), (near[0] - nx // 2, near[1] - ny // 2)], fill=dark)
    d.polygon([base, (mx + nx, my + ny),
               (far[0] + nx // 2, far[1] + ny // 2), tip], fill=leaf)
    d.line([base, (mx, my), tip], fill=light)
    for t in ((1, 2) if breadth >= 5 else ()):
        vx, vy = (bx * (4 - t) + tx * t) // 4, (by * (4 - t) + ty * t) // 4
        d.line([(vx, vy), (vx + nx // 2 + dx // 8, vy + ny // 2 + dy // 8)],
               fill=light)


def _keeper_vine(d, p, points, leaves):
    d.line(points, fill=p[8], width=2)
    d.line([(x - 1, y) for x, y in points], fill=p[7])
    for base, tip, breadth in leaves:
        _keeper_leaf(d, p, base, tip, breadth)


def _keeper_fern(d, p, base, tip, reach=15):
    bx, by = base
    tx, ty = tip
    d.line([base, tip], fill=p[8], width=2)
    for i in range(1, 7):
        x, y = (bx * (8 - i) + tx * i) // 8, (by * (8 - i) + ty * i) // 8
        size = reach * (8 - i) // 8
        for side in (-1, 1):
            _keeper_leaf(d, p, (x, y), (x + side * size, y - 6), 3)
    _keeper_leaf(d, p, ((bx + tx * 6) // 7, (by + ty * 6) // 7), tip, 3)


def _keeper_moss(d, p, x, y, width, height):
    """A defined moss cushion with contiguous lobe shading, never speckled noise."""
    d.ellipse((x, y + 3, x + width, y + height), fill=p[8])
    lobes = [(0, 5, 12, 13), (8, 1, 23, 12), (19, 4, 34, 16),
             (4, 12, 19, 23), (17, 13, 31, 25)]
    for x0, y0, x1, y1 in lobes:
        box = (x + x0 * width // 34, y + y0 * height // 25,
               x + x1 * width // 34, y + y1 * height // 25)
        d.ellipse(box, fill=p[7])
        d.arc((box[0] + 2, box[1] + 1, box[2] - 2, box[3] - 2),
              205, 285, fill=p[9], width=2)


def _keeper_retreat(d, p, x, y, anchor):
    """An irregular layered silk sleeping sac, not a radial capture web."""
    silk = p[10]
    d.line([(x + 2, y + 7), anchor], fill=p[3])
    d.line([(x + 17, y + 5), (x + 19, 6)], fill=p[3])
    d.line([(x + 5, y + 4), (x + 3, 6)], fill=p[3])
    outline = [(x + 1, y + 6), (x + 5, y + 3), (x + 13, y + 2),
               (x + 19, y + 5), (x + 20, y + 9), (x + 16, y + 13),
               (x + 8, y + 14), (x + 3, y + 11)]
    d.polygon(outline, fill=p[3])
    d.polygon([(x + 3, y + 6), (x + 7, y + 4), (x + 14, y + 4),
               (x + 18, y + 7), (x + 16, y + 11), (x + 8, y + 12),
               (x + 4, y + 10)], fill=silk)
    for start, end in (((4, 7), (15, 5)), ((5, 9), (17, 7)),
                       ((7, 11), (16, 9))):
        d.line([(x + start[0], y + start[1]), (x + end[0], y + end[1])], fill=p[3])
    d.ellipse((x + 2, y + 7, x + 6, y + 10), fill=p[5])


def draw_background(name):
    """Eight hand-composed keeper layouts with a clear playable central floor."""
    # Keep the hunting floor subdued; preserve brighter material/silk accents.
    palette = BACKGROUND_PALETTES[name]
    scales = (40, 40, 55, 60, 65, 65, 65, 60, 60, 60, 68, 40)
    p = tuple(_rgba(sum(((color >> shift) & 255) * scale // 100 << shift
                        for shift in (16, 8, 0)))
              for color, scale in zip(palette, scales))
    img = Image.new("RGBA", (120, 120), p[0])
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 119, 119), fill=p[2])
    d.rectangle((2, 2, 117, 117), fill=p[3])
    d.rectangle((5, 5, 114, 114), fill=p[1])
    d.polygon([(8, 9), (111, 8), (111, 111), (9, 111)], fill=p[0])
    if name == "minimal":
        d.polygon([(11, 12), (109, 11), (108, 107), (14, 110)], fill=p[11])
        d.line([(14, 110), (108, 107), (109, 11)], fill=p[1])
        d.line([(13, 14), (106, 13)], fill=p[10])
        d.polygon([(13, 100), (17, 107), (14, 110)], fill=p[0])
        _keeper_twig(d, p, [(15, 53), (18, 35), (27, 24), (45, 20), (63, 14)])
        _keeper_twig(d, p, [(27, 24), (23, 16), (15, 13)])
        _keeper_vine(d, p, [(105, 48), (102, 36), (103, 23)], [
            ((102, 36), (91, 30), 4), ((103, 29), (109, 18), 4),
            ((103, 23), (94, 15), 4)])
        _keeper_retreat(d, p, 49, 9, (43, 20))
    elif name == "cork":
        _keeper_bark(d, p, 10, 14, 24, 73)
        _keeper_tube(d, p, 33, 10, 52)
        _keeper_twig(d, p, [(24, 77), (33, 62), (32, 44), (43, 31)])
        _keeper_vine(d, p, [(108, 75), (104, 59), (108, 40), (101, 24)], [
            ((104, 59), (93, 50), 5), ((107, 45), (96, 33), 6),
            ((106, 38), (111, 23), 5), ((101, 24), (89, 20), 5)])
        _keeper_retreat(d, p, 72, 10, (80, 23))
    elif name == "moss":
        _keeper_moss(d, p, 9, 62, 25, 40)
        _keeper_moss(d, p, 85, 12, 27, 29)
        _keeper_bark(d, p, 11, 11, 22, 47)
        _keeper_twig(d, p, [(19, 67), (34, 43), (45, 24), (70, 17)])
        _keeper_twig(d, p, [(34, 43), (29, 31), (20, 24)])
        _keeper_vine(d, p, [(103, 60), (97, 43), (90, 29)], [
            ((97, 43), (86, 36), 6), ((94, 36), (106, 27), 5),
            ((90, 29), (82, 18), 5)])
        _keeper_retreat(d, p, 58, 9, (64, 18))
    elif name == "leaf_litter":
        for base, tip, breadth in [
            ((15, 99), (31, 87), 6), ((21, 104), (13, 85), 5),
            ((31, 105), (44, 99), 6), ((103, 56), (92, 41), 7),
            ((105, 46), (111, 29), 5), ((96, 34), (80, 27), 6),
        ]:
            _keeper_leaf(d, p, base, tip, breadth, (p[4], p[5], p[11]))
        _keeper_tube(d, p, 11, 13, 51)
        _keeper_twig(d, p, [(109, 81), (100, 66), (104, 47), (94, 28), (73, 17)])
        _keeper_twig(d, p, [(94, 28), (103, 19), (109, 17)])
        _keeper_vine(d, p, [(14, 63), (20, 48), (16, 31)], [
            ((20, 48), (31, 38), 5), ((18, 38), (10, 30), 4)])
        _keeper_retreat(d, p, 62, 9, (74, 19))
    elif name == "sand":
        d.polygon([(9, 99), (15, 95), (26, 97), (32, 108), (9, 111)], fill=p[11])
        d.polygon([(88, 101), (104, 93), (111, 94), (111, 111), (95, 111)], fill=p[1])
        _keeper_tube(d, p, 13, 12, 51)
        _keeper_twig(d, p, [(21, 66), (29, 46), (34, 31), (52, 25), (70, 16)])
        _keeper_twig(d, p, [(29, 46), (17, 40), (13, 30)])
        _keeper_vine(d, p, [(102, 72), (98, 55), (104, 38), (99, 20)], [
            ((98, 55), (84, 49), 6), ((102, 43), (110, 30), 5),
            ((102, 32), (89, 22), 6), ((99, 20), (105, 11), 4)])
        _keeper_retreat(d, p, 65, 9, (64, 20))
    elif name == "fern":
        _keeper_bark(d, p, 11, 11, 19, 54)
        _keeper_moss(d, p, 9, 78, 26, 28)
        _keeper_twig(d, p, [(19, 67), (31, 47), (43, 27), (67, 16)])
        _keeper_fern(d, p, (103, 78), (93, 16), 15)
        _keeper_fern(d, p, (103, 78), (112, 37), 10)
        _keeper_fern(d, p, (18, 103), (31, 69), 9)
        _keeper_retreat(d, p, 54, 9, (59, 19))
    elif name == "orchid":
        _keeper_bark(d, p, 12, 11, 21, 64)
        _keeper_twig(d, p, [(21, 74), (34, 49), (38, 30), (62, 16)])
        _keeper_vine(d, p, [(103, 88), (99, 68), (103, 46), (99, 28)], [
            ((99, 68), (81, 58), 8), ((102, 55), (111, 40), 7),
            ((103, 46), (83, 32), 8), ((101, 35), (110, 20), 6)])
        d.line([(99, 44), (89, 23), (83, 14)], fill=p[8], width=2)
        for cx, cy in ((89, 23), (82, 15)):
            for dx, dy in ((-4, -2), (3, -3), (-3, 3), (4, 3)):
                d.ellipse((cx + dx - 3, cy + dy - 2, cx + dx + 3, cy + dy + 2),
                          fill=p[11])
            d.line([(cx - 3, cy), (cx + 3, cy)], fill=p[10])
            d.rectangle((cx - 1, cy, cx + 1, cy + 2), fill=p[6])
        _keeper_retreat(d, p, 49, 9, (53, 20))
    else:
        _keeper_bark(d, p, 87, 14, 24, 62)
        _keeper_tube(d, p, 13, 12, 51)
        _keeper_twig(d, p, [(93, 84), (79, 64), (87, 46), (78, 27), (63, 17)])
        _keeper_twig(d, p, [(87, 46), (99, 35), (106, 26)])
        _keeper_vine(d, p, [(14, 72), (22, 52), (16, 31)], [
            ((22, 52), (37, 43), 6), ((20, 43), (10, 30), 5),
            ((16, 31), (28, 22), 6)])
        _keeper_moss(d, p, 10, 83, 24, 25)
        for base, tip in (((101, 101), (89, 88)), ((86, 107), (76, 96))):
            _keeper_leaf(d, p, base, tip, 5, (p[4], p[5], p[6]))
        _keeper_retreat(d, p, 61, 9, (68, 23))

    # Rim highlights and narrow paired ventilation slots stay attached to walls.
    d.line([(1, 115), (1, 1), (115, 1)], fill=p[3])
    d.line([(5, 112), (5, 5), (112, 5)], fill=p[2])
    d.line([(7, 112), (112, 112), (112, 7)], fill=p[3])
    for x in (34, 40, 46, 72, 78, 84):
        d.line([(x, 3), (x + 2, 3)], fill=p[2])
        d.line([(x, 116), (x + 2, 116)], fill=p[2])
    for x, y in ((3, 3), (115, 3), (3, 115), (115, 115)):
        d.rectangle((x, y, x + 1, y + 1), fill=p[0])
    return _enlarge(img)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, "assets")
    os.makedirs(out_dir, exist_ok=True)

    count = 0

    def save(img, filename):
        nonlocal count
        encoded = io.BytesIO()
        img.save(encoded, "PNG")
        path = os.path.join(out_dir, filename)
        contents = encoded.getvalue()
        if os.path.isfile(path):
            with open(path, "rb") as existing:
                unchanged = existing.read() == contents
        else:
            unchanged = False
        if not unchanged:
            with open(path, "wb") as output:
                output.write(contents)
        count += 1
        print("kept" if unchanged else "wrote", filename)

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

    for background in BACKGROUND_NAMES:
        save(draw_background(background), "background_%s_0.png" % background)

    print("total: %d files" % count)


if __name__ == "__main__":
    main()
