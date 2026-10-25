"""Cheap availability controls; synthetic rows never replace scientific pins."""
import ast
import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("reference_readiness_tests", ROOT / "scripts/ci_python_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)


class ReferenceReadiness(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="imagejxx-reference-readiness-")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.file = self.root / "original.bin"
        self.file.write_bytes(b"synthetic immutable reference")
        self.sha = hashlib.sha256(self.file.read_bytes()).hexdigest()
        self.rows = (("REQUIRED_PATH", "REQUIRED_HASH", None, self.sha, self.file.stat().st_size),)
        self.environment = {"REQUIRED_PATH": str(self.file), "REQUIRED_HASH": self.sha}

    def check(self, environment=None):
        with patch.object(suite, "REQUIRED_REFERENCE_INPUTS", self.rows):
            return suite.reference_readiness(self.root, self.environment if environment is None else environment)

    def test_literal_bytes_available_without_acceptance(self):
        row = self.check()
        self.assertEqual(row["state"], "ready")
        self.assertIs(row["acceptance_claim"], False)
        self.assertEqual(row["inputs"][0]["observed"], {"path": str(self.file), "bytes": self.file.stat().st_size, "sha256": self.sha})

    def test_missing_configuration_never_generates_a_replacement(self):
        before = sorted(self.root.iterdir())
        self.assertEqual(self.check({})["state"], "unavailable")
        self.assertEqual(sorted(self.root.iterdir()), before)

    def test_missing_and_changed_hash_settings_refuse_before_read(self):
        for value in (None, "0" * 64):
            with self.subTest(value=value), patch.object(suite, "pin", side_effect=AssertionError("must not read")):
                environment = {"REQUIRED_PATH": str(self.file)}
                if value is not None:
                    environment["REQUIRED_HASH"] = value
                self.assertEqual(self.check(environment)["inputs"][0]["state"], "invalid")

    def test_missing_file_and_directory_are_unavailable(self):
        for path in (self.root / "absent", self.root):
            with self.subTest(path=path):
                self.assertEqual(self.check({**self.environment, "REQUIRED_PATH": str(path)})["state"], "unavailable")

    def test_same_size_changed_bytes_do_not_repin(self):
        self.file.write_bytes(b"X" * self.file.stat().st_size)
        row = self.check()["inputs"][0]
        self.assertEqual(row["state"], "invalid")
        self.assertEqual(row["expected_sha256"], self.sha)

    def test_wrong_size_refuses_before_hashing(self):
        self.file.write_bytes(b"short")
        with patch.object(suite, "pin", side_effect=AssertionError("must not read")):
            self.assertEqual(self.check()["inputs"][0]["state"], "invalid")

    def test_oversized_reference_refuses_before_hashing(self):
        self.rows = (("REQUIRED_PATH", "REQUIRED_HASH", None, self.sha, None),)
        with self.file.open("r+b") as stream:
            stream.truncate(64 * 1024 * 1024 + 1)
        with patch.object(suite, "pin", side_effect=AssertionError("must not read")):
            self.assertEqual(self.check()["inputs"][0]["state"], "invalid")

    def test_relative_paths_are_bound_to_declared_repository(self):
        self.assertEqual(self.check({**self.environment, "REQUIRED_PATH": "original.bin"})["state"], "ready")
        self.rows = ((None, None, "original.bin", self.sha, self.file.stat().st_size),)
        self.assertEqual(self.check({})["state"], "ready")

    def test_read_failure_is_retained_as_unavailable(self):
        with patch.object(suite, "pin", side_effect=PermissionError("synthetic denied read")):
            row = self.check()["inputs"][0]
        self.assertEqual(row["state"], "unavailable")
        self.assertIn("synthetic denied", row["reason"])

    def test_mutation_during_inspection_refuses(self):
        original = suite.pin
        def changed(path):
            result = original(path)
            self.file.write_bytes(b"changed after hash")
            return result
        with patch.object(suite, "pin", side_effect=changed):
            self.assertEqual(self.check()["inputs"][0]["state"], "invalid")

    def test_every_missing_input_is_reported(self):
        self.rows = self.rows + (("OTHER_PATH", None, None, self.sha, None),)
        result = self.check({})
        self.assertEqual(len(result["inputs"]), 2)
        self.assertTrue(all(row["state"] == "unavailable" for row in result["inputs"]))

    def test_original_reader_pins_match_gate_literals(self):
        def constants(file):
            tree = ast.parse((ROOT / file).read_text(encoding="utf-8-sig"))
            result = {}
            for node in tree.body:
                if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                    try:
                        result[node.targets[0].id] = ast.literal_eval(node.value)
                    except (ValueError, TypeError):
                        pass
            return result
        gaussian = constants("tests/python/test_gaussian_sigma_ui.py")
        xyz = constants("tests/python/test_xyz_numeric_zoom_ui.py")
        ranges = constants("tests/python/test_axis_range_reference.py")
        expected = [gaussian["UI_CONTRACT_SHA"], gaussian["NUMERIC_CONTRACT_SHA"], gaussian["REFERENCE_SHA"],
                    xyz["CONTRACT_SHA"], ranges["CORPUS_PIN"][1], ranges["FORMULA_PIN"][1]]
        self.assertEqual([row[3] for row in suite.REQUIRED_REFERENCE_INPUTS], expected)
        self.assertEqual([row[4] for row in suite.REQUIRED_REFERENCE_INPUTS][-2:],
                         [ranges["CORPUS_PIN"][0], ranges["FORMULA_PIN"][0]])

    def test_missing_prerequisites_stop_before_product_import_or_capture(self):
        output = self.root / "failed-run"
        with patch.object(suite.importlib.metadata, "distribution", side_effect=AssertionError("no product import")), \
             patch.object(suite, "capture", side_effect=AssertionError("no capture")), \
             patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "reference prerequisites"):
                suite.run(output, self.root)
        import json
        receipt = json.loads((output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual(len(receipt["reference_readiness"]["inputs"]), 6)
        self.assertFalse((output / "cpp-observations").exists())


if __name__ == "__main__":
    unittest.main()
