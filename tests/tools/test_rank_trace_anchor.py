"""Pure trace-anchor guards using owned literals and no retained capture reads."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import sys
import unittest


RAW_ANCHOR = {
    "path": "tests/fixtures/owned-rank-trace-anchor.stdout",
    "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "bytes": 17,
}
RECEIPT_ANCHOR = {
    "path": "tests/fixtures/owned-legacy-trace-receipt.json",
    "sha256": "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
    "bytes": 23,
}


def owned_trace():
    """Construct test-only metadata; paths are never opened by these tests."""
    case_identity = {
        "suite": "pytest", "classname": "owned.rank.trace", "name": "owned_numerical_case",
    }
    observed = {
        "id": "K02/uint8", "dtype": "uint8", "shape_tczyx": [1, 1, 1, 1, 1],
        "position_tcz": [0, 0, 0], "planes": [{"tcz": [0, 0, 0], "after_hex": "03"}],
    }
    fixture = {"id": "F.owned.rank.anchor", "sha256": "1" * 64}
    contract = {
        "id": "FC.owned.rank.anchor", "sha256": "2" * 64,
        "format": "imagej_rank_filter_finite_scalar_v1",
        "capture": {"kind": "retained_imagej_rank_selection_v1", "raw_output": deepcopy(RAW_ANCHOR)},
        "case_set": [{
            "case_id": "rank-000", "locator": 0, "input_sha256": "3" * 64,
            "required_output_paths": ["/planes/0/after_hex"],
        }],
        "comparison": {"method": "exact", "atol": 0, "rtol": 0, "numeric_paths": []},
    }
    row = {
        "producer": {"id": "owned.rank.producer"}, "report_role": "python",
        "report_case_identity": deepcopy(case_identity),
    }
    trace = {
        "schema_version": 1, "run_id": "owned.rank.run", "producer_id": "owned.rank.producer",
        "contract_id": "FC.owned.rank.anchor", "contract_sha256": "2" * 64,
        "fixture_id": "F.owned.rank.anchor", "fixture_sha256": "1" * 64,
        "report_role": "python", "report_case_identity": deepcopy(case_identity),
        "capture_receipt": deepcopy(RAW_ANCHOR),
        "comparisons": [{"case_id": "rank-000", "input_sha256": "3" * 64, "observed": deepcopy(observed)}],
    }
    projections = [{"expected": deepcopy(observed)}]
    return trace, row, contract, fixture, projections


class RankTraceAnchorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).resolve().parents[2] / "scripts" / "parity_fixtures.py"
        spec = importlib.util.spec_from_file_location("_owned_rank_trace_anchor_adapter", source)
        cls.adapter = importlib.util.module_from_spec(spec)
        previous_bytecode = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True
            spec.loader.exec_module(cls.adapter)
        finally:
            sys.dont_write_bytecode = previous_bytecode

    def validate(self, trace, row, contract, fixture, projections):
        return self.adapter._validate_trace(
            trace, "owned.rank.run", row, contract, fixture, projections, "owned rank anchor guard")

    def test_rank_trace_accepts_exact_retained_raw_capture_anchor(self):
        inputs = owned_trace()
        before = deepcopy(inputs)
        self.assertIsNone(self.validate(*inputs))
        self.assertEqual(inputs, before)

    def test_rank_trace_refuses_changed_anchor_fields_or_missing_raw_anchor(self):
        changes = (
            ("path", "tests/fixtures/other-owned-rank-anchor.stdout"),
            ("sha256", "f" * 64),
            ("bytes", 18),
        )
        for key, value in changes:
            with self.subTest(changed_field=key):
                trace, row, contract, fixture, projections = owned_trace()
                trace["capture_receipt"][key] = value
                with self.assertRaisesRegex(ValueError, "exact retained raw capture anchor"):
                    self.validate(trace, row, contract, fixture, projections)
        trace, row, contract, fixture, projections = owned_trace()
        del contract["capture"]["raw_output"]
        with self.assertRaisesRegex(ValueError, "exact retained raw capture anchor"):
            self.validate(trace, row, contract, fixture, projections)

    def test_nonrank_trace_keeps_existing_explicit_receipt_guard(self):
        trace, row, contract, fixture, projections = owned_trace()
        contract["format"] = "owned_cases_v1"
        contract["capture"] = {"receipt": deepcopy(RECEIPT_ANCHOR)}
        trace["capture_receipt"] = deepcopy(RECEIPT_ANCHOR)
        self.assertIsNone(self.validate(trace, row, contract, fixture, projections))
        trace["capture_receipt"] = deepcopy(RAW_ANCHOR)
        with self.assertRaisesRegex(ValueError, "different capture receipt"):
            self.validate(trace, row, contract, fixture, projections)


if __name__ == "__main__":
    unittest.main()
