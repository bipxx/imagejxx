"""Exercise the actual workflow's pip arguments without building or installing."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def install_command(platform):
    text = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf-8")
    section = text.split("      - name: Install " + platform + " IO and both desktop hosts\n", 1)[1]
    section = section.split("\n      - name:", 1)[0]
    if "run: >-" in section:
        return " ".join(section.split("run: >-", 1)[1].split())
    return next(line.strip().removeprefix("run: ") for line in section.splitlines()
                if "python -m pip install '.[io,test]'" in line)


class PlatformInstallArguments(unittest.TestCase):
    def test_both_isolated_builds_select_the_declared_cmake_series(self):
        for platform in ("Windows", "macOS"):
            with self.subTest(platform=platform):
                self.assertIn("cmake.version===3.31.6", install_command(platform))

    @unittest.skipUnless(sys.platform == "win32" and shutil.which("pwsh"),
                         "Requires actual Windows PowerShell native argument passing")
    def test_windows_actual_native_argv_preserves_every_pip_configuration(self):
        with tempfile.TemporaryDirectory(prefix="imagejxx-pip-argv-") as temporary:
            root = Path(temporary).resolve(strict=True)
            spy = root / "arguments.py"
            spy.write_text("import json,sys; print(json.dumps(sys.argv[1:]))\n", encoding="utf-8")
            # Run only the exact pip command through the actual shell, replacing
            # its executable entry with a stdlib argv observer. Never invoke pip.
            quote = lambda value: "'" + str(value).replace("'", "''") + "'"
            command = install_command("Windows").replace(
                "python -m pip install", "& " + quote(sys.executable) + " " + quote(spy), 1)
            script = "$env:IMAGEJXX_CI_DEPENDENCY_PROFILE = 'windows-x64-ci-dynamic-source-001'\n" + command
            result = subprocess.run([shutil.which("pwsh"), "-NoProfile", "-NonInteractive", "-Command", script],
                                    capture_output=True, text=True, encoding="utf-8", timeout=30, check=True)
            self.assertEqual(json.loads(result.stdout), [
                ".[io,test]", "-C", "cmake.version===3.31.6",
                "-C", "cmake.define.IMAGEJXX_BUILD_IO=ON",
                "-C", "cmake.define.IMAGEJXX_BUILD_CPP_UI=ON",
                "-C", "cmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON",
                "-C", "cmake.define.BUILD_TESTING=ON",
                "-C", "cmake.define.IMAGEJXX_CI_SOURCE_PROFILE=windows-x64-ci-dynamic-source-001"])


if __name__ == "__main__":
    unittest.main()
