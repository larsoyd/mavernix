"""Keeps X11 toolkit settings in step with the desktop's generated settings."""

import signal
import subprocess
import sys
from pathlib import Path


def main():
    path = Path(sys.argv[1])
    changed = path.stat().st_mtime_ns
    daemon = subprocess.Popen(["xsettingsd", "--config", str(path)])

    def stop(signum, frame):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        while True:
            try:
                sys.exit(daemon.wait(timeout=1))
            except subprocess.TimeoutExpired:
                updated = path.stat().st_mtime_ns
                if updated != changed:
                    daemon.send_signal(signal.SIGHUP)
                    changed = updated
    finally:
        if daemon.poll() is None:
            daemon.terminate()
            try:
                daemon.wait(timeout=2)
            except subprocess.TimeoutExpired:
                daemon.kill()
                daemon.wait(timeout=2)


if __name__ == "__main__":
    main()
