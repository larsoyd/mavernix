"""Gives LibreOffice's tabbed bar a rim in the dialog colour in place of its own tint.

    python link_libreoffice_style.py [THEME_FOLDER]

Each profile that has been started gets the setting unless it holds a value already.
Without THEME_FOLDER the setting goes again, and the program tints the rim itself.
"""

import os
import re
import sys
from pathlib import Path

# 255 is all dialog colour, 0 all the program's own
RIM = ('<item oor:path="/org.openoffice.Office.Common/Misc"><prop oor:name="NotebookbarColorTransparency" oor:op="fuse">'
       "<value>255</value></prop></item>\n")
ANY_RIM = re.compile(r'oor:name="NotebookbarColorTransparency"')
END = "</oor:items>"


def registries():
    config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return sorted((config / "libreoffice").glob("*/user/registrymodifications.xcu"))


def write_whole(path, text):
    # the file may be the user's own link into a dotfiles folder
    target = path.resolve()
    temporary = target.with_name(target.name + ".new")
    temporary.write_text(text)
    temporary.replace(target)


def main():
    themed = len(sys.argv) > 1
    for registry in registries():
        text = registry.read_text()
        if themed and not ANY_RIM.search(text):
            write_whole(registry, text.replace(END, RIM + END, 1))
        elif not themed and RIM in text:
            write_whole(registry, text.replace(RIM, "", 1))


if __name__ == "__main__":
    main()
