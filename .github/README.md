<h1 align="center">mavernix-larsoyd</h1>

<p align="center">
  <b>A personal branch of Mavernix, with the changes listed below.</b><br>
  <a href="https://github.com/larsoyd/mavernix/blob/main/README.md">Project README</a> · <a href="https://github.com/Mavernix/mavernix">Upstream</a> · <a href="https://github.com/larsoyd/mavernix/tree/main">main branch</a>
</p>

This is the `larsoyd` branch of a fork of [Mavernix](https://github.com/Mavernix/mavernix) with my own opinionated tweaks and additions. The `main` branch mirrors upstream exactly. This branch sits on top of the latest upstream release and is rebased onto each new one.

For what Mavernix is, how to install it and how to use it, read the [project README on main](https://github.com/larsoyd/mavernix/blob/main/README.md). This page only covers how this branch differs.

To build it, check out `larsoyd` and follow [docs/building.md](../docs/building.md). The release changes live in [CHANGELOG.md](../CHANGELOG.md).

## Changes on this branch

### Windows and tiling

- Focus follows mouse, shipped as an extension that loads a compositor plugin, even into a session that is already running.
- When a window is dragged toward the top, a plate of layouts appears with halves, thirds and quarters. Its look comes from the theme, its layouts from the config, and it has an on/off switch in Settings.
- Tiled windows share their edges. Holding the resize binding moves a shared edge, and a new tile takes the room its neighbours leave. Seams and resize margins show the split pointer.
- An extension hides title bars on maximized windows, including plain GTK ones.
- Windows that draw their own frame get the theme's rounded corners and shadow while they float.
- A window's shadow fades as the window gains or loses focus, and window pictures keep their shadow.
- A window that reaches onto another screen is drawn there too, where it can be clicked and is stacked by focus.
- The pointer hides while you type or after it rests.
- F3 and other game keys reach native Wayland games, set by a games list in the config.
- A window's name and icon come from the desktop entry that started it when its class names none.
- Login items marked Hide open minimized. Settings has a Hide column for them.

### Dock and app grid

- Digging at the bottom edge of any screen brings the dock to that screen, sliding it out of the old one as on OS X 10.9. This also works over full screen windows.
- An unkept program's tile grows in, bounces once, and shrinks away when the program quits. A closed minimised window's tile shrinks away too.
- Dock icons are scaled down with Lanczos, and a program's own icon is fitted to the theme's proportions.
- Steam games, Lutris games and Minecraft show their own names and icons.
- The app grid sizes its icons and spacing to the screen, as Launchpad does.
- The app grid, the dock and Dolphin can make a desktop shortcut for any program or file.

### Desktop

- Desktop icons glide to their places after a drop onto the grid, Clean Up or a new sort order, as Finder's do.

### Menu bar

- Global menus for GTK, GMenu and X11 programs, as well as Firefox, Thunderbird, GIMP, REAPER (on X11 and native Wayland) and Windows programs under Wine and Proton.
- A menu bar on every display. Each display can show its own window's menus while the others fade.
- A clipboard history with pins, search and private storage. It opens with Super+V or as a dock stack, and has its own settings pane.
- An emoji menu on Super+. that types the chosen emoji into the focused window.
- The volume menu has a slider for each program playing sound.
- The clock can sit on the left or in the middle and show the date in several styles that follow the region.
- Turning off Translucent menu bar gives an opaque menu bar.

### Look and theme

- Qt and KDE programs use the theme's fonts, icons, tooltips, scroll bars, tool bar buttons and file dialogs.
- Firefox, Thunderbird, GIMP and Steam draw the theme's window buttons. For Steam this is opt in.
- GTK alerts and tabs are drawn as in Mavericks.
- Sea Lion title bars have subpixel letters, and the menu bar's last row is softened.
- Extra icons: LibreOffice, text and markdown files, torrents, Nintendo 64 ROMs (shown as cartridges with the game's logo), and some games.
- The pointer size you pick is kept across theme imports.
- Theme import accepts a restored installer volume.

### Programs

- REAPER opens and saves through the desktop's file dialog, with its own options kept underneath.
- LibreOffice starts as a KDE program, so it uses KDE's file dialog. There is a config switch for this.
- Links and files from xdg-open open as their own service, so the program that asked never waits on the one it opened.
- Dolphin and other KDE programs open the desktop's own terminal.
- A screenshot program can take the pointer out of its grab for a few seconds.

### Session and reliability

- The desktop fades in after login. The mavernix-greeter login screen takes the desktop's look, layout and keyboard.
- `mavernix-reload` brings a running desktop up to date with an installed update without logging out.
- The shell restarts after a crash and keeps its bus names. Panel and dock come back with their monitor.
- The shell's animations step once per drawn frame, so a faster screen is no longer held back by a 60 Hz one.
- Fixes for compositor stops: unplugging or turning off a display, and tiny frameless windows.
- Login programs wait for the tray so Electron apps keep their icon.

## Keeping this page current

Every change on this branch that a user would notice adds or updates a line above in the same commit, under the section it belongs to. A fix that makes a listed feature work as described changes nothing here. When upstream takes over a change in a release, its line goes.
