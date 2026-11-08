"""Exact pixel and incomplete-transfer checks for the screenshot protocol."""
from pathlib import Path
import tempfile
import unittest

from PIL import Image
from capture_screen import capture


class Connection:
    def __init__(self, text):
        self.data = text.encode("ascii")

    def reset_input_buffer(self):
        pass

    def write(self, data):
        return len(data)

    def flush(self):
        pass

    def read(self, size):
        result, self.data = self.data[:size], self.data[size:]
        return result


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "capture.png"

    def test_rgb565_parts_exact_pixels(self):
        capture(Connection("SCREEN BEGIN 3 1 rgb565le\nSCREEN PART 0 0 00f8e007\n"
                           "SCREEN PART 0 4 1f00\nOK screenshot\n"), self.output)
        with Image.open(self.output) as image:
            self.assertEqual([image.getpixel((x, 0)) for x in range(3)],
                             [(255, 0, 0), (0, 255, 0), (0, 0, 255)])

    def test_native_spider_alpha(self):
        capture(Connection("SCREEN BEGIN 2 1 bgra8888\nSCREEN PART 0 0 00ffffff00000000\n"
                           "OK screenshot\n"), self.output, True)
        with Image.open(self.output) as image:
            self.assertEqual([image.getpixel((x, 0)) for x in range(2)],
                             [(255, 255, 0, 255), (0, 0, 0, 0)])

    def test_missing_bytes_rejected(self):
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            capture(Connection("SCREEN BEGIN 3 1 rgb565le\nSCREEN PART 0 0 00f8\n"
                               "OK screenshot\n"), self.output)
        self.assertFalse(self.output.exists())

    def test_overlapping_parts_rejected(self):
        with self.assertRaisesRegex(ValueError, "offset"):
            capture(Connection("SCREEN BEGIN 3 1 rgb565le\nSCREEN PART 0 0 00f8\n"
                               "SCREEN PART 0 0 00f8\n"), self.output)

    def test_part_without_header_rejected(self):
        with self.assertRaisesRegex(ValueError, "offset"):
            capture(Connection("SCREEN PART 0 0 00f8\n"), self.output)


if __name__ == "__main__":
    unittest.main()
