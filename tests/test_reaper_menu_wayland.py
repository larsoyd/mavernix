import json
import os
from pathlib import Path
import queue
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

REGISTRAR = "com.canonical.AppMenu.Registrar"
MENU = "com.canonical.dbusmenu"
CLOSED = "[audio device closed]"
SERVICE, PATH = "kde-appmenu-service-name", "kde-appmenu-object-path"
REQUEST = re.compile(r"-> (\w+)#(\d+)\.(\w+)\((.*)\)")


class Host:
    def __init__(self, host, swell, plugin, resources):
        environment = dict(os.environ, FAKE_SWELL_BACKEND="wayland", WAYLAND_DEBUG="client")
        self.child = subprocess.Popen([host, swell, plugin, resources], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True, start_new_session=True, env=environment)
        self.lines = queue.Queue()
        self.log = []
        for stream in (self.child.stdout, self.child.stderr):
            threading.Thread(target=self.read, args=(stream,), daemon=True).start()

    def read(self, stream):
        for line in stream:
            self.lines.put(line.rstrip("\n"))

    def drain(self):
        while not self.lines.empty():
            self.log.append(self.lines.get())

    def seen(self, text):
        self.drain()
        return any(text in line for line in self.log)

    def requests(self):
        self.drain()
        return [match.groups() for match in map(REQUEST.search, self.log) if match]

    def stop(self):
        if self.child.poll() is None:
            subprocess.run(["ps", "-o", "pid,pgid,args", "-p", str(self.child.pid)], timeout=3)
            self.child.kill()
            self.child.wait(timeout=5)


def wait(predicate, what, seconds=4):
    from gi.repository import GLib
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while GLib.MainContext.default().iteration(False):
            pass
        if predicate():
            return
        time.sleep(.01)
    raise AssertionError(f"timed out waiting for {what}")


def compositor_requests():
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
    return request


def dead_surface_calls(requests):
    # an appmenu whose surface is gone points the compositor at freed memory
    surface_of, dead, wrong = {}, set(), []
    for interface, number, method, arguments in requests:
        if interface == "org_kde_kwin_appmenu_manager" and method == "create":
            menu, surface = re.findall(r"#(\d+)", arguments)[:2]
            surface_of[menu] = surface
            dead.discard(menu)
        elif interface == "wl_surface" and method == "destroy":
            dead |= {menu for menu, surface in surface_of.items() if surface == number}
        elif interface == "org_kde_kwin_appmenu" and method == "set_address" and number in dead:
            wrong.append((number, arguments))
    return wrong


def session(host_path, swell, plugin):
    from gi.repository import Gio, GLib
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    request = compositor_requests()
    bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
                  GLib.Variant("(su)", (REGISTRAR, 0)), None, Gio.DBusCallFlags.NONE, 2000, None)

    def view(number):
        found = [v for v in request("window-rules/list-views") if v.get("title") == f"reaper-host {number}"]
        return found[0] if found else None

    def address(number):
        found = view(number)
        if not found:
            return ("", "")
        return tuple(request("window-rules/get-view-property", id=found["id"], property=name).get("value", "")
                     for name in (SERVICE, PATH))

    def top(service, path):
        reply = bus.call_sync(service, path, MENU, "GetLayout", GLib.Variant("(iias)", (0, -1, [])),
                              None, Gio.DBusCallFlags.NONE, 2000, None)
        return [(item[0], item[1].get("label", "")) for item in reply.unpack()[1][2]]

    resources = tempfile.mkdtemp(prefix="reaper-resources-", dir=os.environ["HOME"])
    host = Host(host_path, swell, plugin, resources)

    def send(command):
        host.child.stdin.write(command + "\n")
        host.child.stdin.flush()
        wait(lambda: host.seen(f"[host] ok {command}"), f"host to answer {command!r}")

    def check(condition, message):
        if not condition:
            host.drain()
            raise AssertionError(message)

    try:
        wait(lambda: host.seen("[host] loaded result=1"), "the plugin to load")
        wait(lambda: host.seen("[reaper-menu] registrar owner="), "the plugin to see the registrar")
        send(f"status {CLOSED}")
        send("tick 10")
        wait(lambda: all(address(1)), "the main window's menu address")
        service, path = address(1)
        check([label for _, label in top(service, path)] == ["_File", "_Help", CLOSED],
              f"the main window's address leads elsewhere: {top(service, path)}")
        print("[reaper-wayland] PASS the main window's surface carries its menu", flush=True)

        send("window 2 0 shared")
        send("window 3 0 menu")
        send("tick 10")
        wait(lambda: all(address(2)) and all(address(3)), "the secondary windows' addresses")
        check(address(2) == (service, path), f"a window without a menu lost the main one: {address(2)}")
        check(address(3)[1] != path and [label for _, label in top(*address(3))] == ["_Edit"],
              f"a window's own menu is not its own: {address(3)}")
        print("[reaper-wayland] PASS other windows get the main menu or their own", flush=True)

        def creates():
            return sum(1 for interface, _, method, _ in host.requests()
                       if interface == "org_kde_kwin_appmenu_manager" and method == "create")
        before = creates()
        send("tick 10")
        send("tick 10")
        check(creates() == before, f"unchanged windows were announced again: {creates() - before} creates")
        print("[reaper-wayland] PASS an unchanged window is announced once", flush=True)

        send("window 4 0 menu unmapped")
        send("tick 10")
        send("map 4")
        wait(lambda: all(address(4)), "a window announced before its first frame")
        print("[reaper-wayland] PASS a window announced before its first frame keeps its menu", flush=True)

        send("remap 2")
        reused = host.seen("[host] remapped window=2 same_surface=1")
        send("tick 10")
        wait(lambda: address(2) == (service, path), "a reopened window's menu")
        print(f"[reaper-wayland] PASS a reopened window gets its menu again same_surface={int(reused)}", flush=True)

        send("hide 3")
        send("tick 10")
        send("show 3")
        send("tick 10")
        wait(lambda: all(address(3)), "a window shown again")
        check(not dead_surface_calls(host.requests()), f"menu set on a dead surface: {dead_surface_calls(host.requests())}")
        print("[reaper-wayland] PASS no menu address ever reaches a destroyed surface", flush=True)

        def own_registrar(owned):
            method, argument = ("RequestName", "(su)") if owned else ("ReleaseName", "(s)")
            bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method,
                          GLib.Variant(argument, (REGISTRAR, 0) if owned else (REGISTRAR,)), None,
                          Gio.DBusCallFlags.NONE, 2000, None)
        own_registrar(False)
        wait(lambda: not any(address(1)), "the menus to leave with the menu bar")
        own_registrar(True)
        wait(lambda: sum("[reaper-menu] registrar owner=" in line for line in host.log) == 2 or host.drain(),
             "the plugin to see the menu bar again")
        send("tick 10")
        wait(lambda: address(1) == (service, path), "the menus to come back with the menu bar")
        print("[reaper-wayland] PASS a restarted menu bar takes the menus away and gets them back", flush=True)

        made = set()
        for interface, _, method, arguments in host.requests():
            if interface == "org_kde_kwin_appmenu_manager" and method == "create":
                made.update(re.findall(r"new id org_kde_kwin_appmenu#(\d+)", arguments))
        check(made, "no appmenu was ever created")
        send("remap 2")
        send("unload")
        wait(lambda: not any(address(1)), "the main window's menu to be withdrawn on unload")
        check(not any(address(2)) and not any(address(4)), f"unload left addresses: {address(2)} {address(4)}")
        released = {n for i, n, m, _ in host.requests() if i == "org_kde_kwin_appmenu" and m == "release"}
        check(made <= released, f"appmenus never released: {sorted(made - released)}")
        check(any(i == "org_kde_kwin_appmenu_manager" and m == "release" for i, _, m, _ in host.requests()),
              "the appmenu manager was never released")
        check(not dead_surface_calls(host.requests()), "unload set a menu on a dead surface")
        print("[reaper-wayland] PASS unloading withdraws every menu and releases every appmenu", flush=True)

        check(not host.seen("[host] event dispatched inside the plugin"), "the plugin dispatched the host's queue")
        print("[reaper-wayland] PASS the plugin never dispatches the program's own queue", flush=True)
        send("quit")
        host.child.wait(timeout=5)
        check(host.child.returncode == 0, f"host exited {host.child.returncode}")
    except AssertionError:
        host.drain()
        print("host log:", *[line for line in host.log if "@" not in line or "appmenu" in line][-80:],
              sep="\n", file=sys.stderr)
        raise
    finally:
        host.stop()


def no_manager(host_path, swell, plugin):
    from gi.repository import Gio, GLib
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
                  GLib.Variant("(su)", (REGISTRAR, 0)), None, Gio.DBusCallFlags.NONE, 2000, None)
    resources = Path(tempfile.mkdtemp(prefix="reaper-nomanager-", dir=os.environ["HOME"]))
    theme = resources / "libSwell-user.colortheme"
    theme.write_text("menubar_height 0\nmenu_font_size 14\n")
    host = Host(host_path, swell, plugin, str(resources))
    try:
        wait(lambda: host.seen("[host] loaded result="), "the plugin to answer")
        if not host.seen("[host] loaded result=0") or not host.seen("no appmenu manager"):
            raise AssertionError("the plugin stayed without a way to show its menus:\n" + "\n".join(host.log[-20:]))
        if theme.read_text() != "menu_font_size 14\n":
            raise AssertionError(f"REAPER lost its own bar with nowhere else to show it: {theme.read_text()!r}")
        print("[reaper-wayland] PASS without the compositor's menu protocol REAPER keeps its own bar", flush=True)
    finally:
        host.stop()


def with_compositor(plugins, run):
    runtime = Path(os.environ["XDG_RUNTIME_DIR"])
    config = runtime / "wayfire.ini"
    config.write_text(f"[core]\nplugins = ipc ipc-rules stipc window-rules {plugins}\nxwayland = false\n")
    with (runtime / "wayfire.log").open("w+") as log:
        compositor = subprocess.Popen(["wayfire", "-c", str(config)], stdout=log, stderr=log, start_new_session=True)
        try:
            end = time.monotonic() + 10
            while time.monotonic() < end:
                if compositor.poll() is not None:
                    raise RuntimeError(f"compositor exited {compositor.returncode}")
                sockets = list(runtime.glob("wayland-[0-9]"))
                if sockets and Path(os.environ["_WAYFIRE_SOCKET"]).exists():
                    os.environ["WAYLAND_DISPLAY"] = sockets[0].name
                    run()
                    return
                time.sleep(.05)
            raise RuntimeError("compositor startup timed out")
        finally:
            if compositor.poll() is None:
                compositor.terminate()
                compositor.wait(timeout=5)
            if compositor.returncode not in (0, -15):
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise AssertionError(f"compositor exited {compositor.returncode}")


if __name__ == "__main__":
    if "--session" in sys.argv:
        sys.argv.remove("--session")
        arguments = sys.argv[1:4]
        if sys.argv[4] == "no-manager":
            with_compositor("", lambda: no_manager(*arguments))
        else:
            with_compositor("kde-appmenu", lambda: session(*arguments))
    else:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        arguments = [str(Path(argument).resolve()) for argument in sys.argv[1:4]]
        for case in ("menus", "no-manager"):
            with tempfile.TemporaryDirectory(prefix="mavernix-reaper-wl-") as folder:
                root = Path(folder)
                bus_config = root / "bus.conf"
                bus_config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>'
                                      '<policy context="default"><allow send_destination="*"/>'
                                      '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
                environment = dict(os.environ)
                for name in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
                    environment.pop(name, None)
                environment.update(HOME=folder, XDG_RUNTIME_DIR=folder, XDG_CONFIG_HOME=folder + "/config",
                                   XDG_DATA_HOME=folder + "/data", XDG_CACHE_HOME=folder + "/cache",
                                   XDG_STATE_HOME=folder + "/state", _WAYFIRE_SOCKET=folder + "/ipc",
                                   WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                                   WLR_LIBINPUT_NO_DEVICES="1", PULSE_SERVER="unix:/nonexistent", KDE_DEBUG="1")
                result = subprocess.run(["timeout", "--kill-after=3", "40", "dbus-run-session",
                                         "--config-file=" + str(bus_config), "--", sys.executable, __file__,
                                         *arguments, case, "--session"], env=environment, timeout=50)
                if result.returncode:
                    sys.exit(result.returncode)
