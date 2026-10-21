"""Current rank registration checks; no product or global historical validation.

Read only the three current manifests, pinned portable fixture and exact current
numerical trace. Retained reference, historical reports and producer artifact
paths are metadata here and are never followed.
"""

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE_ID = "F.G042.rank-imagej.finite-scalar161"
CONTRACT_ID = "FC.G042.rank-imagej.finite-scalar161"
RUN_ID = "p06f.rank-fixture-numerical.001"
TRACE_ID = "FT.G042.rank-imagej.finite-scalar161.python_api"
FIXTURE_PATH = "tests/fixtures/rank_imagej_154r_001.json"
FIXTURE_SHA = "121d1688e9a26c20818fbebfe88cbc4d1704ad26d330bfff5bdcc4d8e11694e5"
TRACE_PATH = "build/p06f-001/rank-fixture-observation001/numerical-trace002.json"
LINKED = (
    "G042.rank-imagej.profile-method-radius",
    "G042.rank-imagej.targets-roi",
)
OTHER_RANK = (
    "G042.rank-imagej.preview-publication",
    "G042.rank-imagej.lifecycle-resources",
    "G042.rank-imagej.record-replay-rebind",
    "G042.rank-imagej.persistence-surfaces",
)
NUMERICAL_CASE = {
    "suite": "p06f_rank_fixture_current",
    "classname": "tests.qualification.test_rank_fixture_observations",
    "name": "test_rank_fixture_current_finite_scalar_observations",
}
RUN_SCOPE = (
    "Current installed public native rank preparation observed through Python:161 finite scalar complete outputs "
    "and10 separately asserted auxiliary footprints. Two actual reader testcases; no full six-option/native-SDK/"
    "ImGui/lifecycle/performance/platform/family acceptance."
)


def one(rows, identity):
    selected = [row for row in rows if row["id"] == identity]
    if len(selected) != 1:
        raise AssertionError("Expected exactly one registered row: " + identity)
    return selected[0]


def canonical_sha(value):
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=False, allow_nan=False).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


class RankFixtureRegistrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ROOT / "docs/parity/fixtures.json").read_text(encoding="utf-8"))
        cls.commands = json.loads((ROOT / "docs/parity/commands.json").read_text(encoding="utf-8"))
        cls.inventory = json.loads((ROOT / "docs/parity/inventory.json").read_text(encoding="utf-8"))
        cls.fixture = one(cls.manifest["fixtures"], FIXTURE_ID)
        cls.contract = one(cls.manifest["fixture_contracts"], CONTRACT_ID)
        cls.accepted_run = one(cls.inventory["verification_runs"], RUN_ID)
        # These two literal paths are the complete non-manifest read allowlist.
        fixture_bytes = (ROOT / FIXTURE_PATH).read_bytes()
        if len(fixture_bytes) != 311070 or hashlib.sha256(fixture_bytes).hexdigest() != FIXTURE_SHA:
            raise AssertionError("Pinned portable rank fixture changed")
        cls.corpus = json.loads(fixture_bytes)

    def test_portable_fixture_pin_and_historical_annotation_remain_exact(self):
        self.assertEqual({key: self.fixture[key] for key in ("path", "bytes", "sha256")}, {
            "path": FIXTURE_PATH, "bytes": 311070, "sha256": FIXTURE_SHA,
        })
        self.assertEqual(self.fixture["case_count"], 161)
        self.assertEqual(self.fixture["capture_status"], "captured")
        self.assertEqual(self.fixture["match_status"], "matched")
        self.assertEqual(self.fixture["requirement_ids"], list(LINKED))
        self.assertEqual(self.fixture["verification_run_ids"], [RUN_ID])
        self.assertEqual(self.fixture["comparison"], {"method": "exact", "atol": 0, "rtol": 0})
        # Original generator annotation is immutable. Current prepared-output
        # scope is asserted separately and grants no publication/editor credit.
        self.assertEqual(self.fixture["scope"],
                         "Core numerical/rectangle API only; no stack dispatch, mask restoration, editor, commercial, or platform acceptance")
        self.assertEqual(self.corpus["run_count"], 161)
        self.assertEqual(len(self.corpus["runs"]), 161)
        self.assertEqual(self.corpus["kernel_count"], 10)
        self.assertEqual(len(self.corpus["kernels"]), 10)
        self.assertEqual(self.corpus["original_selected_run_count"], 158)
        self.assertEqual(self.corpus["supplement_run_count"], 3)

    def test_only_two_partial_requirements_gain_fixture_links(self):
        current_id = "p06f.rank-current-surface-comparison.001"
        contexts = ("cpp_api", "python_api", "imgui_cpp", "imgui_python")
        contract_hashes = {
            "profile-method-radius": "6940e2c1dcd6f61211c139ac7372671ec9b1eec4f9950995a8a79da4e31ac80a",
            "targets-roi": "2b8ac6db13ee0a2a03f04ee7ede6ca58acc001cc4f14bd50890f852fb31aa9a7",
            "preview-publication": "01e684922eabcc5a1a20cee7a1ac2c8fe0dbd0265878c0dde97f68e4ec2fef8b",
            "lifecycle-resources": "90a82e99ba3edbb373ca90e2d253093dfaf1881579e8b67da84a2beec28c5918",
            "record-replay-rebind": "f06f1f95a9b7da4c4ab8cbb15a1f21bfad8b64110a003f11c1cda02e00b9b2ed",
            "persistence-surfaces": "2e3b654fec3ed588514272dc996efd8f2bce0d0a12e6fd7551dbe5daa527edc2",
        }
        current = one(self.inventory["verification_runs"], current_id)
        self.assertIs(current["acceptance"], False)
        self.assertIs(current["complete_requirement_coverage"], False)
        self.assertEqual(current["surface_traces_schema_version"], 1)
        self.assertEqual(current["python"], {
            "passed": 24, "failed": 0, "errors": 0, "skipped": 0, "total": 24,
            "report": "build/p06f-001/current-surfaces001/results.xml",
        })
        binding, = [item for item in self.commands["run_bindings"] if item["run_id"] == current_id]
        self.assertEqual(binding["inventory_run_sha256"], canonical_sha(current))
        report, = binding["reports"]
        self.assertEqual(report, {
            "role": "python", "path": current["python"]["report"], "format": "pytest_junit_v1",
            "sha256": "1faecafda77927bbd0c5e502cd8e0d73337d7262a3f4934153d7c516a6fa3977",
            "totals": {"passed": 24, "failed": 0, "errors": 0, "skipped": 0, "total": 24},
        })
        traces = current["surface_traces"]
        proofs = [proof for proof in self.commands["surface_proofs"] if proof["run_id"] == current_id]
        expected_traces = {
            "trace.rank.current." + stem + "." + context
            for stem in contract_hashes for context in contexts
        }
        self.assertEqual(len(traces), 24)
        self.assertEqual({trace["id"] for trace in traces}, expected_traces)
        self.assertEqual(len(proofs), 24)
        self.assertEqual({proof["id"] for proof in proofs}, {
            "SP.G042.rank-current." + stem + "." + context
            for stem in contract_hashes for context in contexts
        })
        assertion_count = 0
        for identity in LINKED + OTHER_RANK:
            with self.subTest(requirement=identity):
                requirement = one(self.commands["requirements"], identity)
                # Source eligibility is separately adopted; it cannot promote
                # these incomplete options or imply distribution acceptance.
                commercial = requirement["commercial"]
                self.assertEqual(commercial["decision"], "eligible")
                self.assertIn("docs/parity/rank-commercial-review-002.json", commercial["evidence"])
                self.assertIn("docs/parity/rank-commercial-review-001.json", commercial["evidence"])
                self.assertEqual(len(commercial["evidence"]), len(set(commercial["evidence"])))
                self.assertIn(requirement["implementation_status"], ("open", "partial"))
                self.assertEqual(requirement["verification_status"], "partial")
                prefix = ["p06f.rank-surface-comparison.001"]
                if identity in LINKED:
                    self.assertEqual(requirement["fixture_ids"], [FIXTURE_ID])
                    prefix.append(RUN_ID)
                else:
                    self.assertEqual(requirement["fixture_ids"], [])
                    self.assertNotIn(RUN_ID, requirement["verification_run_ids"])
                run_ids = requirement["verification_run_ids"]
                self.assertEqual(run_ids[:len(prefix)], prefix)
                self.assertIn(current_id, run_ids[len(prefix):])
                self.assertEqual(len(run_ids), len(set(run_ids)))
                stem = identity.rsplit(".", 1)[1]
                contract = one(self.commands["surface_contracts"], "SC." + identity)
                self.assertEqual(contract["requirement_id"], identity)
                self.assertEqual(contract["sha256"], contract_hashes[stem])
                self.assertEqual(contract["sha256"], canonical_sha({
                    key: value for key, value in contract.items() if key != "sha256"
                }))
                self.assertEqual(set(contract["contexts"]), set(contexts))
                for context in contexts:
                    assertions = contract["contexts"][context]
                    self.assertTrue(assertions)
                    self.assertEqual(len(assertions), len({item["id"] for item in assertions}))
                    assertion_count += len(assertions)
                    case = {
                        "suite": "p06f_rank_current_surfaces",
                        "classname": "tests.qualification.test_rank_current_surface_reader",
                        "name": "test_rank_current_surface[" + stem + "-" + context + "]",
                    }
                    producer_id = "p06f.rank.current.surface." + context + ".001"
                    trace_id = "trace.rank.current." + stem + "." + context
                    trace_case_id = "case.rank.current." + stem + "." + context
                    proof = one(proofs, "SP.G042.rank-current." + stem + "." + context)
                    self.assertEqual(proof, {
                        "id": proof["id"], "requirement_id": identity, "contract_id": contract["id"],
                        "context": context, "run_id": current_id, "producer_id": producer_id,
                        "trace_id": trace_id, "trace_case_id": trace_case_id,
                        "report_role": "python", "report_case_identity": case,
                    })
                    # Catalogue objects are checked in memory. Do not follow
                    # their reports, traces, producer sources or artifacts.
                    trace = one(traces, trace_id)
                    self.assertEqual(trace["path"],
                                     "build/p06f-001/current-surfaces001/traces/" + stem + "-" + context + ".trace.json")
                    self.assertRegex(trace["sha256"], r"^[0-9a-f]{64}$")
                    self.assertEqual(trace["trace_case_id"], trace_case_id)
                    self.assertEqual(trace["report_case_identity"], case)
                    self.assertEqual(trace["report_role"], report["role"])
                    self.assertEqual(trace["producer"]["id"], producer_id)
                    self.assertEqual(trace["producer"]["context"], context)
                    for role in ("sources", "artifacts"):
                        self.assertTrue(trace["producer"][role])
                        for pin in trace["producer"][role]:
                            self.assertIsInstance(pin["path"], str)
                            self.assertTrue(pin["path"])
                            self.assertIs(type(pin["bytes"]), int)
                            self.assertGreater(pin["bytes"], 0)
                            self.assertRegex(pin["sha256"], r"^[0-9a-f]{64}$")
                    interface = "cpp" if context == "cpp_api" else "python" if context == "python_api" else "imgui"
                    self.assertIn("build/p06f-001/surface-reader001/traces/" + stem + "-" + context + ".catalogue.json",
                                  requirement["interfaces"][interface]["evidence"])
        self.assertEqual(assertion_count, 196)

    def test_contract_has_161_whole_outputs_and_two_explicit_scopes(self):
        contract = self.contract
        self.assertEqual(contract["fixture_id"], FIXTURE_ID)
        self.assertEqual(contract["format"], "imagej_rank_filter_finite_scalar_v1")
        self.assertEqual(contract["sha256"], canonical_sha({key: value for key, value in contract.items() if key != "sha256"}))
        cases = contract["case_set"]
        self.assertEqual(len(cases), 161)
        self.assertEqual([case["case_id"] for case in cases], ["rank-" + str(index).zfill(3) for index in range(161)])
        self.assertEqual([case["locator"] for case in cases], list(range(161)))
        self.assertTrue(all(case["required_output_paths"] == [""] for case in cases))
        self.assertEqual(contract["comparison"]["method"], "exact")
        self.assertEqual(contract["comparison"]["atol"], 0)
        self.assertEqual(contract["comparison"]["rtol"], 0)
        self.assertEqual(contract["comparison"]["numeric_paths"], [])
        self.assertEqual(contract["comparison"]["nonfinite"], "reject")
        self.assertEqual([scope["requirement_id"] for scope in contract["requirement_scopes"]], list(LINKED))
        profile, targets = contract["requirement_scopes"]
        self.assertEqual(profile["required"], [{"case_id": case["case_id"], "path": ""} for case in cases])
        # The retained K10/K11/K14 runs occupy these exact zero-based locators.
        self.assertEqual(targets["required"], [
            {"case_id": "rank-" + str(index).zfill(3), "path": ""} for index in range(130, 158)
        ])
        self.assertTrue(profile["exclusions"])
        self.assertTrue(targets["exclusions"])
        self.assertIn("irregular-ROI restoration", contract["comparison"]["rationale"])
        self.assertIn("untargeted planes", contract["comparison"]["rationale"])

    def test_retained_capture_kind_has_raw_anchor_and_no_fabricated_receipt(self):
        capture = self.contract["capture"]
        self.assertEqual(set(capture), {
            "kind", "generator_sources", "raw_output", "input_plan", "execution_inputs",
            "verified_inputs", "supplement_output",
        })
        self.assertEqual(capture["kind"], "retained_imagej_rank_selection_v1")
        self.assertNotIn("receipt", capture)
        self.assertEqual(capture["raw_output"], {
            "path": "build/references/rank-filter-imagej154r-001/capture-001/reference.stdout",
            "bytes": 865012, "sha256": "f2a3677079d355b53339b11612fc39ca809e5b070b22caa80a5d4fcc6b739df5",
        })
        self.assertEqual(capture["generator_sources"], [{
            "path": "tests/reference/extract_rank_imagej_fixture.py", "bytes": 9716,
            "sha256": "d8bffc6c4082c0e42e1a676f893d81a91f62fbb4ed6f634bd19c1524fd5bc181",
        }])

    def test_run_proof_and_report_bind_two_actual_python_reader_cases(self):
        self.assertEqual(self.accepted_run["scope"], RUN_SCOPE)
        self.assertEqual(self.accepted_run["python"], {
            "passed": 2, "failed": 0, "errors": 0, "skipped": 0,
            "report": "build/p06f-001/rank-fixture-observation001/results.xml",
        })
        self.assertNotIn("native", self.accepted_run)
        self.assertNotIn("sdk_report", self.accepted_run)
        self.assertEqual(self.accepted_run["upstream_reference"], {
            "commit": "62db90a269cccc1ac79f658e4602043214ed055b", "fixture": FIXTURE_PATH,
            "fixture_sha256": FIXTURE_SHA, "matched_cases": 161, "mismatches": 0, "reference_errors": 0,
        })
        traces = self.accepted_run["fixture_traces"]
        self.assertEqual(len(traces), 1)
        self.assertEqual(traces[0]["id"], TRACE_ID)
        self.assertEqual(traces[0]["contract_id"], CONTRACT_ID)
        self.assertEqual(traces[0]["contract_sha256"], self.contract["sha256"])
        self.assertEqual(traces[0]["report_role"], "python")
        self.assertEqual(traces[0]["report_case_identity"], NUMERICAL_CASE)
        self.assertEqual(traces[0]["producer"]["context"], "python_api")
        self.assertTrue(traces[0]["producer"]["sources"])
        self.assertTrue(traces[0]["producer"]["artifacts"])
        binding, = [item for item in self.commands["run_bindings"] if item["run_id"] == RUN_ID]
        self.assertEqual(binding["inventory_run_sha256"], canonical_sha(self.accepted_run))
        self.assertEqual(len(binding["reports"]), 1)
        report, = binding["reports"]
        self.assertRegex(report["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual({key: value for key, value in report.items() if key != "sha256"}, {
            "role": "python", "path": self.accepted_run["python"]["report"], "format": "pytest_junit_v1",
            "totals": {"passed": 2, "failed": 0, "errors": 0, "skipped": 0, "total": 2},
        })
        self.assertEqual([proof for proof in self.manifest["fixture_proofs"] if proof["contract_id"] == CONTRACT_ID], [{
            "id": "FP.G042.rank-imagej.finite-scalar161.python_api", "contract_id": CONTRACT_ID,
            "run_id": RUN_ID, "trace_id": TRACE_ID,
        }])

    def test_current_trace_preserves_all_161_complete_observed_outputs(self):
        pin, = self.accepted_run["fixture_traces"]
        self.assertEqual(pin["path"], TRACE_PATH)  # Refuse other paths before reading.
        payload = (ROOT / TRACE_PATH).read_bytes()
        self.assertEqual(len(payload), pin["bytes"])
        self.assertEqual(hashlib.sha256(payload).hexdigest(), pin["sha256"])
        trace = json.loads(payload)
        self.assertEqual(trace["schema_version"], 1)
        self.assertEqual(trace["run_id"], RUN_ID)
        self.assertEqual(trace["producer_id"], pin["producer"]["id"])
        self.assertEqual(trace["contract_id"], CONTRACT_ID)
        self.assertEqual(trace["contract_sha256"], self.contract["sha256"])
        self.assertEqual(trace["fixture_id"], FIXTURE_ID)
        self.assertEqual(trace["fixture_sha256"], FIXTURE_SHA)
        self.assertEqual(trace["report_role"], "python")
        self.assertEqual(trace["report_case_identity"], NUMERICAL_CASE)
        self.assertEqual(trace["capture_receipt"], self.contract["capture"]["raw_output"])
        self.assertEqual(len(trace["comparisons"]), 161)
        for index, (comparison, source) in enumerate(zip(trace["comparisons"], self.corpus["runs"])):
            with self.subTest(locator=index, captured_run=source["id"]):
                self.assertEqual(comparison["case_id"], "rank-" + str(index).zfill(3))
                self.assertEqual(comparison["input_sha256"], self.contract["case_set"][index]["input_sha256"])
                expected = {key: source[key] for key in ("id", "dtype", "shape_tczyx", "position_tcz")}
                expected["planes"] = [{"tcz": plane["tcz"], "after_hex": plane["after_hex"]} for plane in source["planes"]]
                self.assertEqual(comparison["observed"], expected)
                self.assertEqual(canonical_sha(expected), self.contract["case_set"][index]["expected_sha256"])


if __name__ == "__main__":
    unittest.main()
