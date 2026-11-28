# Leaves room around windows tiled to a half, a quarter or the whole screen.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/window-gaps.xml.
shared_module(
    'window-gaps',
    ['window-gaps.cpp', 'gap-math.cpp', '../game-window/game-marks.cpp'],
    include_directories: include_directories('../game-window'),
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/window-gaps.xml', install_dir: wayfire_metadata_dir)
