import io
import re
import subprocess
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

    def drawing(self, source, width, height):
        content = f"0 0 {width} {height} re f"
        objects = ["<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R]/Count 1>>",
                   f"<</Type/Page/Parent 2 0 R/MediaBox[0 0 {width} {height}]/Contents 4 0 R>>",
                   f"<</Length {len(content)}>>stream\n{content}\nendstream"]
        text, offsets = "%PDF-1.4\n", []
        for number, body in enumerate(objects, 1):
            offsets.append(len(text))
            text += f"{number} 0 obj{body}endobj\n"
        table = "".join(f"{offset:010d} 00000 n \n" for offset in offsets)
        text += f"xref\n0 5\n0000000000 65535 f \n{table}trailer<</Size 5/Root 1 0 R>>\nstartxref\n{len(text)}\n%%EOF\n"
        target = self.work / "root" / source
        target.parent.mkdir(parents=True)
        target.write_bytes(text.encode())

    def test_vector_template_keeps_its_size_where_it_fits_and_shrinks_where_it_does_not(self):
        self.drawing("Applications/Tabs.app/NewTab.pdf", 20, 16)
        contexts = make_icons.write_action_icons(self.work, self.theme, {"tab-new": "Applications/Tabs.app/NewTab.pdf"}, {})
        self.assertEqual(contexts, {(16, 1), (22, 1), (16, 2), (22, 2)})
        # the page is all black, so the painted box is the page plus the emboss line under it
        # shrunk to 14 wide the page is 11.2 high, and a part covered row still counts
        for folder, square, glyph in (("22x22", 22, (20, 17)), ("22x22@2", 44, (40, 34)),
                                      ("16x16", 16, (14, 13)), ("16x16@2", 32, (28, 25))):
            with self.subTest(folder=folder):
                picture = Image.open(self.theme / folder / "actions" / "tab-new.png").convert("RGBA")
                self.assertEqual(picture.size, (square, square))
                left, top, right, bottom = picture.getbbox()
                self.assertEqual((right - left, bottom - top), glyph)
                self.assertLessEqual(abs(left - (square - right)), 1)
                self.assertEqual(picture.getpixel((left + 1, top))[:3], make_theme.TOOLBAR_GLYPH_TOP)
                self.assertEqual(picture.getpixel((left + 1, bottom - 1))[:3], (255, 255, 255))

    def test_missing_vector_template_is_refused(self):
        with self.assertRaises(SystemExit):
            make_icons.write_action_icons(self.work, self.theme, {"tab-new": "Applications/Tabs.app/NewTab.pdf"}, {})

    def test_vector_template_that_cannot_be_drawn_is_refused(self):
        target = self.work / "root" / "Applications/Tabs.app/NewTab.pdf"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"not a drawing")
        with self.assertRaises(SystemExit):
            make_icons.write_action_icons(self.work, self.theme, {"tab-new": "Applications/Tabs.app/NewTab.pdf"}, {})

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


# what kde's icon loader puts in the sheet for a row, text colour first
KDE_SHEET = (".ColorScheme-Text {{ color:{text}; }}.ColorScheme-Background{{ color:#ededed; }}"
             ".ColorScheme-Highlight{{ color:#5199db; }}.ColorScheme-HighlightedText{{ color:#ffffff; }}")


def drawn(svg_text, text_colour=None):
    """What librsvg draws, with the colour sheet swapped as kde swaps it when a colour is given."""
    if text_colour is not None:
        svg_text, swaps = re.subn(r'(<style [^>]*id="current-color-scheme"[^>]*>)[^<]*(</style>)',
                                  lambda found: found.group(1) + KDE_SHEET.format(text=text_colour) + found.group(2),
                                  svg_text)
        if swaps != 1:
            raise AssertionError(f"the glyph holds {swaps} colour sheets")
    run = subprocess.run(["rsvg-convert"], input=svg_text.encode(), capture_output=True, timeout=30)
    if run.returncode != 0:
        raise AssertionError(f"rsvg-convert rc={run.returncode} err={run.stderr[:200]!r}")
    return Image.open(io.BytesIO(run.stdout)).convert("RGBA")


def largest_difference(first, second):
    return max(band.getextrema()[1] for band in ImageChops.difference(first, second).split())


def glyph(size):
    """A blue-grey shape with a soft edge and a lighter line, as the side bar's own are."""
    picture = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(picture)
    draw.ellipse((size // 8, size // 8, size * 7 // 8, size * 7 // 8), fill=(103, 114, 129, 255))
    draw.line((size // 4, size // 2, size * 3 // 4, size // 2), fill=(150, 160, 172, 255))
    draw.point((size // 8, size // 2), fill=(103, 114, 129, 90))
    return picture


class SideBarGlyphTests(unittest.TestCase):
    def test_a_glyph_is_as_drawn_while_the_text_is_dark(self):
        for size in (22, 44):
            for colour in (None, "#000000"):
                with self.subTest(size=size, colour=colour):
                    picture = drawn(make_icons.side_bar_svg(glyph(size)), colour)
                    self.assertEqual(picture.size, (size, size))
                    self.assertLessEqual(largest_difference(picture, glyph(size)), 2)

    def test_a_glyph_turns_white_on_a_selected_row_and_keeps_its_shape(self):
        picture = drawn(make_icons.side_bar_svg(glyph(22)), "#ffffff")
        solid = [pixel for pixel, was in zip(picture.getdata(), glyph(22).getdata()) if was[3] == 255]
        self.assertGreater(len(solid), 100)
        self.assertGreaterEqual(min(min(pixel[:3]) for pixel in solid), 250)
        self.assertEqual(min(pixel[3] for pixel in solid), 255)
        clear = [pixel[3] for pixel, was in zip(picture.getdata(), glyph(22).getdata()) if was[3] == 0]
        self.assertEqual(max(clear), 0)

    def test_light_text_of_a_dark_look_lightens_the_glyph(self):
        picture = drawn(make_icons.side_bar_svg(glyph(22)), "#d0d0d0")
        solid = [pixel for pixel, was in zip(picture.getdata(), glyph(22).getdata()) if was[3] == 255]
        self.assertGreaterEqual(min(min(pixel[:3]) for pixel in solid), 200)

    def test_the_colour_sheet_holds_only_the_text_colour(self):
        text = make_icons.side_bar_svg(glyph(22))
        self.assertEqual(re.findall(r"<style\b[^>]*>([^<]*)</style>", text), [".ColorScheme-Text{color:#000000;}"])
        self.assertIn('<style type="text/css" id="current-color-scheme">', text)

    def test_a_picture_kept_as_a_drawing_has_no_colour_sheet_and_draws_the_same(self):
        picture = glyph(64)
        text = make_icons.picture_svg(picture)
        self.assertNotIn("<style", text)
        self.assertLessEqual(largest_difference(drawn(text), picture), 2)

    def test_index_lets_kde_recolour_and_the_dark_theme_does_not(self):
        text = make_icons.index_text({(22, 1): {"places"}})
        self.assertIn("\nFollowsColorScheme=true\n", text.split("\n\n")[0] + "\n")
        with tempfile.TemporaryDirectory() as folder:
            light = Path(folder) / "sea-lion"
            icons = light / "share" / "icons" / "SeaLion"
            (icons / "22x22/places").mkdir(parents=True)
            (light / "share" / "sounds").mkdir()
            (icons / "index.theme").write_text(text)
            make_dark_theme.make_icon_theme(light, Path(folder) / "sea-lion-dark")
            dark = (Path(folder) / "sea-lion-dark/share/icons/SeaLionDark/index.theme").read_text()
            self.assertIn("22x22/places", dark)
            self.assertNotIn("FollowsColorScheme", dark)

    def theme(self, folder):
        """A theme holding user-home as the side bar has it and at two other sizes, and two names it lacks there."""
        theme = Path(folder)
        for place, size in (("22x22", 22), ("22x22@2", 44), ("16x16", 16), ("128x128", 128)):
            for context in ("places", "devices", "mimetypes"):
                (theme / place / context).mkdir(parents=True)
            glyph(size).save(theme / place / "places" / "user-home.png")
            glyph(size).save(theme / place / "places" / "folder-symbolic.png")
        glyph(22).save(theme / "22x22" / "devices" / "computer.png")
        glyph(64).save(theme / "128x128" / "devices" / "computer.png")
        glyph(16).save(theme / "16x16" / "mimetypes" / "user-home.png")
        glyph(16).save(theme / "16x16" / "places" / "folder.png")
        (theme / "22x22" / "actions").mkdir()
        glyph(22).save(theme / "22x22" / "actions" / "go-next.png")
        glyph(22).save(theme / "22x22" / "actions" / "user-home.png")
        (theme / "index.theme").write_text("[Icon Theme]\nName=SeaLion\nInherits=breeze\nDirectories=22x22/places\n"
                                           "\n[22x22/places]\nSize=22\n")
        return theme

    def test_every_size_of_a_side_bar_name_becomes_a_drawing_and_only_its_glyph_recolours(self):
        with tempfile.TemporaryDirectory() as folder:
            theme = self.theme(folder)
            self.assertEqual(make_icons.draw_side_bar_names(theme), 8)
            self.assertEqual(sorted(str(path.relative_to(theme)) for path in theme.rglob("*.png")),
                             ["128x128/places/folder-symbolic.png", "16x16/places/folder-symbolic.png",
                              "16x16/places/folder.png", "22x22/actions/go-next.png",
                              "22x22/places/folder-symbolic.png", "22x22@2/places/folder-symbolic.png"])
            for place, size in (("22x22", 22), ("22x22@2", 44), ("16x16", 16), ("128x128", 128)):
                with self.subTest(place=place):
                    text = (theme / place / "places" / "user-home.svg").read_text()
                    self.assertEqual("<style" in text, place.startswith("22x22"))
                    self.assertLessEqual(largest_difference(drawn(text), glyph(size)), 2)
            self.assertNotIn("<style", (theme / "16x16" / "mimetypes" / "user-home.svg").read_text())
            self.assertNotIn("<style", (theme / "22x22" / "actions" / "user-home.svg").read_text())
            self.assertNotIn("<style", (theme / "128x128" / "devices" / "computer.svg").read_text())
            self.assertIn("<style", (theme / "22x22" / "devices" / "computer.svg").read_text())

    def test_an_older_theme_is_marked_once_and_a_second_run_changes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            theme = self.theme(folder)
            make_icons.draw_side_bar_names(theme)
            index = (theme / "index.theme").read_text()
            self.assertEqual(index.count("FollowsColorScheme=true"), 1)
            self.assertTrue(index.startswith("[Icon Theme]\nName=SeaLion\n"))
            self.assertIn("\n\n[22x22/places]\nSize=22\n", index)
            before = {path: path.read_bytes() for path in theme.rglob("*") if path.is_file()}
            self.assertEqual(make_icons.draw_side_bar_names(theme), 0)
            self.assertEqual({path: path.read_bytes() for path in theme.rglob("*") if path.is_file()}, before)


if __name__ == "__main__":
    unittest.main()
