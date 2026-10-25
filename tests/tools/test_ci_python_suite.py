"""Installed-runner discovery contracts; fixtures are not executable product proof.

Run with ``python -I -B tests/tools/test_ci_python_suite.py``. File existence and
distribution records are real owned fixtures; execution permission is mocked so
the Windows and POSIX basename contracts run on either host. No product imports,
subprocesses, CMake, downloads or installed environment are required.
"""
from copy import deepcopy
import importlib.metadata
import importlib.util
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ci_python_suite_under_test", ROOT / "scripts/ci_python_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)

# These are the literal historical IDs, independent of the runner's exclusion
# table. Do not replace them with a module/prefix selector or a case count.
TIFF_POLICY = "tests/python/test_tiff_runtime_policy.py::"
WINDOWS_TIFF_IDS = (
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[none]",
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[missing_dll]",
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[changed_dll]",
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[changed_header]",
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[missing_link]",
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[wrong_headers]",
    TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[foreign_zlib]",
    TIFF_POLICY + "test_recheck_refuses_changes_after_successful_configuration[bin/tiff.dll]",
    TIFF_POLICY + "test_recheck_refuses_changes_after_successful_configuration[include/tiffio.h]",
    TIFF_POLICY + "test_install_refuses_a_changed_dependency_before_copying_any_codec",
)
SHARED_TIFF_IDS = tuple(
    TIFF_POLICY + "test_reviewed_closure_rejects_restricted_or_changed_inputs[" + case + "]"
    for case in ("none", "noncommercial", "version", "missing_dependency", "runtime_identity",
                 "extra_dll", "notice", "cmake", "profile"))
WINDOWS_TIFF_REASON = "The exact reviewed delivery profile is Windows x64"


class InstalledApplicationDiscovery(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-ci-discovery-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.site = self.root / "site-packages"
        self.site.mkdir()

    def distribution(self, names):
        records = None if names is None else [importlib.metadata.PackagePath(name) for name in names]
        return SimpleNamespace(files=records,
                               locate_file=Mock(side_effect=lambda record: self.site / record))

    def file(self, name):
        path = self.site / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"owned synthetic application, never executed\n")
        return path.resolve(strict=True)

    def test_windows_selects_exact_exe_record_and_ignores_other_basenames(self):
        expected = self.file("Scripts/imagejxx.exe")
        self.file("bin/imagejxx")
        distribution = self.distribution([
            "imagejxx/__init__.py", "Scripts/imagejxx.exe", "bin/imagejxx"])
        with patch.object(suite.os, "access", return_value=True) as access:
            actual = suite.installed_application(distribution, platform="win32")
        self.assertEqual(actual, expected)
        self.assertIsInstance(actual, Path)
        distribution.locate_file.assert_called_once_with(distribution.files[1])
        access.assert_called_once_with(expected, os.X_OK)

    def test_linux_and_macos_select_exact_unsuffixed_record(self):
        expected = self.file("bin/imagejxx")
        self.file("Scripts/imagejxx.exe")
        for platform in ("linux", "darwin"):
            with self.subTest(platform=platform):
                distribution = self.distribution(["Scripts/imagejxx.exe", "bin/imagejxx"])
                with patch.object(suite.os, "access", return_value=True):
                    self.assertEqual(suite.installed_application(distribution, platform=platform), expected)
                distribution.locate_file.assert_called_once_with(distribution.files[1])

    def test_default_platform_is_read_at_call_time(self):
        windows = self.file("Scripts/imagejxx.exe")
        posix = self.file("bin/imagejxx")
        for platform, expected in (("win32", windows), ("linux", posix), ("darwin", posix)):
            with self.subTest(platform=platform), patch.object(suite.sys, "platform", platform):
                distribution = self.distribution(["Scripts/imagejxx.exe", "bin/imagejxx"])
                with patch.object(suite.os, "access", return_value=True):
                    self.assertEqual(suite.installed_application(distribution), expected)

    def test_wrong_case_suffix_and_platform_basenames_are_refused(self):
        examples = (
            ("win32", "imagejxx"), ("win32", "IMAGEJXX.exe"),
            ("win32", "imagejxx.EXE"), ("win32", "imagejxx.exe.old"),
            ("win32", "imagejxx-script.py"), ("linux", "imagejxx.exe"),
            ("darwin", "ImageJXX"), ("darwin", "imagejxx.app"),
        )
        for platform, name in examples:
            with self.subTest(platform=platform, name=name):
                distribution = self.distribution(["bin/" + name])
                with patch.object(suite.os, "access", return_value=True):
                    with self.assertRaises(ValueError):
                        suite.installed_application(distribution, platform=platform)
                distribution.locate_file.assert_not_called()

    def test_absent_or_empty_distribution_records_are_refused(self):
        for names in (None, []):
            with self.subTest(names=names):
                distribution = self.distribution(names)
                with patch.object(suite.os, "access", return_value=True) as access:
                    with self.assertRaises(ValueError):
                        suite.installed_application(distribution, platform="win32")
                distribution.locate_file.assert_not_called()
                access.assert_not_called()

    def test_missing_recorded_application_is_refused_by_strict_resolution(self):
        distribution = self.distribution(["Scripts/imagejxx.exe"])
        with patch.object(suite.os, "access", return_value=True) as access:
            with self.assertRaises(ValueError):
                suite.installed_application(distribution, platform="win32")
        distribution.locate_file.assert_called_once_with(distribution.files[0])
        access.assert_not_called()

    def test_matching_directory_is_not_an_application(self):
        (self.site / "imagejxx.exe").mkdir()
        distribution = self.distribution(["imagejxx.exe"])
        with patch.object(suite.os, "access", return_value=True):
            with self.assertRaises(ValueError):
                suite.installed_application(distribution, platform="win32")

    def test_non_executable_recorded_file_is_refused(self):
        expected = self.file("bin/imagejxx")
        distribution = self.distribution(["bin/imagejxx"])
        with patch.object(suite.os, "access", return_value=False) as access:
            with self.assertRaises(ValueError):
                suite.installed_application(distribution, platform="linux")
        access.assert_called_once_with(expected, os.X_OK)

    def test_two_recorded_applications_are_ambiguous(self):
        self.file("one/imagejxx.exe")
        self.file("two/imagejxx.exe")
        distribution = self.distribution(["one/imagejxx.exe", "two/imagejxx.exe"])
        with patch.object(suite.os, "access", return_value=True):
            with self.assertRaises(ValueError):
                suite.installed_application(distribution, platform="win32")

    def test_repeated_record_is_not_silently_deduplicated(self):
        self.file("bin/imagejxx")
        distribution = self.distribution(["bin/imagejxx", "bin/imagejxx"])
        with patch.object(suite.os, "access", return_value=True):
            with self.assertRaises(ValueError):
                suite.installed_application(distribution, platform="linux")

    def test_unlisted_path_application_cannot_replace_distribution_ownership(self):
        ambient = self.file("ambient/imagejxx.exe")
        distribution = self.distribution(["imagejxx/__init__.py"])
        with patch.dict(os.environ, {"PATH": str(ambient.parent)}), \
                patch("shutil.which", return_value=str(ambient)) as which, \
                patch.object(suite.subprocess, "run") as process, \
                patch.object(suite.os, "access", return_value=True):
            with self.assertRaises(ValueError):
                suite.installed_application(distribution, platform="win32")
        which.assert_not_called()
        process.assert_not_called()
        distribution.locate_file.assert_not_called()

    def test_recorded_scripts_outside_site_packages_remain_valid(self):
        # Wheel RECORD paths may leave site-packages for the environment's
        # Scripts/bin directory. Ownership is the distribution record, not an
        # invented package-directory boundary or an ambient PATH search.
        expected = self.file("../Scripts/imagejxx.exe")
        self.assertFalse(expected.is_relative_to(self.site))
        distribution = self.distribution(["../Scripts/imagejxx.exe"])
        with patch.object(suite.os, "access", return_value=True):
            self.assertEqual(suite.installed_application(distribution, platform="win32"), expected)
        distribution.locate_file.assert_called_once_with(distribution.files[0])


class PlatformTestSelection(unittest.TestCase):
    def setUp(self):
        # Deliberately interleave and reverse parts of the cohort to prove that
        # selection preserves collection order rather than sorting or grouping.
        self.other = "tests/python/test_example.py::test_required_native_consumer"
        self.ids = [self.other, WINDOWS_TIFF_IDS[9], *SHARED_TIFF_IDS[:4],
                    *WINDOWS_TIFF_IDS[:9], *SHARED_TIFF_IDS[4:]]

    def test_windows_requires_every_original_id_and_excludes_nothing(self):
        before = self.ids.copy()
        selected, excluded = suite.platform_test_selection(self.ids, platform="win32")
        self.assertEqual(selected, before)
        self.assertEqual(excluded, [])
        self.assertEqual(self.ids, before)

    def test_linux_and_macos_exclude_only_exact_ten_with_literal_reason(self):
        for platform in ("linux", "darwin"):
            with self.subTest(platform=platform):
                before = self.ids.copy()
                selected, excluded = suite.platform_test_selection(self.ids, platform=platform)
                self.assertEqual(selected, [item for item in before if item not in WINDOWS_TIFF_IDS])
                self.assertCountEqual(excluded, [
                    {"id": item, "reason": WINDOWS_TIFF_REASON}
                    for item in before if item in WINDOWS_TIFF_IDS])
                self.assertTrue(set(SHARED_TIFF_IDS).issubset(selected))
                self.assertEqual(len(excluded), 10)
                self.assertEqual(self.ids, before)

    def test_default_platform_is_evaluated_at_call_time(self):
        for platform in ("win32", "linux", "darwin"):
            with self.subTest(platform=platform), patch.object(suite.sys, "platform", platform):
                self.assertEqual(suite.platform_test_selection(self.ids),
                                 suite.platform_test_selection(self.ids, platform=platform))

    def test_each_missing_historical_id_is_refused_on_every_platform(self):
        for platform in ("win32", "linux", "darwin"):
            for missing in WINDOWS_TIFF_IDS:
                with self.subTest(platform=platform, missing=missing):
                    ids = [item for item in self.ids if item != missing]
                    with self.assertRaises(ValueError):
                        suite.platform_test_selection(ids, platform=platform)

    def test_same_count_replacement_cannot_hide_a_missing_historical_id(self):
        replacement = TIFF_POLICY + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[new_case]"
        ids = [replacement if item == WINDOWS_TIFF_IDS[0] else item for item in self.ids]
        self.assertEqual(len(ids), len(self.ids))
        for platform in ("win32", "linux", "darwin"):
            with self.subTest(platform=platform), self.assertRaises(ValueError):
                suite.platform_test_selection(ids, platform=platform)

    def test_duplicates_are_refused_in_excluded_and_selected_ids(self):
        for repeated in (WINDOWS_TIFF_IDS[0], SHARED_TIFF_IDS[0], self.other):
            for platform in ("win32", "linux"):
                with self.subTest(platform=platform, repeated=repeated), self.assertRaises(ValueError):
                    suite.platform_test_selection(self.ids + [repeated], platform=platform)

    def test_empty_collection_and_empty_or_non_string_ids_are_refused(self):
        invalid = [[], self.ids + [""], self.ids + [None], self.ids + [7]]
        for ids in invalid:
            with self.subTest(ids=ids), self.assertRaises(ValueError):
                suite.platform_test_selection(ids, platform="linux")

    def test_other_windows_named_cases_are_still_selected(self):
        additional = [
            TIFF_POLICY + "test_future_windows_runtime_policy",
            "tests/python/test_other.py::test_windows_profile[none]",
            WINDOWS_TIFF_IDS[0] + "-not-the-historical-id",
        ]
        selected, excluded = suite.platform_test_selection(self.ids + additional, platform="darwin")
        self.assertEqual(selected[-len(additional):], additional)
        self.assertEqual({row["id"] for row in excluded}, set(WINDOWS_TIFF_IDS))


class JunitIdentity(unittest.TestCase):
    def test_module_function_has_literal_dotted_classname(self):
        self.assertEqual(
            suite.junit_identity("tests/python/test_example.py::test_value"),
            ("tests.python.test_example", "test_value"))

    def test_nested_classes_are_preserved_in_classname(self):
        self.assertEqual(
            suite.junit_identity("tests/python/test_example.py::TestOuter::TestInner::test_value"),
            ("tests.python.test_example.TestOuter.TestInner", "test_value"))

    def test_parameter_text_is_opaque_including_colons_dots_and_nested_brackets(self):
        for parameter in ("params.with::colons[brackets]", "[nested]::x.py", "λ.μ::[left][right]"):
            with self.subTest(parameter=parameter):
                name = "test_value[" + parameter + "]"
                self.assertEqual(
                    suite.junit_identity("tests/python/test_example.py::TestOuter::TestInner::" + name),
                    ("tests.python.test_example.TestOuter.TestInner", name))

    def test_only_terminal_module_py_suffix_is_removed(self):
        self.assertEqual(
            suite.junit_identity("tests/python/test.py.example.py::test_value[fixture.py]"),
            ("tests.python.test.py.example", "test_value[fixture.py]"))


class CompleteResultsValidation(unittest.TestCase):
    def setUp(self):
        self.function = "tests/python/test_example.py::test_required_native_consumer"
        self.nested = ("tests/python/test_example.py::TestOuter::TestInner::"
                       "test_value[params.with::colons[brackets]]")
        self.ids = [self.function, WINDOWS_TIFF_IDS[9], *SHARED_TIFF_IDS[:4],
                    self.nested, *WINDOWS_TIFF_IDS[:9], *SHARED_TIFF_IDS[4:]]
        # Expected terminal identities do not call the function under test.
        self.identities = {
            name: ("tests.python.test_tiff_runtime_policy", name[len(TIFF_POLICY):])
            for name in (*WINDOWS_TIFF_IDS, *SHARED_TIFF_IDS)}
        self.identities[self.function] = ("tests.python.test_example", "test_required_native_consumer")
        self.identities[self.nested] = (
            "tests.python.test_example.TestOuter.TestInner", "test_value[params.with::colons[brackets]]")

    def evidence(self, platform):
        selected = [name for name in self.ids
                    if platform == "win32" or name not in WINDOWS_TIFF_IDS]
        excluded = [] if platform == "win32" else [
            {"id": name, "reason": WINDOWS_TIFF_REASON}
            for name in self.ids if name in WINDOWS_TIFF_IDS]
        collection = {"schema": "imagejxx.complete-collection.001", "platform": platform,
                      "collected": self.ids.copy(), "selected": selected, "excluded": excluded}
        rows = [ET.Element("testcase", classname=self.identities[name][0],
                           name=self.identities[name][1]) for name in selected]
        phases = [{"id": name, "phase": phase, "outcome": "passed", "duration": duration}
                  for name in selected for phase, duration in (("setup", 0), ("call", 0.125), ("teardown", 1))]
        return collection, rows, phases

    def test_complete_windows_linux_and_macos_evidence_is_accepted_without_mutation(self):
        for platform in ("win32", "linux", "darwin"):
            with self.subTest(platform=platform):
                collection, rows, phases = self.evidence(platform)
                before_collection, before_phases = deepcopy(collection), deepcopy(phases)
                before_rows = [ET.tostring(row) for row in rows]
                self.assertIsNone(suite.validate_complete_results(collection, rows, phases, platform=platform))
                self.assertEqual(collection, before_collection)
                self.assertEqual(phases, before_phases)
                self.assertEqual([ET.tostring(row) for row in rows], before_rows)

    def test_default_platform_is_checked_at_call_time(self):
        for platform in ("win32", "linux", "darwin"):
            with self.subTest(platform=platform), patch.object(suite.sys, "platform", platform):
                self.assertIsNone(suite.validate_complete_results(*self.evidence(platform)))

    def test_wrong_platform_or_nonexact_collection_schema_is_refused(self):
        original, rows, phases = self.evidence("linux")
        candidates = [None, [], {**original, "schema": "imagejxx.complete-collection.002"},
                      {**original, "platform": "win32"}, {**original, "unexpected": True}]
        candidates.extend({key: value for key, value in original.items() if key != missing}
                          for missing in original)
        for collection in candidates:
            with self.subTest(collection=collection), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, rows, phases, platform="linux")

    def test_selected_ids_cannot_be_replaced_reordered_duplicated_or_omitted(self):
        original, rows, phases = self.evidence("linux")
        selected = original["selected"]
        variants = [["tests/python/test_example.py::test_different", *selected[1:]],
                    [selected[1], selected[0], *selected[2:]],
                    [selected[0], selected[0], *selected[2:]], selected[:-1]]
        for replacement in variants:
            with self.subTest(selected=replacement), self.assertRaises(ValueError):
                suite.validate_complete_results({**original, "selected": replacement}, rows, phases, platform="linux")

    def test_exact_platform_exclusions_cannot_be_changed_or_expanded(self):
        original, rows, phases = self.evidence("linux")
        excluded = original["excluded"]
        variants = [[], excluded[:-1], [*excluded, {"id": self.function, "reason": WINDOWS_TIFF_REASON}],
                    [{"id": "tests/python/test_other.py::test_windows", "reason": WINDOWS_TIFF_REASON}, *excluded[1:]],
                    [{"id": excluded[0]["id"], "reason": "optional on this host"}, *excluded[1:]],
                    [{**excluded[0], "optional": True}, *excluded[1:]]]
        for replacement in variants:
            with self.subTest(excluded=replacement), self.assertRaises(ValueError):
                suite.validate_complete_results({**original, "excluded": replacement}, rows, phases, platform="linux")
        windows, windows_rows, windows_phases = self.evidence("win32")
        windows["excluded"] = excluded
        with self.assertRaises(ValueError):
            suite.validate_complete_results(windows, windows_rows, windows_phases, platform="win32")

    def test_incomplete_or_duplicate_collection_is_refused_before_terminal_credit(self):
        original, rows, phases = self.evidence("linux")
        variants = [[], self.ids + [self.function], self.ids + [""],
                    [name for name in self.ids if name != WINDOWS_TIFF_IDS[0]]]
        for collected in variants:
            with self.subTest(collected=collected), self.assertRaises(ValueError):
                suite.validate_complete_results({**original, "collected": collected}, rows, phases, platform="linux")

    def test_same_count_wrong_junit_identity_does_not_count_as_completion(self):
        collection, original, phases = self.evidence("win32")
        variants = (("name", "test_same_count_substitute"), ("classname", "tests.python.test_other"),
                    ("name", None), ("classname", None))
        for key, value in variants:
            rows = deepcopy(original)
            if value is None:
                del rows[0].attrib[key]
            else:
                rows[0].set(key, value)
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, rows, phases, platform="win32")

    def test_junit_rows_must_be_complete_unique_and_in_selected_order(self):
        collection, rows, phases = self.evidence("linux")
        variants = [rows[:-1], rows + [deepcopy(rows[-1])],
                    [rows[1], rows[0], *rows[2:]], [rows[0], rows[0], *rows[2:]]]
        for changed in variants:
            with self.subTest(rows=[row.attrib for row in changed]), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, changed, phases, platform="linux")

    def test_ambiguous_junit_mapping_is_refused_even_with_matching_counts(self):
        collection, rows, phases = self.evidence("win32")
        # Distinct source paths collapse to the same dotted JUnit classname.
        for nodeid in ("tests/python/test.a.py::test_value", "tests/python/test/a.py::test_value"):
            collection["collected"].append(nodeid)
            collection["selected"].append(nodeid)
            rows.append(ET.Element("testcase", classname="tests.python.test.a", name="test_value"))
            phases.extend({"id": nodeid, "phase": phase, "outcome": "passed", "duration": 0.01}
                          for phase in ("setup", "call", "teardown"))
        with self.assertRaises(ValueError):
            suite.validate_complete_results(collection, rows, phases, platform="win32")

    def test_terminal_failures_errors_and_skips_are_refused_despite_passed_phases(self):
        collection, original, phases = self.evidence("linux")
        for tag in ("failure", "error", "skipped"):
            rows = deepcopy(original)
            ET.SubElement(rows[0], tag, message="must not be credited")
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, rows, phases, platform="linux")

    def test_every_selected_test_requires_all_three_ordered_phases(self):
        collection, rows, phases = self.evidence("linux")
        variants = [phases[:-1], [*phases[:2], *phases[3:]], phases + [deepcopy(phases[-1])],
                    [phases[1], phases[0], *phases[2:]],
                    [*phases[:2], deepcopy(phases[1]), *phases[3:]],
                    [{**phases[0], "id": self.nested}, *phases[1:]]]
        for changed in variants:
            with self.subTest(phases=changed[:4], count=len(changed)), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, rows, changed, platform="linux")

    def test_phase_rows_have_exact_fields_and_list_container(self):
        collection, rows, phases = self.evidence("win32")
        variants = [None, tuple(phases), [None, *phases[1:]],
                    [{**phases[0], "extra": True}, *phases[1:]]]
        variants.extend([{key: value for key, value in phases[0].items() if key != missing}, *phases[1:]]
                        for missing in phases[0])
        for changed in variants:
            with self.subTest(first=None if changed is None else changed[0]), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, rows, changed, platform="win32")

    def test_any_nonpassed_phase_is_refused_even_if_junit_reports_pass(self):
        collection, rows, phases = self.evidence("linux")
        for index in range(3):
            for outcome in ("skipped", "failed", "xfailed", "", None):
                changed = deepcopy(phases)
                changed[index]["outcome"] = outcome
                with self.subTest(phase=changed[index]["phase"], outcome=outcome), self.assertRaises(ValueError):
                    suite.validate_complete_results(collection, rows, changed, platform="linux")

    def test_phase_duration_must_be_finite_nonnegative_number_not_boolean(self):
        collection, rows, phases = self.evidence("linux")
        for duration in (-1, -0.25, float("nan"), float("inf"), -float("inf"), True, False, "0", None):
            changed = deepcopy(phases)
            changed[1]["duration"] = duration
            with self.subTest(duration=duration), self.assertRaises(ValueError):
                suite.validate_complete_results(collection, rows, changed, platform="linux")


class CurrentProducerDiscovery(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-current-discovery-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.cache = self.root / "build/wheel/native"
        self.cache.mkdir(parents=True)
        self.cache_file = self.cache / "CMakeCache.txt"
        self.cache_file.write_text("\n".join([
            "CMAKE_GENERATOR:INTERNAL=Ninja", "CMAKE_BUILD_TYPE:STRING=Release", "BUILD_TESTING:BOOL=ON",
            "IMAGEJXX_BUILD_PYTHON:BOOL=ON", "IMAGEJXX_BUILD_IO:BOOL=ON", "IMAGEJXX_BUILD_CPP_UI:BOOL=ON",
            "IMAGEJXX_BUILD_PYTHON_UI:BOOL=ON", "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(self.root),
            "CMAKE_CACHEFILE_DIR:INTERNAL=" + str(self.cache)]) + "\n", encoding="utf-8")
        self.environment = {"SYNTHETIC": "transport"}
        self.paths = {name: str(self.root / name) for name in (
            "IMAGEJXX_RGB_COMBINED_INPUT_REPORT", "IMAGEJXX_H1_CPP_REPORT", "IMAGEJXX_R16_CPP_REPORT")}

    def test_missing_or_ambiguous_native_producer_refuses_before_loading(self):
        with patch.object(suite.sys, "platform", "linux"), patch.object(
                suite.importlib.util, "spec_from_file_location", side_effect=AssertionError("must not load")):
            with self.assertRaisesRegex(ValueError, "found 0"):
                suite.capture_current_observations(self.root / "output", self.root / "app", self.root, {})
            for directory in (self.cache, self.cache.parent / "second"):
                directory.mkdir(exist_ok=True)
                (directory / "imagejxx_rgb_combined_input_tests").write_bytes(b"synthetic")
            with self.assertRaisesRegex(ValueError, "found 2"):
                suite.capture_current_observations(self.root / "output", self.root / "app", self.root, {})

    def test_exact_platform_basename_and_original_environment_are_forwarded(self):
        for platform, suffix in (("linux", ""), ("win32", ".exe")):
            with self.subTest(platform=platform):
                executable = self.cache / ("imagejxx_rgb_combined_input_tests" + suffix)
                executable.write_bytes(b"synthetic producer, never executed")
                def captured(*args, **kwargs):
                    args[2].mkdir()
                    return {"state": "captured", "acceptance_claim": False, "reader_environment": self.paths}
                helper = SimpleNamespace(capture_current=Mock(side_effect=captured))
                spec = SimpleNamespace(loader=SimpleNamespace(exec_module=Mock()))
                with patch.object(suite.sys, "platform", platform), \
                     patch.object(suite.importlib.util, "spec_from_file_location", return_value=spec), \
                     patch.object(suite.importlib.util, "module_from_spec", return_value=helper):
                    result = suite.capture_current_observations(self.root / "output", self.root / "app", self.root, self.environment)
                self.assertEqual(result, self.paths)
                helper.capture_current.assert_called_once_with(self.root / "app", executable,
                    self.root / "output", self.environment, root=self.root)
                (self.root / "output/producer-cache.json").unlink()
                (self.root / "output").rmdir()
                executable.unlink()

    def test_disabled_foreign_or_multiconfig_cache_refuses(self):
        executable = self.cache / "imagejxx_rgb_combined_input_tests"
        executable.write_bytes(b"synthetic")
        original = self.cache_file.read_text(encoding="utf-8")
        for source, destination in (("BUILD_TESTING:BOOL=ON", "BUILD_TESTING:BOOL=OFF"),
                                    ("CMAKE_BUILD_TYPE:STRING=Release", "CMAKE_BUILD_TYPE:STRING=Debug"),
                                    ("CMAKE_GENERATOR:INTERNAL=Ninja", "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022"),
                                    (str(self.root), str(self.root / "build"))):
            with self.subTest(change=source):
                self.cache_file.write_text(original.replace(source, destination), encoding="utf-8")
                with self.assertRaises(ValueError):
                    suite.current_producer_cache(executable, self.root)
        self.cache_file.write_text(original, encoding="utf-8")
        self.cache_file.unlink()
        with self.assertRaisesRegex(ValueError, "one regular containing"):
            suite.current_producer_cache(executable, self.root)

    def test_incomplete_or_acceptance_claiming_transport_is_rejected(self):
        executable = self.cache / "imagejxx_rgb_combined_input_tests"
        executable.write_bytes(b"synthetic")
        for result in ({"state": "failed", "acceptance_claim": False, "reader_environment": self.paths},
                       {"state": "captured", "acceptance_claim": True, "reader_environment": self.paths},
                       {"state": "captured", "acceptance_claim": False, "reader_environment": {}}):
            def captured(*args, **kwargs):
                args[2].mkdir(exist_ok=True)
                (args[2] / "producer-cache.json").unlink(missing_ok=True)
                return result
            helper = SimpleNamespace(capture_current=Mock(side_effect=captured))
            spec = SimpleNamespace(loader=SimpleNamespace(exec_module=Mock()))
            with self.subTest(result=result), patch.object(suite.sys, "platform", "linux"), \
                 patch.object(suite.importlib.util, "spec_from_file_location", return_value=spec), \
                 patch.object(suite.importlib.util, "module_from_spec", return_value=helper):
                with self.assertRaisesRegex(ValueError, "exact original reader paths"):
                    suite.capture_current_observations(self.root / "output", self.root / "app", self.root, {})


if __name__ == "__main__":
    unittest.main()
