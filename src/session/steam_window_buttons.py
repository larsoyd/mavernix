"""Gives Steam's own window buttons the desktop theme's look, at the left.

    python steam_window_buttons.py STYLE_FILE [PORT]

Steam draws its window buttons in its web pages. With remote debugging on, it
opens a port on 127.0.0.1 (8080), and this adds the style to each of its
windows as they open, and again when the style file changes. It waits for
Steam to start, starts over when Steam quits, and ends with the session.
"""

import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

FLAG_NAME = ".cef-enable-remote-debugging"
# a flag holding this is ours to take back, any other is the user's
OUR_MARK = "mavernix\n"
STEAM_PAGES = ("https://steamloopback.host/", "about:blank?createflags=")
STEAM_AGENT = "Valve Steam Client"
STYLE_ID = "mavernix-window-buttons"
RETRY_SECONDS = 2
REFUSED_SECONDS = 30

# keeps the style last in the page head, where Steam adds its own sheets
INSTALL = """((css) => {
  const place = () => {
    if (!document.head) return;
    let style = document.getElementById(%(id)s);
    if (!style) { style = document.createElement('style'); style.id = %(id)s; }
    if (style.textContent !== css) style.textContent = css;
    if (document.head.lastElementChild !== style) document.head.appendChild(style);
  };
  window.mavernixWindowButtons?.disconnect();
  place();
  if (document.head) {
    window.mavernixWindowButtons = new MutationObserver(place);
    window.mavernixWindowButtons.observe(document.head, {childList: true});
  }
  return document.querySelectorAll('.title-bar-actions.window-controls').length;
})(%(css)s)"""


class SessionOver(Exception):
    pass


def log(message):
    print(f"[steam-buttons] {message}", file=sys.stderr, flush=True)


def check_session():
    session = os.environ.get("DESKTOP_SESSION_PID")
    if not session:
        return
    try:
        os.kill(int(session), 0)
    except ProcessLookupError:
        raise SessionOver from None
    except PermissionError:
        pass


class Style:
    def __init__(self, path):
        self.path, self.stamp, self.script = path, None, None

    def changed(self):
        try:
            stamp = os.stat(self.path).st_mtime_ns
            text = open(self.path).read() if stamp != self.stamp else None
        except OSError:
            stamp, text = None, ""
        if stamp == self.stamp and self.script is not None:
            return False
        self.stamp = stamp
        self.script = INSTALL % {"id": json.dumps(STYLE_ID), "css": json.dumps(text)}
        return True


def steam_folders():
    home = Path.home()
    data = Path(os.environ.get("XDG_DATA_HOME") or home / ".local" / "share")
    candidates = (data / "Steam", home / ".steam" / "steam", home / ".var/app/com.valvesoftware.Steam/.local/share/Steam")
    return [folder for folder in candidates if folder.is_dir()]


def switch_debugging(wanted):
    """Steam opens its port at its next start while the flag is in its folder."""
    for folder in steam_folders():
        flag = folder / FLAG_NAME
        if wanted and not flag.exists():
            flag.write_text(OUR_MARK)
            log(f"result=debugging_on folder={json.dumps(str(folder))}")
        elif not wanted and flag.is_file() and flag.read_text() == OUR_MARK:
            flag.unlink()
            log(f"result=debugging_off folder={json.dumps(str(folder))}")


def steam_address(port):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version", timeout=2) as reply:
        info = json.load(reply)
    if STEAM_AGENT not in info.get("User-Agent", ""):
        raise PermissionError(f"port {port} belongs to {info.get('Browser', 'another program')}, not Steam")
    return info["webSocketDebuggerUrl"]


class Connection:
    """A WebSocket client, just enough for the debugging protocol."""

    def __init__(self, url):
        address = urllib.parse.urlparse(url)
        self.socket = socket.create_connection((address.hostname, address.port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f"GET {address.path} HTTP/1.1\r\nHost: {address.hostname}:{address.port}\r\n"
                             "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                             f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        self.buffer = b""
        while b"\r\n\r\n" not in self.buffer:
            self.buffer += self.receive_some()
        head, _, self.buffer = self.buffer.partition(b"\r\n\r\n")
        status = head.split(b"\r\n")[0]
        if b" 101 " not in status:
            raise ConnectionError(status.decode(errors="replace"))
        self.socket.settimeout(1)
        self.last_id = 0

    def receive_some(self):
        chunk = self.socket.recv(65536)
        if not chunk:
            raise ConnectionError("Steam closed the connection")
        return chunk

    def take(self, count):
        while len(self.buffer) < count:
            self.buffer += self.receive_some()
        data, self.buffer = self.buffer[:count], self.buffer[count:]
        return data

    def send(self, method, session=None, **params):
        self.last_id += 1
        message = {"id": self.last_id, "method": method, "params": params}
        if session:
            message["sessionId"] = session
        data = json.dumps(message).encode()
        length = len(data)
        if length < 126:
            head = struct.pack("!BB", 0x81, 0x80 | length)
        elif length < 65536:
            head = struct.pack("!BBH", 0x81, 0x80 | 126, length)
        else:
            head = struct.pack("!BBQ", 0x81, 0x80 | 127, length)
        mask = os.urandom(4)
        self.socket.sendall(head + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(data)))
        return self.last_id

    def receive(self):
        """The next message, or None after a second without one."""
        message = b""
        while True:
            try:
                first, second = self.take(2)
            except TimeoutError:
                if message:
                    raise
                return None
            length = second & 0x7F
            if length == 126:
                length = struct.unpack("!H", self.take(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self.take(8))[0]
            payload = self.take(length)
            opcode = first & 0x0F
            if opcode == 8:
                raise ConnectionError("Steam closed the connection")
            if opcode in (0, 1):
                message += payload
                if first & 0x80:
                    return json.loads(message)


def serve(connection, style):
    """Styles Steam's windows until Steam or the session ends."""
    titles, addresses, sessions, attaching, styling = {}, {}, {}, {}, {}

    def put_style(target):
        command = connection.send("Runtime.evaluate", session=sessions[target], expression=style.script, returnByValue=True)
        styling[command] = target

    connection.send("Target.setDiscoverTargets", discover=True)
    while True:
        check_session()
        if style.changed():
            for target in sessions:
                put_style(target)
        message = connection.receive()
        if message is None:
            continue
        method, params = message.get("method"), message.get("params", {})
        if method in ("Target.targetCreated", "Target.targetInfoChanged"):
            info = params["targetInfo"]
            target = info["targetId"]
            if info["type"] != "page" or not info["url"].startswith(STEAM_PAGES):
                continue
            titles[target] = info["title"]
            moved = addresses.get(target) != info["url"]
            addresses[target] = info["url"]
            if target in sessions and moved:
                put_style(target)
            elif target not in sessions and target not in attaching.values():
                attaching[connection.send("Target.attachToTarget", targetId=target, flatten=True)] = target
        elif method == "Target.attachedToTarget":
            target = params["targetInfo"]["targetId"]
            sessions[target] = params["sessionId"]
            put_style(target)
        elif method in ("Target.targetDestroyed", "Target.detachedFromTarget"):
            for table in (titles, addresses, sessions):
                table.pop(params.get("targetId"), None)
        elif message.get("id") in styling:
            window = json.dumps(titles.get(styling.pop(message["id"]), "?"))
            result = message.get("result", {})
            if "error" in message or "exceptionDetails" in result:
                reason = message.get("error", {}).get("message") or result["exceptionDetails"].get("text")
                log(f"result=failed window={window} reason={json.dumps(reason)}")
            else:
                log(f"result=styled window={window} button_rows={result['result'].get('value')}")
        elif message.get("id") in attaching:
            target = attaching.pop(message["id"])
            if "error" in message:
                log(f"result=attach_failed window={json.dumps(titles.get(target, '?'))} "
                    f"reason={json.dumps(message['error'].get('message'))}")


def wait(seconds):
    for _ in range(seconds * 4):
        check_session()
        time.sleep(0.25)


def run(style, port):
    waiting = False
    while True:
        check_session()
        try:
            connection = Connection(steam_address(port))
        except PermissionError as refused:
            log(f"result=refused reason={json.dumps(str(refused))}")
            wait(REFUSED_SECONDS)
            continue
        except (OSError, ValueError, KeyError):
            if not waiting:
                log(f"result=waiting port={port}")
                waiting = True
            wait(RETRY_SECONDS)
            continue
        waiting = False
        log(f"result=connected port={port}")
        try:
            serve(connection, style)
        except (OSError, ValueError) as lost:
            log(f"result=disconnected reason={json.dumps(str(lost))}")
        finally:
            connection.socket.close()


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    port = int(sys.argv[2]) if len(sys.argv) == 3 else 8080
    try:
        run(Style(sys.argv[1]), port)
    except SessionOver:
        log("result=session_over")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
