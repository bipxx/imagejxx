"""Exercise the actual workflow's pip arguments without building or installing."""
import json
import os
from pathlib import Path
import shlex
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
        command = " ".join(section.split("run: >-", 1)[1].split())
    else:
        command = next(line.strip().removeprefix("run: ") for line in section.splitlines()
                       if "python -m pip install '.[io,test]'" in line)
    # These controls exercise the actual unchanged pip argv after its owner
    # wrapper. Wrapper execution/source authentication has separate controls.
    return command.partition(" -- ")[2] if " -- " in command else command


def install_settings(platform):
    """Decode only pip's config-setting tokens; the Windows argv test uses pwsh."""
    arguments = iter(shlex.split(install_command(platform)))
    settings = {}
    for argument in arguments:
        if argument == "-C":
            value = next(arguments)
        elif argument.startswith("-C"):
            value = argument[2:]
        else:
            continue
        key, value = value.split("=", 1)
        if key in settings:
            raise ValueError("Repeated workflow config setting: " + key)
        settings[key] = value
    return settings


class PlatformInstallArguments(unittest.TestCase):
    def test_authenticated_history_is_required_without_replacing_complete_readers(self):
        root_workflow = (ROOT / ".github/workflows/test.yml").read_text(encoding="utf-8")
        source_workflow = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf-8")
        wrapper = "python -I -B scripts/ci_history_build.py --output build/ci-history-build -- python -m pip install"
        self.assertIn(wrapper, root_workflow)
        self.assertEqual(source_workflow.count(wrapper), 1)
        for workflow in (root_workflow, source_workflow):
            self.assertIn("--history-suite --output build/ci-history --history-build-receipt build/ci-history-build/receipt.json", workflow)
            self.assertIn("--output build/ci-python --history-output build/ci-history", workflow)
            self.assertIn("build/ci-history/capture.zip", workflow)
            self.assertLess(workflow.index("--history-suite"), workflow.index("--output build/ci-python"))
        mac_section = source_workflow.split("      - name: Install macOS IO and both desktop hosts\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertNotIn("ci_history_build.py", mac_section)
        self.assertIn("python -m pip install", mac_section)

    def test_both_isolated_builds_select_the_declared_cmake_series(self):
        for platform in ("Windows", "macOS"):
            with self.subTest(platform=platform):
                self.assertIn("cmake.version===3.31.6", install_command(platform))

    def test_windows_explicitly_selects_single_config_release(self):
        settings = install_settings("Windows")
        self.assertEqual(settings["cmake.args"], "-GNinja")
        self.assertEqual(settings["cmake.build-type"], "Release")
        self.assertEqual(settings["ninja.version"], "==1.13.2")

    @unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"),
                         "Requires existing CMake and Ninja for metadata-only configure")
    def test_windows_workflow_settings_configure_single_config_release(self):
        settings = install_settings("Windows")
        with tempfile.TemporaryDirectory(prefix="imagejxx-generator-metadata-") as temporary:
            root = Path(temporary).resolve(strict=True)
            source, build = root / "source", root / "configured"
            source.mkdir()
            # No languages, compiler checks, dependencies, build or install.
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.18)\n"
                "project(workflow_generator_metadata NONE)\n"
                "get_property(is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)\n"
                "if(is_multi)\n  set(kind multi)\nelse()\n  set(kind single)\nendif()\n"
                'file(WRITE "${CMAKE_BINARY_DIR}/observed.txt" '
                '"${CMAKE_GENERATOR}\\n${CMAKE_BUILD_TYPE}\\n${CMAKE_CONFIGURATION_TYPES}\\n${kind}\\n")\n',
                encoding="utf-8",
            )
            environment = os.environ.copy()
            for name in ("CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM",
                         "CMAKE_GENERATOR_TOOLSET", "CMAKE_GENERATOR_INSTANCE"):
                environment.pop(name, None)
            # scikit-build-core's single-config path passes build-type as this
            # cache definition; exercise the actual selected CMake generator.
            result = subprocess.run([
                shutil.which("cmake"), "-S", str(source), "-B", str(build),
                *shlex.split(settings["cmake.args"]),
                "-DCMAKE_BUILD_TYPE:STRING=" + settings["cmake.build-type"],
                "-DCMAKE_MAKE_PROGRAM:FILEPATH=" + shutil.which("ninja"),
            ], env=environment, capture_output=True, text=True, encoding="utf-8",
                timeout=30, check=True)
            self.assertEqual((build / "observed.txt").read_text(encoding="utf-8").splitlines(),
                             ["Ninja", "Release", "", "single"], result.stdout + result.stderr)

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
                "-C", "cmake.args=-GNinja", "-C", "cmake.build-type=Release",
                "-C", "ninja.version===1.13.2",
                "-C", "cmake.define.IMAGEJXX_BUILD_IO=ON",
                "-C", "cmake.define.IMAGEJXX_BUILD_CPP_UI=ON",
                "-C", "cmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON",
                "-C", "cmake.define.BUILD_TESTING=ON",
                "-C", "cmake.define.IMAGEJXX_CI_SOURCE_PROFILE=windows-x64-ci-dynamic-source-001"])


if __name__ == "__main__":
    unittest.main()
