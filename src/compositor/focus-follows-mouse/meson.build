# Gives the keyboard to the window the pointer enters.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/focus-follows-mouse.xml.
shared_module(
    'focus-follows-mouse',
    ['focus-follows-mouse.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/focus-follows-mouse.xml', install_dir: wayfire_metadata_dir)
