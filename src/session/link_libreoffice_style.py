"""Sets what LibreOffice keeps in its own profile and no theme reaches.

    python link_libreoffice_style.py [THEME_FOLDER] [--colour-icons]

With THEME_FOLDER the tabbed bar gets a rim in the dialog colour in place of its
own tint. With --colour-icons it keeps its colour icons when started as a Plasma
program, where it would pick Breeze. Each profile that has been started gets a
setting unless it holds a value already, and loses it again when it is not asked for.
"""

import os
import sys
from pathlib import Path

END = "</oor:items>"


def item(name, value):
    return (f'<item oor:path="/org.openoffice.Office.Common/Misc"><prop oor:name="{name}" oor:op="fuse">'
            f"<value>{value}</value></prop></item>\n")


# 255 is all dialog colour, 0 all the program's own
RIM = item("NotebookbarColorTransparency", 255)
ICONS = item("SymbolStyle", "elementary")


def registries():
    config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return sorted((config / "libreoffice").glob("*/user/registrymodifications.xcu"))


def write_whole(path, text):
    # the file may be the user's own link into a dotfiles folder
    target = path.resolve()
    temporary = target.with_name(target.name + ".new")
    temporary.write_text(text)
    temporary.replace(target)


def settled(text, line, wanted):
    name = line.split('oor:name="')[1].split('"')[0]
    if wanted and f'oor:name="{name}"' not in text:
        return text.replace(END, line + END, 1)
    if not wanted and line in text:
        return text.replace(line, "", 1)
    return text


def main():
    arguments = sys.argv[1:]
    icons = "--colour-icons" in arguments
    themed = any(not argument.startswith("--") for argument in arguments)
    for registry in registries():
        text = registry.read_text()
        new = settled(settled(text, ICONS, icons), RIM, themed)
        if new != text:
            write_whole(registry, new)


if __name__ == "__main__":
    main()
