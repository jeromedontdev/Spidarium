"""Tests for the border conversion pipeline."""

import unittest

from PIL import Image

import import_border as ib


def frame(band=12, size=120, color=(200, 210, 220)):
    """A synthetic square frame with a hollow middle."""
    image = Image.new("RGB", (size, size), (0, 0, 0))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            if x < band or y < band or x >= size - band or y >= size - band:
                shade = 40 + (x + y) % 160
                pixels[x, y] = (shade, shade, shade)
    return image


class GeometryTests(unittest.TestCase):
    def test_opening_mask_is_binary_and_hollow(self):
        mask = ib.opening_mask(8)
        self.assertEqual(mask.size, (120, 120))
        self.assertEqual(set(mask.get_flattened_data()), {0, 255})
        self.assertIsNone(mask.crop((8, 8, 112, 112)).getbbox())

    def test_opening_mask_keeps_the_rim_opaque(self):
        mask = ib.opening_mask(8)
        self.assertEqual(mask.getpixel((0, 0)), 255)
        self.assertEqual(mask.getpixel((119, 119)), 255)
        self.assertEqual(mask.getpixel((60, 4)), 255)
        self.assertEqual(mask.getpixel((60, 60)), 0)

    def test_content_box_ignores_a_black_margin(self):
        image = Image.new("RGB", (120, 120), (0, 0, 0))
        image.paste(frame(10, 100), (10, 10))
        self.assertEqual(ib.content_box(image), (10, 10, 110, 110))

    def test_content_box_rejects_an_all_black_image(self):
        with self.assertRaisesRegex(ValueError, "entirely black"):
            ib.content_box(Image.new("RGB", (120, 120), (0, 0, 0)))

    def test_detect_opening_finds_the_hollow_middle(self):
        self.assertEqual(ib.detect_opening(frame(12)), (12, 12, 108, 108))

    def test_detect_opening_handles_a_white_opening(self):
        # Generators fill the middle with white as often as black.
        image = Image.new("RGB", (120, 120), (255, 255, 255))
        image.paste(frame(12), (0, 0))
        self.assertEqual(ib.detect_opening(image), (12, 12, 108, 108))

    def test_detect_opening_rejects_a_filled_image(self):
        with self.assertRaisesRegex(ValueError, "no band around its middle"):
            ib.detect_opening(Image.new("RGB", (120, 120), (200, 200, 200)))

    def test_fit_frame_lands_on_the_contract_rim(self):
        image, band = ib.fit_frame(frame(12), 8)
        self.assertEqual(image.size, (120, 120))
        self.assertGreater(band, 0)

    def test_fit_frame_upscales_a_narrow_band(self):
        # A generator that draws a 5px band still drew a frame; the fit widens
        # it to the contract rim rather than rejecting the candidate.
        image, band = ib.fit_frame(frame(5), 8)
        self.assertEqual(image.size, (120, 120))
        self.assertGreater(band, 0)

    def test_fit_frame_rejects_a_frame_with_no_band(self):
        with self.assertRaisesRegex(ValueError, "no band around its middle"):
            ib.fit_frame(Image.new("RGB", (120, 120), (200, 200, 200)), 8)

    def test_nine_slice_rejects_an_impossible_rim(self):
        with self.assertRaisesRegex(ValueError, "does not fit"):
            ib.nine_slice(frame(12), 12, 70)


class ConvertTests(unittest.TestCase):
    def test_convert_produces_a_transparent_opening(self):
        image = ib.convert(frame(12), mean=100.0, contrast=1.0)
        self.assertEqual(image.size, (120, 120))
        self.assertIsNone(image.getchannel("A").crop((8, 8, 112, 112)).getbbox())

    def test_convert_keeps_alpha_binary(self):
        image = ib.convert(frame(12), mean=100.0, contrast=1.0)
        self.assertEqual(set(image.getchannel("A").get_flattened_data()), {0, 255})

    def test_convert_respects_the_sixteen_color_cap(self):
        image = ib.convert(frame(12), mean=100.0, contrast=1.0)
        self.assertLessEqual(ib.profile(image)["colors"], ib.MAX_COLORS)

    def test_convert_retargets_the_rim_mean(self):
        image = ib.convert(frame(12), mean=90.0, contrast=1.0)
        self.assertAlmostEqual(ib.profile(image)["mean"], 90.0, delta=6.0)

    def test_convert_rejects_a_source_with_no_frame(self):
        with self.assertRaisesRegex(ValueError, "no band around its middle"):
            ib.convert(Image.new("RGB", (240, 240), (200, 200, 200)))

    def test_published_doubles_every_pixel(self):
        image = ib.convert(frame(12), mean=100.0, contrast=1.0)
        big = ib.published(image)
        self.assertEqual(big.size, (240, 240))
        for y in range(0, 240, 7):
            for x in range(0, 240, 7):
                self.assertEqual(big.getpixel((x, y)), image.getpixel((x // 2, y // 2)))

    def test_published_opening_matches_the_contract(self):
        image = ib.published(ib.convert(frame(12), mean=100.0, contrast=1.0))
        self.assertIsNone(image.getchannel("A").crop((16, 16, 224, 224)).getbbox())

    def test_pixel_hash_is_stable(self):
        first = ib.convert(frame(12), mean=100.0, contrast=1.0)
        second = ib.convert(frame(12), mean=100.0, contrast=1.0)
        self.assertEqual(ib.pixel_hash(first), ib.pixel_hash(second))

    def test_pixel_hash_changes_with_the_mean(self):
        first = ib.convert(frame(12), mean=90.0, contrast=1.0)
        second = ib.convert(frame(12), mean=140.0, contrast=1.0)
        self.assertNotEqual(ib.pixel_hash(first), ib.pixel_hash(second))


class ContrastTests(unittest.TestCase):
    def test_fit_contrast_keeps_detail_under_the_ceiling(self):
        # A frame is decorative, so the fit must not flatten it to crystal's
        # floor-like sd; it only has to come in under the ceiling.
        factor, image = ib.fit_contrast(frame(12), mean=100.0)
        self.assertLessEqual(ib.profile(image)["sd"], ib.SD_CEILING + 2.0)
        self.assertGreater(factor, 0.0)

    def test_fit_contrast_leaves_a_calm_frame_untouched(self):
        calm = Image.new("RGB", (120, 120), (0, 0, 0))
        pixels = calm.load()
        for y in range(120):
            for x in range(120):
                if x < 12 or y < 12 or x >= 108 or y >= 108:
                    pixels[x, y] = (100, 100, 100)
        factor, _ = ib.fit_contrast(calm, mean=100.0)
        self.assertAlmostEqual(factor, 1.0, delta=0.05)


if __name__ == "__main__":
    unittest.main()
