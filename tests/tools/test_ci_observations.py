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
        self.root = Path(self.temporary.name)
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


if __name__ == "__main__":
    unittest.main()
