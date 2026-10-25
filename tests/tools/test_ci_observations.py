"""Capture transport contracts; subprocesses are deliberately mocked, not UI proofs."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ci_observations_under_test", ROOT / "scripts/ci_observations.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class TiffCaptureInputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.folder = self.root / "tests/fixtures/legacy"
        self.folder.mkdir(parents=True)
        self.payload = b"independent fixture bytes"
        self.fixture = self.folder / "input.tif"
        self.fixture.write_bytes(self.payload)
        self.row = {"file": "input.tif", "sha256": hashlib.sha256(self.payload).hexdigest()}
        self.output = self.root / "observations"

    def manifest(self):
        (self.folder / "SOURCE.json").write_text(json.dumps({"files": [self.row]}), encoding="utf-8")

    @staticmethod
    def observer(argv, **kwargs):
        actual = Path(argv[-2])
        actual.mkdir()
        (actual / "case.json").write_text(json.dumps({"state": "observed", "acceptance_claim": False}), encoding="utf-8")
        return subprocess.CompletedProcess(argv, 0)

    def invoke(self, group="deflate-tiff", arguments=None):
        self.manifest()
        arguments = arguments or ["--scientific-tiff-ui-case", "legacy/input.tif"]
        with patch.object(capture, "cases", return_value=iter([(group, "case", arguments)])):
            return capture.capture(self.output, sys.executable, self.root)

    def test_sha_only_manifest_records_size_and_preserves_nested_report(self):
        with patch.object(capture.subprocess, "run", side_effect=self.observer) as process:
            environment = self.invoke()
        self.assertEqual(process.call_count, 1)
        report = self.output / "reports/deflate-tiff/case/capture001/case.json"
        actual = self.output / "private/deflate-tiff/case/capture001/case.json"
        self.assertEqual(report.read_bytes(), actual.read_bytes())
        self.assertEqual(environment["IMAGEJXX_DEFLATE_TIFF_CPP_DIR"], str(self.output / "reports/deflate-tiff"))
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["cases"][0]["inputs"][0]["bytes"], len(self.payload))

    def test_same_size_corruption_is_refused_before_launch(self):
        self.fixture.write_bytes(b"X" + self.payload[1:])
        with patch.object(capture.subprocess, "run") as process:
            with self.assertRaisesRegex(ValueError, "TIFF fixture changed"):
                self.invoke()
        process.assert_not_called()
        self.assertEqual(json.loads((self.output / "receipt.json").read_bytes())["state"], "failed")

    def test_declared_size_remains_enforced_with_a_matching_hash(self):
        self.row["bytes"] = len(self.payload) + 1
        with patch.object(capture.subprocess, "run") as process:
            with self.assertRaisesRegex(ValueError, "TIFF fixture changed"):
                self.invoke()
        process.assert_not_called()

    def test_float_preset_precedes_output_and_fixture_without_nested_reader_directory(self):
        with patch.object(capture.subprocess, "run", side_effect=self.observer) as process:
            self.invoke("float32-tiff", ["--float32-tiff-ui-case", "TF02", "legacy/input.tif"])
        argv = process.call_args.args[0]
        self.assertEqual(argv[1:3], ["--float32-tiff-ui-case", "TF02"])
        self.assertEqual(Path(argv[3]), self.output / "private/float32-tiff/case/capture")
        self.assertEqual(Path(argv[4]), self.fixture)
        self.assertTrue((self.output / "reports/float32-tiff/case/case.json").is_file())


class TiledLayoutsCaptureInputs(unittest.TestCase):
    """Exact fixture membership and mocked transport only, never UI evidence."""
    FILENAMES = (
        "u8-edges-le-finite-poison.tif", "u8-edges-be-finite-poison.tif",
        "rgb8-edges-le-finite-poison.tif", "rgb8-edges-be-finite-poison.tif",
        "f32-edges-le-finite-poison.tif", "f32-edges-be-finite-poison.tif",
    )

    @classmethod
    def expected_cases(cls):
        return [("tiled-layouts", "cpp-" + Path(name).stem + "001",
                 ["--scientific-tiff-ui-case", "scientific-tiff-tiled-layouts/" + name])
                for name in cls.FILENAMES]

    def test_exact_six_variants_match_the_existing_reader_paths(self):
        actual = [row for row in capture.tiff_cases() if row[0] == "tiled-layouts"]
        self.assertEqual(actual, self.expected_cases())
        self.assertEqual(capture.TIFF_READERS["tiled-layouts"],
                         ("IMAGEJXX_TILED_LAYOUTS_CPP_DIR", True))

    def test_all_six_keep_input_paths_and_lossless_nested_transport(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve() / "observations"
            raw = b'{"state":"observed","acceptance_claim":false}\n'
            artifact = b"synthetic retained TIFF output\x00\xff"
            expected = self.expected_cases()

            def observer(argv, **kwargs):
                actual = Path(argv[-2]); actual.mkdir()
                (actual / "case.json").write_bytes(raw)
                (actual / "edited.tif").write_bytes(artifact)
                return subprocess.CompletedProcess(argv, 0)

            with patch.object(capture, "cases", return_value=iter(expected)):
                with patch.object(capture.subprocess, "run", side_effect=observer) as process:
                    environment = capture.capture(output, sys.executable, ROOT)
            self.assertEqual(process.call_count, 6)
            self.assertEqual(environment["IMAGEJXX_TILED_LAYOUTS_CPP_DIR"],
                             str(output / "reports/tiled-layouts"))
            receipt = json.loads((output / "receipt.json").read_bytes())
            self.assertEqual([row["case"] for row in receipt["cases"]],
                             [case for _, case, _ in expected])
            for call, (_, case, arguments) in zip(process.call_args_list, expected):
                actual = output / "private/tiled-layouts" / case / "capture001"
                copied = output / "reports/tiled-layouts" / case / "capture001"
                fixture = ROOT / "tests/fixtures" / arguments[1]
                self.assertEqual(call.args[0][1:],
                                 ["--scientific-tiff-ui-case", str(actual), str(fixture)])
                for folder in (actual, copied):
                    self.assertEqual((folder / "case.json").read_bytes(), raw)
                    self.assertEqual((folder / "edited.tif").read_bytes(), artifact)
            self.assertFalse(receipt["acceptance_claim"])


class LegacyEditorCaptureInputs(unittest.TestCase):
    """Transport/envelope checks only; these mocks supply no UI evidence."""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.output = self.root / "observations"

    def invoke(self, group, report):
        flag, contract = capture.EDITOR_READERS[group]
        def observer(argv, **kwargs):
            actual = Path(argv[-1]); actual.mkdir()
            self.raw = (json.dumps(report, indent=2) + "\n").encode()
            (actual / "case.json").write_bytes(self.raw)
            return subprocess.CompletedProcess(argv, 0)
        with patch.object(capture, "cases", return_value=iter([(group, "legacy", [flag])])):
            with patch.object(capture.subprocess, "run", side_effect=observer):
                return capture.capture(self.output, sys.executable, self.root)

    def rgb(self):
        return dict(context="imgui_cpp", frames=[{}], controls=[{}], preview={},
                    pixels=[1], indexed_type_conversion={}, error="")

    def test_legacy_rgb_report_is_preserved_without_inventing_an_envelope(self):
        environment = self.invoke("rgb-dataset", self.rgb())
        report = Path(environment["IMAGEJXX_RGB_CPP_REPORT"])
        self.assertEqual(report.read_bytes(), self.raw)
        self.assertNotIn("state", json.loads(report.read_bytes()))
        self.assertNotIn("acceptance_claim", json.loads(report.read_bytes()))
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertFalse(receipt["acceptance_claim"])
        self.assertTrue(receipt["cases"][0]["legacy_probe_envelope"])

    def test_incomplete_legacy_rgb_report_is_refused(self):
        report = self.rgb(); del report["indexed_type_conversion"]
        with self.assertRaisesRegex(ValueError, "Legacy editor probe did not finish"):
            self.invoke("rgb-dataset", report)
        self.assertFalse((self.output / "reports/rgb-dataset/legacy").exists())
        self.assertEqual(json.loads((self.output / "receipt.json").read_bytes())["state"], "failed")

    def test_legacy_probe_cannot_claim_acceptance(self):
        report = self.rgb(); report["acceptance_claim"] = True
        with self.assertRaisesRegex(ValueError, "Legacy editor probe did not finish"):
            self.invoke("rgb-dataset", report)

    def test_scientific_process_requires_its_original_completed_contract(self):
        report = dict(context="imgui_cpp", frames=[{}], controls=[{}], before=[], recorded={},
                      state="running", contract="scientific-process-review.ui.001")
        with self.assertRaisesRegex(ValueError, "Legacy editor probe did not finish"):
            self.invoke("scientific-process", report)


class PersistentSessionCaptureInputs(unittest.TestCase):
    """Aggregate transport checks only; real input proofs use installed readers."""
    def invoke(self, root, report):
        output = root / "observations"
        def observer(argv, **kwargs):
            self.assertEqual(argv[1:3], ["--headless-session-consumer-test", "--consumer-dir"])
            actual = Path(argv[-1]); actual.mkdir()
            for row in report["cases"]:
                if isinstance(row, dict): row["directory"] = str(actual / row["case_id"])
            self.raw = (json.dumps(report, indent=2) + "\n").encode()
            (actual / "imgui-cpp.json").write_bytes(self.raw)
            (actual / "artifact.bin").write_bytes(b"preserved session artifact")
            return subprocess.CompletedProcess(argv, 0)
        with patch.object(capture, "cases", return_value=iter([
                ("headless-session", "legacy", ["--headless-session-consumer-test"])])):
            with patch.object(capture.subprocess, "run", side_effect=observer):
                return capture.capture(output, sys.executable, root)

    @staticmethod
    def report():
        return dict(schema_version=1, contract_id="p02a.headless-session.001",
            context="imgui_cpp", status="passed", acceptance_claim=False,
            cases=[dict(case_id=f"S{i:02d}", contract_id="p02a.headless-session.001",
                        context="imgui_cpp", status="passed", acceptance_claim=False,
                        # S07 deliberately fails initialization before recovery.
                        frames=[{}], lifecycle=[] if i == 7 else [{}]) for i in range(1, 9)])

    def test_complete_aggregate_keeps_raw_report_and_artifact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            environment = self.invoke(root, self.report())
            report = Path(environment["IMAGEJXX_SESSION_CPP_REPORT"])
            self.assertEqual(report.read_bytes(), self.raw)
            self.assertEqual(report, root / "observations/private/headless-session/legacy/capture/imgui-cpp.json")
            copied = root / "observations/reports/headless-session/legacy/imgui-cpp.json"
            self.assertEqual(copied.read_bytes(), self.raw)
            self.assertEqual(Path(json.loads(self.raw)["cases"][0]["directory"]).parent, report.parent)
            self.assertEqual((report.parent / "artifact.bin").read_bytes(), b"preserved session artifact")
            receipt = json.loads((root / "observations/receipt.json").read_bytes())
            self.assertEqual(receipt["state"], "observed")
            self.assertTrue(receipt["cases"][0]["legacy_probe_envelope"])
            self.assertFalse(receipt["acceptance_claim"])

    def test_incomplete_or_reordered_membership_is_refused(self):
        for change in ("missing", "duplicate", "reordered", "nonobject"):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve(); report = self.report()
                if change == "missing": report["cases"].pop()
                elif change == "duplicate": report["cases"][-1] = report["cases"][0]
                elif change == "reordered": report["cases"].reverse()
                else: report["cases"][-1] = None
                with self.assertRaisesRegex(ValueError, "Persistent session probe did not finish"):
                    self.invoke(root, report)
                self.assertFalse((root / "observations/reports/headless-session").exists())

    def test_unfinished_or_acceptance_claiming_member_is_refused(self):
        for field, value in (("context", "imgui_python"), ("status", "running"),
                             ("frames", []), ("lifecycle", {}), ("acceptance_claim", True)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve(); report = self.report()
                report["cases"][0][field] = value
                with self.assertRaisesRegex(ValueError, "Persistent session probe did not finish"):
                    self.invoke(root, report)


class SelectionSessionCaptureInputs(unittest.TestCase):
    """Synthetic absolute-path transport; no editor or session-format proof."""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.output = self.root / "observations"
        self.actual = self.output / "private/selection-legacy/legacy/capture"
        self.copied = self.output / "reports/selection-legacy/legacy"
        self.artifact = b"unchanged synthetic selection session artifact\x00\xff"

    def invoke(self, *, failed=False):
        def observer(argv, **kwargs):
            self.assertEqual(argv[1:], ["--selection-creation-ui-test", str(self.actual)])
            self.actual.mkdir()
            report = dict(schema_version=1, contract_id="p05l.selection-creation-ui.001",
                          context="imgui_cpp", observation_state="failed" if failed else "observed",
                          acceptance_claim=False, elapsed_seconds=0.25, cases=[])
            for number in range(1, 17):
                name = f"case{number:02d}"
                directory = self.actual / name
                directory.mkdir()
                row = dict(case_id=f"S{number:02d}", frames=[])
                if number == 15:
                    member = directory / "selection-creation.ijx"
                    member.write_bytes(self.artifact)
                    row["frames"] = [dict(label=label, requested_events=[
                        dict(type="text", value=str(member))]) for label in
                        ("session.save.path.text", "session.open.path.text")]
                raw = (json.dumps(row, indent=2) + "\n").encode()
                (directory / "case.json").write_bytes(raw)
                report["cases"].append(dict(id=row["case_id"], case_id=row["case_id"],
                    relative_path=name + "/case.json", byte_count=len(raw),
                    sha256=hashlib.sha256(raw).hexdigest()))
            report["case_count"] = len(report["cases"])
            self.raw = (json.dumps(report, indent=2) + "\n").encode()
            (self.actual / "imgui-cpp.json").write_bytes(self.raw)
            return subprocess.CompletedProcess(argv, 7 if failed else 0)
        with patch.object(capture, "cases", return_value=iter([
                ("selection-legacy", "legacy", ["--selection-creation-ui-test"])])):
            with patch.object(capture.subprocess, "run", side_effect=observer):
                return capture.capture(self.output, sys.executable, self.root)

    def test_reader_uses_original_absolute_paths_and_copy_keeps_all_bytes(self):
        environment = self.invoke()
        report = Path(environment["IMAGEJXX_SELECTION_CREATION_CPP_REPORT"])
        self.assertEqual(report, self.actual / "imgui-cpp.json")
        self.assertEqual(report.read_bytes(), self.raw)
        self.assertEqual((self.copied / "imgui-cpp.json").read_bytes(), self.raw)
        index = json.loads(report.read_bytes())
        self.assertEqual([row["case_id"] for row in index["cases"]],
                         [f"S{number:02d}" for number in range(1, 17)])
        for entry in index["cases"]:
            actual = report.parent / entry["relative_path"]
            copied = self.copied / entry["relative_path"]
            self.assertEqual(actual.read_bytes(), copied.read_bytes())
            self.assertEqual(actual.stat().st_size, entry["byte_count"])
            self.assertEqual(hashlib.sha256(actual.read_bytes()).hexdigest(), entry["sha256"])
        row = json.loads((report.parent / "case15/case.json").read_bytes())
        for frame in row["frames"]:
            typed = Path(frame["requested_events"][0]["value"])
            self.assertEqual(typed.resolve(strict=True), self.actual / "case15/selection-creation.ijx")
            self.assertNotEqual(typed, self.copied / "case15/selection-creation.ijx")
            self.assertEqual(typed.read_bytes(), self.artifact)
        self.assertEqual((self.copied / "case15/selection-creation.ijx").read_bytes(), self.artifact)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "observed")
        self.assertEqual(receipt["reader_environment"]["IMAGEJXX_SELECTION_CREATION_CPP_REPORT"], str(report))
        self.assertEqual(receipt["cases"][0]["actual"], str(self.actual))
        self.assertEqual(receipt["cases"][0]["transport"], str(self.copied))
        self.assertFalse(receipt["acceptance_claim"])

    def test_failed_capture_keeps_original_path_and_never_publishes_a_copy(self):
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            self.invoke(failed=True)
        self.assertEqual(failure.exception.returncode, 7)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        report = Path(receipt["reader_environment"]["IMAGEJXX_SELECTION_CREATION_CPP_REPORT"])
        self.assertEqual(report, self.actual / "imgui-cpp.json")
        self.assertEqual(report.read_bytes(), self.raw)
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual(receipt["cases"][0]["state"], "failed")
        self.assertEqual(receipt["cases"][0]["exit_code"], 7)
        self.assertEqual(receipt["cases"][0]["actual"], str(self.actual))
        self.assertEqual(receipt["cases"][0]["transport"], str(self.copied))
        self.assertFalse(self.copied.exists())


class IndexedCaptureInputs(unittest.TestCase):
    """Index transport only; raw UI behavior belongs to the installed readers."""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.output = self.root / "observations"
        self.group = "auto-contrast"
        self.flag, self.environment, self.contract, self.cases = capture.INDEXED_READERS[self.group]

    def invoke(self, cases=None, wrong_identity=False):
        def observer(argv, **kwargs):
            case = argv[-2]; actual = Path(argv[-1]); actual.mkdir()
            report = dict(contract_id=self.contract, case_id="wrong" if wrong_identity else case,
                          context="imgui_cpp", observation_state="observed", acceptance_claim=False,
                          frames=[{"requested_events": [{"type": "text", "value": str(actual / "typed.ijx")}]}])
            (actual / "case.json").write_text(json.dumps(report), encoding="utf-8")
            return subprocess.CompletedProcess(argv, 0)
        selected = [(self.group, case, [self.flag, case]) for case in (self.cases if cases is None else cases)]
        with patch.object(capture, "cases", return_value=iter(selected)):
            with patch.object(capture.subprocess, "run", side_effect=observer):
                return capture.capture(self.output, sys.executable, self.root)

    def test_complete_path_only_index_preserves_raw_bytes_and_hashes(self):
        environment = self.invoke()
        index_path = Path(environment[self.environment]); index = json.loads(index_path.read_bytes())
        self.assertEqual(index["contract_id"], self.contract)
        self.assertEqual(index["case_count"], 8); self.assertFalse(index["acceptance_claim"])
        self.assertEqual([row["case_id"] for row in index["cases"]], list(self.cases))
        for row in index["cases"]:
            self.assertEqual(set(row), {"case_id", "id", "relative_path", "byte_count", "sha256"})
            actual = self.output / "private" / self.group / row["case_id"] / "capture/case.json"
            indexed = index_path.parent / row["relative_path"]
            self.assertEqual(indexed, actual)
            copied = self.output / "reports" / self.group / row["case_id"] / "case.json"
            self.assertEqual(copied.read_bytes(), indexed.read_bytes())
            typed = json.loads(indexed.read_bytes())["frames"][0]["requested_events"][0]["value"]
            self.assertEqual(typed, str(indexed.parent / "typed.ijx"))
            self.assertEqual(row["byte_count"], copied.stat().st_size)
            self.assertEqual(row["sha256"], hashlib.sha256(copied.read_bytes()).hexdigest())
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["indices"][self.group]["sha256"], hashlib.sha256(index_path.read_bytes()).hexdigest())

    def test_incomplete_group_has_no_index_and_a_failed_receipt(self):
        with self.assertRaisesRegex(ValueError, "Incomplete indexed observation group"):
            self.invoke(self.cases[:-1])
        self.assertFalse((self.output / (self.group + "-index.json")).exists())
        self.assertEqual(json.loads((self.output / "receipt.json").read_bytes())["state"], "failed")

    def test_wrong_case_identity_is_refused_before_copy(self):
        with self.assertRaisesRegex(ValueError, "Unexpected indexed observation identity"):
            self.invoke(wrong_identity=True)
        self.assertFalse((self.output / "reports" / self.group).exists())

    def test_changed_raw_report_is_refused_before_index_publication(self):
        self.invoke()
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        items = receipt["cases"]; index = self.output / (self.group + "-index.json")
        index.unlink()  # This test owns the temporary fixture.
        report = Path(items[0]["actual"]) / "case.json"
        raw = report.read_bytes(); report.write_bytes(b" " + raw[1:])
        with self.assertRaisesRegex(ValueError, "Indexed report changed after capture"):
            capture.write_case_index(self.output, self.group, items, 0.5)
        self.assertFalse(index.exists())


class SelectionSettingsPair(unittest.TestCase):
    """Synthetic transport checks, separate from actual editor qualification."""
    def test_reopen_requires_the_actual_preceding_persist(self):
        reopen = ("settings-selection", "enlargement-reopen")
        persist = ("settings-selection", "enlargement-persist")
        for selected in ([reopen], [reopen, persist]):
            with self.subTest(selected=selected), self.assertRaisesRegex(ValueError, "preceding persist"):
                capture.select_cases(selected)

    def pair(self, mutate=False, wrong_parent=False):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        output = root/"observations"
        keys = [("settings-selection", case) for case in ("enlargement-persist", "enlargement-reopen")]
        def observe(argv, **kwargs):
            case = argv[-3]; actual, preferences = Path(argv[-2]), Path(argv[-1])
            actual.mkdir()
            if case == "enlargement-persist":
                self.assertFalse(preferences.exists())
                preferences.write_text('{"actual_ui_fixture":3}', encoding="utf-8")
            else:
                self.assertEqual(preferences.read_text(encoding="utf-8"), '{"actual_ui_fixture":3}')
                if mutate:
                    preferences.write_text('{"actual_ui_fixture":4}', encoding="utf-8")
            parent = os.getpid()+1 if wrong_parent else os.getpid()
            (actual/"case.json").write_text(json.dumps({"schema":"cfg2.settings-selection.ui.001",
                "case_id":case,"context":"imgui_cpp","status":"observed","error":"",
                "acceptance_claim":False,"states":{"fixture":{}},"events":[{}],"frames":[{}],
                "fixture":{"runtime":{"process_id":17,"parent_process_id":parent}}}), encoding="utf-8")
            (actual/"runtime.json").write_text(json.dumps({"process_id":17,"parent_process_id":parent}), encoding="utf-8")
            return subprocess.CompletedProcess(argv, 0)
        with patch.object(capture.subprocess, "run", side_effect=observe):
            capture.capture(output, sys.executable, root, selected_cases=keys)
        return json.loads((output/"receipt.json").read_bytes())

    def test_persist_starts_absent_and_reopen_keeps_same_file(self):
        row = self.pair()
        self.assertEqual(row["state"], "observed")
        persist, reopen = row["cases"]
        self.assertIsNone(persist["preferences_before"])
        self.assertEqual(persist["preferences_after"], reopen["preferences_before"])
        self.assertEqual(reopen["preferences_before"], reopen["preferences_after"])

    def test_reopen_rewrite_is_not_accepted(self):
        with self.assertRaisesRegex(ValueError, "changed persisted preference bytes"):
            self.pair(mutate=True)

    def test_unrelated_runtime_parent_is_not_accepted(self):
        with self.assertRaisesRegex(ValueError, "not the launched child"):
            self.pair(wrong_parent=True)


if __name__ == "__main__":
    unittest.main()
