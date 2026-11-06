import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time

PORTAL = "org.freedesktop.portal.Desktop"
PORTAL_PATH = "/org/freedesktop/portal/desktop"
CHOOSER = "org.freedesktop.portal.FileChooser"
CHOOSER_XML = f"""<node><interface name="{CHOOSER}">
<method name="OpenFile"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="a{{sv}}" direction="in"/>
<arg type="o" direction="out"/></method>
<method name="SaveFile"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="a{{sv}}" direction="in"/>
<arg type="o" direction="out"/></method>
</interface></node>"""
MEDIA = "WAV files|*.WAV;*.AIF;|All files|*.*"
PROJECTS = "REAPER Project files (*.RPP)|*.RPP|EDL TXT (Vegas) files (*.TXT)|*.TXT|All files|*.*"


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
    requests = []
    answers = []

    def respond(path, sender, code, results):
        bus.emit_signal(sender, path, "org.freedesktop.portal.Request", "Response",
                        GLib.Variant("(ua{sv})", (code, results)))
        return GLib.SOURCE_REMOVE

    def chooser_call(connection, sender, path, interface, method, arguments, invocation):
        parent, title, options = arguments.unpack()
        requests.append({"method": method, "parent": parent, "title": title, "options": options})
        token = options.get("handle_token", "t")
        request = f"/org/freedesktop/portal/desktop/request/{sender[1:].replace('.', '_')}/{token}"
        answer = answers.pop(0) if answers else ("cancel",)
        if answer[0] == "error":
            invocation.return_dbus_error("org.freedesktop.portal.Error.Failed", "scripted failure")
            return
        if answer[0] == "moved":
            request = "/org/freedesktop/portal/desktop/request/elsewhere/moved"
        invocation.return_value(GLib.Variant("(o)", (request,)))
        if answer[0] == "stray":
            GLib.idle_add(respond, request + "x", sender, 0, answer[2])
            GLib.idle_add(respond, request, sender, 0, answer[1])
        elif answer[0] == "answer-then-vanish":
            GLib.idle_add(lambda: respond(request, sender, 0, answer[1]) or own_portal(False) and GLib.SOURCE_REMOVE)
        elif answer[0] == "vanish":
            GLib.idle_add(lambda: own_portal(False) and GLib.SOURCE_REMOVE)
        elif answer[0] == "cancel":
            GLib.idle_add(respond, request, sender, 1, {})
        elif answer[0] == "fail":
            GLib.idle_add(respond, request, sender, 2, {})
        else:
            GLib.idle_add(respond, request, sender, 0, answer[1])

    node = Gio.DBusNodeInfo.new_for_xml(CHOOSER_XML)
    bus.register_object(PORTAL_PATH, node.interfaces[0], chooser_call, None, None)

    def own_portal(owned):
        method, argument = ("RequestName", "(su)") if owned else ("ReleaseName", "(s)")
        values = (PORTAL, 0) if owned else (PORTAL,)
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method,
                      GLib.Variant(argument, values), None, Gio.DBusCallFlags.NONE, 2000, None)
        return True

    resources = tempfile.mkdtemp(prefix="reaper-resources-", dir=os.environ["HOME"])
    files = Path(tempfile.mkdtemp(prefix="reaper-files-", dir=os.environ["HOME"]))
    child = subprocess.Popen([host, swell, plugin, resources], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, start_new_session=True)
    incoming = queue.Queue()
    log = []

    def read(stream):
        for line in stream:
            incoming.put(line.rstrip("\n"))
    for stream in (child.stdout, child.stderr):
        threading.Thread(target=read, args=(stream,), daemon=True).start()

    def drain():
        while not incoming.empty():
            log.append(incoming.get())

    def send(command, seconds=4):
        mark = len(log)
        child.stdin.write(command + "\n")
        child.stdin.flush()
        wait(lambda: drain() or f"[host] ok {command}" in log[mark:], f"host to answer {command!r}", seconds)
        return log[mark:]

    def seen(text, since=0):
        drain()
        return any(text in line for line in log[since:])

    def result(lines):
        found = [line for line in lines if line.startswith("[host] result ")]
        check(len(found) == 1, f"expected one result in {lines}")
        return found[0][len("[host] result "):]

    def check(condition, message):
        if not condition:
            drain()
            raise AssertionError(message)

    def uri(path):
        return GLib.filename_to_uri(str(path), None)

    def filters(*pairs):
        return [(name, [(0, glob) for glob in globs]) for name, globs in pairs]

    try:
        own_portal(True)
        wait(lambda: seen("[reaper-files] loaded"), "the plugin to load")

        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "a b.wav")])}))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == f"kind=open value={files}/a b.wav|", f"single open: {lines}")
        asked = requests[-1]
        check(asked["method"] == "OpenFile" and asked["title"] == "Import file", f"wrong call: {asked}")
        check(asked["parent"] == "x11:65", f"parent is not the foreground window: {asked['parent']}")
        options = asked["options"]
        check(bytes(options["current_folder"]) == str(files).encode() + b"\0", f"folder: {options}")
        check(options["multiple"] is False and "directory" not in options, f"open flags: {options}")
        check(options["filters"] == filters(("WAV files", ["*.WAV", "*.AIF"]), ("All files", ["*"])),
              f"filters: {options['filters']}")
        check(options["current_filter"] == filters(("WAV files", ["*.WAV", "*.AIF"]))[0], f"current filter: {options}")
        check(not any("swell browse" in line for line in lines), f"swell's own dialog ran: {lines}")
        enables = [line for line in lines if line.startswith("[host] enable window=1")]
        check(enables == ["[host] enable window=1 on=0", "[host] enable window=1 on=1"], f"owner not modal: {enables}")
        print("[reaper-files] PASS an open dialog goes to the portal and returns swell's single form", flush=True)

        for window, parent in (("0", "x11:65"), ("77", "")):
            send(f"foreground {window}")
            answers.append(("cancel",))
            send(f"open 0\tImport file\t{files}\t{MEDIA}")
            check(requests[-1]["parent"] == parent, f"foreground {window} gave parent {requests[-1]['parent']!r}")
        send("foreground 1")
        send("disable 1")
        answers.append(("cancel",))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(not any(line.startswith("[host] enable") for line in lines), f"a disabled owner was enabled: {lines}")
        send("enable 1")
        print("[reaper-files] PASS the parent falls back to the main window and a disabled owner stays disabled", flush=True)

        answers.append(("cancel",))
        send(f"open 0\tImport file\t{files}\tLonely")
        check("filters" not in requests[-1]["options"], f"a name without globs became a filter: {requests[-1]}")
        answers.append(("ok", {"uris": GLib.Variant("as", [])}))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == "kind=open value=/swell/open|", f"an answer without files: {lines}")
        print("[reaper-files] PASS a filter without globs is left out, an answer without files falls back", flush=True)

        stray = {"uris": GLib.Variant("as", [uri(files / "stray.wav")])}
        answers.append(("stray", {"uris": GLib.Variant("as", [uri(files / "mine.wav")])}, stray))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == f"kind=open value={files}/mine.wav|", f"another request's answer was taken: {lines}")
        answers.append(("moved", {"uris": GLib.Variant("as", [uri(files / "moved.wav")])}))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == f"kind=open value={files}/moved.wav|", f"a request at another path: {lines}")
        answers.append(("answer-then-vanish", {"uris": GLib.Variant("as", [uri(files / "last.wav")])}))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == f"kind=open value={files}/last.wav|", f"an answer lost to the portal leaving: {lines}")
        own_portal(True)
        print("[reaper-files] PASS only this request's answer counts, wherever the portal put it", flush=True)

        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "a.wav"), uri(files / "b.wav")])}))
        lines = send(f"open 1\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == f"kind=open value={files}/|a.wav|b.wav|", f"multiple open: {lines}")
        check(requests[-1]["options"]["multiple"] is True, "multiple was not asked for")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "one.wav")])}))
        lines = send(f"open 1\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == f"kind=open value={files}/one.wav|", f"one file from a multiple open: {lines}")
        print("[reaper-files] PASS several files come back as a folder and names", flush=True)

        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files)])}))
        lines = send(f"folder Render path:\t")
        check(result(lines) == f"kind=folder ok=1 value={files}", f"folder: {lines}")
        options = requests[-1]["options"]
        check(options.get("directory") is True and "current_folder" not in options, f"folder options: {options}")
        print("[reaper-files] PASS a folder choice asks for a directory and leaves an empty start out", flush=True)

        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "song")]),
                               "current_filter": GLib.Variant("(sa(us))", ("EDL TXT (Vegas) files", [(0, "*.TXT")]))}))
        lines = send(f"save Save Project\t{files}\t.rpp\t{PROJECTS}")
        check(result(lines) == f"kind=save ok=1 value={files}/song.TXT", f"extension from the chosen filter: {lines}")
        asked = requests[-1]
        check(asked["method"] == "SaveFile" and "current_name" not in asked["options"], f"save options: {asked}")
        check(asked["options"]["current_filter"][0] == "REAPER Project files (*.RPP)", f"filter for .rpp: {asked}")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "song")])}))
        lines = send(f"save Save Project\t{files}\t{files}/mix.rpp\t{PROJECTS}")
        check(result(lines) == f"kind=save ok=1 value={files}/song.RPP", f"extension from the first filter: {lines}")
        check(requests[-1]["options"]["current_name"] == "mix.rpp", f"name: {requests[-1]['options']}")
        (files / "take.rpp").write_text("")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "take.rpp")])}))
        lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
        check(result(lines) == f"kind=save ok=1 value={files}/take.rpp", f"a fitting extension was changed: {lines}")
        check(not seen("[host] messagebox"), "asked about a file nobody named twice")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "noext")])}))
        lines = send(f"save Export\t{files}\t\tAll files|*.*")
        check(result(lines) == f"kind=save ok=1 value={files}/noext", f"a wildcard filter added an extension: {lines}")
        (files / "take.v2").mkdir()
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "take.v2" / "song")])}))
        lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
        check(result(lines) == f"kind=save ok=1 value={files}/take.v2/song.RPP", f"a dot in the folder: {lines}")
        print("[reaper-files] PASS a save gets swell's default extension only when it has none of the list's", flush=True)

        (files / "old.RPP").write_text("")
        for answer, ok in ((2, 0), (1, 1)):
            send(f"answer {answer}")
            answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "old")])}))
            lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
            check(any("caption=Overwrite file?" in line and "old.RPP" in line for line in lines), f"no question: {lines}")
            check(result(lines).startswith(f"kind=save ok={ok}"), f"overwrite answer {answer} ignored: {lines}")
        print("[reaper-files] PASS an added extension that names an existing file asks before overwriting", flush=True)

        answers.append(("cancel",))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == "kind=open value=null", f"cancel: {lines}")
        check(not any("swell browse" in line for line in lines), f"a cancel opened swell's dialog: {lines}")
        answers.append(("fail",))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == "kind=open value=/swell/open|", f"portal failure kept no dialog: {lines}")
        answers.append(("error",))
        lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
        check(result(lines) == "kind=save ok=1 value=/swell/save", f"a refused call kept no dialog: {lines}")
        print("[reaper-files] PASS a cancel stays cancelled, a failing portal falls back to swell", flush=True)

        answers.append(("vanish",))
        lines = send(f"folder Render path:\t{files}")
        check(result(lines) == "kind=folder ok=1 value=/swell/folder", f"portal gone mid-request: {lines}")
        check(lines.count("[host] enable window=1 on=1") == 1, f"owner left disabled: {lines}")
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == "kind=open value=/swell/open|", f"no portal at all: {lines}")
        own_portal(True)
        print("[reaper-files] PASS a portal that goes away or is missing falls back to swell", flush=True)

        send("template 1")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "x.rpp")]),
                               "choices": GLib.Variant("a(ss)", [("1042", "true"), ("1043", "false"), ("1050", "0")])}))
        lines = send(f"open 0\tOpen project\t{files}\t{PROJECTS}")
        check(result(lines) == f"kind=open value={files}/x.rpp|", f"template open: {lines}")
        choices = requests[-1]["options"].get("choices")
        check(choices == [("1042", "Open in new tab", [], "false"), ("1043", "Open read-only", [], "true"),
                          ("1050", "Format:", [("0", "WAV"), ("1", "FLAC")], "1")], f"choices: {choices}")
        check(any(line == "[host] template destroy state=1042:1 1043:0 1044:0 1045:0 1050:#0 " for line in lines),
              f"choices did not reach the template: {lines}")
        commands = [line for line in lines if line.startswith("[host] template command")]
        check(commands == ["[host] template command id=1042 code=0", "[host] template command id=1043 code=0",
                           "[host] template command id=1050 code=1"], f"template not told of changes: {commands}")
        check(not seen("[host] swell template id=1"), "a mapped template still went to swell")
        check(sum(line.startswith("[host] template destroy") for line in lines) == 1, f"destroyed twice: {lines}")
        send("template 1")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "x.rpp")]),
                               "choices": GLib.Variant("a(ss)", [("1042", "false"), ("1043", "true"), ("1050", "7")])}))
        lines = send(f"open 0\tOpen project\t{files}\t{PROJECTS}")
        check(not any(line.startswith("[host] template command") for line in lines), f"unchanged choices sent: {lines}")
        check(any(line == "[host] template destroy state=1042:0 1043:1 1044:0 1045:0 1050:#1 " for line in lines),
              f"an unknown list entry was picked: {lines}")
        print("[reaper-files] PASS template ticks and lists go to the portal and their answers come back", flush=True)

        send("template 1")
        answers.append(("cancel",))
        lines = send(f"open 0\tOpen project\t{files}\t{PROJECTS}")
        check(any(line == "[host] template destroy state=1042:0 1043:1 1044:0 1045:0 1050:#1 " for line in lines),
              f"a cancel changed the template: {lines}")
        check(not any(line.startswith("[host] template command") for line in lines), f"cancel sent commands: {lines}")
        check(sum(line.startswith("[host] template destroy") for line in lines) == 1, f"cancel destroyed twice: {lines}")
        answers.append(("cancel",))
        send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check("choices" not in requests[-1]["options"], "a used template came back for the next dialog")
        send("template 1")
        send("template 0")
        answers.append(("cancel",))
        send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check("choices" not in requests[-1]["options"], "a cleared template still asked choices")
        print("[reaper-files] PASS a cancelled or cleared template keeps its values", flush=True)

        before = len(requests)
        send("template 2")
        lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
        check(len(requests) == before, "a template with a button went to the portal")
        handed = [line for line in lines if line.startswith("[host] swell")]
        check(handed == ["[host] swell template id=2", "[host] swell browse kind=save caption=Save Project"],
              f"swell did not get the template: {lines}")
        check(not any("dialog created" in line and "template=2" in line for line in lines[lines.index(handed[0]):]),
              "the plugin kept its own copy of a template it handed on")
        for kind in (3, 4, 9):
            send(f"template {kind}")
            lines = send(f"open 0\tOpen project\t{files}\t{PROJECTS}")
            check(len(requests) == before and f"[host] swell template id={kind}" in lines,
                  f"template {kind} went to the portal: {lines}")
        print("[reaper-files] PASS a template with controls the portal lacks keeps swell's dialog", flush=True)

        send(f"nest open 0\tNested\t{files}\t{MEDIA}")
        answers.append(("ok", {"uris": GLib.Variant("as", [uri(files / "a.wav")])}))
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        nested = [line for line in lines if "caption=Nested" in line]
        check(nested == ["[host] swell browse kind=open caption=Nested"], f"a nested call reached the portal: {lines}")
        check(requests[-1]["title"] == "Import file", "the nested call replaced the open request")
        print("[reaper-files] PASS a dialog asked for while one is open goes to swell", flush=True)

        send("unload")
        before = len(requests)
        lines = send(f"open 0\tImport file\t{files}\t{MEDIA}")
        check(result(lines) == "kind=open value=/swell/open|" and len(requests) == before, f"after unload: {lines}")
        lines = send("template 1")
        check(any(line == "[host] swell template id=1" for line in lines), f"unload kept the template hook: {lines}")
        print("[reaper-files] PASS unloading hands every function back to swell", flush=True)

        send("quit")
        child.wait(timeout=5)
        check(child.returncode == 0, f"host exited {child.returncode}")
    except AssertionError:
        drain()
        print("portal requests:", *requests, "host log:", *log, sep="\n", file=sys.stderr)
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
    for variable, reason in (("FAKE_SWELL_MISSING", "swell has no BrowseForDirectory"),
                             ("FAKE_SWELL_LOCKED", "could not redirect BrowseForDirectory error=Permission denied")):
        resources = tempfile.mkdtemp(prefix="reaper-refused-", dir=os.environ["HOME"])
        child = subprocess.Popen([host, swell, plugin, resources], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, start_new_session=True,
                                 env=dict(os.environ, **{variable: "BrowseForDirectory"}))
        try:
            output, _ = child.communicate(f"open 0\tImport file\t/tmp\t{MEDIA}\nunload\nquit\n", timeout=5)
        finally:
            stop(child)
        if child.returncode != 0 or "[host] loaded result=0" not in output:
            raise AssertionError(f"a refused plugin did not stay out exit={child.returncode}:\n{output}")
        if f"[reaper-files] refused: {reason}" not in output:
            raise AssertionError(f"the refusal does not say why:\n{output}")
        if "[host] result kind=open value=/swell/open|" not in output:
            raise AssertionError(f"a refused plugin still moved a dialog:\n{output}")
    print("[reaper-files] PASS without every swell function, or one it cannot move, no dialog moves", flush=True)


if __name__ == "__main__":
    if "--session" in sys.argv:
        sys.argv.remove("--session")
        session(*sys.argv[1:4])
    else:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        with tempfile.TemporaryDirectory(prefix="mavernix-reaper-files-") as folder:
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
