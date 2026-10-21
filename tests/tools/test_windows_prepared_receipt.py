"""Pure prepared-receipt admission using only owned temporary test files.

The observer module is loaded without main; no Win32 object, product, retained
fixture, UIA worker or preparation/collection driver is invoked.
"""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


CONTRACT_SHA = "e224a17817cc9088437e008d0157aa0bd952d7274b1ad083229c9756efca710d"
FIXTURE_SCHEMA = "imagejxx.windows-workload-fixtures.001"
OWNED_FIXTURES = (
    ("F01.eager-selection", "eager-selection.ijx", b"owned fixture one\n"),
    ("F02.scientific-czt", "scientific-czt.ijx", b"owned fixture two\n"),
    ("F03.preview-latency", "preview-latency.ijx", b"owned fixture three\n"),
)


class WindowsPreparedReceiptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).resolve().parents[2] / "scripts" / "capture_windows_platform_workload.py"
        spec = importlib.util.spec_from_file_location("_owned_windows_prepared_receipt", source)
        cls.observer = importlib.util.module_from_spec(spec)
        before_bytecode = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True
            spec.loader.exec_module(cls.observer)
        finally:
            sys.dont_write_bytecode = before_bytecode

    def setUp(self):
        owned = tempfile.TemporaryDirectory(prefix="imagejxx-windows-receipt-test-")
        self.addCleanup(owned.cleanup)
        self.workspace = Path(owned.name).resolve()
        stage = self.workspace / "build/windows-workload-001/owned-preparation"
        directory = stage / "fixtures"
        directory.mkdir(parents=True)
        self.receipt = stage / "preparation.json"
        self.contract = {"fixtures": []}
        self.record = {
            "id": "windows-workload.prepare.002",
            "state": "prepared_fixtures_only_no_platform_execution", "visible_launches": 0,
            "contract": {"sha256": CONTRACT_SHA}, "fixtures": [],
        }
        self.payloads = {}
        for identity, filename, payload in OWNED_FIXTURES:
            path = directory / filename
            path.write_bytes(payload)
            checksum = hashlib.sha256(payload).hexdigest()
            self.contract["fixtures"].append({
                "id": identity, "filename": filename, "raw_sha256": checksum,
            })
            self.record["fixtures"].append({
                "id": identity, "path": str(path), "bytes": len(payload),
                "sha256": checksum, "raw_sha256": checksum,
            })
            self.payloads[path] = payload
        root = mock.patch.object(self.observer, "ROOT", self.workspace)
        root.start()
        self.addCleanup(root.stop)

    def check_record(self, value):
        self.receipt.write_text(json.dumps(value), encoding="utf-8")
        return self.observer.verify_prepared(self.receipt, self.contract)

    def test_known_complete_002_receipt_accepts_exact_owned_files_without_mutation(self):
        before_record, before_contract = deepcopy(self.record), deepcopy(self.contract)
        decoded, files = self.check_record(self.record)
        self.assertEqual(decoded, before_record)
        self.assertEqual(files, {
            filename: str(path) for (_, filename, _), path in zip(OWNED_FIXTURES, self.payloads)
        })
        self.assertEqual(self.record, before_record)
        self.assertEqual(self.contract, before_contract)
        for path, expected in self.payloads.items():
            self.assertEqual(path.read_bytes(), expected)

    def test_original_001_and_original_schema_receipts_remain_supported(self):
        original = deepcopy(self.record)
        original["id"] = "windows-workload.prepare.001"
        schema_record = {
            "schema": FIXTURE_SCHEMA, "state": "prepared_fixtures_only",
            "inputs": {"contract": {"sha256": CONTRACT_SHA}},
            "fixtures": deepcopy(self.record["fixtures"]),
        }
        for name, record in (("001", original), ("schema", schema_record)):
            with self.subTest(receipt_kind=name):
                decoded, files = self.check_record(record)
                self.assertEqual(decoded, record)
                self.assertEqual(len(files), 3)

    def test_unknown_ids_states_visibility_and_contract_are_refused(self):
        variants = (
            ("unknown-id", "id", "windows-workload.prepare.003"),
            ("prefix-only", "id", "windows-workload.prepare.002.extra"),
            ("bad-state", "state", "failed"),
            ("launched", "visible_launches", 1),
            ("wrong-contract", "contract", {"sha256": "f" * 64}),
        )
        for name, key, replacement in variants:
            with self.subTest(refusal=name):
                record = deepcopy(self.record)
                record[key] = replacement
                with self.assertRaises(RuntimeError):
                    self.check_record(record)
        wrong_schema = {
            "schema": "unknown.fixture.schema", "state": "prepared_fixtures_only",
            "inputs": {"contract": {"sha256": CONTRACT_SHA}},
            "fixtures": deepcopy(self.record["fixtures"]),
        }
        with self.assertRaises(RuntimeError):
            self.check_record(wrong_schema)

    def test_missing_and_changed_receipt_or_fixture_fields_are_refused(self):
        for key in ("id", "state", "visible_launches", "contract", "fixtures"):
            with self.subTest(missing_receipt_field=key):
                record = deepcopy(self.record)
                del record[key]
                with self.assertRaises((RuntimeError, KeyError)):
                    self.check_record(record)
        for key in ("id", "path", "bytes", "sha256", "raw_sha256"):
            with self.subTest(missing_fixture_field=key):
                record = deepcopy(self.record)
                del record["fixtures"][0][key]
                with self.assertRaises((RuntimeError, KeyError)):
                    self.check_record(record)
        for key, replacement in (("bytes", 999), ("sha256", "a" * 64), ("raw_sha256", "b" * 64)):
            with self.subTest(changed_fixture_field=key):
                record = deepcopy(self.record)
                record["fixtures"][0][key] = replacement
                with self.assertRaises(RuntimeError):
                    self.check_record(record)

    def test_missing_reordered_and_changed_fixture_payloads_are_refused(self):
        incomplete = deepcopy(self.record)
        incomplete["fixtures"].pop()
        with self.assertRaisesRegex(RuntimeError, "Fixture membership changed"):
            self.check_record(incomplete)
        reordered = deepcopy(self.record)
        reordered["fixtures"][0], reordered["fixtures"][1] = reordered["fixtures"][1], reordered["fixtures"][0]
        with self.assertRaisesRegex(RuntimeError, "Fixture membership changed"):
            self.check_record(reordered)
        path = next(iter(self.payloads))
        original = self.payloads[path]
        path.write_bytes(b"X" * len(original))
        with self.assertRaisesRegex(RuntimeError, "Consumed identity changed"):
            self.check_record(self.record)
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            self.check_record(self.record)

    def test_valid_file_outside_owned_preparation_stage_is_refused(self):
        outside = self.workspace / "outside.ijx"
        outside.write_bytes(OWNED_FIXTURES[0][2])
        record = deepcopy(self.record)
        record["fixtures"][0]["path"] = str(outside)
        with self.assertRaisesRegex(RuntimeError, "Prepared file is outside the owned stage"):
            self.check_record(record)


if __name__ == "__main__":
    unittest.main()
