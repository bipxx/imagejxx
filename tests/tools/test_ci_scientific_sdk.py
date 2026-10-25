"""Small SDK admission/transport controls; no compiler or product is executed.

Temporary caches contain text metadata and a deliberately non-executable
compiler marker. Source-contract assertions do not qualify C++ compilation,
TIFF behavior, dependency provenance, or either hosted platform.
"""
from contextlib import redirect_stderr
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]


def source_module(name):
    spec = importlib.util.spec_from_file_location(
        "sdk_control_" + name, ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sdk = source_module("ci_scientific_sdk")
builder = source_module("ci_platform_dependencies")
verifier = source_module("verify_ci_platform_dependencies")
WINDOWS = "windows-x64-ci-dynamic-source-001"
ARM = "macos-arm64-ci-static-source-001"
INTEL = "macos-x86_64-ci-static-source-001"
LINUX = "linux-x86_64-ci-static-source-001"
CASE = "imagejxx_scientific_sdk_roundtrip"
CARRIER_SHA = "c5865779efc1945ed75db444d28f0545f8669dec7828bac8c53239d24fabbb64"


class MetadataControls(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-sdk-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.start_patch(patch.object(subprocess, "Popen",
                                     side_effect=AssertionError("No subprocess is allowed in metadata controls")))
        self.start_patch(patch.object(builder.urllib.request, "urlopen",
                                     side_effect=AssertionError("No acquisition is allowed in metadata controls")))
        self.start_patch(patch.object(sys, "dont_write_bytecode", True))

    def start_patch(self, value):
        result = value.start()
        self.addCleanup(value.stop)
        return result

    def write(self, relative, raw):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw if isinstance(raw, bytes) else raw.encode("utf-8"))
        return path

    def cache(self, changes=None, omitted=()):
        compiler = self.write("metadata-only-compiler.txt", b"Not an executable.\n")
        values = {
            "IMAGEJXX_BUILD_IO": "ON", "CMAKE_BUILD_TYPE": "Release",
            "CMAKE_GENERATOR": "Ninja", "IMAGEJXX_CI_SOURCE_PROFILE": WINDOWS,
            "CMAKE_INSTALL_LIBDIR": "lib", "CMAKE_INSTALL_INCLUDEDIR": "include",
            "CMAKE_HOME_DIRECTORY": str(self.root), "CMAKE_CXX_COMPILER": str(compiler),
        }
        values.update(changes or {})
        for key in omitted:
            del values[key]
        text = "# Synthetic admission metadata, not a producer build.\n"
        text += "\n".join(key + ":STRING=" + value for key, value in values.items()) + "\n"
        return self.write("build/wheel/synthetic/CMakeCache.txt", text), values

    def admit(self, path, profile=WINDOWS):
        with patch.object(sdk, "ROOT", self.root):
            return sdk.admit_cache(path, profile, builder)

    def junit(self, body, attributes='tests="1" failures="0" errors="0" skipped="0"'):
        return self.write("sdk.xml", "<testsuite " + attributes + ">" + body + "</testsuite>")

    def refused_profile(self, name):
        output = self.root / "report"
        prefix = self.root / "dependency-prefix"
        prefix.mkdir()
        marker = self.write("dependency-prefix/unchanged.txt", b"original dependency marker\n")
        loader = Mock(side_effect=lambda name: {"ci_platform_dependencies": builder,
                                              "verify_ci_platform_dependencies": verifier}[name])
        with patch.object(sdk, "ROOT", self.root), patch.object(sdk, "load_script", loader):
            with self.assertRaisesRegex(ValueError, "only the new Windows/macOS"):
                sdk.run_profile(output, name, prefix)
        receipt = json.loads((output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual(receipt["commands"], [])
        self.assertEqual(receipt["scratch"], {"state": "not_created"})
        self.assertIs(receipt["full_platform_qualification"], False)
        self.assertFalse((output / "work").exists())
        self.assertEqual(marker.read_bytes(), b"original dependency marker\n")
        self.assertEqual([call.args[0] for call in loader.call_args_list], ["ci_platform_dependencies"])

    def test_cli_has_only_three_explicit_profiles(self):
        self.assertEqual(sdk.PROFILES, (WINDOWS, ARM, INTEL))
        for profile in (WINDOWS, ARM, INTEL):
            with self.subTest(profile=profile):
                args = ["ci_scientific_sdk.py", "--output", "owned-report", "--profile", profile,
                        "--dependency-prefix", "reviewed-prefix"]
                with patch.object(sys, "argv", args), patch.object(sdk, "run_profile") as run:
                    sdk.main()
                    run.assert_called_once_with(Path("owned-report"), profile, Path("reviewed-prefix"))

    def test_cli_unknown_linux_and_old_windows_profiles_never_run(self):
        for profile in ("latest", LINUX, "windows-x64-dynamic-release-001"):
            with self.subTest(profile=profile):
                args = ["ci_scientific_sdk.py", "--output", "unused", "--profile", profile,
                        "--dependency-prefix", "unused"]
                with patch.object(sys, "argv", args), patch.object(sdk, "run_profile") as run:
                    with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as refused:
                        sdk.main()
                    self.assertEqual(refused.exception.code, 2)
                    run.assert_not_called()

    def test_cli_requires_explicit_dependency_prefix(self):
        with patch.object(sys, "argv", ["ci_scientific_sdk.py", "--output", "unused", "--profile", WINDOWS]):
            with patch.object(sdk, "run_profile") as run, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as refused:
                    sdk.main()
                self.assertEqual(refused.exception.code, 2)
                run.assert_not_called()

    def test_unknown_direct_profile_has_durable_quiet_refusal(self):
        self.refused_profile("not-a-reviewed-profile")

    def test_linux_direct_profile_has_durable_quiet_refusal(self):
        self.refused_profile(LINUX)

    def test_output_must_be_fresh_owned_and_disjoint(self):
        prefix = self.root / "prefix"
        prefix.mkdir()
        self.write("prefix/original", b"keep")
        existing = self.root / "existing"
        existing.mkdir()
        choices = (
            (existing, prefix, "fresh explicitly owned"),
            (self.root, prefix, "fresh explicitly owned"),
            (self.root.parent / (self.root.name + "-unowned"), prefix, "fresh explicitly owned"),
            (self.root / "build/wheel/report", prefix, "producer cache"),
            (prefix / "nested-report", prefix, "disjoint"),
            (self.root / "ancestor-report", self.root / "ancestor-report/prefix", "disjoint"),
        )
        with patch.object(sdk, "ROOT", self.root), patch.object(sdk, "load_script", return_value=builder):
            for output, dependency, message in choices:
                with self.subTest(output=output):
                    existed = output.exists()
                    with self.assertRaisesRegex(ValueError, message):
                        sdk.run_profile(output, WINDOWS, dependency)
                    self.assertEqual(output.exists(), existed)
        self.assertEqual((prefix / "original").read_bytes(), b"keep")

    def test_missing_prefix_certificate_refuses_before_commands_or_work(self):
        prefix = self.root / "prefix"
        prefix.mkdir()
        output = self.root / "report"
        modules = {"ci_platform_dependencies": builder, "verify_ci_platform_dependencies": verifier}
        with patch.object(sdk, "ROOT", self.root), patch.object(sdk, "load_script", side_effect=modules.__getitem__):
            with patch.object(builder, "require_host"), patch.object(sys, "version_info", (3, 12, 0)):
                with self.assertRaises(FileNotFoundError):
                    sdk.run_profile(output, WINDOWS, prefix)
        receipt = json.loads((output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual(receipt["commands"], [])
        self.assertFalse((output / "work").exists())
        self.assertEqual(list(prefix.iterdir()), [])

    def test_unprepared_or_wrong_profile_certificate_is_not_sdk_acceptance(self):
        for index, changes in enumerate(({"state": "passed"}, {"acceptance": True}, {"profile": ARM})):
            with self.subTest(changes=changes):
                prefix = self.root / ("prefix" + str(index))
                certificate = {"schema": "imagejxx.ci-platform-dependencies.001",
                               "state": "prepared", "acceptance": False, "profile": WINDOWS}
                certificate.update(changes)
                original = self.write(prefix.relative_to(self.root) / "share/imagejxx-ci-build.json",
                                      json.dumps(certificate)).read_bytes()
                output = self.root / ("report" + str(index))
                modules = {"ci_platform_dependencies": builder, "verify_ci_platform_dependencies": verifier}
                with patch.object(sdk, "ROOT", self.root), patch.object(sdk, "load_script", side_effect=modules.__getitem__):
                    with patch.object(builder, "require_host"), patch.object(sys, "version_info", (3, 12, 0)):
                        with patch.dict(sdk.os.environ, {"SDK_METADATA_SECRET": "must-not-appear-in-receipt"}):
                            with self.assertRaisesRegex(ValueError, "Incomplete/wrong source preparation"):
                                sdk.run_profile(output, WINDOWS, prefix)
                receipt_raw = (output / "receipt.json").read_bytes()
                receipt = json.loads(receipt_raw)
                self.assertEqual(receipt["commands"], [])
                self.assertIs(receipt["environment_values_recorded"], False)
                self.assertNotIn(b"must-not-appear-in-receipt", receipt_raw)
                self.assertFalse((output / "work").exists())
                self.assertEqual((prefix / "share/imagejxx-ci-build.json").read_bytes(), original)

    def test_wheel_cache_requires_exactly_one_existing_cache(self):
        with self.assertRaisesRegex(ValueError, "unavailable"):
            sdk.wheel_cache(self.root, builder)
        (self.root / "build/wheel").mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "exactly one.*found 0"):
            sdk.wheel_cache(self.root, builder)
        first, _ = self.cache()
        self.assertEqual(sdk.wheel_cache(self.root, builder), first)
        self.write("build/wheel/second/CMakeCache.txt", b"not a chosen generation")
        with self.assertRaisesRegex(ValueError, "exactly one.*found 2"):
            sdk.wheel_cache(self.root, builder)

    def test_synthetic_cache_admits_exact_settings_for_each_source_profile(self):
        for profile in (WINDOWS, ARM, INTEL):
            with self.subTest(profile=profile):
                path, values = self.cache({"IMAGEJXX_CI_SOURCE_PROFILE": profile})
                parsed, admitted = self.admit(path, profile)
                self.assertEqual(parsed, values)
                self.assertEqual(admitted, values)

    def test_cache_changed_or_missing_effective_settings_refuse(self):
        changes = {"IMAGEJXX_BUILD_IO": "OFF", "CMAKE_BUILD_TYPE": "Debug",
                   "CMAKE_GENERATOR": "Ninja Multi-Config", "IMAGEJXX_CI_SOURCE_PROFILE": ARM,
                   "CMAKE_INSTALL_LIBDIR": "lib64", "CMAKE_INSTALL_INCLUDEDIR": "headers"}
        for name, bad_value in changes.items():
            with self.subTest(name=name):
                path, _ = self.cache({name: bad_value})
                with self.assertRaisesRegex(ValueError, "Effective CMake setting changed: " + name):
                    self.admit(path)
                path, _ = self.cache(omitted=(name,))
                with self.assertRaisesRegex(ValueError, "Effective CMake setting changed: " + name):
                    self.admit(path)

    def test_cache_multiconfig_foreign_home_and_missing_compiler_refuse(self):
        path, _ = self.cache({"CMAKE_CONFIGURATION_TYPES": "Debug;Release"})
        with self.assertRaisesRegex(ValueError, "multi-configuration"):
            self.admit(path)
        foreign = self.root / "other-source"
        foreign.mkdir()
        path, _ = self.cache({"CMAKE_HOME_DIRECTORY": str(foreign)})
        with self.assertRaisesRegex(ValueError, "does not belong"):
            self.admit(path)
        path, _ = self.cache(omitted=("CMAKE_HOME_DIRECTORY",))
        with self.assertRaisesRegex(ValueError, "does not belong"):
            self.admit(path)
        path, _ = self.cache({"CMAKE_CXX_COMPILER": str(self.root / "missing-compiler")})
        with self.assertRaises(FileNotFoundError):
            self.admit(path)

    def test_cache_toolchain_and_launcher_injections_refuse(self):
        names = ("CMAKE_TOOLCHAIN_FILE", "CMAKE_CXX_COMPILER_LAUNCHER", "CMAKE_CXX_LINKER_LAUNCHER",
                 "CMAKE_PROJECT_TOP_LEVEL_INCLUDES", "CMAKE_PROJECT_INCLUDE", "CMAKE_PROJECT_INCLUDE_BEFORE",
                 "CMAKE_USER_MAKE_RULES_OVERRIDE", "CMAKE_USER_MAKE_RULES_OVERRIDE_CXX")
        for name in names:
            with self.subTest(name=name):
                path, _ = self.cache({name: "unreviewed-command"})
                with self.assertRaisesRegex(ValueError, "toolchain/launcher"):
                    self.admit(path)

    def test_junit_exact_one_executed_case(self):
        for status in ('', ' status="run"', ' status="passed"'):
            with self.subTest(status=status):
                path = self.junit('<testcase name="' + CASE + '"' + status + '><system-out>literal check log</system-out></testcase>')
                self.assertEqual(sdk.exact_junit(path), {
                    "case_ids": [CASE], "passed": 1, "failed": 0, "errors": 0, "skipped": 0})

    def test_junit_wrong_count_name_or_nonexecuted_status_refuse(self):
        bodies = ("", '<testcase name="other"/>', '<testcase name="' + CASE + '"/><testcase name="extra"/>',
                  '<testcase name="' + CASE + '" status="notrun"/>', '<testcase name="' + CASE + '" status="disabled"/>')
        for body in bodies:
            with self.subTest(body=body):
                with self.assertRaisesRegex(ValueError, "one exact installed"):
                    sdk.exact_junit(self.junit(body))

    def test_junit_failure_error_skip_or_nonzero_suite_counters_refuse(self):
        for tag in ("failure", "error", "skipped"):
            with self.subTest(tag=tag):
                with self.assertRaisesRegex(ValueError, "failed, errored or skipped"):
                    sdk.exact_junit(self.junit('<testcase name="' + CASE + '"><' + tag + '/></testcase>'))
        for counter in ("failures", "errors", "skipped", "disabled"):
            with self.subTest(counter=counter):
                with self.assertRaisesRegex(ValueError, "unqualified SDK case"):
                    sdk.exact_junit(self.junit('<testcase name="' + CASE + '"/>', counter + '="1"'))

    def test_runtime_dependency_observations_are_parsed_without_execution(self):
        windows = self.write("dumpbin.log", "Dump of file consumer.exe\n    tiff.dll\n    KERNEL32.dll\n")
        self.assertEqual(sdk.runtime_libraries(windows, "win32"), ["tiff.dll", "KERNEL32.dll"])
        mac = self.write("otool.log", "consumer:\n\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1.0.0)\n"
                         "\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n")
        self.assertEqual(sdk.runtime_libraries(mac, "darwin"), ["/usr/lib/libc++.1.dylib", "/usr/lib/libSystem.B.dylib"])

    def test_runtime_python_link_or_empty_observation_refuses(self):
        for index, (system, text) in enumerate((
                ("win32", "  python312.dll\n"), ("win32", "tool failed before listing imports\n"),
                ("darwin", "\t/Library/Frameworks/Python.framework/Versions/3.12/Python (compatibility version 3.12.0)\n"),
                ("darwin", "consumer:\n"))):
            with self.subTest(system=system, text=text):
                with self.assertRaisesRegex(ValueError, "absent or contain Python"):
                    sdk.runtime_libraries(self.write(str(index) + ".log", text), system)

    def test_inventory_is_literal_sorted_and_bounded(self):
        self.write("tree/z.bin", b"z")
        self.write("tree/sub/a.bin", b"abc")
        expected = [{"path": "sub/a.bin", "bytes": 3, "sha256": hashlib.sha256(b"abc").hexdigest()},
                    {"path": "z.bin", "bytes": 1, "sha256": hashlib.sha256(b"z").hexdigest()}]
        self.assertEqual(sdk.tree_files(self.root / "tree"), expected)
        for bound, limit in (("MAX_FILES", 1), ("MAX_TREE_BYTES", 3)):
            with self.subTest(bound=bound), patch.object(sdk, bound, limit):
                with self.assertRaisesRegex(ValueError, "inventory exceeds"):
                    sdk.tree_files(self.root / "tree")
        self.assertEqual((self.root / "tree/sub/a.bin").read_bytes(), b"abc")

    def test_retirement_removes_only_owned_work_after_retaining_literal_inventory(self):
        output = self.root / "report"
        self.write("report/work/sdk/lib/archive.txt", b"archive marker")
        self.write("report/work/consumer/bin/client.txt", b"consumer marker")
        self.write("report/receipt.json", b"retained receipt")
        self.write("unrelated/keep.txt", b"unrelated marker")
        expected = [
            {"path": "consumer/bin/client.txt", "bytes": 15, "sha256": hashlib.sha256(b"consumer marker").hexdigest()},
            {"path": "sdk/lib/archive.txt", "bytes": 14, "sha256": hashlib.sha256(b"archive marker").hexdigest()},
        ]
        result = sdk.retire_work(output / "work", builder, output)
        self.assertEqual((result["state"], result["files"], result["bytes"]), ("retired", 2, 29))
        self.assertFalse((output / "work").exists())
        manifest = (output / "retired-work-files.json").read_bytes()
        self.assertEqual(json.loads(manifest), expected)
        self.assertEqual(result["inventory"], {"path": "retired-work-files.json", "bytes": len(manifest),
                                              "sha256": hashlib.sha256(manifest).hexdigest()})
        self.assertEqual((output / "receipt.json").read_bytes(), b"retained receipt")
        self.assertEqual((self.root / "unrelated/keep.txt").read_bytes(), b"unrelated marker")

    def test_retirement_rejects_wrong_target_before_retention_or_deletion(self):
        output = self.root / "report"
        output.mkdir()
        for relative in ("unrelated", "report/other-work", "report/work/nested"):
            with self.subTest(relative=relative):
                target = self.root / relative
                self.write(Path(relative) / "keep.txt", b"do not remove")
                with self.assertRaisesRegex(ValueError, "escaped its owned work root"):
                    sdk.retire_work(target, builder, output)
                self.assertEqual((target / "keep.txt").read_bytes(), b"do not remove")
                self.assertFalse((output / "retired-work-files.json").exists())

    def test_retirement_preserves_work_when_inventory_retention_fails(self):
        output = self.root / "report"
        marker = self.write("report/work/keep.txt", b"retain after diagnostic failure")
        with patch.object(sdk, "write_json", side_effect=OSError("synthetic retention failure")):
            with self.assertRaisesRegex(OSError, "retention failure"):
                sdk.retire_work(output / "work", builder, output)
        self.assertEqual(marker.read_bytes(), b"retain after diagnostic failure")
        self.assertFalse((output / "retired-work-files.json").exists())

    def test_retirement_preserves_every_member_when_inventory_bound_refuses(self):
        output = self.root / "report"
        self.write("report/work/first", b"first")
        self.write("report/work/second", b"second")
        with patch.object(sdk, "MAX_FILES", 1):
            with self.assertRaisesRegex(ValueError, "inventory exceeds"):
                sdk.retire_work(output / "work", builder, output)
        self.assertEqual((output / "work/first").read_bytes(), b"first")
        self.assertEqual((output / "work/second").read_bytes(), b"second")
        self.assertFalse((output / "retired-work-files.json").exists())

    def test_real_link_is_refused_before_discovery_or_retirement(self):
        outside = self.root / "outside"
        self.write("outside/keep.txt", b"outside linked target")
        output = self.root / "report"
        work = output / "work"
        work.mkdir(parents=True)
        link = work / "linked"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest("Host cannot create the bounded test symlink: " + str(error))
        with self.assertRaisesRegex(ValueError, "Linked/non-directory"):
            sdk.retire_work(work, builder, output)
        self.assertEqual((outside / "keep.txt").read_bytes(), b"outside linked target")
        self.assertTrue(link.is_symlink())
        self.assertFalse((output / "retired-work-files.json").exists())
        link.unlink()
        (self.root / "build/wheel").mkdir(parents=True)
        wheel_link = self.root / "build/wheel/linked"
        wheel_link.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "linked directory"):
            sdk.wheel_cache(self.root, builder)
        self.assertEqual((outside / "keep.txt").read_bytes(), b"outside linked target")


class StaticConsumerContract(unittest.TestCase):
    """These source checks intentionally earn no compilation/runtime credit."""

    def test_original_carrier_pin_and_literal_description_remain_unchanged(self):
        fixture = ROOT / "tests/fixtures/scientific-tiff-u16/defaults.tif"
        raw = fixture.read_bytes()
        self.assertEqual((len(raw), hashlib.sha256(raw).hexdigest()), (116, CARRIER_SHA))
        self.assertEqual(sdk.CARRIER_PIN, {"bytes": 116, "sha256": CARRIER_SHA})
        source = json.loads((fixture.parent / "SOURCE.json").read_bytes())
        rows = [row for row in source["files"] if row["file"] == "defaults.tif"]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["shape"], [1, 1, 1, 1, 2, 1])
        self.assertIs(rows[0]["valid"], True)
        self.assertEqual((rows[0]["bytes"], rows[0]["sha256"]), (116, CARRIER_SHA))

    def test_installed_target_and_exact_ctest_contract_are_declared(self):
        source = (ROOT / "examples/cpp-io/CMakeLists.txt").read_text(encoding="utf-8")
        active = "\n".join(line.split("#", 1)[0] for line in source.splitlines())
        self.assertEqual(re.findall(r"\badd_executable\s*\(\s*(\w+)\s+([^)]*)\)", active, re.I),
                         [("imagejxx_scientific_sdk", "main.cpp")])
        self.assertRegex(active, r"target_link_libraries\(imagejxx_scientific_sdk\s+PRIVATE\s+imagejxx::io\)")
        self.assertRegex(active, r"find_package\(imagejxx\s+CONFIG\s+REQUIRED\s+COMPONENTS\s+io\s+PATHS\s+"
                                r'"\$\{IMAGEJXX_SDK_PREFIX\}/lib/cmake/imagejxx"\s+NO_DEFAULT_PATH\)')
        calls = re.findall(r"\badd_test\s*\(([^)]*)\)", active, re.I | re.S)
        self.assertEqual(len(calls), 1)
        self.assertEqual(" ".join(calls[0].split()),
                         'NAME imagejxx_scientific_sdk_roundtrip COMMAND imagejxx_scientific_sdk '
                         '"${IMAGEJXX_SDK_FIXTURE}" "${IMAGEJXX_SDK_OUTPUT}"')
        self.assertRegex(active, r"set_tests_properties\(imagejxx_scientific_sdk_roundtrip\s+PROPERTIES\s+TIMEOUT\s+30\)")
        self.assertNotRegex(active, r"\b(?:add_subdirectory|FetchContent_[A-Za-z_]+|ExternalProject_Add)\s*\(")

    def test_consumer_is_release_active_public_io_with_literal_sample_checks(self):
        source = (ROOT / "examples/cpp-io/main.cpp").read_text(encoding="utf-8")
        self.assertIn("#include <imagejxx/io.hpp>", source)
        self.assertNotRegex(source, r"\bassert\s*\(|NDEBUG|Python\.h|pybind11")
        self.assertRegex(source, r"if\s*\(!condition\)\s*throw\s+std::runtime_error")
        self.assertIn("std::array<int, 6>{1, 1, 1, 1, 2, 1}", source)
        self.assertIn("dataset.pixel_type() == PixelType::UInt16", source)
        self.assertRegex(source, r"plane->pixels\[0\]\s*==\s*0\.0\s*&&\s*plane->pixels\[1\]\s*==\s*1\.0")
        self.assertIn("read_image_series(source_path.u8string()", source)
        self.assertIn("write_image_series(output_path.u8string(), input.dataset", source)
        self.assertIn("read_image_series(output_path.u8string()", source)
        self.assertIn("exact_dataset(restored.dataset)", source)


if __name__ == "__main__":
    unittest.main()
