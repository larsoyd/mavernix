"""mavernix-reveal shows a launcher's program or a file's info in the file manager."""

import os
import sqlite3
import subprocess
import tempfile
import unittest
from pathlib import Path

REVEAL = Path(__file__).resolve().parent.parent / "src" / "session" / "bin" / "mavernix-reveal"


class Reveal(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.home = self.root / "home"
        self.data = self.home / ".local" / "share"
        self.config = self.home / ".config"
        self.programs = self.root / "bin"
        self.programs.mkdir(parents=True)
        self.asked = self.root / "asked"
        self.program("gdbus", f'printf "%s\\n" "$@" > {self.asked}\n')
        self.desktop = self.home / "Desktop"
        self.desktop.mkdir(parents=True)

    def tearDown(self):
        self.scratch.cleanup()

    def program(self, name, body="exit 0\n"):
        path = self.programs / name
        path.write_text("#!/bin/sh\n" + body)
        path.chmod(0o755)
        return path

    def file(self, path, text=""):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def launcher(self, exec_line, more=""):
        return self.file(self.desktop / "Thing.desktop",
                         f"[Desktop Entry]\nType=Application\nName=Thing\nExec={exec_line}\n{more}")

    environment = {}

    def reveal(self, *arguments):
        environment = {"PATH": f"{self.programs}:/usr/bin:/bin", "HOME": str(self.home), **self.environment}
        return subprocess.run([str(REVEAL), *arguments], env=environment, capture_output=True, text=True, timeout=20)

    def shown(self, launcher):
        result = self.reveal("original", str(launcher))
        self.assertEqual(result.returncode, 0, result.stderr)
        words = self.asked.read_text().splitlines()
        method = words[words.index("--method") + 1].rsplit(".", 1)[1]
        return method, words[words.index("--method") + 2]

    def uri(self, path):
        return f"['{Path(path).as_uri()}']"

    def test_a_program_named_alone_is_found_on_the_path(self):
        kate = self.program("kate")
        self.assertEqual(self.shown(self.launcher("kate -b %U")), ("ShowItems", self.uri(kate)))

    def test_a_shim_this_login_wrote_is_passed_over_for_the_program_it_runs(self):
        office = self.program("libreoffice")
        shims = self.root / "runtime" / "mavernix" / "bin"
        shims.mkdir(parents=True)
        (shims / "libreoffice").write_text(f"#!/bin/sh\nexec {office} \"$@\"\n")
        (shims / "libreoffice").chmod(0o755)
        self.environment = {"PATH": f"{shims}:{self.programs}:/usr/bin:/bin", "XDG_RUNTIME_DIR": str(self.root / "runtime")}
        self.assertEqual(self.shown(self.launcher("libreoffice --writer %U")), ("ShowItems", self.uri(office)))

    def test_words_that_name_no_file_leave_the_program(self):
        manager = self.program("tux-manager")
        self.assertEqual(self.shown(self.launcher('tux-manager -qwindowtitle "Task Manager"')),
                         ("ShowItems", self.uri(manager)))

    def test_a_program_behind_a_wrapper_is_shown_not_the_wrapper(self):
        self.program("mullvad-exclude")
        proxy = self.program("wheelproxy-launch")
        steam = self.program("steam-real")
        self.assertEqual(self.shown(self.launcher(f"/usr/bin/env mullvad-exclude {proxy} {steam} %U")),
                         ("ShowItems", self.uri(steam)))

    def test_settings_before_the_program_are_passed_over(self):
        game = self.program("ge007.x86_64")
        line = f"env SDL_VIDEODRIVER=wayland SDL_VIDEO_WAYLAND_WMCLASS=goldeneye-pc {game}"
        self.assertEqual(self.shown(self.launcher(line)), ("ShowItems", self.uri(game)))

    def test_a_script_run_by_its_interpreter_is_shown(self):
        script = self.file(self.root / "tools" / "vm.sh")
        self.assertEqual(self.shown(self.launcher(f"bash {script} up --disk personal")),
                         ("ShowItems", self.uri(script)))

    def test_a_file_the_launcher_opens_is_shown(self):
        rom = self.file(self.root / "Games" / "Mario Kart 64 (Rev A).z64")
        self.assertEqual(self.shown(self.launcher(f'gio open "{rom}"')), ("ShowItems", self.uri(rom)))

    def test_a_path_from_home_is_found(self):
        tool = self.file(self.home / ".local" / "bin" / "notepad++-open")
        self.assertEqual(self.shown(self.launcher("~/.local/bin/notepad++-open %f")), ("ShowItems", self.uri(tool)))

    def test_quoted_escapes_are_read_as_the_entry_format_says(self):
        odd = self.file(self.root / 'a "b" $c.exe')
        quoted = str(odd).replace('"', '\\\\"').replace("$", "\\\\$")
        self.assertEqual(self.shown(self.launcher(f'gio open "{quoted}"')), ("ShowItems", self.uri(odd)))

    def test_a_windows_program_is_found_in_its_prefix(self):
        prefix = self.root / "wine"
        exe = self.file(prefix / "drive_c" / "Program Files" / "Image-Line" / "FL64.exe")
        line = (f'env "WINEPREFIX={prefix}" "WINE_FORCE_PORTAL=1" /usr/bin/wine '
                '"C:\\\\\\\\Program Files\\\\\\\\Image-Line\\\\\\\\FL64.exe"')
        self.assertEqual(self.shown(self.launcher(line)), ("ShowItems", self.uri(exe)))

    def test_a_windows_path_ignores_case_as_windows_does(self):
        exe = self.file(self.home / ".wine" / "drive_c" / "Games" / "Game.exe")
        self.assertEqual(self.shown(self.launcher('wine "C:\\\\\\\\games\\\\\\\\GAME.EXE"')),
                         ("ShowItems", self.uri(exe)))

    def test_a_steam_game_shows_its_folder(self):
        library = self.root / "library"
        game = library / "steamapps" / "common" / "Quake"
        game.mkdir(parents=True)
        self.file(self.data / "Steam" / "steamapps" / "libraryfolders.vdf",
                  f'"libraryfolders"\n{{\n\t"0"\n\t{{\n\t\t"path"\t\t"{self.data / "Steam"}"\n\t}}\n'
                  f'\t"1"\n\t{{\n\t\t"path"\t\t"{library}"\n\t}}\n}}\n')
        self.file(library / "steamapps" / "appmanifest_2310.acf",
                  '"AppState"\n{\n\t"appid"\t\t"2310"\n\t"installdir"\t\t"Quake"\n}\n')
        self.assertEqual(self.shown(self.launcher("steam steam://rungameid/2310")), ("ShowFolders", self.uri(game)))

    def lutris_game(self, number, folder, slug, config=None):
        database = self.data / "lutris" / "pga.db"
        database.parent.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(database) as connection:
            connection.execute("create table if not exists games (id integer, directory text, configpath text)")
            connection.execute("insert into games values (?, ?, ?)", (number, str(folder), slug))
        connection.close()
        if config is not None:
            self.file(self.config / "lutris" / "games" / f"{slug}.yml", config)

    def test_a_lutris_game_shows_its_program(self):
        folder = self.root / "Games" / "firewatch"
        exe = self.file(folder / "drive_c" / "Firewatch" / "Launch.lnk")
        self.lutris_game(28, folder, "firewatch-1", f"script:\n  game:\n    exe: elsewhere\ngame:\n  prefix: {folder}\n"
                                                    f"  exe: {exe}\n")
        self.assertEqual(self.shown(self.launcher("env LUTRIS_SKIP_INIT=1 lutris lutris:rungameid/28")),
                         ("ShowItems", self.uri(exe)))

    def test_a_lutris_program_named_from_its_folder_is_found(self):
        folder = self.root / "Games" / "facade"
        exe = self.file(folder / "Facade.exe")
        self.lutris_game(27, folder, "facade-1", "game:\n  exe: Facade.exe\n")
        self.assertEqual(self.shown(self.launcher("lutris lutris:rungameid/27")), ("ShowItems", self.uri(exe)))

    def test_a_lutris_game_without_its_program_shows_its_folder(self):
        folder = self.root / "Games" / "facade"
        folder.mkdir(parents=True)
        self.lutris_game(27, folder, "facade-1")
        self.assertEqual(self.shown(self.launcher("lutris lutris:rungameid/27")), ("ShowFolders", self.uri(folder)))

    def test_a_program_found_nowhere_falls_back_to_the_folder_it_runs_in(self):
        folder = self.root / "work"
        folder.mkdir()
        self.assertEqual(self.shown(self.launcher("no-such-program", f"Path={folder}\n")),
                         ("ShowFolders", self.uri(folder)))

    def test_a_program_found_nowhere_is_refused(self):
        result = self.reveal("original", str(self.launcher("no-such-program --flag")))
        self.assertEqual(result.returncode, 1)
        self.assertIn("refused: no program found", result.stderr)
        self.assertIn("Thing.desktop", result.stderr)
        self.assertFalse(self.asked.exists())

    def test_a_missing_steam_game_is_refused(self):
        result = self.reveal("original", str(self.launcher("steam steam://rungameid/99")))
        self.assertEqual(result.returncode, 1)
        self.assertIn("steam game 99", result.stderr)

    def test_info_asks_for_the_properties_of_every_file(self):
        first = self.file(self.root / "a b.txt")
        result = self.reveal("info", str(first), str(self.desktop))
        self.assertEqual(result.returncode, 0, result.stderr)
        words = self.asked.read_text().splitlines()
        self.assertEqual(words[words.index("--method") + 1], "org.freedesktop.FileManager1.ShowItemProperties")
        self.assertEqual(words[words.index("--method") + 2],
                         f"['{first.as_uri()}', '{self.desktop.as_uri()}']")
        self.assertIn("--timeout", words)

    def test_a_file_manager_that_fails_is_reported(self):
        self.program("gdbus", "echo no FileManager1 >&2\nexit 1\n")
        result = self.reveal("info", str(self.desktop))
        self.assertEqual(result.returncode, 1)
        self.assertIn("no FileManager1", result.stderr)


if __name__ == "__main__":
    unittest.main()
