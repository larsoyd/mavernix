"""A desktop shortcut is a trusted copy of a program's desktop entry."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ACTION = Path(__file__).resolve().parent.parent / "src" / "session" / "bin" / "mavernix-file-action"

ENTRY = """[Desktop Entry]
Type=Application
Name=Paint Box
Name[nb]=Malerskrin
Icon=paint-box
Exec=paint-box %U

[Desktop Action new]
Name=New Picture
Icon=document-new
Exec=paint-box --new
"""


class DesktopShortcut(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.desktop = self.root / "home" / "Desktop"
        self.system = self.root / "system"
        self.own = self.root / "own"
        (self.system / "applications").mkdir(parents=True)
        (self.own / "applications").mkdir(parents=True)
        (self.system / "applications" / "paint-box.desktop").write_text(ENTRY)

    def tearDown(self):
        self.scratch.cleanup()

    def shortcut(self, *arguments):
        environment = {"PATH": os.environ["PATH"], "HOME": str(self.root / "home"),
                       "XDG_DATA_HOME": str(self.own), "XDG_DATA_DIRS": f"{self.root / 'nothing'}:{self.system}"}
        return subprocess.run([str(ACTION), "shortcut", str(self.desktop), *arguments], env=environment,
                              capture_output=True, text=True, timeout=20)

    def test_the_entry_lands_on_the_desktop_under_its_name_and_may_run(self):
        result = self.shortcut("paint-box")
        self.assertEqual(result.returncode, 0, result.stderr)
        launcher = self.desktop / "Paint Box.desktop"
        self.assertEqual(launcher.read_text(), ENTRY)
        self.assertTrue(os.access(launcher, os.X_OK))

    def test_the_users_own_entry_wins_over_the_systems(self):
        (self.own / "applications" / "paint-box.desktop").write_text(ENTRY.replace("Name=Paint Box", "Name=Mine"))
        self.assertEqual(self.shortcut("paint-box").returncode, 0)
        self.assertEqual([file.name for file in self.desktop.iterdir()], ["Mine.desktop"])

    def test_a_name_and_icon_the_user_chose_go_into_the_program_only(self):
        result = self.shortcut("paint-box", "Dad's \\ & Art", "/pictures/own icon.png")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = (self.desktop / "Dad's \\ & Art.desktop").read_text().splitlines()
        self.assertIn("Name=Dad's \\ & Art", lines)
        self.assertIn("Icon=/pictures/own icon.png", lines)
        self.assertNotIn("Name=Paint Box", lines)
        self.assertNotIn("Icon=paint-box", lines)
        self.assertIn("Name[nb]=Malerskrin", lines)
        self.assertIn("Name=New Picture", lines)
        self.assertIn("Icon=document-new", lines)

    def test_a_second_shortcut_keeps_the_first(self):
        self.shortcut("paint-box")
        self.assertEqual(self.shortcut("paint-box").returncode, 0)
        self.assertEqual(sorted(file.name for file in self.desktop.iterdir()),
                         ["Paint Box 2.desktop", "Paint Box.desktop"])

    def test_a_slash_in_the_name_stays_out_of_the_file_name(self):
        self.assertEqual(self.shortcut("paint-box", "In/Out").returncode, 0)
        self.assertEqual([file.name for file in self.desktop.iterdir()], ["In-Out.desktop"])

    def test_a_program_without_an_entry_is_refused_and_leaves_nothing(self):
        result = self.shortcut("no-such-program")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no-such-program", result.stderr)
        self.assertFalse(self.desktop.exists() and any(self.desktop.iterdir()))


class SendToDesktop(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.home = self.root / "home"
        self.desktop = self.home / "Desktop"
        self.files = self.root / "files"
        self.files.mkdir()
        self.environment = {"PATH": os.environ["PATH"], "HOME": str(self.home)}

    def tearDown(self):
        self.scratch.cleanup()

    def send(self, *files):
        return subprocess.run([str(ACTION), "send-to-desktop", *map(str, files)], env=self.environment,
                              capture_output=True, text=True, timeout=20)

    def test_a_file_and_a_folder_get_links_under_their_own_names(self):
        letter = self.files / "letter.txt"
        letter.write_text("hi")
        (self.files / "Photos").mkdir()
        result = self.send(letter, self.files / "Photos")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(os.readlink(self.desktop / "letter.txt"), str(letter))
        self.assertEqual(os.readlink(self.desktop / "Photos"), str(self.files / "Photos"))

    def test_a_taken_name_gets_a_number_before_the_extension(self):
        letter = self.files / "letter.txt"
        letter.write_text("hi")
        self.send(letter)
        self.assertEqual(self.send(letter).returncode, 0)
        self.assertEqual(os.readlink(self.desktop / "letter 2.txt"), str(letter))

    def test_a_program_entry_becomes_a_launcher_named_like_the_program(self):
        entry = self.files / "paint-box.desktop"
        entry.write_text(ENTRY)
        result = self.send(entry)
        self.assertEqual(result.returncode, 0, result.stderr)
        launcher = self.desktop / "Paint Box.desktop"
        self.assertFalse(launcher.is_symlink())
        self.assertEqual(launcher.read_text(), ENTRY)
        self.assertTrue(os.access(launcher, os.X_OK))

    def test_a_desktop_file_that_is_no_program_gets_a_link(self):
        link = self.files / "site.desktop"
        link.write_text("[Desktop Entry]\nType=Link\nName=Site\nURL=https://example.org\n")
        self.assertEqual(self.send(link).returncode, 0)
        self.assertEqual(os.readlink(self.desktop / "site.desktop"), str(link))

    def test_the_folder_the_desktop_shows_is_used(self):
        settings = self.root / "shell.json"
        settings.write_text('{"config": {"desktop_files": {"folder": "~/Elsewhere"}}}')
        self.environment["DESKTOP_SHELL_SETTINGS"] = str(settings)
        letter = self.files / "letter.txt"
        letter.write_text("hi")
        self.assertEqual(self.send(letter).returncode, 0)
        self.assertTrue((self.home / "Elsewhere" / "letter.txt").is_symlink())

    def test_a_file_already_on_the_desktop_is_left_alone(self):
        self.desktop.mkdir(parents=True)
        note = self.desktop / "note.txt"
        note.write_text("hi")
        self.assertEqual(self.send(note).returncode, 0)
        self.assertEqual([file.name for file in self.desktop.iterdir()], ["note.txt"])

    def test_a_missing_file_is_refused_and_leaves_nothing(self):
        result = self.send(self.files / "gone.txt")
        self.assertEqual(result.returncode, 1)
        self.assertIn("gone.txt", result.stderr)
        self.assertFalse(self.desktop.exists() and any(self.desktop.iterdir()))


if __name__ == "__main__":
    unittest.main()
