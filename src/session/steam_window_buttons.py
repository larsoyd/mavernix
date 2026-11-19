"""Gives Steam's own window buttons the desktop theme's look, at the left.

    python steam_window_buttons.py STYLE_FILE

Every Steam window takes the sheet that steamui/index.html links, whose name
changes between builds. This puts a stub of the same size in its place that
imports Steam's sheet and the style. Steam checks only sizes when it starts,
but an update writes the sheet again or renames it, so this keeps checking
until the session ends.
"""

import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

FLAG_NAME = ".cef-enable-remote-debugging"
# a flag holding this was written by an older version, any other is the user's
OUR_MARK = "mavernix\n"
HEADER = b"/* mavernix window buttons */\n"
KEPT = ".mavernix-original.css"
LINK = re.compile(rb"<link\b[^>]*>")
STYLESHEET = re.compile(rb"""rel=["']stylesheet["']""")
SHEET_NAME = re.compile(rb"""href=["']/css/([^"'/?]+\.css)["'?]""")
# steam stamps the package's own time on what it writes, so a fresh write shows only in ctime
SETTLE_SECONDS = 1
POLL_SECONDS = 0.5
QUIET = ("unchanged", "settling")


class SessionOver(Exception):
    pass


def log(message):
    print(f"[steam-buttons] {message}", file=sys.stderr, flush=True)


def check_session():
    session = os.environ.get("DESKTOP_SESSION_PID")
    if not session:
        return
    try:
        os.kill(int(session), 0)
    except ProcessLookupError:
        raise SessionOver from None
    except PermissionError:
        pass


def steam_folders():
    home = Path.home()
    data = Path(os.environ.get("XDG_DATA_HOME") or home / ".local" / "share")
    candidates = (data / "Steam", home / ".steam" / "steam", home / ".var/app/com.valvesoftware.Steam/.local/share/Steam")
    return [folder for folder in candidates if folder.is_dir()]


def take_back_debugging():
    for folder in steam_folders():
        flag = folder / FLAG_NAME
        if flag.is_file() and flag.read_text() == OUR_MARK:
            flag.unlink()
            log(f"result=debugging_off folder={json.dumps(str(folder))}")


def write(path, data, mode=None):
    temporary = path.with_name(f".{path.name}.mavernix")
    temporary.write_bytes(data)
    if mode is not None:
        temporary.chmod(mode)
    os.replace(temporary, path)


def lose_stub(sheet):
    # a sheet of another size fails steam's check at its next start, and steam writes it again
    sheet.write_bytes(HEADER)
    return "original_lost"


def linked_sheet(folder):
    """The first sheet of Steam's own that its page links, or None."""
    try:
        page = (folder / "steamui" / "index.html").read_bytes()
    except FileNotFoundError:
        return None
    for link in LINK.findall(page):
        name = SHEET_NAME.search(link)
        if name and STYLESHEET.search(link):
            sheet = folder / "steamui" / "css" / name.group(1).decode()
            return sheet if sheet.is_file() else None
    return None


def kept_copy(sheet):
    return sheet.with_name(sheet.name.removesuffix(".css") + KEPT)


def give_back(original):
    """Puts a kept sheet back over its stub, or drops it when Steam wrote or removed that sheet."""
    sheet = original.with_name(original.name.removesuffix(KEPT) + ".css")
    try:
        stubbed = sheet.read_bytes().startswith(HEADER)
    except FileNotFoundError:
        stubbed = False
    if not stubbed:
        original.unlink()
        return
    mode = sheet.stat().st_mode & 0o7777
    os.replace(original, sheet)
    sheet.chmod(mode)


def patch(folder, style):
    sheet = linked_sheet(folder)
    if sheet is None:
        return "no_sheet"
    original = kept_copy(sheet)
    ours = folder / "steamui" / "mavernix" / "window-buttons.css"
    for other in sheet.parent.glob("*" + KEPT):
        if other != original:
            give_back(other)
    stub = HEADER + f'@import url("{original.name}");\n@import url("../mavernix/window-buttons.css");\n'.encode()
    data = sheet.read_bytes()
    if data.startswith(HEADER):
        if not original.is_file():
            return lose_stub(sheet)
        if ours.is_file() and ours.read_text() == style:
            return "unchanged"
        write(ours, style.encode())
        return "style_updated"
    if data.startswith((b"/*", b"@import")):
        return "foreign_sheet"
    if len(stub) > len(data):
        return "sheet_too_small"
    if time.time() - sheet.stat().st_ctime < SETTLE_SECONDS:
        return "settling"
    ours.parent.mkdir(exist_ok=True)
    write(ours, style.encode())
    write(original, data)
    write(sheet, stub.ljust(len(data), b" "), sheet.stat().st_mode & 0o7777)
    return "patched"


def unpatch(folder):
    sheet = linked_sheet(folder)
    if sheet and sheet.read_bytes().startswith(HEADER) and not kept_copy(sheet).is_file():
        return lose_stub(sheet)
    result = "unchanged"
    for original in (folder / "steamui" / "css").glob("*" + KEPT):
        give_back(original)
        result = "restored"
    if (folder / "steamui" / "mavernix").exists():
        shutil.rmtree(folder / "steamui" / "mavernix")
        result = "restored"
    return result


def patch_every_steam(style, last):
    """Patches each Steam with the style, or gives each its sheet back when there is none."""
    for folder in steam_folders():
        try:
            result = patch(folder, style) if style is not None else unpatch(folder)
        except OSError as error:
            result = f"failed reason={json.dumps(str(error))}"
        if result != last.get(folder) and result not in QUIET:
            log(f"result={result} folder={json.dumps(str(folder))}")
        last[folder] = result


def read_style(path):
    try:
        return Path(path).read_text()
    except OSError:
        return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    last = {}
    try:
        while True:
            check_session()
            patch_every_steam(read_style(sys.argv[1]), last)
            time.sleep(POLL_SECONDS)
    except SessionOver:
        log("result=session_over")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
