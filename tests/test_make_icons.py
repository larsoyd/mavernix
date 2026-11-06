import re
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/apps/theme-installer/backend"))
import make_dark_theme
import make_icons
import make_theme

FOLDER_BLUE = (150, 190, 212, 255)

# the emblem folders kio 6.31's new folder dialog offers
DOLPHIN_EMBLEM_FOLDERS = ["folder-bookmark", "folder-cloud", "folder-development", "folder-games", "folder-mail",
                          "folder-music", "folder-print", "folder-tar", "folder-temp", "folder-important"]

# where mavericks' own folders put their emblem, measured at 512 pixels
EMBLEM_CENTRE = (256, 294)
EMBLEM_HEIGHTS = (180, 256)


def folder_picture(size):
    picture = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(picture).rectangle((0, size // 8, size - 1, size * 7 // 8), fill=FOLDER_BLUE)
    return picture


def changed_box(before, after):
    difference = ImageChops.difference(before.convert("RGB"), after.convert("RGB")).convert("L")
    return difference.point(lambda value: 255 if value > 6 else 0).getbbox()


class ActionIconTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.work = Path(self.folder.name)
        self.art = self.work / "artwork" / "sartfile"
        self.art.mkdir(parents=True)
        self.theme = self.work / "theme"

    def tearDown(self):
        self.folder.cleanup()

    def shape(self, entry, size, density, colour=(0, 0, 0, 255)):
        Image.new("RGBA", size, colour).save(self.art / f"{entry:03d}-{density}x.png")

    def test_template_is_painted_like_a_toolbar_glyph_and_centred(self):
        self.shape(127, (9, 10), 1)
        self.shape(127, (18, 20), 2)
        contexts = make_icons.write_action_icons(self.work, self.theme, {"go-previous": 127}, {})
        self.assertEqual(contexts, {(16, 1), (22, 1), (16, 2), (22, 2)})
        for folder, square, density in (("22x22", 22, 1), ("16x16", 16, 1), ("22x22@2", 44, 2)):
            with self.subTest(folder=folder):
                picture = Image.open(self.theme / folder / "actions" / "go-previous.png").convert("RGBA")
                self.assertEqual(picture.size, (square, square))
                left, top, right, bottom = picture.getbbox()
                self.assertLessEqual(abs(left - (square - right)), 1)
                self.assertEqual(picture.getpixel((left + 1, top))[:3], make_theme.TOOLBAR_GLYPH_TOP)
                below = picture.getpixel((left + 1, bottom - 1))
                self.assertEqual(below[:3], (255, 255, 255))
                self.assertLess(below[3], 255)
                self.assertEqual(picture.getpixel((0, 0))[3], 0)

    def test_picture_larger_than_the_square_is_left_to_the_next_size(self):
        self.shape(188, (18, 18), 1)
        make_icons.write_action_icons(self.work, self.theme, {"folder-new": 188}, {})
        self.assertFalse((self.theme / "16x16" / "actions" / "folder-new.png").exists())
        self.assertTrue((self.theme / "22x22" / "actions" / "folder-new.png").exists())

    def test_coloured_picture_is_drawn_as_it_is(self):
        self.shape(239, (14, 14), 1, (200, 20, 20, 180))
        make_icons.write_action_icons(self.work, self.theme, {}, {"edit-clear": 239})
        picture = Image.open(self.theme / "16x16" / "actions" / "edit-clear.png").convert("RGBA")
        self.assertEqual(picture.getpixel((8, 8)), (200, 20, 20, 180))

    def test_missing_shared_art_is_refused(self):
        with self.assertRaises(SystemExit):
            make_icons.write_action_icons(self.work, self.theme, {"go-next": 128}, {})

    def test_index_lists_actions(self):
        text = make_icons.index_text({(22, 1): {"actions"}, (22, 2): {"actions"}})
        self.assertIn("[22x22/actions]\nSize=22\nContext=Actions", text)
        self.assertIn("[22x22@2/actions]\nSize=22\nContext=Actions\nScale=2", text)

    def test_dark_theme_keeps_breeze_dark_for_actions(self):
        light = self.work / "sea-lion"
        icons = light / "share" / "icons" / "SeaLion"
        for folder in ("16x16/places", "22x22/actions"):
            (icons / folder).mkdir(parents=True)
        (light / "share" / "sounds").mkdir()
        (icons / "index.theme").write_text(make_icons.index_text({(16, 1): {"places"}, (22, 1): {"actions"}}))
        make_dark_theme.make_icon_theme(light, self.work / "sea-lion-dark")
        dark = (self.work / "sea-lion-dark/share/icons/SeaLionDark/index.theme").read_text()
        self.assertIn("16x16/places", dark)
        self.assertNotIn("actions", dark)


class EngravingTests(unittest.TestCase):
    def mask(self, size, box):
        mask = Image.new("L", (size, size), 0)
        ImageDraw.Draw(mask).rectangle(box, fill=255)
        return mask

    def test_inside_takes_the_fill_and_far_outside_stays(self):
        folder = folder_picture(64)
        engraved = make_icons.engrave(folder, self.mask(64, (20, 20, 43, 43)), 64)
        k, b, across, down = make_icons.ENGRAVINGS[64][0][0]
        x, y = 32, 32
        expected = round(FOLDER_BLUE[0] * k + b + across * x / 63 + down * y / 63)
        self.assertLessEqual(abs(engraved.getpixel((x, y))[0] - expected), 1)
        self.assertLess(engraved.getpixel((x, y))[2], FOLDER_BLUE[2])
        self.assertEqual(engraved.getpixel((5, 20)), FOLDER_BLUE)
        self.assertEqual(engraved.getpixel((5, 2))[3], 0)

    def test_fill_darkens_less_to_the_right_and_lower_down(self):
        size = 512
        engraved = make_icons.engrave(folder_picture(size), self.mask(size, (60, 100, 451, 411)), size)
        fill = make_icons.ENGRAVINGS[size][0]
        for x, y in ((100, 300), (420, 300), (256, 150), (256, 380)):
            with self.subTest(x=x, y=y):
                for band, (k, b, across, down) in enumerate(fill):
                    expected = FOLDER_BLUE[band] * k + b + across * x / (size - 1) + down * y / (size - 1)
                    self.assertLessEqual(abs(engraved.getpixel((x, y))[band] - expected), 1.5)

    def test_lower_edge_gets_a_light_line_and_upper_edge_a_shadow(self):
        size = 512
        folder = folder_picture(size)
        engraved = make_icons.engrave(folder, self.mask(size, (180, 200, 331, 351)), size)
        filled = make_icons.engraved_fill(folder, make_icons.ENGRAVINGS[size][0])
        self.assertGreater(sum(engraved.getpixel((256, 353))[:3]), sum(FOLDER_BLUE[:3]))
        self.assertLess(sum(engraved.getpixel((256, 201))[:3]), sum(filled.getpixel((256, 201))[:3]) - 6)
        self.assertEqual(engraved.getpixel((256, 150)), FOLDER_BLUE)

    def test_emblem_past_the_folder_leaves_it_see_through(self):
        size = 512
        engraved = make_icons.engrave(folder_picture(size), self.mask(size, (170, 0, 341, 256)), size)
        self.assertEqual(engraved.getchannel("A").crop((0, 0, size, size // 8)).getextrema(), (0, 0))

    def test_small_emblems_are_drawn_crisp(self):
        with tempfile.TemporaryDirectory() as folder:
            artwork = Path(folder) / "bar.svg"
            artwork.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">'
                               '<rect x="100" y="100" width="312" height="312"/>'
                               '<rect x="40" y="0" width="6" height="512"/></svg>')
            small, large = make_icons.emblem_mask(artwork, 16), make_icons.emblem_mask(artwork, 64)
        self.assertEqual(small.getpixel((8, 8)), 255)
        self.assertEqual(small.getpixel((1, 8)), 0)
        self.assertTrue(0 < large.getpixel((5, 8)) < 255)

    def test_size_without_fitted_engraving_is_refused(self):
        with self.assertRaises(SystemExit):
            make_icons.engrave(folder_picture(48), self.mask(48, (10, 10, 30, 30)), 48)


class EmblemFolderTests(unittest.TestCase):
    def test_every_new_folder_emblem_has_an_icon(self):
        drawn = {path.stem for path in make_icons.EMBLEM_FOLDERS.glob("*.svg")}
        for name in DOLPHIN_EMBLEM_FOLDERS:
            with self.subTest(name=name):
                self.assertTrue(name in make_icons.ICONS or name in drawn)

    def test_emblems_sit_where_mavericks_puts_its_own(self):
        with tempfile.TemporaryDirectory() as folder:
            theme = Path(folder)
            sizes = [16, 32, 64, 128, 256, 512]
            for folder_size in sizes + ["22x22", "22x22@2"]:
                place = theme / (folder_size if isinstance(folder_size, str) else f"{folder_size}x{folder_size}")
                (place / "places").mkdir(parents=True)
                folder_picture(22 if isinstance(folder_size, str) else folder_size).save(place / "places" / "folder.png")
            written = make_icons.write_emblem_folders(theme)
            self.assertEqual(sorted(path.name for path in theme.glob("22x22*/places/*.png")), ["folder.png"] * 2)
            names = sorted(path.stem for path in make_icons.EMBLEM_FOLDERS.glob("*.svg"))
            self.assertEqual(len(names), 8)
            self.assertEqual(written, len(names) * len(sizes))
            for name in names:
                for size in sizes:
                    with self.subTest(name=name, size=size):
                        picture = Image.open(theme / f"{size}x{size}" / "places" / f"{name}.png")
                        self.assertEqual(picture.size, (size, size))
                        box = changed_box(folder_picture(size), picture)
                        self.assertIsNotNone(box)
                        if size != 512:
                            continue
                        left, top, right, bottom = box
                        self.assertLessEqual(abs((left + right) / 2 - EMBLEM_CENTRE[0]), 8)
                        self.assertLessEqual(abs((top + bottom) / 2 - EMBLEM_CENTRE[1]), 8)
                        self.assertTrue(EMBLEM_HEIGHTS[0] <= bottom - top <= EMBLEM_HEIGHTS[1] + 6)

    def test_side_bar_shows_each_emblem_as_a_glyph(self):
        with tempfile.TemporaryDirectory() as folder:
            theme = Path(folder)
            written = make_icons.write_side_bar_emblems(theme)
            self.assertEqual(written, {(22, 1), (22, 2)})
            for path in make_icons.EMBLEM_FOLDERS.glob("*.svg"):
                for place, square, density in (("22x22", 22, 1), ("22x22@2", 44, 2)):
                    with self.subTest(name=path.stem, place=place):
                        picture = Image.open(theme / place / "places" / f"{path.stem}.png").convert("RGBA")
                        self.assertEqual(picture.size, (square, square))
                        left, top, right, bottom = picture.getbbox()
                        self.assertEqual(max(right - left, bottom - top - density), 16 * density)
                        self.assertLessEqual(abs(left - (square - right)), 1)
                        darkest = min(sum(pixel[:3]) for pixel in picture.getdata() if pixel[3] == 255)
                        self.assertLessEqual(abs(darkest - sum(make_icons.SIDE_BAR_GLYPH_BOTTOM)), 12)

    def test_emblem_artwork_is_plain_vector(self):
        for path in make_icons.EMBLEM_FOLDERS.glob("*.svg"):
            with self.subTest(name=path.name):
                text = path.read_text()
                self.assertIn('viewBox="0 0 512 512"', text)
                self.assertIsNone(re.search(r"<(image|filter|clipPath|mask|text)\b", text))


if __name__ == "__main__":
    unittest.main()
