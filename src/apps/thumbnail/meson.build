# A preview of a file's contents for the desktop, made by kde's thumbnailers.

kio_gui = dependency('KF6KIO', method: 'cmake', modules: ['KF6::KIOGui', 'KF6::KIOCore'])

executable(
    'mavernix-thumbnail',
    'main.cpp',
    dependencies: [dependency('qt6', modules: ['Core', 'Gui']), kio_gui],
    install: true,
    install_dir: mavernix_bindir,
)
