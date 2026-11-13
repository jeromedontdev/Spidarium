"""Regression checks for the shared SNES material, terrain and sprite contracts."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image, ImageDraw
import terrain_art
import core_decor_art
import furnishing_art
import small_art

from asset_build import (
    ACHIEVEMENT_ICONS, BACKGROUNDS, DECOR_ITEMS, GIFTS, TREATS, expected_dimensions,
    validate_pixel_art, validate_runtime_mappings, write_preview,
)
from gen_sprites import (
    ACHIEVEMENT_NAMES, BACKGROUND_NAMES, DECOR_ITEM_NAMES, GIFT_NAMES,
    PREY_ANIMS, PREY_SPECIES, UI_ICONS, _blob, _save_png, _shade_materials,
    draw_achievement, draw_background, draw_decor, draw_decor_item, draw_gift, draw_icon, draw_prey,
    TREAT_NAMES, _border_corner, _THEME_MATERIALS, draw_border, draw_treat, prey_leg_paths,
)


class ArtStyleTests(unittest.TestCase):
    def test_remaining_material_recipes_cover_the_complete_catalogue(self):
        self.assertEqual(set(furnishing_art.DECOR_DETAILS), set(DECOR_ITEM_NAMES[16:]))
        self.assertEqual(set(furnishing_art.GIFT_DETAILS), set(GIFT_NAMES))
        self.assertEqual(set(furnishing_art.TREAT_DETAILS), set(TREAT_NAMES))
        self.assertEqual(set(small_art.ICON_DETAILS), set(UI_ICONS))
        self.assertEqual(set(small_art.BADGE_DETAILS), set(ACHIEVEMENT_NAMES))
        self.assertEqual(set(small_art.PREY_DETAILS), set(PREY_SPECIES))
        self.assertEqual(set(small_art.SPLATTER_DETAILS), set(PREY_SPECIES))

    def test_remaining_furnishings_match_sources_and_repeat_exactly(self):
        root = Path(__file__).resolve().parents[1]
        for prefix, names, renderer in (
            ("decoritem", DECOR_ITEM_NAMES[16:], draw_decor_item),
            ("gift", GIFT_NAMES, draw_gift), ("treat", TREAT_NAMES, draw_treat),
        ):
            for name in names:
                with self.subTest(group=prefix, item=name):
                    image = renderer(name)
                    self.assertEqual(image.tobytes(), renderer(name).tobytes())
                    with Image.open(root / "assets" / f"{prefix}_{name}.png") as source:
                        self.assertEqual(image.tobytes(), source.tobytes())
                    validate_pixel_art(image, f"{prefix}_{name}.png")

    def test_authored_detail_preserves_alpha_and_cannot_paint_outside_materials(self):
        image = Image.new("RGBA", (16, 16))
        ImageDraw.Draw(image).rectangle((3, 3, 12, 12), fill=(100, 110, 80, 255))
        mask = image.getchannel("A").tobytes()
        colors = [(20, 30, 40, 255), (170, 190, 110, 255)]
        furnishing_art.detail(image, ((1, ((0, 0), (15, 15))),), colors)
        self.assertEqual(mask, image.getchannel("A").tobytes())
        self.assertEqual(image.getpixel((4, 4)), colors[1])
        self.assertEqual(image.getpixel((0, 0)), (0, 0, 0, 0))

    def test_small_details_preserve_ink_alpha_and_existing_finite_palette(self):
        image = Image.new("RGBA", (24, 24))
        ink, body, light = (20, 30, 40, 255), (80, 120, 90, 255), (180, 200, 150, 255)
        d = ImageDraw.Draw(image)
        d.rectangle((3, 3, 20, 20), fill=body, outline=ink)
        d.line((6, 6, 15, 6), fill=light)
        original = image.copy()
        small_art.finish(image, "decor", "icon")
        self.assertEqual(original.getchannel("A").tobytes(), image.getchannel("A").tobytes())
        self.assertLessEqual({p for p in image.get_flattened_data() if p[3]}, {ink, body, light})
        for y in range(24):
            for x in range(24):
                if original.getpixel((x, y)) == ink:
                    self.assertEqual(image.getpixel((x, y)), ink)

    def test_core_decor_redraw_is_explicit_deterministic_and_matches_sources(self):
        self.assertEqual(core_decor_art.NAMES, tuple(DECOR_ITEM_NAMES[:16]))
        root = Path(__file__).resolve().parents[1]
        for name in core_decor_art.NAMES:
            with self.subTest(item=name):
                image = core_decor_art.draw(name)
                self.assertEqual(image.tobytes(), core_decor_art.draw(name).tobytes())
                self.assertEqual(image.tobytes(), draw_decor_item(name).tobytes())
                validate_pixel_art(image, f"decoritem_{name}.png")
                with Image.open(root / "assets" / f"decoritem_{name}.png") as source:
                    self.assertEqual(image.tobytes(), source.tobytes())
                logical = image.resize((32, 32), Image.Resampling.NEAREST)
                self.assertGreater(sum(bool(pixel[3]) for pixel in logical.get_flattened_data()), 80)
                x0, y0, x1, y1 = logical.getbbox()
                self.assertGreaterEqual(min(x0, y0), 1)
                self.assertLessEqual(max(x1, y1), 31)
        with self.assertRaisesRegex(ValueError, "Unknown core decor"):
            core_decor_art.draw("unknown")

    def test_legacy_decor_uses_the_same_redrawn_core_pixels(self):
        for legacy, name, size in (
            ("rock_0", "rock_large", (24, 16)), ("rock_1", "rock_small", (20, 14)),
            ("plant_0", "fern", (24, 32)), ("plant_1", "succulent", (26, 28)),
            ("wood_log", "log", (48, 18)),
        ):
            with self.subTest(item=legacy):
                source = core_decor_art.draw(name).resize((32, 32), Image.Resampling.NEAREST)
                expected = source.crop(source.getbbox()).resize(size, Image.Resampling.NEAREST)
                actual = draw_decor(legacy)
                self.assertEqual(actual.tobytes(), expected.resize(
                    (size[0] * 2, size[1] * 2), Image.Resampling.NEAREST).tobytes())

    def test_every_non_crystal_theme_has_an_explicit_terrain_recipe(self):
        self.assertEqual(set(terrain_art.RECIPES), set(BACKGROUND_NAMES) - {"crystal"})
        self.assertGreaterEqual(len({recipe[0] for recipe in terrain_art.RECIPES.values()}), 18)
        self.assertGreaterEqual(len({recipe[4] for recipe in terrain_art.RECIPES.values()}), 24)

    def test_approved_moss_pair_is_pixel_identical_to_sources(self):
        root = Path(__file__).resolve().parents[1]
        for kind, renderer in (("floor", draw_background), ("border", draw_border)):
            with Image.open(root / "assets" / "sources" / f"moss_{kind}.png") as source:
                self.assertEqual(source.tobytes(), renderer("moss").tobytes())

    def test_generated_terrain_is_deterministic_and_not_a_recolored_layout(self):
        for name in BACKGROUND_NAMES:
            self.assertEqual(draw_background(name).tobytes(), draw_background(name).tobytes())
            self.assertEqual(draw_border(name).tobytes(), draw_border(name).tobytes())
        def tonal_layout(name):
            image = draw_background(name).crop((40, 40, 200, 200))
            colors = sorted(image.getcolors(25600), key=lambda item: item[0], reverse=True)
            indices = {color: index for index, (_, color) in enumerate(colors)}
            return bytes(indices[pixel] for pixel in image.get_flattened_data())
        for left, right in (("cork", "house"), ("moss", "fern"), ("pond", "marsh"),
                            ("gothic", "cathedral"), ("starship", "circuit_garden")):
            self.assertNotEqual(tonal_layout(left), tonal_layout(right))

    def test_flat_terrain_and_excess_center_colors_are_rejected(self):
        image = Image.new("RGBA", (240, 240), (80, 90, 70, 255))
        with self.assertRaisesRegex(ValueError, "finite material shading"):
            validate_pixel_art(image, "background_fern_0.png")
        image = draw_background("fern")
        for index in range(17):
            image.putpixel((100 + index, 100), (index + 60, 80, 65, 255))
        with self.assertRaisesRegex(ValueError, "quiet center"):
            validate_pixel_art(image, "background_fern_0.png")
    def test_all_twenty_achievement_art_mappings_are_covered(self):
        root = Path(__file__).resolve().parents[1]
        validate_runtime_mappings(root)
        with patch.dict(ACHIEVEMENT_ICONS, {"ten_cleans": "play"}):
            with self.assertRaisesRegex(ValueError, "ACH_TEN_CLEANS"):
                validate_runtime_mappings(root)
        with patch.dict(ACHIEVEMENT_ICONS, {"unknown": "clean"}):
            with self.assertRaisesRegex(ValueError, "ACH_UNKNOWN"):
                validate_runtime_mappings(root)
        with patch.dict(ACHIEVEMENT_ICONS, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "every game.h ACH_"):
                validate_runtime_mappings(root)

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

    def test_crystal_border_fit_is_pinned_and_preserves_source_palette(self):
        root = Path(__file__).resolve().parents[1]
        with Image.open(root / "assets" / "sources" / "crystal_border.png") as source:
            self.assertEqual(source.size, (120, 120))
            self.assertIsNone(source.getchannel("A").crop((18, 18, 102, 102)).getbbox())
            source_colors = {color for _, color in source.getcolors() if color[3]}
        image = draw_border("crystal")
        self.assertEqual(validate_pixel_art(image, "border_crystal_0.png"), 16)
        self.assertEqual({color for _, color in image.getcolors() if color[3]}, source_colors)
        with Image.open(root / "assets" / "border_crystal_0.png") as rendered:
            self.assertEqual(image.tobytes(), rendered.tobytes())
        altered = image.copy()
        colors = list(source_colors)
        replacement = next(color for color in colors if color != altered.getpixel((4, 4)))
        ImageDraw.Draw(altered).rectangle((4, 4, 5, 5), fill=replacement)
        with self.assertRaisesRegex(ValueError, "fitted PixelLab Crystal border"):
            validate_pixel_art(altered, "border_crystal_0.png")

    def test_changed_crystal_border_source_is_rejected_before_fitting(self):
        source = Image.new("RGBA", (120, 120))
        source.format = "PNG"
        with patch("gen_sprites.Image.open", return_value=source):
            with self.assertRaisesRegex(ValueError, "source differs"):
                draw_border("crystal")

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

    def test_all_backgrounds_are_distinct_and_follow_their_approved_contract(self):
        contents = set()
        for name in BACKGROUND_NAMES:
            with self.subTest(theme=name):
                image = draw_background(name)
                self.assertEqual(image.size, (240, 240))
                validate_pixel_art(image, f"background_{name}_0.png")
                self.assertNotIn(image.tobytes(), contents)
                contents.add(image.tobytes())

    def test_crystal_floor_is_exact_approved_source_and_keeps_richer_texture(self):
        root = Path(__file__).resolve().parents[1]
        with Image.open(root / "assets" / "sources" / "crystal_floor.png") as source:
            image = draw_background("crystal")
            self.assertEqual(source.tobytes(), image.tobytes())
        self.assertGreater(len(image.crop((40, 40, 200, 200)).getcolors(25600)), 3)
        validate_pixel_art(image, "background_crystal_0.png")
        replacement = image.getpixel((0, 0))
        if replacement == image.getpixel((100, 100)):
            replacement = image.getpixel((239, 239))
        self.assertNotEqual(replacement, image.getpixel((100, 100)))
        image.putpixel((100, 100), replacement)
        with self.assertRaisesRegex(ValueError, "approved textured Crystal"):
            validate_pixel_art(image, "background_crystal_0.png")

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
