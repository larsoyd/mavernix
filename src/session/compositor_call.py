#!/usr/bin/env python3
"""Asks the running compositor to do one thing, through its control socket.

    compositor_call.py METHOD

for example `scale/toggle`; used by touchpad gestures. Wayfire's ipc plugin
listens on WAYFIRE_SOCKET; a message is a 4-byte little-endian length and
that many bytes of JSON.
"""

import json
import os
import socket
import struct
import sys

LENGTH_FORMAT = "<I"
# a compositor stuck in a frame must not hold up the login
ANSWER_SECONDS = 2


def fail(message):
    sys.exit(f"compositor_call: {message}")


def call(method, data):
    """The compositor's answer to method with data, a dict. Raises OSError
    when it cannot be reached in time and ValueError on a garbled answer."""
    path = os.environ.get("WAYFIRE_SOCKET")
    if not path:
        raise OSError("WAYFIRE_SOCKET is not set; this is meant to be run inside the desktop session")
    body = json.dumps({"method": method, "data": data}).encode()
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(ANSWER_SECONDS)
        connection.connect(path)
        connection.sendall(struct.pack(LENGTH_FORMAT, len(body)) + body)
        # read the whole answer so the compositor never writes to a closed socket
        try:
            (length,) = struct.unpack(LENGTH_FORMAT, connection.recv(4, socket.MSG_WAITALL))
        except struct.error as error:
            raise ValueError(f"the compositor's answer to {method} was cut short") from error
        return json.loads(connection.recv(length, socket.MSG_WAITALL))


def main():
    if len(sys.argv) != 2:
        fail("give one method, for example scale/toggle")
    try:
        answer = call(sys.argv[1], {})
    except (OSError, ValueError) as error:
        fail(str(error))
    if "error" in answer:
        fail(f'the compositor answered "{answer["error"]}" to {sys.argv[1]}')


if __name__ == "__main__":
    main()
