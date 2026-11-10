# The pointer goes away while typing or after resting, until the mouse is used.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/hide-pointer.xml.
shared_module(
    'hide-pointer',
    ['hide-pointer.cpp', 'typing-key.cpp'],
    dependencies: [wayfire, dependency('xkbcommon')],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/hide-pointer.xml', install_dir: wayfire_metadata_dir)
