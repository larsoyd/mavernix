"""The session's decisions at login (src/session): for each shipped desktop
config, the compositor's settings and the shell's are written without error and
name the right theme; the desktop that starts when none is named is the right
one; and themes are held to the format and may build on one another.

Run by `meson test`, which says where the sources are (tests/meson.build).
"""

import configparser
import contextlib
import io
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
import unittest
import unittest.mock
from pathlib import Path

SESSION_SOURCE_DIR = Path(os.environ["SESSION_SOURCE_DIR"])
CONFIG_DIR = Path(os.environ["CONFIG_SOURCE_DIR"])
THEMES_DIR = Path(os.environ["THEMES_SOURCE_DIR"])
EXTENSIONS_DIR = Path(os.environ["EXTENSIONS_SOURCE_DIR"])
THEME_DOCS_DIR = Path(os.environ["THEME_DOCS_DIR"])
LOOKS_DIR = Path(os.environ["LOOKS_SOURCE_DIR"])

# The desktops whose themes are shipped.
DESKTOPS = ["plain"]


class SessionTestCase(unittest.TestCase):
    """Runs the session's scripts as if the desktop were installed, from the
    sources: a paths module of its own points at the shipped configs and
    themes, and a home of its own keeps the user's files out of it."""

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        folder = Path(self.folder.name)
        # Run from inside a desktop session, the scripts would ask its compositor about its screens.
        without_compositor = unittest.mock.patch.dict(os.environ)
        without_compositor.start()
        self.addCleanup(without_compositor.stop)
        os.environ.pop("WAYFIRE_SOCKET", None)

        # The installed data folder, made of the sources' configs and themes.
        data = folder / "share" / "mavernix"
        data.mkdir(parents=True)
        (data / "config").symlink_to(CONFIG_DIR)
        (data / "themes").symlink_to(THEMES_DIR)
        (data / "extensions").symlink_to(EXTENSIONS_DIR)
        subprocess.run([sys.executable, str(SESSION_SOURCE_DIR / "check_theme.py"), "--collect",
                        str(THEME_DOCS_DIR), str(data / "theme-keys.json")], check=True)
        paths = (SESSION_SOURCE_DIR / "paths.py.in").read_text()
        paths = paths.replace("@LIBRARY_DIR@", str(folder / "lib" / "mavernix"))
        paths = paths.replace("@DATA_DIR@", str(data))
        (folder / "paths.py").write_text(paths)

        self.home = folder / "home"
        self.environment = {
            **os.environ,
            "HOME": str(self.home),
            "XDG_CONFIG_HOME": str(self.home / ".config"),
            "XDG_DATA_HOME": str(self.home / ".local" / "share"),
            "XDG_RUNTIME_DIR": str(self.home / "runtime"),
            "PYTHONPATH": str(folder),
        }

    def tearDown(self):
        self.folder.cleanup()

    def run_script(self, script, *arguments, may_fail=False, environment=None):
        result = subprocess.run([sys.executable, str(SESSION_SOURCE_DIR / script), *map(str, arguments)],
                                env={**self.environment, **(environment or {})}, capture_output=True, text=True)
        if may_fail:
            return result
        self.assertEqual(result.returncode, 0, f"{script} failed:\n{result.stderr}")
        return result.stdout

    def user_theme(self, name, text):
        """A theme of the user's own, with theme.toml holding `text`."""
        folder = self.home / ".local" / "share" / "mavernix" / "themes" / name
        folder.mkdir(parents=True)
        (folder / "theme.toml").write_text(text)
        return folder


class TestWrittenSettings(SessionTestCase):
    def test_compositor_settings(self):
        for desktop in DESKTOPS:
            with self.subTest(desktop=desktop):
                written = self.home / f"{desktop}.ini"
                self.run_script("make_wayfire_config.py", CONFIG_DIR / f"{desktop}.toml", written)
                settings = configparser.ConfigParser(interpolation=None, strict=False)
                settings.read(written)
                self.assertIn("themed-decoration", settings["core"]["plugins"].split())
                self.assertEqual(Path(settings["themed-decoration"]["theme"]).name, desktop)

    def test_the_users_own_wayfire_settings_come_last(self):
        own = self.home / ".config" / "mavernix" / "wayfire.ini"
        own.parent.mkdir(parents=True)
        own.write_text("[workarounds]\ndisable_primary_selection = true\n")
        written = self.home / "plain.ini"
        self.run_script("make_wayfire_config.py", CONFIG_DIR / "plain.toml", written)
        text = written.read_text()
        self.assertIn("disable_primary_selection = false", text)
        self.assertTrue(text.rstrip().endswith("disable_primary_selection = true"))

    def test_the_users_own_plugins_are_added_to_the_list(self):
        own = self.home / ".config" / "mavernix" / "wayfire.ini"
        own.parent.mkdir(parents=True)
        own.write_text("[core]\nplugins = oswitch move\n\n[oswitch]\nnext_output_with_win = <shift> <super> KEY_RIGHT\n")
        written = self.home / "plain.ini"
        self.run_script("make_wayfire_config.py", CONFIG_DIR / "plain.toml", written)
        settings = configparser.ConfigParser(interpolation=None, strict=False)
        settings.read(written)
        plugins = settings["core"]["plugins"].split()
        self.assertIn("themed-decoration", plugins)
        self.assertIn("oswitch", plugins)
        self.assertEqual(plugins.count("move"), 1)
        self.assertEqual(written.read_text().count("plugins ="), 1)
        self.assertEqual(settings["oswitch"]["next_output_with_win"], "<shift> <super> KEY_RIGHT")

    def test_shell_settings(self):
        for desktop in DESKTOPS:
            with self.subTest(desktop=desktop):
                written = self.home / f"{desktop}.json"
                self.run_script("make_shell_config.py", CONFIG_DIR / f"{desktop}.toml", written)
                settings = json.loads(written.read_text())
                self.assertEqual(settings["desktop"], desktop)
                self.assertEqual(Path(settings["theme_directory"]).name, desktop)
                self.assertIn("window", settings["theme"])

    def test_a_half_installed_extension_is_not_taken_for_one(self):
        folder = self.home / ".config" / "mavernix" / "extensions"
        for name in ("echo", ".echo-old"):
            (folder / name).mkdir(parents=True)
            (folder / name / "extension.toml").write_text('title = "Echo"\n')
        written = self.home / "plain.json"
        self.run_script("make_shell_config.py", CONFIG_DIR / "plain.toml", written)
        self.assertEqual(sorted(json.loads(written.read_text())["extensions"]),
                         ["borderless-maximized", "echo", "focus-follows-mouse"])


class TestRefreshedExtensions(SessionTestCase):
    """An extension installed while the desktop runs (refresh_extensions.py)."""

    def setUp(self):
        super().setUp()
        # only what a test adds, the shipped extensions would be listed too
        shipped = Path(self.folder.name) / "share" / "mavernix" / "extensions"
        shipped.unlink()
        shipped.mkdir()
        self.settings_file = self.home / "state" / "shell.json"
        self.run_script("make_shell_config.py", CONFIG_DIR / "plain.toml", self.settings_file)
        self.settings = json.loads(self.settings_file.read_text())

        # A shell of its own, which notes what it is asked and whether the
        # list was written by then.
        shell_bin = Path(self.folder.name) / "bin"
        shell_bin.mkdir()
        self.asked_shell = Path(self.folder.name) / "asked-shell"
        shell = shell_bin / "qs"
        shell.write_text(f"""#!/bin/sh
listed=no; [ -f {self.settings["extensions_file"]} ] && listed=yes
echo "$* (list written: $listed)" >> {self.asked_shell}
""")
        shell.chmod(0o755)
        self.environment["PATH"] = f"{shell_bin}:{self.environment['PATH']}"

    def add_extension(self, name):
        folder = self.home / ".config" / "mavernix" / "extensions" / name
        folder.mkdir(parents=True)
        (folder / "extension.toml").write_text(f'title = "{name}"\n')

    def found(self):
        return list(json.loads(Path(self.settings["extensions_file"]).read_text())["extensions"])

    def asked(self):
        """What the shell was asked, one line for each time, without the shell's folder."""
        if not self.asked_shell.exists():
            return []
        return [line.split(" ipc ", 1)[1] for line in self.asked_shell.read_text().splitlines()]

    def test_the_list_beside_the_settings_follows_the_folder(self):
        self.assertEqual(Path(self.settings["extensions_file"]), self.settings_file.with_name("extensions.json"))
        self.run_script("refresh_extensions.py", self.settings_file)
        self.assertEqual(self.found(), [])
        self.add_extension("echo")
        self.run_script("refresh_extensions.py", self.settings_file)
        self.assertEqual(self.found(), ["echo"])
        self.assertEqual(self.asked(), [])

    def test_an_update_is_loaded_through_a_new_link(self):
        # The shell keeps code by its path; an update must come by a new one.
        def load_directory():
            self.run_script("refresh_extensions.py", self.settings_file)
            found = json.loads(Path(self.settings["extensions_file"]).read_text())["extensions"]
            return Path(found["echo"]["load_directory"])

        self.add_extension("echo")
        folder = self.home / ".config" / "mavernix" / "extensions" / "echo"
        first = load_directory()
        self.assertEqual(first.resolve(), folder.resolve())
        self.assertEqual(load_directory(), first)

        # As the App Store updates it: a new folder swapped in.
        staged = folder.with_name(".echo-new")
        staged.mkdir()
        (staged / "extension.toml").write_text('title = "echo"\nversion = "2"\n')
        shutil.rmtree(folder)
        staged.rename(folder)
        second = load_directory()
        self.assertNotEqual(second, first)
        self.assertEqual(second.resolve(), folder.resolve())

        # As someone working on it: a file changed in place.
        manifest = folder / "extension.toml"
        manifest.write_text(manifest.read_text() + 'description = "longer"\n')
        os.utime(manifest, ns=(1, manifest.stat().st_mtime_ns + 1))
        self.assertNotIn(load_directory(), (first, second))

    def test_enable_asks_the_shell_once_the_list_is_written(self):
        self.add_extension("echo")
        self.run_script("refresh_extensions.py", self.settings_file, "--enable", "echo")
        self.assertEqual(self.asked(), ["call extensions switchOn echo (list written: yes)"])

    def test_the_users_choices_are_left_to_the_shell(self):
        self.add_extension("echo")
        self.run_script("refresh_extensions.py", self.settings_file, "--enable", "echo")
        self.assertFalse(Path(self.settings["user_choices_file"]).exists())

    def test_a_name_not_found_still_writes_the_list(self):
        self.add_extension("echo")
        result = self.run_script("refresh_extensions.py", self.settings_file, "--enable", "nothing", may_fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no extension called nothing", result.stderr)
        self.assertEqual(self.found(), ["echo"])
        self.assertEqual(self.asked(), [])

    def test_a_shell_that_fails_is_reported(self):
        (Path(self.folder.name) / "bin" / "qs").write_text("#!/bin/sh\necho 'no shell running' >&2\nexit 1\n")
        self.add_extension("echo")
        result = self.run_script("refresh_extensions.py", self.settings_file, "--enable", "echo", may_fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no shell running", result.stderr)
        self.assertEqual(self.found(), ["echo"])


class TestExtensionPlugins(SessionTestCase):
    def wayfire_settings(self, choices):
        (self.home / ".config" / "mavernix").mkdir(parents=True, exist_ok=True)
        (self.home / ".config" / "mavernix" / "plain.json").write_text(json.dumps(choices))
        written = self.home / "session.ini"
        self.run_script("make_wayfire_config.py", str(CONFIG_DIR / "plain.toml"), str(written))
        settings = configparser.ConfigParser(interpolation=None, strict=False)
        settings.read(written)
        return settings

    def test_off_loads_no_plugin(self):
        settings = self.wayfire_settings({})
        self.assertNotIn("focus-follows-mouse", settings["core"]["plugins"].split())
        self.assertNotIn("focus-follows-mouse", settings)

    def test_on_loads_its_plugin_with_the_default_settings(self):
        settings = self.wayfire_settings({"extensions": {"enabled": ["focus-follows-mouse"]}})
        self.assertIn("focus-follows-mouse", settings["core"]["plugins"].split())
        self.assertEqual(dict(settings["focus-follows-mouse"]), {"delay_ms": "100", "raise": "false"})

    def test_the_users_settings_reach_the_plugin(self):
        settings = self.wayfire_settings({"extensions": {
            "enabled": ["focus-follows-mouse"],
            "focus-follows-mouse": {"delay_ms": 250, "raise": True},
        }})
        self.assertEqual(dict(settings["focus-follows-mouse"]), {"delay_ms": "250", "raise": "true"})

    def test_a_plugin_with_no_settings_loads_with_an_empty_section(self):
        settings = self.wayfire_settings({"extensions": {"enabled": ["borderless-maximized"]}})
        self.assertIn("borderless-maximized", settings["core"]["plugins"].split())
        self.assertEqual(dict(settings["borderless-maximized"]), {})

    def gtk_marker(self, folder, *choices):
        marker = folder / "hide-maximized-titlebars"
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.touch()
        self.wayfire_settings(*choices)
        return marker.exists()

    def test_a_marker_left_by_a_crash_goes_when_the_extension_is_off(self):
        self.assertFalse(self.gtk_marker(self.home, {}))

    def test_the_marker_stays_while_the_extension_is_on(self):
        self.assertTrue(self.gtk_marker(self.home, {"extensions": {"enabled": ["borderless-maximized"]}}))

    def test_another_config_leaves_the_running_sessions_marker_alone(self):
        self.assertTrue(self.gtk_marker(self.home / "runtime" / "mavernix", {}))

    def test_an_extension_that_is_gone_loads_nothing(self):
        settings = self.wayfire_settings({"extensions": {"enabled": ["not-here"]}})
        self.assertNotIn("not-here", settings["core"]["plugins"].split())


class TestPickedDesktop(SessionTestCase):
    def picked(self):
        return self.run_script("pick_desktop.py").strip()

    def choose(self, name):
        chosen = self.home / ".config" / "mavernix" / "desktop"
        chosen.parent.mkdir(parents=True, exist_ok=True)
        chosen.write_text(name + "\n")

    def import_sea_lion(self):
        """What the Theme Installer leaves: Sea Lion's themes and its desktops.
        Any theme will do as the imported one; the plain theme is at hand."""
        data = self.home / ".local" / "share" / "mavernix"
        for theme in ("sea-lion-dark", "sea-lion"):
            shutil.copytree(THEMES_DIR / "plain", data / "themes" / theme)
        (data / "config").mkdir(parents=True)
        for desktop in ("sea-lion-dark", "sea-lion"):
            shutil.copyfile(LOOKS_DIR / "sea-lion" / f"{desktop}.toml", data / "config" / f"{desktop}.toml")

    def test_plain_before_anything_is_imported(self):
        self.assertEqual(self.picked(), "plain")

    def test_sea_lion_once_its_theme_is_imported(self):
        self.import_sea_lion()
        self.assertEqual(self.picked(), "sea-lion")

    def test_the_users_choice_comes_first(self):
        self.import_sea_lion()
        self.choose("plain")
        self.assertEqual(self.picked(), "plain")

    def test_a_choice_whose_theme_is_missing_is_passed_over(self):
        self.choose("sea-lion")
        self.assertEqual(self.picked(), "plain")
        self.choose("no-such-desktop")
        self.assertEqual(self.picked(), "plain")


class TestThemes(SessionTestCase):
    def check(self, theme_folder):
        return self.run_script("check_theme.py", theme_folder, may_fail=True)

    def test_plain_follows_the_format(self):
        result = self.check(THEMES_DIR / "plain")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_misspelt_key_is_named(self):
        folder = self.user_theme("misspelt", 'based_on = "plain"\n[menu]\nbackgrund = "#000000"\n')
        result = self.check(folder)
        self.assertEqual(result.returncode, 1)
        self.assertIn("menu.backgrund", result.stdout)

    def test_a_theme_built_on_another(self):
        folder = self.user_theme("darker", 'based_on = "plain"\n[menu]\nbackground = "#000000"\n'
                                 '[panel]\nbackground = { image = "images/panel.png" }\n')
        (folder / "images").mkdir()
        (folder / "images" / "trash-full.svg").write_text("<svg/>")
        config = self.home / "darker.toml"
        config.write_text((CONFIG_DIR / "plain.toml").read_text().replace('theme = "plain"', 'theme = "darker"'))

        written = self.home / "darker.json"
        self.run_script("make_shell_config.py", config, written)
        settings = json.loads(written.read_text())
        theme = settings["theme"]
        self.assertEqual(theme["menu"]["background"], "#000000")
        # A fill is taken whole: plain's colour does not stay beside the picture.
        self.assertEqual(theme["panel"]["background"], {"image": "images/panel.png"})
        self.assertIn("height", theme["panel"])

        joined = Path(settings["theme_directory"])
        self.assertEqual((joined / "images" / "trash-full.svg").read_text(), "<svg/>")
        self.assertTrue((joined / "images" / "app-grid.svg").is_file())

    def test_a_missing_icon_theme_is_replaced_by_an_installed_one(self):
        self.user_theme("odd-icons", 'based_on = "plain"\n[icons]\ntheme = "NoSuchIcons"\n')
        icons = self.home / "data-dir" / "icons" / "breeze"
        icons.mkdir(parents=True)
        (icons / "index.theme").write_text("[Icon Theme]\nName=Breeze\n")
        self.environment["XDG_DATA_DIRS"] = str(self.home / "data-dir")
        config = self.home / "odd-icons.toml"
        config.write_text((CONFIG_DIR / "plain.toml").read_text().replace('theme = "plain"', 'theme = "odd-icons"'))

        written = self.home / "odd-icons.json"
        self.run_script("make_shell_config.py", config, written)
        self.assertEqual(json.loads(written.read_text())["theme"]["icons"]["theme"], "breeze")


class TestWindowAnimations(SessionTestCase):
    def test_pop_is_our_plugins_and_can_be_shortened(self):
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        theme = {"animation": {"window_open": {"kind": "pop", "duration_ms": 200, "curve": "ease-out"}}}
        sections, plugins = make_wayfire_config.animation_sections(theme)
        self.assertIn("themed-open-close", plugins)
        self.assertIn("open_animation = pop", sections[0])
        # Not X11 menus, which Wayland menus are not either.
        self.assertIn('enabled_for = type equals "toplevel"', sections[0])
        self.assertIn(["[themed-open-close]", "pop_duration = 200ms circle"], sections)

        # A desktop's [window] quick_open_close_ms keeps the curve.
        make_wayfire_config.shorten_open_and_close(sections, 120)
        self.assertIn(["[themed-open-close]", "pop_duration = 120ms circle"], sections)


class TestUncoveredColour(SessionTestCase):
    def test_pushed_desktop_uncovers_the_lists_colour(self):
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        theme = {"animation": {"notification_list": {"kind": "push"}},
                 "notifications": {"list": {"background": {"gradient": ["#000000", "#336699", "#ffffff"]}}}}
        self.assertEqual(make_wayfire_config.uncovered_color_lines(theme),
                         ["background_color = 0.200 0.400 0.600 1.000"])
        theme["notifications"]["list"]["background"] = {"color": "#ff000080"}
        self.assertEqual(make_wayfire_config.uncovered_color_lines(theme),
                         ["background_color = 1.000 0.000 0.000 0.502"])
        # A list that slides in over the desktop uncovers nothing.
        theme["animation"]["notification_list"]["kind"] = "slide"
        self.assertEqual(make_wayfire_config.uncovered_color_lines(theme), [])


class TestScreenScale(SessionTestCase):
    def test_a_stretched_mode_is_less_dense(self):
        # A dense panel shown at half its width is drawn at once, not twice.
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        panel = {"eDP-1": {"width": 2560, "dpi": 227.0}}
        with unittest.mock.patch.object(make_wayfire_config, "connected_screens", return_value=panel):
            native = make_wayfire_config.display_sections({})
            stretched = make_wayfire_config.display_sections(
                {"display": {"screens": {"eDP-1": {"mode": "1280x800@60000"}}}})
        self.assertIn("scale = 2", native[0])
        self.assertIn("scale = 1", stretched[0])

    def test_a_size_the_screen_does_not_list_gets_its_timings(self):
        # Wayfire sets only a listed size on a real screen; any other is
        # added to the list from a modeline, here as `cvt -r 1920 1200 120`.
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        panel = {"eDP-1": {"width": 2560, "dpi": 227.0, "sizes": {"2560x1600"}}}

        def written(mode):
            with unittest.mock.patch.object(make_wayfire_config, "connected_screens", return_value=panel):
                return make_wayfire_config.display_sections({"display": {"screens": {"eDP-1": {"mode": mode}}}})[0]

        self.assertIn("custom_mode = 317.00 1920 1968 2000 2080 1200 1203 1209 1271 +hsync -vsync",
                      written("1920x1200@120000"))
        self.assertFalse(any(line.startswith("custom_mode") for line in written("2560x1600@120000")))


    def test_an_unplaced_screen_keeps_where_it_is(self):
        # With several screens, rewriting the config keeps the running
        # arrangement; the config's own position still wins.
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        screens = {"DP-1": {"width": 2560, "height": 1440, "dpi": 93.0},
                   "HDMI-A-1": {"width": 1920, "height": 1080, "dpi": 96.0}}
        current = {"DP-1": {"position": "0,0", "width": 2560}, "HDMI-A-1": {"position": "2560,300", "width": 1920}}
        with unittest.mock.patch.object(make_wayfire_config, "connected_screens", return_value=screens), \
                unittest.mock.patch.object(make_wayfire_config, "screen_positions", return_value=current):
            kept = make_wayfire_config.display_sections({})
            placed = make_wayfire_config.display_sections(
                {"display": {"screens": {"HDMI-A-1": {"position": "0,1440"}}}})
            # DP-1 at scale 2 is half as wide, and HDMI-A-1 would stand off by itself.
            rescaled = make_wayfire_config.display_sections({"display": {"screens": {"DP-1": {"scale": 2}}}})
        self.assertIn("position = 2560,300", kept[1])
        self.assertIn("position = 0,1440", placed[1])
        self.assertEqual(1, sum(line.startswith("position") for line in placed[1]))
        self.assertFalse(any(line.startswith("position") for section in rescaled for line in section))

    def test_placed_screens_touch_again_after_a_change_of_size(self):
        # DP-1 at scale 2 is half as wide: HDMI-A-1, placed beside it at its
        # old width, is moved back to its edge instead of standing apart.
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        screens = {"DP-1": {"width": 2560, "height": 1440, "dpi": 93.0},
                   "HDMI-A-1": {"width": 1920, "height": 1080, "dpi": 96.0}}
        placed = {"DP-1": {"position": "0,0"}, "HDMI-A-1": {"position": "2560,0"}}
        with unittest.mock.patch.object(make_wayfire_config, "connected_screens", return_value=screens), \
                unittest.mock.patch.object(make_wayfire_config, "screen_positions", return_value={}):
            same = make_wayfire_config.display_sections({"display": {"screens": placed}})
            placed["DP-1"]["scale"] = 2
            rescaled = make_wayfire_config.display_sections({"display": {"screens": placed}})
        self.assertIn("position = 2560,0", same[1])
        self.assertIn("position = 0,0", rescaled[0])
        self.assertIn("position = 1280,0", rescaled[1])


class TestRefreshDesktops(SessionTestCase):
    def test_an_imported_desktop_is_brought_up_to_date(self):
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import refresh_desktops
        finally:
            del sys.path[:2]
        folder = Path(self.folder.name)
        looks = folder / "looks"
        (looks / "lion").mkdir(parents=True)
        (looks / "lion" / "look.toml").write_text('desktops = ["lion", "lion-dark"]\n')
        (looks / "lion" / "lion.toml").write_text('theme = "lion"\nnew_word = "Zoom"\n')
        (looks / "lion" / "lion-dark.toml").write_text('theme = "lion-dark"\n')
        (looks / "other").mkdir()
        (looks / "other" / "look.toml").write_text('desktops = ["other"]\n')
        (looks / "other" / "other.toml").write_text('theme = "other"\n')
        configs = folder / "configs"
        configs.mkdir()
        # Imported before records were kept, and up to date: one stale, one not.
        (configs / "lion.toml").write_text('theme = "lion"\n')
        (configs / "lion-dark.toml").write_text('theme = "lion-dark"\n')

        self.assertEqual(refresh_desktops.refresh(looks, configs), ["lion"])
        self.assertIn("new_word", (configs / "lion.toml").read_text())
        # Without a record it may have been changed by hand, so it is kept.
        self.assertEqual((configs / "lion.toml.before-update").read_text(), 'theme = "lion"\n')
        # A look never imported is not added.
        self.assertFalse((configs / "other.toml").exists())

        # Next time, a copy as it was installed is replaced without a spare.
        (configs / "lion.toml.before-update").unlink()
        (looks / "lion" / "lion.toml").write_text('theme = "lion"\nnewer_word = "Zoom"\n')
        self.assertEqual(refresh_desktops.refresh(looks, configs), ["lion"])
        self.assertFalse((configs / "lion.toml.before-update").exists())
        self.assertEqual(refresh_desktops.refresh(looks, configs), [])


class TestSeaLionUpdates(unittest.TestCase):
    """Sea Lion's updates.toml carries tables to themes imported before they
    existed: the App Store's and the menus' fade. Each is a copy of what an
    import writes, so it must stay the same as make_theme's, and Sea Lion
    Dark's App Store table must have the same keys."""

    def setUp(self):
        sys.path.insert(0, str(LOOKS_DIR.parent))
        self.addCleanup(sys.path.remove, str(LOOKS_DIR.parent))
        import make_theme
        self.make_theme = make_theme
        with open(LOOKS_DIR / "sea-lion" / "updates.toml", "rb") as file:
            self.updates = {update["revision"]: update for update in tomllib.load(file)["update"]}

    def test_the_light_table_is_the_imports(self):
        made = tomllib.loads(self.make_theme.APP_STORE_TABLE + self.make_theme.APP_STORE_STAND_INS)
        self.assertEqual(self.updates[2]["themes"], ["sea-lion"])
        self.assertEqual(self.updates[2]["settings"]["app_store"], made["app_store"])

    def test_the_dark_table_has_the_same_keys(self):
        self.assertEqual(self.updates[3]["themes"], ["sea-lion-dark"])
        self.assertEqual(self.updates[3]["settings"]["app_store"].keys(), self.updates[2]["settings"]["app_store"].keys())
        self.assertEqual(self.updates[3]["settings"]["fonts"], self.updates[2]["settings"]["fonts"])

    def test_older_themes_get_the_imports_menu_fade(self):
        made = tomllib.loads(self.make_theme.MENU_CLOSE_TABLE)
        self.assertNotIn("themes", self.updates[5])
        self.assertEqual(self.updates[5]["settings"]["animation.menu_close"], made["animation"]["menu_close"])

    def test_older_light_themes_get_the_imports_panel_edge(self):
        self.assertEqual(self.updates[8]["themes"], ["sea-lion"])
        [correction] = self.updates[8]["corrections"]
        self.assertEqual((correction["table"], correction["key"]), ("panel", "separator"))
        self.assertEqual(correction["now"], [self.make_theme.PANEL_EDGE])


class TestSeaLionMusic(unittest.TestCase):
    """Bandstand's [music] table names pictures make_music_pictures cuts from
    iTunes' resource files, which the import must take from the installer;
    Sea Lion Dark writes ProKit's under the same names."""

    def setUp(self):
        sys.path.insert(0, str(LOOKS_DIR.parent))
        self.addCleanup(sys.path.remove, str(LOOKS_DIR.parent))
        import installer_image
        import make_dark_theme
        import make_theme
        self.installer_image = installer_image
        self.make_dark_theme = make_dark_theme
        self.make_theme = make_theme

    def pictures_named(self, table):
        names = set()
        for value in table.values():
            if isinstance(value, dict):
                names |= self.pictures_named(value)
            elif isinstance(value, str) and value.startswith("images/music/"):
                names.add(value.removeprefix("images/music/").removesuffix(".png"))
        return names

    def test_every_named_picture_is_cut(self):
        music = tomllib.loads(self.make_theme.MUSIC_TABLE)["music"]
        self.assertLessEqual(self.pictures_named(music), set(self.make_theme.MUSIC_PICTURES))

    def test_itunes_files_are_taken_and_read_by_position(self):
        taken = self.installer_image.needed_files(LOOKS_DIR / "sea-lion")["Essentials"]
        for name in self.make_theme.ITUNES_NORMAL_FILES + self.make_theme.ITUNES_DOUBLE_FILES:
            path = f"{self.make_theme.ITUNES_RESOURCES}/{name}.rsrc"
            with self.subTest(path=path):
                self.assertIn(path, taken)
                self.assertIn(path, self.installer_image.DECODED_BY_POSITION)

    def test_the_dark_pictures_have_light_names(self):
        dark = self.make_dark_theme
        written = {name + suffix for name in dark.MUSIC_GLYPHS for suffix in dark.MUSIC_GLYPH_STATES}
        written |= set(dark.MUSIC_PICTURES) - set(dark.MUSIC_DARK_ONLY)
        # the -inactive glyphs are the dark theme's own addition
        self.assertLessEqual({name for name in written if not name.endswith("-inactive")},
                             set(self.make_theme.MUSIC_PICTURES))


class TestSeaLionIcons(unittest.TestCase):
    """make_icons.py reads only what the import takes from the installer, so
    every icon it names must be in needed-files.txt."""

    def setUp(self):
        sys.path.insert(0, str(LOOKS_DIR.parent))
        self.addCleanup(sys.path.remove, str(LOOKS_DIR.parent))
        import installer_image
        import make_icons
        self.make_icons = make_icons
        self.taken = {path for files in installer_image.needed_files(LOOKS_DIR / "sea-lion").values()
                      for path in files}

    def test_every_source_is_taken(self):
        icons = self.make_icons
        sources = [source for _, source in [*icons.ICONS.values(), *icons.SIDE_BAR_ICONS.values()]]
        for source in sources + icons.BUNDLES:
            with self.subTest(source=source):
                if source.endswith(".icns"):
                    self.assertIn(source if "/" in source else f"{icons.SYSTEM_ICONS}/{source}", self.taken)
                else:
                    # The icon's file name is in the Info.plist, which only the installer has.
                    self.assertIn(f"{source}/Contents/Info.plist", self.taken)
                    resources = f"{source}/Contents/Resources/"
                    self.assertTrue(any(path.startswith(resources) and path.endswith(".icns") for path in self.taken))

    def file_type_icons(self, types, subclasses="", generic_icons="", icons="", program_icons=()):
        database = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, database)
        (database / "types").write_text("".join(f"{name}\n" for name in types))
        (database / "subclasses").write_text(subclasses)
        (database / "generic-icons").write_text(generic_icons)
        (database / "icons").write_text(icons)
        return self.make_icons.file_type_icons(database, set(program_icons))

    def test_a_type_mavericks_never_drew_gets_the_plain_document(self):
        word = "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        found = self.file_type_icons(
            [word, "application/x-unheard-of", "text/csv"],
            subclasses=f"{word} application/zip\ntext/csv text/plain\n",
            generic_icons=f"{word}:x-office-document\ntext/csv:x-office-spreadsheet\n")
        # being a zip file inside must not make it look like an archive
        self.assertEqual(found, {
            "application-vnd.openxmlformats-officedocument.wordprocessingml.document": "text-x-generic",
            "application-x-unheard-of": "text-x-generic",
            "text-csv": "text-x-generic",
        })

    def test_text_gets_textedits_document_however_it_is_reached(self):
        found = self.file_type_icons(
            ["text/x-python", "text/x-cython", "application/toml", "application/json", "application/x-subtitle"],
            subclasses="text/x-python application/x-executable\ntext/x-python text/x-cython\n"
                       "application/toml text/plain\napplication/x-subtitle text/plain\n",
            generic_icons="application/toml:text-x-generic\napplication/json:text-x-script\n")
        self.assertEqual(found, {name: "text-plain" for name in (
            "text-x-python", "text-x-cython", "application-toml", "application-json", "application-x-subtitle")})

    def test_a_family_takes_its_generic_icon(self):
        found = self.file_type_icons(
            ["application/vnd.efi.iso", "application/vnd.appimage", "application/x-zstd-compressed-tar",
             "font/ttf2", "image/webp", "audio/flac", "video/matroska"],
            generic_icons="application/vnd.efi.iso:media-optical\napplication/vnd.appimage:application-x-executable\n"
                          "application/x-zstd-compressed-tar:package-x-generic\nfont/ttf2:font-x-generic\n")
        self.assertEqual(found, {
            "application-vnd.efi.iso": "application-x-cd-image",
            "application-vnd.appimage": "application-x-executable",
            "application-x-zstd-compressed-tar": "package-x-generic",
            "font-ttf2": "font-x-generic",
            "image-webp": "image-x-generic",
            "audio-flac": "audio-x-generic",
            "video-matroska": "video-x-generic",
        })

    def test_every_target_is_an_icon_of_the_theme(self):
        targets = {*self.make_icons.GENERIC_FILE_ICONS.values(), *self.make_icons.MEDIA_FILE_ICONS.values(),
                   self.make_icons.TEXT_FILE_ICON, self.make_icons.ANY_FILE_ICON}
        for target in targets:
            with self.subTest(target=target):
                self.assertEqual(self.make_icons.ICONS[target][0], "mimetypes")

    def test_an_icon_the_theme_or_a_program_has_stays(self):
        found = self.file_type_icons(
            ["application/pdf", "application/vnd.oasis.opendocument.text", "application/x-lutris",
             "application/x-vectorcraft", "inode/mount-point", "x-content/video-dvd"],
            icons="application/x-vectorcraft:ai.storyteller.vectorcraft\n",
            program_icons=["application-x-lutris"])
        self.assertEqual(found, {"application-vnd.oasis.opendocument.text": "text-x-generic"})

    def test_the_links_stand_beside_their_target_in_every_size(self):
        theme = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, theme)
        for folder in ("16x16", "64x64"):
            (theme / folder / "mimetypes").mkdir(parents=True)
            (theme / folder / "mimetypes" / "text-plain.png").write_bytes(b"picture")
        (theme / "32x32" / "mimetypes").mkdir(parents=True)

        self.make_icons.write_file_type_links(theme, {"text-x-python": "text-plain"})

        for folder in ("16x16", "64x64"):
            link = theme / folder / "mimetypes" / "text-x-python.png"
            self.assertEqual(os.readlink(link), "text-plain.png")
            self.assertEqual(link.read_bytes(), b"picture")
        self.assertFalse(os.path.lexists(theme / "32x32" / "mimetypes" / "text-x-python.png"))


class TestTypefaceWidths(unittest.TestCase):
    """Apple's Lucida Grande has a negative average width, which leaves
    Chrome's find box with no room to type in."""

    def setUp(self):
        sys.path.insert(0, str(LOOKS_DIR.parent))
        self.addCleanup(sys.path.remove, str(LOOKS_DIR.parent))
        import make_theme
        self.make_theme = make_theme
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.folder = Path(folder.name)

    def face(self, base, average, advances):
        tables = {b"OS/2": struct.pack(">Hh", 2, average) + b"\1\2\3",
                  b"hhea": bytes(34) + struct.pack(">H", len(advances)),
                  b"hmtx": b"".join(struct.pack(">HH", width, 0) for width in advances)}
        records, body = b"", b""
        offset = base + 12 + 16 * len(tables)
        for tag, table in tables.items():
            records += tag + struct.pack(">LLL", 0, offset + len(body), len(table))
            body += table + bytes(-len(table) % 4)
        return b"\0\1\0\0" + struct.pack(">HHHH", len(tables), 0, 0, 0) + records + body

    def collection(self, *faces):
        start = 12 + 4 * len(faces)
        built, bases = b"", []
        for average, advances in faces:
            bases.append(start + len(built))
            built += self.face(bases[-1], average, advances)
        path = self.folder / "faces.ttc"
        path.write_bytes(b"ttcf\0\1\0\0" + struct.pack(f">L{len(faces)}L", len(faces), *bases) + built)
        return path

    def averages(self, path):
        data = path.read_bytes()
        count = struct.unpack_from(">L", data, 8)[0]
        found = []
        for base in struct.unpack_from(f">{count}L", data, 12):
            tag_at = data.index(b"OS/2", base)
            table_at = struct.unpack_from(">L", data, tag_at + 8)[0]
            found.append(struct.unpack_from(">h", data, table_at + 2)[0])
        return found

    def test_a_negative_average_becomes_the_mean_of_the_real_advances(self):
        path = self.collection((-1003, [1000, 0, 2000, 3000]), (-7, [600, 600]))
        self.make_theme.mend_average_widths(path)
        self.assertEqual(self.averages(path), [2000, 600])

    def test_a_sound_average_stays(self):
        path = self.collection((900, [1000, 2000]))
        self.make_theme.mend_average_widths(path)
        self.assertEqual(self.averages(path), [900])

    def test_the_table_checksum_follows_the_new_value(self):
        path = self.collection((-1003, [1000, 2000]))
        self.make_theme.mend_average_widths(path)
        data = path.read_bytes()
        tag_at = data.index(b"OS/2", 16)
        checksum, table_at, length = struct.unpack_from(">LLL", data, tag_at + 4)
        padded = data[table_at:table_at + length].ljust((length + 3) // 4 * 4, b"\0")
        self.assertEqual(checksum, sum(struct.unpack(f">{len(padded) // 4}L", padded)) & 0xffffffff)

    def test_faces_sharing_a_table_all_get_its_new_checksum(self):
        path = self.collection((-1003, [1000, 2000]), (-1003, [1000, 2000]))
        data = bytearray(path.read_bytes())
        first = data.index(b"OS/2", 16)
        second = data.index(b"OS/2", first + 4)
        struct.pack_into(">L", data, second + 8, struct.unpack_from(">L", data, first + 8)[0])
        path.write_bytes(data)
        self.make_theme.mend_average_widths(path)
        data = path.read_bytes()
        self.assertNotEqual(data[first + 4:first + 8], bytes(4))
        self.assertEqual(data[first + 4:first + 8], data[second + 4:second + 8])

    def test_the_import_mends_the_collection_it_copies(self):
        fonts = self.folder / "root" / "System" / "Library" / "Fonts"
        fonts.mkdir(parents=True)
        self.collection((-1003, [1000, 2000])).rename(fonts / "LucidaGrande.ttc")
        (fonts / "Helvetica.dfont").write_bytes(b"resources")
        self.make_theme.copy_typeface(self.folder / "root", self.folder / "theme")
        self.assertEqual(self.averages(self.folder / "theme" / "LucidaGrande.ttc"), [1500])


class TestThemeUpdates(unittest.TestCase):
    """The Theme Installer's updates.py: what imported themes get at login,
    and when a new import is offered."""

    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.folder)
        sys.path.insert(0, str(LOOKS_DIR.parent))
        self.addCleanup(sys.path.remove, str(LOOKS_DIR.parent))
        import updates
        self.updates = updates
        self.look = self.folder / "looks" / "lion"
        self.look.mkdir(parents=True)
        (self.look / "look.toml").write_text('themes = ["lion"]\n')
        self.themes = self.folder / "themes"
        (self.themes / "lion").mkdir(parents=True)

    def write_updates(self, text):
        (self.look / "updates.toml").write_text(text)

    def theme_text(self):
        return (self.themes / "lion" / "theme.toml").read_text()

    def test_settings_and_corrections_are_applied_once(self):
        self.write_updates('[[update]]\nrevision = 1\n'
                           '[update.settings."dock.grid"]\nshare = 0.8\n'
                           '[update.settings.fan]\nmost = 8\n'
                           '[[update.corrections]]\ntable = "dock.grid"\nkey = "width"\nwas = 96\nnow = 100\n')
        # From before revisions were kept; a comment after a header, too.
        (self.themes / "lion" / "theme.toml").write_text(
            '# made by hand\n[theme]\nname = "Lion"\n\n[dock.grid]  # the grid\nwidth = 96\n')

        self.assertEqual(self.updates.refresh(self.folder / "looks", self.themes), ["lion"])
        theme = tomllib.loads(self.theme_text())
        self.assertEqual(theme["dock"]["grid"], {"share": 0.8, "width": 100})
        self.assertEqual(theme["fan"], {"most": 8})
        self.assertEqual(theme["theme"]["updated_to"], 1)
        self.assertTrue(self.theme_text().startswith("# made by hand\n"))

        # What the user changes afterwards stays, even back to the old value.
        edited = self.theme_text().replace("width = 100", "width = 96").replace("share = 0.8\n", "")
        (self.themes / "lion" / "theme.toml").write_text(edited)
        self.assertEqual(self.updates.refresh(self.folder / "looks", self.themes), [])
        self.assertEqual(self.theme_text(), edited)

    def test_an_update_can_be_for_some_themes_only(self):
        (self.look / "look.toml").write_text('themes = ["lion", "lion-dark"]\n')
        (self.themes / "lion-dark").mkdir()
        self.write_updates('[[update]]\nrevision = 1\nthemes = ["lion"]\n[update.settings.window]\ncolor = "#ffffff"\n'
                           '[[update]]\nrevision = 2\nthemes = ["lion-dark"]\n[update.settings.window]\ncolor = "#202020"\n')
        for name in ("lion", "lion-dark"):
            (self.themes / name / "theme.toml").write_text('[theme]\nname = "Lion"\n')

        self.assertEqual(sorted(self.updates.refresh(self.folder / "looks", self.themes)), ["lion", "lion-dark"])
        light = tomllib.loads(self.theme_text())
        dark = tomllib.loads((self.themes / "lion-dark" / "theme.toml").read_text())
        self.assertEqual((light["window"]["color"], light["theme"]["updated_to"]), ("#ffffff", 2))
        self.assertEqual((dark["window"]["color"], dark["theme"]["updated_to"]), ("#202020", 2))

    def test_a_table_written_inline_is_left_alone(self):
        self.write_updates('[[update]]\nrevision = 1\n[update.settings."dock.grid"]\nshare = 0.8\n')
        old = '[theme]\nname = "Lion"\n[dock]\ngrid = { width = 96 }\n'
        (self.themes / "lion" / "theme.toml").write_text(old)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self.updates.refresh(self.folder / "looks", self.themes), [])
        self.assertEqual(self.theme_text(), old)

    def test_later_holds_until_an_update_brings_more(self):
        os.environ["XDG_CONFIG_HOME"] = str(self.folder / "config")
        self.addCleanup(os.environ.pop, "XDG_CONFIG_HOME")
        look = {"themes": ["lion"], "folder": self.look}
        (self.themes / "lion" / "theme.toml").write_text('[theme]\nname = "Lion"\n')
        self.write_updates('[[update]]\nrevision = 1\nimport_brings = ["An icon."]\n')
        self.assertTrue(self.updates.offers_import("lion", look, self.themes))

        self.updates.put_off({"lion": 1})
        self.assertFalse(self.updates.offers_import("lion", look, self.themes))
        # An update with settings only does not bring the offer back.
        self.write_updates('[[update]]\nrevision = 1\nimport_brings = ["An icon."]\n'
                           '[[update]]\nrevision = 2\n[update.settings.fan]\nmost = 8\n')
        self.assertFalse(self.updates.offers_import("lion", look, self.themes))
        # One that brings something new does.
        self.write_updates('[[update]]\nrevision = 1\nimport_brings = ["An icon."]\n'
                           '[[update]]\nrevision = 2\nimport_brings = ["A button."]\n')
        self.assertTrue(self.updates.offers_import("lion", look, self.themes))


class TestPointerSettings(SessionTestCase):
    def test_speed_and_acceleration_reach_the_compositor(self):
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        lines = make_wayfire_config.input_section(
            {"input": {"mouse_speed": -0.5, "mouse_acceleration": False, "touchpad_acceleration": True}}, {})
        self.assertIn("mouse_cursor_speed = -0.5", lines)
        self.assertIn("mouse_accel_profile = flat", lines)
        self.assertIn("touchpad_accel_profile = adaptive", lines)
        self.assertFalse(any(line.startswith("touchpad_cursor_speed") for line in lines))
        more = make_wayfire_config.input_section({"input": {"mouse_scroll_speed": 2.5, "left_handed": True}}, {})
        self.assertIn("mouse_scroll_speed = 2.5", more)
        self.assertIn("left_handed_mode = true", more)
        with self.assertRaises(SystemExit):
            make_wayfire_config.input_section({"input": {"mouse_speed": 2}}, {})


class TestGtkSettingsFile(SessionTestCase):
    def test_x11_gtk_programs_get_the_font_and_pointer(self):
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_app_theme
        finally:
            del sys.path[:2]
        theme = {"controls": {}, "fonts": {"interface": {"family": "Lucida Grande", "size": 13}},
                 "cursors": {"theme": "Aqua", "size": 32}}
        make_app_theme.write_gtk_settings(theme, self.home)
        written = (self.home / "xdg" / "gtk-3.0" / "settings.ini").read_text().splitlines()
        self.assertIn("gtk-font-name=Lucida Grande 9.75", written)
        self.assertIn("gtk-cursor-theme-name=Aqua", written)
        self.assertIn("gtk-cursor-theme-size=32", written)


class TestGameSettings(SessionTestCase):
    def setUp(self):
        super().setUp()
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        self.make = make_wayfire_config

    def test_variable_refresh_rate_per_screen(self):
        screens = {"DP-1": {"vrr": "always"}, "DP-2": {"vrr": "games"}, "HDMI-A-1": {}}
        config = {"display": {"screens": screens}}
        with unittest.mock.patch.object(self.make, "connected_screens", return_value={}):
            sections = {section[0]: section for section in self.make.display_sections(config)}
        self.assertIn("vrr = true", sections["[output:DP-1]"])
        self.assertNotIn("vrr = true", sections["[output:DP-2]"])
        self.assertNotIn("vrr = true", sections["[output:HDMI-A-1]"])
        self.assertEqual(self.make.game_vrr_section(config), (["[game-vrr]", "screens = DP-2"], ["game-vrr"]))
        self.assertEqual(self.make.game_vrr_section({}), (None, []))
        with self.assertRaises(SystemExit):
            self.make.game_vrr_section({"display": {"screens": {"DP-1": {"vrr": "sometimes"}}}})

    def test_screens_can_be_turned_off(self):
        config = {"display": {"screens": {"HDMI-A-1": {"enabled": False}}}}
        self.assertEqual(self.make.screens_off(config, ["eDP-1", "HDMI-A-1"]), {"HDMI-A-1"})
        # One screen always stays on.
        self.assertEqual(self.make.screens_off(config, ["HDMI-A-1"]), set())
        lid = {"power": {"lid_turns_off_screen": True}}
        with unittest.mock.patch.object(self.make, "lid_is_closed", return_value=True):
            self.assertEqual(self.make.screens_off(lid, ["eDP-1", "HDMI-A-1"]), {"eDP-1"})
            self.assertEqual(self.make.screens_off(lid, ["eDP-1"]), set())
            # Shut lid, external turned off by hand: the external one stays off.
            both = {**lid, **config}
            self.assertEqual(self.make.screens_off(both, ["eDP-1", "HDMI-A-1"]), {"HDMI-A-1"})
        with unittest.mock.patch.object(self.make, "lid_is_closed", return_value=False):
            self.assertEqual(self.make.screens_off(lid, ["eDP-1", "HDMI-A-1"]), set())
        screens = {"eDP-1": {"width": 1920, "height": 1080, "dpi": None, "sizes": {"1920x1080"}},
                   "HDMI-A-1": {"width": 1920, "height": 1080, "dpi": None, "sizes": {"1920x1080"}}}
        with unittest.mock.patch.object(self.make, "connected_screens", return_value=screens):
            sections = {section[0]: section for section in self.make.display_sections(config)}
        self.assertEqual(sections["[output:HDMI-A-1]"], ["[output:HDMI-A-1]", "mode = off"])
        self.assertNotIn("mode = off", sections["[output:eDP-1]"])
        # While the compositor shows only eDP-1 (HDMI-A-1 turned off), turning
        # HDMI-A-1 on again writes it on; a connector the compositor never
        # shows (a nested session's host screen) is no screen of ours.
        shown = {"eDP-1": {"width": 1920, "height": 1080}}
        with unittest.mock.patch.object(self.make, "connected_screens", return_value=screens), \
                unittest.mock.patch.object(self.make, "screen_modes", return_value=shown):
            on_again = {section[0]: section for section in self.make.display_sections({})}
            hidden_host = self.make.display_sections({"display": {"screens": {"eDP-1": {"enabled": False}}}})
        self.assertNotIn("mode = off", on_again["[output:HDMI-A-1]"])
        # eDP-1 is the only one shown, so it stays on.
        self.assertNotIn(["[output:eDP-1]", "mode = off"], hidden_host)

    def test_quit_can_close_the_whole_program(self):
        config = {"shortcuts": {"close_window": "<super> KEY_Q"}, "window": {"quit_closes_app": True}}
        self.assertEqual(self.make.with_quit_closing_app(config)["shortcuts"],
                         {"quit_app": "<super> KEY_Q", "close_window": ""})
        # Wayfire's own close key ("<super> KEY_Q | <alt> KEY_F4") must not come back.
        keys = self.make.compositor_key_sections(self.make.with_quit_closing_app(config))
        self.assertIn("close_top_view = none", keys["core"])
        own_key = {"shortcuts": {"close_window": "KEY_W", "quit_app": "KEY_Q"}, "window": {"quit_closes_app": True}}
        self.assertEqual(self.make.with_quit_closing_app(own_key)["shortcuts"], own_key["shortcuts"])
        off = {"shortcuts": {"close_window": "<super> KEY_Q"}}
        self.assertEqual(self.make.with_quit_closing_app(off)["shortcuts"], off["shortcuts"])
        command = self.make.shortcut_section(self.make.with_quit_closing_app(config), {})
        self.assertIn("binding_quit_app = <super> KEY_Q", command)

    def test_shake_sensitivity(self):
        self.assertEqual(self.make.shake_to_find_section({}), ["[shake-to-find]", "sensitivity = 3"])
        self.assertEqual(self.make.shake_to_find_section({"input": {"shake_sensitivity": 5}})[1], "sensitivity = 5")
        with self.assertRaises(SystemExit):
            self.make.shake_to_find_section({"input": {"shake_sensitivity": 9}})

    def test_x11_primary_and_middle_click_paste(self):
        self.assertEqual(self.make.x11_windows_section({}),
                         ["[x11-windows]", "games = true", "moves_need_button = true"])
        self.assertEqual(self.make.x11_windows_section({"window": {"borderless_full_screen": False}})[1],
                         "games = false")
        self.assertEqual(self.make.x11_windows_section({"window": {"x11_moves_need_button": False}})[2],
                         "moves_need_button = false")
        self.assertEqual(self.make.x11_settings_section({}),
                         ["[x11-settings]", "primary_screen = true", "dpi = false"])
        sharp = {"display": {"x11_primary": False, "sharp_x11": True}}
        self.assertEqual(self.make.x11_settings_section(sharp),
                         ["[x11-settings]", "primary_screen = false", "dpi = true"])
        self.assertIn("force_xwayland_scaling = true", self.make.workarounds_section(sharp))
        self.assertIn("force_xwayland_scaling = false", self.make.workarounds_section({}))
        self.assertIn("disable_primary_selection = false", self.make.workarounds_section({}))
        self.assertIn("disable_primary_selection = true",
                      self.make.workarounds_section({"input": {"middle_click_paste": False}}))
        with self.assertRaises(SystemExit):
            self.make.workarounds_section({"input": {"middle_click_paste": "no"}})

    def test_shake_to_find_loads_its_plugin_only_when_asked(self):
        self.assertEqual(self.make.shake_to_find_plugins({}), [])
        self.assertEqual(self.make.shake_to_find_plugins({"input": {"shake_to_find": True}}), ["shake-to-find"])
        with self.assertRaises(SystemExit):
            self.make.shake_to_find_plugins({"input": {"shake_to_find": "yes"}})


class TestGameKeys(SessionTestCase):
    def test_bare_function_keys_are_let_through_but_not_full_screens(self):
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
        finally:
            del sys.path[:2]
        section = make_wayfire_config.game_keys_section({
            "shortcuts": {
                "window_overview": "KEY_F3 | KEY_SCALE | <ctrl> KEY_UP",
                "show_desktop": "<super> KEY_F11",
                "app_grid": "KEY_F4",
                "fullscreen_window": "KEY_F11",
                "lock": "",
            },
            "commands": {"terminal": {"key": "KEY_F12", "run": ["foot"]}, "gone": None},
        })
        self.assertEqual(section, ["[game-keys]", "keys = KEY_F3 KEY_F4 KEY_F12",
                                   "programs = steam_app_* Minecraft*", "all_x11 = false"])
        own = make_wayfire_config.game_keys_section({"game_keys": {"games": ["osu!"], "all_x11": True}})
        self.assertEqual(own[2:], ["programs = osu!", "all_x11 = true"])
        with self.assertRaises(SystemExit):
            make_wayfire_config.game_keys_section({"game_keys": {"games": "steam_app_*"}})


class TestShellBlur(SessionTestCase):
    def setUp(self):
        super().setUp()
        sys.path[:0] = [self.folder.name, str(SESSION_SOURCE_DIR)]
        try:
            import make_wayfire_config
            self.generator = make_wayfire_config
        finally:
            del sys.path[:2]

    # The menu bar's translucency checkbox, and the Desktop pane's blur
    # checkboxes (blur.menu_bar and blur.dock_shelf).
    SWITCHED_ON = {"panel": {"translucent_menu_bar": True}}
    WANTED = {"menu_bar": True, "dock_shelf": True}

    def matcher(self, config, theme=None):
        section, plugins = self.generator.blur_section(config, theme or {})
        self.assertEqual(plugins, ["blur"] if section else [])
        return next((line for line in section if line.startswith("blur_by_default")), "")

    def test_blur_is_opt_in_and_targets_only_selected_shell_surfaces(self):
        self.assertEqual(self.generator.blur_section({}, {}), ([], []))
        for enabled, expected in (({"panel": True}, ['desktop-panel']),
                                  ({"dock": True}, ['mavernix-dock']),
                                  ({"panel": True, "dock": True}, ['desktop-panel', 'mavernix-dock'])):
            with self.subTest(enabled=enabled):
                section, plugins = self.generator.blur_section(
                    {"blur": {**enabled, **self.WANTED}, **self.SWITCHED_ON}, {})
                self.assertEqual(plugins, ["blur"])
                matcher = next(line for line in section if line.startswith("blur_by_default"))
                self.assertEqual(matcher, "blur_by_default = " + " | ".join(f'app_id is "{name}"' for name in expected))
                self.assertNotIn("toplevel", matcher)
                self.assertIn("toggle = none", section)
        self.assertEqual(self.generator.blur_section(
            {"blur": {"panel": False, **self.WANTED}, **self.SWITCHED_ON}, {"blur": {"panel": True}}), ([], []))

    def test_older_imports_blur_nothing_until_a_checkbox_is_on(self):
        # Sea Lion imports before 0.5.8 wrote panel = true and dock = true.
        older = {"blur": {"panel": True, "dock": True}}
        self.assertEqual(self.generator.blur_section(older, {}), ([], []))
        self.assertEqual(self.generator.blur_section({**older, **self.SWITCHED_ON}, {}), ([], []))

    def test_opaque_menu_bar_does_not_disable_dock_blur(self):
        matcher = self.matcher({"blur": {"panel": True, "dock": True, **self.WANTED},
                                "panel": {"translucent_menu_bar": False}})
        self.assertEqual(matcher, 'blur_by_default = app_id is "mavernix-dock"')

    def test_menu_bar_blur_is_independent_and_optional(self):
        for translucent, blurred, expected in ((True, False, False), (True, True, True),
                                                (False, True, False), (False, False, False)):
            with self.subTest(translucent=translucent, blurred=blurred):
                matcher = self.matcher({"panel": {"translucent_menu_bar": translucent},
                                        "blur": {"panel": True, "dock": True,
                                                 "menu_bar": blurred, "dock_shelf": True}})
                self.assertEqual('desktop-panel' in matcher, expected)
                self.assertIn('mavernix-dock', matcher)

    def test_dock_blur_is_its_own_checkbox(self):
        for wanted, expected in ((False, False), (True, True)):
            with self.subTest(wanted=wanted):
                matcher = self.matcher({"blur": {"dock": True, "dock_shelf": wanted}})
                self.assertEqual('mavernix-dock' in matcher, expected)

    def test_reflective_shelf_gets_dock_blur(self):
        config = {"dock": {"window_reflections": True}, "blur": {"dock": True, "dock_shelf": True}}
        lines, _ = self.generator.blur_section(config, {})
        self.assertIn('blur_by_default = app_id is "mavernix-dock-shelf"', lines)

    def test_strength_values_are_validated(self):
        for key, value in (("method", "bad"), ("iterations", -1), ("iterations", 2.5),
                           ("degrade", 0), ("saturation", 4), ("offset", float("nan"))):
            with self.subTest(key=key, value=value), self.assertRaises(SystemExit):
                self.generator.blur_section({"blur": {"panel": True, key: value, **self.WANTED}, **self.SWITCHED_ON}, {})


class TestKdeColours(SessionTestCase):
    def test_the_desktops_icon_theme_beats_the_users_kde_settings(self):
        # Without the lock, a theme chosen under Plasma in ~/.config wins here.
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", output)
        lines = (output / "xdg" / "kdeglobals").read_text().split("\n")
        # the key, not the group: kconfig copies a group's lock into the user's file on the next write
        self.assertEqual(lines[:2], ["[Icons]", "Theme[$i]=Adwaita"])
        scheme = (output / "share" / "color-schemes" / "DesktopTheme.colors").read_text()
        self.assertNotIn("Icons", scheme)
        self.assertNotIn("Theme[", scheme)

    def test_qt6ct_gets_the_themes_font_and_nothing_else_it_could_change(self):
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", output)
        settings = configparser.ConfigParser(interpolation=None)
        settings.read(output / "xdg" / "qt6ct" / "qt6ct.conf")
        self.assertEqual(settings["Fonts"]["general"], '"DejaVu Sans,9.75,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"')
        self.assertEqual(settings["Appearance"]["custom_palette"], "false")
        # Platform.FileDialog does nothing at all without a dialog from the theme
        self.assertEqual(settings["Appearance"]["standard_dialogs"], "xdgdesktopportal")
        self.assertEqual(settings["Appearance"]["icon_theme"], "Adwaita")

    def test_kdeglobals_carries_no_fonts(self):
        # nothing reads them without the kde platform theme, which recolours and rewires every program
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", output)
        text = (output / "xdg" / "kdeglobals").read_text()
        self.assertNotIn("[General]", text)
        self.assertNotIn("font[", text.lower())

    def test_a_theme_without_an_interface_font_writes_no_qt6ct_settings(self):
        theme = self.home / "themed"
        shutil.copytree(THEMES_DIR / "plain", theme)
        toml = theme / "theme.toml"
        toml.write_text(re.sub(r"^interface .*\n", "", toml.read_text(), count=1, flags=re.M))
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", theme, output)
        self.assertFalse((output / "xdg" / "qt6ct").exists())

    def session_functions(self, *names, setup="", qt_theme="yes", environment=None):
        """Functions of configure-desktop on their own, with the settings stubbed."""
        source = (SESSION_SOURCE_DIR / "configure-desktop").read_text()
        pattern = "|".join(names)
        functions = "".join(re.findall(rf"^(?:{pattern})\(\) \{{.*?^\}}\n", source, re.M | re.S))
        state = self.home / "state"
        state.mkdir(exist_ok=True)
        output = self.home / "app-theme"
        script = (f"setting() {{ echo; }}\nconfig_flag() {{ echo {qt_theme}; }}\n{functions}"
                  f"app_theme={shlex.quote(str(output))}\nstate_dir={shlex.quote(str(state))}\n{setup}\n")
        runs = {key: value for key, value in os.environ.items() if not key.startswith("DESKTOP_")}
        runs.update(self.environment_for_link(), **(environment or {}))
        return subprocess.run(["bash", "-c", script], check=True, env=runs, timeout=10, capture_output=True, text=True)

    def environment_for_link(self):
        return {"HOME": str(self.home), "XDG_CONFIG_HOME": str(self.home / ".config")}

    def desktop_environment(self, **options):
        self.session_functions("environment_line", "write_environment", setup="write_environment", **options)
        return (self.home / "state" / "desktop.env").read_text()

    def test_qt_takes_the_themes_font_through_qt6ct(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        self.assertIn("export QT_QPA_PLATFORMTHEME=qt6ct\n", self.desktop_environment())

    def test_no_qt6ct_settings_means_no_platform_theme(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        (self.home / "app-theme" / "xdg" / "qt6ct" / "qt6ct.conf").unlink()
        self.assertIn("unset QT_QPA_PLATFORMTHEME\n", self.desktop_environment())

    def test_a_user_who_turned_the_qt_theme_off_gets_no_platform_theme(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        self.assertIn("unset QT_QPA_PLATFORMTHEME\n", self.desktop_environment(qt_theme="no"))

    def link_settings(self, **options):
        return self.session_functions("link_qt6ct_settings", setup="link_qt6ct_settings", **options)

    def test_the_users_config_links_to_the_sessions_qt6ct_settings(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        self.link_settings()
        link = self.home / ".config" / "qt6ct" / "qt6ct.conf"
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.resolve(), (self.home / "app-theme" / "xdg" / "qt6ct" / "qt6ct.conf").resolve())

    def test_a_qt6ct_file_of_the_users_own_stays_and_is_named(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        own = self.home / ".config" / "qt6ct" / "qt6ct.conf"
        own.parent.mkdir(parents=True)
        own.write_text("mine")
        done = self.link_settings()
        self.assertEqual(own.read_text(), "mine")
        self.assertFalse(own.is_symlink())
        self.assertIn(str(own), done.stderr)

    def test_a_nested_desktop_leaves_the_real_sessions_link_alone(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        self.link_settings(environment={"DESKTOP_NESTED": "1"})
        self.assertFalse((self.home / ".config" / "qt6ct" / "qt6ct.conf").exists())

    def linked_mozilla_profile(self, **options):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        profile = self.home / ".config/thunderbird/abc"
        profile.mkdir(parents=True, exist_ok=True)
        (profile.parent / "profiles.ini").write_text("[Profile0]\nIsRelative=1\nPath=abc\n")
        setup = f"session_dir={shlex.quote(str(SESSION_SOURCE_DIR))}\nlink_mozilla_style"
        self.session_functions("link_mozilla_style", setup=setup, **options)
        return profile / "chrome/mavernix"

    def test_mozilla_profiles_link_the_written_theme(self):
        link = self.linked_mozilla_profile()
        self.assertTrue((link / "mozilla/titlebar.css").is_file())
        self.assertIn("#toolbar-menubar { visibility: collapse !important; }",
                      (link / "mozilla/global-menu.css").read_text())

    def test_mozilla_profiles_lose_the_link_when_gtk_is_left_alone(self):
        link = self.linked_mozilla_profile()
        self.linked_mozilla_profile(qt_theme="no")
        self.assertFalse(link.is_symlink())

    def test_a_nested_desktop_leaves_mozilla_profiles_alone(self):
        link = self.linked_mozilla_profile(environment={"DESKTOP_NESTED": "1"})
        self.assertFalse(link.exists())

    def test_a_link_left_by_an_earlier_theme_goes_when_there_is_no_font(self):
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", self.home / "app-theme")
        self.link_settings()
        (self.home / "app-theme" / "xdg" / "qt6ct" / "qt6ct.conf").unlink()
        self.link_settings()
        self.assertFalse((self.home / ".config" / "qt6ct" / "qt6ct.conf").is_symlink())

    def test_services_and_logins_carry_the_platform_theme(self):
        for script in ("configure-desktop", "start-desktop", "at_login.py"):
            with self.subTest(script=script):
                self.assertIn("QT_QPA_PLATFORMTHEME", (SESSION_SOURCE_DIR / script).read_text())

    def test_default_scheme_is_the_themes(self):
        # A KDE program set to "Default" takes Breeze's scheme by its file
        # name; the session's own copy of that name must hold the theme's.
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", output)
        schemes = output / "share" / "color-schemes"

        def colours(path):
            scheme = configparser.ConfigParser(interpolation=None, strict=False)
            scheme.optionxform = str
            scheme.read(path)
            return {name: dict(scheme[name]) for name in scheme.sections() if name.startswith("Colors:")}

        theme = colours(schemes / "DesktopTheme.colors")
        self.assertIn("Colors:View", theme)
        for name in ("BreezeLight", "BreezeDark"):
            with self.subTest(scheme=name):
                self.assertEqual(colours(schemes / f"{name}.colors"), theme)


class TestLogGuard(unittest.TestCase):
    """The shell's log keeps repeated lines short, and a shell stuck saying
    one line over and over is ended so keep-running starts it again."""

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.log = Path(self.folder.name) / "login.log"
        self.pid_file = Path(self.folder.name) / "shell.pid"

    def guard(self, text):
        subprocess.run([sys.executable, SESSION_SOURCE_DIR / "log_guard.py", self.log, self.pid_file],
                       input=text.encode(), check=True, timeout=30)
        return self.log.read_text()

    def test_repeated_lines_are_counted(self):
        log = self.guard("starting\n" + "same\n" * 4 + "done\n")
        self.assertEqual(log, "starting\nsame\nlog_guard: the line above came 3 more times\ndone\n")

    def test_count_comes_at_the_end_too(self):
        log = self.guard("same\n" * 3)
        self.assertEqual(log, "same\nlog_guard: the line above came 2 more times\n")

    def test_stuck_shell_is_ended(self):
        # A stand-in shell: a program named qs, as the real one is.
        shell_program = Path(self.folder.name) / "qs"
        shutil.copy(shutil.which("sleep"), shell_program)
        shell = subprocess.Popen([shell_program, "60"])
        self.addCleanup(shell.kill)
        self.pid_file.write_text(f"{shell.pid}\n")
        log = self.guard("Invalid socket 285\n" * 6000)
        self.assertEqual(shell.wait(timeout=10), -15)
        self.assertIn("seems stuck; starting it again", log)
        self.assertLess(len(log), 1000)

    def test_stuck_shell_saying_two_things_is_ended(self):
        shell_program = Path(self.folder.name) / "qs"
        shutil.copy(shutil.which("sleep"), shell_program)
        shell = subprocess.Popen([shell_program, "60"])
        self.addCleanup(shell.kill)
        self.pid_file.write_text(f"{shell.pid}\n")
        log = self.guard("Invalid socket 285\nInvalid socket 286\n" * 3000)
        self.assertEqual(shell.wait(timeout=10), -15)
        self.assertIn("seems stuck; starting it again", log)

    def test_other_program_is_left_alone(self):
        other = subprocess.Popen(["sleep", "60"])
        self.addCleanup(other.kill)
        self.pid_file.write_text(f"{other.pid}\n")
        self.guard("Invalid socket 285\n" * 6000)
        self.assertIsNone(other.poll())


class TestAutostartWaitsForTray(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        (Path(folder.name) / "paths.py").write_text("from pathlib import Path\nLIBRARY_DIR = Path('/nowhere')\n")
        sys.path[:0] = [folder.name, str(SESSION_SOURCE_DIR)]
        sys.modules.pop("paths", None)
        try:
            import at_login
        finally:
            del sys.path[:2]
            sys.modules.pop("paths", None)
        self.addCleanup(sys.modules.pop, "at_login", None)
        self.at_login = at_login
        self.answers = []
        self.clock = [0.0]

        def answer(command, **_):
            self.assertEqual(command[:2], ["busctl", "--user"])
            reply = self.answers.pop(0) if self.answers else "b false\n"
            return subprocess.CompletedProcess(command, 0, stdout=reply, stderr="")

        def sleep(seconds):
            self.clock[0] += seconds

        for name, stand_in in (("subprocess.run", answer), ("time.sleep", sleep),
                               ("time.monotonic", lambda: self.clock[0]),
                               ("shutil.which", lambda name: "/usr/bin/" + name)):
            patcher = unittest.mock.patch(f"at_login.{name}", stand_in)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_waits_until_a_host_is_there(self):
        self.answers = ["b false\n", "b false\n", "b true\n"]
        self.at_login.wait_for_tray({"panel": {"right": ["tray", "clock"]}})
        self.assertEqual(self.answers, [])
        self.assertGreater(self.clock[0], 0)

    def test_gives_up_at_the_deadline(self):
        self.at_login.wait_for_tray({"panel": {"right": ["tray"]}})
        self.assertAlmostEqual(self.clock[0], self.at_login.TRAY_HOST_SECONDS, delta=0.5)

    def test_a_watcher_that_is_not_there_yet_is_waited_for(self):
        missing = subprocess.CompletedProcess([], 1, stdout="", stderr="no watcher")
        with unittest.mock.patch("at_login.subprocess.run", side_effect=[missing, missing, self.tray_is_up()]) as run:
            self.at_login.wait_for_tray({"panel": {"left": ["tray"]}})
        self.assertEqual(run.call_count, 3)

    def test_a_hanging_question_counts_as_no_answer(self):
        hung = subprocess.TimeoutExpired("busctl", 1)
        with unittest.mock.patch("at_login.subprocess.run", side_effect=[hung, self.tray_is_up()]) as run:
            self.at_login.wait_for_tray({"panel": {"right": ["tray"]}})
        self.assertEqual(run.call_count, 2)

    def test_no_wait_without_a_tray_in_the_panel(self):
        self.at_login.wait_for_tray({"panel": {"right": ["clock"]}})
        self.assertEqual(self.clock[0], 0)

    def test_no_wait_without_busctl(self):
        with unittest.mock.patch("at_login.shutil.which", lambda name: None):
            self.at_login.wait_for_tray({"panel": {"right": ["tray"]}})
        self.assertEqual(self.clock[0], 0)

    def test_users_programs_start_after_the_wait(self):
        calls = unittest.mock.Mock()
        settings = Path(tempfile.mkdtemp()) / "shell.json"
        self.addCleanup(shutil.rmtree, settings.parent)
        settings.write_text(json.dumps({"config": {"panel": {"right": ["tray"]}}}))
        for name in ("wait_for_tray", "start_users_programs"):
            patcher = unittest.mock.patch(f"at_login.{name}", getattr(calls, name))
            patcher.start()
            self.addCleanup(patcher.stop)
        for name in ("hand_login_to_wallet", "tell_services", "start_xsettings",
                     "start_listed_programs", "offer_new_theme_imports"):
            patcher = unittest.mock.patch(f"at_login.{name}")
            patcher.start()
            self.addCleanup(patcher.stop)
        with unittest.mock.patch.object(sys, "argv", ["at_login.py", str(settings)]), \
             unittest.mock.patch.dict(os.environ, {"DESKTOP_NESTED": ""}):
            self.at_login.main()
        self.assertEqual([call[0] for call in calls.mock_calls], ["wait_for_tray", "start_users_programs"])

    @staticmethod
    def tray_is_up():
        return subprocess.CompletedProcess([], 0, stdout="b true\n", stderr="")

    def test_file_dialog_portal_is_left_out_of_the_restart(self):
        calls = []
        def record(command, **kwargs):
            calls.append(command)
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        with unittest.mock.patch("at_login.subprocess.run", record):
            self.at_login.restart_early_portals()
        restart = next(command for command in calls if "try-restart" in command)
        # a restarted helper with the session's qt6ct settings deadlocks the portal for 25 seconds
        self.assertNotIn("plasma-xdg-desktop-portal-kde.service", restart)


class TestGtkWindowButtons(SessionTestCase):
    def test_session_theme_name_is_published_to_x11_and_wayland(self):
        settings = self.home / "shell.json"
        self.home.mkdir(parents=True)
        settings.write_text(json.dumps({"theme": {"controls": {}, "icons": {"theme": "Adwaita"}},
                                        "config": {"window": {"button_side": "right"}}}))
        fake_bin = self.home / "bin"
        fake_bin.mkdir()
        log = self.home / "gsettings.log"
        gsettings = fake_bin / "gsettings"
        gsettings.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$SETTINGS_LOG"\n')
        gsettings.chmod(0o755)
        self.environment.update(PATH=f"{fake_bin}:{os.environ['PATH']}", SETTINGS_LOG=str(log))
        output = self.home / "xsettingsd.conf"
        self.run_script("write_interface_settings.py", settings, "--xsettings-file", output)
        self.assertIn('Net/ThemeName "DesktopTheme"', output.read_text())
        self.assertIn('Gtk/DecorationLayout ":close,minimize,maximize"', output.read_text())
        self.assertIn("set org.gnome.desktop.interface gtk-theme DesktopTheme", log.read_text())
        settings.write_text(json.dumps({"theme": {"controls": {}}, "config": {"gtk": {"use_theme": False}}}))
        log.write_text("")
        self.run_script("write_interface_settings.py", settings, "--xsettings-file", output)
        self.assertNotIn("Net/ThemeName", output.read_text())
        self.assertIn("reset org.gnome.desktop.interface gtk-theme", log.read_text())

    def test_plain_buttons_keep_their_shapes_and_states(self):
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", output)
        for version in ("gtk-3.0", "gtk-4.0"):
            css = (output / "share/themes/DesktopTheme" / version / "gtk.css").read_text()
            self.assertIn("button.titlebutton.close", css)
            self.assertIn("background-color: #e06060;", css)
            self.assertIn("background-color: #b04040;", css)
            self.assertIn("border-radius: 3px;", css)

    def test_button_artwork_is_copied_without_control_name_collisions(self):
        from PIL import Image

        theme = self.home / "theme"
        theme.mkdir(parents=True)
        text = (THEMES_DIR / "plain/theme.toml").read_text()
        text = text.replace('normal   = { color = "#d0d0d0", radius = 3 }',
                            'normal   = { image = "close.png" }', 1)
        (theme / "theme.toml").write_text(text)
        for suffix, size in (("", 16), ("@2x", 32)):
            Image.new("RGBA", (size, size), "red").save(theme / f"close{suffix}.png")
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", theme, output)
        for version in ("gtk-3.0", "gtk-4.0"):
            gtk = output / "share/themes/DesktopTheme" / version
            for suffix in ("", "@2x"):
                self.assertTrue((gtk / f"assets/window-close-normal{suffix}.png").is_file())
                self.assertEqual((gtk / f"assets/window-close-normal{suffix}.png").read_bytes(),
                                 (theme / f"close{suffix}.png").read_bytes())
            self.assertIn("assets/window-close-normal.png", (gtk / "gtk.css").read_text())


class TestMozillaTitlebar(SessionTestCase):
    STYLE = "share/themes/DesktopTheme/mozilla/titlebar.css"

    def picture_theme(self, buttons_table):
        from PIL import Image

        theme = self.home / "theme"
        theme.mkdir(parents=True)
        text = re.sub(r"\[window\.buttons\].*?(?=\[window\.frame\])", buttons_table, (THEMES_DIR / "plain/theme.toml").read_text(), flags=re.S)
        (theme / "theme.toml").write_text(text)
        for suffix, size in (("", 16), ("@2x", 32)):
            Image.new("RGBA", (size, size), "red").save(theme / f"dot{suffix}.png")
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", theme, output)
        return output

    def rule(self, css, selector):
        for selectors, body in re.findall(r"([^{}]+)\{([^{}]*)\}", re.sub(r"/\*.*?\*/", "", css)):
            if selector in [part.strip() for part in selectors.split(",")]:
                return body
        self.fail(f"no rule for {selector}:\n{css}")

    PICTURE_BUTTONS = """[window.buttons]
size = [16, 14]
spacing = 4
edge_inset = 9
hover_together = true

[window.buttons.close]
normal   = { image = "dot.png" }
hover    = { image = "dot.png" }
pressed  = { image = "dot.png" }
inactive = { image = "dot.png" }

[window.buttons.minimize]
normal   = { image = "dot.png" }

[window.buttons.maximize]
normal   = { image = "dot.png" }

"""

    def test_colours_of_every_state_reach_every_button(self):
        output = self.home / "app-theme"
        self.run_script("make_app_theme.py", THEMES_DIR / "plain", output)
        css = (output / self.STYLE).read_text()
        icon = "> .toolbarbutton-icon"
        self.assertIn("background-color: #d0d0d0 !important;", self.rule(css, f".titlebar-close {icon}"))
        self.assertIn("background-color: #e0e0e0 !important;", self.rule(css, f".titlebar-close {icon}:-moz-window-inactive"))
        self.assertIn("background-color: #e06060 !important;", self.rule(css, f".titlebar-close:hover {icon}"))
        self.assertIn("background-color: #b04040 !important;", self.rule(css, f".titlebar-close:hover:active {icon}"))
        self.assertIn("background-color: #b8b8b8 !important;", self.rule(css, f".titlebar-min:hover {icon}"))
        for button in (".titlebar-max", ".titlebar-restore"):
            self.assertIn("background-color: #a0a0a0 !important;", self.rule(css, f"{button}:hover:active {icon}"))
        self.assertIn("border-radius: 3px !important;", self.rule(css, f".titlebar-close {icon}"))

    def test_buttons_take_the_frames_size_and_spacing(self):
        css = (self.picture_theme(self.PICTURE_BUTTONS) / self.STYLE).read_text()
        box = self.rule(css, ".titlebar-buttonbox")
        self.assertIn("--titlebar-button-size: 16px !important;", box)
        self.assertIn("--titlebar-button-spacing: 2px !important;", box)
        self.assertIn("--titlebar-button-end-spacing: 9px !important;", box)
        icon = self.rule(css, ".titlebar-button > .toolbarbutton-icon")
        self.assertIn("width: 16px !important;", icon)
        self.assertIn("height: 14px !important;", icon)
        self.assertIn("appearance: none !important;", icon)
        self.assertIn("transition: none !important;", icon)
        self.assertRegex(css, r"@media \(-moz-gtk-csd-reversed-placement\) \{\s*\.titlebar-buttonbox \{\s*padding-inline-start: 7px !important;")

    def test_pictures_come_from_the_gtk_assets_at_both_densities(self):
        output = self.picture_theme(self.PICTURE_BUTTONS)
        css = (output / self.STYLE).read_text()
        body = self.rule(css, ".titlebar-close > .toolbarbutton-icon")
        self.assertIn('image-set(url("../gtk-3.0/assets/window-close-normal.png") 1x, '
                      'url("../gtk-3.0/assets/window-close-normal@2x.png") 2x)', body)
        self.assertIn("background-size: 16px 14px !important;", body)
        for name in re.findall(r'url\("\.\./(gtk-3\.0/assets/[^"]+)"\)', css):
            self.assertTrue((output / "share/themes/DesktopTheme" / name).is_file(), name)

    def test_hover_together_lights_all_buttons_from_the_box(self):
        css = (self.picture_theme(self.PICTURE_BUTTONS) / self.STYLE).read_text()
        self.rule(css, ".titlebar-buttonbox:hover .titlebar-close > .toolbarbutton-icon")
        self.assertNotIn(".titlebar-close:hover >", css)
        self.rule(css, ".titlebar-close:hover:active > .toolbarbutton-icon")

    def test_a_state_the_theme_leaves_out_shows_its_normal_picture(self):
        css = (self.picture_theme(self.PICTURE_BUTTONS) / self.STYLE).read_text()
        body = self.rule(css, ".titlebar-min > .toolbarbutton-icon:-moz-window-inactive")
        self.assertIn("window-minimize-normal.png", body)

    def test_no_buttons_in_the_theme_writes_no_style(self):
        output = self.picture_theme("")
        self.assertFalse((output / self.STYLE).exists())
        self.assertFalse((output / "share/themes/DesktopTheme/gimp/titlebar.css").exists())

    def test_gimp_style_reaches_the_gtk_pictures(self):
        css = (self.picture_theme(self.PICTURE_BUTTONS) / "share/themes/DesktopTheme/gimp/titlebar.css").read_text()
        self.assertIn('url("../gtk-3.0/assets/', css)
        self.assertNotIn('url("assets/', css)
        self.assertIn("padding: 0", self.rule(css, "button.titlebutton.close"))


class TestGimpTitlebar(SessionTestCase):
    IMPORT = '@import url("mavernix/gimp/titlebar.css");\n'

    def config(self, version="3.2", style=None):
        folder = self.home / ".config/GIMP" / version
        folder.mkdir(parents=True)
        if style is not None:
            (folder / "gimp.css").write_text(style)
        return folder

    def link(self, *theme):
        return self.run_script("link_gimp_style.py", *theme)

    def test_each_version_gets_the_link_and_import(self):
        folders = [self.config("3.0"), self.config("3.2")]
        self.link("/run/theme")
        for folder in folders:
            self.assertEqual(os.readlink(folder / "mavernix"), "/run/theme")
            self.assertEqual((folder / "gimp.css").read_text(), self.IMPORT)

    def test_the_users_rules_stay_and_the_import_is_not_repeated(self):
        folder = self.config(style="#a { color: red; }\n")
        self.link("/run/theme")
        self.link("/run/theme")
        self.assertEqual((folder / "gimp.css").read_text(), self.IMPORT + "#a { color: red; }\n")

    def test_leaving_the_theme_removes_link_and_import(self):
        folder = self.config(style="#a { color: red; }\n")
        self.link("/run/theme")
        self.link()
        self.assertFalse((folder / "mavernix").is_symlink())
        self.assertEqual((folder / "gimp.css").read_text(), "#a { color: red; }\n")

    def test_a_folder_of_the_users_own_is_left_alone(self):
        folder = self.config()
        (folder / "mavernix").mkdir()
        self.link("/run/theme")
        self.assertFalse((folder / "gimp.css").exists())


class TestMozillaProfiles(SessionTestCase):
    def profile(self, root, name="abc.default", user_chrome=None, user_js=None):
        profile = self.home / root / name
        (profile).mkdir(parents=True)
        (self.home / root / "profiles.ini").write_text(f"[General]\nVersion=2\n\n[Profile0]\nName=d\nIsRelative=1\nPath={name}\n")
        if user_chrome is not None:
            (profile / "chrome").mkdir()
            (profile / "chrome/userChrome.css").write_text(user_chrome)
        if user_js is not None:
            (profile / "user.js").write_text(user_js)
        return profile

    def link(self, theme="/run/theme"):
        return self.run_script("link_mozilla_style.py", theme)

    MENU_IMPORT = '@import url("mavernix/mozilla/global-menu.css");\n'
    IMPORT = MENU_IMPORT + '@import url("mavernix/mozilla/titlebar.css");\n'
    MENU_PREFS = ('user_pref("widget.gtk.global-menu.enabled", {0}); // mavernix\n'
                  'user_pref("widget.gtk.global-menu.wayland.enabled", {0}); // mavernix\n')
    PREF = 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);\n' + MENU_PREFS.format("true")

    def test_every_program_folder_gets_the_style(self):
        profiles = [self.profile(root) for root in (".config/mozilla/firefox", ".mozilla/firefox", ".config/thunderbird", ".thunderbird")]
        self.link()
        for profile in profiles:
            self.assertEqual(os.readlink(profile / "chrome/mavernix"), "/run/theme")
            self.assertEqual((profile / "chrome/userChrome.css").read_text(), self.IMPORT)
            self.assertEqual((profile / "user.js").read_text(), self.PREF)

    def test_the_users_own_rules_stay_behind_the_import(self):
        profile = self.profile(".config/thunderbird", user_chrome="#a { color: red; }\n", user_js='user_pref("a", 1);\n')
        self.link()
        self.assertEqual((profile / "chrome/userChrome.css").read_text(), self.IMPORT + "#a { color: red; }\n")
        self.assertEqual((profile / "user.js").read_text(), 'user_pref("a", 1);\n' + self.PREF)

    def test_a_linked_style_file_stays_a_link(self):
        profile = self.profile(".config/thunderbird")
        dotfiles = self.home / "dotfiles"
        dotfiles.mkdir()
        (dotfiles / "style.css").write_text("#a { color: red; }\n")
        (profile / "chrome").mkdir()
        (profile / "chrome/userChrome.css").symlink_to(dotfiles / "style.css")
        self.link()
        self.assertTrue((profile / "chrome/userChrome.css").is_symlink())
        self.assertEqual((dotfiles / "style.css").read_text(), self.IMPORT + "#a { color: red; }\n")

    def test_a_second_run_changes_nothing(self):
        profile = self.profile(".config/thunderbird", user_chrome="#a { color: red; }\n")
        self.link()
        first = ((profile / "chrome/userChrome.css").read_text(), (profile / "user.js").read_text())
        self.link("/run/other")
        self.assertEqual(first, ((profile / "chrome/userChrome.css").read_text(), (profile / "user.js").read_text()))
        self.assertEqual(os.readlink(profile / "chrome/mavernix"), "/run/other")

    def test_a_setting_the_user_chose_is_left_alone(self):
        chosen = 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", false);\n'
        profile = self.profile(".config/thunderbird", user_js=chosen)
        self.link()
        self.assertEqual((profile / "user.js").read_text(), chosen + self.MENU_PREFS.format("true"))

    def test_a_profile_outside_the_folder_is_found(self):
        elsewhere = self.home / "mail/profile"
        elsewhere.mkdir(parents=True)
        root = self.home / ".thunderbird"
        root.mkdir()
        (root / "profiles.ini").write_text(f"[Profile0]\nIsRelative=0\nPath={elsewhere}\n")
        self.link()
        self.assertTrue((elsewhere / "chrome/mavernix").is_symlink())

    def test_a_profile_that_is_gone_is_skipped(self):
        root = self.home / ".thunderbird"
        root.mkdir(parents=True)
        (root / "profiles.ini").write_text("[Profile0]\nIsRelative=1\nPath=gone\n")
        self.link()
        self.assertFalse((root / "gone").exists())

    def test_a_folder_of_the_users_own_is_not_replaced(self):
        profile = self.profile(".config/thunderbird")
        (profile / "chrome/mavernix").mkdir(parents=True)
        result = self.run_script("link_mozilla_style.py", "/run/theme", may_fail=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn("chrome/mavernix", result.stderr)
        self.assertFalse((profile / "chrome/mavernix").is_symlink())
        self.assertFalse((profile / "chrome/userChrome.css").exists())

    def test_menus_turn_off_when_the_theme_goes(self):
        profile = self.profile(".config/thunderbird")
        self.link()
        self.run_script("link_mozilla_style.py")
        text = (profile / "user.js").read_text()
        self.assertIn(self.MENU_PREFS.format("false"), text)
        self.assertNotIn(self.MENU_PREFS.format("true"), text)
        self.link()
        self.assertEqual((profile / "user.js").read_text().count("global-menu"), 2)
        self.assertIn(self.MENU_PREFS.format("true"), (profile / "user.js").read_text())

    def test_a_profile_that_never_had_menus_gets_no_settings_without_a_theme(self):
        profile = self.profile(".config/thunderbird", user_js='user_pref("a", 1);\n')
        self.run_script("link_mozilla_style.py")
        self.assertEqual((profile / "user.js").read_text(), 'user_pref("a", 1);\n')

    def test_the_opt_out_keeps_the_local_menu_bar(self):
        profile = self.profile(".config/thunderbird")
        self.link()
        self.run_script("link_mozilla_style.py", "/run/theme", environment={"MAVERNIX_GLOBAL_MENU": "0"})
        self.assertNotIn(self.MENU_IMPORT, (profile / "chrome/userChrome.css").read_text())
        self.assertIn("titlebar.css", (profile / "chrome/userChrome.css").read_text())
        self.assertIn(self.MENU_PREFS.format("false"), (profile / "user.js").read_text())

    def test_a_menu_setting_the_user_chose_is_left_alone(self):
        chosen = 'user_pref("widget.gtk.global-menu.enabled", false);\n'
        profile = self.profile(".config/thunderbird", user_js=chosen)
        self.link()
        text = (profile / "user.js").read_text()
        self.assertIn(chosen, text)
        self.assertEqual(text.count("widget.gtk.global-menu.enabled"), 1)
        self.assertIn('user_pref("widget.gtk.global-menu.wayland.enabled", true); // mavernix\n', text)

    def test_no_theme_takes_the_link_away_and_nothing_else(self):
        profile = self.profile(".config/thunderbird", user_chrome="#a { color: red; }\n")
        self.link()
        self.run_script("link_mozilla_style.py")
        self.assertFalse((profile / "chrome/mavernix").is_symlink())
        self.assertIn("#a { color: red; }", (profile / "chrome/userChrome.css").read_text())

    def test_no_programs_at_all_is_fine(self):
        self.home.mkdir(parents=True)
        self.link()


class SessionLog(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.runtime = self.root / "runtime"
        self.runtime.mkdir()
        self.state = self.root / "state"

    def new_log(self, state=None):
        result = subprocess.run(
            ["bash", str(SESSION_SOURCE_DIR / "new-session-log"), str(self.runtime)],
            env={**os.environ, "XDG_STATE_HOME": str(state or self.state)},
            capture_output=True, text=True, timeout=10, check=True,
        )
        return Path(result.stdout.strip())

    def test_log_is_on_disk_and_linked_from_runtime(self):
        log = self.new_log()
        self.assertTrue(log.exists())
        self.assertTrue(log.is_relative_to(self.state))
        self.assertEqual((self.runtime / "session.log").resolve(), log.resolve())

    def test_replaces_a_plain_runtime_log(self):
        (self.runtime / "session.log").write_text("old")
        self.new_log()
        self.assertTrue((self.runtime / "session.log").is_symlink())

    def test_keeps_the_last_five(self):
        logs = self.state / "mavernix" / "logs"
        logs.mkdir(parents=True)
        for day in range(1, 9):
            old = logs / f"session-2026-01-0{day}-000000.log"
            old.write_text("x")
            os.utime(old, (day * 86400, day * 86400))
        newest = self.new_log()
        kept = sorted(p.name for p in logs.iterdir())
        self.assertEqual(len(kept), 5, kept)
        self.assertIn(newest.name, kept)
        self.assertIn("session-2026-01-08-000000.log", kept)
        self.assertNotIn("session-2026-01-04-000000.log", kept)

    def test_falls_back_to_runtime_when_disk_cannot_be_written(self):
        blocked = self.root / "file"
        blocked.write_text("")
        self.assertEqual(self.new_log(state=blocked), self.runtime / "session.log")


class ShellStart(unittest.TestCase):
    def test_shell_starts_when_runtime_dir_is_full(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            shutil.copy(SESSION_SOURCE_DIR / "run-shell", root)
            (root / "paths.sh").write_text("shell_dir=/nowhere\n")
            (root / "desktop.env").write_text("")
            (root / "shell.pid").symlink_to("/dev/full")
            fake = root / "qs"
            fake.write_text('#!/bin/sh\necho started > "$STARTED"\n')
            fake.chmod(0o755)
            started = root / "started"
            result = subprocess.run(
                ["bash", str(root / "run-shell")],
                env={**os.environ, "PATH": f"{root}:{os.environ['PATH']}",
                     "STARTED": str(started),
                     "DESKTOP_SHELL_SETTINGS": str(root / "shell.json")},
                capture_output=True, text=True, timeout=10,
            )
            self.assertTrue(started.exists(), f"rc={result.returncode} stderr={result.stderr}")


if __name__ == "__main__":
    unittest.main()
