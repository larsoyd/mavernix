"""Points each GIMP config folder at the theme's title bar style.

    python link_gimp_style.py [THEME_FOLDER]

Each folder gets a link to the theme and an import at the top of gimp.css.
Without THEME_FOLDER both go, since GIMP refuses an import it cannot read.
"""

import os
import sys
from pathlib import Path

LINK_NAME = "mavernix"
IMPORT_LINE = f'@import url("{LINK_NAME}/gimp/titlebar.css");\n'


def config_folders():
    config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return sorted(path for path in (config / "GIMP").glob("*") if path.is_dir() and not path.is_symlink())


def write_whole(path, text):
    # the file may be the user's own link into a dotfiles folder
    target = path.resolve()
    temporary = target.with_name(target.name + ".new")
    temporary.write_text(text)
    temporary.replace(target)


def link_theme(folder, theme):
    link = folder / LINK_NAME
    if link.exists() and not link.is_symlink():
        print(f"link_gimp_style: {link} is a folder of the user's own, so GIMP keeps its title bar.", file=sys.stderr)
        return
    link.unlink(missing_ok=True)
    link.symlink_to(theme)
    style = folder / "gimp.css"
    text = style.read_text() if style.exists() else ""
    if IMPORT_LINE not in text:
        write_whole(style, IMPORT_LINE + text)


def unlink_theme(folder):
    link = folder / LINK_NAME
    if link.is_symlink():
        link.unlink()
    style = folder / "gimp.css"
    if style.exists() and IMPORT_LINE in style.read_text():
        write_whole(style, style.read_text().replace(IMPORT_LINE, ""))


def main():
    theme = sys.argv[1] if len(sys.argv) > 1 else None
    for folder in config_folders():
        if theme:
            link_theme(folder, theme)
        else:
            unlink_theme(folder)


if __name__ == "__main__":
    main()
