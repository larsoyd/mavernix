"""Runs a Win32 program with a menu bar under Wine and checks that its menus reach the registrar.

    python test_wine_global_menu.py BRIDGE AGENTS_DIR APP64_EXE APP32_EXE

Needs wine, Xvfb, xwininfo and a private session bus (meson runs it under dbus-run-session).
"""

import hashlib
import hmac
import os
import queue
import re
import resource
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

REGISTRAR = "com.canonical.AppMenu.Registrar"
REGISTRAR_PATH = "/com/canonical/AppMenu/Registrar"
MENU = "com.canonical.dbusmenu"
REGISTRAR_XML = f"""<node><interface name="{REGISTRAR}">
<method name="RegisterWindow"><arg type="u" direction="in"/><arg type="o" direction="in"/></method>
<method name="UnregisterWindow"><arg type="u" direction="in"/></method>
</interface></node>"""


class Lines:
    def __init__(self, name, stream):
        self.name = name
        self.log = []
        self.incoming = queue.Queue()
        threading.Thread(target=self.read, args=(stream,), daemon=True).start()

    def read(self, stream):
        for line in stream:
            self.incoming.put(line.rstrip("\n"))

    def drain(self):
        while not self.incoming.empty():
            self.log.append(self.incoming.get())
        return self.log

    def count(self, text):
        return sum(text in line for line in self.drain())

    def last(self, pattern):
        found = [match for line in self.drain() if (match := re.search(pattern, line))]
        return found[-1] if found else None


def private_environment(home):
    if os.environ.get("DBUS_SESSION_BUS_ADDRESS", "") in ("", f"unix:path=/run/user/{os.getuid()}/bus"):
        sys.exit("refused: no private session bus, run under dbus-run-session")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if resource.getrlimit(resource.RLIMIT_CORE)[0] != 0:
        sys.exit("refused: core dumps are still on")
    alsa = os.path.join(home, "null-alsa.conf")
    with open(alsa, "w") as file:
        file.write("pcm.!default { type null }\nctl.!default { type hw card 0 }\n")
    environment = dict(os.environ, KDE_DEBUG="1", WINEPREFIX=os.path.join(home, "prefix"), WINEDEBUG="-all",
                       WINEDLLOVERRIDES="mscoree,mshtml=;winepulse.drv=d", ALSA_CONFIG_PATH=alsa,
                       PULSE_SERVER="unix:/nonexistent", XDG_CACHE_HOME=os.path.join(home, "cache"))
    environment.pop("WAYLAND_DISPLAY", None)
    return environment


def start_display(environment):
    reader, writer = os.pipe()
    display = subprocess.Popen(["Xvfb", "-displayfd", str(writer), "-screen", "0", "1280x800x24", "-nolisten", "tcp"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, pass_fds=(writer,),
                               start_new_session=True)
    os.close(writer)
    with os.fdopen(reader) as answer:
        number = answer.readline().strip()
    if not number.isdigit():
        stop(display)
        sys.exit("refused: no private X display came up")
    environment["DISPLAY"] = f":{number}"
    return display


def wine(environment, *arguments, seconds=120):
    return subprocess.run(["wine", *arguments], env=environment, capture_output=True, text=True, timeout=seconds)


def x_window(environment, title):
    tree = subprocess.run(["xwininfo", "-root", "-tree"], env=environment, capture_output=True, text=True,
                          timeout=5).stdout
    found = re.search(rf'^\s*(0x[0-9a-f]+) "{re.escape(title)}":', tree, re.M)
    return int(found.group(1), 16) if found else None


def in_prefix(prefix):
    found = []
    for pid in filter(str.isdigit, os.listdir("/proc")):
        try:
            with open(f"/proc/{pid}/environ", "rb") as file:
                if f"WINEPREFIX={prefix}".encode() in file.read().split(b"\0"):
                    with open(f"/proc/{pid}/cmdline", "rb") as names:
                        found.append(f"{pid}:{names.read().replace(b'\0', b' ').decode(errors='replace')[:80]}")
        except OSError:
            pass
    return found


def impostor(environment):
    """Points the running programs at a server of ours, which fails the proof once and passes it next."""
    token = "impostor-token"
    listener = socket.create_server(("127.0.0.1", 0))
    listener.settimeout(20)
    agent = os.path.join(environment["XDG_CACHE_HOME"], "mavernix", "wine-menu", "mavernix-wine-menu-agent64.exe")
    published = wine(dict(environment, MAVERNIX_WINE_MENU_PORT=str(listener.getsockname()[1]),
                          MAVERNIX_WINE_MENU_TOKEN=token), agent, seconds=30)
    if published.returncode != 0:
        raise AssertionError(f"the agent did not publish the impostor: {published.stderr[-300:]}")

    def meet(proof):
        connection, _ = listener.accept()
        connection.settimeout(5)
        nonce = os.urandom(16).hex()
        connection.sendall(f"challenge {nonce}\n".encode())
        hello = connection.recv(4096).decode().split("\n")[0].split()
        expected = hmac.new(token.encode(), f"client {nonce}".encode(), hashlib.sha256).hexdigest()
        if hello[:2] != ["hello", expected]:
            raise AssertionError(f"the program's proof does not match the token: {hello}")
        connection.sendall(f"welcome {proof(hello[2])}\n".encode())
        rest = b""
        try:
            while chunk := connection.recv(4096):
                rest += chunk
        except TimeoutError:
            rest += b" (still open)"
        connection.close()
        return rest

    after_lie = meet(lambda nonce: "0" * 64)
    if after_lie != b"":
        raise AssertionError(f"a program kept talking to a bridge that failed the proof: {after_lie[:80]}")
    honest = meet(lambda nonce: hmac.new(token.encode(), f"bridge {nonce}".encode(), hashlib.sha256).hexdigest())
    if b"menu " not in honest:
        raise AssertionError(f"the impostor check proves nothing, an honest bridge got no menu: {honest[:80]}")
    listener.close()


def stop(child):
    if child and child.poll() is None:
        subprocess.run(["ps", "-o", "pid,pgid,args", "-p", str(child.pid)], timeout=3)
        # a killed X server leaves its socket and lock behind
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=5)


def run(bridge_path, agents, app_path, narrow_app_path, home):
    from gi.repository import Gio, GLib

    environment = private_environment(home)
    display = start_display(environment)
    bridge = app = narrow_app = None
    calls = []

    def wait(predicate, what, seconds=10):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            while GLib.MainContext.default().iteration(False):
                pass
            if predicate():
                return
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

    def registered(xid, since=0):
        found = [call for call in calls[since:] if call[0] == "RegisterWindow" and call[2] == xid]
        return found[-1] if found else None

    def layout(service, path, parent=0):
        reply = bus.call_sync(service, path, MENU, "GetLayout", GLib.Variant("(iias)", (parent, -1, [])),
                              None, Gio.DBusCallFlags.NONE, 3000, None)
        return reply.unpack()[1][2]

    def event(service, path, item, name):
        bus.call_sync(service, path, MENU, "Event", GLib.Variant("(isvu)", (item, name, GLib.Variant("s", ""), 0)),
                      None, Gio.DBusCallFlags.NONE, 3000, None)

    def about_to_show(service, path, item):
        bus.call_sync(service, path, MENU, "AboutToShow", GLib.Variant("(i)", (item,)), None,
                      Gio.DBusCallFlags.NONE, 3000, None)

    def find(items, label):
        found = [item for item in items if item[1].get("label") == label]
        check(len(found) == 1, f"no single {label!r} among {[item[1].get('label') for item in items]}")
        return found[0]

    def client_top(lines):
        found = lines.last(r"\[app\] layout client_top=(\d+) menu=1")
        return int(found.group(1)) if found else None

    try:
        prefix_up = wine(environment, "wineboot", "-i")
        check(prefix_up.returncode == 0, f"wineboot failed: {prefix_up.stderr[-400:]}")
        before = wine(environment, "reg", "query", r"HKCU\Environment", "/v", "QT_QPA_PLATFORM")
        check("menus=native" not in before.stdout, "a fresh prefix already asks Qt for native menus")

        own_registrar(True)
        bridge = subprocess.Popen([bridge_path, "--agents", agents, "--only-prefix", environment["WINEPREFIX"]],
                                  env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                  start_new_session=True)
        bridge_lines = Lines("bridge", bridge.stderr)
        app = subprocess.Popen(["wine", app_path], env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True, start_new_session=True)
        app_lines = Lines("app", app.stdout)
        wait(lambda: app_lines.last(r"\[app\] twin client_top=(\d+)"), "the program to show its windows", 60)
        twin_top = int(app_lines.last(r"\[app\] twin client_top=(\d+)").group(1))
        wait(lambda: x_window(environment, "Menu Test 64"), "the program's X window")
        xid = x_window(environment, "Menu Test 64")

        wait(lambda: registered(xid), "the window to register its menu", 40)
        _, service, _, path = registered(xid)
        top = layout(service, path)
        check([item[1].get("label") for item in top] == ["_File", "_View", "_Run"], f"top level: {top}")
        check([item[1].get("children-display", "") for item in top] == ["submenu", "submenu", ""],
              f"only the menus open menus: {top}")
        print("[wine-menu] PASS the window registers its own bar with its X window", flush=True)

        wait(lambda: client_top(app_lines) == twin_top, "the local bar to go")
        print(f"[wine-menu] PASS the local bar gives its room to the window client_top={twin_top}", flush=True)

        file_menu = find(top, "_File")[2]
        new = find(file_menu, "_New")
        check(new[1].get("shortcut") == [["Control", "N"]], f"New lost its shortcut: {new[1]}")
        check(find(file_menu, "_Open...")[1].get("enabled") is False, "a grayed item is enabled")
        check(any(item[1].get("type") == "separator" for item in file_menu), "the separator is gone")
        view_menu = find(top, "_View")[2]
        check(find(view_menu, "_Status Bar")[1].get("toggle-state") == 1, "a checked item lost its mark")
        print("[wine-menu] PASS labels, shortcuts, grayed, separators and checks come through", flush=True)

        event(service, path, new[0], "clicked")
        wait(lambda: app_lines.count("[app] command id=101"), "New to reach the program")
        event(service, path, find(file_menu, "_Open...")[0], "clicked")
        event(service, path, find(top, "_Run")[0], "clicked")
        wait(lambda: app_lines.count("[app] command id=301"), "the bar's own command to reach the program")
        check(app_lines.count("[app] command id=102") == 0, "a grayed item ran")
        print("[wine-menu] PASS items and bar commands run in the program, grayed ones do not", flush=True)

        def order(child, lines, what):
            done = lines.count("[app] done")
            child.stdin.write(what + "\n")
            child.stdin.flush()
            wait(lambda: lines.count("[app] done") > done, f"the program to {what}")

        order(app, app_lines, "keymenu")
        order(app, app_lines, "childproc")
        check(app_lines.count("[app] entermenuloop") == 0, "alt walks a bar nobody can see")
        check(app_lines.count("[app] child proc kept=1"), "a control inside the window was taken over")
        print("[wine-menu] PASS alt leaves the hidden bar alone, controls stay untouched", flush=True)

        order(app, app_lines, "disable")
        event(service, path, new[0], "clicked")
        wait(lambda: bridge_lines.count("reason=window_disabled"), "a disabled window to refuse")
        order(app, app_lines, "enable")
        check(app_lines.count("[app] command id=101") == 1, "a window behind a dialog got a command")
        order(app, app_lines, "byposition")
        event(service, path, new[0], "clicked")
        wait(lambda: app_lines.count("[app] menucommand position=0 file=1"), "a by-position menu to say where")
        check(app_lines.count("[app] command id=101") == 1, "a by-position menu got a plain command")
        print("[wine-menu] PASS a disabled window refuses, a by-position menu hears its place", flush=True)

        port = int(bridge_lines.last(r"listening port=(\d+)").group(1))
        with socket.create_connection(("127.0.0.1", port), timeout=3) as stranger:
            check(stranger.recv(100).startswith(b"challenge "), "the bridge does not ask strangers to prove")
            stranger.sendall(b"hello " + b"0" * 64 + b" 00 1 stranger.exe\n")
            check(stranger.recv(100) == b"", "the bridge talks to a client without the token")
        print("[wine-menu] PASS the bridge refuses a client that does not hold the token", flush=True)

        recent = find(file_menu, "_Recent")
        about_to_show(service, path, recent[0])
        wait(lambda: app_lines.count("[app] initpopup recent=1"), "opening Recent to tell the program")

        def recent_now():
            return find(layout(service, path, find(layout(service, path), "_File")[0]), "_Recent")
        wait(lambda: [item[1].get("label") for item in recent_now()[2]] == ["Recent 1"], "Recent to be filled")
        refreshed = recent_now()
        event(service, path, refreshed[0], "closed")
        wait(lambda: app_lines.count("[app] uninitpopup recent"), "closing Recent to reach the program")
        print("[wine-menu] PASS a menu the program fills on opening shows what it filled", flush=True)

        app.stdin.write("rename\n")
        app.stdin.flush()
        wait(lambda: any(item[1].get("label") == "_New Window"
                         for item in layout(service, path, find(layout(service, path), "_File")[0])),
             "a renamed item to follow")
        renamed = find(layout(service, path, find(layout(service, path), "_File")[0]), "_New Window")
        check(renamed[0] == new[0], f"renaming rebuilt the menu, an open menu would lose its items: {renamed[0]} {new[0]}")
        print("[wine-menu] PASS changes in the program's menus follow", flush=True)

        own_registrar(False)
        wait(lambda: client_top(app_lines) not in (None, twin_top), "the local bar to come back without a registrar")
        with_bar = client_top(app_lines)
        before_calls = len(calls)
        own_registrar(True)
        wait(lambda: registered(xid, before_calls), "the window to register again")
        wait(lambda: client_top(app_lines) == twin_top, "the local bar to go again")
        print(f"[wine-menu] PASS the bar comes back while no registrar runs client_top={with_bar}", flush=True)

        os.killpg(bridge.pid, signal.SIGKILL)
        bridge.wait(timeout=5)
        wait(lambda: client_top(app_lines) == with_bar, "the local bar to come back when the bridge dies")
        print("[wine-menu] PASS a dead bridge gives the program its bar back", flush=True)


        impostor(environment)
        print("[wine-menu] PASS a program sends no menu to a bridge that cannot prove the token", flush=True)

        qt = wine(environment, "reg", "query", r"HKCU\Environment", "/v", "QT_QPA_PLATFORM", seconds=30)
        check("windows:menus=native" in qt.stdout, f"Qt programs are not asked for native menus: {qt.stdout}")
        print("[wine-menu] PASS the prefix asks Qt programs for Win32 menus", flush=True)

        bridge = subprocess.Popen([bridge_path, "--agents", agents, "--only-prefix", environment["WINEPREFIX"]],
                                  env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                  start_new_session=True)
        first_bridge_lines = bridge_lines
        bridge_lines = Lines("bridge", bridge.stderr)
        before_calls = len(calls)
        wait(lambda: registered(xid, before_calls), "a new bridge to find the running program", 40)
        _, service, _, path = registered(xid, before_calls)
        narrow_app = subprocess.Popen(["wine", narrow_app_path], env=environment, stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, start_new_session=True)
        narrow_lines = Lines("app32", narrow_app.stdout)
        wait(lambda: x_window(environment, "Menu Test 32"), "the 32-bit program's X window", 30)
        narrow_xid = x_window(environment, "Menu Test 32")
        wait(lambda: registered(narrow_xid), "the 32-bit program to register its menu", 30)
        _, narrow_service, _, narrow_path = registered(narrow_xid)
        narrow_file = find(layout(narrow_service, narrow_path), "_File")[2]
        event(narrow_service, narrow_path, find(narrow_file, "_New")[0], "clicked")
        wait(lambda: narrow_lines.count("[app] command id=101"), "New to reach the 32-bit program")
        narrow_pid = narrow_lines.last(r"\[app\] ready xid=\S+ pid=(\d+)").group(1)
        hooked_wide = [line for lines in (first_bridge_lines, bridge_lines) for line in lines.drain()
                       if re.search(rf"agent64\] hooked process={narrow_pid} ", line)]
        check(not hooked_wide, f"the 64-bit agent hooked a 32-bit program: {hooked_wide}")
        print("[wine-menu] PASS a 32-bit program gets the same menu, from the agent of its width", flush=True)
        os.killpg(narrow_app.pid, signal.SIGKILL)
        narrow_app.wait(timeout=15)
        wait(lambda: ("UnregisterWindow", narrow_service, narrow_xid) in calls, "a killed program's menu to go")
        print("[wine-menu] PASS a program that dies without a word loses its menu", flush=True)
        app.stdin.write("reuse\n")
        app.stdin.flush()
        wait(lambda: app_lines.last(r"\[app\] reuse found=\d"), "a thread to get a used id again", 90)
        check(app_lines.count("[app] reuse found=1"), "wine handed out no used thread id in 20000 threads")
        wait(lambda: x_window(environment, "Menu Test Reused") and registered(x_window(environment, "Menu Test Reused")),
             "the window on the reused thread to register", 20)
        wait(lambda: app_lines.last(rf"\[app\] reused client_top={twin_top}$"), "the reused thread's bar to go")
        print("[wine-menu] PASS a thread that gets a dead thread's id is taken again", flush=True)

        app.stdin.write("dropmenu\n")
        app.stdin.flush()
        wait(lambda: ("UnregisterWindow", service, xid) in calls, "a dropped menu to unregister")
        print("[wine-menu] PASS a new bridge takes over, a dropped menu unregisters", flush=True)

        app.stdin.write("quit\n")
        app.stdin.flush()
        app.wait(timeout=15)
        try:
            subprocess.run(["wineserver", "-w"], env=environment, timeout=30)
        except subprocess.TimeoutExpired:
            raise AssertionError(f"the prefix still runs: {in_prefix(environment['WINEPREFIX'])}")
        print("[wine-menu] PASS the agent does not keep the prefix running", flush=True)
        print("[wine-menu] result=pass", flush=True)
    except (AssertionError, subprocess.TimeoutExpired):
        print("registrar calls:", *calls, sep="\n", file=sys.stderr)
        for lines in [locals().get("first_bridge_lines"), locals().get("bridge_lines"), locals().get("app_lines"),
                      locals().get("narrow_lines")]:
            if lines:
                print(f"--- {lines.name}", *lines.drain(), sep="\n", file=sys.stderr)
        print("[wine-menu] result=fail", flush=True)
        raise
    finally:
        stop(app)
        stop(narrow_app)
        stop(bridge)
        subprocess.run(["wineserver", "-k"], env=environment, timeout=15)
        stop(display)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    for program in ("wine", "wineserver", "Xvfb", "xwininfo"):
        if shutil.which(program) is None:
            sys.exit(f"refused: {program} is not installed")
    home = tempfile.mkdtemp(prefix="wine-menu-")
    try:
        run(*(os.path.abspath(argument) for argument in sys.argv[1:5]), home)
    finally:
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    main()
