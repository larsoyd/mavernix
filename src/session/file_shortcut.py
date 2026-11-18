"""Writes a launcher that opens FILE from its own folder, with the file's own icon.

usage: file_shortcut.py FILE LAUNCHER NAME
"""

import hashlib
import io
import os
import re
import struct
import subprocess
import sys

import pefile
from PIL import Image

RT_ICON = 3
RT_GROUP_ICON = 14


def escaped(value):
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")


def quoted_argument(value):
    return '"' + re.sub(r'([`"$\\])', r"\\\1", value).replace("%", "%%") + '"'


def resources(pe, kind):
    root = getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None)
    for entry in root.entries if root else []:
        if entry.id == kind:
            for named in entry.directory.entries:
                for language in named.directory.entries:
                    data = language.data.struct
                    yield named.id, pe.get_data(data.OffsetToData, data.Size)


def windows_icon(path):
    """The first icon group of a Windows program as .ico bytes, or None."""
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories([pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    except (pefile.PEFormatError, OSError):
        return None
    pictures = dict(resources(pe, RT_ICON))
    group = next((data for _, data in resources(pe, RT_GROUP_ICON)), None)
    if group is None or len(group) < 6:
        return None
    count = struct.unpack_from("<H", group, 4)[0]
    entries, blobs = [], []
    offset = 6 + 16 * count
    for index in range(count):
        width, height, colours, _, planes, bits, _, picture_id = struct.unpack_from("<BBBBHHIH", group, 6 + 14 * index)
        blob = pictures.get(picture_id)
        if blob is None:
            return None
        entries.append(struct.pack("<BBBBHHII", width, height, colours, 0, planes, bits, len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    return struct.pack("<HHH", 0, 1, count) + b"".join(entries) + b"".join(blobs)


def saved_icon(ico):
    """The largest picture of ICO as a PNG in the user's data folder, or None."""
    try:
        picture = Image.open(io.BytesIO(ico))
        out = io.BytesIO()
        picture.convert("RGBA").save(out, "PNG")
    except (OSError, ValueError, SyntaxError):
        return None
    data = out.getvalue()
    folder = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"),
                          "mavernix", "shortcut-icons")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, hashlib.sha256(data).hexdigest()[:16] + ".png")
    with open(path, "wb") as file:
        file.write(data)
    return path


def kind_icon(path):
    shown = subprocess.run(["gio", "info", "-a", "standard::icon", path], capture_output=True, text=True,
                           timeout=10).stdout
    for line in shown.splitlines():
        if "standard::icon:" in line:
            return line.split(":", 3)[-1].split(",")[0].strip()
    return "application-x-generic"


def icon_for(path):
    ico = windows_icon(path)
    return (ico and saved_icon(ico)) or kind_icon(path)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    target, launcher, name = sys.argv[1:]
    lines = [
        "[Desktop Entry]",
        "Type=Application",
        "Name=" + escaped(name),
        "Icon=" + escaped(icon_for(target)),
        "Exec=" + escaped("gio open " + quoted_argument(target)),
        "Path=" + escaped(os.path.dirname(target)),
    ]
    with open(launcher, "x") as file:
        file.write("\n".join(lines) + "\n")
    os.chmod(launcher, 0o755)


if __name__ == "__main__":
    main()
