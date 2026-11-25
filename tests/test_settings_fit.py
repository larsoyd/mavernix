"""Every pane of the Settings window fits its fixed width, drawn as the session
draws it, with the words of the plain config and of each look that brings its own."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

SETTINGS_PROGRAM = os.environ["SETTINGS_PROGRAM"]
SESSION_DIR = Path(os.environ["SESSION_SOURCE_DIR"])
CONFIG_DIR = Path(os.environ["CONFIG_SOURCE_DIR"])
THEMES_DIR = Path(os.environ["THEMES_SOURCE_DIR"])
LOOKS_DIR = Path(os.environ["LOOKS_SOURCE_DIR"])


def load(path):
    with open(path, "rb") as file:
        return tomllib.load(file)


class SettingsFit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        folder = Path(cls.folder.name)
        data = folder / "share" / "mavernix"
        data.mkdir(parents=True)
        (data / "config").symlink_to(CONFIG_DIR)
        (data / "themes").symlink_to(THEMES_DIR)
        paths = (SESSION_DIR / "paths.py.in").read_text()
        paths = paths.replace("@LIBRARY_DIR@", str(folder / "lib")).replace("@DATA_DIR@", str(data))
        paths = paths.replace("@QT_PLUGIN_DIR@", str(folder / "plugins"))
        (folder / "paths.py").write_text(paths)
        # the session styles Qt programs with Kvantum and the theme it makes
        app_theme = folder / "app-theme"
        made = subprocess.run([sys.executable, str(SESSION_DIR / "make_app_theme.py"), str(THEMES_DIR / "plain"),
                               str(app_theme)], env={**os.environ, "PYTHONPATH": str(folder), "HOME": str(folder)},
                              stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
        if made.returncode != 0:
            raise AssertionError(f"make_app_theme.py failed:\n{made.stderr}")
        kvantum = folder / "config" / "Kvantum"
        shutil.copytree(app_theme / "share" / "themes" / "DesktopTheme" / "Kvantum", kvantum / "DesktopTheme")
        (kvantum / "kvantum.kvconfig").write_text("[General]\ntheme=DesktopTheme\n")

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def check_fit(self, words):
        folder = Path(self.folder.name)
        config = load(CONFIG_DIR / "plain.toml")
        config["settings"].setdefault("words", {}).update(words)
        settings = folder / "settings.json"
        settings.write_text(json.dumps({
            "config": config,
            "default_config": config,
            "theme": load(THEMES_DIR / "plain" / "theme.toml"),
            "theme_directory": str(THEMES_DIR / "plain"),
            "user_choices_file": str(folder / "choices.json"),
        }))
        # qt6ct, the session's platform theme, crashes in the menu bar without a display
        dropped = ("DBUS_SESSION_BUS_ADDRESS", "WAYLAND_DISPLAY", "DISPLAY", "QT_QPA_PLATFORMTHEME")
        environment = {key: value for key, value in os.environ.items() if key not in dropped}
        # no way to the session's own sound server or buses
        runtime = folder / "runtime"
        runtime.mkdir(mode=0o700, exist_ok=True)
        environment.update({"DESKTOP_SHELL_SETTINGS": str(settings), "QT_QPA_PLATFORM": "offscreen",
                            "QT_STYLE_OVERRIDE": "kvantum", "HOME": str(folder),
                            "XDG_CONFIG_HOME": str(folder / "config"), "XDG_DATA_HOME": str(folder / "data"),
                            "XDG_CACHE_HOME": str(folder / "cache"), "XDG_RUNTIME_DIR": str(runtime)})
        result = subprocess.run([SETTINGS_PROGRAM, "--check-fit"], env=environment, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=60)
        found = "\n".join(line for line in result.stderr.splitlines() if line.startswith("[settings]"))
        self.assertIn("[settings] check_fit", found, f"exit={result.returncode}\n{result.stderr}")
        self.assertEqual(result.returncode, 0, found)

    def test_plain_words_fit(self):
        self.check_fit({})

    def test_each_looks_words_fit(self):
        looks = [look for look in sorted(LOOKS_DIR.glob("*/*.toml")) if load(look).get("settings", {}).get("words")]
        self.assertTrue(looks, f"no look under {LOOKS_DIR} brings words")
        for look in looks:
            with self.subTest(look=look.stem):
                self.check_fit(load(look)["settings"]["words"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], "-v"])
