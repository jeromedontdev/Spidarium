"""Dedicated 24px companion glyphs: finite material planes, never palette swaps."""

from PIL import Image, ImageDraw


NAMES = (
    "personality", "bond", "favorite_treat", "favorite_place", "favorite_game",
    "today", "checkin", "request_pet", "request_play", "request_hunt",
    "request_visit", "request_clean", "diary", "memory_treat", "memory_place",
    "memory_game", "memory_bond", "memory_growth", "memory_recovery",
    "memory_achievement", "memory_requests", "pin", "unpin", "dismiss",
    "usb_chimes", "music_volume", "soundtrack", "daily_task",
    "training", "trick_wave", "trick_spin", "trick_bow", "perform", "practice",
    "album", "camera", "photo", "calendar", "daylight", "weather", "visitor",
    "season_reward", "generations", "retire", "resume", "confirm_cancel", "activity",
    "delete_photo", "light_gesture", "enclosure_light",
)

INK = "#18253b"
CREAM = "#fff0ce"
RAMPS = {
    "gold": ("#785035", "#b4773d", "#e7a64e", "#ffda88"),
    "mint": ("#28545a", "#368579", "#72c5a0", "#bce6c1"),
    "rose": ("#703f63", "#ad577b", "#e68eaa", "#ffd2c9"),
    "blue": ("#35466e", "#536fa3", "#8ba7dc", "#c5dcf2"),
    "wood": ("#563d3e", "#8b5c4c", "#bf8b65", "#ebc699"),
    "violet": ("#4b3b71", "#80629c", "#b79bce", "#e5cee9"),
}


class Canvas:
    def __init__(self):
        self.image = Image.new("RGBA", (24, 24))
        self.d = ImageDraw.Draw(self.image)

    def shape(self, points, material="gold"):
        """A stepped ink rim with four connected, top-left-lit material planes."""
        mask = Image.new("1", (24, 24))
        ImageDraw.Draw(mask).polygon(points, fill=1)
        x0, y0, x1, y1 = mask.getbbox()
        ramp = RAMPS[material]
        for y in range(y0, y1):
            for x in range(x0, x1):
                if not mask.getpixel((x, y)):
                    continue
                edge = any(not (0 <= x + dx < 24 and 0 <= y + dy < 24)
                           or not mask.getpixel((x + dx, y + dy))
                           for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)))
                position = (x - x0) + 2 * (y - y0)
                span = (x1 - x0) + 2 * (y1 - y0)
                tone = 3 if position < span // 3 else 2 if position < span * 3 // 5 else 1
                if y >= y1 - 3 or x >= x1 - 2:
                    tone = 0
                self.d.point((x, y), fill=INK if edge else ramp[tone])

    def box(self, bounds, material="wood"):
        x0, y0, x1, y1 = bounds
        self.shape(((x0, y0), (x1, y0), (x1, y1), (x0, y1)), material)

    def gem(self, bounds, material="gold"):
        x0, y0, x1, y1 = bounds
        self.shape(((x0 + 2, y0), (x1 - 2, y0), (x1, y0 + 2),
                    (x1, y1 - 2), (x1 - 2, y1), (x0 + 2, y1),
                    (x0, y1 - 2), (x0, y0 + 2)), material)

    def line(self, points, color=CREAM, width=1):
        self.d.line(points, fill=color, width=width)

    def heart(self, x, y, material="rose"):
        self.shape(((x, y + 1), (x + 1, y), (x + 3, y), (x + 4, y + 1),
                    (x + 5, y), (x + 7, y), (x + 8, y + 1),
                    (x + 8, y + 4), (x + 4, y + 8), (x, y + 4)), material)

    def check(self, x, y, color=CREAM):
        self.line(((x, y + 2), (x + 2, y + 4), (x + 6, y)), INK, 3)
        self.line(((x, y + 2), (x + 2, y + 4), (x + 6, y)), color)


def draw(name):
    """Return exact nearest-neighbor 48px artwork for one semantic action."""
    if name not in NAMES:
        raise ValueError(f"Unknown companion icon: {name}")
    c = Canvas()
    if name == "personality":
        # A two-expression theatre mask, not the circular mood indicator.
        c.shape(((4, 4), (10, 5), (18, 3), (20, 7), (19, 16),
                 (15, 21), (10, 20), (5, 15)), "violet")
        c.shape(((5, 5), (11, 6), (11, 18), (8, 16), (6, 12)), "gold")
        c.line(((7, 9), (9, 8), (10, 9)), INK)
        c.line(((14, 8), (17, 7)), INK)
        c.line(((8, 13), (10, 15), (13, 16), (16, 13)), INK)
        c.line(((13, 5), (13, 11)), CREAM)
    elif name == "bond":
        c.gem((2, 8, 14, 19), "gold")
        c.d.rectangle((6, 11, 10, 15), fill=(0, 0, 0, 0))
        c.gem((10, 3, 21, 14), "mint")
        c.d.rectangle((14, 6, 17, 10), fill=(0, 0, 0, 0))
        c.line(((9, 14), (14, 9)), CREAM, 2)
    elif name == "favorite_treat":
        c.line(((5, 21), (18, 4)), INK, 3)
        c.line(((5, 20), (18, 4)), RAMPS["gold"][0])
        c.gem((6, 10, 15, 18), "rose")
        c.gem((11, 4, 20, 12), "gold")
        c.line(((8, 12), (11, 12)), CREAM)
        c.shape(((3, 4), (6, 5), (8, 3), (8, 8), (5, 10), (3, 8)), "mint")
    elif name == "favorite_place":
        c.shape(((2, 9), (8, 6), (14, 9), (21, 6), (21, 19),
                 (14, 22), (8, 19), (2, 22)), "mint")
        c.line(((8, 8), (8, 18)), RAMPS["mint"][0])
        c.line(((14, 11), (14, 20)), CREAM)
        c.heart(9, 2)
    elif name == "favorite_game":
        c.shape(((3, 7), (14, 3), (21, 8), (21, 18), (10, 22), (3, 17)), "blue")
        c.line(((4, 8), (11, 12), (20, 8)), CREAM)
        c.line(((11, 12), (11, 21)), RAMPS["blue"][0])
        c.heart(11, 10)
        c.box((5, 11, 7, 13), "gold")
        c.box((6, 16, 8, 18), "gold")
    elif name == "today":
        c.gem((6, 6, 18, 18), "gold")
        for points in (((12, 1), (12, 4)), ((3, 5), (5, 7)),
                       ((20, 5), (18, 7)), ((2, 11), (4, 11)),
                       ((20, 11), (22, 11))):
            c.line(points, CREAM)
        c.shape(((2, 16), (6, 14), (11, 16), (16, 14), (21, 16),
                 (21, 20), (2, 20)), "mint")
        c.line(((5, 18), (10, 18)), CREAM)
    elif name == "checkin":
        c.shape(((5, 2), (15, 2), (15, 8), (18, 11), (18, 15),
                 (3, 15), (3, 11), (7, 8), (5, 6)), "wood")
        c.box((2, 15, 20, 20), "gold")
        c.line(((4, 17), (18, 17)), CREAM)
        c.check(13, 6, RAMPS["mint"][2])
    elif name == "request_pet":
        c.shape(((5, 20), (3, 13), (4, 11), (7, 13), (7, 5),
                 (9, 4), (10, 10), (11, 3), (13, 3), (14, 10),
                 (16, 5), (18, 6), (17, 12), (20, 10), (21, 12),
                 (17, 20), (14, 22), (8, 22)), "gold")
        c.line(((9, 14), (12, 12), (15, 14)), RAMPS["gold"][0])
        c.line(((7, 16), (8, 19), (11, 20)), CREAM)
        c.line(((2, 5), (4, 5)), RAMPS["rose"][2], 2)
    elif name == "request_play":
        c.box((11, 2, 13, 7), "wood")
        c.shape(((4, 10), (10, 6), (15, 6), (21, 10),
                 (13, 21), (11, 21)), "rose")
        c.line(((5, 11), (19, 11)), CREAM)
        c.shape(((10, 12), (15, 12), (12, 19)), "gold")
        c.line(((2, 16), (2, 19), (5, 21)), RAMPS["mint"][2])
    elif name == "request_hunt":
        c.line(((2, 8), (2, 3), (7, 3)), RAMPS["gold"][2], 2)
        c.line(((17, 20), (22, 20), (22, 15)), RAMPS["gold"][1], 2)
        c.shape(((11, 10), (7, 5), (4, 7), (5, 12), (10, 14)), "mint")
        c.shape(((13, 10), (17, 5), (20, 7), (19, 12), (14, 14)), "mint")
        c.gem((9, 9, 15, 18), "gold")
        c.line(((12, 7), (12, 15)), CREAM)
        c.line(((6, 17), (9, 15)), RAMPS["gold"][3])
    elif name == "request_visit":
        c.shape(((4, 21), (4, 6), (8, 2), (16, 2), (20, 6), (20, 21)), "wood")
        c.d.polygon(((8, 20), (8, 7), (11, 5), (16, 6), (16, 20)), fill=INK)
        c.shape(((8, 6), (14, 9), (14, 21), (8, 19)), "gold")
        c.line(((11, 14), (11, 15)), CREAM)
        c.line(((16, 17), (21, 17), (19, 15)), CREAM)
    elif name == "request_clean":
        c.shape(((3, 13), (7, 10), (20, 12), (20, 19),
                 (16, 22), (3, 20)), "gold")
        c.line(((4, 15), (16, 17), (19, 14)), CREAM)
        c.line(((7, 18), (9, 18)), RAMPS["gold"][0])
        c.line(((13, 20), (15, 20)), RAMPS["gold"][0])
        c.gem((5, 3, 12, 10), "blue")
        c.gem((15, 2, 20, 7), "mint")
        c.line(((7, 5), (9, 5)), CREAM)
    elif name == "diary":
        c.shape(((5, 3), (17, 3), (19, 5), (19, 21),
                 (5, 21), (3, 19), (3, 5)), "wood")
        c.line(((6, 4), (6, 19)), RAMPS["gold"][3])
        c.line(((7, 19), (17, 19)), CREAM)
        c.box((14, 8, 21, 12), "gold")
        c.shape(((9, 4), (12, 4), (12, 13), (10, 11), (9, 13)), "rose")
    elif name == "memory_treat":
        c.shape(((5, 5), (10, 2), (16, 3), (16, 7), (20, 8),
                 (18, 11), (21, 14), (19, 19), (13, 22),
                 (6, 20), (2, 15), (2, 9)), "gold")
        for x, y in ((7, 7), (12, 6), (6, 13), (12, 16), (16, 14), (9, 19)):
            c.line(((x, y), (x + 1, y)), RAMPS["wood"][0], 2)
        c.line(((4, 9), (4, 12)), CREAM)
    elif name == "memory_place":
        c.shape(((2, 4), (21, 6), (19, 21), (1, 18)), "mint")
        c.shape(((4, 6), (19, 8), (18, 16), (3, 14)), "blue")
        c.shape(((3, 14), (9, 8), (12, 12), (15, 10), (18, 16)), "mint")
        c.line(((4, 17), (11, 18)), CREAM)
        c.gem((14, 2, 21, 8), "mint")
    elif name == "memory_game":
        c.box((2, 17, 21, 21), "wood")
        for x in (4, 10, 16):
            c.box((x, 18, x + 2, 20), "gold")
        c.shape(((7, 15), (9, 12), (9, 8), (7, 7), (7, 3),
                 (10, 3), (10, 5), (13, 5), (13, 3), (16, 3),
                 (16, 7), (14, 8), (14, 12), (17, 15)), "violet")
        c.line(((10, 9), (10, 12)), CREAM)
    elif name == "memory_bond":
        c.shape(((2, 9), (5, 6), (10, 8), (13, 12), (9, 19),
                 (6, 19), (2, 15)), "blue")
        c.shape(((21, 7), (18, 5), (13, 8), (9, 12), (12, 18),
                 (15, 19), (21, 13)), "gold")
        c.line(((9, 11), (14, 15), (16, 13)), RAMPS["gold"][0])
        c.line(((5, 9), (8, 11)), CREAM)
        c.line(((13, 2), (13, 4)), RAMPS["rose"][2], 2)
    elif name == "memory_growth":
        c.box((16, 2, 21, 22), "gold")
        for y in (5, 9, 13, 17):
            c.line(((17, y), (19, y)), INK)
        c.line(((9, 20), (9, 9)), INK, 3)
        c.line(((9, 19), (9, 9)), RAMPS["mint"][2])
        c.shape(((9, 13), (4, 11), (2, 6), (7, 7), (10, 10)), "mint")
        c.shape(((9, 9), (10, 4), (14, 2), (14, 7)), "mint")
        c.shape(((3, 20), (7, 18), (12, 19), (14, 22), (3, 22)), "wood")
    elif name == "memory_recovery":
        c.box((8, 2, 16, 5), "wood")
        c.shape(((9, 5), (15, 5), (15, 8), (19, 12), (19, 20),
                 (17, 22), (7, 22), (5, 20), (5, 12), (9, 8)), "blue")
        c.box((7, 14, 17, 19), "mint")
        c.line(((12, 10), (12, 18)), CREAM, 2)
        c.line(((9, 13), (15, 13)), CREAM, 2)
        c.line(((7, 10), (7, 12)), RAMPS["blue"][3])
    elif name == "memory_achievement":
        c.shape(((6, 13), (11, 15), (9, 22), (6, 19), (3, 21)), "rose")
        c.shape(((13, 15), (18, 13), (21, 21), (17, 19), (15, 22)), "rose")
        c.gem((5, 2, 19, 16), "gold")
        c.shape(((12, 4), (14, 7), (17, 8), (14, 10),
                 (14, 13), (12, 12), (9, 13), (9, 10), (7, 8), (10, 7)), "rose")
        c.line(((8, 5), (10, 4)), CREAM)
    elif name == "memory_requests":
        c.shape(((2, 7), (12, 2), (21, 7), (21, 20), (2, 20)), "wood")
        c.shape(((3, 8), (12, 13), (20, 8), (20, 19), (3, 19)), "gold")
        c.line(((3, 19), (8, 14)), CREAM)
        c.check(10, 10, RAMPS["mint"][3])
        c.check(14, 15, RAMPS["mint"][3])
    elif name == "pin":
        c.line(((11, 15), (11, 22)), INK, 3)
        c.line(((11, 15), (11, 21)), CREAM)
        c.shape(((6, 3), (17, 3), (17, 6), (15, 7), (15, 11),
                 (19, 14), (19, 16), (4, 16), (4, 14), (8, 11),
                 (8, 7), (6, 6)), "rose")
        c.line(((9, 5), (14, 5)), CREAM)
    elif name == "unpin":
        c.line(((12, 12), (3, 21)), INK, 3)
        c.line(((12, 12), (4, 20)), CREAM)
        c.shape(((11, 2), (21, 10), (19, 13), (16, 12), (13, 15),
                 (13, 18), (10, 18), (5, 13), (6, 10), (9, 10),
                 (12, 7), (10, 5)), "blue")
        c.line(((14, 5), (18, 8)), CREAM)
        c.line(((18, 18), (21, 14), (21, 18)), RAMPS["gold"][2], 2)
    elif name == "dismiss":
        c.shape(((3, 4), (15, 4), (19, 8), (19, 21), (3, 21)), "blue")
        c.shape(((14, 4), (14, 9), (19, 9)), "gold")
        c.line(((6, 8), (10, 8)), CREAM)
        c.line(((6, 12), (11, 12)), CREAM)
        c.line(((12, 14), (20, 21)), INK, 3)
        c.line(((20, 14), (12, 21)), INK, 3)
        c.line(((12, 14), (20, 21)), RAMPS["rose"][2])
        c.line(((20, 14), (12, 21)), RAMPS["rose"][2])
    elif name == "usb_chimes":
        c.box((3, 2, 9, 7), "gold")
        c.box((2, 6, 10, 13), "blue")
        c.line(((5, 3), (5, 5)), CREAM)
        c.line(((7, 3), (7, 5)), INK)
        c.line(((6, 13), (6, 19), (10, 21), (14, 19)), INK, 3)
        c.line(((6, 13), (6, 19), (10, 21), (14, 19)), RAMPS["blue"][2])
        c.shape(((12, 16), (14, 12), (14, 8), (16, 6), (18, 6),
                 (20, 8), (20, 12), (22, 16)), "gold")
        c.box((16, 17, 18, 19), "wood")
        c.line(((16, 8), (16, 11)), CREAM)
    elif name == "music_volume":
        c.shape(((3, 3), (14, 3), (14, 7), (11, 7), (9, 18),
                 (6, 20), (3, 18)), "violet")
        c.line(((5, 6), (5, 16), (7, 18)), CREAM)
        c.line(((8, 6), (7, 15)), RAMPS["gold"][3])
        for x, top in ((12, 17), (16, 12), (20, 7)):
            c.box((x, top, x + 2, 21), "mint")
    elif name == "soundtrack":
        c.shape(((2, 3), (15, 3), (17, 5), (17, 21), (2, 21)), "rose")
        c.gem((7, 5, 22, 20), "blue")
        c.gem((12, 10, 17, 15), "rose")
        c.line(((10, 9), (12, 7), (16, 7)), CREAM)
        c.line(((19, 13), (19, 16), (17, 18)), RAMPS["blue"][0])
        c.line(((4, 6), (6, 6)), RAMPS["rose"][3])
    elif name == "daily_task":
        c.shape(((6, 3), (19, 3), (21, 5), (21, 8), (18, 8),
                 (18, 20), (16, 22), (4, 22), (2, 20), (2, 17),
                 (5, 17), (5, 5)), "gold")
        c.line(((7, 5), (18, 5)), CREAM)
        c.line(((5, 18), (5, 20), (15, 20)), RAMPS["wood"][0])
        c.check(7, 8, RAMPS["mint"][0])
        c.line(((8, 15), (14, 15)), RAMPS["wood"][0])
        c.line(((8, 17), (12, 17)), RAMPS["wood"][0])
    elif name == "training":
        c.shape(((3, 8), (7, 5), (9, 7), (9, 10), (15, 10),
                 (15, 7), (17, 5), (21, 8), (21, 16), (17, 19),
                 (15, 17), (15, 14), (9, 14), (9, 17), (7, 19),
                 (3, 16)), "blue")
        c.line(((9, 11), (15, 11)), CREAM, 2)
        c.line(((9, 13), (15, 13)), RAMPS["gold"][2], 2)
    elif name == "trick_wave":
        c.shape(((4, 20), (3, 15), (5, 12), (7, 13), (7, 7),
                 (9, 5), (11, 6), (11, 11), (12, 3), (14, 2),
                 (16, 4), (15, 11), (17, 6), (19, 6), (20, 8),
                 (18, 15), (15, 21), (9, 22)), "rose")
        c.line(((9, 15), (12, 17), (15, 15)), RAMPS["rose"][0])
        c.line(((3, 5), (5, 3)), RAMPS["gold"][2], 2)
        c.line(((19, 3), (21, 5)), RAMPS["gold"][2], 2)
    elif name == "trick_spin":
        c.gem((6, 6, 18, 18), "violet")
        c.d.rectangle((9, 9, 15, 15), fill=INK)
        c.line(((11, 12), (14, 12), (14, 9)), CREAM, 2)
        c.line(((3, 8), (4, 4), (8, 3)), RAMPS["mint"][2], 2)
        c.line(((16, 3), (20, 4), (21, 8)), RAMPS["gold"][2], 2)
        c.line(((21, 16), (20, 20), (16, 21)), RAMPS["rose"][2], 2)
    elif name == "trick_bow":
        c.shape(((4, 15), (5, 10), (8, 7), (11, 7), (13, 10),
                 (15, 7), (18, 7), (21, 11), (19, 15),
                 (16, 13), (14, 19), (11, 22), (8, 18),
                 (6, 20), (3, 18)), "wood")
        c.line(((10, 11), (12, 14), (15, 11)), RAMPS["gold"][3])
        c.line(((8, 4), (10, 2)), RAMPS["rose"][2], 2)
        c.line(((16, 3), (18, 5)), RAMPS["rose"][2], 2)
    elif name == "perform":
        c.shape(((2, 3), (7, 5), (7, 19), (2, 21)), "rose")
        c.shape(((22, 3), (17, 5), (17, 19), (22, 21)), "rose")
        c.shape(((7, 5), (17, 5), (17, 8), (7, 8)), "gold")
        c.gem((8, 9, 16, 18), "blue")
        c.line(((12, 10), (12, 16), (14, 13)), CREAM)
    elif name == "practice":
        c.shape(((8, 2), (16, 2), (14, 20), (10, 20)), "wood")
        c.shape(((9, 16), (15, 16), (17, 21), (7, 21)), "blue")
        c.gem((9, 5, 15, 11), "blue")
        c.line(((12, 7), (12, 9), (14, 9)), CREAM)
        c.line(((5, 4), (4, 7), (5, 10)), CREAM, 2)
        c.line(((19, 4), (20, 7), (19, 10)), CREAM, 2)
    elif name == "album":
        c.shape(((2, 8), (9, 5), (12, 7), (12, 21), (8, 18),
                 (3, 20)), "wood")
        c.shape(((12, 7), (15, 5), (22, 8), (21, 20),
                 (16, 18), (12, 21)), "blue")
        c.gem((5, 8, 9, 12), "blue")
        c.line(((5, 15), (9, 14)), CREAM)
        c.gem((15, 8, 19, 12), "blue")
        c.line(((15, 15), (19, 14)), CREAM)
    elif name == "delete_photo":
        c.shape(((4, 9), (18, 9), (17, 22), (6, 22)), "blue")
        c.shape(((2, 6), (19, 3), (21, 6), (3, 9)), "rose")
        c.line(((8, 4), (8, 2), (13, 1), (15, 3)), INK, 2)
        c.line(((9, 12), (14, 18)), CREAM, 2)
        c.line(((14, 12), (9, 18)), CREAM, 2)
    elif name == "light_gesture":
        c.shape(((2, 20), (3, 13), (5, 11), (7, 13), (8, 6), (10, 5),
                 (12, 7), (11, 12), (16, 11), (18, 14), (15, 21), (8, 22)), "rose")
        c.gem((15, 2, 20, 7), "gold")
        c.line(((19, 10), (22, 8)), RAMPS["blue"][2], 2)
        c.line(((13, 3), (12, 1)), RAMPS["blue"][2])
    elif name == "enclosure_light":
        c.box((3, 4, 21, 21), "wood")
        c.d.rectangle((6, 7, 18, 18), fill=INK)
        c.shape(((8, 9), (11, 7), (15, 7), (17, 10), (15, 13),
                 (14, 16), (11, 16), (10, 13)), "gold")
        c.line(((11, 18), (14, 18)), CREAM)
    elif name == "camera":
        c.shape(((2, 8), (7, 8), (9, 5), (14, 5), (16, 8),
                 (21, 8), (22, 16), (19, 19), (5, 19), (2, 16)), "blue")
        c.gem((8, 8, 16, 17), "gold")
        c.d.rectangle((10, 10, 14, 15), fill=INK)
        c.gem((17, 9, 19, 11), "rose")
        c.line(((7, 19), (5, 21), (8, 21)), RAMPS["wood"][2], 2)
        c.line(((17, 19), (19, 21), (16, 21)), RAMPS["wood"][2], 2)
        c.line(((9, 4), (12, 2), (14, 4)), CREAM)
    elif name == "photo":
        c.shape(((6, 3), (20, 6), (18, 19), (4, 16)), "wood")
        c.shape(((8, 6), (17, 8), (16, 14), (7, 12)), "blue")
        c.gem((13, 8, 16, 11), "blue")
        c.shape(((7, 12), (10, 9), (12, 13), (14, 11), (16, 14),
                 (16, 15), (7, 13)), "mint")
        c.line(((8, 15), (15, 16)), CREAM)
    elif name == "calendar":
        c.shape(((4, 6), (20, 6), (21, 16), (16, 21),
                 (4, 20), (3, 17)), "blue")
        c.shape(((4, 6), (20, 6), (20, 9), (4, 9)), "rose")
        for x in (7, 12, 17):
            c.box((x, 2, x + 1, 7), "gold")
        for x, y in ((7, 12), (12, 12), (17, 12), (7, 16), (12, 16)):
            c.box((x, y, x + 1, y + 1), "mint")
        c.line(((16, 18), (19, 16)), CREAM)
    elif name == "daylight":
        c.gem((8, 3, 16, 11), "gold")
        for points in (((12, 1), (12, 2)), ((5, 4), (6, 5)),
                       ((18, 4), (19, 5)), ((4, 9), (6, 9)),
                       ((18, 9), (20, 9))):
            c.line(points, CREAM, 2)
        c.shape(((3, 14), (8, 12), (12, 15), (17, 12), (21, 14),
                 (21, 20), (3, 20)), "mint")
        c.line(((6, 17), (10, 17)), RAMPS["mint"][0])
    elif name == "weather":
        c.gem((15, 2, 20, 7), "gold")
        c.shape(((5, 18), (3, 16), (4, 12), (7, 11), (9, 7),
                 (15, 7), (17, 10), (20, 11), (21, 15),
                 (19, 18)), "blue")
        c.line(((8, 20), (7, 22)), RAMPS["mint"][2], 2)
        c.line(((13, 20), (12, 22)), RAMPS["mint"][2], 2)
        c.line(((18, 20), (17, 22)), RAMPS["mint"][2], 2)
    elif name == "visitor":
        c.shape(((3, 10), (5, 6), (9, 6), (11, 10),
                 (10, 13), (5, 13)), "gold")
        c.shape(((12, 8), (14, 4), (18, 4), (21, 8),
                 (20, 12), (14, 12)), "gold")
        c.shape(((2, 19), (4, 15), (9, 14), (12, 17),
                 (14, 14), (19, 14), (22, 18), (22, 21), (2, 21)), "blue")
        c.line(((7, 9), (8, 9)), INK)
        c.line(((16, 7), (18, 7)), INK)
        c.line(((10, 17), (14, 17)), CREAM)
    elif name == "season_reward":
        c.shape(((7, 14), (9, 17), (8, 22), (12, 20),
                 (16, 22), (15, 17), (18, 14)), "rose")
        c.gem((5, 2, 19, 16), "gold")
        c.shape(((8, 12), (8, 8), (10, 10), (12, 6), (14, 10),
                 (16, 8), (16, 13), (12, 15)), "gold")
        c.line(((12, 4), (12, 5)), CREAM)
    elif name == "generations":
        c.gem((9, 2, 15, 8), "gold")
        c.gem((2, 15, 8, 21), "mint")
        c.gem((9, 15, 15, 21), "mint")
        c.gem((16, 15, 22, 21), "mint")
        c.line(((12, 8), (12, 11), (5, 11), (5, 15)), CREAM, 2)
        c.line(((12, 11), (12, 15)), CREAM, 2)
        c.line(((12, 11), (19, 11), (19, 15)), CREAM, 2)
    elif name == "retire":
        c.shape(((3, 12), (21, 12), (20, 18), (17, 21),
                 (7, 21), (4, 18)), "wood")
        c.line(((12, 3), (12, 13)), RAMPS["blue"][2], 3)
        c.shape(((7, 8), (12, 13), (17, 8), (15, 8),
                 (12, 11), (9, 8)), "blue")
        c.line(((5, 16), (19, 16)), CREAM)
    elif name == "resume":
        c.line(((17, 5), (20, 7), (21, 10), (21, 14), (19, 18),
                (15, 21), (9, 21), (5, 18), (3, 14), (3, 9),
                (6, 5), (10, 3), (14, 3)), RAMPS["blue"][2], 2)
        c.shape(((11, 2), (18, 3), (14, 8), (14, 5)), "gold")
        c.shape(((9, 8), (9, 16), (16, 12)), "wood")
    elif name == "confirm_cancel":
        c.shape(((3, 4), (10, 4), (10, 20), (3, 20)), "mint")
        c.shape(((14, 4), (21, 4), (21, 20), (14, 20)), "rose")
        c.check(4, 9, CREAM)
        c.line(((15, 10), (20, 15)), INK, 3)
        c.line(((20, 10), (15, 15)), INK, 3)
        c.line(((15, 10), (20, 15)), CREAM)
        c.line(((20, 10), (15, 15)), CREAM)
    elif name == "activity":
        c.shape(((5, 3), (7, 2), (9, 5), (8, 7),
                 (16, 7), (15, 5), (17, 2), (19, 3),
                 (18, 7), (20, 10), (20, 14), (18, 17),
                 (15, 19), (9, 19), (6, 17), (4, 14),
                 (4, 10), (6, 7)), "wood")
        c.shape(((8, 8), (16, 8), (18, 10), (18, 14),
                 (16, 17), (8, 17), (6, 14), (6, 10)), "blue")
        c.line(((12, 9), (12, 12), (15, 14)), CREAM, 2)
        c.line(((8, 20), (7, 22)), RAMPS["gold"][2], 2)
        c.line(((16, 20), (17, 22)), RAMPS["gold"][2], 2)
    return c.image.resize((48, 48), Image.Resampling.NEAREST)
