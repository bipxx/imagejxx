"""Synthetic evidence joins for the required five-ID headless cohort.

Only stdlib data and XML rows are exercised. No pytest collection, product
import, subprocess, capture, compiler or environment is executed by this file.
These controls do not count as execution of the five original test bodies.
"""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ci_headless_suite_under_test",
                                            ROOT / "scripts/ci_python_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)

# Original identities are literals here, not copied from the routing helper.
HEADLESS = (
    "tests/python/test_history_settings.py::test_history_step_policy_effects",
    "tests/python/test_history_settings.py::test_history_pixel_budget_effects",
    "tests/python/test_history_settings.py::test_history_persist_reopen_adopt",
    "tests/python/test_history_settings.py::test_history_detached_settings_no_owner_effects",
    "tests/python/test_shortcuts.py::test_A01_base_registry_no_desktop",
)
TIFF = "tests/python/test_tiff_runtime_policy.py::"
WINDOWS = (
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[none]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[missing_dll]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[changed_dll]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[changed_header]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[missing_link]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[wrong_headers]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[foreign_zlib]",
    TIFF + "test_recheck_refuses_changes_after_successful_configuration[bin/tiff.dll]",
    TIFF + "test_recheck_refuses_changes_after_successful_configuration[include/tiffio.h]",
    TIFF + "test_install_refuses_a_changed_dependency_before_copying_any_codec",
)
SHARED = tuple(TIFF + "test_reviewed_closure_rejects_restricted_or_changed_inputs[" + value + "]"
               for value in ("none", "noncommercial", "version", "missing_dependency", "runtime_identity",
                             "extra_dll", "notice", "cmake", "profile"))
REASON = "The exact reviewed delivery profile is Windows x64"
BROAD = ("tests/python/test_other.py::test_first", "tests/python/test_other.py::test_last")
COLLECTED = [BROAD[0], SHARED[0], *reversed(HEADLESS), *WINDOWS, *SHARED[1:], BROAD[1]]
PLATFORMS = ("win32", "linux", "darwin")


def rows(ids):
    result = []
    for nodeid in ids:
        module, name = nodeid.split("::", 1)
        result.append(ET.Element("testcase", classname=module[:-3].replace("/", "."), name=name))
    return result


def phases(ids):
    return [{"id": nodeid, "phase": phase, "outcome": "passed", "duration": duration}
            for nodeid in ids for phase, duration in (("setup", 0), ("call", 0.125), ("teardown", 1))]


def singleton(nodeid, platform):
    collection = {"schema": "imagejxx.headless-collection.001", "platform": platform,
                  "collected": [nodeid], "selected": [nodeid], "excluded": []}
    return collection, rows([nodeid]), phases([nodeid])


def owned_process():
    # A literal protocol row, not a claim that a process actually ran.
    return {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
            "cleanup_complete": True, "target_completed": True, "target_pid": 123,
            "cleanup": {"actions": [], "remaining": []}}


def routed_evidence(platform):
    excluded = [] if platform == "win32" else [{"id": name, "reason": REASON} for name in WINDOWS]
    omitted = {row["id"] for row in excluded}
    selected = [name for name in COLLECTED if name not in HEADLESS and name not in omitted]
    collection = {"schema": "imagejxx.complete-collection.002", "platform": platform,
                  "collected": list(COLLECTED), "selected": selected, "excluded": excluded,
                  "routed": list(reversed(HEADLESS))}
    reports = []
    # Receipt order is the frozen five-ID order, independently of collection order.
    for nodeid in HEADLESS:
        child, terminal, events = singleton(nodeid, platform)
        reports.append({"id": nodeid, "collection": child, "junit_rows": terminal,
                        "phases": events, "process": owned_process()})
    return collection, rows(selected), phases(selected), reports


class HeadlessEvidence(unittest.TestCase):
    def test_required_original_ids_are_exact_and_ordered(self):
        self.assertIsInstance(suite.HEADLESS_TEST_IDS, tuple)
        self.assertEqual(suite.HEADLESS_TEST_IDS, HEADLESS)

    def test_every_original_singleton_passes_on_each_platform_without_mutation(self):
        for platform in PLATFORMS:
            for nodeid in HEADLESS:
                with self.subTest(platform=platform, nodeid=nodeid):
                    collection, terminal, events = singleton(nodeid, platform)
                    before = deepcopy((collection, events)), [ET.tostring(row) for row in terminal]
                    self.assertIsNone(suite.validate_headless_results(
                        collection, terminal, events, nodeid, platform=platform))
                    self.assertEqual((collection, events), before[0])
                    self.assertEqual([ET.tostring(row) for row in terminal], before[1])

    def test_default_platform_is_resolved_at_call_time(self):
        for platform in PLATFORMS:
            with self.subTest(platform=platform), patch.object(suite.sys, "platform", platform):
                self.assertIsNone(suite.validate_headless_results(*singleton(HEADLESS[0], platform), HEADLESS[0]))

    def test_unknown_or_nonoriginal_expected_id_is_refused(self):
        evidence = singleton(HEADLESS[0], "linux")
        for nodeid in (None, "", BROAD[0], HEADLESS[0] + "[invented]", 1):
            with self.subTest(nodeid=nodeid), self.assertRaises(ValueError):
                suite.validate_headless_results(*evidence, nodeid, platform="linux")

    def test_headless_collection_schema_platform_and_fields_are_exact(self):
        original, terminal, events = singleton(HEADLESS[0], "linux")
        candidates = [None, [], {**original, "schema": "imagejxx.complete-collection.001"},
                      {**original, "platform": "win32"}, {**original, "routed": []}]
        candidates.extend({key: value for key, value in original.items() if key != missing}
                          for missing in original)
        for collection in candidates:
            with self.subTest(collection=collection), self.assertRaises(ValueError):
                suite.validate_headless_results(collection, terminal, events, HEADLESS[0], platform="linux")

    def test_singleton_collection_cannot_hide_other_tests_or_exclusions(self):
        original, terminal, events = singleton(HEADLESS[0], "linux")
        variants = [{"collected": []}, {"selected": []},
                    {"collected": [HEADLESS[0], HEADLESS[1]]},
                    {"selected": [HEADLESS[0], HEADLESS[0]]},
                    {"collected": [HEADLESS[1]], "selected": [HEADLESS[1]]},
                    {"excluded": [{"id": HEADLESS[1], "reason": "not executed"}]}]
        for changes in variants:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                suite.validate_headless_results({**original, **changes}, terminal, events,
                                                HEADLESS[0], platform="linux")

    def test_singleton_terminal_identity_and_outcome_cannot_be_substituted(self):
        collection, terminal, events = singleton(HEADLESS[0], "linux")
        variants = [[], terminal + deepcopy(terminal), rows([HEADLESS[1]]), rows([BROAD[0]])]
        for tag in ("failure", "error", "skipped"):
            altered = deepcopy(terminal)
            ET.SubElement(altered[0], tag, message="synthetic unaccepted outcome")
            variants.append(altered)
        for changed in variants:
            with self.subTest(rows=[ET.tostring(row) for row in changed]), self.assertRaises(ValueError):
                suite.validate_headless_results(collection, changed, events, HEADLESS[0], platform="linux")

    def test_singleton_requires_exact_ordered_passed_phases_and_valid_durations(self):
        collection, terminal, original = singleton(HEADLESS[0], "linux")
        variants = [None, original[:-1], original + [original[2]], [original[1], original[0], original[2]],
                    phases([HEADLESS[1]]), [{**original[0], "extra": 1}, *original[1:]]]
        for index in range(3):
            for outcome in ("failed", "skipped", "xfailed"):
                changed = deepcopy(original)
                changed[index]["outcome"] = outcome
                variants.append(changed)
        for duration in (-1, float("inf"), float("nan"), True, "0", None):
            variants.append([original[0], {**original[1], "duration": duration}, original[2]])
        for changed in variants:
            with self.subTest(events=changed), self.assertRaises(ValueError):
                suite.validate_headless_results(collection, terminal, changed, HEADLESS[0], platform="linux")


class RoutedCompleteEvidence(unittest.TestCase):
    def test_selection_preserves_broad_order_and_routes_five_without_new_exclusions(self):
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                collection, _, _, _ = routed_evidence(platform)
                original = list(COLLECTED)
                selected, excluded, routed = suite.routed_test_selection(COLLECTED, platform=platform)
                self.assertEqual(selected, collection["selected"])
                self.assertEqual(excluded, collection["excluded"])
                self.assertEqual(routed, list(reversed(HEADLESS)))
                self.assertTrue(set(SHARED) <= set(selected))
                self.assertFalse(set(HEADLESS) & {row["id"] for row in excluded})
                self.assertEqual(COLLECTED, original)

    def test_all_five_and_all_original_windows_policy_ids_remain_required(self):
        candidates = [[], COLLECTED + [HEADLESS[0]], COLLECTED + [""], COLLECTED + [None]]
        candidates.extend([name for name in COLLECTED if name != missing]
                          for missing in (*HEADLESS, *WINDOWS))
        for collection in candidates:
            with self.subTest(collection=collection), self.assertRaises(ValueError):
                suite.routed_test_selection(collection, platform="linux")

    def test_each_platform_joins_independent_singleton_and_broad_evidence(self):
        for platform in PLATFORMS:
            with self.subTest(platform=platform):
                evidence = routed_evidence(platform)
                before = deepcopy(evidence)
                self.assertIsNone(suite.validate_routed_results(*evidence, platform=platform))
                self.assertEqual(evidence[0], before[0])
                self.assertEqual(evidence[2], before[2])
                self.assertEqual([ET.tostring(row) for row in evidence[1]],
                                 [ET.tostring(row) for row in before[1]])
                for actual, original in zip(evidence[3], before[3]):
                    self.assertEqual({key: value for key, value in actual.items() if key != "junit_rows"},
                                     {key: value for key, value in original.items() if key != "junit_rows"})
                    self.assertEqual([ET.tostring(row) for row in actual["junit_rows"]],
                                     [ET.tostring(row) for row in original["junit_rows"]])

    def test_default_platform_applies_to_selection_and_complete_join(self):
        for platform in PLATFORMS:
            with self.subTest(platform=platform), patch.object(suite.sys, "platform", platform):
                collection, terminal, events, reports = routed_evidence(platform)
                self.assertEqual(suite.routed_test_selection(COLLECTED),
                                 (collection["selected"], collection["excluded"], collection["routed"]))
                self.assertIsNone(suite.validate_routed_results(collection, terminal, events, reports))

    def test_complete_v2_schema_and_derived_routing_cannot_be_edited(self):
        original, terminal, events, reports = routed_evidence("linux")
        candidates = [None, [], {**original, "schema": "imagejxx.complete-collection.001"},
                      {**original, "platform": "win32"}, {**original, "extra": True},
                      {**original, "routed": list(HEADLESS)}, {**original, "routed": []},
                      {**original, "selected": original["selected"] + [HEADLESS[0]]},
                      {**original, "selected": list(reversed(original["selected"]))},
                      {**original, "excluded": original["excluded"] + [{"id": HEADLESS[0], "reason": "headless"}]}]
        candidates.extend({key: value for key, value in original.items() if key != missing}
                          for missing in original)
        for collection in candidates:
            with self.subTest(collection=collection), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, events, reports, platform="linux")

    def test_exact_five_report_fields_membership_and_constant_order_are_required(self):
        collection, terminal, events, original = routed_evidence("linux")
        variants = [None, {}, original[:-1], original + [original[-1]],
                    [original[1], original[0], *original[2:]],
                    [original[0], original[0], *original[2:]],
                    [{**original[0], "id": BROAD[0]}, *original[1:]],
                    [{**original[0], "extra": True}, *original[1:]]]
        variants.extend([{key: value for key, value in original[0].items() if key != missing},
                         *original[1:]] for missing in original[0])
        for reports in variants:
            with self.subTest(reports=reports), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, events, reports, platform="linux")

    def test_cross_child_collection_junit_or_phase_substitution_is_refused(self):
        collection, terminal, events, original = routed_evidence("linux")
        for field in ("collection", "junit_rows", "phases"):
            reports = deepcopy(original)
            reports[0][field] = deepcopy(reports[1][field])
            with self.subTest(field=field), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, events, reports, platform="linux")

    def test_each_child_requires_an_exited_zero_status_fully_drained_owned_process(self):
        collection, terminal, events, original = routed_evidence("linux")
        changes = (("schema", "different"), ("state", "running"), ("state", "timed_out"),
                   ("exit_code", 1), ("exit_code", None), ("exit_code", False),
                   ("cleanup_complete", False), ("cleanup_complete", 1),
                   ("target_completed", False), ("target_completed", 1))
        for field, value in changes:
            reports = deepcopy(original)
            reports[2]["process"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, events, reports, platform="linux")
        for field in ("schema", "state", "exit_code", "cleanup_complete", "target_completed"):
            reports = deepcopy(original)
            del reports[3]["process"][field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, events, reports, platform="linux")
        for invalid in (None, [], "exited"):
            reports = deepcopy(original)
            reports[4]["process"] = invalid
            with self.subTest(process=invalid), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, events, reports, platform="linux")

    def test_broad_junit_and_phases_still_require_every_remaining_original_test(self):
        collection, terminal, events, reports = routed_evidence("linux")
        row_variants = [terminal[:-1], terminal + rows([HEADLESS[0]]),
                        [terminal[1], terminal[0], *terminal[2:]],
                        rows(["tests/python/test_other.py::test_substitute", *collection["selected"][1:]])]
        for tag in ("failure", "error", "skipped"):
            changed = deepcopy(terminal)
            ET.SubElement(changed[0], tag)
            row_variants.append(changed)
        for changed in row_variants:
            with self.subTest(rows=[ET.tostring(row) for row in changed]), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, changed, events, reports, platform="linux")
        phase_variants = [events[:-1], events + phases([HEADLESS[0]]),
                          [{**events[0], "id": HEADLESS[0]}, *events[1:]],
                          [{**events[0], "outcome": "skipped"}, *events[1:]],
                          [events[1], events[0], *events[2:]]]
        for changed in phase_variants:
            with self.subTest(events=changed[:3]), self.assertRaises(ValueError):
                suite.validate_routed_results(collection, terminal, changed, reports, platform="linux")

    def test_existing_complete_v1_contract_keeps_its_original_unrouted_meaning(self):
        for platform in PLATFORMS:
            selected = [name for name in COLLECTED if platform == "win32" or name not in WINDOWS]
            collection = {"schema": "imagejxx.complete-collection.001", "platform": platform,
                          "collected": list(COLLECTED), "selected": selected,
                          "excluded": [] if platform == "win32" else [
                              {"id": name, "reason": REASON} for name in WINDOWS]}
            with self.subTest(platform=platform):
                self.assertTrue(set(HEADLESS) <= set(selected))
                self.assertIsNone(suite.validate_complete_results(collection, rows(selected), phases(selected),
                                                                 platform=platform))


class HeadlessOrchestration(unittest.TestCase):
    """Real tiny evidence files from a mocked launcher, never actual children."""
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-headless-transport-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.root = self.directory / "repository"
        self.root.mkdir()
        self.output = self.directory / "headless"
        self.environment = {"CONTROL": "explicit synthetic child environment"}
        self.clock = 100.0
        self.advance = []
        self.events = []
        self.calls = []
        self.proof_changes = {}
        self.fail_owner = False
        self.after_emit = None
        guard = patch.object(suite.subprocess, "Popen", side_effect=AssertionError("No real child in transport controls"))
        guard.start(); self.addCleanup(guard.stop)

    @staticmethod
    def write_json(path, value):
        path.write_text(json.dumps(value) + "\n", encoding="utf-8")

    def callback(self, path):
        self.assertIsInstance(path, Path)
        self.assertTrue(path.parent.is_dir())
        self.assertFalse(path.exists())
        self.events.append(("callback", path))

    def launch(self, argv, **options):
        directory = options["output"]
        expected_id = argv[argv.index("--headless-test-id") + 1]
        self.assertEqual(self.events[-1], ("callback", directory / "process.json"))
        self.assertEqual(list(directory.iterdir()), [], "Each child must receive fresh owned scratch")
        self.events.append(("launch", directory / "process.json"))
        self.calls.append((list(argv), dict(options)))
        proof = {**owned_process(), **self.proof_changes}
        self.write_json(directory / "process.json", proof)
        if self.fail_owner:
            failure = RuntimeError("synthetic unresolved owner failure")
            failure.receipt = proof
            raise failure
        if proof["exit_code"] != 0:
            return proof
        collection, terminal, event_rows = singleton(expected_id, "linux")
        child = {"id": expected_id, "state": "passed", "acceptance_claim": False,
                 "scope": "SYNTHETIC transport only; original test body was not executed"}
        self.write_json(directory / "headless-receipt.json", child)
        self.write_json(directory / "complete-collection.json", collection)
        document = ET.Element("testsuites")
        group = ET.SubElement(document, "testsuite", name="synthetic", tests="1")
        group.extend(terminal)
        ET.ElementTree(document).write(directory / "pytest.xml", encoding="utf-8", xml_declaration=True)
        (directory / "test-phases.jsonl").write_text(
            "".join(json.dumps(row) + "\n" for row in event_rows), encoding="utf-8")
        if self.after_emit is not None:
            self.after_emit(directory, expected_id)
        index = len(self.calls) - 1
        if index < len(self.advance):
            self.clock += self.advance[index]
        return proof

    def invoke(self, *, deadline=300.0, on_start=None, environment=None, dependency_site=None):
        with patch.object(suite.sys, "platform", "linux"):
            with patch.object(suite.time, "monotonic", side_effect=lambda: self.clock):
                with patch.object(suite, "complete_command", side_effect=self.launch):
                    return suite.run_headless_tests(self.output, self.root,
                        self.environment if environment is None else environment,
                        deadline=deadline, on_start=self.callback if on_start is None else on_start,
                        dependency_site=dependency_site)

    def receipt(self):
        return json.loads((self.output / "receipt.json").read_bytes())

    def test_five_serial_fresh_children_use_exact_ids_cli_and_callback_before_launch(self):
        environment_before = dict(self.environment)
        reports = self.invoke()
        self.assertEqual([row["id"] for row in reports], list(HEADLESS))
        self.assertEqual(len(self.calls), 5)
        expected_events = []
        for index, ((argv, options), expected_id) in enumerate(zip(self.calls, HEADLESS), 1):
            directory = self.output / f"case-{index:02d}"
            self.assertEqual(argv, [sys.executable, "-I", "-X", "utf8", "-B", str(Path(suite.__file__).resolve()),
                "--headless-pytest-child", str(directory), "--repository", str(self.root),
                "--headless-test-id", expected_id])
            self.assertEqual(options, {"output": directory, "root": self.root,
                                      "environment": self.environment, "timeout": 30})
            expected_events.extend((("callback", directory / "process.json"),
                                    ("launch", directory / "process.json")))
            report = reports[index - 1]
            self.assertEqual(set(report), {"id", "collection", "junit_rows", "phases", "process"})
            self.assertEqual(report["collection"]["collected"], [expected_id])
            self.assertEqual(report["phases"], phases([expected_id]))
            self.assertEqual(report["process"], owned_process())
        self.assertEqual(self.events, expected_events)
        self.assertEqual(self.environment, environment_before)
        receipt = self.receipt()
        self.assertEqual(receipt["schema"], "imagejxx.headless-suite.001")
        self.assertEqual(receipt["state"], "passed")
        self.assertIs(receipt["acceptance_claim"], False)
        self.assertEqual(receipt["ids"], list(HEADLESS))
        self.assertEqual([row["id"] for row in receipt["cases"]], list(HEADLESS))
        self.assertTrue(all(row["state"] == "passed" for row in receipt["cases"]))

    def test_explicit_absolute_deadline_is_shared_and_remaining_time_caps_each_child(self):
        self.advance = [20.0, 20.0, 5.0]
        with self.assertRaisesRegex(TimeoutError, "deadline exhausted"):
            self.invoke(deadline=145.0)
        self.assertEqual([options["timeout"] for _, options in self.calls], [30, 25.0, 5.0])
        self.assertEqual([argv[-1] for argv, _ in self.calls], list(HEADLESS[:3]))
        self.assertFalse((self.output / "case-04").exists())
        self.assertFalse((self.output / "case-05").exists())
        receipt = self.receipt()
        self.assertEqual(receipt["state"], "failed")
        self.assertIs(receipt["acceptance_claim"], False)
        self.assertEqual([row["state"] for row in receipt["cases"]], ["passed"] * 3)

    def test_default_deadline_is_anchored_once_not_reset_for_each_case(self):
        # Anchor at100 +2400, independently of the number of clock reads.
        # Registration consumes parent time; the two synthetic launches then
        # consume15 and10. No process waits and the original25/10 oracle stays.
        shared_deadline = 2500.0
        self.advance = [15.0, 10.0]
        def admitted(path):
            self.callback(path)
            if not self.calls:
                self.clock = shared_deadline - 25.0
        with self.assertRaisesRegex(TimeoutError, "deadline exhausted"):
            self.invoke(deadline=None, on_start=admitted)
        self.assertEqual([options["timeout"] for _, options in self.calls], [25.0, 10.0])
        self.assertEqual(self.clock, shared_deadline)
        self.assertFalse((self.output / "case-03").exists())
        self.assertEqual(self.receipt()["state"], "failed")

    def test_admission_consuming_deadline_never_launches_the_registered_case(self):
        def admitted(path):
            self.callback(path)
            self.clock = 145.0
        with self.assertRaisesRegex(TimeoutError, "during isolated admission"):
            self.invoke(deadline=145.0, on_start=admitted)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.events, [("callback", self.output / "case-01/process.json")])
        self.assertFalse((self.output / "case-01/process.json").exists())
        self.assertFalse((self.output / "case-02").exists())
        receipt = self.receipt()
        self.assertEqual(receipt["state"], "failed")
        self.assertIs(receipt["acceptance_claim"], False)
        self.assertEqual([row["state"] for row in receipt["cases"]], ["not_launched_deadline"])

    def test_unlaunched_deadline_invokes_registration_cancel_exactly_once(self):
        active, canceled = set(), []
        def admitted(path):
            self.callback(path)
            active.add(path)
            self.clock = 145.0
            def cancel():
                active.remove(path)
                canceled.append(path)
            return cancel
        with self.assertRaisesRegex(TimeoutError, "during isolated admission"):
            self.invoke(deadline=145.0, on_start=admitted)
        process = self.output / "case-01/process.json"
        self.assertEqual(active, set())
        self.assertEqual(canceled, [process])
        self.assertEqual(self.calls, [])
        self.assertEqual(self.events, [("callback", process)])
        self.assertFalse(process.exists())
        self.assertFalse((self.output / "case-02").exists())
        receipt = self.receipt()
        self.assertEqual(receipt["state"], "failed")
        self.assertIs(receipt["acceptance_claim"], False)
        self.assertEqual([row["state"] for row in receipt["cases"]], ["not_launched_deadline"])

    def test_explicit_existing_dependency_site_is_only_a_headless_child_argument(self):
        dependency = self.directory / "synthetic dependency locator"
        dependency.mkdir()
        reports = self.invoke(dependency_site=dependency)
        self.assertEqual([row["id"] for row in reports], list(HEADLESS))
        for (argv, _), expected_id in zip(self.calls, HEADLESS):
            self.assertEqual(argv[-4:], ["--headless-test-id", expected_id,
                                        "--headless-dependency-site", str(dependency)])
            self.assertEqual(argv.count("--headless-dependency-site"), 1)
        self.assertEqual(list(dependency.iterdir()), [], "Locator is not an environment or cache")

    def test_expired_deadline_prevents_callback_and_first_child(self):
        with self.assertRaisesRegex(TimeoutError, "deadline exhausted"):
            self.invoke(deadline=100.0)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.events, [])
        self.assertEqual(self.receipt()["cases"], [])
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertFalse((self.output / "case-01").exists())

    def test_nonfinite_boolean_or_nonnumeric_deadline_never_launches(self):
        for index, deadline in enumerate((float("nan"), float("inf"), -float("inf"), True, "300")):
            with self.subTest(deadline=deadline):
                self.output = self.directory / f"invalid-deadline-{index}"
                with self.assertRaisesRegex(ValueError, "finite absolute"):
                    self.invoke(deadline=deadline)
                self.assertEqual(self.calls, [])
                self.assertEqual(self.events, [])
                self.assertFalse((self.output / "case-01").exists())

    def test_existing_output_is_not_reused_or_modified(self):
        self.output.mkdir()
        marker = self.output / "preserved.json"
        marker.write_bytes(b"original retained bytes")
        with self.assertRaisesRegex(ValueError, "fresh headless"):
            self.invoke()
        self.assertEqual(self.calls, [])
        self.assertEqual(self.events, [])
        self.assertEqual(list(self.output.iterdir()), [marker])
        self.assertEqual(marker.read_bytes(), b"original retained bytes")

    def test_optional_observation_environment_is_refused_even_when_empty(self):
        for value in ("", "explicit-observations"):
            environment = {**self.environment, "IMAGEJXX_SHORTCUTS_PYTHON_OBSERVATIONS": value}
            before = dict(environment)
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "Optional shortcut"):
                self.invoke(environment=environment)
            self.assertEqual(environment, before)
            self.assertEqual(self.calls, [])
            self.assertEqual(self.events, [])
            self.assertFalse(self.output.exists())

    def test_callback_failure_records_pending_case_but_never_launches_it(self):
        def refused(path):
            self.callback(path)
            raise RuntimeError("synthetic outer ownership registration refused")
        with self.assertRaisesRegex(RuntimeError, "registration refused"):
            self.invoke(on_start=refused)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.events, [("callback", self.output / "case-01/process.json")])
        self.assertFalse((self.output / "case-01/process.json").exists())
        self.assertFalse((self.output / "case-02").exists())
        receipt = self.receipt()
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual([row["state"] for row in receipt["cases"]], ["failed"])
        self.assertIs(receipt["acceptance_claim"], False)

    def test_unresolved_owned_exception_preserves_proof_and_stops_remaining_cases(self):
        self.proof_changes = {"state": "ownership_failed", "cleanup_complete": False, "exit_code": None}
        self.fail_owner = True
        with self.assertRaisesRegex(RuntimeError, "unresolved owner"):
            self.invoke()
        self.assertEqual(len(self.calls), 1)
        process = json.loads((self.output / "case-01/process.json").read_bytes())
        self.assertIs(process["cleanup_complete"], False)
        self.assertEqual(process["state"], "ownership_failed")
        self.assertFalse((self.output / "case-01/headless-receipt.json").exists())
        self.assertFalse((self.output / "case-02").exists())
        self.assertEqual(self.receipt()["state"], "failed")
        self.assertIs(self.receipt()["acceptance_claim"], False)

    def test_zero_status_with_unresolved_saved_process_has_no_qualification(self):
        self.proof_changes = {"cleanup_complete": False}
        with self.assertRaisesRegex(ValueError, "resolved owned exit"):
            self.invoke()
        self.assertEqual(len(self.calls), 1)
        self.assertFalse((self.output / "case-02").exists())
        receipt = self.receipt()
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual([row["state"] for row in receipt["cases"]], ["failed"])
        self.assertIs(receipt["acceptance_claim"], False)

    def test_nonzero_status_is_preserved_without_reading_absent_child_results(self):
        self.proof_changes = {"exit_code": 7}
        with self.assertRaises(subprocess.CalledProcessError) as refused:
            self.invoke()
        self.assertEqual(refused.exception.returncode, 7)
        self.assertEqual(len(self.calls), 1)
        self.assertFalse((self.output / "case-01/headless-receipt.json").exists())
        self.assertFalse((self.output / "case-02").exists())
        self.assertEqual(self.receipt()["state"], "failed")

    def test_same_count_wrong_child_identity_stops_before_next_launch(self):
        def replace(directory, expected_id):
            child = json.loads((directory / "headless-receipt.json").read_bytes())
            child["id"] = HEADLESS[1]
            self.write_json(directory / "headless-receipt.json", child)
        self.after_emit = replace
        with self.assertRaisesRegex(ValueError, "original required test"):
            self.invoke()
        self.assertEqual(len(self.calls), 1)
        self.assertFalse((self.output / "case-02").exists())
        self.assertEqual(self.receipt()["state"], "failed")


class HeadlessDeliveryCLI(unittest.TestCase):
    def test_local_delivery_options_cannot_change_complete_or_focused_profiles(self):
        import subprocess
        script = ROOT / "scripts/ci_python_suite.py"
        for option in ("--headless-install-manifest", "--headless-dependency-site"):
            for mode in ("--complete-pytest-child", "--focused-pytest-child"):
                with self.subTest(option=option, mode=mode):
                    result = subprocess.run([sys.executable, "-I", "-B", str(script), mode, "unused",
                                             "--repository", str(ROOT), option, "unused"],
                                            text=True, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("only for the isolated headless suite", result.stderr)


if __name__ == "__main__":
    unittest.main()
