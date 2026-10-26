"""Transport controls using synthetic bytes; no scientific parity is claimed."""
import ast
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("axis_transport_suite", ROOT / "scripts/ci_python_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)
HISTORICAL = "build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001"


def reader_functions(filename, names, namespace):
    """Run the actual reader bodies without importing the native application."""
    source = ROOT / "tests/python" / filename
    tree = ast.parse(source.read_bytes())
    nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    assert len(nodes) == len(names)
    for node in nodes:
        node.decorator_list = []
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), namespace)
    return namespace


class AxisReferenceTransport(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-axis-transport-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / "tests/fixtures").mkdir(parents=True)
        self.corpus = self.root / "original-corpus.json"
        self.formula = self.root / "original-formula.json"
        singleton = self.root / "tests/fixtures/axis-plane-imagej154r.json"
        corpus = {"schema_version": 1, "imagej_version": "1.54r",
                  "imagej_commit": "62db90a269cccc1ac79f658e4602043214ed055b",
                  "exact_ordered_case_ids": ["synthetic"], "cases": [{"case_id": "synthetic"}]}
        def write(path, value):
            raw = json.dumps(value).encode()
            path.write_bytes(raw)
            return len(raw), hashlib.sha256(raw).hexdigest()
        self.corpus_pin = write(self.corpus, corpus)
        formula = {"state": "passed_independent48_reference_formula_verification",
                   "observed_cases": 48, "target_product_invoked": False,
                   "corpus": {"path": HISTORICAL + "/capture-001/corpus.json",
                              "sha256": self.corpus_pin[1], "bytes": self.corpus_pin[0]},
                   "cases": [{"case_id": "synthetic"}]}
        self.formula_pin = write(self.formula, formula)
        singleton_pin = write(singleton, {"exact_ordered_case_ids": ["single"], "cases": [{"case_id": "single"}]})
        self.environment = {"IMAGEJXX_AXIS_RANGE_REFERENCE": str(self.corpus),
                            "IMAGEJXX_AXIS_RANGE_FORMULA_REFERENCE": str(self.formula)}
        self.reader = reader_functions("test_axis_range_reference.py",
            {"_pairs", "_nonfinite", "_load", "references"},
            {"__file__": str(self.root / "tests/python/test_axis_range_reference.py"),
             "Path": Path, "os": os, "json": json, "hashlib": hashlib,
             "CORPUS_PIN": self.corpus_pin, "FORMULA_PIN": self.formula_pin,
             "SINGLETON_PIN": singleton_pin, "CASE_IDS": ("synthetic",), "SINGLETON_IDS": ("single",)})

    def test_relocated_originals_preserve_embedded_historical_identity(self):
        before = self.formula.read_bytes()
        with patch.dict(os.environ, self.environment, clear=True):
            corpus, singleton = self.reader["references"]()
        self.assertEqual(set(corpus), {"synthetic"})
        self.assertEqual(set(singleton), {"single"})
        self.assertEqual(self.formula.read_bytes(), before)
        self.assertFalse((self.root / "build").exists())

    def test_relative_locations_are_repository_relative(self):
        environment = {name: Path(value).name for name, value in self.environment.items()}
        with patch.dict(os.environ, environment, clear=True):
            self.reader["references"]()

    def test_changed_corpus_or_formula_is_rejected_by_actual_reader(self):
        for path in (self.corpus, self.formula):
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.write_bytes(b"X" * len(original))
                with patch.dict(os.environ, self.environment, clear=True), self.assertRaises(AssertionError):
                    self.reader["references"]()
                path.write_bytes(original)

    def test_explicit_empty_or_missing_override_never_uses_fallback(self):
        for name in self.environment:
            for value in ("", str(self.root / "missing")):
                with self.subTest(name=name, value=value), \
                     patch.dict(os.environ, {**self.environment, name: value}, clear=True), \
                     self.assertRaises(AssertionError):
                    self.reader["references"]()

    def test_readiness_override_and_fallback_with_unchanged_pin(self):
        rows = (("IMAGEJXX_AXIS_RANGE_REFERENCE", None, "original-corpus.json",
                 self.corpus_pin[1], self.corpus_pin[0]),)
        with patch.object(suite, "REQUIRED_REFERENCE_INPUTS", rows):
            for environment in ({}, self.environment, {"IMAGEJXX_AXIS_RANGE_REFERENCE": self.corpus.name}):
                self.assertEqual(suite.reference_readiness(self.root, environment)["state"], "ready")
            for value in ("", "missing"):
                result = suite.reference_readiness(self.root, {"IMAGEJXX_AXIS_RANGE_REFERENCE": value})
                self.assertEqual(result["state"], "unavailable")
            self.corpus.write_bytes(b"X" * self.corpus_pin[0])
            result = suite.reference_readiness(self.root, self.environment)
            self.assertEqual(result["inputs"][0]["state"], "invalid")
            self.assertEqual(result["inputs"][0]["expected_sha256"], self.corpus_pin[1])

    def test_other_corpus_consumers_refuse_wrong_original_hash(self):
        self.corpus.write_bytes(b"wrong original")
        fixtures = reader_functions("test_parity_fixtures.py", {"range_corpus_and_fixture"},
            {"ROOT": self.root, "os": os, "digest": lambda raw: hashlib.sha256(raw).hexdigest()})
        ui = reader_functions("test_axis_range_reslice_ui.py", {"range_oracles"},
            {"__file__": str(self.root / "tests/python/test_axis_range_reslice_ui.py"),
             "Path": Path, "os": os, "hashlib": hashlib})
        for reader, name in ((fixtures, "range_corpus_and_fixture"), (ui, "range_oracles")):
            for value in (str(self.corpus), self.corpus.name, ""):
                with self.subTest(reader=name, value=value), \
                     patch.dict(os.environ, {"IMAGEJXX_AXIS_RANGE_REFERENCE": value}, clear=True), \
                     self.assertRaises(AssertionError):
                    reader[name]()

    def test_gate_uses_the_consumers_existing_corpus_override(self):
        self.assertEqual([row[0] for row in suite.REQUIRED_REFERENCE_INPUTS][-2:],
                         ["IMAGEJXX_AXIS_RANGE_REFERENCE", "IMAGEJXX_AXIS_RANGE_FORMULA_REFERENCE"])


if __name__ == "__main__":
    unittest.main()
