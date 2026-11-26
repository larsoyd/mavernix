"""Quickshell drops the style from its own environment, so programs the shell opens must not take it from there."""

import re
import unittest
from pathlib import Path

NATIVE = Path(__file__).resolve().parent.parent / "src" / "shell" / "native"
LAUNCHER = "activated-start.cpp"


def matches(pattern):
    found = []
    for source in sorted(NATIVE.glob("*.[ch]pp")):
        for number, line in enumerate(source.read_text().splitlines(), 1):
            if re.search(pattern, line):
                found.append(f"{source.name}:{number}: {line.strip()}")
    return found


class ShellStartEnvironment(unittest.TestCase):
    def test_the_launcher_is_found(self):
        self.assertTrue((NATIVE / LAUNCHER).is_file(), f"{LAUNCHER} missing from {NATIVE}")

    def test_no_program_gets_the_shells_changed_environment(self):
        self.assertEqual(matches(r"\bsystemEnvironment\s*\(|char\s*\*\*\s*environ\b"), [])

    def test_only_the_launcher_opens_programs(self):
        found = [hit for hit in matches(r"\bstartDetached\b|\bposix_spawn|\bexec[lv]p?e?\s*\(|\bg_spawn")
                 if not hit.startswith(LAUNCHER + ":")]
        self.assertEqual(found, [])


if __name__ == "__main__":
    unittest.main()
