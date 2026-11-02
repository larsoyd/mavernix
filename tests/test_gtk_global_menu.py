import os
from pathlib import Path
import socket
import struct
import json
import subprocess
import sys
import tempfile
import time


def run_session(module):
    import gi
    gi.require_version("Gtk", "3.0")
    gi.require_version("Gdk", "3.0")
    from gi.repository import Gio, GLib, Gdk, Gtk

    def wait(predicate, seconds=4):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            while GLib.MainContext.default().iteration(False):
                pass
            if predicate():
                return
            time.sleep(.01)
        raise AssertionError("condition timed out")

    bus = Gio.bus_get_sync(Gio.BusType.SESSION)

    def call(service, path, interface, method, args=None):
        result = []
        def done(connection, reply):
            try:
                result.append(connection.call_finish(reply))
            except GLib.Error as error:
                result.append(error)
        bus.call(service, path, interface, method, args, None,
                 Gio.DBusCallFlags.NONE, 2000, None, done)
        wait(lambda: result)
        if isinstance(result[0], Exception):
            raise result[0]
        return result[0].unpack()

    def registrar(acquire):
        call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
             "RequestName" if acquire else "ReleaseName",
             GLib.Variant("(su)", ("com.canonical.AppMenu.Registrar", 0)) if acquire
             else GLib.Variant("(s)", ("com.canonical.AppMenu.Registrar",)))

    ipc = socket.socket(socket.AF_UNIX)
    ipc.settimeout(3)
    ipc.connect(os.environ["_WAYFIRE_SOCKET"])

    def request(method, **data):
        payload = json.dumps(dict(method=method, data=data)).encode()
        ipc.sendall(struct.pack("<I", len(payload)) + payload)
        def read(size):
            result = b""
            while len(result) < size:
                part = ipc.recv(size - len(result))
                if not part:
                    raise RuntimeError("compositor disconnected")
                result += part
            return result
        return json.loads(read(struct.unpack("<I", read(4))[0]))

    Gtk.init([])
    window = Gtk.Window(title="Menu engine test")
    window.set_default_size(500, 300)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    window.add(box)
    group = Gtk.AccelGroup()
    window.add_accel_group(group)
    activated = []

    def make_bar(label):
        bar = Gtk.MenuBar()
        top = Gtk.MenuItem.new_with_mnemonic(label)
        submenu = Gtk.Menu()
        item = Gtk.MenuItem.new_with_mnemonic("_Run")
        item.connect("activate", lambda *_: activated.append(label))
        item.add_accelerator("activate", group, Gdk.KEY_r, Gdk.ModifierType.CONTROL_MASK,
                             Gtk.AccelFlags.VISIBLE)
        submenu.append(item)
        top.set_submenu(submenu)
        bar.append(top)
        box.pack_start(bar, False, False, 0)
        bar.show_all()
        return bar, item

    bar, item = make_bar("_File")
    box.pack_end(Gtk.Label(label="Content"), True, True, 0)
    window.show_all()
    wait(lambda: window.get_mapped())

    def height(widget):
        return widget.get_preferred_height().natural_height

    wait(lambda: height(bar) > 0)
    registrar(True)
    wait(lambda: any(v.get("title") == "Menu engine test"
                     for v in request("window-rules/list-views")))
    view = next(v for v in request("window-rules/list-views") if v.get("title") == "Menu engine test")

    def address():
        return tuple(request("window-rules/get-view-property", id=view["id"], property=p).get("value", "")
                     for p in ("kde-appmenu-service-name", "kde-appmenu-object-path"))

    wait(lambda: all(address()))
    service, path = address()

    def layout():
        return call(service, path, "com.canonical.dbusmenu", "GetLayout", GLib.Variant("(iias)", (0, -1, [])))[1]

    def flatten(node):
        yield node
        for child in node[2]:
            yield from flatten(child)

    wait(lambda: any(n[1].get("label") == "_Run" for n in flatten(layout())))
    assert bar.get_visible(), "export must preserve application visibility"
    assert bar.get_mapped(), "export must preserve GTK accelerator mapping"
    assert height(bar) == 0, "exported menu must occupy no layout height"
    assert Gtk.accel_groups_activate(window, Gdk.KEY_r, Gdk.ModifierType.CONTROL_MASK)
    assert activated == ["_File"], activated
    request("window-rules/focus-view", id=view["id"])
    for key in ("KEY_LEFTCTRL", "KEY_R"):
        request("stipc/feed_key", key=key, state=True)
    for key in ("KEY_R", "KEY_LEFTCTRL"):
        request("stipc/feed_key", key=key, state=False)
    wait(lambda: len(activated) == 2)
    assert window.mnemonic_activate(Gdk.KEY_f, Gdk.ModifierType.MOD1_MASK)
    submenu = bar.get_children()[0].get_submenu()
    wait(lambda: submenu.get_mapped())
    submenu.popdown()
    bar.cancel()
    item.set_sensitive(False)
    assert not Gtk.accel_groups_activate(window, Gdk.KEY_r, Gdk.ModifierType.CONTROL_MASK)
    item.set_sensitive(True)
    entry = next(n for n in flatten(layout()) if n[1].get("label") == "_Run")
    assert entry[1].get("shortcut") == [["Control", "r"]], entry[1]
    call(service, path, "com.canonical.dbusmenu", "Event",
         GLib.Variant("(isvu)", (entry[0], "clicked", GLib.Variant("i", 0), 0)))
    wait(lambda: len(activated) == 3)
    item.set_label("Updated")
    wait(lambda: any(n[1].get("label") == "Updated" for n in flatten(layout())))
    print("[gtk-menu] PASS export, shortcuts, sensitivity, remote activation, live labels", flush=True)

    registrar(False)
    wait(lambda: height(bar) > 0)
    bar.hide()
    registrar(True)
    time.sleep(.1)
    registrar(False)
    wait(lambda: not bar.get_visible())
    bar.show()
    registrar(True)
    wait(lambda: height(bar) == 0)
    bar.hide()
    registrar(False)
    wait(lambda: not bar.get_visible())
    print("[gtk-menu] PASS fallback preserves application hide requests", flush=True)

    bar.destroy()
    registrar(True)
    replacement, item = make_bar("_Replacement")
    wait(lambda: height(replacement) == 0)
    service, path = address()
    wait(lambda: any(n[1].get("label") == "_Replacement" for n in flatten(layout())))
    second, _ = make_bar("_Second")
    wait(lambda: height(replacement) > 0 and height(second) > 0)
    second.destroy()
    wait(lambda: height(replacement) == 0)
    print("[gtk-menu] PASS late/replaced bars and multiple-bar fallback", flush=True)

    window.hide()
    wait(lambda: not window.get_mapped())
    window.show()
    wait(lambda: window.get_mapped() and height(replacement) == 0)
    view = next(v for v in request("window-rules/list-views") if v.get("title") == "Menu engine test")
    wait(lambda: all(address()))
    service, path = address()
    assert any(n[1].get("label") == "_Replacement" for n in flatten(layout()))
    window.destroy()
    registrar(False)
    print("[gtk-menu] PASS remap and destruction", flush=True)

    def settle():
        end = time.monotonic() + .4
        wait(lambda: time.monotonic() > end)

    def maximized_look(subject, child, look):
        subject.show_all()
        wait(subject.get_mapped)
        subject.maximize()
        wait(subject.is_maximized)
        settle()
        seen = look(child)
        subject.unmaximize()
        wait(lambda: not subject.is_maximized())
        settle()
        return seen

    def content_top(child):
        return child.translate_coordinates(child.get_toplevel(), 0, 0)[1]

    def stock():
        subject = Gtk.Window(title="Stock")
        subject.set_default_size(400, 300)
        child = Gtk.Label(label="Content")
        subject.add(child)
        return subject, child

    marker = Path(os.environ["XDG_RUNTIME_DIR"]) / "mavernix/hide-maximized-titlebars"
    wait(marker.exists)
    subject, child = stock()
    top = maximized_look(subject, child, content_top)
    assert top == 0, f"stock bar stayed maximized top={top}"
    assert content_top(child) > 0, "stock bar did not come back on restore"
    subject.destroy()

    subject, child = stock()
    custom = Gtk.HeaderBar(title="Custom")
    subject.set_titlebar(custom)
    assert maximized_look(subject, custom, Gtk.Widget.get_child_visible), "custom title bar was hidden"
    subject.destroy()

    subject, child = stock()
    late = Gtk.HeaderBar(title="Late")
    subject.show_all()
    subject.set_titlebar(late)
    assert maximized_look(subject, late, Gtk.Widget.get_child_visible), "title bar set after showing was hidden"
    subject.destroy()

    dialog = Gtk.Dialog(title="Dialog")
    label = Gtk.Label(label="Content")
    dialog.get_content_area().add(label)
    top = maximized_look(dialog, label, content_top)
    assert top > 0, f"dialog lost its bar top={top}"
    dialog.destroy()

    subject, child = stock()
    subject.set_type_hint(Gdk.WindowTypeHint.UTILITY)
    top = maximized_look(subject, child, content_top)
    assert top > 0, f"utility window lost its bar top={top}"
    subject.destroy()

    config = Path(os.environ["XDG_RUNTIME_DIR"]) / "wayfire.ini"
    pending = config.with_suffix(".new")
    pending.write_text(config.read_text().replace(" borderless-maximized", ""))
    pending.replace(config)
    wait(lambda: not marker.exists())
    subject, child = stock()
    top = maximized_look(subject, child, content_top)
    assert top > 0, f"bar hidden with the extension off top={top}"
    subject.destroy()

    subject, child = stock()
    subject.set_hide_titlebar_when_maximized(True)
    top = maximized_look(subject, child, content_top)
    assert top == 0, f"program's own hide request lost top={top}"
    subject.destroy()
    print("[gtk-menu] PASS stock title bars follow the extension", flush=True)


def session(module):
    runtime = Path(os.environ["XDG_RUNTIME_DIR"])
    config = runtime / "wayfire.ini"
    routes = "--routes" in sys.argv
    plugins = "ipc ipc-rules stipc window-rules kde-appmenu"
    if routes:
        plugins += " global-menu gtk-shell"
    else:
        plugins += " borderless-maximized"
    config.write_text(f"[core]\nplugins = {plugins}\nxwayland = {'true' if routes else 'false'}\n")
    with (runtime / "wayfire.log").open("w+") as log:
        compositor = subprocess.Popen(["wayfire", "-c", str(config)], stdout=log, stderr=log,
                                      start_new_session=True)
        try:
            end = time.monotonic() + 10
            while time.monotonic() < end:
                sockets = list(runtime.glob("wayland-[0-9]"))
                if compositor.poll() is not None:
                    raise RuntimeError(f"compositor exited {compositor.returncode}")
                if sockets and Path(os.environ["_WAYFIRE_SOCKET"]).exists():
                    os.environ["WAYLAND_DISPLAY"] = sockets[0].name
                    if routes:
                        from menu_routes import run
                        run(sys.argv[sys.argv.index("--routes") + 1])
                    else:
                        run_session(module)
                    return
                time.sleep(.05)
            raise RuntimeError("compositor startup timed out")
        finally:
            if compositor.poll() is None:
                subprocess.run(["ps", "-o", "pid,pgid,args", "-p", str(compositor.pid)], timeout=3)
                compositor.terminate()
                compositor.wait(timeout=5)
            log.seek(0)
            text = log.read()
            if compositor.returncode not in (0, -15):
                print(text, file=sys.stderr)
                raise AssertionError(f"compositor exited {compositor.returncode}")


if __name__ == "__main__":
    module = str(Path(sys.argv[1]).resolve())
    if "--session" in sys.argv:
        session(module)
    else:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        with tempfile.TemporaryDirectory(prefix="mavernix-menu-") as folder:
            root = Path(folder)
            environment = dict(os.environ)
            for name in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "GTK_MODULES"):
                environment.pop(name, None)
            environment.update(HOME=folder, XDG_RUNTIME_DIR=folder,
                               XDG_CONFIG_HOME=folder + "/config", XDG_DATA_HOME=folder + "/data",
                               XDG_CACHE_HOME=folder + "/cache", XDG_STATE_HOME=folder + "/state",
                               _WAYFIRE_SOCKET=folder + "/ipc", WLR_BACKENDS="headless",
                               WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                               WLR_LIBINPUT_NO_DEVICES="1", PULSE_SERVER="unix:/nonexistent",
                               GDK_BACKEND="wayland", GTK3_MODULES=module, KDE_DEBUG="1")
            if "--routes" in sys.argv:
                environment["WAYFIRE_PLUGIN_PATH"] = str(Path(module).parent.parent / "compositor/global-menu")
                environment["WAYFIRE_PLUGIN_XML_PATH"] = str(Path(__file__).resolve().parent.parent / "src/compositor/global-menu/metadata")
            else:
                environment["WAYFIRE_PLUGIN_PATH"] = str(Path(module).parent.parent / "compositor/borderless-maximized")
                environment["WAYFIRE_PLUGIN_XML_PATH"] = str(Path(__file__).resolve().parent.parent / "src/compositor/borderless-maximized/metadata")
            bus_config = root / "bus.conf"
            bus_config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>'
                                  '<policy context="default"><allow send_destination="*"/>'
                                  '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
            result = subprocess.run(["timeout", "--kill-after=3", "55", "dbus-run-session",
                                     "--config-file=" + str(bus_config), "--", sys.executable,
                                     __file__, module, "--session", *sys.argv[2:]], env=environment, timeout=60)
            sys.exit(result.returncode)
