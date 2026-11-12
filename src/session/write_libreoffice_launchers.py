"""Starts LibreOffice as a Plasma program, the only way it shows KDE's file dialog.

    python write_libreoffice_launchers.py FOLDER [--off]

FOLDER comes before the system's programs on the PATH. Each of LibreOffice's
commands found behind it gets a launcher there. With --off, or without
LibreOffice's KDE plugin or KDE's platform theme for Qt, the launchers go and
it starts as it always did.
"""

import os
import shlex
import shutil
import sys
from pathlib import Path

from paths import QT_PLUGIN_DIR

NAMES = ("libreoffice", "soffice", "loffice", "lowriter", "localc", "loimpress", "lodraw", "lomath", "lobase", "loweb",
         "lofromtemplate")
MARK = "# Written by configure-desktop."
LAUNCHER = """#!/bin/sh
{mark} LibreOffice asks KDE for its file dialog only as a Plasma program.
OOO_FORCE_DESKTOP=plasma6 QT_QPA_PLATFORMTHEME=kde exec {program} "$@"
"""


def is_ours(path):
    try:
        return path.is_file() and MARK in path.read_text()
    except (OSError, UnicodeDecodeError):
        return False


def behind(folder):
    """The PATH without FOLDER, so a launcher never finds itself."""
    places = os.environ.get("PATH", "").split(os.pathsep)
    return os.pathsep.join(place for place in places if place and Path(place).resolve() != folder.resolve())


def has_kde_theme():
    return (QT_PLUGIN_DIR / "platformthemes" / "KDEPlasmaPlatformTheme6.so").exists()


def has_kde_plugin(path):
    program = shutil.which("libreoffice", path=path) or shutil.which("soffice", path=path)
    return bool(program) and (Path(program).resolve().parent / "libvclplug_kf6lo.so").exists()


def main():
    folder = Path(sys.argv[1])
    path = behind(folder)
    wanted = "--off" not in sys.argv[2:] and has_kde_plugin(path) and has_kde_theme()
    for name in NAMES:
        launcher = folder / name
        program = shutil.which(name, path=path) if wanted else None
        if launcher.exists() and not is_ours(launcher):
            continue
        if not program:
            launcher.unlink(missing_ok=True)
            continue
        folder.mkdir(parents=True, exist_ok=True)
        temporary = launcher.with_name(name + ".new")
        temporary.write_text(LAUNCHER.format(mark=MARK, program=shlex.quote(program)))
        temporary.chmod(0o755)
        temporary.replace(launcher)


if __name__ == "__main__":
    main()
