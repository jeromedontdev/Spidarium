"""Deterministic SNES terrain and thin frames, based on the approved moss trial."""

import hashlib
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw


SOURCE = Path(__file__).resolve().parents[1] / "assets" / "sources"
MOSS_FLOOR_SHA = "af3f0f41a75732f076f185f792c8df7e63d750816147d99c78d28683c749f173"
MOSS_BORDER_SHA = "ac24829fa37058028b319dcc7e4893ce4b8b030f7836806b7f5f2e6f24b4b72c"

# Material, substrate midtone, secondary midtone, trim material, trim motif.
RECIPES = {
    "cork": ("bark", "796044", "8a704f", "wood", "cork"),
    "moss": ("moss", "4b603a", "4b6245", "stone", "fern"),
    "leaf_litter": ("litter", "74583c", "8c6c41", "wood", "leaves"),
    "sand": ("sand", "bc9b69", "b58e60", "sandstone", "shell"),
    "fern": ("moss", "345c3e", "4d6344", "wood", "fern"),
    "orchid": ("soil", "62485d", "695267", "wood", "petals"),
    "woodland": ("soil", "685238", "736344", "wood", "leaves"),
    "minimal": ("minimal", "adac8f", "adac8f", "stone", "plain"),
    "mushroom": ("moss", "5b6439", "6f7041", "wood", "mushroom"),
    "gothic": ("paving", "494556", "514959", "slate", "gothic"),
    "seashell": ("shore", "b69c78", "728d88", "sandstone", "shell"),
    "succulent": ("gravel", "586b61", "70775d", "stone", "succulent"),
    "willow": ("bank", "5f6f43", "3c6460", "wood", "willow"),
    "desert": ("dunes", "ae895c", "bf9765", "sandstone", "cactus"),
    "cemetery": ("paving", "4a5860", "5c6464", "slate", "gothic"),
    "house": ("wood", "876348", "8b6a4c", "wood", "cloth"),
    "dark_cave": ("mineral", "383b49", "47404d", "slate", "mineral"),
    "night": ("soil", "353b50", "42465c", "slate", "stars"),
    "autumn": ("litter", "88603b", "986b3b", "wood", "maple"),
    "snow": ("snow", "98acba", "869eae", "ice", "snow"),
    "pond": ("bank", "526646", "365a61", "stone", "reeds"),
    "ruins": ("flagstone", "797566", "636b5c", "stone", "runes"),
    "marsh": ("mud", "535b40", "3f5b50", "wood", "reeds"),
    "fairy_glade": ("moss", "4e6b46", "6c744b", "wood", "petals"),
    "moonflower_grove": ("moss", "3c5053", "485b5d", "slate", "moonflower"),
    "cathedral": ("paving", "626274", "56586b", "slate", "arches"),
    "rose_courtyard": ("flagstone", "756366", "636553", "stone", "roses"),
    "library": ("wood", "735442", "896346", "wood", "books"),
    "observatory": ("paving", "414e68", "535d73", "metal", "stars"),
    "kitchen": ("tile", "959890", "848d88", "ceramic", "checks"),
    "sewing_room": ("cloth", "876779", "97798a", "wood", "stitches"),
    "witch_garden": ("soil", "493d4c", "4a5441", "wood", "herbs"),
    "moon_altar": ("flagstone", "555264", "454858", "slate", "moon"),
    "primeval_jungle": ("moss", "3b5335", "4f6139", "wood", "cycad"),
    "amber_basin": ("gravel", "826039", "8e7041", "sandstone", "fossil"),
    "neon_alley": ("asphalt", "303b4d", "374254", "metal", "circuit"),
    "circuit_garden": ("metal", "344953", "3d545a", "metal", "circuit"),
    "lunar_base": ("regolith", "6c7380", "797e89", "metal", "bolts"),
    "starship": ("metal", "52657b", "5b6c7f", "metal", "panels"),
    "enchanted_court": ("flagstone", "66657b", "706680", "stone", "runes"),
    "dragon_heath": ("ash", "655045", "725e45", "slate", "ember"),
    "lost_city": ("flagstone", "898268", "77795c", "sandstone", "glyphs"),
    "sunken_temple": ("submerged", "41645e", "53716c", "stone", "glyphs"),
    "zen_garden": ("raked", "a39d80", "928f76", "stone", "zen"),
    "bamboo_grove": ("litter", "73764b", "636e45", "bamboo", "bamboo"),
    "sakura_courtyard": ("flagstone", "817679", "777467", "wood", "sakura"),
    "cloud_sanctuary": ("cloud", "929bab", "a6a7af", "ice", "stars"),
}
TRIM = {
    "wood": ("302f27", "776047", "b9a272", "375633", "a9bd66"),
    "stone": ("243832", "626e5d", "b5b197", "416138", "b0c371"),
    "slate": ("242835", "626779", "b6b5c8", "665376", "b9a2bf"),
    "sandstone": ("514638", "9b825d", "d3bd8b", "696447", "b6ad72"),
    "ice": ("34485d", "8babbc", "d8e5e6", "698ca9", "bfd6df"),
    "metal": ("1c2b3b", "5c7989", "b6cbd0", "37646b", "7dbcc3"),
    "ceramic": ("40535a", "96a6a5", "dddcc8", "68818c", "a5c4c5"),
    "bamboo": ("33412c", "778050", "c4c793", "41603b", "a5bb70"),
}


def color(text):
    return tuple(bytes.fromhex(text.removeprefix("#"))) + (255,)


def ramp(text, step=8):
    r, g, b, _ = color(text)
    return [(max(0, min(255, r + offset * step - offset)),
             max(0, min(255, g + offset * step)),
             max(0, min(255, b + offset * step + offset)), 255)
            for offset in range(-3, 5)]


def approved_moss(kind):
    expected = MOSS_FLOOR_SHA if kind == "floor" else MOSS_BORDER_SHA
    with Image.open(SOURCE / f"moss_{kind}.png") as image:
        image.load()
        if (image.format != "PNG" or image.mode != "RGBA" or image.size != (240, 240)
                or hashlib.sha256(image.tobytes()).hexdigest() != expected):
            raise ValueError(f"Moss {kind} differs from the approved offline trial")
        return image.copy()


def seed(name):
    return int.from_bytes(hashlib.sha256(("terrain-v2:" + name).encode()).digest()[:4], "little")


def leaf(draw, x, y, length, width, direction, colors):
    u, v = math.cos(direction), math.sin(direction)

    def at(a, b):
        return round(x + a * u - b * v), round(y + a * v + b * u)

    draw.polygon([at(0, 0), at(length * .35, -width),
                  at(length * .75, -width * .65), at(length, 0),
                  at(length * .55, width * .7), at(length * .2, width * .5)],
                 fill=colors[1])
    draw.polygon([at(0, 0), at(length * .35, -width),
                  at(length, 0), at(length * .5, 0)], fill=colors[3])
    draw.line([at(length * .2, 0), at(length * .7, 0)], fill=colors[4])
    draw.line([at(length * .4, 0), at(length * .55, -width * .7)], fill=colors[5])


def patch(draw, x, y, w, h, colors, tone):
    draw.polygon([(x, y + h // 2), (x + 2, y), (x + w // 2, y - 1),
                  (x + w - 1, y + 1), (x + w + 1, y + h - 1),
                  (x + w // 2, y + h + 1), (x + 1, y + h)], fill=colors[tone])
    draw.line([(x + 1, y + h), (x + w // 2, y + h + 1),
               (x + w - 1, y + h)], fill=colors[max(0, tone - 1)])


def ground_detail(draw, rng, colors, count=2200):
    for _ in range(count):
        x, y = rng.randrange(240), rng.randrange(240)
        length = rng.randrange(1, 4)
        tone = rng.choice((2, 3, 4, 4, 5))
        draw.line((x, y, x + length, y), fill=colors[tone])
        if length > 1 and rng.randrange(3) == 0:
            draw.line((x + 1, y + 1, x + length, y + 1), fill=colors[max(1, tone - 1)])


def paving(draw, rng, colors, mode):
    pitch = 30 if mode == "flagstone" else 24 if mode == "tile" else 40
    for row, y in enumerate(range(-pitch, 250, pitch)):
        shift = pitch // 2 if row % 2 else 0
        for x in range(-pitch + shift, 250, pitch):
            w = pitch - 1 + (rng.randrange(-4, 5) if mode == "flagstone" else 0)
            h = pitch - 1
            tone = rng.choice((2, 3, 3, 4))
            if mode == "tile":
                tone = 3 + ((row + x // pitch) % 2)
            draw.polygon([(x + 2, y), (x + w - 2, y), (x + w, y + 2),
                          (x + w, y + h - 2), (x + w - 2, y + h),
                          (x + 1, y + h), (x, y + h - 2), (x, y + 2)],
                         fill=colors[tone])
            draw.line((x + 3, y + 1, x + w - 3, y + 1), fill=colors[tone + 1])
            draw.line((x + 1, y + 3, x + 1, y + h - 3), fill=colors[tone + 1])
            draw.line((x + 3, y + h - 1, x + w - 3, y + h - 1), fill=colors[tone - 1])
            if mode != "tile":
                cx, cy = x + rng.randrange(4, pitch - 4), y + rng.randrange(4, pitch - 4)
                draw.line([(cx, cy), (cx + 2, cy + 2), (cx + 1, cy + 5)],
                          fill=colors[max(1, tone - 1)])
    ground_detail(draw, rng, colors, 1500 if mode != "tile" else 450)


def water(draw, rng, wet, mode):
    for y in range(240):
        if mode == "shore":
            edge = 24 + int(9 * math.sin(y / 40) + 4 * math.sin(y / 13))
            draw.line((0, y, edge, y), fill=wet[3])
            draw.point((edge, y), fill=wet[5])
        elif mode in ("bank", "mud"):
            edge = 220 + int(6 * math.sin(y / 23))
            draw.line((edge, y, 239, y), fill=wet[3])
    for _ in range(550):
        x, y = rng.randrange(240), rng.randrange(240)
        if ((mode == "shore" and x > 15)
                or (mode in ("bank", "mud") and x < 228)):
            continue
        draw.line((x, y, x + rng.randrange(2, 8), y), fill=wet[rng.choice((2, 4, 5))])
    if mode == "submerged":
        for _ in range(45):
            x, y = rng.randrange(240), rng.randrange(240)
            draw.line([(x, y), (x + 6, y - 2), (x + 12, y + 1)], fill=wet[5])


def background(name):
    if name == "moss":
        return approved_moss("floor")
    mode, base_hex, secondary_hex, _, motif = RECIPES[name]
    rng = random.Random(seed(name))
    base, secondary = ramp(base_hex), ramp(secondary_hex)
    image = Image.new("RGBA", (240, 240), base[3])
    draw = ImageDraw.Draw(image)
    if mode == "minimal":
        for _ in range(270):
            x, y = rng.randrange(240), rng.randrange(240)
            draw.line((x, y, x + 2, y), fill=base[rng.choice((2, 4))])
        return image
    if mode in ("paving", "flagstone", "tile", "submerged"):
        paving(draw, rng, base, mode)
        if mode == "submerged":
            water(draw, rng, secondary, mode)
    elif mode == "wood":
        pitch = 29
        for x in range(-pitch, 250, pitch):
            tone = rng.choice((2, 3, 4))
            draw.rectangle((x, 0, x + pitch - 2, 239), fill=base[tone])
            draw.line((x + 1, 0, x + 1, 239), fill=base[tone + 1])
            for _ in range(28):
                sx, sy = x + rng.randrange(3, pitch - 2), rng.randrange(240)
                draw.line([(sx, sy), (sx + 1, sy + 5), (sx, sy + rng.randrange(8, 22))],
                          fill=base[max(1, tone - 1)])
            yy = rng.randrange(45, 210)
            draw.line((x + 2, yy, x + pitch - 3, yy), fill=base[1])
            draw.ellipse((x + 5, yy + 9, x + 12, yy + 22), outline=base[2])
        ground_detail(draw, rng, base, 1400)
    elif mode == "bark":
        for _ in range(1300):
            x, y = rng.randrange(-4, 240), rng.randrange(-10, 240)
            w, h = rng.randrange(3, 10), rng.randrange(5, 17)
            tone = rng.choice((2, 3, 4))
            patch(draw, x, y, w, h, base, tone)
            draw.line((x + 2, y + 2, x + 2, y + h - 2), fill=base[tone + 1])
            draw.line((x + w - 1, y + 3, x + w - 1, y + h), fill=base[tone - 1])
            draw.line((x + 3, y + h - 1, x + w - 2, y + h - 1), fill=secondary[2])
        ground_detail(draw, rng, base, 1800)
    elif mode == "metal":
        paving(draw, rng, base, "paving")
        for y in range(20, 240, 40):
            for x in range(6, 240, 40):
                draw.rectangle((x, y, x + 3, y + 1), fill=secondary[4])
                draw.line((x, y + 4, x + 5, y + 4), fill=base[2])
                draw.point((x + 1, y + 1), fill=base[5])
        for y in (76, 156):
            draw.line((0, y, 239, y), fill=base[1])
            draw.line((0, y + 1, 239, y + 1), fill=base[4])
    elif mode == "cloth":
        for y in range(0, 240, 3):
            draw.line((0, y, 239, y), fill=base[2 if y % 6 else 4])
        for x in range(0, 240, 4):
            for y in range(x % 7, 240, 6):
                draw.line((x, y, x + 1, y + 1), fill=secondary[4])
        ground_detail(draw, rng, base, 1100)
    else:
        for _ in range(270):
            x, y = rng.randrange(-15, 240), rng.randrange(-8, 240)
            patch(draw, x, y, rng.randrange(8, 25), rng.randrange(3, 9),
                  base, rng.choice((2, 3, 3, 4)))
        ground_detail(draw, rng, base)
        if mode in ("moss", "soil", "litter", "bank", "mud"):
            density = 1500 if mode == "moss" else 850 if mode in ("bank", "mud") else 650
            for _ in range(density):
                x, y = rng.randrange(-6, 240), rng.randrange(-4, 240)
                w, h = rng.randrange(4, 12), rng.randrange(2, 5)
                tone = rng.choice((2, 3, 3, 4))
                growth = base if mode in ("bank", "mud") else secondary
                patch(draw, x, y, w, h, growth, tone)
                for _ in range(rng.randrange(3, 8)):
                    sx, sy = x + rng.randrange(w), y + rng.randrange(h)
                    length = rng.randrange(1, 4)
                    peak = min(6, tone + rng.choice((0, 1, 1, 2)))
                    draw.line((sx, sy, sx + length, sy - 1), fill=growth[peak])
                    draw.line((sx + 1, sy + 1, sx + length, sy + 1), fill=growth[max(1, peak - 2)])
            for _ in range(120 if mode == "moss" else 50):
                x, y = rng.randrange(240), rng.randrange(240)
                leaf(draw, x, y, rng.randrange(3, 7), 1, rng.uniform(-3, -.4),
                     base if mode in ("bank", "mud") else secondary)
        if mode in ("gravel", "regolith", "mineral", "ash"):
            for _ in range(1300):
                x, y = rng.randrange(240), rng.randrange(240)
                w, h = rng.randrange(2, 7), rng.randrange(1, 4)
                tone = rng.choice((2, 3, 4))
                patch(draw, x, y, w, h, secondary, tone)
                draw.line((x + 1, y, x + w - 1, y), fill=secondary[tone + 1])
        if mode in ("dunes", "raked"):
            for y in range(-10, 250, 7 if mode == "raked" else 17):
                points = [(x, y + int(3 * math.sin(x / 24 + y / 18))) for x in range(240)]
                draw.line(points, fill=base[2])
                draw.line([(x, yy + 1) for x, yy in points], fill=base[4])
        if mode in ("snow", "cloud"):
            for _ in range(550 if mode == "snow" else 180):
                x, y = rng.randrange(240), rng.randrange(240)
                w, h = rng.randrange(3, 10), rng.randrange(1, 4)
                patch(draw, x, y, w, h, secondary, rng.choice((3, 4)))
                draw.line((x + 1, y, x + w - 1, y), fill=base[5])
        if mode in ("shore", "bank", "mud"):
            water(draw, rng, secondary, mode)

    # Surface accents are tiny and stay out of the playable center.
    for _ in range(85):
        side = rng.randrange(4)
        along, inward = rng.randrange(240), rng.randrange(8, 33)
        x, y = ((along, inward), (239 - inward, along),
                (along, 239 - inward), (inward, along))[side]
        if motif in ("leaves", "fern", "cycad", "willow", "herbs", "bamboo"):
            leaf(draw, x, y, rng.randrange(4, 9), 2, rng.uniform(-3, 0), secondary)
        elif motif in ("petals", "roses", "moonflower", "sakura", "maple"):
            petal_colors = ramp("967079" if motif != "maple" else "a97a48")
            leaf(draw, x, y, rng.randrange(3, 6), 1, rng.uniform(-3, 0), petal_colors)
        elif motif == "mushroom":
            draw.rectangle((x, y, x + 1, y + 3), fill=base[5])
            draw.line((x - 1, y, x + 2, y), fill=secondary[5])
        elif motif in ("runes", "glyphs", "gothic", "arches"):
            draw.line([(x, y), (x, y + 3), (x + 2, y + 3)], fill=secondary[2])
        elif motif in ("circuit", "panels", "bolts"):
            draw.line([(x, y), (x + 4, y), (x + 4, y + 3)], fill=secondary[5])
        elif motif == "ember":
            draw.line((x, y, x + 1, y), fill=color("98704b"))
    return image


def stone(draw, x, y, w, h, c, rng):
    draw.polygon([(x, y + 2), (x + 2, y), (x + w - 3, y), (x + w - 1, y + 2),
                  (x + w - 2, y + h - 1), (x + 2, y + h), (x, y + h - 1)], fill=c[0])
    draw.polygon([(x + 1, y + 2), (x + 3, y + 1), (x + w - 3, y + 1),
                  (x + w - 2, y + 3), (x + w - 3, y + h - 1), (x + 2, y + h - 1)],
                 fill=c[3])
    draw.polygon([(x + 2, y + 2), (x + 3, y + 1), (x + w - 3, y + 1),
                  (x + w - 4, y + 3), (x + 3, y + 3)], fill=c[5])
    draw.line((x + 3, y + 1, x + w - 4, y + 1), fill=c[6])
    draw.line((x + 3, y + h - 1, x + w - 3, y + h - 1), fill=c[2])
    cx = x + rng.randrange(3, max(4, w - 2))
    draw.line([(cx, y + 2), (cx - 1, y + 3), (cx + 1, y + h - 1)], fill=c[2])


def border(name):
    if name == "moss":
        return approved_moss("border")
    _, _, _, material, motif = RECIPES[name]
    ink, middle, light, accent, highlight = TRIM[material]
    accent, highlight = {
        "petals": ("92648a", "ddb4c8"), "roses": ("92516a", "d796a0"),
        "sakura": ("a4748e", "e1c1cc"), "maple": ("a3723e", "d5b66b"),
        "mushroom": ("776189", "bfa5c4"), "moonflower": ("73779d", "c6cce1"),
        "ember": ("986143", "c79962"), "fossil": ("a58752", "d9c091"),
        "circuit": ("547c85", "9fc9cb") if name == "circuit_garden" else ("825074", "c292b5"),
        "books": ("646d67", "bfc3a8"),
    }.get(motif, (accent, highlight))
    c = [color(ink)] + ramp(middle)[:6] + [color(light)]
    a = ramp(accent)[:6] + [color(highlight)]
    rng = random.Random(seed(name) + 1)
    image = Image.new("RGBA", (120, 120))
    for side in range(4):
        strip = Image.new("RGBA", (120, 8))
        draw = ImageDraw.Draw(strip)
        draw.line((0, 1, 119, 1), fill=c[1])
        x = -3
        while x < 120:
            w, h = rng.randrange(8, 16), rng.randrange(4, 7)
            y = rng.choice((0, 0, 1))
            if material == "bamboo":
                draw.rectangle((x, y + 1, x + w, y + h), fill=c[3], outline=c[0])
                draw.line((x + 1, y + 1, x + w - 1, y + 1), fill=c[6])
                draw.line((x + 2, y + 3, x + w - 3, y + 3), fill=c[2])
                draw.point((x + 3, y + h - 1), fill=c[4])
                draw.line((x + w - 2, y + 1, x + w - 2, y + h), fill=c[6])
            elif material == "metal":
                draw.rectangle((x, 1, x + w - 1, 5), fill=c[3], outline=c[0])
                draw.line((x + 1, 1, x + w - 2, 1), fill=c[6])
                draw.point((x + 2, 3), fill=c[7])
            else:
                stone(draw, x, y, w, h, c, rng)
                if material == "wood":
                    draw.line((x + 2, y + 3, x + w - 4, y + 3), fill=c[2])
                    draw.point((x + 4, y + h - 1), fill=c[4])
            if motif in ("fern", "cycad", "herbs", "willow", "leaves", "reeds", "zen"):
                for sx in range(x + 2, x + w - 2, 2):
                    sy = rng.randrange(1, 3)
                    draw.line((sx, sy, sx + 2, sy), fill=a[rng.choice((3, 4, 5))])
                    draw.point((sx, sy + 1), fill=a[1])
            x += w - rng.randrange(0, 3)
        for anchor in (8, 26, 48, 70, 91, 112):
            x = anchor + rng.randrange(-2, 3)
            if motif in ("fern", "cycad", "herbs", "willow", "reeds"):
                draw.line((x, 7, x + 4, 1), fill=a[2])
                for j in range(3):
                    xx, yy = x + j + 1, 6 - j
                    draw.line((xx, yy, xx - 2, yy - 1), fill=a[4])
                    draw.line((xx + 1, yy - 1, xx + 3, yy - 1), fill=a[3])
            elif motif in ("leaves", "succulent", "bamboo", "zen", "cactus", "cork"):
                leaf(draw, x, 6, 7, 2, -1.1, a)
                leaf(draw, x + 3, 5, 5, 2, -2.7, a)
            elif motif in ("petals", "roses", "moonflower", "sakura", "maple"):
                draw.line((x, 7, x + 2, 4), fill=a[2])
                draw.polygon([(x, 3), (x + 2, 1), (x + 4, 3),
                              (x + 2, 5)], fill=a[4])
                draw.point((x + 2, 3), fill=a[6])
                leaf(draw, x, 6, 4, 1, -2.4, a)
            elif motif == "mushroom":
                draw.rectangle((x + 1, 4, x + 2, 7), fill=c[7])
                draw.polygon([(x - 1, 4), (x, 2), (x + 3, 2), (x + 4, 4)], fill=a[4])
                draw.line((x, 4, x + 3, 4), fill=a[1])
                draw.point((x + 1, 2), fill=a[6])
            elif motif in ("shell", "fossil"):
                draw.arc((x - 2, 1, x + 4, 7), 180, 355, fill=c[7], width=1)
                draw.line((x + 1, 5, x - 1, 3), fill=a[4])
                draw.line((x + 1, 5, x + 1, 2), fill=c[6])
                draw.line((x + 1, 5, x + 3, 3), fill=a[4])
            elif motif in ("runes", "glyphs", "gothic", "arches", "moon"):
                if motif == "arches":
                    draw.arc((x - 2, 1, x + 4, 9), 180, 360, fill=c[7])
                else:
                    draw.line([(x, 6), (x, 2), (x + 3, 2), (x + 3, 4),
                               (x + 1, 4), (x + 1, 5)], fill=a[5])
                draw.point((x - 1, 2), fill=c[6])
            elif motif in ("circuit", "panels", "bolts"):
                draw.line([(x - 3, 6), (x, 6), (x + 2, 3), (x + 5, 3)], fill=a[4])
                draw.rectangle((x + 3, 2, x + 5, 4), fill=a[1], outline=a[6])
            elif motif in ("snow", "stars", "mineral", "ember"):
                draw.polygon([(x, 1), (x + 3, 4), (x, 7), (x - 2, 4)], fill=a[3])
                draw.line((x, 2, x, 5), fill=a[6])
                if motif == "stars":
                    draw.line((x - 2, 4, x + 3, 4), fill=a[5])
            elif motif == "books":
                draw.rectangle((x - 2, 2, x + 3, 6), fill=a[1], outline=c[0])
                draw.line((x - 1, 3, x + 2, 3), fill=c[7])
                draw.line((x - 1, 5, x + 2, 5), fill=c[6])
            elif motif in ("cloth", "stitches", "checks"):
                draw.line((x - 2, 5, x + 3, 5), fill=a[4])
                draw.line((x, 3, x, 7), fill=a[6])
        if side:
            strip = strip.transpose((Image.Transpose.ROTATE_90, Image.Transpose.ROTATE_180,
                                     Image.Transpose.ROTATE_270)[side - 1])
        image.alpha_composite(strip, ((0, 0), (0, 0), (0, 112), (112, 0))[side])
    return image.resize((240, 240), Image.Resampling.NEAREST)
