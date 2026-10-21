"""Capture transport contracts; subprocesses are deliberately mocked, not UI proofs."""
import hashlib
import importlib.util
import json
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


if __name__ == "__main__":
    unittest.main()
