import json
import os
import shutil
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
PANEL = "mavernix-reaper-save-panel"
# stands in for the save panel: follows the steps in FAKE_PANEL_SCRIPT and logs every line it is sent
FAKE_PANEL = """#!{python}
import json, os, sys, time
def say(text):
    print("[panel] " + text, file=sys.stderr, flush=True)
def until(word):
    for line in sys.stdin:
        say("got " + line.rstrip("\\n"))
        if line == "show\\n":
            print("shown", flush=True)
        if line.rstrip("\\n").split("\\t")[0] == word:
            return
    say("eof")
    sys.exit(0)
say(f"pid={{os.getpid()}}")
for step in json.load(open(os.environ["FAKE_PANEL_SCRIPT"])):
    kind, value = next(iter(step.items()))
    if kind == "until":
        until(value)
    elif kind == "send":
        print(value, flush=True)
    elif kind == "exit":
        sys.exit(value)
    elif kind == "sleep":
        time.sleep(value)
    elif kind == "close":
        os.close(0)
        os.close(1)
    elif kind == "signal":
        os.kill(os.getpid(), value)
until(None)
"""


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
    installed = Path(tempfile.mkdtemp(prefix="reaper-plugin-", dir=os.environ["HOME"]))
    plugin = shutil.copy(plugin, installed)
    panel = installed / PANEL
    panel.write_text(FAKE_PANEL.format(python=sys.executable))
    panel.chmod(0o755)
    os.environ["FAKE_PANEL_SCRIPT"] = str(installed / "script.json")
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

        def script(*steps):
            Path(os.environ["FAKE_PANEL_SCRIPT"]).write_text(json.dumps(steps))

        def told(since):
            return [line[len("[panel] got "):] for line in log[since:] if line.startswith("[panel] got ")]

        def panel_pid(since):
            wait(lambda: seen("[panel] pid=", since), "the panel to start")
            return int(next(line for line in log[since:] if line.startswith("[panel] pid="))[len("[panel] pid="):])

        def gone(pid, seconds=4):
            wait(lambda: not Path(f"/proc/{pid}").exists(), f"panel {pid} to be stopped and reaped", seconds)

        before = len(requests)
        script({"until": "show"}, {"send": f"accept\t{files}/song\t*.RPP"})
        send("template 5")
        mark = len(log)
        lines = send(f"save Save Project\t{files}\t{files}/mix.rpp\t{PROJECTS}")
        check(result(lines) == f"kind=save ok=1 value={files}/song.RPP", f"panel save: {lines}")
        gone(panel_pid(mark))
        wait(lambda: seen("[panel] eof", mark), "the panel to be told the dialog is over")
        check(told(mark) == [
            "title\tSave Project", f"folder\t{files}", "name\tmix.rpp",
            "filter\tREAPER Project files (*.RPP)\t*.RPP", "filter\tEDL TXT (Vegas) files (*.TXT)\t*.TXT",
            "filter\tAll files\t*", "current\t0",
            "control\t1042\tcheck\t8\t2\t209\t19\t1\t0\tCreate subdirectory for project",
            "control\t1043\tcheck\t8\t23\t277\t19\t1\t0\tCopy all media into project directory, using:",
            "control\t1057\tcheck\t291\t23\t112\t19\t0\t0\tConvert media",
            "control\t1056\tbutton\t403\t21\t68\t23\t0\t0\tFormat...",
            "control\t1058\tcheck\t479\t23\t142\t19\t0\t0\tTrim media, tail size:",
            "control\t1577\tedit\t625\t21\t66\t23\t0\t0\t500",
            "control\t1578\tlabel\t695\t25\t19\t15\t0\t0\tms",
            "control\t1044\tcheck\t8\t46\t245\t19\t1\t0\tMove all media into project directory",
            "control\t1045\tcheck\t260\t46\t431\t19\t0\t0\tCopy \\\\ rather\\tthan move",
            "show"], f"panel was told: {told(mark)}")
        check(len(requests) == before and not seen("swell browse", mark), f"the panel save went elsewhere: {lines}")
        enables = [line for line in lines if line.startswith("[host] enable window=1")]
        check(enables == ["[host] enable window=1 on=0", "[host] enable window=1 on=1"], f"owner not modal: {enables}")
        check(sum(line.startswith("[host] template destroy") for line in lines) == 1, f"destroyed twice: {lines}")
        check(not any(line.startswith("[host] template command") for line in lines), f"untouched options sent: {lines}")
        print("[reaper-files] PASS a template the portal cannot show goes to the save panel with every control", flush=True)

        script({"until": "show"}, {"send": "check\t1057\t1"}, {"until": "ready"}, {"send": "click\t1056"},
               {"until": "ready"}, {"send": "text\t1577\t9"}, {"until": "ready"},
               {"send": "check\t1043\t1"}, {"until": "ready"}, {"send": "check\t1057\t1"},
               {"until": "ready"}, {"send": "text\t1577\t2\\t5\\\\"}, {"until": "ready"}, {"send": "click\t1056"},
               {"send": "cancel"})
        send("template 5")
        mark = len(log)
        lines = send(f"save Save Project\t{files}\t.rpp\t{PROJECTS}")
        check(result(lines) == "kind=save ok=0 value=", f"panel cancel after changes: {lines}")
        gone(panel_pid(mark))
        check(not any(line.startswith("name\t") for line in told(mark)) and "current\t0" in told(mark),
              f"a bare extension is no name but picks its filter: {told(mark)}")
        commands = [line for line in lines if line.startswith("[host] template command")]
        check(commands == ["[host] template command id=1043 code=0", "[host] template command id=1057 code=0",
                           "[host] template command id=1577 code=768", "[host] template command id=1056 code=0"],
              f"changes did not reach the template: {commands}")
        after = told(mark)[told(mark).index("show") + 1:]
        check(after == ["state\t1057\t0\t0\tConvert media", "ready", "state\t1056\t0\t0\tFormat...", "ready",
                        "state\t1577\t0\t0\t500", "ready", "state\t1057\t1\t0\tConvert media", "ready",
                        "state\t1056\t1\t0\tFormat...", "state\t1058\t1\t0\tTrim media, tail size:",
                        "state\t1577\t1\t0\t500", "state\t1578\t1\t0\tms", "ready",
                        "ready", "busy", "ready"], f"panel was not kept in step: {after}")
        check("[host] template destroy state=1042:0 1043:1 1057:1 1056:0 1058:0 1577:=2\t5\\ 1044:0 1045:0 "
              "1046:0 " in lines, f"template state: {lines}")
        check(lines.index("[host] template modal end") < next(i for i, line in enumerate(lines)
                                                                 if line.startswith("[host] template destroy")),
              f"an answer was taken while reaper's own dialog was open: {lines}")
        print("[reaper-files] PASS panel changes reach reaper's procedure at once and what it wakes comes back", flush=True)

        def falls_back(reason, *steps, seconds=4, modal=True):
            script(*steps)
            send("template 5")
            mark = len(log)
            lines = send(f"save Save Project\t{files}\t\t{PROJECTS}", seconds)
            check(result(lines) == "kind=save ok=1 value=/swell/save", f"{reason}: no fallback: {lines}")
            check(seen(f"[reaper-files] fallback function=BrowseForSaveFile reason={reason}", mark),
                  f"{reason}: wrong reason: {log[mark:]}")
            handed = [line for line in lines if line.startswith("[host] swell")]
            check(handed == ["[host] swell template id=5", "[host] swell browse kind=save caption=Save Project"],
                  f"{reason}: swell did not get the template: {lines}")
            enables = [line for line in lines if line.startswith("[host] enable window=1")]
            check(enables == (["[host] enable window=1 on=0", "[host] enable window=1 on=1"] if modal else []),
                  f"{reason}: owner not restored: {enables}")
            return mark

        gone(panel_pid(falls_back("panel_exit=3", {"exit": 3})))
        gone(panel_pid(falls_back("panel_line=bogus", {"until": "show"}, {"send": "bogus"})))
        gone(panel_pid(falls_back("panel_line=check\\t9999\\t1", {"until": "show"}, {"send": "check\t9999\t1"})))
        gone(panel_pid(falls_back("panel_line=check\\t1578\\t1", {"until": "show"}, {"send": "check\t1578\t1"})))
        gone(panel_pid(falls_back("panel_exit=0", {"until": "show"}, {"send": "check\t1043\t1"}, {"until": "ready"},
                                  {"exit": 0})))
        gone(panel_pid(falls_back("no_files", {"until": "show"}, {"send": "accept\t\t*.RPP"})))
        gone(panel_pid(falls_back("panel_line=accept", {"send": f"accept\t{files}/early\t*.RPP"})))
        gone(panel_pid(falls_back("panel_line=check\\t1043\\t1x", {"until": "show"}, {"send": "check\t1043\t1x"})))
        gone(panel_pid(falls_back("panel_line=check\\t1043\\t2", {"until": "show"}, {"send": "check\t1043\t2"})))
        gone(panel_pid(falls_back("panel_signal=9", {"until": "show"}, {"signal": 9})))
        gone(panel_pid(falls_back("panel_write", {"until": "show"}, {"send": "check\t1043\t1"}, {"until": "ready"},
                                  {"send": "check\t1057\t1"}, {"until": "ready"}, {"send": "click\t1056"}, {"close": 1},
                                  {"sleep": 30})))
        gone(panel_pid(falls_back("panel_closed", {"until": "show"}, {"close": 1}, {"sleep": 30}, seconds=9)))
        gone(panel_pid(falls_back("panel_silent", {"sleep": 30}, seconds=14)))
        panel.rename(installed / "elsewhere")
        falls_back("panel_spawn=", modal=False)
        (installed / "elsewhere").rename(panel)
        print("[reaper-files] PASS a panel that fails, lies, stalls or is missing is stopped and swell's dialog shows", flush=True)

        script({"until": "show"}, {"send": f"accept\t{files}/first\t*.RPP\nbogus"}, {"sleep": 30})
        send("template 5")
        mark = len(log)
        lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
        check(result(lines) == f"kind=save ok=1 value={files}/first.RPP", f"a line after the answer counted: {lines}")
        pid = panel_pid(mark)
        time.sleep(3)
        check(Path(f"/proc/{pid}").exists(), "a panel was stopped before its time to leave")
        gone(pid, seconds=4)
        print("[reaper-files] PASS the first answer counts and a panel that stays after it is stopped", flush=True)

        for kind in (2, 4):
            script({"until": "show"}, {"send": "select\t1050\t0"}, {"until": "ready"}, {"send": "cancel"})
            send(f"template {kind}")
            mark = len(log)
            lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
            check(result(lines) == "kind=save ok=0 value=" and len(requests) == before, f"template {kind}: {lines}")
            gone(panel_pid(mark))
            check("[host] template command id=1050 code=1" in lines, f"template {kind}: list choice lost: {lines}")
            sent = told(mark)
            check(sent[sent.index("control\t1050\tlist\t0\t0\t0\t0\t1\t1\t") + 1:][:2] == ["item\t1050\tWAV", "item\t1050\tFLAC"],
                  f"template {kind}: list items: {sent}")
        save_line, open_line = f"save Save Project\t{files}\t\t{PROJECTS}", f"open 0\tOpen project\t{files}\t{PROJECTS}"
        for kind, line in ((3, save_line), (9, save_line), (2, open_line), (4, open_line)):
            send(f"template {kind}")
            mark = len(log)
            lines = send(line)
            check(len(requests) == before and f"[host] swell template id={kind}" in lines, f"template {kind}: {lines}")
            check(not seen("[panel] pid=", mark), f"template {kind} went to the panel: {log[mark:]}")
        print("[reaper-files] PASS a save with a button or text field goes to the panel, anything else keeps swell's dialog",
              flush=True)

        script({"until": "show"}, {"send": f"accept\t{files}/nest\t*.RPP"})
        send("template 5")
        send("nest template 5")
        send(f"nest save Nested\t{files}\t\t{PROJECTS}")
        lines = send(f"save Save Project\t{files}\t\t{PROJECTS}")
        check("[host] swell browse kind=save caption=Nested" in lines and lines.count("[host] swell template id=5") == 1,
              f"a nested call reached the panel: {lines}")
        check(f"[host] result kind=save ok=1 value={files}/nest.RPP" in lines, f"nested call broke the panel: {lines}")
        print("[reaper-files] PASS a dialog asked for while the panel is open goes to swell", flush=True)

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
    unwritable(host, swell, plugin)


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


def unwritable(host, swell, plugin):
    # gio ignores sigpipe for the whole process once a bus is used, so this host never touches one
    installed = Path(tempfile.mkdtemp(prefix="reaper-unwritable-", dir=os.environ["HOME"]))
    plugin = shutil.copy(plugin, installed)
    panel = installed / PANEL
    panel.write_text(FAKE_PANEL.format(python=sys.executable))
    panel.chmod(0o755)
    steps = [{"until": "show"}, {"send": "check\t1043\t1"}, {"until": "ready"}, {"send": "check\t1057\t1"},
             {"until": "ready"}, {"send": "click\t1056"}, {"close": 1}, {"sleep": 30}]
    (installed / "script.json").write_text(json.dumps(steps))
    child = subprocess.Popen([host, swell, plugin, str(installed)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, start_new_session=True,
                             env=dict(os.environ, FAKE_PANEL_SCRIPT=str(installed / "script.json")))
    try:
        output, _ = child.communicate(f"template 5\nsave Save Project\t/tmp\t\t{PROJECTS}\nquit\n", timeout=10)
    except subprocess.TimeoutExpired:
        raise AssertionError(f"host exit={child.poll()} and a panel left behind holds its output")
    finally:
        stop(child)
    if child.returncode != 0 or "reason=panel_write" not in output or "value=/swell/save" not in output:
        raise AssertionError(f"a panel that stopped reading took reaper down exit={child.returncode}:\n{output}")
    print("[reaper-files] PASS a panel that stops reading costs a fallback, not reaper", flush=True)


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
            result = subprocess.run(["timeout", "--kill-after=3", "60", "dbus-run-session",
                                     "--config-file=" + str(bus_config), "--", sys.executable, __file__,
                                     *arguments, "--session"], env=environment, timeout=70)
            sys.exit(result.returncode)
