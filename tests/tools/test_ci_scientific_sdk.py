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
import os
from pathlib import Path
import py_compile
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]


def source_module(name):
    path = ROOT / "scripts" / (name + ".py")
    before = path.read_bytes()
    spec = importlib.util.spec_from_file_location(
        "sdk_control_" + name, path)
    module = importlib.util.module_from_spec(spec)
    exec(compile(before, str(path), "exec", dont_inherit=True), module.__dict__)
    if path.read_bytes() != before:
        raise ValueError("SDK control helper source changed while loading")
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


class SourceLoadingControls(unittest.TestCase):
    def test_both_source_loaders_bypass_a_real_timestamp_valid_stale_pyc(self):
        with tempfile.TemporaryDirectory(prefix="imagejxx-sdk-source-control-") as temporary, \
                patch.object(sys, "pycache_prefix", None), patch.object(sys, "dont_write_bytecode", True):
            root = Path(temporary).resolve()
            (root / "scripts").mkdir()
            path = root / "scripts/marker.py"
            old, current = b"VALUE = 'old'\n", b"VALUE = 'new'\n"
            self.assertEqual(len(old), len(current))
            path.write_bytes(old)
            os.utime(path, (1600000000, 1600000000))
            timestamp = path.stat().st_mtime_ns
            cached = Path(py_compile.compile(str(path), doraise=True,
                         invalidation_mode=py_compile.PycInvalidationMode.TIMESTAMP))
            old_cache = cached.read_bytes()
            path.write_bytes(current)
            os.utime(path, ns=(timestamp, timestamp))
            source_pin = hashlib.sha256(current).hexdigest()
            with patch.object(sdk, "ROOT", root), patch.dict(globals(), {"ROOT": root}):
                for label, loader in (("SDK", sdk.load_script), ("test harness", source_module)):
                    with self.subTest(loader=label):
                        legacy_spec = importlib.util.spec_from_file_location("sdk_legacy_control", path)
                        legacy = importlib.util.module_from_spec(legacy_spec)
                        legacy_spec.loader.exec_module(legacy)
                        self.assertEqual(legacy.VALUE, "old", "Causal stale-bytecode control did not reproduce")
                        loaded = loader("marker")
                        self.assertEqual(loaded.VALUE, "new")
                        self.assertEqual(loaded.__file__, str(path))
                        self.assertEqual(loaded.__spec__.origin, str(path))
                        self.assertEqual((path.stat().st_size, path.stat().st_mtime_ns), (len(current), timestamp))
                        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), source_pin)
                        self.assertEqual(cached.read_bytes(), old_cache)


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

    def archive_fixture(self, *, mismatch):
        """Plain text markers only; these files are not compiled archives."""
        cache, installed = self.root / "cache", self.root / "sdk"
        names = ("libimagejxx_core.a", "libimagejxx_io.a")
        inputs = {}
        for name, raw in zip(names, (b"synthetic-core\n", b"synthetic-io\n")):
            inputs[name] = sdk.pin(self.write(Path("cache") / name, raw))
            self.write(Path("sdk/lib") / name, b"different-core\n" if mismatch and name == names[0] else raw)
        script = self.write("cache/cmake_install.cmake", '# Synthetic source only.\r\nmessage("literal \u03bb")\n')
        inputs["cmake_install.cmake"] = sdk.pin(script)
        return cache, installed, names, inputs, script

    def test_matching_archives_retain_both_pins_without_install_script_read(self):
        cache, installed, names, inputs, script = self.archive_fixture(mismatch=False)
        receipt = {}
        with patch.object(sdk, "pin", wraps=sdk.pin) as pins:
            with patch.object(sdk, "install_script_diagnostic", side_effect=AssertionError("No mismatch text read")):
                sdk.verify_installed_archives(cache, installed, names, inputs, receipt)
        self.assertEqual([call.args[0] for call in pins.call_args_list], [installed / "lib" / n for n in names])
        self.assertEqual(receipt["archive_comparisons"]["mismatches"], [])
        self.assertEqual(set(receipt["archive_comparisons"]["installed_after_install"]), set(names))
        self.assertNotIn("archive_install_script", receipt)
        self.assertEqual(sdk.pin(script), inputs["cmake_install.cmake"])

    def test_first_archive_mismatch_is_fatal_with_both_pins_and_exact_install_source(self):
        cache, installed, names, inputs, script = self.archive_fixture(mismatch=True)
        original = script.read_bytes()
        receipt = {}
        with self.assertRaisesRegex(ValueError, "^Installed SDK archive differs from the existing wheel build$"):
            sdk.verify_installed_archives(cache, installed, names, inputs, receipt)
        comparison = receipt["archive_comparisons"]
        self.assertEqual(comparison["mismatches"], [names[0]])
        for name in names:
            self.assertEqual(comparison["raw_before_install"][name], {"path": str(cache/name), **inputs[name]})
            path = installed / "lib" / name
            self.assertEqual(comparison["installed_after_install"][name], {"path": str(path), **sdk.pin(path)})
        diagnostic = receipt["archive_install_script"]
        self.assertEqual(diagnostic["state"], "observed")
        self.assertEqual(diagnostic["source"], diagnostic["observed"])
        self.assertEqual(diagnostic["source"], {"path": str(script), **inputs["cmake_install.cmake"]})
        self.assertEqual(diagnostic["utf8"].encode("utf-8"), original)
        self.assertEqual(script.read_bytes(), original)

    def test_unavailable_changed_or_oversized_install_source_never_masks_archive_refusal(self):
        for problem in ("changed", "oversized", "missing"):
            with self.subTest(problem=problem):
                cache, installed, names, inputs, script = self.archive_fixture(mismatch=True)
                if problem == "changed":
                    script.write_bytes(script.read_bytes().replace(b"literal", b"altered"))
                elif problem == "missing":
                    script.unlink()
                receipt = {}
                with patch.object(sdk, "MAX_INSTALL_SCRIPT_BYTES", 8 if problem == "oversized" else 256*1024):
                    with self.assertRaisesRegex(ValueError, "^Installed SDK archive differs from the existing wheel build$"):
                        sdk.verify_installed_archives(cache, installed, names, inputs, receipt)
                self.assertEqual(set(receipt["archive_comparisons"]["installed_after_install"]), set(names))
                diagnostic = receipt["archive_install_script"]
                self.assertEqual(diagnostic["state"], "unavailable")
                self.assertEqual(diagnostic["source"], {"path": str(script), **inputs["cmake_install.cmake"]})
                self.assertNotIn("utf8", diagnostic)
                self.assertIn("error", diagnostic)
                if problem == "changed":
                    self.assertNotEqual(diagnostic["observed"], diagnostic["source"])
                elif problem == "oversized":
                    self.assertEqual(diagnostic["byte_limit"], 8)
                    self.assertIn("byte bound", diagnostic["error"]["message"])
                else:
                    self.assertEqual(diagnostic["error"]["type"], "FileNotFoundError")

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


class RanlibDiagnosticControls(unittest.TestCase):
    """Synthetic BSD bytes and stubbed process transport; no tool is executed."""
    @staticmethod
    def archive(name=b"tiny.o/", payload=b"object marker\n", date=b"0"):
        header = (name.ljust(16, b" ") + date.ljust(12, b" ") + b"0".ljust(6, b" ") +
                  b"0".ljust(6, b" ") + b"100644".ljust(8, b" ") + str(len(payload)).encode().ljust(10, b" ") + b"`\n")
        return b"!<arch>\n" + header + payload + (b"\n" if len(payload) % 2 else b"")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-ranlib-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.raw = self.root / "libimagejxx_io.a"
        self.raw.write_bytes(self.archive())
        self.raw_pin = sdk.pin(self.raw)
        self.tool = self.root / "ranlib"
        self.tool.write_bytes(b"Not an executable; transport is stubbed.\n")
        self.script = self.root / "cmake_install.cmake"
        self.script.write_text('    execute_process(COMMAND "' + self.tool.as_posix() +
            '" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libimagejxx_io.a")\n', encoding="utf-8")
        self.install = sdk.install_script_diagnostic(self.script, sdk.pin(self.script))
        self.output = self.root / "diagnostic"
        self.calls = []
        self.exit_code, self.cleanup = 0, True
        self.process_guard = patch.object(subprocess, "Popen", side_effect=AssertionError("No real diagnostic process"))
        self.process_guard.start()
        self.addCleanup(self.process_guard.stop)

    def execute(self, argv, *, cwd, env, timeout, stdout, stderr, receipt_path):
        self.calls.append({"argv": argv, "environment": dict(env), "timeout": timeout})
        target = Path(argv[1])
        raw = target.read_bytes()
        # Only the declared ar date's first byte changes; object payload stays literal.
        target.write_bytes(raw[:24] + (b"0" if env.get("ZERO_AR_DATE") == "1" else b"1") + raw[25:])
        proof = {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": self.exit_code,
                 "target_completed": True, "cleanup_complete": self.cleanup, "timed_out": False,
                 "argv": argv, "cwd": str(cwd)}
        Path(receipt_path).write_text(json.dumps(proof), encoding="utf-8")
        return proof

    def run_diagnostic(self, environment=None):
        return sdk.diagnose_archive_ranlib(self.raw, self.raw_pin, self.install, output=self.output,
            root=self.root, execute=self.execute, environment=environment or {"CONTROL_SECRET": "do-not-retain"})

    def test_bsd_short_and_extended_members_have_literal_payload_hashes(self):
        rows = sdk.archive_member_summary(self.archive())
        self.assertEqual(len(rows), 1)
        self.assertEqual((rows[0]["name"], rows[0]["header_offset"], rows[0]["date"], rows[0]["payload_bytes"]),
                         ("tiny.o", 8, "0", 14))
        self.assertEqual(rows[0]["payload_sha256"], hashlib.sha256(b"object marker\n").hexdigest())
        extended = sdk.archive_member_summary(self.archive(b"#1/20", b"longer-name.o".ljust(20, b"\0") + b"abc"))
        self.assertEqual((extended[0]["name"], extended[0]["payload_bytes"], extended[0]["payload_sha256"]),
                         ("longer-name.o", 3, hashlib.sha256(b"abc").hexdigest()))

    def test_unsupported_thin_gnu_truncated_and_unbounded_forms_refuse(self):
        valid = self.archive()
        inputs = (b"!<thin>\n", b"not an archive", valid[:-1], valid[:66] + b"xx" + valid[68:],
                  self.archive(b"//"), self.archive(b"#1/99999"), self.archive(b"tiny.o/", b"a")[:-1])
        for raw in inputs:
            with self.subTest(raw=raw[:32]), self.assertRaises(ValueError):
                sdk.archive_member_summary(raw)
        with patch.object(sdk, "MAX_DIAGNOSTIC_ARCHIVE_BYTES", len(valid) - 1), self.assertRaises(ValueError):
            sdk.archive_member_summary(valid)

    def test_diff_offsets_are_exact_and_many_ranges_are_explicitly_bounded(self):
        before, after = self.archive(), self.archive(date=b"1")
        self.assertEqual(sdk.archive_byte_diff(before, after),
                         {"changed_bytes": 1, "ranges": [[24, 25]], "range_count": 1, "ranges_complete": True})
        many = sdk.archive_byte_diff(b"00" * 200, b"10" * 200)
        self.assertEqual((many["changed_bytes"], many["range_count"], len(many["ranges"]), many["ranges_complete"]),
                         (200, 200, 128, False))

    def test_owned_diagnostic_records_three_steps_and_retires_only_its_copies(self):
        original, tool = self.raw.read_bytes(), self.tool.read_bytes()
        result = self.run_diagnostic()
        self.assertEqual(result["state"], "observed")
        self.assertTrue(result["cleanup_complete"] and result["raw_source_unchanged"])
        self.assertFalse(result["acceptance_claim"])
        self.assertEqual([row["matches_raw"] for row in result["steps"]], [False, True, True])
        self.assertTrue(result["steps"][2]["unchanged_by_command"])
        self.assertEqual(result["steps"][0]["diff_from_raw"]["ranges"], [[24, 25]])
        for step in result["steps"]:
            self.assertEqual(step["after"]["members"][0]["payload_sha256"], hashlib.sha256(b"object marker\n").hexdigest())
            self.assertTrue((self.output / step["process"]["path"]).is_file())
        self.assertEqual([row["environment"].get("ZERO_AR_DATE") for row in self.calls], [None, "1", "1"])
        self.assertTrue(all(row["timeout"] == 30 and row["argv"][0] == str(self.tool) for row in self.calls))
        self.assertTrue(all(row["retired"] for row in result["copies"]))
        self.assertFalse(list(self.output.glob("*.a")))
        self.assertEqual((self.raw.read_bytes(), self.tool.read_bytes()), (original, tool))
        self.assertNotIn("do-not-retain", (self.output / "receipt.json").read_text())

    def test_inherited_zero_date_is_recorded_without_changing_default_environment(self):
        result = self.run_diagnostic({"ZERO_AR_DATE": "1"})
        self.assertEqual(result["inherited_ZERO_AR_DATE"], "1")
        self.assertEqual([step["matches_raw"] for step in result["steps"]], [True, True, True])
        self.assertEqual(self.calls[0]["environment"], {"ZERO_AR_DATE": "1"})

    def test_nonzero_tool_records_unavailable_and_resolved_cleanup(self):
        self.exit_code = 7
        result = self.run_diagnostic()
        self.assertEqual((result["state"], len(self.calls)), ("unavailable", 1))
        self.assertTrue(result["cleanup_complete"])
        self.assertFalse(list(self.output.glob("*.a")))
        self.assertEqual(sdk.pin(self.raw), self.raw_pin)

    def test_unresolved_tool_preserves_copies_without_claiming_cleanup(self):
        self.cleanup = False
        result = self.run_diagnostic()
        self.assertEqual(result["state"], "unavailable")
        self.assertFalse(result["cleanup_complete"])
        self.assertEqual(len(list(self.output.glob("*.a"))), 3)
        self.assertIn("ownership unresolved", result["cleanup_error"]["message"])

    def test_changed_script_refuses_before_any_tool(self):
        self.script.write_text("changed source", encoding="utf-8")
        result = self.run_diagnostic()
        self.assertEqual((result["state"], self.calls), ("unavailable", []))
        self.assertTrue(result["cleanup_complete"])
        self.assertFalse(result["copies"])

    def test_original_guard_remains_fatal_with_diagnostic_success_or_error(self):
        installed = self.root / "sdk/lib"
        installed.mkdir(parents=True)
        (installed / self.raw.name).write_bytes(b"different installed bytes")
        inputs = {self.raw.name: self.raw_pin, "cmake_install.cmake": sdk.pin(self.script)}
        for platform, outcome in (("darwin", {"state": "observed"}), ("darwin", RuntimeError("diagnostic refused")),
                                  ("win32", {"state": "must not run"})):
            callback = Mock(side_effect=outcome) if isinstance(outcome, Exception) else Mock(return_value=outcome)
            receipt = {}
            with self.subTest(platform=platform, outcome=outcome), patch.object(sdk.sys, "platform", platform):
                with self.assertRaisesRegex(ValueError, "^Installed SDK archive differs from the existing wheel build$"):
                    sdk.verify_installed_archives(self.root, installed.parent, [self.raw.name], inputs, receipt,
                                                  diagnostic=callback)
            self.assertEqual(callback.call_count, 1 if platform == "darwin" else 0)
            self.assertEqual(receipt["archive_comparisons"]["mismatches"], [self.raw.name])


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
