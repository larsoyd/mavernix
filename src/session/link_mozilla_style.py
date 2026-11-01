"""Points each Firefox and Thunderbird profile at the theme's title bar style.

    python link_mozilla_style.py [THEME_FOLDER]

Each profile gets chrome/mavernix, a link to the theme's folder, an import of
its style at the top of chrome/userChrome.css, and the setting that makes the
program read that file. Without THEME_FOLDER the links go; the import stays,
and a missing style does nothing.
"""

import configparser
import os
import sys
from pathlib import Path

LINK_NAME = "mavernix"
IMPORT_LINE = f'@import url("{LINK_NAME}/mozilla/titlebar.css");\n'
STYLE_SETTING = "toolkit.legacyUserProfileCustomizations.stylesheets"


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


def link_theme(profile, theme):
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
    if IMPORT_LINE not in text:
        write_whole(style, IMPORT_LINE + text)

    settings = profile / "user.js"
    text = settings.read_text() if settings.exists() else ""
    if STYLE_SETTING not in text:
        write_whole(settings, text + f'user_pref("{STYLE_SETTING}", true);\n')


def main():
    theme = sys.argv[1] if len(sys.argv) > 1 else None
    for folder in program_folders():
        for profile in profiles_of(folder):
            if theme:
                link_theme(profile, theme)
            else:
                (profile / "chrome" / LINK_NAME).unlink(missing_ok=True)


if __name__ == "__main__":
    main()
