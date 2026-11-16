"""Tests for the background ground-texture prompt builder."""

import re
import tempfile
import unittest
from pathlib import Path

import asset_build
import background_prompts as bp

ROOT = Path(__file__).resolve().parents[1]


class CoverageTests(unittest.TestCase):
    def test_every_background_theme_has_a_recipe(self):
        self.assertEqual(bp.missing(), [])

    def test_no_recipe_is_orphaned(self):
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                self.assertIn(theme, asset_build.BACKGROUNDS)

    def test_unknown_theme_is_rejected(self):
        with self.assertRaisesRegex(KeyError, "no prompt recipe"):
            bp.prompt("no_such_theme")


class PromptTests(unittest.TestCase):
    def test_prompt_states_it_is_a_material_sample(self):
        # The device composites a separate border layer, so a floor that draws
        # its own edge decoration would be drawn twice.
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("FLAT MATERIAL SAMPLE, NOT A SCENE", text)
                self.assertIn("no border", text)

    def test_prompt_never_names_a_commercial_game(self):
        # Naming a title makes the provider refuse with a copyright block.
        banned = ("mario", "zelda", "chrono", "mana", "pokemon", "final fantasy",
                  "stardew", "terraria", "nes", "snes")
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                words = set(re.findall(r"[a-z]+", bp.prompt(theme).lower()))
                for word in banned:
                    self.assertNotIn(word, words)

    def test_organic_themes_forbid_repetition(self):
        for theme, recipe in bp.THEMES.items():
            if not recipe[4]:
                continue
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("NON-REPETITION IS THE MOST IMPORTANT RULE", text)
                self.assertIn("must NOT repeat", text)

    def test_manufactured_themes_allow_a_repeat_unit(self):
        for theme, recipe in bp.THEMES.items():
            if recipe[4]:
                continue
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("REGULARITY IS CORRECT HERE", text)
                self.assertIn(recipe[5], text)
                self.assertNotIn("NON-REPETITION", text)

    def test_sparse_themes_ask_for_a_narrow_band(self):
        for theme in bp.SPARSE:
            with self.subTest(theme=theme):
                self.assertIn("EXTREMELY calm", bp.prompt(theme))

    def test_every_prompt_carries_the_pixel_art_rules(self):
        for theme in bp.THEMES:
            with self.subTest(theme=theme):
                text = bp.prompt(theme)
                self.assertIn("no anti-aliasing", text)
                self.assertIn("Maximum 64 colors", text)
                self.assertIn("No text, no UI, no characters", text)

    def test_prompt_is_deterministic(self):
        self.assertEqual(bp.prompt("snow"), bp.prompt("snow"))


class CliTests(unittest.TestCase):
    def test_theme_prints_one_prompt(self):
        self.assertEqual(bp.main(["--theme", "snow"]), 0)

    def test_all_writes_every_prompt(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            out = Path(directory)
            self.assertEqual(bp.main(["--all", "--out", str(out)]), 0)
            written = sorted(path.name for path in out.glob("prompt_*.txt"))
            self.assertEqual(len(written), len(bp.THEMES))
            self.assertIn("prompt_snow.txt", written)

    def test_all_requires_an_output_directory(self):
        with self.assertRaises(SystemExit):
            bp.main(["--all"])


if __name__ == "__main__":
    unittest.main()
