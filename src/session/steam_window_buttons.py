"""Gives Steam's own window buttons the desktop theme's look, at the left.

    python steam_window_buttons.py STYLE_FILE

Every Steam window takes its sheets from steamui/css/library.css. This puts a
stub of the same size in its place that imports Steam's sheet and the style.
Steam checks only sizes when it starts, but an update writes the sheet again,
so this keeps checking until the session ends.
"""

import json
import os
import shutil
import sys
import time
from pathlib import Path

FLAG_NAME = ".cef-enable-remote-debugging"
# a flag holding this was written by an older version, any other is the user's
OUR_MARK = "mavernix\n"
HEADER = b"/* mavernix window buttons */\n"
ORIGINAL = "library.mavernix-original.css"
STUB = HEADER + f'@import url("{ORIGINAL}");\n@import url("../mavernix/window-buttons.css");\n'.encode()
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


def patch(folder, style):
    css = folder / "steamui" / "css"
    sheet, original = css / "library.css", css / ORIGINAL
    ours = folder / "steamui" / "mavernix" / "window-buttons.css"
    try:
        data = sheet.read_bytes()
    except FileNotFoundError:
        return "no_sheet"
    if data.startswith(HEADER):
        if not original.is_file():
            return lose_stub(sheet)
        if ours.is_file() and ours.read_text() == style:
            return "unchanged"
        write(ours, style.encode())
        return "style_updated"
    if data.startswith((b"/*", b"@import")):
        return "foreign_sheet"
    if len(STUB) > len(data):
        return "sheet_too_small"
    if time.time() - sheet.stat().st_ctime < SETTLE_SECONDS:
        return "settling"
    ours.parent.mkdir(exist_ok=True)
    write(ours, style.encode())
    write(original, data)
    write(sheet, STUB.ljust(len(data), b" "), sheet.stat().st_mode & 0o7777)
    return "patched"


def unpatch(folder):
    css = folder / "steamui" / "css"
    sheet, original = css / "library.css", css / ORIGINAL
    try:
        data = sheet.read_bytes()
    except FileNotFoundError:
        return "no_sheet"
    result = "unchanged"
    if data.startswith(HEADER):
        if not original.is_file():
            return lose_stub(sheet)
        mode = sheet.stat().st_mode & 0o7777
        os.replace(original, sheet)
        sheet.chmod(mode)
        result = "restored"
    elif original.exists():
        original.unlink()
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
