"""Qt 6.12 can drop a live WeakMap from its sweep list and keep freed keys in it."""

import re
import unittest
from pathlib import Path

SHELL = Path(__file__).resolve().parent.parent / "src" / "shell"


class ShellWeakMaps(unittest.TestCase):
    def test_shell_uses_no_weak_collections(self):
        found = []
        for source in sorted(SHELL.rglob("*")):
            if source.suffix not in (".qml", ".js"):
                continue
            for number, line in enumerate(source.read_text().splitlines(), 1):
                if re.search(r"\bWeak(Map|Set)\b", line):
                    found.append(f"{source.relative_to(SHELL)}:{number}: {line.strip()}")
        self.assertEqual(found, [])


if __name__ == "__main__":
    unittest.main()
