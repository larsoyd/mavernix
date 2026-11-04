# Panel items

Panel items are the small icons at the end of the panel: sound, network, battery, and other programs' icons. Each one is a short QML file in `src/shell/status/`. The desktop config says which ones are shown, and in what order:

```toml
[panel]
right = ["tray", "bluetooth", "volume", "network", "battery", "clock"]
```

| Name | Shows | Needs |
|---|---|---|
| `tray` | the icons other programs put on the panel, in their own colours. Programs listed in `one_colour` under `[status.tray]` are drawn in the panel's colour instead, for an icon that is a plain light shape (Steam's, by default) | nothing; programs announce their icons themselves |
| `clipboard` | everything you copy, newest first: text, links, files and pictures in any format a program offers (kept as PNG, with a small preview). The menu has a search field, then the copies under Pinned and Recent, and scrolls once there are more than fit; resting on a copy shows all of it beside the menu, with when it was copied. Choosing one puts it back on the clipboard. Ctrl+1 to Ctrl+9 pick the first nine, Page Up and Page Down move a screenful, Ctrl+P pins or unpins the marked one, Alt+Backspace removes it, and a right click offers Copy as Plain Text. Pinned copies stay until removed. Option-click on the icon, or Pause Clipboard History, stops recording. Passwords from password managers are never kept. Super+V opens it (`clipboard_history` under `[shortcuts]`), or the dock's clipboard when the menu bar does not show this item. `[status.clipboard]` sets `keep_items` (pinned copies do not count), `remember`, `keep_images` and `image_limit_mb`, the largest copy kept; System Preferences, Clipboard, changes them. The same history can stand in the dock as a stack, `{ name = "Clipboard", icon = "mavernix-clipboard", clipboard = true }` in `[dock] groups`, or Add to Dock on the divider's menu. Its own menu shows it as a grid of previews or as a list, newest first, that scrolls (`view = "grid"` or `"list"` on the tile), and sets how much of the screen it may take: Small, Medium or Large (`size = "small"`, `"medium"`, the default, or `"large"`). Medium shows at most 4 by 3 previews or 12 lines and half the screen's height, so on a small screen the previews shrink, and on any screen the rest scrolls, a row per wheel notch. Resting on a copy in the grid or the list, or marking it with the keys, shows all of it beside the box after half a second, as the menu does (`preview_delay_ms` under `[status.clipboard]`) | a compositor with the data control protocol, such as Wayfire 0.11 |
| `bluetooth` | whether Bluetooth is on or off. The menu switches it, and lists the paired devices, each with a submenu that shows its charge and connects or disconnects it. While the menu is open, it looks for devices nearby and pairs the one you choose | BlueZ |
| `volume` | how loud the sound output is. The scroll wheel changes it. The menu has a slider for it, the output and input devices to choose from, Mute, and the sound settings | PipeWire |
| `network` | a cable, or the strength of the wireless signal. The menu turns Wi-Fi on and off and lists the networks in reach, with a lock on those that need a password and their signal strength; choosing one joins it, asking for the password if needed. A network program's tray icon (`tray_program` under `[status.network]`, NetworkManager's applet by default) is left out; with `menu = "program"` the menu is that program's whole menu instead | NetworkManager |
| `battery` | the charge, optionally with the percentage as text. The menu says how long it will last and what's powering the machine | UPower |
| `notifications` | opens and closes the list of past notifications | nothing |
| `search` | opens and closes the search for applications and files | nothing |

The clock isn't a status item, but it has a menu too. It shows the date, in the format set by `clock_menu_date_format` under `[panel]`.

Where the clock stands and what it shows are set under `[panel]` too, or in System Preferences, Date & Time:

| Key | Meaning |
|---|---|
| `clock_position` | `"right"` where the config lists it, `"left"` just after the logo, or `"middle"` of the bar. Menu titles that would run into a clock in the middle are left out |
| `clock_format` | the time, as Qt writes formats. A leading day of the week (`ddd`) also leads the date |
| `clock_shows_time`, `clock_shows_date` | either or both |
| `clock_date_style` | `"long"` (Tuesday January 13th), `"medium"` (Tue Jan. 13th) or `"short"` (13/01/2026, in the region's order). The long style writes the day of the week out |
| `clock_region_time` | the time in `LC_TIME`'s own format instead of `clock_format` |
| `clock_region_date` | the day and month in `LC_TIME`'s language (Tirsdag 13. januar), and its own short date (13.01.2026) |

Otherwise day and month names follow the desktop's language (`LANG`). Ordinals like 13th are English only, other languages get their own day and month.

If an item's service isn't running, or it has nothing to show (say, there's no battery), it takes up no room.

## Adding one without writing a file

If a command can print what you want to show, the config is all you need. Give a name of your own a command, and list that name:

```toml
[panel]
right = ["load", "clock"]

[status.load]
command = ["sh", "-c", "cut -d' ' -f1 /proc/loadavg"]
every_seconds = 10                       # optional: how often to run it again; without it, it runs once
icon = "utilities-system-monitor"        # optional: an icon after the text
click_command = ["foot", "-e", "top"]    # optional: what clicking it starts
```

The panel shows the first line the command prints, in the panel's own font and colour. (The component behind this is `src/shell/StatusFromConfig.qml`. It can also give the item a menu of commands.)

## Adding one as a file

1. Write `NAME.qml` in a folder of your own, and list that folder in the config:

   ```toml
   [panel]
   item_folders = ["/home/me/.config/mavernix/status"]   # a full path
   ```

   Folders from the config are searched first, so a file there with the same name as a built-in item replaces it.

2. Put `"NAME"` into `left` or `right` in the config.

The file is a `StatusItem` (`src/shell/StatusItem.qml`). Here's the smallest one:

```qml
import QtQuick
import qs            // the shell's own components

StatusItem {
    iconName: "network-wired"
}
```

What an item can set:

| Property | Meaning |
|---|---|
| `iconName` | the icon, named the way Linux desktops name icons, without "-symbolic": `audio-volume-high`, `network-wired`, `weather-clear` |
| `text` | a short text before the icon (or on its own, with no `iconName`) |
| `shown` | `false` while there's nothing to show |
| `menuItems` | a menu that opens when it's clicked. Each entry is `{ id, label, enabled, separator, checkable, checked, shortcut: [], children: [] }`. An entry with `slider: VALUE` (0 to 1, and `enabled: false`) is drawn as a slider. Don't change the items while the slider is being dragged (see `src/shell/status/volume.qml`) |
| `popover` | instead of a menu, a panel of its own in the menu's place: a component whose item sets its `implicitWidth` and `implicitHeight` ([extensions](extensions.md#a-panel-item)) |

What it can react to:

- `onChosen: id => …`: a menu entry was picked.
- `onSlid: (id, value) => …`: a slider in the menu was moved.
- `onOpening`: its menu is about to open. (`open` tells you whether the menu is open.)
- `onActivated`: it was clicked and has no menu.
- `onScrolled: notches => …`: the scroll wheel moved.

The item is handed `panel` and `itemRow`. `panel` is the panel window, and `panel.settings.config` is the desktop config, so an item can have settings of its own under `[status.NAME]`.

## How an item gets its look

An item never names a picture file. It names an icon, and the theme decides what that looks like:

1. If the theme lists a picture for that name in `[panel.status.icons]`, that picture is used.
2. Otherwise, the icon theme's one-colour icon with that name is used.

Either way, only the icon's shape is used. It's painted in the panel's text colour, and in the highlighted text colour while its menu is open. That way a new item fits every theme without the themes having to know about it. (`tray` is the exception: other programs' icons keep their own colours.)

Spacing and size come from `[panel.status]` in the theme ([theme format: panel](themes/panel.md)).
