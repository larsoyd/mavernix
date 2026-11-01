import sys
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/apps/theme-installer/backend"))
import make_cursors as cursors


class TextOutlineTests(unittest.TestCase):
    def test_outline_contrasts_and_preserves_hotspot_at_every_size(self):
        for size in cursors.NOMINAL_SIZES:
            with self.subTest(size=size):
                factor = size / cursors.POINTS_AT_NOMINAL
                source = Image.new("RGBA", (5, 9))
                source.putpixel((2, 4), (0, 0, 0, 255))
                picture, x, y = cursors.with_text_outline(source, 2, 4, factor)
                x, y = round(x), round(y)
                self.assertEqual(picture.getpixel((x, y)), (0, 0, 0, 255))
                for background in ("black", "#202020", "white"):
                    composite = Image.new("RGBA", picture.size, background)
                    composite.alpha_composite(picture)
                    self.assertEqual(composite.getpixel((x - 1, y)), (255, 255, 255, 255))
                self.assertEqual(picture.getpixel((0, 0))[3], 0)
                self.assertEqual(picture.width - source.width, 2 * x - 4)

    def test_outline_does_not_clip_edge_or_change_translucent_color(self):
        source = Image.new("RGBA", (1, 1), (0, 0, 0, 128))
        picture, x, y = cursors.with_text_outline(source, 0, 0, 1)
        self.assertEqual((x, y), (1, 1))
        self.assertEqual(picture.size, (3, 3))
        self.assertEqual(picture.getpixel((0, 1)), (255, 255, 255, 128))
        self.assertEqual(picture.getpixel((1, 1))[3], 192)


if __name__ == "__main__":
    unittest.main()
