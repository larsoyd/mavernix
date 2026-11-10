# Tells the shell when the pointer digs at another screen's bottom edge.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/dock-dig.xml.
shared_module(
    'dock-dig',
    ['dock-dig.cpp', 'dig-detect.cpp', 'dig-edges.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/dock-dig.xml', install_dir: wayfire_metadata_dir)
