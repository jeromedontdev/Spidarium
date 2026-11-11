"""Regression checks for the shared SNES material, terrain and sprite contracts."""
from pathlib import Path
import tempfile
import unittest

from PIL import Image, ImageDraw

from asset_build import (
    BACKGROUNDS, DECOR_ITEMS, GIFTS, TREATS, expected_dimensions,
    validate_pixel_art, write_preview,
)
from gen_sprites import (
    ACHIEVEMENT_NAMES, BACKGROUND_NAMES, DECOR_ITEM_NAMES, GIFT_NAMES,
    PREY_ANIMS, PREY_SPECIES, UI_ICONS, _blob, _save_png, _shade_materials,
    draw_achievement, draw_background, draw_decor_item, draw_gift, draw_icon, draw_prey,
    TREAT_NAMES, _border_corner, _THEME_MATERIALS, draw_border, draw_treat, prey_leg_paths,
)


class ArtStyleTests(unittest.TestCase):
    def test_audio_icons_have_distinct_silhouettes_and_match_published_sources(self):
        root = Path(__file__).resolve().parents[1]
        for left, right in (("volume", "sound"), ("music", "play")):
            masks = []
            for name in (left, right):
                image = draw_icon(name)
                with Image.open(root / "assets" / f"ui_{name}.png") as source:
                    self.assertEqual(source.convert("RGBA").tobytes(), image.tobytes())
                masks.append({i for i, alpha in enumerate(image.getchannel("A").tobytes()) if alpha})
            intersection = len(masks[0] & masks[1])
            union = len(masks[0] | masks[1])
            self.assertLess(intersection / union, 0.65, f"{left}/{right} silhouettes too similar")

    def test_expanded_catalogue_assets_are_appended_without_reordering(self):
        self.assertEqual(len(DECOR_ITEM_NAMES), 68)
        self.assertEqual(DECOR_ITEM_NAMES[16:34], [
            "fern_pot", "acorn_hide", "driftwood_arch", "web_hammock", "dew_pool",
            "mushroom_pair", "flower_planter", "crystal_cluster", "shell_cove",
            "twig_ladder", "moss_stone", "seed_pod", "silk_tent", "leaf_umbrella",
            "bone_arch", "fossil", "lantern_cap", "stone_bridge",
        ])
        self.assertEqual(len(BACKGROUND_NAMES), 48)
        self.assertEqual(BACKGROUND_NAMES[16:24], [
            "house", "dark_cave", "night", "autumn", "snow", "pond", "ruins", "marsh",
        ])
        self.assertEqual(len(UI_ICONS), 30)
        self.assertEqual(UI_ICONS[13:], [
            "daily", "treats", "backgrounds", "achievements", "prey", "settings",
            "wifi", "sd", "about", "battery", "display", "sound", "auto_feed",
            "screenshot", "reset", "volume", "music",
        ])
        self.assertEqual(tuple(DECOR_ITEM_NAMES), DECOR_ITEMS)
        self.assertEqual(tuple(BACKGROUND_NAMES), BACKGROUNDS)
        self.assertEqual(tuple(GIFT_NAMES), GIFTS)
        self.assertEqual(tuple(TREAT_NAMES), TREATS)
        self.assertEqual(len(GIFT_NAMES), 36)
        self.assertEqual(len(expected_dimensions()), 337)

    def test_all_theme_frames_have_transparent_openings_and_distinct_art(self):
        contents = set()
        for name in BACKGROUND_NAMES:
            with self.subTest(theme=name):
                image = draw_border(name)
                self.assertEqual(image.size, (240, 240))
                validate_pixel_art(image, f"border_{name}_0.png")
                self.assertIsNone(image.getchannel("A").crop((16, 16, 224, 224)).getbbox())
                self.assertNotIn(image.tobytes(), contents)
                contents.add(image.tobytes())

    def test_border_center_paint_is_rejected(self):
        image = draw_border("woodland")
        ImageDraw.Draw(image).rectangle((20, 20, 21, 21), fill=(0, 0, 0, 255))
        with self.assertRaisesRegex(ValueError, "transparent opening"):
            validate_pixel_art(image, "border_woodland_0.png")

    def test_theme_families_use_distinct_corner_silhouettes(self):
        silhouettes = set()
        for family in _THEME_MATERIALS:
            with self.subTest(family=family):
                corner = _border_corner(family)
                self.assertIsNone(corner.getchannel("A").crop((8, 8, 28, 28)).getbbox())
                silhouette = corner.getchannel("A").tobytes()
                self.assertNotIn(silhouette, silhouettes)
                silhouettes.add(silhouette)
        self.assertGreaterEqual(len(silhouettes), 11)

    def test_new_theme_materials_are_not_universally_wood_and_purple(self):
        for family, item in (
            ("gothic", "gargoyle"), ("academia", "globe"), ("household", "tea_cup"),
            ("witchy", "cauldron"), ("prehistoric", "amber_fossil"),
            ("cyberpunk", "neon_sign"), ("scifi", "moon_rover"),
            ("fantasy", "wizard_tower"), ("ancient", "glyph_tablet"),
            ("zen", "stone_lantern"), ("fairy", "pixie_door"),
        ):
            with self.subTest(family=family, item=item):
                image = draw_decor_item(item)
                colors = {color for _, color in image.getcolors()}
                expected = {
                    tuple((color >> shift) & 255 for shift in (16, 8, 0)) + (255,)
                    for ramp in _THEME_MATERIALS[family] for color in ramp
                }
                self.assertGreaterEqual(len(colors & expected), 7)

    def test_new_props_and_treats_are_not_recolored_silhouettes(self):
        contents, silhouettes = set(), set()
        for prefix, names, renderer in (
            ("decoritem", DECOR_ITEM_NAMES[34:], draw_decor_item),
            ("gift", GIFT_NAMES[18:], draw_gift),
            ("treat", TREAT_NAMES, draw_treat),
        ):
            for name in names:
                with self.subTest(group=prefix, name=name):
                    image = renderer(name)
                    validate_pixel_art(image, f"{prefix}_{name}.png")
                    pixels = (image.size, image.tobytes())
                    silhouette = (image.size, image.getchannel("A").tobytes())
                    self.assertNotIn(pixels, contents)
                    self.assertNotIn(silhouette, silhouettes)
                    contents.add(pixels)
                    silhouettes.add(silhouette)

    def test_generated_sources_match_all_published_png_pixels(self):
        root = Path(__file__).resolve().parents[1]
        for prefix, names, renderer in (
            ("decoritem", DECOR_ITEM_NAMES, draw_decor_item),
            ("gift", GIFT_NAMES, draw_gift),
            ("treat", TREAT_NAMES, draw_treat),
            ("background", BACKGROUND_NAMES, draw_background),
            ("border", BACKGROUND_NAMES, draw_border),
        ):
            for name in names:
                filename = f"{prefix}_{name}" + ("_0" if prefix in ("background", "border") else "") + ".png"
                with self.subTest(asset=filename), Image.open(root / "assets" / filename) as source:
                    self.assertEqual(source.tobytes(), renderer(name).tobytes())

    def test_insect_leg_roots_are_fixed_and_feet_alternate_tripods(self):
        for species in ("fly", "fruitfly", "cricket", "roach", "beetle", "moth"):
            first, second = (prey_leg_paths(species, frame) for frame in (0, 1))
            self.assertEqual(len(first), 6)
            for a, b in zip(first, second):
                self.assertEqual(a[:2], b[:2])
                self.assertIn(a[0][0], (12, 14, 16))
                self.assertIn(a[0][1], (11, 13))
                self.assertEqual(abs(a[-1][0] - b[-1][0]), 2)
            shifts = [b[-1][0] - a[-1][0] for a, b in zip(first, second)]
            self.assertEqual(shifts[:3], [-v for v in shifts[3:]])
            self.assertEqual(shifts[0], shifts[2])
            self.assertEqual(shifts[0], -shifts[1])

    def test_all_backgrounds_are_distinct_and_quiet(self):
        contents = set()
        for name in BACKGROUND_NAMES:
            with self.subTest(theme=name):
                image = draw_background(name)
                self.assertEqual(image.size, (240, 240))
                validate_pixel_art(image, f"background_{name}_0.png")
                self.assertNotIn(image.tobytes(), contents)
                contents.add(image.tobytes())

    def test_every_decor_item_has_richer_materials_on_the_pixel_grid(self):
        contents = set()
        for name in DECOR_ITEM_NAMES:
            with self.subTest(item=name):
                image = draw_decor_item(name)
                self.assertEqual(image.size, (64, 64))
                validate_pixel_art(image, f"decoritem_{name}.png")
                self.assertNotIn(image.tobytes(), contents)
                contents.add(image.tobytes())

    def test_all_gift_furnishings_are_distinct_and_richly_shaded(self):
        contents = set()
        for name in GIFT_NAMES:
            with self.subTest(gift=name):
                image = draw_gift(name)
                self.assertEqual(image.size, (32, 32))
                validate_pixel_art(image, f"gift_{name}.png")
                self.assertNotIn(image.tobytes(), contents)
                contents.add(image.tobytes())

    def test_shading_never_leaks_outside_the_silhouette(self):
        image = Image.new("RGBA", (32, 32))
        ink = (22, 24, 35, 255)
        _blob(ImageDraw.Draw(image), 16, 16, 10, 7,
              (140, 140, 130, 255), (220, 210, 170, 255), (70, 75, 90, 255), ink)
        mask = Image.new("1", image.size)
        ImageDraw.Draw(mask).ellipse((6, 9, 26, 23), fill=1)
        for y in range(32):
            for x in range(32):
                self.assertEqual(image.getpixel((x, y))[3] == 255, bool(mask.getpixel((x, y))))
                if mask.getpixel((x, y)) and any(
                    not mask.getpixel((x + dx, y + dy))
                    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))
                ):
                    self.assertEqual(image.getpixel((x, y)), ink)

    def test_all_remaining_frames_and_glyphs_use_shaded_pixel_materials(self):
        for species in PREY_SPECIES:
            for clip, frames in PREY_ANIMS:
                for frame in range(frames):
                    name = f"prey_{species}_{clip}_{frame}.png"
                    with self.subTest(asset=name):
                        validate_pixel_art(draw_prey(species, clip, frame), name)
        for prefix, names, renderer in (
            ("ui", UI_ICONS, draw_icon),
            ("achievement", ACHIEVEMENT_NAMES, draw_achievement),
        ):
            contents = set()
            for name in names:
                with self.subTest(group=prefix, asset=name):
                    image = renderer(name)
                    validate_pixel_art(image, f"{prefix}_{name}.png")
                    if prefix == "ui":
                        self.assertNotIn(image.tobytes(), contents)
                        contents.add(image.tobytes())

    def test_material_shading_preserves_mask_ink_and_direction(self):
        image = Image.new("RGBA", (8, 8))
        ink, material = (22, 24, 35, 255), (100, 140, 80, 255)
        draw = ImageDraw.Draw(image)
        draw.rectangle((1, 1, 6, 6), fill=material, outline=ink)
        before = image.copy()
        shaded = _shade_materials(image, (material,))
        self.assertEqual(shaded.getchannel("A").tobytes(), before.getchannel("A").tobytes())
        self.assertEqual(shaded.getpixel((1, 1)), ink)
        self.assertGreater(shaded.getpixel((2, 2))[0], material[0])
        self.assertEqual(shaded.getpixel((3, 4)), material)
        self.assertLess(shaded.getpixel((5, 5))[0], material[0])

    def test_flat_remaining_groups_are_rejected(self):
        image = Image.new("RGBA", (48, 48), (100, 120, 80, 255))
        for name in ("prey_fly_walk_0.png", "prey_fly_dead_0.png",
                     "ui_feed.png", "achievement_adult.png"):
            with self.subTest(asset=name), self.assertRaisesRegex(ValueError, "shaded materials"):
                validate_pixel_art(image, name)

    def test_review_sheet_covers_every_non_spider_source(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as directory:
            output = Path(directory) / "review.png"
            write_preview(Path(__file__).resolve().parents[1] / "assets", output)
            with Image.open(output) as sheet:
                self.assertEqual(sheet.size, (960, 11000))

    def test_equal_pixels_preserve_source_bytes_across_png_encoders(self):
        image = draw_icon("feed")
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]) as directory:
            path = Path(directory) / "icon.png"
            image.save(path, compress_level=0)
            original = path.read_bytes()
            self.assertFalse(_save_png(image, path))
            self.assertEqual(path.read_bytes(), original)
            image.putpixel((0, 0), (12, 34, 56, 255))
            self.assertTrue(_save_png(image, path))
            self.assertNotEqual(path.read_bytes(), original)
            with Image.open(path) as generated:
                self.assertEqual(generated.tobytes(), image.tobytes())
            self.assertFalse(_save_png(image, path))

    def test_noisy_center_is_rejected(self):
        image = draw_background("moss")
        image.putpixel((120, 120), (255, 0, 255, 255))
        with self.assertRaisesRegex(ValueError, "quiet center"):
            validate_pixel_art(image, "background_moss_0.png")

    def test_continuous_palette_is_rejected(self):
        image = draw_background("sand")
        for x in range(65):
            image.putpixel((x, 0), (x, 0, 0, 255))
        with self.assertRaisesRegex(ValueError, "exceeds 64 colors"):
            validate_pixel_art(image, "background_sand_0.png")

    def test_flat_decor_is_rejected(self):
        image = Image.new("RGBA", (64, 64), (100, 120, 80, 255))
        with self.assertRaisesRegex(ValueError, "at least 8"):
            validate_pixel_art(image, "decoritem_fern.png")

    def test_flat_gift_is_rejected(self):
        image = Image.new("RGBA", (32, 32), (100, 120, 80, 255))
        with self.assertRaisesRegex(ValueError, "at least 8"):
            validate_pixel_art(image, "gift_leaf.png")


if __name__ == "__main__":
    unittest.main()
