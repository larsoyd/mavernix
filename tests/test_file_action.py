"""A desktop shortcut is a trusted copy of a program's desktop entry."""

import io
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

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

    def launched(self, launcher):
        """Starts the launcher with gio and returns what its program was given and where it ran."""
        stub = self.root / "stub"
        stub.mkdir(exist_ok=True)
        record = self.root / "record"
        (stub / "gio").write_text(f'#!/bin/sh\n{{ pwd; printf "%s\\n" "$@"; }} > "{record}"\n')
        (stub / "gio").chmod(0o755)
        environment = dict(self.environment, PATH=f"{stub}:{os.environ['PATH']}")
        subprocess.run(["/usr/bin/gio", "launch", str(launcher)], env=environment, check=True, timeout=20)
        for _ in range(100):
            if record.exists() and record.read_text().count("\n") >= 3:
                break
            subprocess.run(["sleep", "0.05"], check=True)
        return record.read_text().splitlines()

    def entry(self, launcher):
        return dict(line.split("=", 1) for line in launcher.read_text().splitlines() if "=" in line)

    def test_a_file_gets_a_launcher_that_opens_it_from_its_own_folder(self):
        folder = self.files / "Game's $HOME `x` 100% \\ dir"
        folder.mkdir()
        game = folder / 'run "me".exe'
        game.write_bytes(b"MZ")
        result = self.send(game)
        self.assertEqual(result.returncode, 0, result.stderr)
        launcher = self.desktop / 'run "me".desktop'
        self.assertFalse(launcher.is_symlink())
        self.assertTrue(os.access(launcher, os.X_OK))
        self.assertEqual(self.entry(launcher)["Name"], 'run "me"')
        self.assertEqual(self.launched(launcher), [str(folder), "open", str(game)])

    def test_a_windows_program_shows_its_own_icon(self):
        game = self.files / "game.exe"
        game.write_bytes(windows_program_with_icon((16, (10, 10, 10)), (48, (200, 30, 40)), (32, (10, 10, 10))))
        self.assertEqual(self.send(game).returncode, 0)
        icon = Path(self.entry(self.desktop / "game.desktop")["Icon"])
        self.assertTrue(icon.is_absolute(), icon)
        self.assertEqual(Image.open(icon).size, (48, 48))
        self.assertEqual(Image.open(icon).convert("RGB").getpixel((8, 8)), (200, 30, 40))

    def previewer(self, command, needs="python3", kind="application/x-n64-rom", system=None, file=None):
        folder = self.home / ".local/share/thumbnailers"
        folder.mkdir(parents=True)
        (folder / "a-broken.thumbnailer").write_text(f"MimeType={kind};\n")
        share = self.root / "share"
        (share / "thumbnailers").mkdir(parents=True)
        (share / "mime").symlink_to("/usr/share/mime")
        self.environment["XDG_DATA_DIRS"] = str(share)
        entry = "[Thumbnailer Entry]\nTryExec={}\nExec={}\nMimeType={};\n"
        (folder / "cartridge.thumbnailer").write_text(entry.format(needs, command, kind))
        if system:
            (share / "thumbnailers" / "a-system.thumbnailer").write_text(entry.format("python3", system, kind))
        game = file or self.files / "kart.z64"
        if not game.exists():
            game.write_bytes(b"\x80\x37\x12\x40" + bytes(4092))
        self.assertEqual(self.send(game).returncode, 0)
        return self.entry(self.desktop / f"{game.stem}.desktop")["Icon"]

    def test_a_file_with_a_preview_shows_its_preview(self):
        icon = Path(self.previewer(f'python3 -c "{self.DRAW}" %o %s'))
        self.assertTrue(icon.is_absolute(), icon)
        with Image.open(icon) as picture:
            self.assertEqual(picture.size, (512, 512))
            self.assertEqual(picture.convert("RGB").getpixel((256, 256)), (250, 0, 250))

    def test_the_users_own_previewer_wins_over_the_systems(self):
        icon = Path(self.previewer(f'python3 -c "{self.DRAW}" %o %s', system="python3 -c 'raise SystemExit(1)'"))
        self.assertTrue(icon.is_absolute(), icon)

    def test_a_previewer_for_another_kind_is_not_asked(self):
        notes = self.files / "notes.txt"
        notes.write_text("hello\n")
        self.assertEqual(self.previewer(f'python3 -c "{self.DRAW}" %o %s', file=notes), "text-plain")

    def test_a_windows_programs_own_icon_wins_over_a_preview(self):
        game = self.files / "game.exe"
        game.write_bytes(windows_program_with_icon((48, (200, 30, 40)),))
        kind = subprocess.run(["gio", "info", "-a", "standard::content-type", str(game)], capture_output=True,
                              text=True, timeout=10).stdout.rsplit(":", 1)[-1].strip()
        icon = self.previewer(f'python3 -c "{self.DRAW}" %o %s', kind=kind, file=game)
        with Image.open(icon) as picture:
            self.assertEqual(picture.convert("RGB").getpixel((8, 8)), (200, 30, 40))

    DRAW = "from PIL import Image; import sys; Image.new('RGBA', (int(sys.argv[2]),) * 2, (250, 0, 250, 255)).save(sys.argv[1], 'PNG')"

    def test_a_previewer_whose_program_is_missing_is_passed_over(self):
        self.assertEqual(self.previewer(f'python3 -c "{self.DRAW}" %o %s', needs="no-such-previewer"),
                         "application-x-n64-rom")

    def test_a_previewer_with_a_broken_command_leaves_the_icon_of_its_kind(self):
        self.assertEqual(self.previewer('python3 -c "unclosed'), "application-x-n64-rom")

    def test_a_preview_that_fails_leaves_the_icon_of_its_kind(self):
        self.assertEqual(self.previewer("python3 -c 'raise SystemExit(1)'"), "application-x-n64-rom")

    def test_a_link_to_a_file_opens_the_file_from_its_real_folder(self):
        game = self.files / "game.exe"
        game.write_bytes(b"MZ")
        (self.root / "elsewhere").mkdir()
        alias = self.root / "elsewhere" / "game.exe"
        alias.symlink_to(game)
        self.assertEqual(self.send(alias).returncode, 0)
        self.assertEqual(self.launched(self.desktop / "game.desktop"), [str(self.files), "open", str(game)])

    def test_a_file_without_its_own_icon_shows_the_icon_of_its_kind(self):
        notes = self.files / "notes.txt"
        notes.write_text("hi")
        self.assertEqual(self.send(notes).returncode, 0)
        self.assertEqual(self.entry(self.desktop / "notes.desktop")["Icon"], "text-plain")

    def test_a_folder_gets_a_link_under_its_own_name(self):
        (self.files / "Photos").mkdir()
        self.assertEqual(self.send(self.files / "Photos").returncode, 0)
        self.assertEqual(os.readlink(self.desktop / "Photos"), str(self.files / "Photos"))

    def test_a_taken_name_gets_a_number(self):
        letter = self.files / "letter.txt"
        letter.write_text("hi")
        self.send(letter)
        self.assertEqual(self.send(letter).returncode, 0)
        self.assertEqual(self.launched(self.desktop / "letter 2.desktop")[-1], str(letter))

    def test_a_program_entry_becomes_a_launcher_named_like_the_program(self):
        entry = self.files / "paint-box.desktop"
        entry.write_text(ENTRY)
        result = self.send(entry)
        self.assertEqual(result.returncode, 0, result.stderr)
        launcher = self.desktop / "Paint Box.desktop"
        self.assertFalse(launcher.is_symlink())
        self.assertEqual(launcher.read_text(), ENTRY)
        self.assertTrue(os.access(launcher, os.X_OK))

    def test_a_desktop_file_that_is_no_program_gets_a_launcher_that_opens_it(self):
        link = self.files / "site.desktop"
        link.write_text("[Desktop Entry]\nType=Link\nName=Site\nURL=https://example.org\n")
        self.assertEqual(self.send(link).returncode, 0)
        self.assertEqual(self.launched(self.desktop / "site.desktop")[-1], str(link))

    def test_the_folder_the_desktop_shows_is_used(self):
        settings = self.root / "shell.json"
        settings.write_text('{"config": {"desktop_files": {"folder": "~/Elsewhere"}}}')
        self.environment["DESKTOP_SHELL_SETTINGS"] = str(settings)
        letter = self.files / "letter.txt"
        letter.write_text("hi")
        self.assertEqual(self.send(letter).returncode, 0)
        self.assertTrue((self.home / "Elsewhere" / "letter.desktop").is_file())

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


def windows_program_with_icon(*icons):
    """A PE file holding one icon group of (size, colour) pictures, laid out by hand from the PE format."""
    pngs = []
    for size, colour in icons:
        picture = io.BytesIO()
        Image.new("RGB", (size, size), colour).save(picture, "PNG")
        pngs.append(picture.getvalue())
    group = struct.pack("<HHH", 0, 1, len(icons)) + b"".join(
        struct.pack("<BBBBHHIH", size % 256, size % 256, 0, 0, 1, 32, len(png), number)
        for number, ((size, _), png) in enumerate(zip(icons, pngs), 1))
    blobs = pngs + [group]

    def directory(entries):
        return struct.pack("<IIHHHH", 0, 0, 0, 0, 0, len(entries)) + b"".join(entries)

    # root, two type folders, a language folder per blob, then data entries
    section_rva = 0x1000
    icon_type = 16 + 2 * 8
    group_type = icon_type + 16 + 8 * len(pngs)
    languages = group_type + 16 + 8
    data_entries = languages + 24 * len(blobs)
    at = data_entries + 16 * len(blobs)
    rsrc = directory([struct.pack("<II", 3, 0x80000000 | icon_type), struct.pack("<II", 14, 0x80000000 | group_type)])
    rsrc += directory([struct.pack("<II", number, 0x80000000 | (languages + 24 * (number - 1)))
                       for number in range(1, len(pngs) + 1)])
    rsrc += directory([struct.pack("<II", 1, 0x80000000 | (languages + 24 * len(pngs)))])
    rsrc += b"".join(directory([struct.pack("<II", 1033, data_entries + 16 * index)]) for index in range(len(blobs)))
    contents = b""
    for blob in blobs:
        rsrc += struct.pack("<IIII", section_rva + at + len(contents), len(blob), 0, 0)
        contents += blob + b"\0" * (-len(blob) % 4)
    rsrc += contents
    rsrc += b"\0" * (-len(rsrc) % 0x200)

    headers_size = 0x200
    dos = b"MZ" + b"\0" * 58 + struct.pack("<I", 64)
    coff = struct.pack("<HHIIIHH", 0x14C, 1, 0, 0, 0, 224, 0x0102)
    optional = struct.pack("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", 0x10B, 0, 0, 0, 0, 0, 0, 0, 0, 0x400000, 0x1000, 0x200,
                           4, 0, 0, 0, 4, 0, 0, section_rva + len(rsrc), headers_size, 0, 2, 0, 0, 0, 0, 0, 0, 16)
    directories = [(0, 0)] * 16
    directories[2] = (section_rva, len(rsrc))
    optional += b"".join(struct.pack("<II", *pair) for pair in directories)
    section = struct.pack("<8sIIIIIIHHI", b".rsrc", len(rsrc), section_rva, len(rsrc), headers_size, 0, 0, 0, 0,
                          0x40000040)
    head = (dos + b"PE\0\0" + coff + optional + section).ljust(headers_size, b"\0")
    return head + rsrc


if __name__ == "__main__":
    unittest.main()
