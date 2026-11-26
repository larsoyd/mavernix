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
SWITCH_OPTIONS = {
    "ctrl_space": "grp:ctrl_space_toggle",
    "alt_shift": "grp:alt_shift_toggle",
    "caps_lock": "grp:caps_toggle",
}
ALLOWED_SWITCH_VALUES = set(SWITCH_OPTIONS.values())
INPUT_FLOAT_KEYS = ("mouse_cursor_speed", "touchpad_cursor_speed", "mouse_scroll_speed")
INPUT_INT_KEYS = ("kb_repeat_delay", "kb_repeat_rate")
INPUT_BOOL_KEYS = ("tap_to_click", "natural_scroll", "mouse_natural_scroll", "left_handed_mode")
INPUT_STR_KEYS = ("mouse_accel_profile", "touchpad_accel_profile", "click_method")
CLOCK_KEYS = (
    ("clock_format", "format"),
    ("clock_shows_date", "show_date"),
    ("clock_shows_time", "show_time"),
    ("clock_region_time", "region_time"),
    ("clock_region_date", "region_date"),
    ("clock_date_style", "date_style"),
)
CLASSIC_GRADIENT = {
    "color": "#51535bff",
    "color2": "#2d2d32ff",
    "center_y": 0.382,
    "radius_x": 0.6,
    "radius_y": 0.688,
    "veil": "#00000000",
}
LIGHT_MENU_COLORS = {
    "menu_bg": "#f2f2f2f2",
    "menu_border": "#00000040",
    "menu_text": "#000000ff",
    "menu_highlight": "#3875d7ff",
    "menu_highlight_text": "#ffffffff",
}
DARK_MENU_COLORS = {
    "menu_bg": "#2c2c2ef0",
    "menu_border": "#ffffff26",
    "menu_text": "#f2f2f2ff",
    "menu_highlight": "#3875d7ff",
    "menu_highlight_text": "#ffffffff",
}


def say(text):
    print(f"[greeter-look] {text}", file=sys.stderr)


def picture_suffix(path):
    p = Path(path)
    return p.suffix.lower() if p.is_file() and p.suffix.lower() in PICTURES else None


def readable_by_anyone(path):
    if not path.stat().st_mode & stat.S_IROTH:
        return False
    return all(p.stat().st_mode & stat.S_IXOTH for p in path.resolve().parents)


def read_choices(settings):
    try:
        choices = json.loads(Path(settings.get("user_choices_file") or "").read_text())
    except (OSError, ValueError):
        return {}
    return choices if isinstance(choices, dict) else {}


def merged_table(config, choices, name):
    base = config.get(name, {}) if isinstance(config.get(name), dict) else {}
    over = choices.get(name, {}) if isinstance(choices.get(name), dict) else {}
    return {**base, **over}


def normalize_rgba(value):
    if not isinstance(value, str):
        return None
    v = value.strip()
    if re.fullmatch(r"#[0-9a-fA-F]{6}", v):
        return v.lower() + "ff"
    if re.fullmatch(r"#[0-9a-fA-F]{8}", v):
        return v.lower()
    return None


def is_dark_theme(theme, theme_id):
    explicit = theme.get("greeter", {}).get("appearance")
    if explicit in ("light", "dark"):
        return explicit == "dark"
    if theme_id.endswith("-dark") or "-dark-" in theme_id:
        return True
    palette = theme.get("controls", {}).get("palette", {})
    win = normalize_rgba(palette.get("window"))
    if win:
        r, g, b = int(win[1:3], 16), int(win[3:5], 16), int(win[5:7], 16)
        lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
        return lum < 0.35
    return False


def theme_colors(theme, dark):
    colors = dict(DARK_MENU_COLORS if dark else LIGHT_MENU_COLORS)
    palette = theme.get("controls", {}).get("palette", {})
    highlight = normalize_rgba(palette.get("highlight"))
    if highlight:
        colors["menu_highlight"] = highlight
    rings = theme.get("lock", {}).get("field", {}).get("focus_rings", [])
    if isinstance(rings, list) and rings:
        ring0 = normalize_rgba(rings[0])
        if ring0:
            colors["focus"] = ring0
            colors["focus2"] = normalize_rgba(rings[1]) if len(rings) > 1 and normalize_rgba(rings[1]) else ring0
    own = theme.get("greeter", {}).get("colors", {})
    if isinstance(own, dict):
        for k, v in own.items():
            rgba = normalize_rgba(v)
            if rgba:
                colors[k] = rgba
    return colors


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


def input_settings(ini):
    if not ini.has_section("input"):
        return {}
    sec = ini["input"]
    out = {}
    for key in INPUT_FLOAT_KEYS:
        if key in sec:
            try:
                out[key] = float(sec[key])
            except ValueError:
                pass
    for key in INPUT_INT_KEYS:
        if key in sec:
            try:
                out[key] = int(sec[key])
            except ValueError:
                pass
    for key in INPUT_BOOL_KEYS:
        if key in sec:
            val = sec[key].strip().lower()
            if val in ("true", "false"):
                out[key] = val == "true"
    for key in INPUT_STR_KEYS:
        if key in sec and sec[key].strip():
            out[key] = sec[key].strip()
    return out


def keyboard_settings(ini, input_cfg):
    kb = {k: ini["input"][f"xkb_{k}"] for k in XKB_KEYS if ini.has_option("input", f"xkb_{k}")}
    if "," in kb.get("layout", ""):
        sw = input_cfg.get("keyboard_switch_key", "")
        if sw in SWITCH_OPTIONS:
            kb["switch_option"] = SWITCH_OPTIONS[sw]
        elif "options" in kb:
            for token in kb["options"].split(","):
                if token.strip() in ALLOWED_SWITCH_VALUES:
                    kb["switch_option"] = token.strip()
                    break
    return kb


def resolve_picture(picture, folder, theme):
    if picture and not Path(picture).is_absolute():
        picture = str(folder / picture)
    if not picture_suffix(picture):
        picture = str(folder / theme.get("desktop", {}).get("background", {}).get("image", ""))
    return picture if picture_suffix(picture) else ""


def build_background(mode, greeter_cfg, desktop_cfg, folder, theme, files):
    veil = normalize_rgba(greeter_cfg.get("background_veil"))
    if mode == "gradient":
        bg = dict(CLASSIC_GRADIENT)
        own = theme.get("greeter", {}).get("background", {})
        if isinstance(own, dict):
            for k in ("color", "color2"):
                rgba = normalize_rgba(own.get(k))
                if rgba:
                    bg[k] = rgba
        if veil:
            bg["veil"] = veil
        return bg

    if mode == "custom":
        raw = greeter_cfg.get("background_picture") or ""
        pic = resolve_picture(raw, folder, theme) if raw else ""
        if not pic:
            pic = resolve_picture(desktop_cfg.get("picture") or "", folder, theme)
    else:
        pic = resolve_picture(desktop_cfg.get("picture") or "", folder, theme)

    default_veil = "#00000033" if mode == "desktop_blurred" else "#00000026"
    bg = {"veil": veil or default_veil, "fit": "cover"}
    if mode == "desktop_blurred":
        blur = greeter_cfg.get("background_blur", 28)
        if isinstance(blur, (int, float)) and not isinstance(blur, bool):
            bg["blur"] = max(0, min(64, int(blur)))
    elif isinstance(greeter_cfg.get("background_blur"), (int, float)) and not isinstance(greeter_cfg.get("background_blur"), bool):
        b = max(0, min(64, int(greeter_cfg["background_blur"])))
        if b > 0:
            bg["blur"] = b

    if pic:
        bg["image"] = "background" + picture_suffix(pic).replace(".jpeg", ".jpg")
        files[bg["image"]] = Path(pic)
    return bg


def clock_settings(panel_cfg, greeter_cfg):
    clock = {"visible": greeter_cfg.get("show_clock", True) is not False}
    if greeter_cfg.get("match_clock_format", True) is not False:
        for src, dst in CLOCK_KEYS:
            if src in panel_cfg and panel_cfg[src] is not None:
                clock[dst] = panel_cfg[src]
    return clock


def status_settings(greeter_cfg):
    return {
        "keyboard": greeter_cfg.get("show_keyboard", True) is not False,
        "battery": greeter_cfg.get("show_battery", True) is not False,
        "hostname": greeter_cfg.get("show_hostname", True) is not False,
    }


def schedule_export(schedule_cfg, greeter_cfg, bg_mode, files):
    if greeter_cfg.get("follow_schedule", True) is False:
        return None
    if not isinstance(schedule_cfg, dict) or schedule_cfg.get("enabled") is not True:
        return None
    day_name, night_name = schedule_cfg.get("day"), schedule_cfg.get("night")
    if not isinstance(day_name, str) or not isinstance(night_name, str) or not day_name or not night_name or day_name == night_name:
        return None
    day_from = str(schedule_cfg.get("day_from", "07:00"))
    night_from = str(schedule_cfg.get("night_from", "19:00"))
    if day_from == night_from:
        return None

    try:
        from themes import find_theme, read_theme
        from user_choices import find_desktop_config, load_desktop_config
    except Exception:
        return None

    out = {"enabled": True, "day_from": day_from, "night_from": night_from}
    for slot, desktop_name in (("day", day_name), ("night", night_name)):
        try:
            cfg_path = find_desktop_config(desktop_name)
            if cfg_path is None:
                return None
            cfg = load_desktop_config(cfg_path)
            theme_name = cfg.get("theme")
            if not theme_name:
                return None
            tdir = find_theme(theme_name)
            tdict = read_theme(tdir)
        except BaseException:
            return None
        dark = is_dark_theme(tdict, tdir.name)
        variant = {
            "id": tdir.name,
            "appearance": "dark" if dark else "light",
            "colors": theme_colors(tdict, dark),
        }
        if bg_mode in ("desktop", "desktop_blurred"):
            pic = resolve_picture(cfg.get("desktop", {}).get("picture") or "", tdir, tdict)
            if pic:
                ext = picture_suffix(pic).replace(".jpeg", ".jpg")
                asset_name = f"background_{slot}{ext}"
                files[asset_name] = Path(pic)
                variant["background"] = {
                    "image": asset_name,
                    "veil": "#00000038" if dark else "#00000026",
                }
        out[slot] = variant
    return out


def plan(settings, ini, drm_driver):
    theme, folder = settings["theme"], Path(settings["theme_directory"])
    config, user = settings.get("config", {}), settings.get("user", {})
    choices = read_choices(settings)
    desktop_cfg = merged_table(config, choices, "desktop")
    greeter_cfg = merged_table(config, choices, "greeter")
    panel_cfg = merged_table(config, choices, "panel")
    input_cfg = merged_table(config, choices, "input")
    idle_cfg = merged_table(config, choices, "idle")
    display_cfg = merged_table(config, choices, "display")
    schedule_cfg = merged_table(config, choices, "theme_schedule")

    files = {}
    bg_mode = greeter_cfg.get("background_mode", "desktop")
    if bg_mode not in ("desktop", "desktop_blurred", "custom", "gradient"):
        bg_mode = "desktop"
    background = build_background(bg_mode, greeter_cfg, desktop_cfg, folder, theme, files)

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
    dark = is_dark_theme(theme, folder.name)
    handoff_style = greeter_cfg.get("handoff_style", "choreographed")
    if handoff_style not in ("choreographed", "simple", "instant"):
        handoff_style = "choreographed"

    display = {"outputs": outputs(ini), "main_output": display_cfg.get("main_screen", "")}
    if drm_driver:
        display["drm_driver"] = drm_driver
    dpms = greeter_cfg.get("screen_off_after_seconds")
    if dpms is None and greeter_cfg.get("follow_idle_screen_off", True):
        dpms = idle_cfg.get("screen_off_after_seconds", 0)
    if isinstance(dpms, int) and not isinstance(dpms, bool) and dpms > 0:
        display["idle_dpms_seconds"] = max(30, min(7200, dpms))

    look = {
        "schema": SCHEMA, "version": 1, "id": folder.name, "name": theme.get("theme", {}).get("name", folder.name),
        "appearance": "dark" if dark else "light",
        "background": background,
        "fonts": {k: interface[k] for k in ("family",) if k in interface},
        "colors": theme_colors(theme, dark),
        "cursor": {k: cursor[k] for k in ("theme", "size") if k in cursor},
        "keyboard": keyboard_settings(ini, input_cfg),
        "display": display,
        "clock": clock_settings(panel_cfg, greeter_cfg),
        "status": status_settings(greeter_cfg),
        "handoff": {"style": handoff_style},
        "accounts": accounts,
    }
    inp = input_settings(ini)
    if inp:
        look["input"] = inp

    animation = {}
    if handoff_style == "instant":
        animation["fade_in_ms"] = 0
        animation["fade_ms"] = 60
    wrong = theme.get("animation", {}).get("lock_wrong")
    if isinstance(wrong, dict) and wrong.get("kind") == "shake":
        shake = {}
        if isinstance(wrong.get("swings"), list):
            shake["kind"] = "swings"
            shake["swings"] = wrong["swings"]
        if isinstance(wrong.get("swing_ms"), int):
            shake["swing_ms"] = wrong["swing_ms"]
        if shake:
            animation["shake"] = shake
    if animation:
        look["animation"] = animation

    sched = schedule_export(schedule_cfg, greeter_cfg, bg_mode, files)
    if sched:
        look["schedule"] = sched

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
