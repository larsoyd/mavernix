# Moves a GTK 3 window's menu bar into the desktop's menu bar.

add_languages('c', native: false)

wayland_scanner = find_program('wayland-scanner')
appmenu_xml = files('appmenu.xml')
appmenu_header = custom_target('appmenu-header', input: appmenu_xml, output: 'appmenu-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
appmenu_code = custom_target('appmenu-code', input: appmenu_xml, output: 'appmenu-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

shared_module('mavernix-global-menu', 'global-menu.c', appmenu_header, appmenu_code,
    dependencies: [dependency('gtk+-wayland-3.0'), dependency('gtk+-3.0'), dependency('dbusmenu-gtk3-0.4'),
        dependency('wayland-client')],
    install: true,
    install_dir: mavernix_libdir / 'gtk-3.0',
)
