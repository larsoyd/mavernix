import json
import os
from pathlib import Path
import shlex
import socket
import struct
import subprocess
import sys
import time

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gio, GLib, Gtk


def wait(predicate, seconds=6):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while GLib.MainContext.default().iteration(False):
            pass
        if predicate():
            return
        time.sleep(.02)
    raise AssertionError("menu route condition timed out")


def call(bus, service, path, interface, method, args=None):
    result = []
    def done(connection, reply):
        try:
            result.append(connection.call_finish(reply))
        except GLib.Error as error:
            result.append(error)
    bus.call(service, path, interface, method, args, None, Gio.DBusCallFlags.NONE, 2000, None, done)
    wait(lambda: result)
    if isinstance(result[0], Exception):
        raise result[0]
    return result[0].unpack()


def stop(process):
    if process.poll() is None:
        subprocess.run(["ps", "-o", "pid,pgid,args", "-p", str(process.pid)], timeout=3)
        process.terminate()
        process.wait(timeout=5)
    assert process.returncode in (0, -15), f"{process.args} exited {process.returncode}"


def flatten(items):
    for item in items:
        yield item
        yield from flatten(item.get("children", []))


def run(helper):
    runtime = Path(os.environ["XDG_RUNTIME_DIR"])
    ipc = socket.socket(socket.AF_UNIX)
    ipc.settimeout(3)
    ipc.connect(os.environ["_WAYFIRE_SOCKET"])
    def request(method, **data):
        payload = json.dumps(dict(method=method, data=data)).encode()
        ipc.sendall(struct.pack("<I", len(payload)) + payload)
        def read(size):
            data = b""
            while len(data) < size:
                part = ipc.recv(size - len(data))
                if not part:
                    raise RuntimeError("compositor disconnected")
                data += part
            return data
        return json.loads(read(struct.unpack("<I", read(4))[0]))

    display_file = runtime / "display"
    request("stipc/run", cmd="sh -c " + shlex.quote('printf "%s" "$DISPLAY" > ' + shlex.quote(str(display_file))))
    wait(lambda: display_file.exists() and display_file.read_text())
    display = display_file.read_text()
    env = dict(os.environ, WAYFIRE_SOCKET=os.environ["_WAYFIRE_SOCKET"], DISPLAY=display)
    log = (runtime / "probe.log").open("w+")
    probe = subprocess.Popen([helper], env=env, stdout=log, stderr=log, start_new_session=True)
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    def probe_call(method, args=None):
        assert probe.poll() is None, f"probe exited {probe.returncode}"
        return call(bus, "org.mavernix.MenuTest", "/", "org.mavernix.MenuTest", method, args)
    def state():
        return json.loads(probe_call("State")[0])
    def entry(label):
        return next((item for item in flatten(state()["items"]) if item.get("label") == label), None)
    fixtures = []
    try:
        wait(lambda: call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "NameHasOwner", GLib.Variant("(s)", ("org.mavernix.MenuTest",)))[0])
        app = Gtk.Application(application_id="org.mavernix.GMenuFixture")
        app.register(None)
        activated = []
        action = Gio.SimpleAction.new("run", None)
        action.connect("activate", lambda *_: activated.append(True))
        app.add_action(action)
        root = Gio.Menu()
        submenu = Gio.Menu()
        submenu.append("_GMenu Run", "app.run")
        root.append_submenu("_File", submenu)
        app.set_menubar(root)
        window = Gtk.ApplicationWindow(application=app, title="GMenu fixture")
        window.set_show_menubar(False)
        window.set_default_size(400, 250)
        window.add(Gtk.Label(label="GMenu fixture"))
        window.show_all()
        wait(lambda: entry("GMenu Run"))
        assert state()["address"]["protocol"] == "gmenu", state()
        probe_call("Activate", GLib.Variant("(i)", (entry("GMenu Run")["id"],)))
        wait(lambda: activated)
        action.set_enabled(False)
        wait(lambda: not entry("GMenu Run")["enabled"])
        submenu.append("_Added", "app.run")
        wait(lambda: entry("Added"))
        print("[menu-routes] PASS Wayland GMenu discovery, activation, updates", flush=True)

        for mode in ("dbusmenu", "gmenu"):
            report = runtime / (mode + ".activated")
            fixture_env = dict(env, GDK_BACKEND="x11")
            fixture_env.pop("WAYLAND_DISPLAY", None)
            fixture = subprocess.Popen([sys.executable, __file__, "--fixture", mode, str(report)],
                                       env=fixture_env, stdout=log, stderr=log, start_new_session=True)
            fixtures.append(fixture)
            wait(lambda: entry("X11 Run"))
            assert state()["address"]["protocol"] == mode, state()
            views = request("window-rules/list-views")
            view = next(v for v in views if v.get("title") == "X11 fixture")
            assert request("global-menu/address", id=view["id"]).get("xid", 0) > 0
            probe_call("Activate", GLib.Variant("(i)", (entry("X11 Run")["id"],)))
            wait(lambda: report.exists())
            stop(fixture)
            wait(lambda: not entry("X11 Run"))
            print(f"[menu-routes] PASS X11 {mode} association and activation", flush=True)
        window.destroy()
        wait(lambda: not state()["items"])
        if "--apps" in sys.argv:
            evidence = Path(os.environ["MENU_EVIDENCE_DIR"])
            evidence.mkdir(parents=True, exist_ok=True)
            app_log = (evidence / "gimp.log").open("w")
            gimp = subprocess.Popen(["/usr/bin/gimp-3.2", "--new-instance", "--no-data", "--no-fonts",
                                     "--no-splash", "--console-messages", "--stack-trace-mode=never"],
                                    env=env, stdout=app_log, stderr=app_log, start_new_session=True)
            fixtures.append(gimp)
            def gimp_main():
                for view in request("window-rules/list-views"):
                    if "gimp" in view.get("app-id", "").lower():
                        address = request("global-menu/address", id=view["id"])
                        if address.get("protocol") == "dbusmenu":
                            return view
                return None
            wait(gimp_main, seconds=25)
            request("window-rules/focus-view", id=gimp_main()["id"])
            wait(lambda: entry("File"))
            assert state()["address"]["protocol"] == "dbusmenu", state()
            panel = None
            if os.environ.get("MENU_PANEL_QML"):
                import re
                panel_log = (evidence / "panel.log").open("w+")
                panel = subprocess.Popen(["qs", "-p", os.environ["MENU_PANEL_QML"]],
                                         env=dict(env, QS_DISABLE_CRASH_HANDLER="1"),
                                         stdout=panel_log, stderr=panel_log, start_new_session=True)
                fixtures.append(panel)
                def title_position():
                    panel_log.seek(0)
                    match = re.search(r'MENU_TITLE=(\{[^\n]+\})', panel_log.read())
                    return json.loads(match[1]) if match else None
                wait(title_position)
                position = title_position()
                request("stipc/move_cursor", x=position["x"], y=position["y"])
                request("stipc/feed_button", mode="press", combo="BTN_LEFT")
                request("stipc/feed_button", mode="release", combo="BTN_LEFT")
                end = time.monotonic() + .3
                wait(lambda: time.monotonic() >= end)
                subprocess.run(["grim", str(evidence / "gimp-panel-menu.png")], check=True, timeout=10)
            subprocess.run(["grim", str(evidence / "gimp-global.png")], check=True, timeout=10)
            before = request("window-rules/list-views")
            new = next(item for item in flatten(state()["items"]) if item.get("label", "").startswith("New"))
            if panel:
                for key in ("KEY_DOWN", "KEY_ENTER"):
                    request("stipc/feed_key", key=key, state=True)
                    request("stipc/feed_key", key=key, state=False)
            else:
                probe_call("Activate", GLib.Variant("(i)", (new["id"],)))
            wait(lambda: len(request("window-rules/list-views")) > len(before))
            added = [v for v in request("window-rules/list-views") if v["id"] not in {b["id"] for b in before}]
            assert any("New" in v.get("title", "") for v in added), added
            for view in added:
                request("window-rules/close-view", id=view["id"])
            if panel:
                stop(panel)
                panel_log.close()
                print("[menu-routes] PASS production panel drawing and UI activation", flush=True)
            probe_call("Quit")
            probe.wait(timeout=5)
            assert probe.returncode == 0, f"probe exited {probe.returncode}"
            end = time.monotonic() + .3
            wait(lambda: time.monotonic() >= end)
            subprocess.run(["grim", str(evidence / "gimp-local.png")], check=True, timeout=10)
            for view in request("window-rules/list-views"):
                if "gimp" in view.get("app-id", "").lower():
                    request("window-rules/close-view", id=view["id"])
            gimp.wait(timeout=5)
            stop(gimp)
            app_log.close()
            print("[menu-routes] PASS real GIMP menu and New dialog, fallback captured", flush=True)
            probe = subprocess.Popen([helper], env=env, stdout=log, stderr=log, start_new_session=True)
            wait(lambda: call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                              "NameHasOwner", GLib.Variant("(s)", ("org.mavernix.MenuTest",)))[0])
            qt_env = dict(env, QT_QPA_PLATFORM="wayland")
            qt = subprocess.Popen(["/usr/bin/kolourpaint"], env=qt_env, stdout=log, stderr=log, start_new_session=True)
            fixtures.append(qt)
            wait(lambda: entry("File") and "kolourpaint" in state()["app"].lower())
            assert state()["address"]["protocol"] == "dbusmenu", state()
            print("[menu-routes] PASS real Kolourpaint native DBusMenu", flush=True)
            stop(qt)
        probe_call("Quit")
        probe.wait(timeout=5)
        assert probe.returncode == 0, f"probe exited {probe.returncode}"
    except Exception:
        print("[menu-routes] failure views=", request("window-rules/list-views"), flush=True)
        if probe.poll() is None:
            print("[menu-routes] failure state=", state(), flush=True)
        if os.environ.get("MENU_EVIDENCE_DIR"):
            subprocess.run(["grim", str(Path(os.environ["MENU_EVIDENCE_DIR"]) / "failure.png")], timeout=10)
        raise
    finally:
        for fixture in fixtures:
            stop(fixture)
        stop(probe)
        log.seek(0)
        print(log.read())
        log.close()


def fixture(mode, report):
    gi.require_version("GdkX11", "3.0")
    from gi.repository import GdkX11
    Gtk.init([])
    window = Gtk.Window(title="X11 fixture")
    window.set_default_size(400, 250)
    window.add(Gtk.Label(label="X11 fixture"))
    window.show_all()
    xid = GdkX11.X11Window.get_xid(window.get_window())
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    if mode == "dbusmenu":
        gi.require_version("Dbusmenu", "0.4")
        from gi.repository import Dbusmenu
        server = Dbusmenu.Server.new("/fixture/menu")
        root = Dbusmenu.Menuitem.new()
        item = Dbusmenu.Menuitem.new()
        item.property_set("label", "_X11 Run")
        item.property_set_bool("visible", True)
        item.property_set_bool("enabled", True)
        item.connect("item-activated", lambda *_: Path(report).write_text("activated"))
        root.child_append(item)
        server.set_root(root)
        call(bus, "com.canonical.AppMenu.Registrar", "/com/canonical/AppMenu/Registrar",
             "com.canonical.AppMenu.Registrar", "RegisterWindow", GLib.Variant("(uo)", (xid, "/fixture/menu")))
    else:
        import ctypes
        x11 = ctypes.CDLL("libX11.so.6")
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XInternAtom.restype = ctypes.c_ulong
        x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        x11.XChangeProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong,
                                       ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        x11.XFlush.argtypes = [ctypes.c_void_p]
        display = x11.XOpenDisplay(os.environ["DISPLAY"].encode())
        assert display
        model = Gio.Menu()
        model.append("_X11 Run", "app.run")
        group = Gio.SimpleActionGroup()
        action = Gio.SimpleAction.new("run", None)
        action.connect("activate", lambda *_: Path(report).write_text("activated"))
        group.add_action(action)
        bus.export_menu_model("/fixture/menu", model)
        bus.export_action_group("/fixture/actions", group)
        properties = {"_GTK_UNIQUE_BUS_NAME": bus.get_unique_name(),
                      "_GTK_MENUBAR_OBJECT_PATH": "/fixture/menu",
                      "_GTK_APPLICATION_OBJECT_PATH": "/fixture/actions"}
        for name, value in properties.items():
            atom = x11.XInternAtom(display, name.encode(), False)
            utf8 = x11.XInternAtom(display, b"UTF8_STRING", False)
            x11.XChangeProperty(display, xid, atom, utf8, 8, 0, value.encode(), len(value.encode()))
        x11.XFlush(display)
    GLib.timeout_add_seconds(35, Gtk.main_quit)
    Gtk.main()


if __name__ == "__main__":
    fixture(sys.argv[2], sys.argv[3])
