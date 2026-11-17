# Takes the pointer out of screenshots for a short lease.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/screenshot-pointer.xml.
shared_module(
    'screenshot-pointer',
    ['screenshot-pointer.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/screenshot-pointer.xml', install_dir: wayfire_metadata_dir)
