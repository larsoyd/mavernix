"""Hands REAPER's menus to the desktop's menu bar.

    python link_reaper_menu.py [PLUGIN]

REAPER gets a link to PLUGIN among its user plugins, and a line in its toolkit
theme that takes the height of its own menu bar away. The theme is read at
start and the plugin cannot change it, so the two come and go together: without
PLUGIN both go, and the next start of REAPER has its bar again.
"""

import os
import sys
from pathlib import Path

PLUGIN_NAME = "reaper_mavernix_menu.so"
THEME_NAME = "libSwell-user.colortheme"
HEIGHT_NAME = "menubar_height"
EMPTY_BAR = f"{HEIGHT_NAME} 0\n"


def config_folder():
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "REAPER"


def link_plugin(folder, plugin):
    link = folder / "UserPlugins" / PLUGIN_NAME
    if link.exists() and not link.is_symlink():
        print(f"link_reaper_menu: {link} is a file of the user's own, so REAPER keeps its menu bar.", file=sys.stderr)
        return
    theme = folder / THEME_NAME
    text = theme.read_text() if theme.exists() else ""
    if HEIGHT_NAME in text and EMPTY_BAR not in text:
        return
    link.parent.mkdir(exist_ok=True)
    link.unlink(missing_ok=True)
    link.symlink_to(plugin)
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
    folder = config_folder()
    if not folder.is_dir():
        return
    if len(sys.argv) > 1:
        link_plugin(folder, sys.argv[1])
    else:
        unlink_plugin(folder)


if __name__ == "__main__":
    main()
