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
    def test_mac_ninja_experiment_is_explicit_and_keeps_full_install(self):
        text = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf-8")
        section = text.split("      - name: Check actual macOS Ninja console ownership\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("if: runner.os == 'macOS'", section)
        self.assertIn("IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE", section)
        self.assertIn("tests/tools/test_ci_mac_ninja_process.py --require-darwin", section)
        self.assertIn("--only-binary=:all: --no-deps --require-hashes -r .github/requirements/mac-ninja-controls.txt", text)
        self.assertIn("tests/tools/test_ci_mac_ninja_process.py --print-tool", section)
        requirement = (ROOT / ".github/requirements/mac-ninja-controls.txt").read_text(encoding="utf-8")
        self.assertIn("ninja==1.13.2 --hash=sha256:fd82e26c0706ad4ab88e5fdd26f3fab0a987a90f810160f6c322e752c6af298b", requirement)
        self.assertIn("if: always() && runner.os == 'macOS'", text)
        install = text.split("      - name: Select Ninja for the bounded macOS console experiment\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("--force-reinstall", install)
        self.assertIn("--report build/ci-ninja-console/install.json", install)
        self.assertIn("build/ci-ninja-console/control.log", text)
        self.assertIn("build/ci-ninja-console/install.json", text)
        self.assertLess(text.index("Check actual macOS Ninja console ownership"),
                        text.index("Install macOS IO and both desktop hosts"))
        self.assertIn("python -m pip install", install_command("macOS"))

    def test_mac_observer_witness_precedes_full_build_with_original_target(self):
        text = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf-8")
        section = text.split("      - name: Check independent observations on the macOS11 target\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("if: runner.os == 'macOS'", section)
        self.assertIn("-mmacosx-version-min=11.0", section)
        self.assertIn("-std=c++17", section)
        self.assertIn("tests/native/test_observation_decimal.cpp", section)
        self.assertIn("build/ci-observation-decimal/witness >", section)
        self.assertLess(text.index("Check independent observations on the macOS11 target"),
                        text.index("Install macOS IO and both desktop hosts"))
        self.assertIn("build/ci-observation-decimal/result.log", text)

    def test_authenticated_history_is_required_without_replacing_complete_readers(self):
        root_workflow = (ROOT / ".github/workflows/test.yml").read_text(encoding="utf-8")
        source_workflow = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf-8")
        wrapper = "python -I -B scripts/ci_history_build.py --output build/ci-history-build -- python -m pip install"
        self.assertIn(wrapper, root_workflow)
        self.assertEqual(source_workflow.count(wrapper), 1)
        self.assertIn("--history-suite --output build/ci-history --history-build-receipt build/ci-history-build/receipt.json", root_workflow)
        self.assertIn("--output build/ci-python --history-output build/ci-history", root_workflow)
        self.assertLess(root_workflow.index("--history-suite"), root_workflow.index("--output build/ci-python"))
        self.assertIn("--windows-gl-suite", source_workflow)
        self.assertIn("--history-build-receipt build/ci-history-build/receipt.json", source_workflow)
        self.assertIn("--history-output build/ci-history --appearance-output build/ci-appearance", source_workflow)
        for workflow in (root_workflow, source_workflow):
            self.assertIn("build/ci-history/capture.zip", workflow)
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


class AppearanceWorkflow(unittest.TestCase):
    def test_transport_checks_follow_installed_pytest_and_precede_sdk_qualification(self):
        text = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf8")
        early = text.split("      - name: Check installed qualification contracts before compilation\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertNotIn("test_ci_python_suite_transport.py", early)
        for program in ("history_integration", "scientific_sdk", "windows_gl"):
            self.assertIn("tests/tools/test_ci_" + program + ".py", early)
        title = "Check installed suite transport after test dependencies"
        step = text.split("      - name: " + title + "\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("run: python -I -X utf8 -B tests/tools/test_ci_python_suite_transport.py", step)
        self.assertNotIn("if:", step)
        for platform in ("Windows", "macOS"):
            self.assertIn(".[io,test]", install_command(platform))
            self.assertLess(text.index("Install " + platform + " IO and both desktop hosts"), text.index(title))
            self.assertLess(text.index(title), text.index("Qualify the installed " + platform + " scientific SDK"))

    def test_appearance_uses_authenticated_build_then_reuses_qualified_packet(self):
        for name in ("source-platform.yml", "test.yml"):
            text = (ROOT / ".github/workflows" / name).read_text(encoding="utf8")
            with self.subTest(workflow=name):
                self.assertIn("--history-output build/ci-history --appearance-output build/ci-appearance", text)
                if name == "test.yml":
                    self.assertIn("--appearance-suite --output build/ci-appearance --appearance-build-receipt build/ci-history-build/receipt.json", text)
                    self.assertLess(text.index("--history-suite"), text.index("--appearance-suite"))
                    self.assertLess(text.index("--appearance-suite"), text.index("--appearance-output"))
                else:
                    self.assertIn("--windows-gl-suite", text)
                    self.assertIn("--history-build-receipt build/ci-history-build/receipt.json", text)
                for member in ("receipt.json", "product-bindings.json", "capture.zip", "reader/*.xml", "capture-owner/*"):
                    self.assertIn("build/ci-appearance/" + member, text)

    def test_windows_provider_wraps_required_suite_after_sdk_and_retains_no_dlls(self):
        text = (ROOT / ".github/workflows/source-platform.yml").read_text(encoding="utf8")
        title = "Qualify Windows UI and complete suite with the disposable GL provider"
        step = text.split("      - name: " + title + "\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("if: runner.os == 'Windows'", step)
        argv = shlex.split(" ".join(step.split("run: >-", 1)[1].split()))
        self.assertEqual(argv[:5], ["python", "-I", "-X", "utf8", "-B"])
        self.assertEqual(argv[5:12], ["scripts/ci_windows_gl.py", "--output", "build/ci-windows-gl",
                                    "--notices", ".github/ci-data/windows-gl-notices.zip",
                                    "--notices-sha256", "8600b81dfa22930629e6a60ee170582dacb238e90f011fd63360105730ed2b10"])
        self.assertEqual(argv[12:], ["--", "python", "-I", "-X", "utf8", "-B",
                                    "scripts/ci_python_suite.py", "--windows-gl-suite",
                                    "--output", "build/ci-python", "--history-build-receipt", "build/ci-history-build/receipt.json",
                                    "--history-output", "build/ci-history", "--appearance-output", "build/ci-appearance"])
        self.assertLess(text.index("Qualify the installed Windows scientific SDK"), text.index(title))
        self.assertLess(text.index("Check installed qualification contracts before compilation"),
                        text.index("Install Windows IO and both desktop hosts"))
        retained = text.split("      - name: Retain disposable Windows GL diagnostics and notices\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("if: always() && runner.os == 'Windows'", retained)
        paths = retained.split("path: |", 1)[1].split("if-no-files-found:", 1)[0].split()
        self.assertEqual(paths, ["build/ci-windows-gl/*.json", "build/ci-windows-gl/*.log", "build/ci-windows-gl/notices.zip"])
        self.assertNotIn("continue-on-error", step)
        self.assertIn("python -I -B tests/tools/test_ci_windows_gl.py",
                      (ROOT / ".github/workflows/test.yml").read_text(encoding="utf8"))


if __name__ == "__main__":
    unittest.main()
