# The shell's native QML module, "Shell.Native": the few things the shell
# needs that QML cannot do by itself.

# Private headers: icon-files.cpp uses Qt's internal icon lookup.
qt_native = dependency('qt6', modules: ['Core', 'DBus', 'Gui', 'Network', 'Qml', 'Quick'],
    private_headers: true)

add_languages('c', native: false)
wayland_scanner = find_program('wayland-scanner')
virtual_keyboard_xml = files('protocols/virtual-keyboard-unstable-v1.xml')
virtual_keyboard_header = custom_target('virtual-keyboard-header', input: virtual_keyboard_xml,
    output: 'virtual-keyboard-unstable-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
virtual_keyboard_code = custom_target('virtual-keyboard-code', input: virtual_keyboard_xml,
    output: 'virtual-keyboard-unstable-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
wayland_client = dependency('wayland-client')

moc_sources = qt.compile_moc(
    headers: [
        'active-window.hpp',
        'application-menu.hpp',
        'bluetooth-agent.hpp',
        'clipboard-history.hpp',
        'compositor-socket.hpp',
        'desktop-aside.hpp',
        'desktops.hpp',
        'dock-dig.hpp',
        'emoji-set.hpp',
        'file-kind.hpp',
        'file-moment.hpp',
        'folder-contents.hpp',
        'folder-watch.hpp',
        'gmenu-reader.hpp',
        'held-keys.hpp',
        'icon-files.hpp',
        'item-picture.hpp',
        'lid-watch.hpp',
        'main-screen.hpp',
        'menu-registrar.hpp',
        'plugin.hpp',
        'pointer-place.hpp',
        'session-watch.hpp',
        'shortcut-pause.hpp',
        'sound-pack.hpp',
        'typed-text.hpp',
        'window-attention.hpp',
        'window-games.hpp',
        'window-pictures.hpp',
        'xmenu-properties.hpp',
    ],
    dependencies: qt_native,
)

module_dir = mavernix_libdir / 'qml' / 'Shell' / 'Native'

shared_module(
    'shellnative',
    [
        'active-window.cpp',
        'application-menu.cpp',
        'bluetooth-agent.cpp',
        'clipboard-history.cpp',
        'compositor-socket.cpp',
        'desktop-aside.cpp',
        'desktops.cpp',
        'dock-dig.cpp',
        'emoji-set.cpp',
        'file-kind.cpp',
        'file-moment.cpp',
        'folder-contents.cpp',
        'folder-watch.cpp',
        'game-facts.cpp',
        'gmenu-reader.cpp',
        'held-keys.cpp',
        'icon-files.cpp',
        'icon-fit.cpp',
        'item-picture.cpp',
        'lid-watch.cpp',
        'main-screen.cpp',
        'menu-registrar.cpp',
        'plugin.cpp',
        'pointer-place.cpp',
        'session-watch.cpp',
        'shortcut-pause.cpp',
        'sound-pack.cpp',
        'typed-text.cpp',
        virtual_keyboard_header,
        virtual_keyboard_code,
        'window-attention.cpp',
        'window-games.cpp',
        'window-picture-provider.cpp',
        'window-pictures.cpp',
        'xmenu-properties.cpp',
        moc_sources,
    ],
    dependencies: [qt_native, librsvg, gio, kguiaddons, dependency('xcb'), wayland_client],
    install: true,
    install_dir: module_dir,
)

install_data('qmldir', install_dir: module_dir)
