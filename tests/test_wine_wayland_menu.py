"""Runs a Win32 program on Wine's Wayland driver and checks that its menu reaches the compositor.

    python test_wine_wayland_menu.py BRIDGE AGENTS_DIR APP64_EXE WINE

WINE is a wine whose Wayland driver publishes __wine_appmenu_address through org_kde_kwin_appmenu.
Needs wayfire and a private session bus (meson runs it under dbus-run-session).
"""

import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from test_wine_global_menu import MENU, REGISTRAR, REGISTRAR_PATH, REGISTRAR_XML, Lines, private_environment, stop


def start_compositor(environment, runtime):
    config = runtime / "wayfire.ini"
    config.write_text("[core]\nplugins = ipc ipc-rules stipc window-rules kde-appmenu\nxwayland = false\n"
                      "preferred_decoration_mode = server\n")
    environment.update(XDG_RUNTIME_DIR=str(runtime), _WAYFIRE_SOCKET=str(runtime / "ipc"), WLR_BACKENDS="headless",
                       WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1", WLR_LIBINPUT_NO_DEVICES="1")
    environment.pop("DISPLAY", None)
    log = open(runtime / "wayfire.log", "w")
    compositor = subprocess.Popen(["wayfire", "-c", str(config)], env=environment, stdout=log, stderr=log,
                                  start_new_session=True)
    end = time.monotonic() + 10
    while time.monotonic() < end:
        sockets = sorted(runtime.glob("wayland-[0-9]"))
        if compositor.poll() is not None:
            sys.exit(f"refused: the compositor exited {compositor.returncode}")
        if sockets and (runtime / "ipc").exists():
            environment["WAYLAND_DISPLAY"] = sockets[0].name
            return compositor
        time.sleep(.05)
    stop(compositor)
    sys.exit("refused: no private compositor came up")


class Ipc:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(3)
        self.socket.connect(path)

    def call(self, method, **data):
        payload = json.dumps(dict(method=method, data=data)).encode()
        self.socket.sendall(struct.pack("<I", len(payload)) + payload)
        size = struct.unpack("<I", self.read(4))[0]
        return json.loads(self.read(size))

    def read(self, size):
        result = b""
        while len(result) < size:
            part = self.socket.recv(size - len(result))
            if not part:
                raise AssertionError("the compositor closed its socket")
            result += part
        return result

    def view(self, title):
        return next((view for view in self.call("window-rules/list-views") if view.get("title") == title), None)

    def address(self, title):
        view = self.view(title)
        if not view:
            return None
        return tuple(self.call("window-rules/get-view-property", id=view["id"], property=name).get("value", "")
                     for name in ("kde-appmenu-service-name", "kde-appmenu-object-path"))


def run(bridge_path, agents, app_path, wine_path, home):
    from gi.repository import Gio, GLib

    environment = private_environment(home)
    environment["PATH"] = os.path.dirname(wine_path) + os.pathsep + environment["PATH"]
    # the bar's room is measured against wine's own frame, the compositor's frame would move it
    environment["WAYLANDDRV_SSD"] = "0"
    # a wayland socket path stops at 108 bytes
    runtime = Path(tempfile.mkdtemp(prefix="wl.", dir="/tmp"))
    compositor = start_compositor(environment, runtime)
    ipc = Ipc(environment["_WAYFIRE_SOCKET"])
    bridge = app = None
    calls = []

    def wait(predicate, what, seconds=10):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            while GLib.MainContext.default().iteration(False):
                pass
            if predicate():
                return
            if compositor.poll() is not None:
                raise AssertionError(f"the compositor died {compositor.returncode} waiting for {what}")
            time.sleep(.02)
        raise AssertionError(f"timed out waiting for {what}")

    def check(condition, message):
        if not condition:
            raise AssertionError(message)

    bus = Gio.bus_get_sync(Gio.BusType.SESSION)

    def registrar_call(connection, sender, path, interface, method, arguments, invocation):
        calls.append((method, sender, *arguments.unpack()))
        invocation.return_value(None)

    node = Gio.DBusNodeInfo.new_for_xml(REGISTRAR_XML)
    bus.register_object(REGISTRAR_PATH, node.interfaces[0], registrar_call, None, None)

    def own_registrar(owned):
        method, argument = ("RequestName", "(su)") if owned else ("ReleaseName", "(s)")
        values = (REGISTRAR, 0) if owned else (REGISTRAR,)
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method,
                      GLib.Variant(argument, values), None, Gio.DBusCallFlags.NONE, 2000, None)

    def layout(service, path, parent=0):
        reply = bus.call_sync(service, path, MENU, "GetLayout", GLib.Variant("(iias)", (parent, -1, [])),
                              None, Gio.DBusCallFlags.NONE, 3000, None)
        return reply.unpack()[1][2]

    def event(service, path, item, name):
        bus.call_sync(service, path, MENU, "Event", GLib.Variant("(isvu)", (item, name, GLib.Variant("s", ""), 0)),
                      None, Gio.DBusCallFlags.NONE, 3000, None)

    def owner(name):
        reply = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                              "GetNameOwner", GLib.Variant("(s)", (name,)), None, Gio.DBusCallFlags.NONE, 2000, None)
        return reply.unpack()[0]

    def client_top(lines):
        found = lines.last(r"\[app\] layout client_top=(\d+) menu=1")
        return int(found.group(1)) if found else None

    title = "Menu Test 64"
    try:
        booted = subprocess.run(["wine", "wineboot", "-i"], env=environment, capture_output=True, text=True,
                                timeout=120)
        check(booted.returncode == 0, f"wineboot failed: {booted.stderr[-400:]}")
        own_registrar(True)
        bridge = subprocess.Popen([bridge_path, "--agents", agents, "--only-prefix", environment["WINEPREFIX"]],
                                  env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                  start_new_session=True)
        bridge_lines = Lines("bridge", bridge.stderr)
        app = subprocess.Popen(["wine", app_path], env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True, start_new_session=True)
        app_lines = Lines("app", app.stdout)
        wait(lambda: app_lines.last(r"\[app\] ready xid=(\S+)"), "the program to show its windows", 60)
        check(app_lines.last(r"\[app\] ready xid=(\S+)").group(1) == "0x0", "the program did not run on Wayland")
        twin_top = int(app_lines.last(r"\[app\] twin client_top=(\d+)").group(1))
        wait(lambda: ipc.view(title), "the program's window in the compositor")

        wait(lambda: all(ipc.address(title) or ()), "the compositor to learn the window's menu", 40)
        service, path = ipc.address(title)
        bridge_name = owner("org.mavernix.WineMenu")
        check(service == bridge_name, f"the address names {service}, the bridge is {bridge_name}")
        top = layout(service, path)
        check([item[1].get("label") for item in top] == ["_File", "_View", "_Run"], f"top level: {top}")
        check(ipc.address("Menu Test Twin") in (None, ("", "")), "a window without a menu got an address")
        check(not [call for call in calls if call[0] == "RegisterWindow"], f"a Wayland window was registered: {calls}")
        print("[wine-wayland-menu] PASS the window's own surface carries the bridge's menu address", flush=True)

        elsewhere = Path(tempfile.mkdtemp(prefix="wl.", dir="/tmp"))
        with socket.socket(socket.AF_UNIX) as other_display:
            other_display.bind(str(elsewhere / "wayland-0"))
            outsider = subprocess.run(["timeout", "3", "dbus-run-session", "--", bridge_path, "--agents", agents],
                                      env=dict(environment, XDG_RUNTIME_DIR=str(elsewhere), WAYLAND_DISPLAY="wayland-0"),
                                      capture_output=True, text=True, timeout=10)
        shutil.rmtree(elsewhere, ignore_errors=True)
        prefix = os.path.realpath(environment["WINEPREFIX"])
        check(f"prefix={prefix}, its programs show on another display" in outsider.stderr and
              f"agent pid=" not in outsider.stderr, f"a bridge on another display reached in: {outsider.stderr[-600:]}")
        print("[wine-wayland-menu] PASS a bridge on another display leaves the program alone", flush=True)

        wait(lambda: client_top(app_lines) == twin_top, "the local bar to go")
        print(f"[wine-wayland-menu] PASS the local bar gives its room to the window client_top={twin_top}", flush=True)

        file_menu = next(item for item in top if item[1].get("label") == "_File")[2]
        new = next(item for item in file_menu if item[1].get("label") == "_New")
        event(service, path, new[0], "clicked")
        wait(lambda: app_lines.count("[app] command id=101"), "New to reach the program")
        print("[wine-wayland-menu] PASS an item runs in the program", flush=True)

        own_registrar(False)
        wait(lambda: client_top(app_lines) not in (None, twin_top), "the local bar to come back without a registrar")
        wait(lambda: ipc.address(title) == ("", ""), "the compositor to forget the address without a registrar")
        own_registrar(True)
        wait(lambda: ipc.address(title) == (service, path), "the address to come back with the registrar")
        wait(lambda: client_top(app_lines) == twin_top, "the local bar to go again")
        print("[wine-wayland-menu] PASS the bar and the address follow the registrar", flush=True)

        def order(what):
            done = app_lines.count("[app] done")
            app.stdin.write(what + "\n")
            app.stdin.flush()
            wait(lambda: app_lines.count("[app] done") > done, f"the program to {what}")

        first_view = ipc.view(title)["id"]
        order("flash")
        wait(lambda: (ipc.view(title) or {}).get("id") not in (None, first_view), "the window to come back as a new view")
        wait(lambda: ipc.address(title) == (service, path), "a window shown again at once to carry its address")
        order("hidewindow")
        wait(lambda: ipc.view(title) is None, "the window to leave the compositor")
        wait(lambda: bridge_lines.count("withdrawn window="), "the bridge to drop a hidden window's menu")
        order("address")
        check(app_lines.count("[app] address=0"), "a hidden window still names an atom the client let go")
        order("showwindow")
        wait(lambda: all(ipc.address(title) or ()), "a window shown again later to get a menu again")
        again = ipc.address(title)
        check([item[1].get("label") for item in layout(*again)] == ["_File", "_View", "_Run"], f"menu after showing: {again}")
        wait(lambda: client_top(app_lines) == twin_top, "the local bar to go after showing")
        print("[wine-wayland-menu] PASS a window hidden and shown again keeps its menu in the bar", flush=True)

        order("stall")
        wait(lambda: ipc.view("Menu Test Stalled"), "the window of a busy thread")
        wait(lambda: all(ipc.address("Menu Test Stalled") or ()), "a busy thread's window to get its menu once free", 20)
        print("[wine-wayland-menu] PASS a window whose thread was busy gets its menu when it answers", flush=True)

        os.killpg(bridge.pid, signal.SIGKILL)
        bridge.wait(timeout=5)
        wait(lambda: client_top(app_lines) not in (None, twin_top), "the local bar to come back when the bridge dies")
        wait(lambda: ipc.address(title) == ("", ""), "a dead bridge's address to leave the compositor")
        print("[wine-wayland-menu] PASS a dead bridge gives the program its bar back and clears the address", flush=True)

        app.stdin.write("quit\n")
        app.stdin.flush()
        app.wait(timeout=15)
        subprocess.run(["wineserver", "-w"], env=environment, timeout=30)
        print("[wine-wayland-menu] result=pass", flush=True)
    except (AssertionError, subprocess.TimeoutExpired):
        print("registrar calls:", *calls, sep="\n", file=sys.stderr)
        for lines in [locals().get("bridge_lines"), locals().get("app_lines")]:
            if lines:
                print(f"--- {lines.name}", *lines.drain(), sep="\n", file=sys.stderr)
        print("[wine-wayland-menu] result=fail", flush=True)
        raise
    finally:
        stop(app)
        stop(bridge)
        subprocess.run(["wineserver", "-k"], env=environment, timeout=15)
        stop(compositor)
        shutil.rmtree(runtime, ignore_errors=True)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    wine_path = os.path.abspath(sys.argv[4])
    search = os.path.dirname(wine_path) + os.pathsep + os.environ["PATH"]
    for program in (wine_path, shutil.which("wineserver", path=search), shutil.which("wayfire")):
        if not program or not os.access(program, os.X_OK):
            sys.exit(f"refused: {program} is missing")
    home = tempfile.mkdtemp(prefix="wine-wayland-menu-")
    try:
        run(*(os.path.abspath(argument) for argument in sys.argv[1:4]), wine_path, home)
    finally:
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    main()
