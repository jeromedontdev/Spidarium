"""Readable small-scale material details; glyph meaning and animation stay fixed."""

from PIL import Image, ImageDraw


ICON_DETAILS = {
    "feed": (((7, 15), (9, 17), (14, 17)), ((12, 8), (13, 9))),
    "clean": (((7, 13), (7, 16)), ((12, 18), (14, 17))),
    "play": (((7, 8), (9, 6)), ((14, 14), (16, 12))),
    "sleep": (((7, 9), (6, 12), (7, 15)), ((10, 18), (13, 19))),
    "decor": (((6, 17), (8, 15)), ((13, 13), (15, 11))),
    "status": (((7, 6), (10, 5)), ((15, 17), (17, 15))),
    "back": (((5, 12), (8, 9)), ((10, 15), (17, 15))),
    "hunger": (((6, 15), (8, 16)), ((15, 16), (17, 15))),
    "mood": (((7, 8), (9, 6)), ((15, 17), (17, 15))),
    "energy": (((12, 5), (10, 9)), ((12, 16), (14, 13))),
    "hygiene": (((8, 12), (8, 15)), ((12, 18), (15, 16))),
    "xp": (((11, 6), (10, 9)), ((15, 16), (16, 18))),
    "lock": (((9, 7), (10, 5), (13, 5)), ((7, 16), (7, 18))),
    "daily": (((5, 8), (17, 8)), ((7, 16), (8, 17))),
    "treats": (((7, 9), (9, 7)), ((15, 15), (17, 13))),
    "backgrounds": (((4, 17), (7, 14)), ((16, 11), (19, 14))),
    "achievements": (((6, 7), (8, 6)), ((11, 18), (14, 18))),
    "prey": (((9, 10), (11, 9)), ((8, 14), (10, 15))),
    "settings": (((7, 8), (9, 6)), ((15, 17), (17, 15))),
    "wifi": (((6, 8), (10, 6), (14, 6)), ((10, 14), (12, 13))),
    "sd": (((7, 4), (13, 4)), ((8, 17), (15, 17))),
    "about": (((7, 7), (9, 5)), ((14, 17), (16, 16))),
    "battery": (((5, 9), (5, 15)), ((8, 17), (17, 17))),
    "display": (((5, 6), (17, 6)), ((7, 16), (17, 16))),
    "sound": (((7, 10), (9, 9)), ((14, 8), (15, 10))),
    "auto_feed": (((6, 16), (9, 18)), ((15, 7), (17, 8))),
    "screenshot": (((5, 9), (8, 9)), ((14, 15), (16, 13))),
    "reset": (((7, 7), (10, 5)), ((15, 16), (17, 13))),
    "volume": (((5, 10), (7, 10)), ((11, 8), (11, 10))),
    "music": (((11, 5), (15, 4)), ((6, 17), (8, 16))),
}

BADGE_DETAILS = {
    "first_meal": (((5, 15), (7, 17)), ((15, 19), (17, 18))),
    "first_pet": (((6, 15), (8, 15)), ((16, 6), (18, 8))),
    "first_clean": (((6, 13), (6, 16)), ((11, 19), (13, 18))),
    "first_play": (((8, 7), (10, 5)), ((13, 11), (15, 9))),
    "first_hunt": (((5, 9), (7, 7)), ((13, 16), (15, 14))),
    "first_molt": (((5, 10), (6, 8)), ((17, 18), (19, 16))),
    "ten_meals": (((5, 17), (7, 18)), ((17, 19), (19, 18))),
    "adult": (((5, 5), (8, 6)), ((13, 20), (16, 18))),
    "level_5": (((4, 18), (6, 18)), ((17, 11), (19, 11))),
    "survived_sick": (((9, 4), (14, 4)), ((14, 18), (14, 20))),
    "first_perfect": (((9, 6), (11, 5)), ((14, 10), (16, 8))),
}

PREY_DETAILS = {
    "fly": (((8, 10), (10, 10)), ((7, 13), (9, 14))),
    "fruitfly": (((8, 10), (10, 10)), ((8, 13), (10, 13))),
    "moth": (((7, 7), (9, 8)), ((7, 16), (9, 15))),
    "beetle": (((6, 10), (8, 9), (10, 9)), ((7, 14), (10, 14))),
    "roach": (((6, 10), (9, 9)), ((6, 14), (9, 14))),
    "cricket": (((7, 11), (9, 11)), ((10, 13), (12, 13))),
    "mealworm": (((6, 11), (7, 11)), ((11, 13), (12, 13))),
    "waxworm": (((7, 10), (8, 10)), ((12, 14), (13, 14))),
}

SPLATTER_DETAILS = {
    name: (((6, 13), (8, 12)), ((13, 16), (15, 15)))
    for name in PREY_DETAILS
}


def finish(image, name, group):
    """Keep alpha and ink exact; add tiny broken highlights inside material planes."""
    recipes = (ICON_DETAILS if group == "icon" else BADGE_DETAILS if group == "badge" else
               SPLATTER_DETAILS if group == "splatter" else PREY_DETAILS)
    colors = sorted({pixel for pixel in image.get_flattened_data() if pixel[3]},
                    key=lambda p: p[0] * 299 + p[1] * 587 + p[2] * 114)
    if len(colors) < 3:
        raise ValueError(f"{group}/{name}: missing material ramp")
    ink, shadow, light = colors[0], colors[1], colors[-2]
    original = image.copy()
    overlay = Image.new("RGBA", image.size)
    draw = ImageDraw.Draw(overlay)
    for index, points in enumerate(recipes[name]):
        draw.line(points, fill=light if index == 0 else shadow)
    for y in range(image.height):
        for x in range(image.width):
            if original.getpixel((x, y))[3] and original.getpixel((x, y)) != ink:
                pixel = overlay.getpixel((x, y))
                if pixel[3]:
                    image.putpixel((x, y), pixel)
    return image
