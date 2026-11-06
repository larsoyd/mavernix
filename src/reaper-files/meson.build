# Sends REAPER's file dialogs to the desktop's file chooser portal, and saves the portal cannot show to the save panel.

reaper_files_plugin = shared_module('reaper_mavernix_files', 'reaper-files.cpp',
    name_prefix: '',
    gnu_symbol_visibility: 'hidden',
    dependencies: [gio, dependency('gdk-x11-3.0')],
    install: true,
    install_dir: mavernix_libdir / 'reaper',
)

# The desktop's save dialog with REAPER's own options below the list, for saves the portal cannot show.
kio_widgets = dependency('KF6KIO', method: 'cmake', modules: ['KF6::KIOFileWidgets'])
save_panel_library = static_library('reaper-save-panel', 'save-panel.cpp',
    qt.compile_moc(headers: 'save-panel.h', dependencies: [qt_widgets, kio_widgets]),
    dependencies: [qt_widgets, kio_widgets])
executable('mavernix-reaper-save-panel', 'save-panel-main.cpp',
    link_with: save_panel_library,
    dependencies: [qt_widgets, kio_widgets],
    install: true,
    install_dir: mavernix_libdir / 'reaper',
)
