"""With several windows Qt steps animations at the first screen's refresh rate."""

import unittest
from pathlib import Path

SHELL = Path(__file__).resolve().parent.parent / "src" / "shell"


class ShellFrameSteps(unittest.TestCase):
    def test_shell_asks_for_a_step_per_drawn_frame(self):
        lines = (SHELL / "shell.qml").read_text().splitlines()
        first_import = next(number for number, line in enumerate(lines) if line.startswith("import "))
        self.assertIn("//@ pragma Env QSG_USE_SIMPLE_ANIMATION_DRIVER=1", lines[:first_import])


if __name__ == "__main__":
    unittest.main()
