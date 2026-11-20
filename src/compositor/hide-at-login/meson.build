# Opens the first window of chosen login programs minimized.

# Wayfire looks for lib<name>.so, so the name must match the plugin's
# name in metadata/hide-at-login.xml.
shared_module(
    'hide-at-login',
    ['hide-at-login.cpp', 'expectations.cpp'],
    dependencies: [wayfire],
    cpp_args: wayfire_plugin_args,
    install: true,
    install_dir: wayfire_plugin_dir,
)
install_data('metadata/hide-at-login.xml', install_dir: wayfire_metadata_dir)
