"""Points each Firefox and Thunderbird profile at the theme's styles.

    python link_mozilla_style.py [THEME_FOLDER]

Each profile gets chrome/mavernix, a link to the theme's folder, imports of
its styles at the top of chrome/userChrome.css, and the setting that makes the
program read that file. Without THEME_FOLDER the links go; the imports stay,
and a missing style does nothing.

The program's menus go to the desktop's menu bar through two settings and one
of those styles, unless MAVERNIX_GLOBAL_MENU=0. A setting stays in the program
after its line goes, so turning them off writes false. Lines ending in
OUR_MARK are this script's, and any other line of those settings is the user's.
"""

import configparser
import os
import sys
from pathlib import Path

LINK_NAME = "mavernix"
IMPORT_LINE = f'@import url("{LINK_NAME}/mozilla/titlebar.css");\n'
MENU_IMPORT_LINE = f'@import url("{LINK_NAME}/mozilla/global-menu.css");\n'
STYLE_SETTING = "toolkit.legacyUserProfileCustomizations.stylesheets"
MENU_SETTINGS = ("widget.gtk.global-menu.enabled", "widget.gtk.global-menu.wayland.enabled")
OUR_MARK = " // mavernix"


def program_folders():
    config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return [config / "mozilla" / "firefox", Path.home() / ".mozilla" / "firefox",
            config / "thunderbird", Path.home() / ".thunderbird"]


def profiles_of(folder):
    listing = configparser.RawConfigParser(strict=False)
    listing.read(folder / "profiles.ini")
    for name in listing.sections():
        if name.startswith("Profile") and "path" in listing[name]:
            profile = folder / listing[name]["path"]
            if profile.is_dir():
                yield profile


def write_whole(path, text):
    # the file may be the user's own link into a dotfiles folder
    target = path.resolve()
    temporary = target.with_name(target.name + ".new")
    temporary.write_text(text)
    temporary.replace(target)


def with_menu_settings(text, wanted):
    others = "".join(line for line in text.splitlines(keepends=True) if not line.rstrip().endswith(OUR_MARK))
    if not wanted and others == text:
        return text
    value = "true" if wanted else "false"
    return others + "".join(f'user_pref("{name}", {value});{OUR_MARK}\n' for name in MENU_SETTINGS if name not in others)


def write_menu_settings(profile, wanted):
    settings = profile / "user.js"
    text = settings.read_text() if settings.exists() else ""
    changed = with_menu_settings(text, wanted)
    if changed != text:
        write_whole(settings, changed)


def link_theme(profile, theme, menus):
    chrome = profile / "chrome"
    link = chrome / LINK_NAME
    if link.exists() and not link.is_symlink():
        print(f"link_mozilla_style: {link} is a folder of the user's own, so this profile keeps its title bar.", file=sys.stderr)
        return
    chrome.mkdir(exist_ok=True)
    link.unlink(missing_ok=True)
    link.symlink_to(theme)

    style = chrome / "userChrome.css"
    text = style.read_text() if style.exists() else ""
    changed = text if IMPORT_LINE in text else IMPORT_LINE + text
    if menus and MENU_IMPORT_LINE not in changed:
        changed = MENU_IMPORT_LINE + changed
    elif not menus:
        changed = changed.replace(MENU_IMPORT_LINE, "")
    if changed != text:
        write_whole(style, changed)

    settings = profile / "user.js"
    text = settings.read_text() if settings.exists() else ""
    if STYLE_SETTING not in text:
        write_whole(settings, text + f'user_pref("{STYLE_SETTING}", true);\n')
    write_menu_settings(profile, menus)


def main():
    theme = sys.argv[1] if len(sys.argv) > 1 else None
    menus = os.environ.get("MAVERNIX_GLOBAL_MENU", "1") != "0"
    for folder in program_folders():
        for profile in profiles_of(folder):
            if theme:
                link_theme(profile, theme, menus)
            else:
                (profile / "chrome" / LINK_NAME).unlink(missing_ok=True)
                write_menu_settings(profile, False)


if __name__ == "__main__":
    main()
