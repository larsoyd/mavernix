import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class MenuEnvironment(unittest.TestCase):
    def test_module_is_shared_preserves_modules_and_is_idempotent(self):
        script = Path(__file__).resolve().parents[1] / "src/session/global-menu.sh"
        with tempfile.TemporaryDirectory() as folder:
            module = Path(folder) / "gtk-3.0/libmavernix-global-menu.so"
            module.parent.mkdir()
            module.touch()
            for existing, other, disabled, expected in (
                ("", "", "", str(module)),
                ("other", "", "", "other:" + str(module)),
                (str(module), "", "", str(module)),
                ("appmenu-gtk-module", "", "", "appmenu-gtk-module"),
                ("other", "unity-gtk-module", "", "other"),
                ("other", "", "0", "other"),
            ):
                with self.subTest(existing=existing, other=other, disabled=disabled):
                    env = dict(os.environ, GTK3_MODULES=existing, GTK_MODULES=other,
                               MAVERNIX_GLOBAL_MENU=disabled, library_dir=folder)
                    result = subprocess.run(["bash", "-c", 'source "$1"; source "$1"; printf %s "$GTK3_MODULES"',
                                             "test", str(script)], env=env, capture_output=True, text=True,
                                            check=True, timeout=5)
                    self.assertEqual(result.stdout, expected)


if __name__ == "__main__":
    unittest.main()
