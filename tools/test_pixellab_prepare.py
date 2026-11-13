"""Offline pilot guardrails; no credential handling or API calls."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

from pixellab_prepare import (
    ROOT, asset_name, check_candidate, main, make_brief, mask_image, prompt, write_new,
)


class PixelLabPreparationTests(unittest.TestCase):
    def test_draft_is_explicitly_offline_and_has_no_cost_assumption(self):
        brief = make_brief("background", "fern", "edit", None)
        self.assertTrue(brief["offline_only"])
        self.assertFalse(brief["ready_for_reference_review"])
        self.assertIsNone(brief["api_payload"])
        self.assertEqual(brief["budget"]["generations_spent_by_this_tool"], 0)
        self.assertIsNone(brief["budget"]["generations_per_api_request"])
        self.assertEqual(brief["budget"]["pilot_max_requests"], 1)
        self.assertEqual(brief["budget"]["automatic_retries"], 0)
        self.assertIn("16 close substrate tones", brief["prompt"])
        self.assertIn("No blur", brief["prompt"])

    def test_actual_reference_is_identified_not_embedded_or_transmitted(self):
        reference = ROOT / "assets" / "background_moss_0.png"
        brief = make_brief("border", "moss", "inpaint", reference)
        self.assertTrue(brief["ready_for_reference_review"])
        self.assertEqual(brief["style_reference"]["size"], [240, 240])
        self.assertEqual(len(brief["style_reference"]["sha256"]), 64)
        self.assertIn("white mask", brief["prompt"])
        self.assertIn("uniform aligned 2x2", brief["prompt"])
        self.assertNotIn("base64", brief)

    def test_masks_preserve_exact_openings(self):
        for kind, box, count in (
            ("background", (40, 40, 200, 200), 160 * 160),
            ("border", (16, 16, 224, 224), 208 * 208),
        ):
            image = mask_image(kind)
            self.assertEqual(image.size, (240, 240))
            self.assertIsNone(image.crop(box).getbbox())
            self.assertEqual(image.histogram()[0], count)
            self.assertEqual(image.getpixel((0, 0)), 255)

    def test_existing_sources_pass_candidate_checks_without_installation(self):
        for kind in ("background", "border"):
            path = ROOT / "assets" / asset_name(kind, "moss")
            result = check_candidate(path, kind, "moss")
            self.assertEqual(result["technical_checks"], "PASS")
            self.assertFalse(result["installed"])

    def test_invalid_dimensions_and_quiet_center_fail_not_autocorrect(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.png"
            Image.new("RGBA", (256, 256), (100, 100, 100, 255)).save(path)
            with self.assertRaisesRegex(ValueError, "240x240"):
                check_candidate(path, "background", "moss")
            image = Image.new("RGBA", (240, 240), (100, 100, 100, 255))
            for index in range(17):
                image.putpixel((50 + index, 50), (index, 0, 0, 255))
            image.save(path)
            with self.assertRaisesRegex(ValueError, "quiet center"):
                check_candidate(path, "background", "moss")

    def test_no_overwrite_and_no_unknown_asset_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "brief.json"
            write_new(path, "{}")
            with self.assertRaises(FileExistsError):
                write_new(path, "changed")
            self.assertEqual(path.read_text().strip(), "{}")
        with self.assertRaises(ValueError):
            asset_name("spider", "moss")
        with self.assertRaises(ValueError):
            prompt("background", "unknown", "edit")

    def test_cli_requires_reference_and_refuses_repository_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "brief.json"
            with patch("sys.argv", ["pixellab_prepare", "brief", "--kind",
                                   "background", "--output", str(output)]):
                with self.assertRaises(SystemExit) as error:
                    main()
                self.assertEqual(error.exception.code, 1)
            self.assertFalse(output.exists())
        output = ROOT / "assets" / "pilot-must-not-exist.png"
        with patch("sys.argv", ["pixellab_prepare", "mask", "--kind", "border",
                               "--output", str(output)]):
            with self.assertRaises(SystemExit) as error:
                main()
            self.assertEqual(error.exception.code, 1)
        self.assertFalse(output.exists())

    def test_crystal_brief_preserves_approved_texture_without_baked_in_border(self):
        text = prompt("background", "crystal", "edit")
        self.assertIn("Do not restrict its center to three tones", text)
        self.assertIn("separate border/decor layers", text)

    def test_crystal_border_brief_separates_material_style_from_large_geometry(self):
        text = prompt("border", "crystal", "edit")
        self.assertIn("not its crystal sizes", text)
        self.assertIn("outer 16px rim", text)
        self.assertIn("8x8", text)
        self.assertIn("retain the raw source", text)
        self.assertIn("nine-slice", text)


if __name__ == "__main__":
    unittest.main()
