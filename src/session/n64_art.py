"""Thumbnails for Nintendo 64 ROMs: the game's logo on a cartridge label.

usage: n64_art.py thumbnail CARTRIDGE ROM OUTPUT SIZE
       n64_art.py enable-in-dolphin DOLPHINRC MARKER

The game is named by the ROM's hash in the No-Intro catalogue, or by the serial
in its header when the ROM was changed. Its logo, or its title screen cut to the
logo, comes from the libretro thumbnail collection and is kept in the cache.
Without either the label shows the name from the header.
"""

import collections
import hashlib
import io
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

DAT_URL = os.environ.get("MAVERNIX_N64_DAT_URL", "https://raw.githubusercontent.com/libretro/libretro-database/"
                         "master/metadat/no-intro/Nintendo%20-%20Nintendo%2064.dat")
ART_URL = os.environ.get("MAVERNIX_N64_ART_URL",
                         "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Nintendo_64/master")
REQUEST_SECONDS = 5
TOTAL_SECONDS = 10
CATALOGUE_DAYS = 30
MISS_HOURS = 24
# the label's box and corner radius on the cartridge picture, as parts of its side
LABEL = (0.2516, 0.3095, 0.7484, 0.6925)
LABEL_RADIUS = 0.0228
LABEL_INSET = 0.06
PAPER = (244, 242, 236, 255)
INK = (40, 40, 40, 255)
PLUGIN = "mavernix-n64"

Rom = collections.namedtuple("Rom", "name serial sha1")


class Missing(Exception):
    pass


def big_endian(data):
    first = data[:4]
    if first == b"\x80\x37\x12\x40":
        return data
    if first == b"\x37\x80\x40\x12":
        swapped = bytearray(len(data))
        swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
        return bytes(swapped)
    if first == b"\x40\x12\x37\x80":
        swapped = bytearray(len(data))
        for offset in range(4):
            swapped[offset::4] = data[3 - offset::4]
        return bytes(swapped)
    raise ValueError(f"not a nintendo 64 rom, first word {first.hex()}")


def read_rom(data):
    data = big_endian(data[:len(data) - len(data) % 4])
    name = data[0x20:0x34].decode("shift_jis", errors="replace").strip(" \0")
    return Rom(name, data[0x3B:0x3F].decode("ascii", errors="replace"), hashlib.sha1(data).hexdigest())


def game_name(rom, dat):
    by_serial = {}
    for block in re.findall(r"^game \(\n(.*?)^\)$", dat, re.M | re.S):
        name = re.search(r'^\tname "(.*)"$', block, re.M)
        if not name:
            continue
        if rom.sha1 in (sha1.lower() for sha1 in re.findall(r"\bsha1 ([0-9A-Fa-f]{40})\b", block)):
            return name[1], "sha1"
        for serial in re.findall(r'\bserial "([^"]+)"', block):
            by_serial.setdefault(serial, name[1])
    if rom.serial in by_serial:
        return by_serial[rom.serial], "serial"
    return None, "header"


def picture_name(name):
    return re.sub(r'[&*/:`<>?\\|"]', "_", name) + ".png"


def download(url, timeout):
    request = urllib.request.Request(url, headers={"User-Agent": "mavernix-n64-art"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        if error.code == 404:
            raise Missing(url) from error
        raise


def left(deadline):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("past the deadline")
    return min(REQUEST_SECONDS, remaining)


def catalogue(cache, url, fetch, deadline):
    path = cache / "n64.dat"
    if path.exists() and time.time() - path.stat().st_mtime < CATALOGUE_DAYS * 86400:
        return path.read_text(encoding="utf-8", errors="replace")
    try:
        data = fetch(url, left(deadline))
        cache.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    except (OSError, Missing, ValueError):
        pass
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def collection_picture(base, folder, name, fetch, deadline):
    url = f"{base}/{folder}/{urllib.parse.quote(picture_name(name))}"
    data = fetch(url, left(deadline))
    if not data.startswith(b"\x89PNG") and len(data) < 512 and data.endswith(b".png"):
        # a git link in the collection arrives as the name of the file it points at
        url = f"{base}/{folder}/{urllib.parse.quote(data.decode('utf-8'))}"
        data = fetch(url, left(deadline))
    return Image.open(io.BytesIO(data))


def largest_band(picture):
    lit = picture.convert("L").point(lambda value: 255 if value > 24 else 0)
    width, height = lit.size
    rows = [lit.crop((0, y, width, y + 1)).getbbox() is not None for y in range(height)]
    bands, start = [], None
    for y, filled in enumerate(rows + [False]):
        if filled and start is None:
            start = y
        elif not filled and start is not None:
            bands.append((start, y))
            start = None
    if not bands:
        return None
    top, bottom = max(bands, key=lambda band: lit.crop((0, band[0], width, band[1])).histogram()[255])
    box = lit.crop((0, top, width, bottom)).getbbox()
    return picture.crop((box[0], top + box[1], box[2], top + box[3]))


def label_art(name, cache, base, fetch, deadline):
    cache.mkdir(parents=True, exist_ok=True)
    stem = cache / picture_name(name)[:-4]
    for kind in ("logo", "title"):
        kept = stem.with_name(f"{stem.name}.{kind}.png")
        if kept.exists():
            with Image.open(kept) as picture:
                return picture.convert("RGBA"), kind
    miss = stem.with_name(f"{stem.name}.none")
    if miss.exists() and time.time() - miss.stat().st_mtime < MISS_HOURS * 3600:
        return None, "text"
    try:
        for kind, folder in (("logo", "Named_Logos"), ("title", "Named_Titles")):
            try:
                picture = collection_picture(base, folder, name, fetch, deadline)
            except Missing:
                continue
            picture = picture.convert("RGBA") if kind == "logo" else largest_band(picture.convert("RGB"))
            if picture is None:
                continue
            picture.save(stem.with_name(f"{stem.name}.{kind}.png"))
            return picture, kind
    except (OSError, ValueError):
        return None, "text"
    miss.touch()
    return None, "text"


def label_font(size):
    try:
        found = subprocess.run(["fc-match", "-f", "%{file}", "Lucida Grande:bold"], capture_output=True, text=True,
                               timeout=5).stdout
        return ImageFont.truetype(found, size)
    except (OSError, subprocess.SubprocessError):
        return ImageFont.load_default(size)


def name_label(text, size, inner):
    label = Image.new("RGBA", size, PAPER)
    draw = ImageDraw.Draw(label)
    points = max(1, inner[1] // 3)
    font = label_font(points)
    while points > 1 and draw.textlength(text, font=font) > inner[0]:
        points -= 1
        font = label_font(points)
    draw.text((size[0] / 2, size[1] / 2), text, font=font, fill=INK, anchor="mm")
    return label


def fitted(picture, size):
    scale = min(size[0] / picture.width, size[1] / picture.height)
    return picture.resize((max(1, round(picture.width * scale)), max(1, round(picture.height * scale))),
                          Image.LANCZOS)


def thumbnail(cartridge, art, kind, header_name, side):
    picture = Image.open(cartridge).convert("RGBA").resize((side, side), Image.LANCZOS)
    left_edge, top, right, bottom = (round(edge * side) for edge in LABEL)
    box = (right - left_edge, bottom - top)
    inset = round(min(box) * LABEL_INSET)
    inner = (max(1, box[0] - 2 * inset), max(1, box[1] - 2 * inset))
    if art is None:
        label = name_label(header_name, box, inner)
    else:
        label = Image.new("RGBA", box, (0, 0, 0, 255) if kind == "title" else PAPER)
        drawn = fitted(art.convert("RGBA"), inner)
        label.alpha_composite(drawn, ((box[0] - drawn.width) // 2, (box[1] - drawn.height) // 2))
    corners = Image.new("L", box, 0)
    ImageDraw.Draw(corners).rounded_rectangle((0, 0, box[0] - 1, box[1] - 1), round(LABEL_RADIUS * side), fill=255)
    picture.paste(label, (left_edge, top), corners)
    return picture


def make_thumbnail(cartridge, rom_path, output, side):
    started = time.monotonic()
    deadline = started + TOTAL_SECONDS
    try:
        rom = read_rom(Path(rom_path).read_bytes())
    except (OSError, ValueError) as error:
        print(f"[n64-art] refused: {error} rom={rom_path}", file=sys.stderr)
        return 1
    cache = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "mavernix" / "n64"
    name, source = game_name(rom, catalogue(cache, DAT_URL, download, deadline))
    art, kind = (None, "text") if name is None else label_art(name, cache, ART_URL, download, deadline)
    thumbnail(cartridge, art, kind, rom.name, side).save(output, "PNG")
    milliseconds = round((time.monotonic() - started) * 1000)
    print(f"[n64-art] rom={rom_path} game={name or rom.name} source={source} "
          f"art={kind} ms={milliseconds}", file=sys.stderr)
    return 0


def enable_in_dolphin(rc_path, marker):
    rc, marker = Path(rc_path), Path(marker)
    if marker.exists() or not rc.exists():
        return 0
    text = rc.read_text()
    section = re.search(r"^\[PreviewSettings\]\n((?:(?!\[).*\n?)*)", text, re.M)
    plugins = section and re.search(r"^Plugins=(.*)$", section[1], re.M)
    if not plugins:
        return 0
    if PLUGIN not in plugins[1].split(","):
        at = section.start(1) + plugins.end(1)
        rc.write_text(text[:at] + f",{PLUGIN}" + text[at:])
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.touch()
    print(f"[n64-art] dolphin previews plugin={PLUGIN} added", file=sys.stderr)
    return 0


def main(arguments):
    if len(arguments) == 5 and arguments[0] == "thumbnail":
        return make_thumbnail(arguments[1], arguments[2], arguments[3], int(arguments[4]))
    if len(arguments) == 3 and arguments[0] == "enable-in-dolphin":
        return enable_in_dolphin(arguments[1], arguments[2])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
