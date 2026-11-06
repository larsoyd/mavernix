"""Hands REAPER's menus to the desktop's menu bar and its file dialogs to the desktop's.

    python link_reaper_menu.py [--files FILES_PLUGIN] [PLUGIN]

REAPER gets a link to PLUGIN among its user plugins, and a line in its toolkit
theme that takes the height of its own menu bar away. The theme is read at
start and the plugin cannot change it, so the two come and go together: without
PLUGIN both go, and the next start of REAPER has its bar again.

FILES_PLUGIN gets a link of its own beside it, and goes when it is not given.
"""

import argparse
import os
import sys
from pathlib import Path

PLUGIN_NAME = "reaper_mavernix_menu.so"
FILES_NAME = "reaper_mavernix_files.so"
THEME_NAME = "libSwell-user.colortheme"
HEIGHT_NAME = "menubar_height"
EMPTY_BAR = f"{HEIGHT_NAME} 0\n"


def config_folder():
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "REAPER"


def users_own(link, keeps):
    if link.exists() and not link.is_symlink():
        print(f"link_reaper_menu: {link} is a file of the user's own, so REAPER keeps {keeps}.", file=sys.stderr)
        return True
    return False


def place(link, plugin):
    link.parent.mkdir(exist_ok=True)
    link.unlink(missing_ok=True)
    link.symlink_to(plugin)


def link_files(folder, plugin):
    link = folder / "UserPlugins" / FILES_NAME
    if plugin is None:
        if link.is_symlink():
            link.unlink()
    elif not users_own(link, "its own file dialogs"):
        place(link, plugin)


def link_plugin(folder, plugin):
    link = folder / "UserPlugins" / PLUGIN_NAME
    if users_own(link, "its menu bar"):
        return
    theme = folder / THEME_NAME
    text = theme.read_text() if theme.exists() else ""
    if HEIGHT_NAME in text and EMPTY_BAR not in text:
        return
    place(link, plugin)
    if EMPTY_BAR not in text:
        theme.write_text(text + ("\n" if text and not text.endswith("\n") else "") + EMPTY_BAR)


def unlink_plugin(folder):
    link = folder / "UserPlugins" / PLUGIN_NAME
    if link.is_symlink():
        link.unlink()
    theme = folder / THEME_NAME
    if not theme.exists():
        return
    text = theme.read_text().replace(EMPTY_BAR, "")
    if text:
        theme.write_text(text)
    else:
        theme.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--files")
    parser.add_argument("plugin", nargs="?")
    arguments = parser.parse_args()
    folder = config_folder()
    if not folder.is_dir():
        return
    link_files(folder, arguments.files)
    if arguments.plugin:
        link_plugin(folder, arguments.plugin)
    else:
        unlink_plugin(folder)


if __name__ == "__main__":
    main()
