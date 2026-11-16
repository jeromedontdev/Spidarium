"""Offline tests for the generated-background import pipeline."""

import hashlib
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import import_background as ib

ROOT = Path(__file__).resolve().parents[1]


def noisy_source(size=1024, seed=3):
    """A deterministic high-contrast source, standing in for a generated image."""
    image = Image.new("RGB", (size, size))
    pixels = image.load()
    state = seed
    for y in range(size):
        for x in range(size):
            state = (state * 1103515245 + 12345) & 0x7FFFFFFF
            value = (state >> 16) & 0xFF
            pixels[x, y] = (value, max(0, value - 40), max(0, value - 80))
    return image


class ReduceTests(unittest.TestCase):
    def test_reduces_to_device_size(self):
        result = ib.reduce_to_target(noisy_source())
        self.assertEqual(result.size, (240, 240))

    def test_crops_to_centred_multiple(self):
        # 1000 is not a multiple of 240; the largest centred multiple is 960.
        result = ib.reduce_to_target(noisy_source(1000))
        self.assertEqual(result.size, (240, 240))

    def test_rejects_source_smaller_than_target(self):
        with self.assertRaisesRegex(ValueError, "at least 240x240"):
            ib.reduce_to_target(Image.new("RGB", (200, 200)))

    def test_accepts_exact_target_size(self):
        result = ib.reduce_to_target(Image.new("RGB", (240, 240), (10, 20, 30)))
        self.assertEqual(result.size, (240, 240))


class ToneTests(unittest.TestCase):
    def test_contrast_reduction_lowers_spread(self):
        base = ib.reduce_to_target(noisy_source())
        before = ib.profile(base)["sd"]
        after = ib.profile(ib.scale_contrast(base, 0.5))["sd"]
        self.assertLess(after, before)

    def test_contrast_is_uniform_so_no_seam_appears(self):
        base = ib.reduce_to_target(noisy_source())
        reduced = ib.scale_contrast(base, 0.5)
        # A region-limited adjustment would leave a step at the play-area edge.
        inside = ib.luma(reduced.getpixel((199, 120)))
        outside = ib.luma(reduced.getpixel((200, 120)))
        self.assertLess(abs(inside - outside), 40)

    def test_retarget_mean_hits_target(self):
        base = ib.reduce_to_target(noisy_source())
        moved = ib.retarget_mean(base, 65.0)
        self.assertAlmostEqual(ib.profile(moved)["mean"], 65.0, delta=1.0)

    def test_retarget_mean_is_a_noop_when_already_on_target(self):
        base = ib.reduce_to_target(noisy_source())
        settled = ib.retarget_mean(base, 65.0)
        self.assertIs(ib.retarget_mean(settled, 65.0), settled)

    def test_saturation_reduction_pulls_toward_grey(self):
        base = Image.new("RGB", (240, 240), (200, 60, 20))
        greyed = ib.scale_saturation(base, 0.5)
        r, g, b = greyed.getpixel((10, 10))
        self.assertLess(r - b, 180)

    def test_lighting_brightens_top_left(self):
        flat = Image.new("RGB", (240, 240), (100, 100, 100))
        lit = ib.apply_lighting(flat, 16)
        self.assertGreater(ib.luma(lit.getpixel((2, 2))),
                           ib.luma(lit.getpixel((237, 237))))

    def test_lighting_is_antisymmetric_about_the_centre(self):
        # The gradient runs top-left to bottom-right, so opposite corners must
        # sit equally far either side of the original tone.
        flat = Image.new("RGB", (240, 240), (100, 100, 100))
        lit = ib.apply_lighting(flat, 16)
        top_left = ib.luma(lit.getpixel((0, 0)))
        bottom_right = ib.luma(lit.getpixel((239, 239)))
        self.assertAlmostEqual(top_left - 100.0, 100.0 - bottom_right, delta=1.0)

    def test_lighting_leaves_the_centre_untouched(self):
        flat = Image.new("RGB", (240, 240), (100, 100, 100))
        lit = ib.apply_lighting(flat, 16)
        self.assertAlmostEqual(ib.luma(lit.getpixel((120, 120))), 100.0, delta=1.0)


class QuantizeTests(unittest.TestCase):
    def test_quantize_caps_colors(self):
        result = ib.quantize(ib.reduce_to_target(noisy_source()))
        pixels = result.load()
        colors = {pixels[x, y] for y in range(240) for x in range(240)}
        self.assertLessEqual(len(colors), ib.MAX_COLORS)

    def test_quantize_does_not_dither(self):
        # Dithering would scatter isolated pixels; a flat field must stay flat.
        flat = Image.new("RGB", (240, 240), (90, 70, 50))
        result = ib.quantize(flat)
        pixels = result.load()
        colors = {pixels[x, y] for y in range(240) for x in range(240)}
        self.assertEqual(len(colors), 1)


class ConvertTests(unittest.TestCase):
    def test_convert_produces_opaque_device_background(self):
        result = ib.convert(noisy_source())
        self.assertEqual(result.size, (240, 240))
        self.assertEqual(result.mode, "RGBA")
        alphas = {alpha for *_, alpha in result.get_flattened_data()}
        self.assertEqual(alphas, {255})

    def test_convert_caps_colors(self):
        result = ib.convert(noisy_source())
        pixels = result.load()
        colors = {pixels[x, y] for y in range(240) for x in range(240)}
        self.assertLessEqual(len(colors), ib.MAX_COLORS)

    def test_convert_is_deterministic(self):
        first = ib.convert(noisy_source())
        second = ib.convert(noisy_source())
        self.assertEqual(ib.pixel_hash(first), ib.pixel_hash(second))

    def test_convert_matches_reference_mean(self):
        result = ib.convert(noisy_source())
        self.assertAlmostEqual(ib.profile(result)["mean"], ib.REFERENCE_MEAN, delta=2.0)


class DescribeTests(unittest.TestCase):
    def test_describe_reports_contract_measurements(self):
        text = ib.describe(ib.convert(noisy_source()))
        for field in ("colors=", "center_tones=", "mean=", "sd=", "p10-p90=", "span="):
            self.assertIn(field, text)


class ThemeMeanTests(unittest.TestCase):
    def test_every_background_theme_has_a_brightness(self):
        import asset_build
        for theme in asset_build.BACKGROUNDS:
            with self.subTest(theme=theme):
                self.assertIn(theme, ib.THEME_MEAN)

    def test_unknown_theme_falls_back_to_the_reference(self):
        self.assertEqual(ib.theme_mean("no_such_theme"), ib.REFERENCE_MEAN)

    def test_bright_themes_stay_bright_and_dark_themes_stay_dark(self):
        # Forcing every floor to crystal's mean would turn snow into mud.
        self.assertGreater(ib.theme_mean("snow"), ib.theme_mean("crystal") + 60)
        self.assertLess(ib.theme_mean("dark_cave"), ib.theme_mean("crystal"))

    def test_cli_uses_the_theme_brightness_by_default(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            scratch = Path(directory)
            source = scratch / "source.png"
            noisy_source().save(source)
            out = scratch / "out"
            code = ib.main([str(source), "--theme", "snow", "--out", str(out)])
            self.assertEqual(code, 0)
            with Image.open(out / "background_snow_0.png") as result:
                result.load()
                mean = ib.profile(result.convert("RGB"))["mean"]
            self.assertAlmostEqual(mean, ib.theme_mean("snow"), delta=3.0)

    def test_cli_mean_flag_overrides_the_theme_brightness(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            scratch = Path(directory)
            source = scratch / "source.png"
            noisy_source().save(source)
            out = scratch / "out"
            code = ib.main([str(source), "--theme", "snow", "--out", str(out),
                            "--mean", "70"])
            self.assertEqual(code, 0)
            with Image.open(out / "background_snow_0.png") as result:
                result.load()
                mean = ib.profile(result.convert("RGB"))["mean"]
            self.assertAlmostEqual(mean, 70.0, delta=3.0)


class CliTests(unittest.TestCase):
    def test_dry_run_writes_to_out_and_not_assets(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            scratch = Path(directory)
            source = scratch / "source.png"
            noisy_source().save(source)
            out = scratch / "out"
            code = ib.main([str(source), "--theme", "ghost", "--out", str(out)])
            self.assertEqual(code, 0)
            self.assertTrue((out / "background_ghost_0.png").exists())
            self.assertFalse((ib.SOURCES / "ghost_floor.png").exists())

    def test_install_refuses_to_overwrite(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            scratch = Path(directory)
            source = scratch / "source.png"
            noisy_source().save(source)
            original = ib.SOURCES
            ib.SOURCES = scratch / "sources"
            try:
                ib.SOURCES.mkdir()
                (ib.SOURCES / "woodland_floor.png").write_bytes(b"existing")
                code = ib.main([str(source), "--theme", "woodland",
                                "--out", str(scratch), "--install"])
                self.assertEqual(code, 1)
                self.assertEqual((ib.SOURCES / "woodland_floor.png").read_bytes(), b"existing")
            finally:
                ib.SOURCES = original

    def test_install_writes_approved_source(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            scratch = Path(directory)
            source = scratch / "source.png"
            noisy_source().save(source)
            original = ib.SOURCES
            ib.SOURCES = scratch / "sources"
            try:
                code = ib.main([str(source), "--theme", "woodland",
                                "--out", str(scratch), "--install"])
                self.assertEqual(code, 0)
                installed = ib.SOURCES / "woodland_floor.png"
                self.assertTrue(installed.exists())
                with Image.open(installed) as check:
                    self.assertEqual(check.size, (240, 240))
                    self.assertEqual(check.mode, "RGBA")
            finally:
                ib.SOURCES = original


if __name__ == "__main__":
    unittest.main()
