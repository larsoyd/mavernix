# The session scripts: start the desktop and write the compositor's and the
# shell's settings from a desktop config and its theme.

session_dir = mavernix_libdir / 'session'

paths = configuration_data()
paths.set('LIBRARY_DIR', get_option('prefix') / mavernix_libdir)
paths.set('DATA_DIR', get_option('prefix') / mavernix_datadir)
configure_file(input: 'paths.sh.in', output: 'paths.sh', configuration: paths,
    install_dir: session_dir)
configure_file(input: 'paths.py.in', output: 'paths.py', configuration: paths,
    install_dir: session_dir)

install_data(
    'start-desktop',
    'configure-desktop',
    'new-session-log',
    'run-shell',
    'keep-running',
    'after-session',
    'compositor_call.py',
    install_dir: session_dir,
    install_mode: 'rwxr-xr-x',
)
install_data(
    'at_login.py',
    'global-menu.sh',
    'check_theme.py',
    'extensions.py',
    'link_gimp_style.py',
    'link_mozilla_style.py',
    'link_reaper_menu.py',
    'export_greeter_look.py',
    'log_guard.py',
    'make_app_theme.py',
    'make_default_programs.py',
    'make_shell_config.py',
    'make_wayfire_config.py',
    'pick_desktop.py',
    'refresh_desktops.py',
    'refresh_extensions.py',
    'refresh_themes.py',
    'themes.py',
    'toml_text.py',
    'user_choices.py',
    'write_interface_settings.py',
    'run_xsettings.py',
    'kded5rc',
    install_dir: session_dir,
)
install_subdir('quick-style', install_dir: mavernix_libdir / 'qml', strip_directory: true)
install_data('bin/mavernix-dock-geometry', 'bin/mavernix-switch', 'bin/mavernix-reconfigure', 'bin/mavernix-file-action',
    'bin/mavernix-check-theme', 'bin/mavernix-extensions-changed',
    install_dir: session_dir / 'bin', install_mode: 'rwxr-xr-x')
install_data('notice/shell.qml', install_dir: session_dir / 'notice')
install_data('portals/wayfire-portals.conf', install_dir: session_dir / 'portals')
# with the session's qt6ct the kde helper calls the portal while starting and both wait 25 s
install_data('portals' / 'plasma-xdg-desktop-portal-kde.conf',
    install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user' / 'plasma-xdg-desktop-portal-kde.service.d',
    rename: 'mavernix.conf')

# The login screen's entry.
configure_file(
    input: 'mavernix.desktop.in',
    output: 'mavernix.desktop',
    configuration: paths,
    install_dir: get_option('datadir') / 'wayland-sessions',
)
