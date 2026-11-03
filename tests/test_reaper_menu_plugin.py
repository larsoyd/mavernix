import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time

REGISTRAR = "com.canonical.AppMenu.Registrar"
REGISTRAR_PATH = "/com/canonical/AppMenu/Registrar"
MENU = "com.canonical.dbusmenu"
CLOSED = "[audio device closed]"
RUNNING = "[44.1kHz 24bit WAV : 0/2ch 512spls ~0.0/0.0ms Dummy Audio]"
REGISTRAR_XML = f"""<node><interface name="{REGISTRAR}">
<method name="RegisterWindow"><arg type="u" direction="in"/><arg type="o" direction="in"/></method>
<method name="UnregisterWindow"><arg type="u" direction="in"/></method>
</interface></node>"""


def session(host, swell, plugin):
    from gi.repository import Gio, GLib

    def wait(predicate, what, seconds=4):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            while GLib.MainContext.default().iteration(False):
                pass
            if predicate():
                return
            time.sleep(.01)
        raise AssertionError(f"timed out waiting for {what}")

    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    calls = []

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

    resources = tempfile.mkdtemp(prefix="reaper-resources-", dir=os.environ["HOME"])
    child = subprocess.Popen([host, swell, plugin, resources], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True)
    lines = queue.Queue()
    log = []
    def read(stream):
        for line in stream:
            lines.put(line.rstrip("\n"))
    for stream in (child.stdout, child.stderr):
        threading.Thread(target=read, args=(stream,), daemon=True).start()

    def drain():
        while not lines.empty():
            log.append(lines.get())

    def send(command):
        child.stdin.write(command + "\n")
        child.stdin.flush()
        wait(lambda: drain() or f"[host] ok {command}" in log, f"host to answer {command!r}")

    def seen(text):
        drain()
        return any(text in line for line in log)

    def layout(service, path):
        reply = bus.call_sync(service, path, MENU, "GetLayout", GLib.Variant("(iias)", (0, -1, [])),
                              None, Gio.DBusCallFlags.NONE, 2000, None)
        return reply.unpack()[1][2]

    def top(service, path):
        return [(item[0], item[1].get("label", ""), item[1].get("children-display", "")) for item in layout(service, path)]

    def click(service, path, item):
        bus.call_sync(service, path, MENU, "Event",
                      GLib.Variant("(isvu)", (item, "clicked", GLib.Variant("s", ""), 0)),
                      None, Gio.DBusCallFlags.NONE, 2000, None)

    def registered(xid):
        found = [call for call in calls if call[0] == "RegisterWindow" and call[2] == xid]
        return found[-1] if found else None

    def check(condition, message):
        if not condition:
            drain()
            print("\n".join(log), file=sys.stderr)
            raise AssertionError(message)

    try:
        own_registrar(True)
        wait(lambda: seen("[reaper-menu] registrar owner="), "the plugin to see the registrar")
        send(f"status {CLOSED}")
        send("tick 10")
        wait(lambda: registered(101), "the main window to register")
        _, service, _, path = registered(101)
        labels = top(service, path)
        check([label for _, label, _ in labels] == ["_File", "_Help", CLOSED],
              f"status is not the last title: {labels}")
        check(labels[-1][2] == "", f"the status opens a menu: {labels[-1]}")
        status_id = labels[-1][0]
        print("[reaper-plugin] PASS the status is the last title", flush=True)

        send(f"status {RUNNING}")
        send("tick 10")
        wait(lambda: top(service, path)[-1][1] == RUNNING, "the status text to follow")
        check(top(service, path)[-1][0] == status_id, "the status item was rebuilt instead of relabelled")
        print("[reaper-plugin] PASS the status follows REAPER's text in place", flush=True)

        click(service, path, status_id)
        wait(lambda: seen("[host] posted window=1 message=0x111 command=50121"), "the status command")
        print("[reaper-plugin] PASS a click on the status runs REAPER's own command", flush=True)

        send("disable 1")
        click(service, path, status_id)
        wait(lambda: seen("command refused id=50121 reason=window_disabled"), "a refusal while disabled")
        check(sum("command=50121" in line for line in log) == 1, "a disabled window got the status command")
        send("enable 1")
        print("[reaper-plugin] PASS a disabled window gets no status command", flush=True)

        send("undo    [Add new track]")
        send("status ")
        send("tick 10")
        wait(lambda: [label for _, label, _ in top(service, path)] == ["_File", "_Help"], "an empty status to go")
        send("status Recording")
        send("tick 10")
        check([label for _, label, _ in top(service, path)] == ["_File", "_Help"], "a plain word became the status")
        send(f"status {CLOSED}")
        send("tick 10")
        wait(lambda: top(service, path)[-1][1] == CLOSED, "the status to come back")
        print("[reaper-plugin] PASS an empty status leaves the bar, the last action never shows", flush=True)

        send("window 2 102 shared")
        send("window 3 103 menu")
        send("tick 10")
        wait(lambda: registered(102) and registered(103), "the secondary windows to register")
        check(registered(102)[3] == path, f"a window without a menu lost the main menu: {registered(102)}")
        check(registered(103)[3] != path, "a window with its own menu shares the main one")
        own_labels = [label for _, label, _ in top(service, registered(103)[3])]
        check(own_labels == ["_Edit"], f"a window's own menu carries the status: {own_labels}")
        print("[reaper-plugin] PASS other windows keep the main status or their own menu", flush=True)

        send("window 4 104 statusmenu")
        send("tick 10")
        wait(lambda: registered(104), "a window whose last menu looks like a status")
        own_top = top(service, registered(104)[3])
        check([(label, display) for _, label, display in own_top] ==
              [("_Edit", "submenu"), ("[recording settings]", "submenu")], f"a last menu was taken twice: {own_top}")
        send("hide 4")
        print("[reaper-plugin] PASS a last item with a menu stays a menu", flush=True)

        send("hide 2")
        send("tick 10")
        wait(lambda: ("UnregisterWindow", service, 102) in calls, "a hidden window to unregister")
        print("[reaper-plugin] PASS a hidden window is unregistered", flush=True)

        own_registrar(False)
        wait(lambda: seen("registrar vanished"), "the plugin to see the registrar go")
        before = len(calls)
        own_registrar(True)
        wait(lambda: drain() or sum("[reaper-menu] registrar owner=" in line for line in log) == 2,
             "the plugin to see the registrar again")
        send("tick 10")
        wait(lambda: any(call[0] == "RegisterWindow" and call[2] == 101 for call in calls[before:]),
             "the main window to register again")
        check(top(service, path)[-1][1] == CLOSED, "the status was lost over a registrar restart")
        print("[reaper-plugin] PASS a restarted registrar gets the windows and the status again", flush=True)

        before = len(calls)
        send("unload")
        wait(lambda: {call[2] for call in calls[before:] if call[0] == "UnregisterWindow"} >= {101, 103},
             "the shown windows to unregister on unload")
        check(seen("[host] register name=-timer"), "the timer outlived the plugin")
        send("tick 10")
        check(len([call for call in calls[before:] if call[0] == "RegisterWindow"]) == 0, "registered after unload")
        print("[reaper-plugin] PASS unloading lets go of every window and the timer", flush=True)

        send("quit")
        child.wait(timeout=5)
        check(child.returncode == 0, f"host exited {child.returncode}")
    except AssertionError:
        drain()
        print("registrar calls:", *calls, "host log:", *log, sep="\n", file=sys.stderr)
        raise
    finally:
        stop(child)
    refused(host, swell, plugin)


def stop(child):
    if child.poll() is None:
        subprocess.run(["ps", "-o", "pid,pgid,args", "-p", str(child.pid)], timeout=3)
        child.kill()
        child.wait(timeout=5)


def refused(host, swell, plugin):
    resources = Path(tempfile.mkdtemp(prefix="reaper-refused-", dir=os.environ["HOME"]))
    theme = resources / "libSwell-user.colortheme"
    theme.write_text("menubar_height 0\nmenu_font_size 14\n")
    child = subprocess.Popen([host, swell, plugin, str(resources)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, start_new_session=True,
                             env=dict(os.environ, FAKE_SWELL_MISSING="IsWindowEnabled"))
    try:
        output, _ = child.communicate("unload\nquit\n", timeout=5)
    finally:
        stop(child)
    if child.returncode != 0 or "[host] loaded result=0" not in output or "[host] ok unload" not in output:
        raise AssertionError(f"a refused plugin did not stay out exit={child.returncode}:\n{output}")
    if "swell has no IsWindowEnabled" not in output or theme.read_text() != "menu_font_size 14\n":
        raise AssertionError(f"a refused plugin left REAPER without its bar: {theme.read_text()!r}\n{output}")
    print("[reaper-plugin] PASS without SWELL the plugin stays out and REAPER gets its bar back", flush=True)



if __name__ == "__main__":
    if "--session" in sys.argv:
        sys.argv.remove("--session")
        session(*sys.argv[1:4])
    else:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        with tempfile.TemporaryDirectory(prefix="mavernix-reaper-") as folder:
            bus_config = Path(folder) / "bus.conf"
            bus_config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen>'
                                  '<policy context="default"><allow send_destination="*"/>'
                                  '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
            environment = dict(os.environ, HOME=folder, XDG_RUNTIME_DIR=folder, KDE_DEBUG="1")
            for name in ("DISPLAY", "WAYLAND_DISPLAY"):
                environment.pop(name, None)
            arguments = [str(Path(argument).resolve()) for argument in sys.argv[1:4]]
            result = subprocess.run(["timeout", "--kill-after=3", "40", "dbus-run-session",
                                     "--config-file=" + str(bus_config), "--", sys.executable, __file__,
                                     *arguments, "--session"], env=environment, timeout=50)
            sys.exit(result.returncode)
