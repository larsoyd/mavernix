"""Nintendo 64 ROMs get their game's logo on a cartridge as a thumbnail."""

import hashlib
import importlib.util
import io
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from PIL import Image

SCRIPT = Path(__file__).resolve().parent.parent / "src" / "session" / "n64_art.py"
spec = importlib.util.spec_from_file_location("n64_art", SCRIPT)
n64_art = importlib.util.module_from_spec(spec)
spec.loader.exec_module(n64_art)

DAT_URL = "http://dat.test/n64.dat"
ART_URL = "http://art.test/n64"


def z64_rom(name=b"SUPER MARIO 64", serial=b"NSME", filler=b"\x11\x22\x33\x44"):
    header = bytearray(0x40)
    header[0:4] = b"\x80\x37\x12\x40"
    header[0x20:0x34] = name.ljust(20, b" ")
    header[0x3B:0x3F] = serial
    return bytes(header) + filler * 1024


def as_v64(z64):
    return b"".join(z64[i + 1:i + 2] + z64[i:i + 1] for i in range(0, len(z64), 2))


def as_n64(z64):
    return b"".join(z64[i:i + 4][::-1] for i in range(0, len(z64), 4))


def dat_entry(name, serial, sha1):
    return (f'game (\n\tname "{name}"\n\tregion "USA"\n\tserial "{serial}"\n'
            f'\trom ( name "{name}.z64" size 8388608 crc 3CE60709 md5 20B854B239203BAF6C961B850A4A51A2 '
            f'sha1 {sha1.upper()} serial "{serial}" )\n)\n\n')


def png(size, colour, mode="RGBA"):
    buffer = io.BytesIO()
    Image.new(mode, size, colour).save(buffer, "PNG")
    return buffer.getvalue()


def title_screen():
    screen = Image.new("RGB", (320, 240), (0, 0, 0))
    screen.paste((180, 180, 180), (20, 8, 120, 16))
    screen.paste((250, 40, 40), (60, 40, 260, 100))
    screen.paste((200, 200, 200), (120, 200, 200, 208))
    buffer = io.BytesIO()
    screen.save(buffer, "PNG")
    return buffer.getvalue()


class Network:
    def __init__(self, files):
        self.files = files
        self.asked = []

    def __call__(self, url, timeout):
        self.asked.append(url)
        if url not in self.files:
            raise n64_art.Missing(url)
        return self.files[url]


class Header(unittest.TestCase):
    def test_every_byte_order_reads_as_the_same_game_and_hash(self):
        z64 = z64_rom()
        for data in (z64, as_v64(z64), as_n64(z64)):
            rom = n64_art.read_rom(data)
            self.assertEqual((rom.name, rom.serial), ("SUPER MARIO 64", "NSME"))
            self.assertEqual(rom.sha1, hashlib.sha1(z64).hexdigest())

    def test_a_file_that_is_no_rom_is_refused_with_its_first_word(self):
        with self.assertRaisesRegex(ValueError, "deadbeef"):
            n64_art.read_rom(b"\xde\xad\xbe\xef" + bytes(0x40))


class Naming(unittest.TestCase):
    def setUp(self):
        self.rom = n64_art.read_rom(z64_rom())
        self.dat = (dat_entry("Super Mario 64 (USA)", "NSME", "9bef1128717f958171a4afac3ed78ee2bb4e86ce")
                    + dat_entry("Some Game (USA)", "NXXE", self.rom.sha1)
                    + dat_entry("Super Mario 64 (USA) (Rev 1)", "NSME", "0" * 40))

    def test_a_known_dump_is_named_by_its_hash(self):
        self.assertEqual(n64_art.game_name(self.rom, self.dat), ("Some Game (USA)", "sha1"))

    def test_a_changed_rom_is_named_by_the_first_release_with_its_serial(self):
        changed = n64_art.read_rom(z64_rom(filler=b"\x55\x66\x77\x88"))
        self.assertEqual(n64_art.game_name(changed, self.dat), ("Super Mario 64 (USA)", "serial"))

    def test_an_unknown_game_has_no_name(self):
        stranger = n64_art.read_rom(z64_rom(serial=b"NZZE", filler=b"\x01\x02\x03\x04"))
        self.assertEqual(n64_art.game_name(stranger, self.dat), (None, "header"))

    def test_reserved_characters_in_a_name_become_underscores_in_its_picture_name(self):
        self.assertEqual(n64_art.picture_name("Mario & Luigi: A/B? <C> *D* |E| \"F\" `G` \\H"),
                         "Mario _ Luigi_ A_B_ _C_ _D_ _E_ _F_ _G_ _H.png")


class LabelArt(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.cache = Path(self.scratch.name)

    def tearDown(self):
        self.scratch.cleanup()

    def art(self, network, name="Some Game (USA)"):
        return n64_art.label_art(name, self.cache, ART_URL, network, time.monotonic() + 10)

    def test_the_logo_comes_first(self):
        network = Network({f"{ART_URL}/Named_Logos/Some%20Game%20%28USA%29.png": png((64, 32), (0, 0, 255, 255)),
                           f"{ART_URL}/Named_Titles/Some%20Game%20%28USA%29.png": title_screen()})
        picture, kind = self.art(network)
        self.assertEqual(kind, "logo")
        self.assertEqual(picture.size, (64, 32))

    def test_a_logo_that_is_a_link_is_followed_to_its_target(self):
        network = Network({f"{ART_URL}/Named_Logos/Some%20Game%20%28USA%29.png": b"Some Game (Europe).png",
                           f"{ART_URL}/Named_Logos/Some%20Game%20%28Europe%29.png": png((48, 16), (0, 255, 0, 255))})
        picture, kind = self.art(network)
        self.assertEqual((kind, picture.size), ("logo", (48, 16)))

    def test_without_a_logo_the_title_screen_is_cut_to_its_largest_band(self):
        network = Network({f"{ART_URL}/Named_Titles/Some%20Game%20%28USA%29.png": title_screen()})
        picture, kind = self.art(network)
        self.assertEqual((kind, picture.size), ("title", (200, 60)))
        self.assertEqual(picture.convert("RGB").getpixel((100, 30)), (250, 40, 40))

    def test_no_request_outlasts_the_deadline(self):
        timeouts = []

        def stalled(url, timeout):
            timeouts.append(timeout)
            time.sleep(timeout)
            raise TimeoutError(url)
        started = time.monotonic()
        self.assertEqual(n64_art.label_art("Some Game (USA)", self.cache, ART_URL, stalled, started + 0.4), (None, "text"))
        self.assertLess(time.monotonic() - started, 0.8)
        self.assertTrue(all(0 < timeout <= 0.4 for timeout in timeouts), timeouts)

    def test_nothing_is_asked_once_the_deadline_has_passed(self):
        network = Network({})
        self.assertEqual(n64_art.label_art("Some Game (USA)", self.cache, ART_URL, network, time.monotonic() - 1),
                         (None, "text"))
        self.assertEqual(network.asked, [])

    def test_art_is_fetched_once_per_game(self):
        network = Network({f"{ART_URL}/Named_Logos/Some%20Game%20%28USA%29.png": png((64, 32), (0, 0, 255, 255))})
        self.art(network)
        self.art(network)
        self.assertEqual(len(network.asked), 1)

    def test_a_game_without_art_is_not_asked_for_again_within_a_day(self):
        network = Network({})
        self.assertEqual(self.art(network), (None, "text"))
        self.assertEqual(self.art(network), (None, "text"))
        self.assertEqual(len(network.asked), 2)
        for miss in self.cache.glob("*.none"):
            old = time.time() - 25 * 3600
            os.utime(miss, (old, old))
        self.art(network)
        self.assertEqual(len(network.asked), 4)

    def test_a_network_failure_is_not_remembered_as_a_miss(self):
        def down(url, timeout):
            raise OSError("unreachable")
        self.assertEqual(self.art(down), (None, "text"))
        self.assertEqual(list(self.cache.glob("*.none")), [])


class Catalogue(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.cache = Path(self.scratch.name)

    def tearDown(self):
        self.scratch.cleanup()

    def test_the_catalogue_is_kept_for_a_month(self):
        network = Network({DAT_URL: b"fresh"})
        self.assertEqual(n64_art.catalogue(self.cache, DAT_URL, network, time.monotonic() + 10), "fresh")
        self.assertEqual(n64_art.catalogue(self.cache, DAT_URL, network, time.monotonic() + 10), "fresh")
        self.assertEqual(len(network.asked), 1)
        old = time.time() - 31 * 86400
        os.utime(self.cache / "n64.dat", (old, old))
        network.files[DAT_URL] = b"newer"
        self.assertEqual(n64_art.catalogue(self.cache, DAT_URL, network, time.monotonic() + 10), "newer")

    def test_an_old_catalogue_is_used_when_it_cannot_be_renewed(self):
        (self.cache / "n64.dat").write_text("old")
        old = time.time() - 31 * 86400
        os.utime(self.cache / "n64.dat", (old, old))
        self.assertEqual(n64_art.catalogue(self.cache, DAT_URL, Network({}), time.monotonic() + 10), "old")

    def test_without_any_catalogue_there_is_none(self):
        self.assertEqual(n64_art.catalogue(self.cache, DAT_URL, Network({}), time.monotonic() + 10), "")


class Thumbnail(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.rom = self.root / "game.z64"
        self.rom.write_bytes(z64_rom())
        self.cartridge = self.root / "cartridge.png"
        Image.new("RGBA", (512, 512), (128, 128, 128, 255)).save(self.cartridge)

    def tearDown(self):
        self.scratch.cleanup()

    def thumbnail(self, size, art_url):
        environment = {"PATH": os.environ["PATH"], "HOME": str(self.root), "XDG_CACHE_HOME": str(self.root / "cache"),
                       "MAVERNIX_N64_DAT_URL": f"{art_url}/n64.dat", "MAVERNIX_N64_ART_URL": art_url}
        output = self.root / "out.png"
        started = time.monotonic()
        result = subprocess.run([sys.executable, "-I", str(SCRIPT), "thumbnail", str(self.cartridge), str(self.rom),
                                 str(output), str(size)], env=environment, capture_output=True, text=True, timeout=30)
        return result, output, time.monotonic() - started

    def test_a_stalled_server_gives_the_header_name_label_within_the_deadline(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        held = []

        def hold():
            try:
                while True:
                    held.append(listener.accept())
            except OSError:
                pass
        threading.Thread(target=hold, daemon=True).start()
        try:
            result, output, seconds = self.thumbnail(128, f"http://127.0.0.1:{listener.getsockname()[1]}")
        finally:
            for peer, _ in held:
                peer.shutdown(socket.SHUT_RDWR)
                peer.close()
            listener.shutdown(socket.SHUT_RDWR)
            listener.close()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLess(seconds, n64_art.TOTAL_SECONDS + 3)
        self.assertIn("source=header art=text", result.stderr)
        with Image.open(output) as picture:
            self.assertEqual(picture.size, (128, 128))
            self.assertEqual(picture.mode, "RGBA")

    def test_the_label_is_drawn_inside_the_cartridge(self):
        result, output, _ = self.thumbnail(256, "http://127.0.0.1:9")
        self.assertEqual(result.returncode, 0, result.stderr)
        with Image.open(output) as picture:
            left, top, right, bottom = (round(edge * 256) for edge in n64_art.LABEL)
            self.assertNotEqual(picture.getpixel(((left + right) // 2, (top + bottom) // 2)), (128, 128, 128, 255))
            self.assertEqual(picture.getpixel((2, 2)), (128, 128, 128, 255))
            self.assertEqual(picture.getpixel((left, top)), (128, 128, 128, 255), "the label's corners are round")

    def test_the_widest_header_name_keeps_off_the_label_edges(self):
        self.rom.write_bytes(z64_rom(name=b"W" * 20, serial=b"NZZE"))
        result, output, _ = self.thumbnail(256, "http://127.0.0.1:9")
        self.assertEqual(result.returncode, 0, result.stderr)
        with Image.open(output) as picture:
            left, top, right, bottom = (round(edge * 256) for edge in n64_art.LABEL)
            inset = round(min(right - left, bottom - top) * n64_art.LABEL_INSET)
            margins = [picture.getpixel((x, y)) for y in range(top + inset, bottom - inset)
                       for x in (*range(left + 1, left + inset), *range(right - inset, right - 1))]
            self.assertEqual(set(margins), {n64_art.PAPER})

    def test_a_file_that_is_no_rom_fails_without_writing(self):
        self.rom.write_bytes(b"\0" * 4096)
        result, output, _ = self.thumbnail(128, "http://127.0.0.1:9")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refused", result.stderr)
        self.assertFalse(output.exists())


class DolphinPreviews(unittest.TestCase):
    RC = "[General]\nVersion=202\n\n[PreviewSettings]\nPlugins=imagethumbnail,svgthumbnail\n\n[MainWindow]\nState=x\n"

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.rc = self.root / "dolphinrc"
        self.marker = self.root / "state" / "n64-previews-added"

    def tearDown(self):
        self.scratch.cleanup()

    def enable(self):
        return subprocess.run([sys.executable, "-I", str(SCRIPT), "enable-in-dolphin", str(self.rc), str(self.marker)],
                              capture_output=True, text=True, timeout=20)

    def test_an_explicit_list_gets_the_thumbnailer_once(self):
        self.rc.write_text(self.RC)
        self.assertEqual(self.enable().returncode, 0)
        self.assertEqual(self.rc.read_text(), self.RC.replace("svgthumbnail\n", "svgthumbnail,mavernix-n64\n"))
        self.assertTrue(self.marker.exists())

    def test_a_list_that_has_it_already_keeps_one(self):
        listed = self.RC.replace("svgthumbnail\n", "svgthumbnail,mavernix-n64\n")
        self.rc.write_text(listed)
        self.assertEqual(self.enable().returncode, 0)
        self.assertEqual(self.rc.read_text(), listed)

    def test_a_list_the_user_took_it_out_of_stays_as_they_left_it(self):
        self.rc.write_text(self.RC)
        self.enable()
        self.rc.write_text(self.RC)
        self.enable()
        self.assertEqual(self.rc.read_text(), self.RC)

    def test_without_a_list_dolphin_runs_it_already_and_nothing_is_written(self):
        text = "[General]\nVersion=202\n"
        self.rc.write_text(text)
        self.assertEqual(self.enable().returncode, 0)
        self.assertEqual(self.rc.read_text(), text)
        self.assertFalse(self.marker.exists())

    def test_without_a_dolphinrc_nothing_is_made(self):
        self.assertEqual(self.enable().returncode, 0)
        self.assertFalse(self.rc.exists())


if __name__ == "__main__":
    unittest.main()
