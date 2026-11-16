"""Large climb structures: twigs, vines and roots the spider walks up.

These are a separate family from the 64x64 props in core_decor_art. A prop can
only lift the spider a few pixels, which reads as a hop; these are authored at
96x96 logical and doubled to 192x192 so a structure spans a large part of the
tank and the climb is genuinely visible.

Style follows docs/ART_STYLE.md: hue-shifted material ramps, top-left lighting,
shaded volume, broken highlights, binary alpha, exact nearest-neighbor doubling.
"""

import math

from PIL import Image, ImageDraw

NAMES = (
    "climb_branch",
    "climb_vine",
    "climb_root",
    "climb_ivy",
    "climb_boulder",
    "climb_driftwood",
)

LOGICAL = 96
SCALE = 2

# Hue-shifted ramps, dark to light, matching the core decor palette family.
RAMPS = {
    "bark": ("241f26", "3a2b2c", "52382f", "6d4735", "8a5a3c", "a87046",
             "c48c58", "dcae74", "efd09a"),
    "vine": ("1d2a26", "24382f", "2f4c3a", "3d6444", "4f7d4c", "669657",
             "82ae66", "a3c47c", "c6d99a"),
    "root": ("221d22", "33262a", "472f2e", "5c3c33", "744c3a", "8d5f44",
             "a87752", "c49365", "dcb083"),
    "stone": ("1f2430", "2b3240", "3a4351", "4c5765", "616d7a", "7a8791",
              "96a29c", "b4bda8", "d2d6b8"),
    "moss": ("1e2b26", "26382c", "324a33", "42603a", "557743", "6d8f4e",
             "88a75c", "a6bf72", "c6d68f"),
    "drift": ("262229", "3b3033", "54413c", "6f5447", "8c6a54", "a88365",
              "c29f7c", "d9bb96", "eed6b2"),
}

INK = (0x14, 0x12, 0x18, 255)


def _rgba(value):
    return tuple(bytes.fromhex(value)) + (255,)


def _ramp(name):
    return [_rgba(v) for v in RAMPS[name]]


def _canvas():
    image = Image.new("RGBA", (LOGICAL, LOGICAL), (0, 0, 0, 0))
    return image, ImageDraw.Draw(image)


def _taper(draw, points, widths, colors):
    """Draw a limb as a chain of quads that narrows along its length.

    A single thick line reads as a pipe. Tapering each segment and stepping the
    colour along the ramp is what makes a branch look like it grew.
    """
    for i in range(len(points) - 1):
        (x0, y0), (x1, y1) = points[i], points[i + 1]
        w0, w1 = widths[i], widths[i + 1]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / length, dx / length
        quad = [
            (x0 + nx * w0, y0 + ny * w0),
            (x1 + nx * w1, y1 + ny * w1),
            (x1 - nx * w1, y1 - ny * w1),
            (x0 - nx * w0, y0 - ny * w0),
        ]
        draw.polygon(quad, fill=colors[min(i, len(colors) - 1)])


def _highlight(draw, points, widths, color, offset=(-1, -1)):
    """A broken top-left highlight, not a continuous rim."""
    for i in range(0, len(points) - 1, 2):
        (x0, y0), (x1, y1) = points[i], points[i + 1]
        w = max(1, int(widths[i] * 0.45))
        draw.line([(x0 + offset[0], y0 + offset[1]), (x1 + offset[0], y1 + offset[1])],
                  fill=color, width=w)


def _leaf(draw, base, tip, breadth, body, light, dark):
    """A pointed leaf with a midrib and a lit upper half.

    A plain triangle reads as a spike at this scale, so the outline is built
    from a curved pair of edges and the midrib is drawn in the dark tone.
    """
    bx, by = base
    tx, ty = tip
    dx, dy = tx - bx, ty - by
    length = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / length, dx / length
    left, right = [], []
    steps = 7
    for i in range(steps + 1):
        t = i / steps
        # Widest at 40% of the length, tapering to a point at both ends.
        w = breadth * math.sin(math.pi * (t ** 0.75)) * (1.0 - 0.25 * t)
        px, py = bx + dx * t, by + dy * t
        left.append((px + nx * w, py + ny * w))
        right.append((px - nx * w, py - ny * w))
    draw.polygon(left + right[::-1], fill=body)
    # Lit half: the side facing the top-left light.
    lit = [(bx + dx * 0.15, by + dy * 0.15)]
    for i in range(1, steps):
        t = i / steps
        w = breadth * math.sin(math.pi * (t ** 0.75)) * (1.0 - 0.25 * t)
        px, py = bx + dx * t, by + dy * t
        lit.append((px + nx * w * 0.55, py + ny * w * 0.55))
    lit.append((bx + dx * 0.85, by + dy * 0.85))
    draw.polygon(lit, fill=light)
    draw.line([(bx, by), (tx, ty)], fill=dark, width=1)


def _moss_patch(draw, cx, cy, rx, ry, ramp):
    """A clumped moss cushion: a base blob plus lit lobes."""
    draw.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=ramp[3])
    draw.ellipse([cx - rx + 1, cy - ry, cx + rx - 2, cy + ry - 2], fill=ramp[5])
    for i in range(5):
        a = i * 1.3
        ox = cx + math.cos(a) * rx * 0.5
        oy = cy + math.sin(a) * ry * 0.5
        r = max(2, int(rx * 0.28))
        draw.ellipse([ox - r, oy - r, ox + r, oy + r], fill=ramp[6])
    draw.ellipse([cx - rx * 0.4, cy - ry * 0.6, cx + rx * 0.1, cy - ry * 0.1], fill=ramp[7])


def _outline(image):
    """One continuous ink ring around the finished silhouette."""
    pixels = image.load()
    edge = []
    for y in range(LOGICAL):
        for x in range(LOGICAL):
            if pixels[x, y][3]:
                continue
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < LOGICAL and 0 <= ny < LOGICAL and pixels[nx, ny][3]:
                    edge.append((x, y))
                    break
    for x, y in edge:
        pixels[x, y] = INK
    return image


def _branch():
    """A fallen branch running corner to corner, thick at the base."""
    image, draw = _canvas()
    ramp = _ramp("bark")
    points = [(10, 88), (26, 74), (44, 62), (62, 48), (78, 32), (88, 16)]
    widths = [10, 9, 8, 7, 5, 3]
    _taper(draw, points, widths, ramp[2:8])
    _highlight(draw, points, widths, ramp[8])
    # Grain: broken dark lines following the length, not a continuous stripe.
    for off in (-3, 1, 4):
        for i in range(0, len(points) - 1, 2):
            (x0, y0), (x1, y1) = points[i], points[i + 1]
            draw.line([(x0, y0 + off), (x1, y1 + off)], fill=ramp[3], width=1)
    # Knots where the stubs meet the trunk.
    for kx, ky in ((44, 62), (62, 48)):
        draw.ellipse([kx - 3, ky - 3, kx + 3, ky + 3], fill=ramp[3])
        draw.ellipse([kx - 1, ky - 1, kx + 1, ky + 1], fill=ramp[1])
    # Two stubs so it reads as a branch rather than a plank.
    _taper(draw, [(44, 62), (36, 48), (34, 38)], [5, 3, 2], ramp[3:6])
    _taper(draw, [(62, 48), (72, 52), (80, 50)], [4, 2, 1], ramp[3:6])
    for base, tip in (((34, 38), (24, 30)), ((80, 50), (90, 46)), ((26, 74), (16, 68))):
        _leaf(draw, base, tip, 5, ramp[5], ramp[8], ramp[1])
    return _outline(image)


def _vine():
    """A vine arching across the tank with hanging leaves."""
    image, draw = _canvas()
    ramp = _ramp("vine")
    points = [(6, 84), (18, 62), (34, 44), (50, 34), (66, 36), (80, 48), (90, 68)]
    widths = [5, 5, 5, 5, 5, 4, 3]
    _taper(draw, points, widths, ramp[2:8])
    _highlight(draw, points, widths, ramp[8])
    # Bark-like ridges across the vine so it is not a smooth tube.
    for i in range(1, len(points) - 1):
        x, y = points[i]
        draw.line([(x - 2, y - 2), (x + 2, y + 2)], fill=ramp[3], width=1)
    for i, (x, y) in enumerate(points[1:-1]):
        side = 1 if i % 2 else -1
        _leaf(draw, (x, y), (x + side * 11, y + 9), 5, ramp[4], ramp[8], ramp[1])
    # Tendrils hanging from the crown.
    for x, y in ((40, 40), (56, 34), (70, 40)):
        _taper(draw, [(x, y), (x + 2, y + 10), (x - 1, y + 18)], [2, 2, 1], ramp[3:6])
        draw.ellipse([x - 2, y + 17, x + 1, y + 20], fill=ramp[6])
    return _outline(image)


def _root():
    """Gnarled roots spreading from a central mass.

    The first attempt read as a starfish because four straight limbs met at one
    point. Real roots fork asymmetrically and thicken where they meet, so the
    limbs now curve, differ in length, and merge into a raised crown.
    """
    image, draw = _canvas()
    ramp = _ramp("root")
    limbs = (
        ([(46, 56), (38, 64), (32, 74), (28, 84), (22, 90)], [8, 7, 6, 4, 2]),
        ([(50, 56), (58, 62), (64, 72), (70, 82), (78, 88)], [8, 7, 5, 3, 2]),
        ([(46, 54), (43, 42), (41, 30), (37, 20), (34, 12)], [7, 6, 4, 3, 2]),
        ([(52, 54), (58, 46), (62, 36), (66, 28), (70, 20)], [6, 5, 4, 3, 2]),
        ([(48, 58), (49, 70), (47, 80), (46, 86)], [6, 5, 3, 2]),
    )
    for points, widths in limbs:
        _taper(draw, points, widths, ramp[2:7])
        _highlight(draw, points, widths, ramp[8])
    # Crown: an irregular mass, not a circle, so the limbs read as one plant.
    draw.polygon([(36, 50), (44, 42), (56, 44), (62, 54), (56, 64), (42, 64)],
                 fill=ramp[4])
    draw.polygon([(40, 48), (46, 44), (54, 46), (56, 54), (48, 58), (42, 56)],
                 fill=ramp[6])
    draw.polygon([(42, 48), (46, 46), (50, 48), (48, 52), (43, 52)], fill=ramp[7])
    for base, tip in (((28, 84), (18, 80)), ((70, 82), (80, 78)),
                      ((37, 20), (29, 14)), ((66, 28), (74, 24))):
        _leaf(draw, base, tip, 5, ramp[5], ramp[8], ramp[1])
    return _outline(image)


def _ivy():
    """A vertical ivy column climbing the tank wall."""
    image, draw = _canvas()
    ramp = _ramp("vine")
    points = [(46, 92), (43, 74), (49, 56), (43, 38), (49, 20), (46, 6)]
    widths = [9, 9, 8, 8, 6, 4]
    _taper(draw, points, widths, ramp[2:8])
    _highlight(draw, points, widths, ramp[8])
    # Cross-ridges so the stem reads as woody, not as a green pipe.
    for x, y in points[1:-1]:
        draw.line([(x - 4, y), (x + 4, y)], fill=ramp[3], width=1)
    for i, (x, y) in enumerate(points[:-1]):
        for side in (-1, 1):
            if (i + (side > 0)) % 2:
                continue
            # Angle the leaf down and out so it hangs off the stem instead of
            # sticking out sideways like a thorn.
            _leaf(draw, (x, y), (x + side * 20, y + 11), 9, ramp[4], ramp[8], ramp[1])
    return _outline(image)


def _boulder():
    """A mossy boulder with a climbable shoulder.

    A plain ellipse reads as a smooth pebble. The silhouette is built from
    straight chords so it has planes, and each plane gets its own tone.
    """
    image, draw = _canvas()
    stone = _ramp("stone")
    moss = _ramp("moss")
    # Faceted silhouette: flat chords, not a circle.
    draw.polygon([(14, 74), (18, 52), (32, 36), (52, 30), (72, 38), (84, 56),
                  (82, 78), (66, 90), (34, 90), (18, 84)], fill=stone[3])
    # Lit top-left plane.
    draw.polygon([(18, 52), (32, 36), (52, 30), (58, 48), (40, 58), (22, 62)],
                 fill=stone[6])
    draw.polygon([(32, 36), (52, 30), (54, 42), (38, 48)], fill=stone[7])
    # Shadowed right plane.
    draw.polygon([(58, 48), (72, 38), (84, 56), (82, 78), (62, 74)], fill=stone[4])
    # Front plane.
    draw.polygon([(22, 62), (40, 58), (62, 74), (66, 90), (34, 90), (18, 84)],
                 fill=stone[5])
    # Cracks: single-pixel dark lines along the plane seams.
    draw.line([(40, 58), (44, 74), (38, 88)], fill=stone[1], width=1)
    draw.line([(58, 48), (62, 74)], fill=stone[1], width=1)
    _moss_patch(draw, 34, 40, 18, 11, moss)
    _moss_patch(draw, 62, 52, 10, 7, moss)
    return _outline(image)


def _driftwood():
    """A wide driftwood ramp, the tallest structure."""
    image, draw = _canvas()
    ramp = _ramp("drift")
    points = [(4, 90), (22, 78), (40, 66), (58, 52), (74, 36), (92, 18)]
    widths = [12, 11, 10, 8, 6, 4]
    _taper(draw, points, widths, ramp[2:8])
    _highlight(draw, points, widths, ramp[8])
    # Grain lines along the length, broken rather than continuous.
    for off in (-4, 0, 4):
        for i in range(0, len(points) - 1, 2):
            (x0, y0), (x1, y1) = points[i], points[i + 1]
            draw.line([(x0, y0 + off), (x1, y1 + off)], fill=ramp[3], width=1)
    # Weathering: pale bleached patches where the sun hit it.
    for cx, cy, rx, ry in ((30, 76, 7, 4), (52, 60, 6, 3), (72, 42, 5, 3)):
        draw.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=ramp[7])
    _taper(draw, [(40, 66), (30, 54), (28, 42)], [6, 4, 2], ramp[3:6])
    _taper(draw, [(58, 52), (70, 58), (80, 56)], [5, 3, 1], ramp[3:6])
    for base, tip in (((28, 42), (18, 34)), ((80, 56), (90, 52))):
        _leaf(draw, base, tip, 5, ramp[5], ramp[8], ramp[1])
    return _outline(image)


_DRAWERS = {
    "climb_branch": _branch,
    "climb_vine": _vine,
    "climb_root": _root,
    "climb_ivy": _ivy,
    "climb_boulder": _boulder,
    "climb_driftwood": _driftwood,
}


def draw(name):
    """Render one climb structure at 192x192 with exact nearest doubling."""
    if name not in _DRAWERS:
        raise ValueError(f"Unknown climb structure: {name}")
    image = _DRAWERS[name]()
    return image.resize((LOGICAL * SCALE, LOGICAL * SCALE), Image.Resampling.NEAREST)
