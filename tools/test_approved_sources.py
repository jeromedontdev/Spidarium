"""Tests for the hash-pinned approved background source registry."""

import hashlib
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import approved_sources as approved

ROOT = Path(__file__).resolve().parents[1]


class RegistryTests(unittest.TestCase):
    def test_crystal_is_registered(self):
        self.assertTrue(approved.is_approved("crystal"))

    def test_unregistered_themes_are_not_approved(self):
        # Moss keeps its own pinned path in terrain_art, so it is deliberately
        # absent from this registry.
        self.assertFalse(approved.is_approved("moss"))
        self.assertFalse(approved.is_approved("no_such_theme"))

    def test_every_shipped_theme_is_approved(self):
        import asset_build
        for theme in asset_build.BACKGROUNDS:
            if theme == "moss":
                continue
            with self.subTest(theme=theme):
                self.assertTrue(approved.is_approved(theme))

    def test_unknown_theme_returns_none(self):
        self.assertIsNone(approved.floor("no_such_theme"))

    def test_every_registered_theme_loads_and_matches_its_pin(self):
        for theme, (_, expected) in approved.APPROVED_FLOORS.items():
            with self.subTest(theme=theme):
                image = approved.floor(theme)
                self.assertEqual(image.size, (240, 240))
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(
                    hashlib.sha256(image.tobytes()).hexdigest(), expected
                )

    def test_every_registered_theme_matches_its_published_asset(self):
        # The registry and the shipped PNGs must never disagree.
        for theme, (_, expected) in approved.APPROVED_FLOORS.items():
            with self.subTest(theme=theme):
                with Image.open(
                    ROOT / "assets" / f"background_{theme}_0.png"
                ) as published:
                    published.load()
                    self.assertEqual(
                        hashlib.sha256(published.convert("RGBA").tobytes()).hexdigest(),
                        expected,
                    )


class RejectionTests(unittest.TestCase):
    def setUp(self):
        self.original = approved.SOURCES
        self.directory = tempfile.TemporaryDirectory(dir=ROOT)
        approved.SOURCES = Path(self.directory.name)

    def tearDown(self):
        approved.SOURCES = self.original
        self.directory.cleanup()

    def test_missing_source_is_reported(self):
        approved.APPROVED_FLOORS["ghost"] = ("ghost_floor.png", "0" * 64)
        try:
            with self.assertRaisesRegex(ValueError, "is missing"):
                approved.floor("ghost")
        finally:
            del approved.APPROVED_FLOORS["ghost"]

    def test_wrong_pixels_are_rejected(self):
        Image.new("RGBA", (240, 240), (1, 2, 3, 255)).save(
            approved.SOURCES / "ghost_floor.png"
        )
        approved.APPROVED_FLOORS["ghost"] = ("ghost_floor.png", "0" * 64)
        try:
            with self.assertRaisesRegex(ValueError, "differ from the approved"):
                approved.floor("ghost")
        finally:
            del approved.APPROVED_FLOORS["ghost"]

    def test_wrong_size_is_rejected(self):
        Image.new("RGBA", (120, 120), (1, 2, 3, 255)).save(
            approved.SOURCES / "ghost_floor.png"
        )
        approved.APPROVED_FLOORS["ghost"] = ("ghost_floor.png", "0" * 64)
        try:
            with self.assertRaisesRegex(ValueError, "240x240 RGBA PNG"):
                approved.floor("ghost")
        finally:
            del approved.APPROVED_FLOORS["ghost"]

    def test_wrong_mode_is_rejected(self):
        Image.new("RGB", (240, 240), (1, 2, 3)).save(
            approved.SOURCES / "ghost_floor.png"
        )
        approved.APPROVED_FLOORS["ghost"] = ("ghost_floor.png", "0" * 64)
        try:
            with self.assertRaisesRegex(ValueError, "240x240 RGBA PNG"):
                approved.floor("ghost")
        finally:
            del approved.APPROVED_FLOORS["ghost"]


class BorderRegistryTests(unittest.TestCase):
    def test_crystal_and_moss_are_approved_borders(self):
        self.assertTrue(approved.is_border_approved("crystal"))
        self.assertTrue(approved.is_border_approved("moss"))

    def test_unregistered_themes_are_not_approved_borders(self):
        self.assertFalse(approved.is_border_approved("no_such_theme"))

    def test_every_shipped_theme_is_an_approved_border(self):
        import asset_build
        for theme in asset_build.BACKGROUNDS:
            with self.subTest(theme=theme):
                self.assertTrue(approved.is_border_approved(theme))

    def test_unknown_theme_returns_no_border(self):
        self.assertIsNone(approved.border("no_such_theme"))

    def test_crystal_is_a_plain_registry_entry(self):
        # Crystal used to be nine-sliced by gen_sprites; it is now a normal
        # approved import like every other theme.
        self.assertIn("crystal", approved.APPROVED_BORDERS)
        self.assertEqual(approved.BORDER_SPECIAL, set())

    def test_every_registered_border_loads_and_matches_its_pin(self):
        for theme, (_, expected) in approved.APPROVED_BORDERS.items():
            with self.subTest(theme=theme):
                image = approved.border(theme)
                self.assertEqual(image.size, (240, 240))
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(
                    hashlib.sha256(image.tobytes()).hexdigest(), expected
                )

    def test_every_registered_border_matches_its_published_asset(self):
        for theme, (_, expected) in approved.APPROVED_BORDERS.items():
            with self.subTest(theme=theme):
                with Image.open(
                    ROOT / "assets" / f"border_{theme}_0.png"
                ) as published:
                    published.load()
                    self.assertEqual(
                        hashlib.sha256(published.convert("RGBA").tobytes()).hexdigest(),
                        expected,
                    )

    def test_registered_borders_keep_the_opening_clear(self):
        for theme in approved.APPROVED_BORDERS:
            with self.subTest(theme=theme):
                image = approved.border(theme)
                self.assertIsNone(
                    image.getchannel("A").crop((16, 16, 224, 224)).getbbox()
                )

    def test_border_source_may_be_120_or_240(self):
        # A 120x120 source is doubled; a 240x240 source is used as-is.
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            original = approved.SOURCES
            approved.SOURCES = Path(directory)
            try:
                small = Image.new("RGBA", (120, 120), (0, 0, 0, 0))
                small.paste((10, 20, 30, 255), (0, 0, 120, 8))
                small.save(approved.SOURCES / "ghost_border.png")
                doubled = small.resize((240, 240), Image.Resampling.NEAREST)
                approved.APPROVED_BORDERS["ghost"] = (
                    "ghost_border.png",
                    hashlib.sha256(doubled.tobytes()).hexdigest(),
                )
                self.assertEqual(approved.border("ghost").size, (240, 240))
            finally:
                del approved.APPROVED_BORDERS["ghost"]
                approved.SOURCES = original

    def test_border_with_a_bad_size_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            original = approved.SOURCES
            approved.SOURCES = Path(directory)
            try:
                Image.new("RGBA", (100, 100), (1, 2, 3, 255)).save(
                    approved.SOURCES / "ghost_border.png"
                )
                approved.APPROVED_BORDERS["ghost"] = ("ghost_border.png", "0" * 64)
                with self.assertRaisesRegex(ValueError, "120x120 or 240x240"):
                    approved.border("ghost")
            finally:
                del approved.APPROVED_BORDERS["ghost"]
                approved.SOURCES = original

    def test_border_with_wrong_pixels_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            original = approved.SOURCES
            approved.SOURCES = Path(directory)
            try:
                Image.new("RGBA", (240, 240), (1, 2, 3, 255)).save(
                    approved.SOURCES / "ghost_border.png"
                )
                approved.APPROVED_BORDERS["ghost"] = ("ghost_border.png", "0" * 64)
                with self.assertRaisesRegex(ValueError, "differ from the approved"):
                    approved.border("ghost")
            finally:
                del approved.APPROVED_BORDERS["ghost"]
                approved.SOURCES = original


if __name__ == "__main__":
    unittest.main()
