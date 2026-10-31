"""The extensions there are, the user's own and the bundled ones."""

import os
import sys
import tomllib
from pathlib import Path

from paths import EXTENSIONS_DIR


def extension_folders():
    """Where extensions are looked for: the user's own first, then bundled ones."""
    own = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "mavernix" / "extensions"
    return [own, EXTENSIONS_DIR]


def found_extensions(config, theme_name):
    """Every extension, by folder name.

    Looks come from looks/THEME.toml or looks/default.toml; settings are the
    manifest's [settings] under the config's [extensions.NAME]. A broken
    extension is reported and left out rather than stopping the desktop.
    """
    found = {}
    for folder in extension_folders():
        if not folder.is_dir():
            continue
        for directory in sorted(folder.iterdir()):
            manifest_file = directory / "extension.toml"
            # the user's own wins over a bundled one of the same name
            if not manifest_file.is_file() or directory.name in found:
                continue
            # not load_toml, which exits on an unreadable file
            try:
                with open(manifest_file, "rb") as file:
                    manifest = tomllib.load(file)
                looks = {}
                for looks_file in (directory / "looks" / f"{theme_name}.toml", directory / "looks" / "default.toml"):
                    if looks_file.is_file():
                        with open(looks_file, "rb") as file:
                            looks = tomllib.load(file)
                        break
            except (OSError, ValueError) as error:
                print(f"make_shell_config: leaving out the extension in {directory}: {error}", file=sys.stderr)
                continue
            settings = dict(manifest.get("settings", {}))
            decimal_settings = [key for key, default in settings.items() if isinstance(default, float)]
            wishes = config.get("extensions", {}).get(directory.name, {})
            if isinstance(wishes, dict):
                settings.update(wishes)
            found[directory.name] = {
                "directory": str(directory),
                "manifest": manifest,
                "looks": looks,
                "settings": settings,
                "decimal_settings": decimal_settings,
            }
    return found
