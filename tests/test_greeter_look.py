import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(os.environ["SESSION_SOURCE_DIR"]) / "export_greeter_look.py"
JPG = b"\xff\xd8\xff\xe0" + b"\0" * 64
PNG = b"\x89PNG\r\n\x1a\n" + b"\0" * 64
XCUR = b"Xcur" + b"\0" * 64

SESSION_INI = """[input]
xkb_layout = no
xkb_model = pc105
cursor_theme = SeaLion

[output:DP-1]
scale = 1
mode = 3440x1440@180000
position = 0,0

[output:HDMI-A-1]
scale = 0.75
position = 6000,0
transform = 90
"""


class Export(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.theme = self.root / "themes" / "sea-lion"
        (self.theme / "fonts").mkdir(parents=True)
        (self.theme / "fonts" / "Face.ttc").write_bytes(b"ttcf" + b"\0" * 32)
        (self.theme / "fonts" / "notes.txt").write_text("not a font")
        (self.theme / "wallpapers").mkdir()
        (self.theme / "wallpapers" / "Wave.jpg").write_bytes(JPG)
        cursors = self.theme / "share" / "icons" / "SeaLion" / "cursors"
        cursors.mkdir(parents=True)
        (cursors / "left_ptr").write_bytes(XCUR)
        (cursors / "default").symlink_to("left_ptr")
        self.picture = self.root / "pic.png"
        self.picture.write_bytes(PNG)
        self.face = self.root / "face.png"
        self.face.write_bytes(PNG)
        self.inbox = self.root / "inbox"
        self.inbox.mkdir()
        self.settings = self.root / "shell.json"
        self.ini = self.root / "session.ini"
        self.ini.write_text(SESSION_INI)
        self.write_settings()

    def write_settings(self, picture=None, face=None):
        self.settings.write_text(json.dumps({
            "theme": {"theme": {"name": "Sea Lion"}, "fonts": {"interface": {"family": ".Lucida Grande UI", "size": 13}},
                      "cursors": {"theme": "SeaLion", "size": 48},
                      "desktop": {"background": {"image": "wallpapers/Wave.jpg", "fit": "cover"}}},
            "theme_directory": str(self.theme),
            "config": {"desktop": {"picture": str(picture if picture is not None else self.picture)},
                       "display": {"main_screen": "DP-1"}},
            "user": {"name": "ada", "picture": str(face if face is not None else self.face)},
        }))

    def run_export(self, *extra):
        return subprocess.run([sys.executable, SCRIPT, self.settings, self.ini, self.inbox, *extra],
                              capture_output=True, text=True, timeout=60)

    def snapshot(self):
        return json.loads((self.inbox / "snapshot" / "theme.json").read_text())

    def test_snapshot_holds_the_look(self):
        r = self.run_export("--drm-driver", "nvidia")
        self.assertEqual(r.returncode, 0, r.stderr)
        t = self.snapshot()
        self.assertEqual((t["schema"], t["version"], t["id"], t["name"]), ("mavernix-greeter.theme", 1, "sea-lion", "Sea Lion"))
        self.assertEqual(t["fonts"], {"family": ".Lucida Grande UI"})
        self.assertEqual(t["cursor"], {"theme": "SeaLion", "size": 48})
        self.assertEqual(t["background"]["image"], "background.png")
        self.assertEqual(t["keyboard"], {"layout": "no", "model": "pc105"})
        self.assertEqual(t["display"]["main_output"], "DP-1")
        self.assertEqual(t["display"]["drm_driver"], "nvidia")
        self.assertEqual(t["display"]["outputs"], [
            {"name": "DP-1", "mode": "3440x1440@180000", "position": "0,0", "scale": 1},
            {"name": "HDMI-A-1", "position": "6000,0", "scale": 0.75, "transform": "90"}])
        self.assertEqual(t["accounts"], {"ada": {"avatar": "accounts/ada.png"}})
        snap = self.inbox / "snapshot"
        self.assertEqual((snap / "background.png").read_bytes(), PNG)
        self.assertTrue((snap / "fonts" / "Face.ttc").is_file())
        self.assertFalse((snap / "fonts" / "notes.txt").exists())
        self.assertEqual((snap / "cursors/SeaLion/cursors/left_ptr").read_bytes(), XCUR)
        self.assertEqual(os.readlink(snap / "cursors/SeaLion/cursors/default"), "left_ptr")
        self.assertIn("Name=SeaLion", (snap / "cursors/SeaLion/index.theme").read_text())
        self.assertTrue((self.inbox / "ready").is_file())

    def test_no_inbox_writes_nothing(self):
        self.inbox.rmdir()
        r = self.run_export()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("skipped", r.stderr)
        self.assertFalse(self.inbox.exists())

    def test_read_only_inbox_writes_nothing(self):
        self.inbox.chmod(0o500)
        try:
            r = self.run_export()
        finally:
            self.inbox.chmod(0o700)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("skipped", r.stderr)
        self.assertEqual(list(self.inbox.iterdir()), [])

    def test_unchanged_look_not_sent_again(self):
        self.run_export()
        (self.inbox / "ready").unlink()
        r = self.run_export()
        self.assertIn("unchanged", r.stderr)
        self.assertFalse((self.inbox / "ready").exists())
        other = self.root / "other.jpg"
        other.write_bytes(JPG)
        self.write_settings(picture=other)
        self.run_export()
        self.assertTrue((self.inbox / "ready").exists())
        self.assertEqual(self.snapshot()["background"]["image"], "background.jpg")

    def test_the_users_current_choice_wins_over_the_login_settings(self):
        chosen = self.root / "chosen.jpg"
        chosen.write_bytes(JPG)
        choices = self.root / "choices.json"
        choices.write_text(json.dumps({"desktop": {"picture": str(chosen)}}))
        data = json.loads(self.settings.read_text())
        data["user_choices_file"] = str(choices)
        self.settings.write_text(json.dumps(data))
        self.run_export()
        self.assertEqual(self.snapshot()["background"]["image"], "background.jpg")
        self.assertEqual((self.inbox / "snapshot" / "background.jpg").read_bytes(), JPG)

    def test_a_chosen_picture_inside_the_theme(self):
        choices = self.root / "choices.json"
        (self.theme / "wallpapers" / "Other.png").write_bytes(PNG)
        self.write_settings(picture=self.root / "gone.jpg")
        choices.write_text(json.dumps({"desktop": {"picture": "wallpapers/Other.png"}}))
        data = json.loads(self.settings.read_text())
        data["user_choices_file"] = str(choices)
        self.settings.write_text(json.dumps(data))
        self.run_export()
        self.assertEqual(self.snapshot()["background"]["image"], "background.png")

    def test_theme_picture_when_desktop_picture_missing(self):
        self.write_settings(picture=self.root / "gone.png")
        self.run_export()
        self.assertEqual(self.snapshot()["background"]["image"], "background.jpg")

    def test_picture_anyone_can_read_left_in_place(self):
        self.root.chmod(0o755)
        self.face.chmod(0o644)
        self.run_export()
        self.assertNotIn("ada", self.snapshot()["accounts"])
        self.face.chmod(0o600)
        self.run_export()
        self.assertIn("ada", self.snapshot()["accounts"])

    def test_only_one_snapshot_kept(self):
        self.run_export()
        self.write_settings(picture=self.root / "gone.png")
        self.run_export()
        self.assertEqual(sorted(p.name for p in self.inbox.iterdir()), ["ready", "snapshot", "stamp"])


if __name__ == "__main__":
    unittest.main()
