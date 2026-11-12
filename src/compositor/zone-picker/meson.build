# Offers a plate of layouts while a window is dragged toward the top of a screen.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/zone-picker.xml.
shared_module(
    'zone-picker',
    ['zone-picker.cpp', 'zone-layout.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/zone-picker.xml', install_dir: wayfire_metadata_dir)
