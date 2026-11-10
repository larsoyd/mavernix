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


if __name__ == "__main__":
    unittest.main()
