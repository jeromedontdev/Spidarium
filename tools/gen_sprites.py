#!/usr/bin/env python3
"""
Spidarium sprite generator.

Draws every sprite procedurally with Pillow and writes RGBA PNGs into assets/.
Deterministic: no randomness, fixed geometry, so re-running produces identical
bytes. All art tunables live in PALETTE and GEOMETRY at the top.

Usage:  python tools/gen_sprites.py
"""

import colorsys
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


def _decor_canvas():
    """A 32x32 canvas for a placeable decor item."""
    return _pixel_canvas(32, 32)


def _outline(d, points, color, width=1):
    """Draw a closed polygon outline, so every prop reads against the floor."""
    d.line(list(points) + [points[0]], fill=color, width=width)


def _blob(d, cx, cy, rx, ry, body, light, dark, ink=INK):
    """A rounded prop body with SNES-era shading.

    Ink rim, mid body, then a lit crescent on the top-left and a shadowed
    crescent on the bottom-right. Drawing the crescents as offset ellipses
    clipped by the body is what gives the prop volume; concentric rings just
    read as a flat disc with a border.
    """
    def box(inset):
        x0, y0 = cx - rx + inset, cy - ry + inset
        x1, y1 = cx + rx - inset, cy + ry - inset
        return (x0, y0, x1, y1) if x1 > x0 and y1 > y0 else None

    outer = box(0)
    if not outer:
        return
    d.ellipse(outer, fill=ink)
    inner = box(1)
    if not inner:
        return
    d.ellipse(inner, fill=body)
    # Lit crescent: a smaller ellipse nudged up-left, then the body re-stamped
    # over its lower-right so only the crescent survives.
    lit = (cx - rx + 1 - rx // 3, cy - ry + 1 - ry // 3,
           cx + rx - 1 - rx // 3, cy + ry - 1 - ry // 3)
    if lit[2] > lit[0] and lit[3] > lit[1]:
        d.ellipse(lit, fill=light)
        d.ellipse((cx - rx + 1 + rx // 4, cy - ry + 1 + ry // 4,
                   cx + rx - 1, cy + ry - 1), fill=body)
    # Shadow crescent on the opposite side.
    shade = (cx - rx + 1 + rx // 3, cy - ry + 1 + ry // 3,
             cx + rx - 1 + rx // 3, cy + ry - 1 + ry // 3)
    if shade[2] > shade[0] and shade[3] > shade[1]:
        d.ellipse(shade, fill=dark)
        d.ellipse((cx - rx + 1, cy - ry + 1,
                   cx + rx - 1 - rx // 4, cy + ry - 1 - ry // 4), fill=body)


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
    d.polygon([(px + (1 if px >= bx else -1), py + (1 if py >= by else -1))
               for px, py in points], fill=ink)
    d.polygon(points, fill=body)
    d.polygon([(bx, by)] + left + [(tx, ty)], fill=light)
    d.line([(bx, by), (tx, ty)], fill=dark)


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
    steps = 9
    spine = []
    for step in range(steps + 1):
        t = step / steps
        # Bend the rachis sideways so the frond arcs instead of standing rigid.
        bend = curve * math.sin(math.pi * t)
        spine.append((bx + dx * t + nx * bend, by + dy * t + ny * bend))
    d.line(spine, fill=ink, width=2)
    d.line(spine, fill=dark, width=1)
    for step in range(1, steps):
        t = step / steps
        px, py = spine[step]
        span = 4.6 * math.sin(math.pi * (0.15 + 0.85 * t)) ** 0.6
        if span < 1.2:
            continue
        for side in (1, -1):
            ex = px + nx * span * side + dx / length * 1.6
            ey = py + ny * span * side + dy / length * 1.6
            d.line([(px, py), (ex, ey)], fill=ink, width=2)
            d.line([(px, py), (ex, ey)], fill=body, width=1)
            d.point((ex, ey), fill=light)


def draw_decor_item(name):
    """One placeable decor item at 32x32, doubled to 64x64."""
    img, d = _decor_canvas()
    stone, stone_hi, stone_dark = map(_rgba, (0x8A8F86, 0xC6C9BE, 0x5A5F58))
    leaf, leaf_hi, leaf_dark = map(_rgba, (0x4E8A34, 0x8CC85A, 0x2E5A22))
    bark, bark_hi, bark_dark = map(_rgba, (0xA8763C, 0xE4BC80, 0x6E4A22))
    white, white_hi, white_dark = map(_rgba, (0xF2EEDC, 0xFFFFFF, 0xC8C2AC))
    pink, pink_hi, pink_dark = map(_rgba, (0xE87AB0, 0xFFB8D8, 0xB84E80))
    red, red_hi, red_dark = map(_rgba, (0xD83A2E, 0xFF7A5A, 0x8A1E18))
    cyan, cyan_hi, cyan_dark = map(_rgba, (0x62C8D8, 0xC8F4FF, 0x2E7A8A))
    bone, bone_hi, bone_dark = map(_rgba, (0xE8E2CC, 0xFFFDF0, 0xA8A088))
    shell, shell_hi, shell_dark = map(_rgba, (0xF2A08E, 0xFFD8C8, 0xC06A5A))

    if name == "rock_small":
        _blob(d, 16, 19, 10, 7, stone, stone_hi, stone_dark)
        d.line([(11, 16), (15, 14), (20, 16)], fill=stone_hi)
        d.line([(13, 21), (18, 22)], fill=stone_dark)
    elif name == "rock_large":
        _blob(d, 16, 17, 14, 11, stone, stone_hi, stone_dark)
        d.line([(7, 14), (13, 9), (21, 10)], fill=stone_hi, width=2)
        d.line([(10, 21), (17, 24), (23, 21)], fill=stone_dark, width=2)
        d.line([(19, 13), (22, 17)], fill=stone_dark)
    elif name == "pebbles":
        for cx, cy, rx, ry in ((10, 22, 5, 4), (20, 24, 4, 3), (16, 15, 6, 5),
                               (24, 16, 3, 3), (7, 14, 3, 2)):
            _blob(d, cx, cy, rx, ry, stone, stone_hi, stone_dark)
    elif name == "log":
        d.rectangle((3, 12, 28, 21), fill=INK)
        d.rectangle((4, 13, 27, 20), fill=bark)
        d.rectangle((4, 13, 27, 15), fill=bark_hi)
        d.rectangle((4, 18, 27, 20), fill=bark_dark)
        for x in (10, 16, 22):
            d.line([(x, 14), (x - 1, 19)], fill=bark_dark)
        d.ellipse((1, 11, 8, 22), fill=INK)
        d.ellipse((2, 12, 7, 21), fill=bark_hi)
        d.ellipse((3, 14, 6, 19), fill=bark_dark)
    elif name == "bark":
        d.polygon([(4, 20), (8, 10), (18, 7), (27, 12), (26, 22), (14, 25)],
                  fill=INK)
        d.polygon([(5, 19), (9, 11), (18, 8), (26, 13), (25, 21), (14, 24)],
                  fill=bark)
        d.polygon([(5, 19), (9, 11), (18, 8), (20, 12), (12, 20)], fill=bark_hi)
        for x, y in ((14, 14), (19, 17), (11, 17)):
            d.line([(x, y), (x + 3, y + 3)], fill=bark_dark)
    elif name == "fern":
        # A rosette of arcing fronds, the way a real fern grows from a crown.
        for base, tip, curve in (((16, 29), (5, 8), 2.5),
                                 ((16, 29), (16, 3), 0.0),
                                 ((16, 29), (27, 8), -2.5),
                                 ((16, 29), (2, 18), 3.0),
                                 ((16, 29), (30, 18), -3.0)):
            _frond(d, base, tip, curve, leaf, leaf_hi, leaf_dark)
    elif name == "moss_clump":
        for cx, cy, rx, ry in ((12, 20, 8, 6), (21, 21, 7, 5), (16, 14, 7, 5),
                               (9, 15, 5, 4), (24, 14, 4, 3)):
            _blob(d, cx, cy, rx, ry, leaf, leaf_hi, leaf_dark)
    elif name == "succulent":
        for step in range(8):
            angle = step * math.pi / 4
            cx = int(16 + math.cos(angle) * 9)
            cy = int(17 + math.sin(angle) * 7)
            _blob(d, cx, cy, 5, 4, leaf, leaf_hi, leaf_dark)
        _blob(d, 16, 17, 6, 5, leaf_hi, white_hi, leaf_dark)
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
            d.line([(16, 27), (16 + step * 5, 6)], fill=shell_dark)
        d.ellipse((12, 24, 20, 29), fill=shell_hi)
    elif name == "crystal":
        d.polygon([(16, 2), (25, 12), (22, 28), (10, 28), (7, 12)], fill=INK)
        d.polygon([(16, 3), (24, 12), (21, 27), (11, 27), (8, 12)], fill=cyan)
        d.polygon([(16, 3), (24, 12), (21, 27), (16, 27)], fill=cyan_hi)
        d.polygon([(16, 3), (8, 12), (11, 27), (16, 27)], fill=cyan_dark)
        d.line([(16, 4), (16, 26)], fill=INK)
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
    else:  # bone
        d.rectangle((10, 12, 22, 20), fill=INK)
        d.rectangle((11, 13, 21, 19), fill=bone)
        d.rectangle((11, 13, 21, 15), fill=bone_hi)
        for cx, cy in ((9, 11), (9, 21), (23, 11), (23, 21)):
            d.ellipse((cx - 4, cy - 4, cx + 4, cy + 4), fill=INK)
            d.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=bone)
            d.ellipse((cx - 3, cy - 3, cx + 1, cy), fill=bone_hi)
        d.line([(13, 17), (19, 17)], fill=bone_dark)
    return _enlarge(img)


# ---------------------------------------------------------------------------
# Enclosures — authored at the full 240x240 the panel displays, with no palette
# cap and no pixel-grid quantisation. These are illustrations of a keeper's
# substrate seen from above, not 8-bit sprites: the detail budget is whatever
# the QOI stream can carry.
#
# Palettes are generated from HLS specs so every theme keeps the same contrast
# structure: foliage always sits well above the ground in lightness, and its
# shadow well below. Hand-picked greens kept collapsing into the substrate.
#
# Roles, each a ramp from shadow to highlight:
#
#   ground 7 tones   stone 5 tones   foliage 5 tones   accents 3
# ---------------------------------------------------------------------------
def _hls(hue, light, sat):
    """HLS triple to a packed RGB int, clamped to the 0-255 cube."""
    r, g, b = colorsys.hls_to_rgb((hue % 360) / 360.0,
                                  max(0.0, min(1.0, light)),
                                  max(0.0, min(1.0, sat)))
    return (int(r * 255) << 16) | (int(g * 255) << 8) | int(b * 255)


def _ramp(hue, light, sat, count, spread):
    """A lightness ramp centred on the base tone, shadow first."""
    return [_hls(hue, light + spread * (i / (count - 1) - 0.5), sat)
            for i in range(count)]


def _palette(ground, foliage, stone, accents):
    """Expand four HLS specs into the role ramps a scene draws from.

    Each role keeps its HLS spec alongside a coarse ramp: the ramp is used for
    flat fills, and the spec drives continuous per-pixel shading. The reference
    art is a smooth-shaded render with tens of thousands of colours, so a
    handful of flat tones reads as 8-bit no matter how good the shapes are.
    """
    gh, gl, gs = ground
    fh, fl, fs = foliage
    sh, sl, ss = stone
    # The trail is a warm earth tone, always distinct from both the floor and
    # the foliage: the reference's floors are green or pale, and its paths are
    # bare brown dirt.
    trail = (28, gl * 0.85 + 0.06, 0.52)
    return {
        "ground": _ramp(gh, gl, gs, 26, 0.34),
        "stone": _ramp(sh, sl, ss, 18, 0.40),
        "leaf": _ramp(fh, fl, fs, 20, 0.36),
        "trail": _ramp(trail[0], trail[1], trail[2], 22, 0.36),
        "accent": [_ramp(spec[0], spec[1], spec[2], 12, 0.30)
                   for spec in accents],
        "spec": {"ground": ground, "leaf": foliage, "stone": stone,
                 "trail": trail, "accent": accents},
    }


def _tone(p, role, t, index=0):
    """A continuous tone from a role's HLS spec, so shading never bands."""
    hue, light, sat = p["spec"][role][index] if role == "accent" \
        else p["spec"][role]
    spread = {"ground": 0.34, "leaf": 0.36, "trail": 0.36}.get(role, 0.40)
    return _rgba(_hls(hue, light + spread * (max(0.0, min(1.0, t)) - 0.5), sat))


BACKGROUND_PALETTES = {
    # Every theme follows the reference's material logic: an earth-toned floor,
    # green foliage, neutral grey stone and a pale flower. Themes differ in the
    # hue of the floor and in which props they scatter, not in whether the
    # foliage separates from the ground.
    #
    # cork — warm ochre bark mulch
    "cork": _palette((30, 0.40, 0.58), (96, 0.50, 0.72), (40, 0.62, 0.10),
                     ((52, 0.94, 0.16), (18, 0.52, 0.72), (44, 0.78, 0.62))),
    # moss — dark peaty floor under vivid moss
    "moss": _palette((26, 0.30, 0.44), (96, 0.52, 0.78), (40, 0.60, 0.10),
                     ((52, 0.95, 0.14), (28, 0.50, 0.66), (72, 0.80, 0.66))),
    # leaf_litter — rust drift
    "leaf_litter": _palette((20, 0.38, 0.62), (92, 0.50, 0.70), (40, 0.60, 0.10),
                            ((52, 0.94, 0.14), (8, 0.56, 0.70), (40, 0.82, 0.66))),
    # sand — pale gold dunes
    "sand": _palette((40, 0.62, 0.62), (110, 0.34, 0.62), (40, 0.66, 0.08),
                     ((52, 0.96, 0.12), (24, 0.54, 0.68), (44, 0.80, 0.62))),
    # fern — dark forest floor
    "fern": _palette((28, 0.26, 0.42), (96, 0.54, 0.80), (40, 0.58, 0.10),
                     ((52, 0.95, 0.12), (28, 0.48, 0.64), (76, 0.82, 0.68))),
    # orchid — deep plum floor
    "orchid": _palette((310, 0.30, 0.42), (100, 0.44, 0.62), (40, 0.58, 0.10),
                       ((330, 0.72, 0.72), (312, 0.56, 0.62), (300, 0.90, 0.86))),
    # woodland — timber and moss
    "woodland": _palette((26, 0.34, 0.52), (100, 0.46, 0.66), (40, 0.60, 0.10),
                         ((52, 0.94, 0.14), (20, 0.50, 0.62), (44, 0.80, 0.68))),
    # minimal — bare pale gravel
    "minimal": _palette((40, 0.70, 0.06), (110, 0.36, 0.56), (40, 0.62, 0.06),
                        ((52, 0.96, 0.10), (36, 0.30, 0.62), (0, 0.00, 0.98))),
    # mushroom — warm loam
    "mushroom": _palette((28, 0.38, 0.56), (100, 0.44, 0.64), (40, 0.60, 0.10),
                         ((52, 0.94, 0.14), (2, 0.72, 0.62), (16, 0.80, 0.66))),
    # gothic — violet flagstones
    "gothic": _palette((262, 0.30, 0.40), (100, 0.40, 0.58), (250, 0.44, 0.10),
                       ((52, 0.92, 0.12), (272, 0.44, 0.62), (286, 0.60, 0.76))),
    # seashell — shell sand
    "seashell": _palette((34, 0.70, 0.34), (150, 0.34, 0.56), (40, 0.66, 0.08),
                         ((52, 0.96, 0.10), (8, 0.72, 0.72), (350, 0.66, 0.66))),    # succulent — cool teal floor
    "succulent": _palette((176, 0.26, 0.38), (150, 0.52, 0.66), (40, 0.58, 0.10),
                          ((52, 0.94, 0.12), (160, 0.34, 0.78), (140, 0.44, 0.86))),
    # willow — dark bank under yellow-green
    "willow": _palette((30, 0.28, 0.44), (66, 0.56, 0.76), (40, 0.58, 0.10),
                       ((52, 0.94, 0.12), (28, 0.50, 0.62), (70, 0.82, 0.70))),
    # desert — terracotta sand
    "desert": _palette((22, 0.50, 0.62), (120, 0.34, 0.56), (40, 0.64, 0.08),
                       ((52, 0.96, 0.10), (36, 0.72, 0.72), (130, 0.50, 0.50))),
    # crystal — dark geode rock
    "crystal": _palette((220, 0.24, 0.40), (186, 0.52, 0.66), (250, 0.40, 0.10),
                        ((186, 0.90, 0.86), (176, 0.72, 0.72), (322, 0.66, 0.66))),
    # cemetery — cold earth
    "cemetery": _palette((206, 0.24, 0.36), (110, 0.34, 0.54), (210, 0.34, 0.10),
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
        return state % n

    return nxt


def _rect(d, x0, y0, x1, y1, color):
    d.rectangle((x0, y0, x1, y1), fill=color)


def _poly(d, points, color):
    d.polygon(points, fill=color)


def _oval(d, cx, cy, rx, ry, color):
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=color)


def _line(d, points, color, width=1):
    d.line(points, fill=color, width=width)


def _pick(ramp, t):
    """Sample a ramp by a 0..1 position, interpolating between entries.

    Interpolating rather than snapping is what keeps shading smooth: a ramp of
    twenty tones sampled by index gives twenty flat bands, which is exactly the
    8-bit look we are trying to avoid.
    """
    t = max(0.0, min(1.0, t)) * (len(ramp) - 1)
    low = int(t)
    high = min(low + 1, len(ramp) - 1)
    return _mix(ramp[low], ramp[high], t - low)


def _channels(color):
    """Normalise a packed RGB int or an RGBA tuple to three channels."""
    if isinstance(color, tuple):
        return color[:3]
    return ((color >> 16) & 255, (color >> 8) & 255, color & 255)


def _mix(a, b, t):
    """Blend two colours, so shading is continuous rather than banded."""
    t = max(0.0, min(1.0, t))
    ca, cb = _channels(a), _channels(b)
    return tuple(int(ca[i] * (1 - t) + cb[i] * t) for i in range(3)) + (255,)


def _noise(rng, cells):
    """A smooth value-noise field over the whole frame, bilinearly sampled."""
    grid = [[rng(256) for _ in range(cells + 1)] for _ in range(cells + 1)]
    field = []
    for y in range(BG_SIZE):
        fy = y * cells / BG_SIZE
        y0 = min(int(fy), cells - 1)
        ty = fy - y0
        row = []
        for x in range(BG_SIZE):
            fx = x * cells / BG_SIZE
            x0 = min(int(fx), cells - 1)
            tx = fx - x0
            top = grid[y0][x0] * (1 - tx) + grid[y0][x0 + 1] * tx
            bot = grid[y0 + 1][x0] * (1 - tx) + grid[y0 + 1][x0 + 1] * tx
            row.append(int(top * (1 - ty) + bot * ty))
        field.append(row)
    return field


def _ground(d, p, rng, cells=9, contrast=1.0, path=None, debris=1.0):
    """Layered value noise, a directional light, and an optional worn path.

    Backgrounds are GROUND TEXTURE ONLY. Props used to be baked in, which made
    the floor busy enough to fight the spider for contrast and left the player
    nothing to arrange. Everything that sits on the floor is now a decor item
    the player buys and places, so the floor's only job is to read as a
    material: a lit side, a shadowed side, and a grain that does not compete
    with a moving creature on top of it.
    """
    tones = p["ground"]
    coarse = _noise(rng, cells)
    medium = _noise(rng, cells * 4)
    fine = _noise(rng, cells * 13)
    # A meandering path of a second material, if the theme has one. Two
    # materials meeting is what stops a floor reading as wallpaper.
    if path is not None:
        wander = _noise(rng, 3)
        band = _noise(rng, 7)
        edge = _noise(rng, 11)
    for y in range(BG_SIZE):
        for x in range(BG_SIZE):
            value = (coarse[y][x] * 0.55 + medium[y][x] * 0.30
                     + fine[y][x] * 0.15) / 255.0
            value = 0.5 + (value - 0.5) * contrast
            # Directional light from the top-left, plus a soft vignette.
            value *= 1.0 - 0.30 * (0.55 * (y / BG_SIZE) + 0.45 * (x / BG_SIZE))
            if path is not None:
                centre = 34 + wander[y][0] * 172 / 255.0
                half = 26 + edge[y][x] / 9.0
                if abs(x - centre) < half:
                    # Blend the trail into the ground over its last few pixels.
                    blend = min(1.0, (half - abs(x - centre)) / 7.0)
                    trail = 0.40 + band[y][x] / 255.0 * 0.45
                    d.point((x, y), fill=_mix(_tone(p, "ground", value),
                                              _tone(p, "trail", trail), blend))
                    continue
            d.point((x, y), fill=_tone(p, "ground", value))
    # Barely-there tonal drift, so the floor is not perfectly flat. Anything
    # stronger reads as a stain, and a stain under the spider reads as a bug.
    for _ in range(30):
        cx, cy = rng(BG_SIZE), rng(BG_SIZE)
        tone = _pick(tones, 0.46 + rng(12) / 100.0)
        for _ in range(3 + rng(4)):
            _oval(d, cx + rng(41) - 20, cy + rng(41) - 20,
                  8 + rng(16), 6 + rng(12), tone)
    # Fine grit only. No twigs, husks or pebbles: those are decor now, and
    # scattering them here is what made the floor fight the spider.
    for _ in range(int(2200 * debris)):
        x, y = rng(BG_SIZE), rng(BG_SIZE)
        d.point((x, y), fill=_pick(tones, 0.0 if rng(3) == 0 else 1.0))
    for _ in range(int(700 * debris)):
        x, y = rng(BG_SIZE), rng(BG_SIZE)
        dx, dy = rng(5) - 2, rng(5) - 2
        _line(d, [(x, y), (x + dx, y + dy)],
              _pick(tones, 0.0 if rng(2) else 1.0))


def _bg_cork(d, p, rng):
    """Warm ochre bark mulch."""
    _ground(d, p, rng, path=True)


def _bg_moss(d, p, rng):
    """Dark peaty floor under moss."""
    _ground(d, p, rng, cells=11, path=True)


def _bg_leaf_litter(d, p, rng):
    """Rust autumn drift."""
    _ground(d, p, rng, path=True, debris=1.4)


def _bg_sand(d, p, rng):
    """Pale gold dunes."""
    _ground(d, p, rng, cells=13, contrast=0.7, debris=0.8)


def _bg_fern(d, p, rng):
    """Dark forest floor."""
    _ground(d, p, rng, cells=11, path=True)


def _bg_orchid(d, p, rng):
    """Deep plum floor."""
    _ground(d, p, rng, cells=10)


def _bg_woodland(d, p, rng):
    """Timber-brown forest floor."""
    _ground(d, p, rng, path=True)


def _bg_minimal(d, p, rng):
    """Bare pale gravel."""
    _ground(d, p, rng, cells=15, contrast=0.6, path=True, debris=0.5)


def _bg_mushroom(d, p, rng):
    """Warm loam."""
    _ground(d, p, rng, path=True)


def _bg_gothic(d, p, rng):
    """Violet flagstones."""
    _ground(d, p, rng, cells=8, contrast=0.5)
    for row in range(9):
        x = -12 + rng(12)
        y = row * 28 - 8
        while x < BG_SIZE:
            w = 24 + rng(24)
            _rect(d, x, y, x + w, y + 26, _pick(p["ground"], 0.0))
            _rect(d, x + 1, y + 1, x + w - 1, y + 25, _pick(p["ground"], 0.55))
            _rect(d, x + 2, y + 2, x + w - 2, y + 5, _pick(p["ground"], 0.80))
            for _ in range(3):
                _line(d, [(x + 3 + rng(w - 6), y + 6 + rng(18)),
                          (x + 3 + rng(w - 6), y + 6 + rng(18))],
                      _pick(p["ground"], 0.30))
            x += w + 4 + rng(6)


def _bg_seashell(d, p, rng):
    """Shell sand."""
    _ground(d, p, rng, cells=12, contrast=0.8, debris=0.9)


def _bg_succulent(d, p, rng):
    """Cool teal floor."""
    _ground(d, p, rng, cells=10)


def _bg_willow(d, p, rng):
    """Dark bank under willow."""
    _ground(d, p, rng, cells=10, path=True)


def _bg_desert(d, p, rng):
    """Terracotta sand."""
    _ground(d, p, rng, cells=12, contrast=0.8, debris=0.9)


def _bg_crystal(d, p, rng):
    """Dark geode rock."""
    _ground(d, p, rng, cells=9, contrast=0.9, debris=0.7)


def _bg_cemetery(d, p, rng):
    """Cold earth."""
    _ground(d, p, rng, cells=9, contrast=0.7, debris=0.8)


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
    img = Image.new("RGBA", (BG_SIZE, BG_SIZE), _rgba(p["ground"][13]))
    d = ImageDraw.Draw(img)
    rng = _rng(BACKGROUND_SEEDS[name])
    _BACKGROUND_SCENES[name](d, p, rng)
    return img


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

    for item in DECOR_ITEM_NAMES:
        save(draw_decor_item(item), "decoritem_%s.png" % item)

    for background in BACKGROUND_NAMES:
        save(draw_background(background), "background_%s_0.png" % background)

    print("total: %d files" % count)


if __name__ == "__main__":
    main()
