# Sends REAPER's file dialogs to the desktop's file chooser portal.

reaper_files_plugin = shared_module('reaper_mavernix_files', 'reaper-files.cpp',
    name_prefix: '',
    gnu_symbol_visibility: 'hidden',
    dependencies: [gio, dependency('gdk-x11-3.0')],
    install: true,
    install_dir: mavernix_libdir / 'reaper',
)
