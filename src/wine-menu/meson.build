# Moves the menu bars of Windows programs running under Wine into the desktop's menu bar.

wine_menu_dir = mavernix_libdir / 'wine-menu'
wine_menu_windows = []
foreach arch : [['x86_64-w64-mingw32-gcc', '64', '-DSIBLING_NAME=L"mavernix-wine-menu-agent32.exe"'],
        ['i686-w64-mingw32-gcc', '32', '-DSIBLING_NAME_NONE']]
    cross_cc = find_program(arch[0])
    wine_menu_windows += custom_target('wine-menu-client' + arch[1],
        input: 'client.c',
        output: 'mavernix-wine-menu' + arch[1] + '.dll',
        command: [cross_cc, '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-static-libgcc', '-s',
            '-Wl,--kill-at', '-o', '@OUTPUT@', '@INPUT@', '-lws2_32', '-lbcrypt', '-lcomctl32'],
        install: true,
        install_dir: wine_menu_dir,
    )
    wine_menu_windows += custom_target('wine-menu-agent' + arch[1],
        input: 'agent.c',
        output: 'mavernix-wine-menu-agent' + arch[1] + '.exe',
        command: [cross_cc, '-O2', '-Wall', '-Wextra', '-Werror', '-municode', '-static-libgcc', '-s',
            '-DCLIENT_NAME=L"mavernix-wine-menu' + arch[1] + '.dll"',
            '-DAGENT_NAME=L"mavernix-wine-menu-agent' + arch[1] + '.exe"', arch[2], '-o', '@OUTPUT@', '@INPUT@'],
        install: true,
        install_dir: wine_menu_dir,
    )
endforeach

wine_menu_bridge = executable('mavernix-wine-menu', 'bridge.cpp',
    include_directories: include_directories('../reaper-menu'),
    cpp_args: ['-DAGENTS_DIR="' + (get_option('prefix') / wine_menu_dir) + '"'],
    dependencies: [gio, dependency('dbusmenu-glib-0.4')],
    install: true,
    install_dir: wine_menu_dir,
)
