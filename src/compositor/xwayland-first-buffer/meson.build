# Maps X11 windows whose first buffer arrived before their surface was paired.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/xwayland-first-buffer.xml.
shared_module(
    'xwayland-first-buffer',
    ['xwayland-first-buffer.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/xwayland-first-buffer.xml', install_dir: wayfire_metadata_dir)
