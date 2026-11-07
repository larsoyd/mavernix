# Gives windows that draw their own frame the theme's corners and shadow.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/rounded-corners.xml.
shared_module(
    'rounded-corners',
    ['rounded-corners.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/rounded-corners.xml', install_dir: wayfire_metadata_dir)
