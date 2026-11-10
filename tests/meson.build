# The tests: the logic that needs no screen, one file per subject.
#
#   meson test -C build

test_sources = '..' / 'src'

tests = {
    'snap-keys': {
        'sources': [test_sources / 'compositor/snap-keys/snap-steps.cpp'],
        'include': test_sources / 'compositor/snap-keys',
        'dependencies': [],
    },
    'shared-edges': {
        'sources': [test_sources / 'compositor/shared-edges/seam-math.cpp'],
        'include': test_sources / 'compositor/shared-edges',
        'dependencies': [],
    },
    'hide-pointer': {
        'sources': [test_sources / 'compositor/hide-pointer/typing-key.cpp'],
        'include': test_sources / 'compositor/hide-pointer',
        'dependencies': [dependency('xkbcommon')],
    },
    'dock-dig': {
        'sources': [
            test_sources / 'compositor/dock-dig/dig-detect.cpp',
            test_sources / 'compositor/dock-dig/dig-edges.cpp',
        ],
        'include': test_sources / 'compositor/dock-dig',
        'dependencies': [],
    },
    'shake-to-find': {
        'sources': [test_sources / 'compositor/shake-to-find/shake-detect.cpp'],
        'include': test_sources / 'compositor/shake-to-find',
        'dependencies': [],
    },
    'theme-file': {
        'sources': [test_sources / 'compositor/decoration/theme-file.cpp'],
        'include': test_sources / 'compositor/decoration',
        'dependencies': [tomlplusplus],
    },
    'keyboard': {
        'sources': [
            test_sources / 'apps/settings/shortcut-keys.cpp',
            test_sources / 'apps/settings/keyboard-layouts.cpp',
        ],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
    'panel-modes': {
        'sources': [test_sources / 'apps/settings/panel-modes.cpp'],
        'include': test_sources / 'apps/settings',
        'dependencies': [],
    },
    'icon-theme-names': {
        'sources': [test_sources / 'apps/settings/icon-theme-names.cpp'],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
    'game-facts': {
        'sources': [test_sources / 'shell/native/game-facts.cpp'],
        'include': test_sources / 'shell/native',
        'dependencies': [],
    },
    'icon-fit': {
        'sources': [test_sources / 'shell/native/icon-fit.cpp'],
        'include': test_sources / 'shell/native',
        'dependencies': [],
    },
    'fitted-icon': {
        'sources': [
            test_sources / 'shell/native/icon-fit.cpp',
            test_sources / 'shell/native/icon-files.cpp',
            qt.compile_moc(headers: test_sources / 'shell/native/icon-files.hpp', dependencies: qt_native),
        ],
        'include': test_sources / 'shell/native',
        'dependencies': [qt_native, librsvg],
    },
    'folder-contents': {
        'sources': [
            test_sources / 'shell/native/folder-contents.cpp',
            test_sources / 'shell/native/file-moment.cpp',
            qt.compile_moc(headers: test_sources / 'shell/native/folder-contents.hpp', dependencies: qt_native),
        ],
        'include': test_sources / 'shell/native',
        'dependencies': [qt_native],
    },
    'clipboard-history': {
        'sources': [
            test_sources / 'shell/native/clipboard-history.cpp',
            qt.compile_moc(headers: test_sources / 'shell/native/clipboard-history.hpp', dependencies: qt_native),
        ],
        'include': test_sources / 'shell/native',
        'dependencies': [qt_native, kguiaddons],
    },
    'screen-layout': {
        'sources': [test_sources / 'apps/settings/screen-layout.cpp'],
        'include': test_sources / 'apps/settings',
        'dependencies': [],
    },
    'datetime-pane': {
        'sources': [
            test_sources / 'apps/settings/datetime-pane.cpp',
            test_sources / 'apps/settings/menu-bar-box.cpp',
            test_sources / 'apps/settings/pane-parts.cpp',
            test_sources / 'apps/settings/choices.cpp',
            qt.compile_moc(headers: test_sources / 'apps/settings/datetime-pane.hpp', dependencies: qt_widgets),
        ],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
    'emoji-set': {
        'sources': [
            test_sources / 'shell/native/emoji-set.cpp',
            qt.compile_moc(headers: test_sources / 'shell/native/emoji-set.hpp', dependencies: qt_native),
        ],
        'include': test_sources / 'shell/native',
        'dependencies': [qt_native],
    },
    'typed-text': {
        'sources': [
            test_sources / 'shell/native/typed-text.cpp',
            qt.compile_moc(headers: test_sources / 'shell/native/typed-text.hpp', dependencies: qt_native),
            virtual_keyboard_header,
            virtual_keyboard_code,
        ],
        'include': test_sources / 'shell/native',
        'dependencies': [qt_native, wayland_client, kguiaddons, dependency('xkbcommon')],
    },
    'clipboard-pane': {
        'sources': [
            test_sources / 'apps/settings/clipboard-pane.cpp',
            test_sources / 'apps/settings/menu-bar-box.cpp',
            test_sources / 'apps/settings/pane-parts.cpp',
            test_sources / 'apps/settings/choices.cpp',
            qt.compile_moc(headers: test_sources / 'apps/settings/clipboard-pane.hpp', dependencies: qt_widgets),
        ],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
    'overview': {
        'sources': [
            test_sources / 'apps/settings/overview.cpp',
            qt.compile_moc(headers: test_sources / 'apps/settings/overview.hpp', dependencies: qt_widgets),
        ],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
    'menu-bar-box': {
        'sources': [test_sources / 'apps/settings/menu-bar-box.cpp', test_sources / 'apps/settings/choices.cpp'],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
    'pane-layout': {
        'sources': [test_sources / 'apps/settings/pane-parts.cpp'],
        'include': test_sources / 'apps/settings',
        'dependencies': [qt_widgets],
    },
}

foreach name, test_parts : tests
    test_file = 'test-' + name + '.cpp'
    executable_file = executable(
        'test-' + name,
        [test_file, test_parts['sources'], qt.compile_moc(sources: test_file, dependencies: qt_test)],
        include_directories: include_directories(test_parts['include']),
        dependencies: [qt_test, test_parts['dependencies']],
        # The shipped themes, for the theme file's test.
        cpp_args: ['-DTHEMES_DIR="' + (meson.project_source_root() / 'data' / 'themes') + '"'],
    )
    test(name, executable_file, env: {'QT_QPA_PLATFORM': 'offscreen'})
endforeach

quick_style_test = executable(
    'test-quick-style',
    ['test-quick-style.cpp', qt.compile_moc(sources: 'test-quick-style.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'QuickControls2'])],
    cpp_args: ['-DQUICK_STYLE_DIR="' + (meson.project_source_root() / 'src' / 'session' / 'quick-style') + '"'],
)
test('quick-style', quick_style_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

menu_bar_clock_test = executable(
    'test-menu-bar-clock',
    ['test-menu-bar-clock.cpp', qt.compile_moc(sources: 'test-menu-bar-clock.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Qml'])],
    cpp_args: ['-DSHELL_DIR="' + (meson.project_source_root() / 'src' / 'shell') + '"'],
)
test('menu-bar-clock', menu_bar_clock_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

clipboard_stack_size_test = executable(
    'test-clipboard-stack-size',
    ['test-clipboard-stack-size.cpp', qt.compile_moc(sources: 'test-clipboard-stack-size.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Qml'])],
    cpp_args: ['-DSHELL_DIR="' + (meson.project_source_root() / 'src' / 'shell') + '"'],
)
test('clipboard-stack-size', clipboard_stack_size_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

app_grid_layout_test = executable(
    'test-app-grid-layout',
    ['test-app-grid-layout.cpp', qt.compile_moc(sources: 'test-app-grid-layout.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Qml'])],
    cpp_args: ['-DSHELL_DIR="' + (meson.project_source_root() / 'src' / 'shell') + '"'],
)
test('app-grid-layout', app_grid_layout_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

dock_screen_test = executable(
    'test-dock-screen',
    ['test-dock-screen.cpp', qt.compile_moc(sources: 'test-dock-screen.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Qml'])],
    cpp_args: ['-DSHELL_DIR="' + (meson.project_source_root() / 'src' / 'shell') + '"'],
)
test('dock-screen', dock_screen_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

folder_grid_scroll_test = executable(
    'test-folder-grid-scroll',
    ['test-folder-grid-scroll.cpp', qt.compile_moc(sources: 'test-folder-grid-scroll.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Qml'])],
    cpp_args: ['-DSHELL_DIR="' + (meson.project_source_root() / 'src' / 'shell') + '"'],
)
test('folder-grid-scroll', folder_grid_scroll_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

python = find_program('python3')
test(
    'greeter-look',
    python,
    args: [files('test_greeter_look.py')],
    env: {'SESSION_SOURCE_DIR': meson.project_source_root() / 'src' / 'session'},
)
test(
    'session-config',
    python,
    args: [files('test_session_config.py')],
    env: {
        'SESSION_SOURCE_DIR': meson.project_source_root() / 'src' / 'session',
        'CONFIG_SOURCE_DIR': meson.project_source_root() / 'data' / 'config',
        'THEMES_SOURCE_DIR': meson.project_source_root() / 'data' / 'themes',
        'EXTENSIONS_SOURCE_DIR': meson.project_source_root() / 'data' / 'extensions',
        'THEME_DOCS_DIR': meson.project_source_root() / 'docs' / 'themes',
        'LOOKS_SOURCE_DIR': meson.project_source_root() / 'src' / 'apps' / 'theme-installer' / 'backend' / 'looks',
    },
)

test(
    'app-store',
    python,
    args: [files('test_app_store.py')],
    env: {'APP_STORE_BACKEND_DIR': meson.project_source_root() / 'src' / 'apps' / 'app-store' / 'backend'},
)

test(
    'emoji-list',
    python,
    args: [files('test_emoji_list.py')],
    env: {'EMOJI_SOURCE_DIR': meson.project_source_root() / 'data' / 'emoji'},
)

test(
    'shell-weak-maps',
    python,
    args: [files('test_shell_weak_maps.py')],
)

test(
    'file-action',
    python,
    args: [files('test_file_action.py')],
)

test(
    'shell-frame-steps',
    python,
    args: [files('test_shell_frame_steps.py')],
)

test(
    'cursors',
    python,
    args: [files('test_cursors.py')],
)

test(
    'make-icons',
    python,
    args: [files('test_make_icons.py')],
)

menu_test_sources = files('../src/shell/native/application-menu.cpp', '../src/shell/native/menu-registrar.cpp',
    '../src/shell/native/gmenu-reader.cpp')
menu_test_headers = files('../src/shell/native/application-menu.hpp', '../src/shell/native/menu-registrar.hpp',
    '../src/shell/native/gmenu-reader.hpp')
menu_test = executable('test-menu-adapters', 'test-menu-adapters.cpp', menu_test_sources,
    files('../src/shell/native/compositor-socket.cpp'),
    qt.compile_moc(sources: 'test-menu-adapters.cpp', headers: [menu_test_headers,
        files('../src/shell/native/compositor-socket.hpp')], dependencies: qt_test),
    include_directories: include_directories('../src/shell/native'), dependencies: [qt_test, qt_widgets, gio])
test('menu-adapters', find_program('dbus-run-session'), args: ['--', menu_test], timeout: 30)

menu_probe = executable('menu-probe', 'menu-probe.cpp', menu_test_sources,
    files('../src/shell/native/active-window.cpp', '../src/shell/native/compositor-socket.cpp',
        '../src/shell/native/xmenu-properties.cpp'),
    qt.compile_moc(sources: 'menu-probe.cpp', headers: [menu_test_headers,
        files('../src/shell/native/active-window.hpp', '../src/shell/native/compositor-socket.hpp',
            '../src/shell/native/xmenu-properties.hpp')], dependencies: qt_test),
    include_directories: include_directories('../src/shell/native'),
    dependencies: [qt_widgets, gio, dependency('xcb')])

reaper_menu_test = executable('test-reaper-menu-text', 'test-reaper-menu-text.cpp',
    qt.compile_moc(sources: 'test-reaper-menu-text.cpp', dependencies: qt_test),
    include_directories: include_directories('../src/reaper-menu'), dependencies: [qt_test])
test('reaper-menu-text', reaper_menu_test)

fake_swell = shared_module('libSwell', 'fake-swell.c', name_prefix: '', name_suffix: 'so',
    gnu_symbol_visibility: 'hidden')
reaper_plugin_host = executable('reaper-plugin-host', 'reaper-plugin-host.cpp',
    dependencies: [dependency('glib-2.0')], export_dynamic: true, gnu_symbol_visibility: 'hidden')
test('reaper-menu-plugin', python,
    args: [files('test_reaper_menu_plugin.py'), reaper_plugin_host, meson.current_build_dir(), reaper_menu_plugin],
    depends: fake_swell, timeout: 45)
reaper_save_panel_test = executable('test-reaper-save-panel', 'test-reaper-save-panel.cpp',
    qt.compile_moc(sources: 'test-reaper-save-panel.cpp', dependencies: [qt_test, kio_widgets]),
    include_directories: include_directories('../src/reaper-files'), link_with: save_panel_library,
    dependencies: [qt_test, qt_widgets, kio_widgets])
test('reaper-save-panel', reaper_save_panel_test, env: {'QT_QPA_PLATFORM': 'offscreen'}, timeout: 60)
test('reaper-files-plugin', python,
    args: [files('test_reaper_files_plugin.py'), reaper_plugin_host, meson.current_build_dir(), reaper_files_plugin],
    depends: fake_swell, timeout: 75)

test('menu-session', python, args: files('test_menu_session.py'))
test('gtk-global-menu', python, args: [files('test_gtk_global_menu.py'), gtk_menu_module],
    depends: borderless_maximized_plugin, timeout: 65, is_parallel: false)
test('menu-routes', python, args: [files('test_gtk_global_menu.py'), gtk_menu_module, '--routes', menu_probe],
    depends: menu_discovery_plugin, timeout: 65, is_parallel: false)
