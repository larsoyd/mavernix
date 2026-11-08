"""Writes the theme's colour scheme, icon theme and font, and the config's
window buttons, where GTK reads them.

    python write_interface_settings.py SHELL-SETTINGS.json [--window-buttons-only]

GTK, the portal and others read these from dconf. start-desktop points
DCONF_PROFILE at a database of this session's own ("mavernix"), so Plasma's
choices do not show here and Plasma's store is not touched.

The button layout must be written: left out, GTK header bars get the
schema's default of a close button only. It comes from [gtk] button_side and
button_order, falling back to [window]. `--window-buttons-only` writes it
and the pointer alone, for mavernix-reconfigure.

Without `gsettings`, only the optional X11 settings file is written.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

INTERFACE_SCHEMA = "org.gnome.desktop.interface"
WINDOW_BUTTONS_SCHEMA = "org.gnome.desktop.wm.preferences"

BUTTON_SIDES = ("left", "right")

LIGHT_SCHEME = "prefer-light"
DARK_SCHEME = "prefer-dark"

# dconf's service is started on demand; if it hangs, the desktop must start anyway.
WRITE_TIME_LIMIT_SECONDS = 5

# Theme sizes are pixels; GTK font sizes are points at 96 pixels per inch.
POINTS_PER_PIXEL = 72 / 96


def say(message):
    print(f"write_interface_settings: {message}", file=sys.stderr)


def button_layout(config):
    """GTK's layout string, e.g. "close,minimize,maximize:" (left side) or
    ":minimize,maximize,close" (right). None for an unknown side."""
    gtk = config.get("gtk", {})
    window = config.get("window", {})
    side = gtk.get("button_side", window.get("button_side", "left"))
    buttons = gtk.get("button_order", window.get("button_order", ["close", "minimize", "maximize"]))
    if side not in BUTTON_SIDES:
        say(f'button_side is "{side}"; it must be "left" or "right", GTK programs keep their own layout')
        return None
    listed = ",".join(buttons)
    return f"{listed}:" if side == "left" else f":{listed}"


def wanted_window_button_values(config):
    """The keys of org.gnome.desktop.wm.preferences the config decides."""
    layout = button_layout(config)
    return {} if layout is None else {"button-layout": layout}


def wanted_interface_values(theme, config):
    """The org.gnome.desktop.interface values the theme decides."""
    values = {}
    # A theme without [controls] leaves applications alone.
    controls = theme.get("controls")
    if controls is not None:
        values["color-scheme"] = DARK_SCHEME if controls.get("dark", False) else LIGHT_SCHEME
        if config.get("gtk", {}).get("use_theme", True):
            values["gtk-theme"] = "DesktopTheme"
    icon_theme = theme.get("icons", {}).get("theme")
    if icon_theme:
        values["icon-theme"] = icon_theme
    cursors = theme.get("cursors", {})
    if cursors.get("theme"):
        values["cursor-theme"] = cursors["theme"]
    if cursors.get("size"):
        values["cursor-size"] = str(cursors["size"])
    for role, key in (("interface", "font-name"), ("document", "document-font-name"),
                      ("monospace", "monospace-font-name")):
        font = theme.get("fonts", {}).get(role)
        if font and font.get("family") and font.get("size"):
            points = round(font["size"] * POINTS_PER_PIXEL, 2)
            values[key] = f"{font['family']} {points:g}"
    return values


# Keys a theme may leave out that must not keep the last desktop's value:
# the session's database outlives a switch.
RESET_WHEN_UNSET = ["cursor-theme", "cursor-size", "gtk-theme"]
POINTER_KEYS = ["cursor-theme", "cursor-size"]


def write_xsettings(settings, path):
    interface = wanted_interface_values(settings["theme"], settings["config"])
    names = {"gtk-theme": "Net/ThemeName", "icon-theme": "Net/IconThemeName",
             "font-name": "Gtk/FontName", "cursor-theme": "Gtk/CursorThemeName",
             "cursor-size": "Gtk/CursorThemeSize"}
    values = {name: interface[key] for key, name in names.items() if key in interface}
    if "cursor-size" in interface:
        values["Gtk/CursorThemeSize"] = int(interface["cursor-size"])
    layout = button_layout(settings["config"])
    if layout is not None:
        values["Gtk/DecorationLayout"] = layout
    text = "".join(f"{key} {json.dumps(value, ensure_ascii=False)}\n" for key, value in values.items())
    temporary = path.with_suffix(".tmp")
    temporary.write_text(text)
    temporary.replace(path)


def reset_values(schema, keys):
    for key in keys:
        try:
            subprocess.run(["gsettings", "reset", schema, key], capture_output=True,
                           timeout=WRITE_TIME_LIMIT_SECONDS)
        except subprocess.TimeoutExpired:
            return


def write_values(schema, values):
    """Sets each key; False when dconf times out, so the caller can stop."""
    for key, value in values.items():
        try:
            result = subprocess.run(["gsettings", "set", schema, key, value], capture_output=True,
                                    text=True, timeout=WRITE_TIME_LIMIT_SECONDS)
        except subprocess.TimeoutExpired:
            say(f"setting {key} took longer than {WRITE_TIME_LIMIT_SECONDS} s; is dconf's service "
                "running (ca.desrt.dconf on the session bus)? GTK programs keep the user's own settings")
            return False
        if result.returncode != 0:
            say(f"could not set {key} to {value}: {result.stderr.strip()}")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("settings")
    parser.add_argument("--window-buttons-only", action="store_true")
    parser.add_argument("--xsettings-file", type=Path)
    arguments = parser.parse_args()
    with open(arguments.settings) as file:
        settings = json.load(file)
    if arguments.xsettings_file:
        write_xsettings(settings, arguments.xsettings_file)

    if shutil.which("gsettings") is None:
        say("gsettings is not installed (package glib2); GTK programs keep the user's own settings")
        return
    if not arguments.window_buttons_only:
        wanted = wanted_interface_values(settings["theme"], settings["config"])
        if not write_values(INTERFACE_SCHEMA, wanted):
            return
        reset_values(INTERFACE_SCHEMA, [key for key in RESET_WHEN_UNSET if key not in wanted])
    else:
        pointer = wanted_interface_values(settings["theme"], settings["config"])
        if not write_values(INTERFACE_SCHEMA, {key: pointer[key] for key in POINTER_KEYS if key in pointer}):
            return
    write_values(WINDOW_BUTTONS_SCHEMA, wanted_window_button_values(settings["config"]))


if __name__ == "__main__":
    main()
