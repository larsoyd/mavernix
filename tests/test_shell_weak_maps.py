"""Qt 6.12 can free a string stored in a WeakMap during incremental marking."""

import re
import unittest
from pathlib import Path

SHELL = Path(__file__).resolve().parent.parent / "src" / "shell"
HOLDS_ONLY_NUMBERS = {"windowNumbers"}


class ShellWeakMaps(unittest.TestCase):
    def test_weak_maps_hold_only_numbers(self):
        found = set()
        for source in SHELL.rglob("*.qml"):
            found |= set(re.findall(r"property var (\w+): new WeakMap\(\)", source.read_text()))
        self.assertEqual(found - HOLDS_ONLY_NUMBERS, set())


if __name__ == "__main__":
    unittest.main()
