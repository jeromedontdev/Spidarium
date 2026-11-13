"""First decor redraw: hand-placed material clusters on a 32px logical grid."""

import math

from PIL import Image, ImageDraw


NAMES = (
    "rock_small", "rock_large", "pebbles", "log", "bark", "fern", "moss_clump",
    "succulent", "flower_white", "flower_pink", "mushroom", "shell", "crystal",
    "leaf_pile", "twig", "bone",
)

RAMPS = {
    "stone": ("252a38", "404551", "59636c", "788482", "a0ab9b", "c6cbb0",
              "e0d9b8", "334d43", "50694b", "7b8b51", "a7ac67", "d0c77e"),
    "wood": ("2c2630", "493539", "654239", "88533c", "ad7546", "d09b5f",
             "e8c68a", "29423b", "3e6249", "62834c", "91a35b", "b9c176"),
    "fern": ("223c39", "295445", "376c4b", "51834c", "789c55", "a7b968",
             "d0d48a", "493a34", "69503c", "926f4b", "b59964", "d0bb85"),
    "moss": ("293a37", "344f40", "496442", "657c46", "879748", "adb461",
             "d4ce88", "453a35", "65513f", "896c48", "b09863", "d7c58c"),
    "succulent": ("283b45", "315654", "3e7767", "589a80", "7ab796", "a7d0ac",
                  "d9dfb4", "514452", "795965", "a87a7e", "cda195", "ebc8ad"),
    "white": ("333b43", "687f80", "9ba99c", "c1c9af", "dce0c7", "f0edd6",
              "fff6df", "78502f", "aa7439", "d29a48", "ebbd63", "f7d88d"),
    "pink": ("493347", "82445f", "a65379", "c97091", "e595ac", "f3b8c5",
             "ffe0d9", "78502f", "aa7439", "d29a48", "ebbd63", "f7d88d"),
    "mushroom": ("382c39", "6b3540", "944347", "ba5c4c", "d77b58", "eea873",
                 "f6ca92", "80684f", "ad9372", "d2bb92", "ead4ab", "fff0ca"),
    "shell": ("4b3948", "78505a", "a76b6b", "c88e7f", "deae94", "edcbb0",
              "fae6c8", "8e6775", "b4848c", "d4a9a2", "ebcec0", "fff0da"),
    "crystal": ("293449", "34526d", "3b798d", "51a5af", "7ac9ca", "afe7df",
                "e6f9e9", "484a76", "64639d", "8985bb", "b2acd7", "ddd5ef"),
    "leaves": ("3d3034", "643c32", "8b4e34", "ad6e3b", "cb944e", "e3b66b",
               "f3d795", "424d39", "617044", "8d9251", "b7af65", "d6cd8d"),
    "bone": ("36373f", "69605a", "8b806b", "b0a186", "cebea0", "e5d7b6",
             "f5ebce", "6e685f", "968b73", "bcb191", "d7cba9", "eee0bb"),
}


def _polygon(draw, points, color):
    draw.polygon(points, fill=color)


def _stone(draw, x, y, scale, c):
    def points(coords):
        return [(x + round(px * scale), y + round(py * scale)) for px, py in coords]
    _polygon(draw, points([(0, 7), (3, 2), (8, 0), (15, 1), (20, 6),
                          (21, 12), (17, 16), (7, 17), (1, 13)]), c[0])
    _polygon(draw, points([(1, 7), (4, 3), (9, 1), (15, 2), (19, 6),
                          (20, 12), (16, 15), (7, 16), (2, 12)]), c[2])
    _polygon(draw, points([(2, 7), (5, 3), (9, 2), (14, 3), (12, 8),
                          (6, 10)]), c[4])
    _polygon(draw, points([(5, 4), (9, 2), (13, 3), (10, 5), (7, 6)]), c[5])
    _polygon(draw, points([(13, 4), (17, 5), (19, 9), (15, 11),
                          (11, 9)]), c[3])
    _polygon(draw, points([(3, 11), (8, 13), (15, 12), (19, 10), (18, 13),
                          (15, 15), (7, 15)]), c[1])
    draw.line(points([(4, 6), (6, 4), (9, 3)]), fill=c[6])
    draw.line(points([(14, 4), (13, 7), (15, 9), (14, 12)]), fill=c[1])
    draw.line(points([(15, 5), (14, 7), (16, 9)]), fill=c[5])
    draw.line(points([(4, 10), (7, 9), (9, 10)]), fill=c[3])
    draw.line(points([(8, 13), (10, 14), (12, 14)]), fill=c[3])


def _leaf(draw, base, tip, breadth, c, offset=1):
    bx, by = base
    tx, ty = tip
    dx, dy = tx - bx, ty - by
    length = max(1, math.hypot(dx, dy))
    nx, ny = -dy / length, dx / length
    left = (round(bx + dx * .5 + nx * breadth), round(by + dy * .5 + ny * breadth))
    right = (round(bx + dx * .6 - nx * breadth), round(by + dy * .6 - ny * breadth))
    _polygon(draw, [base, left, tip, right], c[0])
    inner = (round(bx + dx * .82), round(by + dy * .82))
    _polygon(draw, [base, left, inner], c[offset + 2])
    _polygon(draw, [base, inner, right], c[offset])
    draw.line([base, inner], fill=c[offset + 3])
    if length > 9:
        draw.line([(round(bx + dx * .45), round(by + dy * .45)), left],
                  fill=c[offset + 1])
        draw.point((round(bx + dx * .7), round(by + dy * .7)), fill=c[offset + 4])


def _frond(draw, spine, c):
    draw.line(spine, fill=c[1], width=2)
    draw.line(spine, fill=c[4])
    for i, ((px, py), (nx, ny)) in enumerate(zip(spine, spine[1:])):
        dx, dy = nx - px, ny - py
        length = max(1, math.hypot(dx, dy))
        for t in (.3, .8):
            x, y = px + dx * t, py + dy * t
            span = 4.5 * (1 - (i + t) / (len(spine) - 1)) + 1
            for side in (-1, 1):
                tx = x - dy / length * span * side + dx / length * 1.8
                ty = y + dx / length * span * side + dy / length * 1.8
                tip = (round(tx), round(ty))
                shoulder = (round((x + tx) / 2 - dx / length),
                            round((y + ty) / 2 - dy / length))
                _polygon(draw, [(round(x), round(y)), shoulder, tip,
                                (round(x + dx / length), round(y + dy / length))],
                         c[2 if side == -1 else 3])
                draw.line([(round(x), round(y)), tip], fill=c[4 if side == -1 else 5])
                draw.point(shoulder, fill=c[1])
    draw.point(spine[-1], fill=c[6])


def _moss(draw, x, y, c):
    _polygon(draw, [(x, y + 3), (x + 1, y + 1), (x + 3, y + 1),
                   (x + 4, y), (x + 6, y + 1), (x + 8, y),
                   (x + 10, y + 3), (x + 9, y + 5), (x + 2, y + 5)], c[1])
    for dx, dy in ((1, 3), (3, 2), (5, 3), (7, 1)):
        draw.line([(x + dx, y + dy), (x + dx + 1, y + dy - 1),
                   (x + dx + 2, y + dy)], fill=c[4])
        draw.line([(x + dx + 1, y + dy + 1), (x + dx + 2, y + dy + 1)], fill=c[2])
        draw.point((x + dx, y + dy - 1), fill=c[5])
    draw.point((x + 4, y + 1), fill=c[6])


def _flower(draw, c):
    petals = (
        [(15, 16), (11, 11), (12, 5), (15, 3), (18, 4), (20, 8), (18, 14)],
        [(17, 16), (20, 9), (26, 9), (29, 12), (27, 16), (21, 19)],
        [(18, 18), (25, 18), (27, 23), (25, 27), (21, 28), (17, 23)],
        [(15, 19), (18, 25), (14, 29), (10, 28), (8, 24), (11, 20)],
        [(13, 17), (9, 22), (4, 20), (2, 16), (4, 12), (9, 12)],
    )
    for petal in petals:
        _polygon(draw, petal, c[0])
        cx = sum(x for x, _ in petal) / len(petal)
        cy = sum(y for _, y in petal) / len(petal)
        inset = [(round(cx + (x - cx) * .75), round(cy + (y - cy) * .75))
                 for x, y in petal]
        _polygon(draw, inset, c[3])
        draw.line(inset[1:4], fill=c[5], width=2)
        draw.line(inset[-2:], fill=c[1], width=2)
        draw.line([(round(cx), round(cy)), (15, 17)], fill=c[2])
        draw.point(inset[2], fill=c[6])
    _polygon(draw, [(11, 15), (14, 12), (18, 13), (20, 16), (19, 20),
                   (15, 22), (11, 19)], c[7])
    _polygon(draw, [(12, 15), (15, 13), (18, 14), (19, 17), (17, 20),
                   (13, 19)], c[9])
    draw.line([(12, 16), (14, 14), (17, 14)], fill=c[11])
    for x, y in ((14, 16), (17, 16), (15, 19), (18, 18)):
        draw.point((x, y), fill=c[8])
        draw.point((x, y - 1), fill=c[10])


def _draw(name):
    family = ("stone" if name in ("rock_small", "rock_large", "pebbles") else
              "wood" if name in ("log", "bark", "twig") else
              "fern" if name == "fern" else
              "moss" if name == "moss_clump" else
              "white" if name == "flower_white" else
              "pink" if name == "flower_pink" else
              "leaves" if name == "leaf_pile" else name)
    c = [tuple(bytes.fromhex(value)) + (255,) for value in RAMPS[family]]
    image = Image.new("RGBA", (32, 32))
    d = ImageDraw.Draw(image)
    if name in ("rock_small", "rock_large"):
        x, y, scale = (6, 11, .95) if name == "rock_small" else (2, 5, 1.3)
        _stone(d, x, y, scale, c)
        for x, y in (((9, 23), (11, 24)) if name == "rock_small" else
                     ((5, 23), (7, 24), (8, 22), (24, 24))):
            d.line([(x, y), (x + 2, y - 1), (x + 3, y)], fill=c[8])
            d.line([(x, y - 1), (x + 1, y - 1)], fill=c[10])
            d.point((x + 2, y + 1), fill=c[7])
    elif name == "pebbles":
        for x, y, scale in ((3, 19, .40), (16, 22, .43), (10, 10, .55),
                            (23, 13, .32), (3, 12, .28)):
            _stone(d, x, y, scale, c)
        d.line([(12, 25), (14, 24), (16, 25)], fill=c[8])
        d.point((14, 23), fill=c[10])
    elif name == "log":
        _polygon(d, [(2, 12), (7, 8), (23, 4), (27, 6), (30, 12),
                     (29, 22), (10, 29), (4, 27), (1, 22)], c[0])
        _polygon(d, [(7, 10), (23, 5), (27, 7), (29, 13), (28, 21),
                     (9, 28), (6, 24)], c[2])
        _polygon(d, [(8, 11), (23, 6), (26, 8), (27, 12), (10, 18)], c[4])
        for y in (10, 15, 20, 24):
            d.line([(11, y + 1), (16, y - 2), (20, y - 2), (27, y - 5)], fill=c[1])
            d.line([(12, y), (16, y - 3), (19, y - 3)], fill=c[5])
            d.line([(18, y), (21, y - 1), (24, y - 3)], fill=c[3])
        d.line([(12, 9), (20, 6), (24, 6)], fill=c[6])
        _polygon(d, [(3, 13), (7, 11), (11, 15), (12, 21), (9, 27),
                     (5, 26), (2, 21)], c[5])
        _polygon(d, [(4, 15), (7, 13), (10, 16), (10, 22), (8, 25),
                     (5, 24), (3, 20)], c[3])
        _polygon(d, [(5, 16), (7, 15), (9, 18), (9, 22), (7, 24),
                     (4, 21)], c[0])
        d.line([(3, 15), (5, 12), (7, 12)], fill=c[6])
        d.line([(5, 25), (7, 26), (9, 24)], fill=c[4])
        d.line([(6, 13), (7, 15)], fill=c[1])
        d.line([(4, 22), (3, 24)], fill=c[1])
        for x, y in ((18, 8), (21, 7), (24, 19)):
            d.line([(x, y), (x + 2, y - 1), (x + 3, y)], fill=c[8])
            d.point((x + 1, y - 1), fill=c[10])
    elif name == "bark":
        _polygon(d, [(3, 20), (6, 11), (12, 8), (15, 4), (21, 6),
                     (28, 12), (27, 23), (21, 26), (12, 29), (5, 26)], c[0])
        _polygon(d, [(4, 20), (7, 12), (13, 9), (16, 5), (21, 7),
                     (27, 12), (26, 22), (20, 25), (12, 28), (6, 25)], c[2])
        for points in (
            [(6, 21), (9, 13), (14, 11), (18, 7), (20, 8), (15, 15), (12, 23)],
            [(14, 25), (18, 16), (22, 12), (25, 13), (24, 19), (20, 23)],
        ):
            _polygon(d, points, c[4])
            d.line(points[:4], fill=c[5])
        d.line([(9, 23), (12, 18), (11, 16), (14, 12)], fill=c[1])
        d.line([(18, 23), (20, 19), (19, 17), (22, 13)], fill=c[1])
        d.line([(11, 25), (15, 22), (16, 18)], fill=c[3])
        d.line([(9, 14), (11, 12), (14, 11)], fill=c[6])
        d.line([(6, 23), (8, 24), (10, 24)], fill=c[3])
        for x, y in ((8, 25), (22, 21), (24, 20)):
            d.line([(x, y), (x + 1, y - 2), (x + 2, y - 1)], fill=c[9])
            d.point((x, y - 1), fill=c[11])
            d.point((x + 2, y), fill=c[7])
    elif name == "fern":
        _polygon(d, [(8, 27), (11, 24), (20, 24), (24, 27), (22, 30),
                     (10, 30)], c[7])
        d.line([(10, 27), (15, 25), (20, 26)], fill=c[10])
        d.line([(12, 29), (18, 28), (22, 28)], fill=c[8])
        d.point((11, 27), fill=c[11])
        d.point((20, 27), fill=c[9])
        for spine in (
            [(16, 28), (12, 24), (9, 22), (6, 21), (3, 22)],
            [(16, 28), (12, 21), (9, 16), (7, 11), (5, 8), (3, 7)],
            [(16, 28), (16, 22), (15, 16), (14, 11), (15, 7), (17, 4)],
            [(16, 28), (20, 24), (23, 22), (26, 22), (28, 24)],
            [(16, 28), (21, 20), (23, 15), (25, 10), (27, 8)],
        ):
            _frond(d, spine, c)
    elif name == "moss_clump":
        _polygon(d, [(2, 22), (4, 16), (9, 15), (10, 9), (15, 7),
                     (20, 10), (25, 11), (29, 17), (30, 23), (24, 27),
                     (15, 29), (7, 27)], c[0])
        _polygon(d, [(3, 22), (5, 17), (10, 16), (11, 10), (15, 8),
                     (20, 11), (24, 12), (28, 17), (29, 23), (23, 26),
                     (15, 28), (7, 26)], c[2])
        for x, y in ((6, 19), (16, 21), (10, 12), (17, 14), (3, 21)):
            _moss(d, x, y, c)
        d.line([(9, 27), (14, 28), (20, 27)], fill=c[7])
        d.line([(11, 26), (14, 27), (17, 26)], fill=c[9])
        d.point((13, 26), fill=c[11])
        d.point((19, 26), fill=c[8])
        d.line([(24, 13), (25, 10), (26, 9)], fill=c[3])
        d.point((26, 8), fill=c[6])
    elif name == "succulent":
        for base, tip, width in (
            ((16, 21), (3, 20), 4), ((16, 22), (5, 10), 4),
            ((16, 22), (14, 3), 5), ((16, 22), (25, 7), 5),
            ((16, 22), (30, 17), 5), ((16, 20), (24, 28), 5),
            ((16, 20), (8, 28), 5),
        ):
            _leaf(d, base, tip, width, c)
        for base, tip, width in (
            ((16, 21), (9, 15), 3), ((16, 22), (17, 9), 4),
            ((16, 22), (23, 16), 3), ((16, 20), (17, 26), 3),
        ):
            _leaf(d, base, tip, width, c)
        d.line([(16, 20), (15, 17), (17, 15)], fill=c[6])
        for x, y in ((5, 10), (14, 3), (25, 7), (30, 17), (8, 28)):
            d.point((x, y), fill=c[9])
            d.point((x, y + 1), fill=c[11])
    elif name in ("flower_white", "flower_pink"):
        _flower(d, c)
    elif name == "mushroom":
        _polygon(d, [(13, 15), (19, 15), (19, 23), (22, 27), (20, 29),
                     (11, 29), (10, 27), (13, 23)], c[0])
        _polygon(d, [(14, 16), (18, 16), (18, 24), (20, 27), (17, 28),
                     (12, 27), (14, 22)], c[9])
        d.line([(14, 20), (15, 23), (14, 26)], fill=c[11])
        d.line([(17, 21), (17, 26), (19, 27)], fill=c[7])
        d.point((13, 27), fill=c[8])
        _polygon(d, [(2, 16), (5, 10), (10, 6), (15, 4), (21, 6),
                     (27, 10), (30, 16), (28, 19), (21, 21), (10, 21),
                     (3, 19)], c[0])
        _polygon(d, [(3, 16), (6, 11), (11, 7), (15, 5), (21, 7),
                     (26, 11), (29, 16), (24, 18), (8, 18)], c[3])
        _polygon(d, [(5, 14), (9, 9), (15, 6), (19, 7), (15, 10),
                     (13, 14)], c[4])
        d.line([(7, 12), (11, 8), (15, 7)], fill=c[6])
        d.line([(20, 9), (24, 11), (27, 15)], fill=c[2], width=2)
        d.line([(4, 18), (10, 20), (21, 20), (28, 18)], fill=c[7])
        for x in (8, 12, 17, 22, 26):
            d.line([(x, 18), (16 + (x - 16) // 2, 20)], fill=c[10])
        for x, y, w in ((10, 11, 3), (18, 8, 2), (22, 14, 3), (7, 15, 2)):
            d.line([(x, y), (x + w, y)], fill=c[11], width=2)
            d.point((x + w, y + 1), fill=c[8])
    elif name == "shell":
        _polygon(d, [(15, 27), (5, 19), (2, 13), (4, 8), (9, 5),
                     (14, 3), (20, 4), (26, 7), (29, 12), (27, 19),
                     (18, 27)], c[0])
        _polygon(d, [(15, 26), (6, 18), (3, 13), (5, 9), (10, 6),
                     (14, 4), (20, 5), (25, 8), (28, 12), (26, 18),
                     (18, 26)], c[3])
        for tx, ty in ((5, 11), (9, 7), (14, 5), (20, 6), (25, 10)):
            d.line([(16, 26), (tx, ty)], fill=c[1], width=2)
            d.line([(15, 24), (tx - 1, ty)], fill=c[5])
            d.line([(15, 22), (tx, ty + 2)], fill=c[4])
        d.line([(5, 12), (8, 8), (13, 5), (18, 5)], fill=c[6])
        d.line([(7, 17), (12, 21), (17, 23), (23, 18)], fill=c[8])
        d.line([(9, 17), (13, 20), (17, 21), (22, 17)], fill=c[9])
        _polygon(d, [(12, 26), (16, 24), (20, 26), (20, 29), (12, 29)], c[1])
        d.line([(13, 27), (16, 26), (19, 27)], fill=c[11])
        d.line([(13, 28), (18, 28)], fill=c[7])
        d.point((14, 26), fill=c[10])
    elif name == "crystal":
        for x, y, w, h in ((7, 13, 5, 15), (24, 10, 5, 18), (16, 2, 7, 27)):
            _polygon(d, [(x, y), (x + w, y + 6), (x + w - 1, y + h),
                         (x - w + 1, y + h), (x - w, y + 6)], c[0])
            _polygon(d, [(x, y + 1), (x, y + h - 1), (x - w + 2, y + h - 1),
                         (x - w + 1, y + 6)], c[3])
            _polygon(d, [(x + 1, y + 2), (x + w - 1, y + 6),
                         (x + w - 2, y + h - 1), (x + 1, y + h - 1)], c[1])
            _polygon(d, [(x, y + 1), (x + w - 1, y + 6), (x, y + 9),
                         (x - w + 1, y + 6)], c[4])
            d.line([(x - 1, y + 4), (x - 2, y + 6), (x - 2, y + 11)], fill=c[6])
            d.line([(x - 1, y + 13), (x - 1, y + h - 3)], fill=c[5])
            d.line([(x + 2, y + 8), (x + 2, y + 14)], fill=c[2])
            d.line([(x - 2, y + h - 5), (x + 2, y + h - 8)], fill=c[8])
            d.point((x - 1, y + h - 6), fill=c[10])
        d.line([(5, 28), (12, 29), (22, 29), (27, 27)], fill=c[7])
        d.point((24, 28), fill=c[9])
    elif name == "leaf_pile":
        for base, tip, width, offset in (
            ((15, 25), (3, 19), 4, 1), ((14, 24), (8, 7), 5, 7),
            ((15, 25), (24, 6), 5, 1), ((17, 25), (30, 17), 5, 7),
            ((14, 25), (3, 27), 3, 7), ((15, 25), (23, 27), 3, 1),
            ((15, 25), (14, 12), 4, 1),
        ):
            _leaf(d, base, tip, width, c, offset)
        d.line([(5, 28), (14, 26), (27, 27)], fill=c[1])
        d.line([(14, 27), (21, 25), (24, 24)], fill=c[5])
    elif name == "twig":
        for path, width in (
            ([(4, 28), (12, 21), (17, 14), (21, 4)], 5),
            ([(12, 21), (8, 13), (4, 10)], 4),
            ([(17, 14), (25, 12), (28, 6)], 3),
        ):
            d.line(path, fill=c[0], width=width + 2)
            d.line(path, fill=c[2], width=width)
            d.line([(x - 1, y - 1) for x, y in path], fill=c[4])
        d.line([(5, 27), (10, 23), (13, 19)], fill=c[5])
        d.line([(18, 10), (19, 8), (19, 5)], fill=c[3])
        d.line([(8, 15), (9, 17), (10, 18)], fill=c[1])
        d.line([(14, 19), (15, 17)], fill=c[6])
        _leaf(d, (23, 13), (30, 16), 3, c, 7)
        _leaf(d, (20, 9), (14, 4), 3, c, 7)
        d.point((21, 4), fill=c[6])
        d.point((4, 10), fill=c[5])
    elif name == "bone":
        _polygon(d, [(3, 24), (2, 20), (4, 17), (7, 17), (9, 19),
                     (20, 11), (19, 8), (21, 4), (24, 3), (26, 5),
                     (26, 7), (29, 7), (30, 10), (28, 14), (24, 14),
                     (13, 23), (13, 27), (10, 29), (7, 28), (7, 26)], c[0])
        _polygon(d, [(4, 23), (3, 20), (5, 18), (7, 18), (9, 20),
                     (22, 11), (20, 8), (22, 5), (24, 4), (25, 6),
                     (25, 8), (28, 8), (29, 10), (27, 13), (24, 13),
                     (12, 22), (12, 26), (10, 28), (8, 27), (8, 25)], c[3])
        _polygon(d, [(5, 19), (7, 19), (9, 21), (22, 11), (22, 8),
                     (23, 6), (24, 6), (24, 10), (11, 22), (7, 23),
                     (4, 22)], c[5])
        d.line([(10, 22), (17, 18), (24, 12), (27, 12)], fill=c[2])
        d.line([(9, 21), (15, 17), (20, 13)], fill=c[6])
        d.line([(8, 24), (10, 24), (10, 26)], fill=c[1])
        d.line([(26, 9), (27, 10), (26, 11)], fill=c[1])
        d.line([(13, 21), (16, 19)], fill=c[4])
        d.point((5, 20), fill=c[6])
        d.point((11, 26), fill=c[7])
        d.point((22, 7), fill=c[8])
        d.line([(18, 16), (20, 14)], fill=c[10])
    return image


def draw(name):
    """Keep filename, canvas, binary alpha and exact nearest-neighbor doubling."""
    if name not in NAMES:
        raise ValueError(f"Unknown core decor: {name}")
    return _draw(name).resize((64, 64), Image.Resampling.NEAREST)
