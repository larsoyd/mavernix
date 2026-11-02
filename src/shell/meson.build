# The shell: panel, dock and overlays. Its QML is read by Quickshell at run
# time, so it is installed as it is; only the native module and the shaders
# are compiled.

subdir('native')

qt_bins = run_command(find_program('qmake6'), '-query', 'QT_HOST_BINS', check: true).stdout().strip()
qsb = find_program('qsb', dirs: qt_bins)

foreach shader : ['lanczos', 'cast-shadow']
    custom_target(
        shader + '-shader',
        input: 'shaders' / shader + '.frag',
        output: shader + '.frag.qsb',
        command: [qsb, '--glsl', '300 es,330', '-o', '@OUTPUT@', '@INPUT@'],
        install: true,
        install_dir: mavernix_datadir / 'shell',
    )
endforeach

install_subdir(
    '.',
    install_dir: mavernix_datadir / 'shell',
    strip_directory: true,
    exclude_directories: ['native', 'shaders'],
    exclude_files: ['meson.build'],
)
