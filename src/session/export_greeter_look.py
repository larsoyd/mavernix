import argparse
import configparser
import hashlib
import json
import os
import re
import shutil
import stat
import sys
from pathlib import Path

SCHEMA = "mavernix-greeter.theme"
PICTURES = {".png", ".jpg", ".jpeg", ".webp"}
FONTS = {".ttf", ".otf", ".ttc", ".dfont"}
NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.+@ -]{0,95}")
OUTPUT_KEYS = ("mode", "position", "scale", "transform")
XKB_KEYS = ("layout", "model", "variant", "options")


def say(text):
    print(f"[greeter-look] {text}", file=sys.stderr)


def picture_suffix(path):
    p = Path(path)
    return p.suffix.lower() if p.is_file() and p.suffix.lower() in PICTURES else None


def readable_by_anyone(path):
    if not path.stat().st_mode & stat.S_IROTH:
        return False
    return all(p.stat().st_mode & stat.S_IXOTH for p in path.resolve().parents)


def outputs(ini):
    found = []
    for section in ini.sections():
        if not section.startswith("output:"):
            continue
        o = {"name": section.split(":", 1)[1]}
        for key in OUTPUT_KEYS:
            if key in ini[section]:
                value = ini[section][key]
                o[key] = float(value) if key == "scale" else value
                if key == "scale" and o[key].is_integer():
                    o[key] = int(o[key])
        found.append(o)
    return found


def plan(settings, ini, drm_driver):
    theme, folder = settings["theme"], Path(settings["theme_directory"])
    config, user = settings.get("config", {}), settings.get("user", {})
    files = {}
    picture = config.get("desktop", {}).get("picture") or ""
    if not picture_suffix(picture):
        picture = str(folder / theme.get("desktop", {}).get("background", {}).get("image", ""))
    background = {"veil": "#00000026", "fit": "cover"}
    if picture_suffix(picture):
        background["image"] = "background" + picture_suffix(picture).replace(".jpeg", ".jpg")
        files[background["image"]] = Path(picture)
    for font in sorted((folder / "fonts").glob("*")):
        if font.suffix.lower() in FONTS and font.is_file() and NAME.fullmatch(font.name):
            files[f"fonts/{font.name}"] = font
    links = {}
    cursor = theme.get("cursors", {})
    if NAME.fullmatch(cursor.get("theme", "")):
        source = folder / "share" / "icons" / cursor["theme"] / "cursors"
        for c in sorted(source.glob("*")) if source.is_dir() else []:
            target = f"cursors/{cursor['theme']}/cursors/{c.name}"
            if c.is_symlink():
                links[target] = os.readlink(c)
            elif c.is_file() and NAME.fullmatch(c.name):
                files[target] = c
    accounts = {}
    face = user.get("picture") or ""
    if face and Path(face).is_file() and not readable_by_anyone(Path(face)):
        suffix = ".jpg" if Path(face).read_bytes()[:3] == b"\xff\xd8\xff" else ".png"
        accounts[user["name"]] = {"avatar": f"accounts/{user['name']}{suffix}"}
        files[accounts[user["name"]]["avatar"]] = Path(face)
    interface = theme.get("fonts", {}).get("interface", {})
    look = {
        "schema": SCHEMA, "version": 1, "id": folder.name, "name": theme.get("theme", {}).get("name", folder.name),
        "background": background,
        "fonts": {k: interface[k] for k in ("family",) if k in interface},
        "cursor": {k: cursor[k] for k in ("theme", "size") if k in cursor},
        "keyboard": {k: ini["input"][f"xkb_{k}"] for k in XKB_KEYS if ini.has_option("input", f"xkb_{k}")},
        "display": {"outputs": outputs(ini), "main_output": config.get("display", {}).get("main_screen", "")},
        "accounts": accounts,
    }
    if drm_driver:
        look["display"]["drm_driver"] = drm_driver
    return look, files, links


def stamp(look, files, links):
    h = hashlib.sha256(json.dumps([look, sorted(links.items())], sort_keys=True).encode())
    for name, path in sorted(files.items()):
        st = path.stat()
        h.update(f"{name}\0{path}\0{st.st_size}\0{st.st_mtime_ns}\n".encode())
    return h.hexdigest()


def write(inbox, look, files, links):
    new, current, old = inbox / "snapshot.new", inbox / "snapshot", inbox / "snapshot.old"
    for d in (new, old):
        shutil.rmtree(d, ignore_errors=True)
    new.mkdir(mode=0o700)
    (new / "theme.json").write_text(json.dumps(look, indent=1) + "\n")
    for name, source in files.items():
        (new / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, new / name)
    for name, target in links.items():
        (new / name).parent.mkdir(parents=True, exist_ok=True)
        os.symlink(target, new / name)
    if look["cursor"].get("theme"):
        index = new / "cursors" / look["cursor"]["theme"] / "index.theme"
        index.parent.mkdir(parents=True, exist_ok=True)
        index.write_text(f"[Icon Theme]\nName={look['cursor']['theme']}\n")
    if current.exists():
        current.rename(old)
    new.rename(current)
    shutil.rmtree(old, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("settings")
    ap.add_argument("session_ini")
    ap.add_argument("inbox")
    ap.add_argument("--drm-driver", default="")
    args = ap.parse_args()
    inbox = Path(args.inbox)
    if not inbox.is_dir() or not os.access(inbox, os.W_OK):
        say(f"skipped reason=no_inbox path={inbox}")
        return 0
    settings = json.loads(Path(args.settings).read_text())
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(args.session_ini)
    look, files, links = plan(settings, ini, args.drm_driver)
    digest = stamp(look, files, links)
    stamp_file = inbox / "stamp"
    if stamp_file.is_file() and stamp_file.read_text().strip() == digest:
        say("result=unchanged")
        return 0
    write(inbox, look, files, links)
    stamp_file.write_text(digest + "\n")
    (inbox / "ready").write_text(f"{digest}\n")
    say(f"result=exported theme={look['id']} files={len(files)} links={len(links)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
