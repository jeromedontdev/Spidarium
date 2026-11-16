"""Tests for the enclosure-frame prompt builder."""

import re
import unittest
from pathlib import Path

import asset_build
import border_prompts as bp

ROOT = Path(__file__).resolve().parents[1]


class CoverageTests(unittest.TestCase):
    def test_every_border_theme_has_a_recipe(self):
        self.assertEqual(bp.missing(), [])

    def test_no_recipe_is_orphaned(self):
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                self.assertIn(theme, asset_build.BACKGROUNDS)

    def test_unknown_theme_is_rejected(self):
        with self.assertRaisesRegex(KeyError, "no prompt recipe"):
            bp.prompt("no_such_theme")


class PromptTests(unittest.TestCase):
    def test_prompt_demands_a_hollow_centre(self):
        # The border is composited over the floor, so a filled middle would
        # hide the spider entirely.
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("EMPTY and hollow", text)
                self.assertIn("no filled centre", text)

    def test_prompt_demands_a_thin_band(self):
        # The published rim is 8 logical pixels of a 120px border, so the
        # generator must not draw a wide band that has to be crushed down.
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                self.assertIn("NARROW", bp.prompt(theme))

    def test_prompt_never_names_a_commercial_game(self):
        # Naming a title makes the provider refuse with a copyright block.
        banned = ("mario", "zelda", "chrono", "mana", "pokemon", "final fantasy",
                  "stardew", "terraria", "nes", "snes")
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                words = set(re.findall(r"[a-z]+", bp.prompt(theme).lower()))
                for word in banned:
                    self.assertNotIn(word, words)

    def test_prompt_forbids_a_scene(self):
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("no scene", text)
                self.assertIn("no landscape", text)

    def test_prompt_states_the_light_direction(self):
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                self.assertIn("one light from the top-left", bp.prompt(theme))

    def test_prompt_caps_the_palette_at_sixteen(self):
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                self.assertIn("Maximum 16 colors", bp.prompt(theme))

    def test_plain_themes_ask_for_restraint(self):
        for theme in bp.PLAIN:
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("RESTRAINT", text)
                self.assertNotIn("MOTIF:", text)

    def test_motif_themes_name_their_motif(self):
        for theme in bp.THEMES:
            if theme in bp.PLAIN:
                continue
            with self.subTest(theme=theme):
                self.assertIn("MOTIF:", bp.prompt(theme))

    def test_every_recipe_has_three_parts(self):
        for theme, recipe in bp.THEMES.items():
            with self.subTest(theme=theme):
                self.assertEqual(len(recipe), 3)
                for part in recipe:
                    self.assertTrue(part.strip())


if __name__ == "__main__":
    unittest.main()
