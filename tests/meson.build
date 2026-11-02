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
    'screen-layout': {
        'sources': [test_sources / 'apps/settings/screen-layout.cpp'],
        'include': test_sources / 'apps/settings',
        'dependencies': [],
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
    test(name, executable_file)
endforeach

quick_style_test = executable(
    'test-quick-style',
    ['test-quick-style.cpp', qt.compile_moc(sources: 'test-quick-style.cpp', dependencies: qt_test)],
    dependencies: [qt_test, dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'QuickControls2'])],
    cpp_args: ['-DQUICK_STYLE_DIR="' + (meson.project_source_root() / 'src' / 'session' / 'quick-style') + '"'],
)
test('quick-style', quick_style_test, env: {'QT_QPA_PLATFORM': 'offscreen'})

python = find_program('python3')
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
    'shell-weak-maps',
    python,
    args: [files('test_shell_weak_maps.py')],
)

test(
    'cursors',
    python,
    args: [files('test_cursors.py')],
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

test('menu-session', python, args: files('test_menu_session.py'))
test('gtk-global-menu', python, args: [files('test_gtk_global_menu.py'), gtk_menu_module],
    depends: borderless_maximized_plugin, timeout: 65, is_parallel: false)
test('menu-routes', python, args: [files('test_gtk_global_menu.py'), gtk_menu_module, '--routes', menu_probe],
    depends: menu_discovery_plugin, timeout: 65, is_parallel: false)
