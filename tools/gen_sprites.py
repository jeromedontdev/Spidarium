#!/usr/bin/env python3
"""
Spidarium sprite generator.

Draws every sprite procedurally with Pillow and writes RGBA PNGs into assets/.
Deterministic: no randomness, fixed geometry, so re-running produces identical
bytes. All art tunables live in PALETTE and GEOMETRY at the top.

Usage:  python tools/gen_sprites.py
"""

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

PREY_SPECIES = ["fly", "cricket", "moth"]
PREY_ANIMS = [("walk", 2), ("flee", 2), ("dead", 1)]

UI_ICONS = [
    "feed", "clean", "play", "sleep", "decor", "status", "back",
    "hunger", "mood", "energy", "hygiene", "xp", "lock",
]


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
# Prey — 48x48, centre (24, 24), drawn facing right
# ---------------------------------------------------------------------------
def draw_prey(species, anim, frame):
    size = GEOMETRY["prey_canvas"]
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = GEOMETRY["prey_centre"]

    body = PALETTE["prey_body"]
    dark = PALETTE["prey_dark"]
    legc = PALETTE["prey_leg"]

    if anim == "dead":
        # On its back, legs curled up — visually distinct from walk.
        _ell(d, (cx, cy + 1), (9.0, 6.0), body)
        _ell(d, (cx - 7, cy + 1), (4.0, 4.0), dark)
        for i in range(3):
            x = cx - 2 + i * 4
            _taper(d, (x, cy - 4), (x - 2, cy - 11), 2.0, 1.0, legc)
            _taper(d, (x, cy + 6), (x - 2, cy + 12), 2.0, 1.0, legc)
        return img

    flap = 0.0
    if anim == "flee":
        flap = 3.0 if frame == 0 else -3.0
    step = 0.0
    if anim == "walk":
        step = 1.5 if frame == 0 else -1.5

    if species == "fly":
        _ell(d, (cx - 1, cy - 6 - flap), (7.0, 4.0), PALETTE["prey_wing"])
        _ell(d, (cx - 1, cy + 6 + flap), (7.0, 4.0), PALETTE["prey_wing"])
        _ell(d, (cx, cy), (9.0, 6.0), body)
        _ell(d, (cx + 8, cy), (4.0, 4.0), dark)
        _ell(d, (cx + 10, cy - 2), (2.0, 2.0), PALETTE["eye"])
        _ell(d, (cx + 10, cy + 2), (2.0, 2.0), PALETTE["eye"])
        for i in range(3):
            x = cx - 3 + i * 3
            _taper(d, (x, cy - 4), (x - 2 + step, cy - 9), 1.6, 0.8, legc)
            _taper(d, (x, cy + 4), (x - 2 - step, cy + 9), 1.6, 0.8, legc)

    elif species == "cricket":
        _ell(d, (cx, cy), (11.0, 5.0), body)
        _ell(d, (cx + 9, cy), (4.0, 4.0), dark)
        _taper(d, (cx + 12, cy - 1), (cx + 20, cy - 7), 1.2, 0.6, legc)
        _taper(d, (cx + 12, cy + 1), (cx + 20, cy + 7), 1.2, 0.6, legc)
        _taper(d, (cx - 6, cy - 3), (cx - 12, cy - 10 + step), 2.6, 1.2, legc)
        _taper(d, (cx - 6, cy + 3), (cx - 12, cy + 10 - step), 2.6, 1.2, legc)
        for i in range(2):
            x = cx - 1 + i * 4
            _taper(d, (x, cy - 4), (x - 1, cy - 8), 1.4, 0.7, legc)
            _taper(d, (x, cy + 4), (x - 1, cy + 8), 1.4, 0.7, legc)

    else:  # moth
        _ell(d, (cx - 2, cy - 7 - flap), (9.0, 6.0), PALETTE["prey_wing"])
        _ell(d, (cx - 2, cy + 7 + flap), (9.0, 6.0), PALETTE["prey_wing"])
        _ell(d, (cx, cy), (8.0, 5.5), body)
        _ell(d, (cx + 7, cy), (4.0, 4.0), dark)
        _taper(d, (cx + 10, cy - 1), (cx + 16, cy - 6), 1.0, 0.5, legc)
        _taper(d, (cx + 10, cy + 1), (cx + 16, cy + 6), 1.0, 0.5, legc)
        for i in range(3):
            x = cx - 3 + i * 3
            _taper(d, (x, cy - 4), (x - 1 + step, cy - 8), 1.4, 0.7, legc)
            _taper(d, (x, cy + 4), (x - 1 - step, cy + 8), 1.4, 0.7, legc)

    return img


# ---------------------------------------------------------------------------
# UI icons — 48x48, one accent colour + white
# ---------------------------------------------------------------------------
def draw_icon(name):
    size = GEOMETRY["ui_canvas"]
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    W = PALETTE["white"]
    A = PALETTE["hunger"]

    if name == "feed":
        d.arc([8, 18, 40, 44], start=0, end=180, fill=A, width=4)
        d.ellipse([10, 14, 38, 26], fill=W)
        d.ellipse([16, 10, 24, 18], fill=A)
        d.ellipse([26, 12, 32, 18], fill=A)

    elif name == "clean":
        d.ellipse([14, 20, 34, 40], fill=PALETTE["hygiene"])
        d.polygon([(24, 6), (34, 24), (14, 24)], fill=PALETTE["hygiene"])
        d.line([(36, 10), (36, 18)], fill=W, width=2)
        d.line([(32, 14), (40, 14)], fill=W, width=2)

    elif name == "play":
        _star(d, (24, 24), 15.0, 6.5, PALETTE["mood"])

    elif name == "sleep":
        d.ellipse([10, 10, 38, 38], fill=PALETTE["energy"])
        d.ellipse([18, 6, 44, 32], fill=(0, 0, 0, 0))

    elif name == "decor":
        d.polygon([(24, 8), (38, 24), (24, 40), (10, 24)], fill=PALETTE["mood"])
        d.line([(24, 12), (24, 36)], fill=W, width=2)

    elif name == "status":
        d.rectangle([10, 26, 16, 38], fill=W)
        d.rectangle([21, 18, 27, 38], fill=W)
        d.rectangle([32, 10, 38, 38], fill=W)

    elif name == "back":
        d.line([(30, 10), (16, 24), (30, 38)], fill=W, width=5)

    elif name == "hunger":
        d.line([(24, 20), (24, 40)], fill=A, width=4)
        for x in (16, 24, 32):
            d.line([(x, 8), (x, 20)], fill=A, width=3)
        d.line([(16, 20), (32, 20)], fill=A, width=3)

    elif name == "mood":
        d.ellipse([8, 8, 40, 40], outline=PALETTE["mood"], width=4)
        d.ellipse([16, 18, 21, 23], fill=W)
        d.ellipse([27, 18, 32, 23], fill=W)
        d.arc([15, 22, 33, 36], start=20, end=160, fill=W, width=3)

    elif name == "energy":
        d.polygon([(26, 6), (14, 26), (22, 26), (18, 42), (32, 20), (24, 20)],
                  fill=PALETTE["energy"])

    elif name == "hygiene":
        d.ellipse([14, 20, 34, 40], fill=PALETTE["hygiene"])
        d.polygon([(24, 6), (34, 24), (14, 24)], fill=PALETTE["hygiene"])

    elif name == "xp":
        _star(d, (24, 24), 16.0, 7.0, A)

    elif name == "lock":
        d.arc([15, 8, 33, 30], start=180, end=360, fill=PALETTE["dim"], width=4)
        d.rectangle([12, 22, 36, 40], fill=PALETTE["dim"])
        d.ellipse([21, 28, 27, 34], fill=(0, 0, 0, 0))

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
        img.save(os.path.join(out_dir, filename), "PNG")
        count += 1
        print("wrote", filename)

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

    print("total: %d files" % count)


if __name__ == "__main__":
    main()
