# Moves REAPER's menus into the desktop's menu bar.

reaper_menu_plugin = shared_module('reaper_mavernix_menu', 'reaper-menu.cpp',
    name_prefix: '',
    gnu_symbol_visibility: 'hidden',
    dependencies: [gio, dependency('gdk-x11-3.0'), dependency('dbusmenu-glib-0.4')],
    install: true,
    install_dir: mavernix_libdir / 'reaper',
)
