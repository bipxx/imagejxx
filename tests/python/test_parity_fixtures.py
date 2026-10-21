"""Independent owned fixture contracts and immutable comparison evidence.

These data certify validator behavior, not ImageJ, Fiji, UI or licensing parity.
Expected projections, hashes and report identities are constructed independently
of the implementation under test. No capture or producer process is executed.
"""

from copy import deepcopy
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
from functools import lru_cache
import zipfile

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "owned_parity_fixtures_tests", ROOT / "scripts/parity_fixtures.py"
)
fixtures = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixtures)

BASELINE = "owned-fixture-baseline"
REVISION = "a" * 40
RUN_ID = "owned-fixture-run"
CASE_IDENTITY = {"suite": "owned", "classname": "owned.fixture", "name": "compare"}
FIXTURE_KEYS = (
    "id", "path", "sha256", "case_count", "reference", "generator", "scope",
    "comparison", "requirement_ids",
)
COUNTS = (
    "fixtures_declared", "fixtures_required", "content_files_verified",
    "contracts_required", "contracts_validated", "required_case_assertions",
    "verified_case_assertions",
)
GAPS = (
    "missing_contracts", "missing_case_assertions", "capture_gaps",
    "unavailable_evidence", "unsupported_methods", "enumeration_gaps",
)
REAL_CORPORA = (
    ("binary", "imagej_binary_processor_v1", 1184,
     "f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd"),
    ("conversion", "imagej_conversion_processor_v1", 462,
     "eb177fe986754d379fa0389ddca91de495c83ad8a095e06ecc63664611db3bd1"),
    ("roi-style", "imagej_roi_style_v1", 73,
     "e8e845400f8d69c1e9d41e693b1b70a57e71b05b3c19a108d8953edaea3d186a"),
)
AXIS_CASE_IDS = (
    "u8.xy.lower", "u8.xz.lower", "u8.yz.lower",
    "u16.xy.interior", "u16.xz.interior", "u16.yz.interior",
    "f32.xy.upper", "f32.xz.upper", "f32.yz.upper",
    "rgb.xy.interior", "rgb.xz.interior", "rgb.yz.interior",
)
AXIS_SELECTORS = (
    "/source_before/raw_hex", "/source_after/raw_hex", "/output/shape",
    "/output/pixel_type", "/output/S", "/output/raw_hex", "/output/spacing",
)


def canonical(value):
    """Independent frozen typed JSON serialization, including array order."""
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def digest(payload):
    return hashlib.sha256(payload).hexdigest()


def fingerprint(value):
    return digest(canonical(value))


def contract_digest(contract):
    return fingerprint({key: value for key, value in contract.items() if key != "sha256"})


def json_bytes(value):
    return canonical(value) + b"\n"


def pin(root, path, payload):
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
    return {"path": path, "sha256": digest(payload)}


def child_snapshot(child):
    return {
        "reference": deepcopy(child["reference"]),
        "contract": deepcopy(child["contract"]),
        "interfaces": {
            name: {key: deepcopy(surface[key]) for key in
                   ("applicable", "entry_points", "inapplicable_reason") if key in surface}
            for name, surface in child["interfaces"].items()
        },
    }


def independent_fixture_snapshot(fixture, children):
    result = {key: deepcopy(fixture[key]) for key in FIXTURE_KEYS if key in fixture}
    by_id = {child["id"]: child for child in children}
    result["requirements"] = {
        child_id: child_snapshot(by_id[child_id])
        for child_id in fixture["requirement_ids"]
    }
    return result


def owned_child(child_id, fixture_id, reference):
    return {
        "id": child_id, "parent_id": "G052", "milestones": ["M2"],
        "kind": "option", "title": "Owned comparison scope",
        "required_work": "Verify only the expressly selected owned outputs.",
        "candidate_ids": ["R-owned"], "reference": deepcopy(reference),
        "contract": {
            "parameters": [{"name": "mode", "type": "string", "default": "owned",
                            "units": "", "enabled_when": "always"}],
            "pixel_types": ["u8", "f32"], "samples": ["gray"], "axes": ["XY"],
            "targets": ["whole_plane"], "roi": ["whole_plane"],
            "border": ["constant_background"], "nonfinite": "Reject nonfinite inputs.",
            "geometry": "Owned fixed geometry.",
        },
        "interfaces": {
            name: {"applicable": True, "entry_points": ["owned_" + name], "evidence": []}
            for name in ("cpp", "python", "imgui")
        },
        "settings_consumers": [], "fixture_ids": [fixture_id],
        "verification_run_ids": [RUN_ID], "implementation_status": "partial",
        "verification_status": "partial",
        "commercial": {"decision": "pending", "evidence": [],
                       "reason": "Numerical validation grants no incorporation approval."},
        "provenance": {"implementation": "owned_native_equivalent",
                       "incorporated_components": []},
        "remaining": ["Real parity and commercial review remain required."],
    }


def default_datasets():
    return [
        ("pixels", [
            {"input": {"shape": [1, 3], "source": [0, 255, 7], "operation": "owned-a"},
             "expected": {"pixels": [255, 0, 7]}},
            {"input": {"shape": [1, 3], "source": [8, 1, 250], "operation": "owned-b"},
             "expected": {"pixels": [8, 2, 249]}},
        ], ["/pixels"], False),
        ("state", [
            {"input": {"order": ["fill", "stroke"], "name": "alpha"},
             "expected": {"style": {"rgba": [18, 52, 86, 128], "width": 0.5},
                          "enabled": True, "annotation": "unselected"}},
            {"input": {"order": ["stroke", "clear"], "name": "beta"},
             "expected": {"style": {"rgba": None, "width": 0.0},
                          "enabled": False, "annotation": None}},
        ], ["/style", "/enabled"], False),
        ("float", [
            {"input": {"sample": "zero"}, "expected": {"value": 0.0, "label": "zero"}},
            {"input": {"sample": "negative"}, "expected": {"value": -8.0, "label": "negative"}},
            {"input": {"sample": "positive"}, "expected": {"value": 32.0, "label": "positive"}},
        ], ["/value", "/label"], True),
    ]


def owned_set(tmp_path, datasets=None):
    """Full owned graph; all supplied hashes and expected values are independent."""
    root = tmp_path / "workspace"
    root.mkdir()
    member_path = "owned/reference.py"
    member_bytes = b"# Project-owned synthetic reference; never executed.\n"
    archive_buffer = io.BytesIO()
    with zipfile.ZipFile(archive_buffer, "w", compression=zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo(member_path, date_time=(2026, 1, 1, 0, 0, 0))
        archive.writestr(info, member_bytes)
    archive_pin = pin(root, "owned/reference.zip", archive_buffer.getvalue())
    members = [{"path": member_path, "sha256": digest(member_bytes)}]
    reference = {
        "reference_id": BASELINE, "component_id": "imagej-core",
        "archive_sha256": archive_pin["sha256"], "revision": REVISION,
        "scope": "Project-owned synthetic comparison only.", "source_members": members,
    }
    helper = pin(root, "scripts/owned_capture_helper.py", b"def identity(value):\n    return value\n")
    generator = pin(root, "scripts/owned_capture.py",
                    b"from owned_capture_helper import identity\n# Never executed.\n")
    runtime = {
        "kind": "owned_synthetic",
        "executables": [pin(root, "owned/runtime.bin", b"Owned runtime bytes.\n")],
        "sources": [deepcopy(generator), deepcopy(helper)],
        "version": "owned-synthetic-1",
    }
    source = pin(root, "owned/producer.py", b"# Owned observation producer source.\n")
    artifact = pin(root, "owned/producer.bin", b"Owned tested artifact bytes.\n")
    report = pin(root, "build/owned/python.xml", (
        '<testsuites><testsuite name="owned" tests="1" failures="0" errors="0" skipped="0">'
        '<testcase classname="owned.fixture" name="compare"/>'
        '</testsuite></testsuites>'
    ).encode("utf-8"))
    documents = {
        "references.json": {"schema_version": 1, "reference_id": BASELINE,
                            "imagej": {"commit": REVISION, "sha256": archive_pin["sha256"]},
                            "fiji": {"sha256": "b" * 64}},
        "fiji-components.json": {"schema_version": 1, "reference_id": BASELINE,
                                 "archive_sha256": "b" * 64, "files": []},
        "commands.json": {
            "schema_version": 1, "reference_id": BASELINE,
            "candidates": [{"id": "R-owned", "component_id": "imagej-core",
                            "evidence": {"archive_member": member_path}}],
            "requirements_schema_version": 1, "enumeration_status": "frozen",
            "requirements": [], "added_capabilities": [], "remaining": [],
            "run_bindings_schema_version": 1, "run_bindings": [],
            "owned_metadata": {"keep": [True, None, "é"]},
        },
        "fixtures.json": {
            "schema_version": 1, "reference_id": BASELINE,
            "enumeration_status": "frozen", "remaining": [], "fixtures": [],
            "fixture_evidence_schema_version": 1, "fixture_contracts": [],
            "fixture_proofs": [], "owned_metadata": {"keep": "disclaimer"},
        },
    }
    run = {
        "id": RUN_ID, "date": "2026-10-11", "scope": "Owned fixture validator test only.",
        "platform": "Owned synthetic", "python": {"report": report["path"],
        "passed": 1, "failed": 0, "skipped": 0, "errors": 0},
        "fixture_evidence_schema_version": 1, "fixture_traces": [],
    }
    inventory = {"schema_version": 1, "capabilities": [{"id": "G052", "milestones": ["M2"]}],
                 "verification_runs": [run]}
    for name, cases, selectors, floating in datasets or default_datasets():
        fixture_id = "F.owned-" + name
        children = [owned_child("G052.owned-" + name, fixture_id, reference)]
        if name == "state":
            children.append(owned_child("G052.owned-state-overlap", fixture_id, reference))
        documents["commands.json"]["requirements"].extend(children)
        corpus = {"schema_version": 1, "cases": deepcopy(cases),
                  "owned_metadata": {"keep": ["full input", "full expected"]}}
        corpus_pin = pin(root, "tests/fixtures/owned-" + name + ".json", json_bytes(corpus))
        comparison = {"method": "float_tolerance" if floating else "exact",
                      "atol": 0.25 if floating else 0, "rtol": 0.125 if floating else 0}
        fixture = {
            "id": fixture_id, **corpus_pin, "case_count": len(cases),
            "reference": deepcopy(reference), "generator": deepcopy(generator),
            "scope": reference["scope"], "comparison": comparison,
            "requirement_ids": [child["id"] for child in children],
            "capture_status": "captured", "match_status": "matched",
            "verification_run_ids": [],
            "license": {"decision": "pending", "evidence": [], "reason": "Review remains required."},
            "remaining": ["Synthetic is not real upstream parity."],
        }
        raw = pin(root, "build/owned/" + name + ".jsonl",
                  b"".join(json_bytes({"input": row["input"], "expected": row["expected"]})
                           for row in cases))
        capture = {
            "raw_output": raw, "generator_sources": [deepcopy(generator), deepcopy(helper)],
            "reference_archive": deepcopy(archive_pin), "reference_members": deepcopy(members),
            "driver": None, "runtime": deepcopy(runtime),
        }
        receipt = {"schema_version": 1, "fixture_sha256": fixture["sha256"],
                   "case_count": len(cases), "exit_code": 0, "reference_errors": 0, **deepcopy(capture)}
        capture["receipt"] = pin(root, "build/owned/" + name + "-receipt.json", json_bytes(receipt))
        case_rows = [{"case_id": name + "." + str(index), "locator": index,
                      "input_sha256": fingerprint(row["input"]),
                      "expected_sha256": fingerprint(row["expected"]),
                      "required_output_paths": deepcopy(selectors)}
                     for index, row in enumerate(cases)]
        scopes = [{"requirement_id": child["id"], "snapshot": child_snapshot(child),
                   "required": [{"case_id": row["case_id"], "path": path}
                                for row in case_rows for path in
                                (selectors if offset == 0 else ["/style"])],
                   "exclusions": ["Unselected metadata has no coverage."]}
                  for offset, child in enumerate(children)]
        contract = {
            "id": "FC.owned-" + name, "fixture_id": fixture_id,
            "fixture_snapshot": independent_fixture_snapshot(fixture, children),
            "format": "owned_cases_v1", "case_set": case_rows,
            "requirement_scopes": scopes,
            "comparison": {**comparison, "numeric_paths": ["/value"] if floating else [],
                           "units": "owned sample units", "rationale": "Frozen binary-exact boundary checks.",
                           "nonfinite": "reject"}, "capture": capture,
        }
        contract["sha256"] = contract_digest(contract)
        trace = {
            "schema_version": 1, "run_id": RUN_ID, "producer_id": "producer.owned",
            "contract_id": contract["id"], "contract_sha256": contract["sha256"],
            "fixture_id": fixture_id, "fixture_sha256": fixture["sha256"],
            "report_role": "python", "report_case_identity": deepcopy(CASE_IDENTITY),
            "capture_receipt": deepcopy(capture["receipt"]),
            "comparisons": [{"case_id": row["case_id"], "input_sha256": row["input_sha256"],
                             "observed": deepcopy(case["expected"])}
                            for row, case in zip(case_rows, cases)],
        }
        trace_pin = pin(root, "build/owned/" + name + "-trace.json", json_bytes(trace))
        run["fixture_traces"].append({
            "id": "FT.owned-" + name, **trace_pin, "contract_id": contract["id"],
            "contract_sha256": contract["sha256"], "report_role": "python",
            "report_case_identity": deepcopy(CASE_IDENTITY),
            "producer": {"id": "producer.owned", "context": "python_api",
                         "sources": [deepcopy(source)], "artifacts": [deepcopy(artifact)]},
        })
        documents["fixtures.json"]["fixtures"].append(fixture)
        documents["fixtures.json"]["fixture_contracts"].append(contract)
        documents["fixtures.json"]["fixture_proofs"].append({
            "id": "FP.owned-" + name, "contract_id": contract["id"],
            "run_id": RUN_ID, "trace_id": "FT.owned-" + name,
        })
    documents["commands.json"]["run_bindings"].append({
        "run_id": RUN_ID, "inventory_run_sha256": fingerprint(run), "reference": None,
        "reports": [{"role": "python", **report, "format": "pytest_junit_v1",
                     "totals": {"passed": 1, "failed": 0, "skipped": 0, "errors": 0, "total": 1}}],
    })
    return root, documents, inventory


def real_documents():
    # Discovery manifests retain legacy Infinity annotations. Content is strict.
    return {path.name: json.loads(path.read_text(encoding="utf-8"))
            for path in (ROOT / "docs/parity").glob("*.json")
            if path.name != "inventory.json"}


def validate(graph, **options):
    root, documents, inventory = graph
    return fixtures.validate_fixtures(root, documents, inventory, **options)


def first_contract(graph, index=0):
    return graph[1]["fixtures.json"]["fixture_contracts"][index]


def rebind(graph):
    graph[1]["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = fingerprint(
        graph[2]["verification_runs"][0]
    )


def read_json(root, identity):
    return json.loads((root / identity["path"]).read_text(encoding="utf-8"))


def rewrite_json(root, identity, value):
    payload = json_bytes(value)
    (root / identity["path"]).write_bytes(payload)
    identity["sha256"] = digest(payload)


def update_trace(graph, index, change):
    entry = graph[2]["verification_runs"][0]["fixture_traces"][index]
    trace = read_json(graph[0], entry)
    change(trace)
    rewrite_json(graph[0], entry, trace)
    rebind(graph)


def refresh_contract(graph, index=0):
    contract = first_contract(graph, index)
    contract["sha256"] = contract_digest(contract)
    entry = graph[2]["verification_runs"][0]["fixture_traces"][index]
    entry["contract_sha256"] = contract["sha256"]
    update_trace(graph, index, lambda trace: trace.update(contract_sha256=contract["sha256"]))


def test_fixture_snapshot_and_fingerprint_are_independent_and_ignore_status(tmp_path):
    graph = owned_set(tmp_path)
    fixture = graph[1]["fixtures.json"]["fixtures"][0]
    children = graph[1]["commands.json"]["requirements"]
    expected = independent_fixture_snapshot(fixture, children)
    assert fixtures.fixture_snapshot(fixture, {row["id"]: row for row in children}) == expected
    contract = first_contract(graph)
    assert fixtures.contract_fingerprint(contract) == contract_digest(contract)
    changed = deepcopy(fixture)
    changed.update(match_status="unverified", verification_run_ids=["different"],
                   remaining=["different"], license={"decision": "excluded"})
    assert fixtures.fixture_snapshot(changed, {row["id"]: row for row in children}) == expected
    changed_contract = deepcopy(contract)
    changed_contract["case_set"].reverse()
    assert fixtures.contract_fingerprint(changed_contract) != contract_digest(contract)
    changed_contract = deepcopy(contract)
    changed_contract["case_set"][0]["locator"] = 0.0
    assert fixtures.contract_fingerprint(changed_contract) != contract_digest(contract)


@pytest.mark.parametrize("name,format_name,count,sha", REAL_CORPORA)
def test_real_corpus_projection_preserves_all_inputs_and_expected_fields(name, format_name, count, sha):
    path = ROOT / "tests/fixtures" / (name + "-imagej154r.json")
    payload = path.read_bytes()
    assert digest(payload) == sha
    corpus = json.loads(payload)
    actual = fixtures.project_cases(format_name, corpus)
    expected = []
    for locator, case in enumerate(corpus["cases"]):
        output_keys = {"rgba"} if case.get("operation") == "color" else (
            {"stroke", "fill", "width"} if format_name == "imagej_roi_style_v1" else {"pixels"}
        )
        expected.append({"locator": locator,
                         "input": {key: deepcopy(value) for key, value in case.items() if key not in output_keys},
                         "expected": {key: deepcopy(case[key]) for key in output_keys}})
    assert actual == expected
    assert len(actual) == count
    if name == "roi-style":
        assert sum(row["input"]["operation"] == "color" for row in actual) == 19
        assert sum(row["input"]["operation"] == "setters" for row in actual) == 36
        assert sum(row["input"]["operation"] == "width" for row in actual) == 18


def test_real_legacy_corpora_remain_partial_without_mutation():
    documents = real_documents()
    inventory = json.loads((ROOT / "docs/parity/inventory.json").read_text(encoding="utf-8"))
    before_documents, before_inventory = deepcopy(documents), deepcopy(inventory)
    before_bytes = {row["path"]: (ROOT / row["path"]).read_bytes()
                    for row in documents["fixtures.json"]["fixtures"]}
    result = fixtures.validate_fixtures(ROOT, documents, inventory)
    # Preserve the original five proofs; Slicer240 and rank161 are additive.
    # Four other fixture contracts and global enumeration stay open.
    assert result["fixtures_declared"] == result["fixtures_required"] == 11
    assert result["content_files_verified"] == 10
    assert result["unsupported_methods"] == [
        {"fixture_id": "F.xyz-navigation-owned", "format": None,
         "reason": "Fixture adapter is not implemented"},
    ]
    assert result["contracts_required"] == 11
    assert result["contracts_validated"] == 7
    assert result["required_case_assertions"] == result["verified_case_assertions"] == 5523
    assert 3682 + 1680 + 161 == 5523
    archived, = result["archived_tool_sources"]
    assert archived["kind"] == "archived_tool_source"
    assert archived["run_id"] == "axis-plane-export-p05f-windows-evidence-001"
    assert archived["path"] == "scripts/parity_fixtures.py"
    assert archived["snapshot"]["sha256"] == "9306b01fa3b640d7821e8f32f0634dc4bf421b933e08540b2749082a5525c867"
    assert archived["snapshot"]["path"] == "build/plan-implementation/p05f-axis-plane-export-001/source-preparation-019/scripts/parity_fixtures.py"
    assert archived["snapshot"]["bytes"] == 100414
    assert result["missing_case_assertions"] == []
    assert len(result["missing_contracts"]) == 4
    assert {gap["fixture_id"] for gap in result["missing_contracts"]} == {
        "F.binary-imagej154r", "F.conversion-imagej154r",
        "F.roi-style-imagej154r", "F.xyz-navigation-owned",
    }
    axis_fixture, = [row for row in documents["fixtures.json"]["fixtures"]
                     if row["id"] == "F.G029.axis-plane.pixels"]
    assert axis_fixture["requirement_ids"] == ["G029.axis_plane_export"]
    assert axis_fixture["case_count"] == 12
    assert axis_fixture["capture_status"] == "captured"
    assert axis_fixture["match_status"] == "matched"
    assert axis_fixture["verification_run_ids"] == ["axis-plane-export-p05f-windows-evidence-001"]
    range_fixture, = [row for row in documents["fixtures.json"]["fixtures"]
                      if row["id"] == "F.G029.integer-range.pixels"]
    assert range_fixture["requirement_ids"] == ["G029.integer_range_reslice"]
    assert range_fixture["case_count"] == 48
    assert range_fixture["capture_status"] == "captured"
    assert range_fixture["match_status"] == "matched"
    assert range_fixture["verification_run_ids"] == ["integer-range-reslice-p05g-windows-evidence-001"]
    orientation_fixture, = [row for row in documents["fixtures.json"]["fixtures"]
                            if row["id"] == "F.G029.axis-range-orientation.pixels"]
    assert orientation_fixture["requirement_ids"] == ["G029.axis_range_orientation"]
    assert orientation_fixture["case_count"] == 96
    assert orientation_fixture["capture_status"] == "captured"
    assert orientation_fixture["match_status"] == "matched"
    assert orientation_fixture["verification_run_ids"] == ["axis-range-orientation-p05h-windows-evidence-001"]
    assert [row["id"] for row in documents["fixtures.json"]["fixture_contracts"]][:5] == [
        "FC.G016.area-boolean.pixels", "FC.G029.axis-plane.pixels", "FC.G029.integer-range.pixels",
        "FC.G029.axis-range-orientation.pixels", "FC.G029.physical-spacing.owned142",
    ]
    assert [row["id"] for row in documents["fixtures.json"]["fixture_contracts"]][5:] == [
        "FC.G029.slicer-bilinear.imagej240", "FC.G042.rank-imagej.finite-scalar161",
    ]
    contract, = [row for row in documents["fixtures.json"]["fixture_contracts"]
                 if row["id"] == "FC.G016.area-boolean.pixels"]
    assert contract["id"] == "FC.G016.area-boolean.pixels"
    assert contract["fixture_id"] == "F.G016.area-boolean.pixels"
    assert contract["format"] == "imagej_roi_boolean_v1"
    assert len(contract["case_set"]) == 72
    selectors = ["/operands/0/mask_rows", "/operands/1/mask_rows", "/result/mask_rows"]
    assert all(case["required_output_paths"] == selectors for case in contract["case_set"])
    scope, = contract["requirement_scopes"]
    assert scope["requirement_id"] == "G016.manager.area_boolean"
    assert len(scope["required"]) == 216
    assert {(item["case_id"], item["path"]) for item in scope["required"]} == {
        (case["case_id"], path) for case in contract["case_set"] for path in selectors
    }
    assert documents["fixtures.json"]["fixture_proofs"][:5] == [{
        "id": "FP.G016.area_boolean.cpp_api",
        "contract_id": contract["id"],
        "run_id": "roi-area-boolean-p05e-windows-evidence-001",
        "trace_id": "FT.G016.area_boolean.cpp_api",
    }, {
        "id": "FP.G029.axis_plane.cpp_api",
        "contract_id": "FC.G029.axis-plane.pixels",
        "run_id": "axis-plane-export-p05f-windows-evidence-001",
        "trace_id": "FT.G029.axis_plane.cpp_api",
    }, {
        "id": "FP.G029.integer_range.cpp_api",
        "contract_id": "FC.G029.integer-range.pixels",
        "run_id": "integer-range-reslice-p05g-windows-evidence-001",
        "trace_id": "FT.G029.integer_range.cpp_api",
    }, {
        "id": "FP.G029.axis_range_orientation.cpp_api",
        "contract_id": "FC.G029.axis-range-orientation.pixels",
        "run_id": "axis-range-orientation-p05h-windows-evidence-001",
        "trace_id": "FT.G029.axis_range_orientation.cpp_api",
    }, {
        "id": "FP.G029.physical_spacing.cpp_api",
        "contract_id": "FC.G029.physical-spacing.owned142",
        "run_id": "physical-inplane-spacing-p05i-windows-fixture-001",
        "trace_id": "FT.G029.physical_spacing.cpp_api",
    }]
    assert documents["fixtures.json"]["fixture_proofs"][5:] == [{
        "id": "FP.G029.slicer-bilinear.imagej240.python_api",
        "contract_id": "FC.G029.slicer-bilinear.imagej240",
        "run_id": "slicer-bilinear-retained-comparison-p05j-candidate-001",
        "trace_id": "FT.G029.slicer-bilinear.imagej240.python_api.candidate",
    }, {
        "id": "FP.G042.rank-imagej.finite-scalar161.python_api",
        "contract_id": "FC.G042.rank-imagej.finite-scalar161",
        "run_id": "p06f.rank-fixture-numerical.001",
        "trace_id": "FT.G042.rank-imagej.finite-scalar161.python_api",
    }]
    axis_contract, = [row for row in documents["fixtures.json"]["fixture_contracts"]
                      if row["id"] == "FC.G029.axis-plane.pixels"]
    assert axis_contract["fixture_id"] == axis_fixture["id"]
    assert axis_contract["format"] == "imagej_axis_plane_v1"
    assert len(axis_contract["case_set"]) == 12
    axis_selectors = ["/source_before/raw_hex", "/source_after/raw_hex",
                      "/output/shape", "/output/pixel_type", "/output/S",
                      "/output/raw_hex", "/output/spacing"]
    assert all(case["required_output_paths"] == axis_selectors
               for case in axis_contract["case_set"])
    axis_scope, = axis_contract["requirement_scopes"]
    assert axis_scope["requirement_id"] == "G029.axis_plane_export"
    assert len(axis_scope["required"]) == 84
    assert {(item["case_id"], item["path"]) for item in axis_scope["required"]} == {
        (case["case_id"], path) for case in axis_contract["case_set"] for path in axis_selectors
    }
    range_contract, = [row for row in documents["fixtures.json"]["fixture_contracts"]
                       if row["id"] == "FC.G029.integer-range.pixels"]
    assert range_contract["fixture_id"] == range_fixture["id"]
    assert range_contract["format"] == "imagej_axis_range_v1"
    assert range_contract["sha256"] == "ca8f0b49182f4f4dedcf0e9308641364c9d7977dafd743e592d8d72466e4c4e4"
    assert len(range_contract["case_set"]) == 48
    assert [case["case_id"] for case in range_contract["case_set"]] == list(RANGE_IDS)
    assert [case["locator"] for case in range_contract["case_set"]] == list(range(48))
    range_selectors = ["/source_before/raw_hex", "/source_after/raw_hex",
                       "/output/shape", "/output/pixel_type", "/output/S",
                       "/output/raw_hex", "/output/in_plane_spacing"]
    assert all(case["required_output_paths"] == range_selectors
               for case in range_contract["case_set"])
    range_scope, = range_contract["requirement_scopes"]
    assert range_scope["requirement_id"] == "G029.integer_range_reslice"
    assert len(range_scope["required"]) == 336
    assert [(item["case_id"], item["path"]) for item in range_scope["required"]] == [
        (case_id, path) for case_id in RANGE_IDS for path in range_selectors
    ]
    assert range_contract["capture"]["receipt"] == {
        "path": "build/plan-implementation/p05f-axis-plane-export-001/range-parity-adoption-001/reference-capture-receipt-001.json",
        "sha256": "01915c47228b6f53eb4c630143de1adbd97ce3a56478102e203b78ef77adb6f5",
        "bytes": 494359,
    }
    orientation_contract, = [row for row in documents["fixtures.json"]["fixture_contracts"]
                             if row["id"] == "FC.G029.axis-range-orientation.pixels"]
    assert orientation_contract["fixture_id"] == orientation_fixture["id"]
    assert orientation_contract["format"] == "imagej_axis_range_orientation_v1"
    assert orientation_contract["sha256"] == "d273ce54b83b94a008861c780876a9202fbde4a62237c0fd96a6a787bc3bed37"
    assert len(orientation_contract["case_set"]) == 96
    assert [case["case_id"] for case in orientation_contract["case_set"]] == list(ORIENTATION_IDS)
    assert [case["locator"] for case in orientation_contract["case_set"]] == list(range(96))
    assert all(case["required_output_paths"] == list(ORIENTATION_SELECTORS)
               for case in orientation_contract["case_set"])
    orientation_scope, = orientation_contract["requirement_scopes"]
    assert orientation_scope["requirement_id"] == "G029.axis_range_orientation"
    assert len(orientation_scope["required"]) == 672
    assert [(item["case_id"], item["path"]) for item in orientation_scope["required"]] == [
        (case_id, path) for case_id in ORIENTATION_IDS for path in ORIENTATION_SELECTORS
    ]
    assert orientation_contract["capture"]["receipt"] == {
        "path": "build/plan-implementation/p05f-axis-plane-export-001/next-range-orientation-formal-fixture-002/prepared-inputs-002/reference-capture-receipt-002.json",
        "sha256": "d78d8d5d38936e2856943db1edcabbcda86722ca5cd9e2a79d736508715abe72",
        "bytes": 496341,
    }
    adoption_raw = (ROOT / "build/plan-implementation/p05f-axis-plane-export-001/orientation-formal-fixture-adoption-001/adoption-001.json").read_bytes()
    assert len(adoption_raw) == 16729
    assert hashlib.sha256(adoption_raw).hexdigest() == "e6640510c5e4d043a82035d4c31a322bf9915fda550222c414bb70ddb0644411"
    adoption = json.loads(adoption_raw)
    assert adoption["run_id"] == "axis-range-orientation-p05h-windows-evidence-001"
    assert adoption["fixture_contract"] == orientation_contract["id"]
    assert adoption["fixture_proof"] == "FP.G029.axis_range_orientation.cpp_api"
    assert adoption["fixture_trace"] == "FT.G029.axis_range_orientation.cpp_api"
    assert adoption["fixture_evidence"]["required_case_assertions"] == adoption["fixture_evidence"]["verified_case_assertions"] == 1308
    orientation_run, = [row for row in inventory["verification_runs"]
                       if row["id"] == "axis-range-orientation-p05h-windows-evidence-001"]
    orientation_trace, = orientation_run["fixture_traces"]
    assert orientation_trace["id"] == "FT.G029.axis_range_orientation.cpp_api"
    assert orientation_trace["contract_id"] == orientation_contract["id"]
    assert orientation_trace["contract_sha256"] == orientation_contract["sha256"]
    assert {key: orientation_trace[key] for key in ("path", "sha256", "bytes")} == {
        "path": "build/plan-implementation/p05f-axis-plane-export-001/next-range-orientation-formal-fixture-002/execution-002/fixture-cpp_api.json",
        "sha256": "f0b01e5930b12657908ee74ec288869cc14e34b5eb57d8a3c71383d9b2dc8b05",
        "bytes": 4653758,
    }
    trace_raw = (ROOT / orientation_trace["path"]).read_bytes()
    assert len(trace_raw) == orientation_trace["bytes"]
    assert hashlib.sha256(trace_raw).hexdigest() == orientation_trace["sha256"]
    trace = json.loads(trace_raw)
    assert trace["run_id"] == orientation_run["id"]
    assert trace["contract_id"] == orientation_contract["id"] and trace["contract_sha256"] == orientation_contract["sha256"]
    assert trace["fixture_id"] == orientation_fixture["id"]
    assert trace["report_case_identity"] == orientation_trace["report_case_identity"] == {
        "suite": "pytest",
        "classname": "build.plan-implementation.p05f-axis-plane-export-001.next-range-orientation-formal-fixture-002.test_axis_range_orientation_fixture_evidence",
        "name": "test_fixture_exact[cpp_api]",
    }
    assert len(trace["comparisons"]) == 96
    assert [row["case_id"] for row in trace["comparisons"]] == list(ORIENTATION_IDS)
    physical_fixture, = [row for row in documents["fixtures.json"]["fixtures"]
                         if row["id"] == "F.G029.physical-spacing.owned142"]
    assert physical_fixture["requirement_ids"] == ["G029.physical_inplane_spacing"]
    assert physical_fixture["case_count"] == 142
    assert physical_fixture["capture_status"] == "generated"
    assert physical_fixture["match_status"] == "matched"
    assert physical_fixture["origin"] == "owned_deterministic"
    assert physical_fixture["reference_execution"] is False
    assert physical_fixture["verification_run_ids"] == []
    assert {key: physical_fixture[key] for key in ("path", "sha256", "bytes")} == {
        "path": "build/plan-implementation/p05f-axis-plane-export-001/physical-grid-owned-fixture-generation-001/owned142.json",
        "sha256": "38895184d3dad10e230e0d097a74f8e2ea568d7d5c7dd332761e5b6e1c725d4c",
        "bytes": 3710570,
    }
    physical_contract, = [row for row in documents["fixtures.json"]["fixture_contracts"]
                          if row["id"] == "FC.G029.physical-spacing.owned142"]
    assert physical_contract["fixture_id"] == physical_fixture["id"]
    assert physical_contract["format"] == "owned_cases_v1"
    assert physical_contract["sha256"] == "faeae35882cbd10cc404daf15cddb75b374de6faafcdfdad09e3a78910f3fb36"
    assert len(physical_contract["case_set"]) == 142
    assert [case["locator"] for case in physical_contract["case_set"]] == list(range(142))
    assert physical_contract["case_set"][0]["case_id"] == "spacing.primary.u8_s1.xz.nearest.identity"
    assert physical_contract["case_set"][-1]["case_id"] == "spacing.reject.linear_quiet_policy_admission"
    physical_common_selectors = ["/api_phase", "/outcome", "/exception_class", "/output_role"]
    physical_output_selectors = physical_common_selectors + [
        "/output/shape", "/output/pixel_type", "/output/sample_policy",
        "/output/stored_float64_le_sha256", "/output/grid",
        "/output/calibration/spacing_bits_hex", "/output/calibration/direction_bits_hex",
        "/output/calibration/frame_interval_bits_hex", "/output/calibration/time_origin_bits_hex",
        "/output/calibration/units", "/output/calibration/origin", "/output/world_shape",
        "/output/world_coordinates", "/output/provenance_fields",
    ]
    physical_no_output_selectors = physical_common_selectors + ["/output"]
    assert all(case["required_output_paths"] in (physical_output_selectors, physical_no_output_selectors)
               for case in physical_contract["case_set"])
    assert sum(case["required_output_paths"] == physical_output_selectors
               for case in physical_contract["case_set"]) == 128
    assert sum(case["required_output_paths"] == physical_no_output_selectors
               for case in physical_contract["case_set"]) == 14
    physical_scope, = physical_contract["requirement_scopes"]
    assert physical_scope["requirement_id"] == "G029.physical_inplane_spacing"
    assert len(physical_scope["required"]) == 2374
    assert [(item["case_id"], item["path"]) for item in physical_scope["required"]] == [
        (case["case_id"], path) for case in physical_contract["case_set"]
        for path in case["required_output_paths"]
    ]
    assert 128 * 18 + 14 * 5 == 2374
    assert physical_contract["capture"]["driver"] is None
    assert physical_contract["capture"]["runtime"]["kind"] == "owned_synthetic"
    assert physical_contract["capture"]["receipt"] == {
        "path": "build/plan-implementation/p05f-axis-plane-export-001/physical-grid-owned-fixture-generation-001/owned142-capture-receipt.json",
        "sha256": "5f615c62bf30961b10c19d005178c639c9443670a533348f5c40717954b78ca9",
        "bytes": 4707,
    }
    physical_run, = [row for row in inventory["verification_runs"]
                     if row["id"] == "physical-inplane-spacing-p05i-windows-fixture-001"]
    physical_binding, = [row for row in documents["commands.json"]["run_bindings"]
                         if row["run_id"] == physical_run["id"]]
    assert physical_binding["reference"] is None
    physical_trace, = physical_run["fixture_traces"]
    assert physical_trace["id"] == "FT.G029.physical_spacing.cpp_api"
    assert physical_trace["contract_id"] == physical_contract["id"]
    assert physical_trace["contract_sha256"] == physical_contract["sha256"]
    assert {key: physical_trace[key] for key in ("path", "sha256", "bytes")} == {
        "path": "build/plan-implementation/p05f-axis-plane-export-001/physical-grid-owned-fixture-formal-preparation-002/execution-001/fixture-cpp_api.json",
        "sha256": "53bca704b64fe7d5e53c9629354d9be7020ba6a04fe6e821f31496a0bb343051",
        "bytes": 10827777,
    }
    assert physical_trace["report_case_identity"] == {
        "suite": "pytest",
        "classname": "build.plan-implementation.p05f-axis-plane-export-001.physical-grid-owned-fixture-formal-preparation-002.test_physical_owned142_fixture_evidence",
        "name": "test_owned_cpp142_fixture_evidence",
    }
    assert result["capture_gaps"] == result["unavailable_evidence"] == []
    assert result["enumeration_gaps"] == [{
        "kind": "fixtures", "reason": "Fixture enumeration must be frozen and nonempty",
    }]
    assert documents == before_documents and inventory == before_inventory
    assert before_bytes == {path: (ROOT / path).read_bytes() for path in before_bytes}
    with pytest.raises(ValueError):
        fixtures.validate_fixtures(ROOT, documents, inventory, require_complete=True)



@pytest.mark.parametrize("replacement", [True, 0, 1.0, "1", None])
def test_owned_adapter_rejects_malformed_schema_version(replacement):
    with pytest.raises(ValueError):
        fixtures.project_cases("owned_cases_v1", {"schema_version": replacement, "cases": []})


@pytest.mark.parametrize("replacement", [True, "0", None, float("nan"), float("inf"), -float("inf")])
def test_binary_adapter_rejects_nonintegral_or_nonfinite_pixel_values(replacement):
    corpus = json.loads((ROOT / "tests/fixtures/binary-imagej154r.json").read_text(encoding="utf-8"))
    corpus["cases"] = [deepcopy(corpus["cases"][0])]
    corpus["cases"][0]["pixels"][0] = replacement
    with pytest.raises(ValueError):
        fixtures.project_cases("imagej_binary_processor_v1", corpus)


@pytest.mark.parametrize("field,replacement", [
    ("shape", [0, 9]), ("shape", [7, True]), ("shape", [7]),
    ("source", [0]), ("operation", "unreviewed"), ("iterations", True),
    ("neighbors", 1.5), ("black_background", 0), ("pad_edges", "false"),
])
def test_binary_adapter_rejects_invalid_dimensions_options_and_boolean_coercion(field, replacement):
    corpus = json.loads((ROOT / "tests/fixtures/binary-imagej154r.json").read_text(encoding="utf-8"))
    corpus["cases"] = [deepcopy(corpus["cases"][0])]
    corpus["cases"][0][field] = replacement
    with pytest.raises(ValueError):
        fixtures.project_cases("imagej_binary_processor_v1", corpus)


def test_owned_adapter_hash_input_and_expected_cover_unselected_fields():
    corpus = {"schema_version": 1, "cases": [{"input": {"order": ["a", "b"], "flag": True},
              "expected": {"pixels": [1, 2], "unselected": "keep"}, "metadata": "keep"}]}
    actual = fixtures.project_cases("owned_cases_v1", corpus)
    assert actual == [{"locator": 0, "input": corpus["cases"][0]["input"],
                       "expected": corpus["cases"][0]["expected"]}]
    changed = deepcopy(corpus)
    changed["cases"][0]["input"]["order"].reverse()
    assert fingerprint(fixtures.project_cases("owned_cases_v1", changed)[0]["input"]) != fingerprint(actual[0]["input"])


@pytest.mark.parametrize("mutation", ["locator", "reorder", "input", "expected", "duplicate", "scope", "selector"])
def test_contract_rejects_wrong_projection_order_or_child_selector_scope(tmp_path, mutation):
    graph = owned_set(tmp_path)
    contract = first_contract(graph)
    if mutation == "locator":
        contract["case_set"][0]["locator"] = 1
    elif mutation == "reorder":
        contract["case_set"].reverse()
    elif mutation in {"input", "expected"}:
        contract["case_set"][0][mutation + "_sha256"] = "0" * 64
    elif mutation == "duplicate":
        contract["case_set"][1]["case_id"] = contract["case_set"][0]["case_id"]
    elif mutation == "scope":
        contract["requirement_scopes"][0]["required"].pop()
    else:
        contract["case_set"][0]["required_output_paths"] = ["/missing"]
    refresh_contract(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("key", ["fixture_evidence_schema_version", "fixture_contracts", "fixture_proofs"])
def test_additive_extension_is_all_or_none(tmp_path, key):
    graph = owned_set(tmp_path)
    del graph[1]["fixtures.json"][key]
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("value", [True, 1.0, "1", 0, 2])
def test_additive_extension_rejects_boolean_or_unknown_version(tmp_path, value):
    graph = owned_set(tmp_path)
    graph[1]["fixtures.json"]["fixture_evidence_schema_version"] = value
    with pytest.raises(ValueError):
        validate(graph)


def test_absent_legacy_extension_retains_obligations(tmp_path):
    graph = owned_set(tmp_path)
    manifest = graph[1]["fixtures.json"]
    for key in ("fixture_evidence_schema_version", "fixture_contracts", "fixture_proofs"):
        del manifest[key]
    graph[2]["verification_runs"][0].pop("fixture_traces")
    graph[2]["verification_runs"][0].pop("fixture_evidence_schema_version")
    rebind(graph)
    result = validate(graph)
    assert result["fixtures_required"] == result["contracts_required"] == 3
    assert len(result["missing_contracts"]) == 3
    assert result["verified_case_assertions"] == 0
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


def test_changed_present_corpus_bytes_always_reject_even_without_proofs(tmp_path):
    graph = owned_set(tmp_path)
    fixture = graph[1]["fixtures.json"]["fixtures"][0]
    (graph[0] / fixture["path"]).write_bytes(b"{}\n")
    with pytest.raises(ValueError):
        validate(graph)


def refresh_capture(graph, index=0):
    """Rebind independently edited unit evidence so failures reach the target rule."""
    contract = first_contract(graph, index)
    capture = contract["capture"]
    receipt = read_json(graph[0], capture["receipt"])
    for key in ("raw_output", "generator_sources", "reference_archive",
                "reference_members", "driver", "runtime"):
        if key in capture:
            receipt[key] = deepcopy(capture[key])
        else:
            receipt.pop(key, None)
    rewrite_json(graph[0], capture["receipt"], receipt)
    update_trace(graph, index, lambda trace: trace.update(capture_receipt=deepcopy(capture["receipt"])))
    refresh_contract(graph, index)


def replace_corpus_bytes(graph, payload, index=0):
    fixture = graph[1]["fixtures.json"]["fixtures"][index]
    (graph[0] / fixture["path"]).write_bytes(payload)
    fixture["sha256"] = digest(payload)
    contract = first_contract(graph, index)
    contract["fixture_snapshot"]["sha256"] = fixture["sha256"]
    receipt = read_json(graph[0], contract["capture"]["receipt"])
    receipt["fixture_sha256"] = fixture["sha256"]
    rewrite_json(graph[0], contract["capture"]["receipt"], receipt)
    update_trace(graph, index, lambda trace: trace.update(fixture_sha256=fixture["sha256"]))
    refresh_capture(graph, index)


def evidence_pin(graph, role):
    contract = first_contract(graph)
    capture = contract["capture"]
    entry = graph[2]["verification_runs"][0]["fixture_traces"][0]
    if role == "fixture":
        return graph[1]["fixtures.json"]["fixtures"][0]
    if role == "generator":
        return capture["generator_sources"][0]
    if role == "helper":
        return capture["generator_sources"][1]
    if role == "runtime":
        return capture["runtime"]["executables"][0]
    if role == "runtime_source":
        return capture["runtime"]["sources"][1]
    if role == "report":
        return graph[1]["commands.json"]["run_bindings"][0]["reports"][0]
    if role == "producer_source":
        return entry["producer"]["sources"][0]
    if role == "producer_artifact":
        return entry["producer"]["artifacts"][0]
    if role == "trace":
        return entry
    return capture[role]


def test_complete_owned_set_recomputes_twelve_assertions_without_mutation(tmp_path):
    graph = owned_set(tmp_path)
    root, documents, inventory = graph
    before_documents, before_inventory = deepcopy(documents), deepcopy(inventory)
    before_files = {path.relative_to(root).as_posix(): path.read_bytes()
                    for path in root.rglob("*") if path.is_file()}
    result = validate(graph, require_evidence=True, require_complete=True)
    assert {key: result[key] for key in COUNTS} == {
        "fixtures_declared": 3, "fixtures_required": 3, "content_files_verified": 3,
        "contracts_required": 3, "contracts_validated": 3,
        "required_case_assertions": 12, "verified_case_assertions": 12,
    }
    assert all(result[key] == [] for key in GAPS)
    assert all(type(result[key]) is int for key in COUNTS)
    assert documents == before_documents and inventory == before_inventory
    assert before_files == {path.relative_to(root).as_posix(): path.read_bytes()
                            for path in root.rglob("*") if path.is_file()}
    assert all(row["license"]["decision"] == "pending" for row in documents["fixtures.json"]["fixtures"])
    assert all(row["commercial"]["decision"] == "pending" for row in documents["commands.json"]["requirements"])


def test_overlapping_child_scopes_do_not_certify_unselected_metadata(tmp_path):
    graph = owned_set(tmp_path)
    contract = first_contract(graph, 1)
    assert len(contract["requirement_scopes"]) == 2
    assert len(contract["requirement_scopes"][0]["required"]) == 4
    assert len(contract["requirement_scopes"][1]["required"]) == 2
    update_trace(graph, 1, lambda trace: trace["comparisons"][0]["observed"].update(annotation="different"))
    result = validate(graph, require_complete=True)
    assert result["required_case_assertions"] == result["verified_case_assertions"] == 12


@pytest.mark.parametrize("role", [
    "fixture", "generator", "helper", "runtime", "runtime_source", "reference_archive",
    "receipt", "raw_output", "report", "producer_source", "producer_artifact", "trace",
])
def test_each_missing_evidence_file_retains_gaps_and_rejects_required_modes(tmp_path, role):
    graph = owned_set(tmp_path)
    (graph[0] / evidence_pin(graph, role)["path"]).unlink()
    result = validate(graph)
    assert any(result[key] for key in GAPS)
    assert result["fixtures_required"] == result["contracts_required"] == 3
    assert result["verified_case_assertions"] < result["required_case_assertions"]
    for options in ({"require_evidence": True}, {"require_complete": True}):
        with pytest.raises(ValueError):
            validate(graph, **options)


@pytest.mark.parametrize("role", [
    "generator", "helper", "runtime", "reference_archive", "receipt", "raw_output",
    "report", "producer_source", "producer_artifact", "trace",
])
def test_present_changed_evidence_bytes_never_become_historical_gaps(tmp_path, role):
    graph = owned_set(tmp_path)
    target = graph[0] / evidence_pin(graph, role)["path"]
    target.write_bytes(target.read_bytes() + b"\n")
    with pytest.raises(ValueError):
        validate(graph)


def test_captured_matched_labels_cannot_replace_a_missing_proof(tmp_path):
    graph = owned_set(tmp_path)
    graph[1]["fixtures.json"]["fixture_proofs"].pop(0)
    result = validate(graph)
    assert result["required_case_assertions"] == 12
    assert result["verified_case_assertions"] == 10
    assert len(result["missing_case_assertions"]) == 2
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("key", ["receipt", "raw_output", "generator_sources", "reference_archive",
                                 "reference_members", "driver", "runtime"])
def test_capture_role_omission_cannot_certify_complete_set(tmp_path, key):
    graph = owned_set(tmp_path)
    del first_contract(graph)["capture"][key]
    refresh_contract(graph)
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("field,value", [
    ("exit_code", 1), ("exit_code", True), ("exit_code", 0.0),
    ("reference_errors", 1), ("reference_errors", False),
    ("case_count", 1), ("case_count", True), ("fixture_sha256", "0" * 64),
    ("schema_version", True),
])
def test_capture_receipt_rejects_failed_boolean_or_inconsistent_results(tmp_path, field, value):
    graph = owned_set(tmp_path)
    capture = first_contract(graph)["capture"]
    receipt = read_json(graph[0], capture["receipt"])
    receipt[field] = value
    rewrite_json(graph[0], capture["receipt"], receipt)
    update_trace(graph, 0, lambda trace: trace.update(capture_receipt=deepcopy(capture["receipt"])))
    refresh_contract(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("mutation", ["empty", "reorder", "truncate", "duplicate", "input", "expected", "error", "malformed"])
def test_capture_raw_output_is_reconciled_as_full_ordered_cases(tmp_path, mutation):
    graph = owned_set(tmp_path)
    raw = first_contract(graph)["capture"]["raw_output"]
    rows = [json.loads(line) for line in (graph[0] / raw["path"]).read_bytes().splitlines()]
    if mutation == "empty":
        rows = []
    elif mutation == "reorder":
        rows.reverse()
    elif mutation == "truncate":
        rows.pop()
    elif mutation == "duplicate":
        rows[1] = deepcopy(rows[0])
    elif mutation == "input":
        rows[0]["input"]["operation"] = "different"
    elif mutation == "expected":
        rows[0]["expected"]["pixels"][0] = 99
    elif mutation == "error":
        rows[0]["reference_error"] = "Unhandled producer error"
    payload = b"{" if mutation == "malformed" else b"".join(json_bytes(row) for row in rows)
    (graph[0] / raw["path"]).write_bytes(payload)
    raw["sha256"] = digest(payload)
    refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_local_capture_import_cannot_be_omitted_from_generator_sources(tmp_path):
    graph = owned_set(tmp_path)
    first_contract(graph)["capture"]["generator_sources"].pop()
    refresh_capture(graph)
    assert validate(graph)["capture_gaps"]
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("role", ["executables", "sources"])
def test_owned_runtime_requires_nonempty_explicit_execution_and_source_pins(tmp_path, role):
    graph = owned_set(tmp_path)
    first_contract(graph)["capture"]["runtime"][role] = []
    refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("mutation", ["case_missing", "case_extra", "case_duplicate", "reorder", "input", "pixels", "missing_output", "extra_output", "dimensions"])
def test_actual_trace_observations_cannot_be_replaced_by_counts_or_claimed_match(tmp_path, mutation):
    graph = owned_set(tmp_path)
    def change(trace):
        rows = trace["comparisons"]
        trace["matched_cases"] = 2
        trace["matched"] = True
        if mutation == "case_missing":
            rows.pop()
        elif mutation == "case_extra":
            rows.append({**deepcopy(rows[0]), "case_id": "extra"})
        elif mutation == "case_duplicate":
            rows[1] = deepcopy(rows[0])
        elif mutation == "reorder":
            rows.reverse()
        elif mutation == "input":
            rows[0]["input_sha256"] = "0" * 64
        elif mutation == "pixels":
            rows[0]["observed"]["pixels"][0] = 17
        elif mutation == "missing_output":
            rows[0]["observed"].pop("pixels")
        elif mutation == "extra_output":
            rows[0]["observed"]["other"] = 17
        else:
            rows[0]["observed"]["pixels"].pop()
    update_trace(graph, 0, change)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("replacement", [True, "255", None, [255], {"number": 255}])
def test_exact_comparison_never_coerces_booleans_strings_or_container_types(tmp_path, replacement):
    graph = owned_set(tmp_path)
    update_trace(graph, 0, lambda trace: trace["comparisons"][0]["observed"]["pixels"].__setitem__(0, replacement))
    with pytest.raises(ValueError):
        validate(graph)


def test_exact_numeric_comparison_accepts_equal_finite_legacy_integer_floats(tmp_path):
    graph = owned_set(tmp_path)
    update_trace(graph, 0, lambda trace: trace["comparisons"][0]["observed"].update(pixels=[255.0, 0.0, 7.0]))
    assert validate(graph, require_complete=True)["verified_case_assertions"] == 12


def test_float_tolerance_is_inclusive_at_absolute_negative_and_relative_boundaries(tmp_path):
    graph = owned_set(tmp_path)
    def change(trace):
        for row, observed in zip(trace["comparisons"], [0.25, -6.75, 36.25]):
            row["observed"]["value"] = observed
    update_trace(graph, 2, change)
    assert validate(graph, require_complete=True)["verified_case_assertions"] == 12


@pytest.mark.parametrize("index,boundary", [(0, 0.25), (1, -6.75), (2, 36.25)])
def test_float_tolerance_rejects_the_first_float_beyond_each_boundary(tmp_path, index, boundary):
    graph = owned_set(tmp_path)
    observed = math.nextafter(boundary, math.inf)
    update_trace(graph, 2, lambda trace: trace["comparisons"][index]["observed"].update(value=observed))
    with pytest.raises(ValueError):
        validate(graph)


def test_float_relative_tolerance_uses_reference_magnitude_without_symmetric_expansion(tmp_path):
    graph = owned_set(tmp_path)
    # Difference 4.5 exceeds .25+.125*32=4.25; a max-magnitude rule permits it.
    update_trace(graph, 2, lambda trace: trace["comparisons"][2]["observed"].update(value=36.5))
    with pytest.raises(ValueError):
        validate(graph)


def test_float_contract_preserves_exact_metadata_comparison(tmp_path):
    graph = owned_set(tmp_path)
    update_trace(graph, 2, lambda trace: trace["comparisons"][0]["observed"].update(label="different"))
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("replacement", [True, "0.0", None, [], {}])
def test_float_comparison_rejects_coercion_and_type_substitution(tmp_path, replacement):
    graph = owned_set(tmp_path)
    update_trace(graph, 2, lambda trace: trace["comparisons"][0]["observed"].update(value=replacement))
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("field,value", [
    ("atol", True), ("rtol", "0.125"), ("atol", -1),
    ("numeric_paths", []), ("numeric_paths", ["/label"]),
    ("numeric_paths", ["/missing"]), ("numeric_paths", ["/value", "/value"]),
    ("units", ""), ("rationale", ""), ("nonfinite", "allow"),
])
def test_float_contract_requires_reviewed_numeric_selectors_and_finite_tolerances(tmp_path, field, value):
    graph = owned_set(tmp_path)
    first_contract(graph, 2)["comparison"][field] = value
    refresh_contract(graph, 2)
    with pytest.raises(ValueError):
        validate(graph)


def test_exact_contract_cannot_acquire_a_tolerance_or_numeric_selector(tmp_path):
    graph = owned_set(tmp_path)
    first_contract(graph)["comparison"]["atol"] = 0.01
    refresh_contract(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("payload", [
    b"\xff", b"{", b'{"schema_version":1,"schema_version":1,"cases":[]}',
    b'{"schema_version":1,"cases":[{"input":{},"expected":{"value":NaN}}]}',
    b'{"schema_version":1,"cases":[{"input":{},"expected":{"value":Infinity}}]}',
    b'{"schema_version":1,"cases":[{"input":{},"expected":{"value":1e400}}]}',
])
def test_strict_corpus_content_rejects_utf8_duplicate_keys_and_nonfinite_json(tmp_path, payload):
    graph = owned_set(tmp_path)
    replace_corpus_bytes(graph, payload)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("role", ["receipt", "trace", "raw_output"])
@pytest.mark.parametrize("token", ["NaN", "Infinity", "1e400"])
def test_receipt_raw_and_trace_json_are_strictly_finite(tmp_path, role, token):
    graph = owned_set(tmp_path)
    identity = evidence_pin(graph, role)
    payload = (graph[0] / identity["path"]).read_bytes().rstrip()
    # Add a nonfinite annotation to otherwise valid JSON; unknown metadata is not a bypass.
    if role == "raw_output":
        lines = payload.splitlines()
        lines[0] = lines[0][:-1] + (',"nonfinite":' + token + '}').encode()
        payload = b"\n".join(lines) + b"\n"
    else:
        payload = payload[:-1] + (',"nonfinite":' + token + '}\n').encode()
    (graph[0] / identity["path"]).write_bytes(payload)
    identity["sha256"] = digest(payload)
    if role == "trace":
        rebind(graph)
    elif role == "receipt":
        update_trace(graph, 0, lambda trace: trace.update(capture_receipt=deepcopy(identity)))
        refresh_contract(graph)
    else:
        refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("path", ["/absolute.json", "../outside.json", "build/../outside.json",
                                 "build\\trace.json", "C:/outside.json", "build//trace.json"])
def test_contract_pins_reject_unsafe_workspace_paths(tmp_path, path):
    graph = owned_set(tmp_path)
    first_contract(graph)["capture"]["raw_output"]["path"] = path
    refresh_contract(graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_resolved_pin_escape_rejects_even_with_lexically_safe_path(tmp_path, monkeypatch):
    graph = owned_set(tmp_path)
    lexical = graph[0] / first_contract(graph)["capture"]["raw_output"]["path"]
    outside = tmp_path / "outside.jsonl"
    outside.write_bytes(lexical.read_bytes())
    original_resolve = Path.resolve
    # Model symlink resolution independently of Windows symlink privileges.
    def resolved(path, *args, **kwargs):
        return outside if path == lexical else original_resolve(path, *args, **kwargs)
    monkeypatch.setattr(Path, "resolve", resolved)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("method", ["geometry", "statistical", "interaction"])
def test_unsupported_comparison_methods_remain_explicit_certification_gaps(tmp_path, method):
    graph = owned_set(tmp_path)
    fixture = graph[1]["fixtures.json"]["fixtures"][0]
    fixture["comparison"]["method"] = method
    contract = first_contract(graph)
    contract["fixture_snapshot"]["comparison"]["method"] = method
    contract["comparison"]["method"] = method
    refresh_contract(graph)
    result = validate(graph)
    assert result["unsupported_methods"]
    assert result["verified_case_assertions"] < result["required_case_assertions"]
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


def test_unknown_adapter_remains_gap_and_cannot_use_dynamic_comparator(tmp_path):
    graph = owned_set(tmp_path)
    first_contract(graph)["format"] = "unreviewed_dynamic_format"
    refresh_contract(graph)
    assert validate(graph)["unsupported_methods"]
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("enumeration", ["pending", "partial"])
def test_nonempty_complete_evidence_does_not_close_fixture_enumeration(tmp_path, enumeration):
    graph = owned_set(tmp_path)
    graph[1]["fixtures.json"]["enumeration_status"] = enumeration
    graph[1]["fixtures.json"]["remaining"] = ["Additional fixtures required."]
    result = validate(graph)
    assert result["verified_case_assertions"] == 12
    assert result["enumeration_gaps"]
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


def test_empty_fixture_enumeration_cannot_be_complete(tmp_path):
    graph = owned_set(tmp_path)
    graph[1]["fixtures.json"].update(fixtures=[], fixture_contracts=[], fixture_proofs=[])
    graph[1]["commands.json"].update(requirements=[], run_bindings=[])
    graph[2]["verification_runs"] = []
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("mutation", ["unknown_contract", "duplicate_contract", "unknown_fixture",
                                    "duplicate_proof", "unknown_run", "unknown_trace", "stale_contract"])
def test_unknown_duplicate_and_stale_extension_identities_reject(tmp_path, mutation):
    graph = owned_set(tmp_path)
    manifest = graph[1]["fixtures.json"]
    if mutation == "unknown_contract":
        manifest["fixture_proofs"][0]["contract_id"] = "FC.unknown"
    elif mutation == "duplicate_contract":
        manifest["fixture_contracts"].append(deepcopy(first_contract(graph)))
    elif mutation == "unknown_fixture":
        first_contract(graph)["fixture_id"] = "F.unknown"
        refresh_contract(graph)
    elif mutation == "duplicate_proof":
        manifest["fixture_proofs"].append(deepcopy(manifest["fixture_proofs"][0]))
    elif mutation == "unknown_run":
        manifest["fixture_proofs"][0]["run_id"] = "unknown-run"
    elif mutation == "unknown_trace":
        manifest["fixture_proofs"][0]["trace_id"] = "FT.unknown"
    else:
        first_contract(graph)["comparison"]["rationale"] = "Changed after binding."
        first_contract(graph)["sha256"] = contract_digest(first_contract(graph))
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("field,value", [
    ("contract_sha256", "0" * 64), ("fixture_sha256", "0" * 64),
    ("run_id", "unknown-run"), ("producer_id", "producer.other"),
    ("report_role", "native"),
    ("report_case_identity", {"suite": "owned", "classname": "owned.fixture", "name": "metadata"}),
    ("schema_version", True),
])
def test_trace_identity_must_match_real_bound_producer_and_report_case(tmp_path, field, value):
    graph = owned_set(tmp_path)
    update_trace(graph, 0, lambda trace: trace.update({field: value}))
    with pytest.raises(ValueError):
        validate(graph)


def test_bound_run_fingerprint_cannot_be_silently_reseeded(tmp_path):
    graph = owned_set(tmp_path)
    graph[2]["verification_runs"][0]["scope"] = "Changed accepted run."
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("outcome", ["failure", "error", "skipped"])
def test_rebound_report_failure_error_or_skip_never_qualifies_comparison(tmp_path, outcome):
    graph = owned_set(tmp_path)
    report = graph[1]["commands.json"]["run_bindings"][0]["reports"][0]
    payload = (
        '<testsuites><testsuite name="owned" tests="1" failures="' + str(int(outcome == "failure"))
        + '" errors="' + str(int(outcome == "error")) + '" skipped="' + str(int(outcome == "skipped"))
        + '"><testcase classname="owned.fixture" name="compare"><' + outcome
        + '/></testcase></testsuite></testsuites>'
    ).encode()
    (graph[0] / report["path"]).write_bytes(payload)
    report["sha256"] = digest(payload)
    with pytest.raises(ValueError):
        validate(graph)


def binary_adapter_set(tmp_path):
    """Owned binary-shaped data exercises driver/Java identity, without execution."""
    cases = [
        {"input": {"operation": "erode", "iterations": 1, "neighbors": 1,
                   "black_background": True, "pad_edges": False, "shape": [1, 2],
                   "pattern": 0, "source": [0, 255]}, "expected": {"pixels": [0, 0]}},
        {"input": {"operation": "dilate", "iterations": 1, "neighbors": 1,
                   "black_background": True, "pad_edges": True, "shape": [1, 2],
                   "pattern": 1, "source": [7, 255]}, "expected": {"pixels": [255, 255]}},
    ]
    graph = owned_set(tmp_path, [("pixels", cases, ["/pixels"], False)])
    root, documents, _ = graph
    fixture = documents["fixtures.json"]["fixtures"][0]
    child = documents["commands.json"]["requirements"][0]
    contract = first_contract(graph)
    capture = contract["capture"]
    member_path = "ImageJ-" + REVISION + "/ij/process/Owned.java"
    member_bytes = b"// Project-owned reference member.\n"
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr(zipfile.ZipInfo(member_path, (2026, 1, 1, 0, 0, 0)), member_bytes)
    archive_pin = pin(root, "owned/java-reference.zip", buffer.getvalue())
    members = [{"path": member_path, "sha256": digest(member_bytes)}]
    documents["references.json"]["imagej"]["sha256"] = archive_pin["sha256"]
    fixture["reference"].update(archive_sha256=archive_pin["sha256"], source_members=deepcopy(members))
    child["reference"] = deepcopy(fixture["reference"])
    driver_text = "public class OwnedDriver {\n}\n"
    driver_sha = digest(driver_text.encode("utf-8"))
    generator_bytes = (
        "from owned_capture_helper import identity\nCOMMIT = " + repr(REVISION)
        + "\nIMAGEJ_SHA = " + repr(archive_pin["sha256"])
        + "\nDRIVER = " + repr(driver_text) + "\n"
    ).encode("utf-8")
    generator = pin(root, "scripts/capture_binary_reference.py", generator_bytes)
    fixture["generator"] = {**generator, "driver_sha256": driver_sha}
    capture["generator_sources"][0] = deepcopy(generator)
    capture["reference_archive"] = archive_pin
    capture["reference_members"] = deepcopy(members)
    capture["driver"] = {
        "text_sha256": driver_sha,
        "artifact": pin(root, "build/owned/OwnedDriver.java", driver_text.replace("\n", "\r\n").encode()),
    }
    capture["runtime"] = {
        "kind": "java", "version": "owned-java-runtime-1",
        "executables": [pin(root, "owned/java.bin", b"Owned java binary.\n"),
                        pin(root, "owned/javac.bin", b"Owned javac binary.\n")],
        "classpath": [pin(root, "owned/reference.jar", b"Owned reference classpath.\n")],
        "classes": [pin(root, "owned/OwnedDriver.class", b"Owned class bytes.\n")],
        "resources": [pin(root, "owned/resource.bin", b"Owned resource bytes.\n")],
    }
    corpus = {
        "schema_version": 1, "imagej_commit": REVISION,
        "source_archive_sha256": archive_pin["sha256"], "scope": fixture["reference"]["scope"],
        "generator": generator["path"], "source_member": member_path,
        "source_member_sha256": digest(member_bytes), "driver_sha256": driver_sha,
        "runtime_version": capture["runtime"]["version"],
        "cases": [{**row["input"], **row["expected"]} for row in cases],
    }
    raw_bytes = b"".join(json_bytes(row) for row in corpus["cases"])
    capture["raw_output"] = pin(root, capture["raw_output"]["path"], raw_bytes)
    contract["format"] = "imagej_binary_processor_v1"
    fixture["sha256"] = digest(json_bytes(corpus))
    contract["fixture_snapshot"] = independent_fixture_snapshot(fixture, [child])
    contract["requirement_scopes"][0]["snapshot"] = child_snapshot(child)
    replace_corpus_bytes(graph, json_bytes(corpus))
    return graph


def test_imagej_shaped_owned_capture_keeps_embedded_text_and_driver_bytes_separate(tmp_path):
    graph = binary_adapter_set(tmp_path)
    driver = first_contract(graph)["capture"]["driver"]
    assert driver["text_sha256"] != driver["artifact"]["sha256"]
    result = validate(graph, require_evidence=True, require_complete=True)
    assert result["required_case_assertions"] == result["verified_case_assertions"] == 2
    assert all(result[key] == [] for key in GAPS)


@pytest.mark.parametrize("mutation", ["text_as_raw_hash", "wrong_text", "wrong_runtime", "owned_runtime"])
def test_driver_and_executed_runtime_identities_cannot_be_substituted(tmp_path, mutation):
    graph = binary_adapter_set(tmp_path)
    capture = first_contract(graph)["capture"]
    if mutation == "text_as_raw_hash":
        capture["driver"]["artifact"]["sha256"] = capture["driver"]["text_sha256"]
    elif mutation == "wrong_text":
        capture["driver"]["text_sha256"] = "0" * 64
    elif mutation == "wrong_runtime":
        capture["runtime"]["version"] = "different runtime"
    else:
        capture["runtime"]["kind"] = "owned_synthetic"
    refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("role", ["executables", "classpath", "classes", "resources"])
def test_java_runtime_declared_pins_are_required_exact_bytes(tmp_path, role):
    graph = binary_adapter_set(tmp_path)
    runtime_pin = first_contract(graph)["capture"]["runtime"][role][0]
    (graph[0] / runtime_pin["path"]).unlink()
    assert validate(graph)["unavailable_evidence"]
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("role", ["executables", "classpath", "classes"])
def test_java_runtime_required_arrays_cannot_be_empty(tmp_path, role):
    graph = binary_adapter_set(tmp_path)
    first_contract(graph)["capture"]["runtime"][role] = []
    refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_java_resources_can_be_explicitly_empty(tmp_path):
    graph = binary_adapter_set(tmp_path)
    first_contract(graph)["capture"]["runtime"]["resources"] = []
    refresh_capture(graph)
    assert validate(graph, require_complete=True)["verified_case_assertions"] == 2


@pytest.mark.parametrize("mutation", ["missing", "extra", "hash", "wrong_prefix"])
def test_reference_capture_requires_the_complete_exact_member_pin_set(tmp_path, mutation):
    graph = binary_adapter_set(tmp_path)
    members = first_contract(graph)["capture"]["reference_members"]
    if mutation == "missing":
        members.clear()
    elif mutation == "extra":
        members.append({"path": "unreviewed/Other.java", "sha256": "0" * 64})
    elif mutation == "hash":
        members[0]["sha256"] = "0" * 64
    else:
        members[0]["path"] = "ImageJ-" + "b" * 40 + "/ij/process/Owned.java"
    refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_pinned_archive_member_content_is_checked_beyond_archive_hash(tmp_path):
    graph = owned_set(tmp_path)
    archive_pin = first_contract(graph)["capture"]["reference_archive"]
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        archive.writestr("owned/reference.py", b"Different member bytes.\n")
    payload = buffer.getvalue()
    (graph[0] / archive_pin["path"]).write_bytes(payload)
    archive_sha = digest(payload)
    graph[1]["references.json"]["imagej"]["sha256"] = archive_sha
    for fixture in graph[1]["fixtures.json"]["fixtures"]:
        fixture["reference"]["archive_sha256"] = archive_sha
    for child in graph[1]["commands.json"]["requirements"]:
        child["reference"]["archive_sha256"] = archive_sha
    children = graph[1]["commands.json"]["requirements"]
    for index, contract in enumerate(graph[1]["fixtures.json"]["fixture_contracts"]):
        fixture = graph[1]["fixtures.json"]["fixtures"][index]
        contract["fixture_snapshot"] = independent_fixture_snapshot(fixture, children)
        by_id = {child["id"]: child for child in children}
        for scope in contract["requirement_scopes"]:
            scope["snapshot"] = child_snapshot(by_id[scope["requirement_id"]])
        contract["capture"]["reference_archive"]["sha256"] = archive_sha
        refresh_capture(graph, index)
    with pytest.raises(ValueError, match="member"):
        validate(graph)


@pytest.mark.parametrize("pointer", ["/style/rgba/01", "/style/rgba/-1", "/style/rgba/4", "/style/~2", "/STYLE", "/style/"])
def test_json_pointer_requires_exact_keys_valid_escapes_and_canonical_indices(tmp_path, pointer):
    graph = owned_set(tmp_path)
    contract = first_contract(graph, 1)
    contract["case_set"][0]["required_output_paths"][0] = pointer
    contract["requirement_scopes"][0]["required"][0]["path"] = pointer
    contract["requirement_scopes"][1]["required"][0]["path"] = pointer
    refresh_contract(graph, 1)
    with pytest.raises(ValueError):
        validate(graph)


def test_json_pointer_supports_rfc6901_escaped_keys(tmp_path):
    cases = [{"input": {"name": "escaped"}, "expected": {"a/b": {"~key": [5, 9]}}}]
    graph = owned_set(tmp_path, [("escape", cases, ["/a~1b/~0key/1"], False)])
    assert validate(graph, require_complete=True)["verified_case_assertions"] == 1


def test_finite_values_with_overflowing_comparison_arithmetic_are_rejected(tmp_path):
    cases = [{"input": {"sample": "overflow"}, "expected": {"value": -1e308, "label": "overflow"}}]
    graph = owned_set(tmp_path, [("float", cases, ["/value", "/label"], True)])
    update_trace(graph, 0, lambda trace: trace["comparisons"][0]["observed"].update(value=1e308))
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("mutation", ["fixture", "child_link", "scope", "snapshot"])
def test_required_fixture_and_child_obligations_cannot_disappear(tmp_path, mutation):
    graph = owned_set(tmp_path)
    if mutation == "fixture":
        graph[1]["fixtures.json"]["fixtures"].pop(0)
    elif mutation == "child_link":
        graph[1]["commands.json"]["requirements"][0]["fixture_ids"] = []
    elif mutation == "scope":
        first_contract(graph, 1)["requirement_scopes"].pop()
        refresh_contract(graph, 1)
    else:
        graph[1]["commands.json"]["requirements"][0]["contract"]["targets"].append("selected_plane")
    with pytest.raises(ValueError):
        validate(graph)


def test_each_proof_must_link_all_children_of_its_frozen_scope(tmp_path):
    graph = owned_set(tmp_path)
    graph[1]["commands.json"]["requirements"][2]["verification_run_ids"] = []
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("role", ["sources", "artifacts"])
def test_producer_requires_nonempty_unique_sources_and_artifacts(tmp_path, role):
    graph = owned_set(tmp_path)
    graph[2]["verification_runs"][0]["fixture_traces"][0]["producer"][role] = []
    rebind(graph)
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("value", [True, 7, None, ["ij/process/Owned.java"]])
def test_embedded_source_member_metadata_requires_string_before_prefix_normalization(tmp_path, value):
    graph = binary_adapter_set(tmp_path)
    fixture = graph[1]["fixtures.json"]["fixtures"][0]
    corpus = read_json(graph[0], fixture)
    corpus["source_member"] = value
    replace_corpus_bytes(graph, json_bytes(corpus))
    with pytest.raises(ValueError):
        validate(graph)


@pytest.mark.parametrize("pointer", ["no-leading-slash", "/pixels~2", "/pixels/~"])
def test_invalid_pointer_syntax_rejects_even_when_fixture_content_is_unavailable(tmp_path, pointer):
    graph = owned_set(tmp_path)
    contract = first_contract(graph)
    contract["case_set"][0]["required_output_paths"] = [pointer]
    contract["requirement_scopes"][0]["required"][0]["path"] = pointer
    refresh_contract(graph)
    (graph[0] / graph[1]["fixtures.json"]["fixtures"][0]["path"]).unlink()
    with pytest.raises(ValueError):
        validate(graph)


def relocate_capture_helper(graph, destination):
    """Move equal bytes and every declared pin, without altering import syntax."""
    root, documents, _ = graph
    helper = first_contract(graph)["capture"]["generator_sources"][1]
    original_path = helper["path"]
    relocated = pin(root, destination, (root / original_path).read_bytes())
    (root / original_path).unlink()
    children = documents["commands.json"]["requirements"]
    for index, contract in enumerate(documents["fixtures.json"]["fixture_contracts"]):
        capture = contract["capture"]
        for identities in (capture["generator_sources"], capture["runtime"]["sources"]):
            for identity in identities:
                if identity["path"] == original_path:
                    identity.update(deepcopy(relocated))
        fixture = documents["fixtures.json"]["fixtures"][index]
        contract["fixture_snapshot"] = independent_fixture_snapshot(fixture, children)
        refresh_capture(graph, index)


def replace_generator_source(graph, index, path, payload):
    root, documents, _ = graph
    fixture = documents["fixtures.json"]["fixtures"][index]
    original_path = fixture["generator"]["path"]
    replacement = pin(root, path, payload)
    fixture["generator"] = deepcopy(replacement)
    contract = first_contract(graph, index)
    capture = contract["capture"]
    for identities in (capture["generator_sources"], capture["runtime"]["sources"]):
        for identity in identities:
            if identity["path"] == original_path:
                identity.update(deepcopy(replacement))
    contract["fixture_snapshot"] = independent_fixture_snapshot(
        fixture, documents["commands.json"]["requirements"]
    )
    refresh_capture(graph, index)


def test_capture_import_rejects_equal_helper_bytes_bound_to_unrelated_directory(tmp_path):
    graph = owned_set(tmp_path)
    relocate_capture_helper(graph, "unrelated/owned_capture_helper.py")
    assert (graph[0] / "unrelated/owned_capture_helper.py").is_file()
    assert not (graph[0] / "scripts/owned_capture_helper.py").exists()
    # All capture, runtime, receipt, contract and run/trace pins now bind the move.
    # Import resolution, rather than byte staleness, must reject it.
    with pytest.raises(ValueError):
        validate(graph)


def test_unresolved_absent_import_remains_capture_gap_without_assertion_credit(tmp_path):
    graph = owned_set(tmp_path)
    payload = (b"from owned_capture_helper import identity\n"
               b"from fixture_helper import identity as fixture_identity\n")
    replace_generator_source(graph, 0, "scripts/owned_capture_pixels.py", payload)
    assert not (graph[0] / "scripts/fixture_helper.py").exists()
    assert all(Path(identity["path"]).name != "fixture_helper.py"
               for identity in first_contract(graph)["capture"]["generator_sources"])
    result = validate(graph)
    assert any(gap.get("fixture_id") == "F.owned-pixels" and "fixture_helper" in gap["reason"]
               for gap in result["capture_gaps"])
    assert result["required_case_assertions"] == 12
    assert result["verified_case_assertions"] == 10
    assert {gap["fixture_id"] for gap in result["missing_case_assertions"]} == {"F.owned-pixels"}
    for options in ({"require_evidence": True}, {"require_complete": True}):
        with pytest.raises(ValueError):
            validate(graph, **options)


def package_relative_capture_set(tmp_path):
    graph = owned_set(tmp_path)
    replace_generator_source(
        graph, 0, "scripts/owned_capture_pixels.py",
        b"from package.capture_helper import identity\n",
    )
    capture = first_contract(graph)["capture"]
    primary = deepcopy(capture["generator_sources"][0])
    initializer = pin(graph[0], "scripts/package/__init__.py", b"# Owned package initializer.\n")
    helper = pin(graph[0], "scripts/package/capture_helper.py",
                 b"from .relative_helper import identity\n")
    relative = pin(graph[0], "scripts/package/relative_helper.py",
                   b"def identity(value):\n    return value\n")
    capture["generator_sources"] = [primary, initializer, helper, relative]
    capture["runtime"]["sources"] = deepcopy(capture["generator_sources"])
    refresh_capture(graph)
    return graph


def test_capture_package_relative_import_closure_pins_exact_parent_and_initializer(tmp_path):
    graph = package_relative_capture_set(tmp_path)
    result = validate(graph, require_evidence=True, require_complete=True)
    assert result["required_case_assertions"] == result["verified_case_assertions"] == 12
    assert not result["capture_gaps"]


def test_capture_relative_import_rejects_equal_helper_bytes_bound_to_wrong_parent(tmp_path):
    graph = package_relative_capture_set(tmp_path)
    capture = first_contract(graph)["capture"]
    original_path = "scripts/package/relative_helper.py"
    moved = pin(graph[0], "scripts/relative_helper.py", (graph[0] / original_path).read_bytes())
    (graph[0] / original_path).unlink()
    for identities in (capture["generator_sources"], capture["runtime"]["sources"]):
        for identity in identities:
            if identity["path"] == original_path:
                identity.update(deepcopy(moved))
    refresh_capture(graph)
    with pytest.raises(ValueError):
        validate(graph)


def test_capture_package_import_cannot_omit_required_initializer_pin(tmp_path):
    graph = package_relative_capture_set(tmp_path)
    capture = first_contract(graph)["capture"]
    for role in ("generator_sources",):
        capture[role] = [identity for identity in capture[role]
                         if identity["path"] != "scripts/package/__init__.py"]
    capture["runtime"]["sources"] = [identity for identity in capture["runtime"]["sources"]
                                        if identity["path"] != "scripts/package/__init__.py"]
    refresh_capture(graph)
    result = validate(graph)
    assert any(gap.get("fixture_id") == "F.owned-pixels" and "__init__" in gap["reason"]
               for gap in result["capture_gaps"])
    assert result["verified_case_assertions"] == 10
    with pytest.raises(ValueError):
        validate(graph, require_complete=True)


@pytest.mark.parametrize("dynamic_import", [
    b"__import__('fixture_helper')\n",
    b"import importlib\nimportlib.import_module('fixture_helper')\n",
])
def test_dynamic_capture_import_retains_gap_without_assertion_credit(tmp_path, dynamic_import):
    graph = owned_set(tmp_path)
    replace_generator_source(
        graph, 0, "scripts/owned_capture_pixels.py",
        b"from owned_capture_helper import identity\n" + dynamic_import,
    )
    result = validate(graph)
    assert any(gap.get("fixture_id") == "F.owned-pixels" for gap in result["capture_gaps"])
    assert result["required_case_assertions"] == 12
    assert result["verified_case_assertions"] == 10
    assert {gap["fixture_id"] for gap in result["missing_case_assertions"]} == {"F.owned-pixels"}
    for options in ({"require_evidence": True}, {"require_complete": True}):
        with pytest.raises(ValueError):
            validate(graph, **options)


BOOLEAN_FORMAT = "imagej_roi_boolean_v1"
BOOLEAN_SHA = "407a52c8804adf0b63e9efb2951cd701f985714b696a109db0a1a174e95e4488"
BOOLEAN_OPERAND_SETS = (
    "overlap_rectangles", "overlap_rectangles_reversed",
    "disconnected_rectangle_ellipse", "disconnected_rectangle_ellipse_reversed",
    "ring_and_inner_island", "ring_and_partial_hole_fill", "disconnected_rings",
    "negative_edges", "right_bottom_edges", "entirely_outside",
    "empty_area_and_rectangle", "empty_area_and_rectangle_reversed",
    "two_empty_areas", "fractional_rectangle_ellipse",
    "fractional_rectangle_ellipse_reversed", "fractional_polygon_rectangle",
    "fractional_polygon_rectangle_reversed", "duplicate_polygon",
)
BOOLEAN_OPERATORS = ("union", "intersection", "subtraction", "xor")


def boolean_corpus():
    payload = (ROOT / "tests/fixtures/roi-boolean-imagej154r.json").read_bytes()
    assert len(payload) == 88018 and digest(payload) == BOOLEAN_SHA
    return json.loads(payload)


def boolean_declaration():
    return next(row for row in real_documents()["fixtures.json"]["fixtures"]
                if row["id"] == "F.G016.area-boolean.pixels")


def test_boolean_real_projection_retains_all72_inputs_and_complete_observations():
    corpus = boolean_corpus()
    original = deepcopy(corpus)
    actual = fixtures.project_cases(BOOLEAN_FORMAT, corpus)
    expected_ids = [name + "__" + operator
                    for name in BOOLEAN_OPERAND_SETS for operator in BOOLEAN_OPERATORS]
    assert [case["id"] for case in corpus["cases"]] == expected_ids
    assert len(actual) == 72 and [row["locator"] for row in actual] == list(range(72))
    for projection, case in zip(actual, corpus["cases"]):
        source = {key: deepcopy(value) for key, value in case.items()
                  if key not in {"operands", "result"}}
        source["operands"] = [{"geometry": deepcopy(value["geometry"])}
                              for value in case["operands"]]
        annotations = [deepcopy(value) for value in case["operands"]]
        for value in annotations:
            del value["geometry"]
            del value["mask_rows"]
        result_annotations = deepcopy(case["result"])
        del result_annotations["mask_rows"]
        source["reference_annotations"] = {
            "operands": annotations, "result": result_annotations,
        }
        assert projection["input"] == source
        assert projection["expected"] == {
            "operands": [{"mask_rows": value["mask_rows"]} for value in case["operands"]],
            "result": {"mask_rows": case["result"]["mask_rows"]},
        }
        for value in [*projection["expected"]["operands"], projection["expected"]["result"]]:
            assert set(value) == {"mask_rows"}
            assert len(value["mask_rows"]) == 9
            assert all(len(row) == 13 for row in value["mask_rows"])
        reconstructed = {key: deepcopy(value) for key, value in projection["input"].items()
                         if key not in {"operands", "reference_annotations"}}
        reconstructed["operands"] = [
            {**deepcopy(geometry), **deepcopy(annotation), **deepcopy(observation)}
            for geometry, annotation, observation in zip(
                projection["input"]["operands"], annotations, projection["expected"]["operands"]
            )
        ]
        reconstructed["result"] = {
            **deepcopy(result_annotations), **deepcopy(projection["expected"]["result"]),
        }
        assert reconstructed == case
    assert corpus == original
    actual[0]["input"]["operands"][0]["geometry"]["xywh"][0] = -999
    actual[0]["expected"]["result"]["mask_rows"][0] = "1" * 13
    assert corpus == original


def test_boolean_raw_jsonl_projection_has_the_same_complete72_case_contract():
    corpus = boolean_corpus()
    # This is the exact JSONL envelope consumed by the generic capture verifier.
    raw_envelope = {"schema_version": 1, "cases": deepcopy(corpus["cases"])}
    assert fixtures.project_cases(BOOLEAN_FORMAT, raw_envelope) == fixtures.project_cases(BOOLEAN_FORMAT, corpus)
    assert fixtures.LEGACY_FORMATS["capture_roi_boolean_reference.py"] == BOOLEAN_FORMAT


@pytest.mark.parametrize("mutation", [
    "missing", "extra", "duplicate", "reorder", "declared_ids", "operator",
    "method", "order", "canvas", "canvas_boolean", "operands", "id", "operand_set",
])
def test_boolean_adapter_rejects_incomplete_order_and_unreviewed_case_identity(mutation):
    corpus = boolean_corpus()
    cases = corpus["cases"]
    if mutation == "missing":
        cases.pop()
    elif mutation == "extra":
        cases.append(deepcopy(cases[0]))
    elif mutation == "duplicate":
        cases[1] = deepcopy(cases[0])
    elif mutation == "reorder":
        cases[0], cases[4] = cases[4], cases[0]
    elif mutation == "declared_ids":
        corpus["exact_ordered_case_ids"].reverse()
    elif mutation == "operator":
        cases[0]["operator"] = "nand"
    elif mutation == "method":
        cases[0]["operation"] = "ShapeRoi.and"
    elif mutation == "order":
        cases[0]["operand_order"] = "right_then_left"
    elif mutation == "canvas":
        cases[0]["canvas_wh"] = [9, 13]
    elif mutation == "canvas_boolean":
        cases[0]["canvas_wh"] = [True, 9]
    elif mutation == "operands":
        cases[0]["operands"].pop()
    elif mutation == "id":
        cases[0]["id"] = "different__union"
    else:
        cases[0]["operand_set"] = "unknown"
    with pytest.raises(ValueError):
        fixtures.project_cases(BOOLEAN_FORMAT, corpus)


@pytest.mark.parametrize("role", ["left", "right", "result"])
@pytest.mark.parametrize("mutation", [
    "rows_missing", "row_short", "row_nonbinary", "row_nonstr",
    "bounds_missing", "bounds_boolean", "bounds_fractional", "bounds_negative_span",
    "empty_numeric", "empty_nonzero",
])
def test_boolean_adapter_rejects_malformed_masks_bounds_and_empty_states(role, mutation):
    corpus = boolean_corpus()
    case = corpus["cases"][0]
    value = case["result"] if role == "result" else case["operands"][0 if role == "left" else 1]
    if mutation == "rows_missing":
        value["mask_rows"].pop()
    elif mutation == "row_short":
        value["mask_rows"][0] = "0" * 12
    elif mutation == "row_nonbinary":
        value["mask_rows"][0] = "0" * 12 + "2"
    elif mutation == "row_nonstr":
        value["mask_rows"][0] = [0] * 13
    elif mutation == "bounds_missing":
        value["bounds_xywh"].pop()
    elif mutation == "bounds_boolean":
        value["bounds_xywh"][0] = True
    elif mutation == "bounds_fractional":
        value["bounds_xywh"][0] = 1.5
    elif mutation == "bounds_negative_span":
        value["bounds_xywh"][2] = -1
    elif mutation == "empty_numeric":
        value["vector_area_empty"] = 0
    else:
        value["vector_area_empty"] = True
    with pytest.raises(ValueError):
        fixtures.project_cases(BOOLEAN_FORMAT, corpus)


@pytest.mark.parametrize("mutation", [
    "unknown_kind", "constructor", "box_short", "box_boolean", "box_zero_span",
    "box_nan", "polygon_short", "polygon_pair", "polygon_infinite",
    "polygon_float32_overflow", "ring_zero_hole", "empty_false", "geometry_reordered",
])
def test_boolean_adapter_rejects_malformed_geometry_and_changed_ordered_construction(mutation):
    corpus = boolean_corpus()
    case = corpus["cases"][0]
    geometry = case["operands"][0]["geometry"]
    if mutation == "unknown_kind":
        geometry["kind"] = "freehand"
    elif mutation == "constructor":
        geometry["constructor"] = "ShapeRoi(Rectangle2D)"
    elif mutation == "box_short":
        geometry["xywh"].pop()
    elif mutation == "box_boolean":
        geometry["xywh"][0] = False
    elif mutation == "box_zero_span":
        geometry["xywh"][2] = 0
    elif mutation == "box_nan":
        geometry["xywh"][0] = float("nan")
    elif mutation.startswith("polygon"):
        geometry = next(value["geometry"] for row in corpus["cases"] for value in row["operands"]
                        if value["geometry"]["kind"] == "polygon")
        if mutation == "polygon_short":
            geometry["points"] = geometry["points"][:2]
        elif mutation == "polygon_pair":
            geometry["points"][0].append(3)
        elif mutation == "polygon_infinite":
            geometry["points"][0][0] = float("inf")
        else:
            geometry["points"][0][0] = 1e100
    elif mutation == "ring_zero_hole":
        geometry = next(value["geometry"] for row in corpus["cases"] for value in row["operands"]
                        if value["geometry"]["kind"] == "ring")
        geometry["hole_xywh"][3] = 0
    elif mutation == "empty_false":
        value = next(value for row in corpus["cases"] for value in row["operands"]
                     if value["geometry"]["kind"] == "empty_area")
        value["vector_area_empty"] = False
    else:
        corpus["cases"][1]["operands"].reverse()
    with pytest.raises(ValueError):
        fixtures.project_cases(BOOLEAN_FORMAT, corpus)


@pytest.mark.parametrize("mutation", ["changed_contract", "contract_hash", "missing_contract", "missing_hash"])
def test_boolean_declared_input_contract_cannot_drift(mutation):
    corpus = boolean_corpus()
    if mutation == "changed_contract":
        corpus["input_contract"]["operand_sets"]["overlap_rectangles"][0]["xywh"][0] = 99
    elif mutation == "contract_hash":
        corpus["input_contract_sha256"] = "0" * 64
    elif mutation == "missing_contract":
        del corpus["input_contract"]
    else:
        del corpus["input_contract_sha256"]
    with pytest.raises(ValueError):
        fixtures.project_cases(BOOLEAN_FORMAT, corpus)


def test_boolean_projection_fingerprints_include_unselected_input_and_result_metadata():
    corpus = boolean_corpus()
    original = fixtures.project_cases(BOOLEAN_FORMAT, corpus)[0]
    changed = deepcopy(corpus)
    changed["cases"][0]["operand_annotation"] = {"retained": "input context"}
    added_input = fixtures.project_cases(BOOLEAN_FORMAT, changed)[0]
    assert fingerprint(added_input["input"]) != fingerprint(original["input"])
    assert added_input["expected"] == original["expected"]
    for role in ("operand", "result"):
        changed = deepcopy(corpus)
        value = changed["cases"][0]["result"] if role == "result" else changed["cases"][0]["operands"][0]
        value["bounds_xywh"][0] += 1
        projection = fixtures.project_cases(BOOLEAN_FORMAT, changed)[0]
        assert fingerprint(projection["input"]) != fingerprint(original["input"])
        assert projection["expected"] == original["expected"]
        value["reference_note"] = {"retained": [1, "capture annotation"]}
        projection = fixtures.project_cases(BOOLEAN_FORMAT, changed)[0]
        annotations = projection["input"]["reference_annotations"]
        retained = annotations["result"] if role == "result" else annotations["operands"][0]
        assert retained["reference_note"] == value["reference_note"]
        assert projection["expected"] == original["expected"]


def test_boolean_reference_annotations_preserve_raster_empty_vector_nonempty_distinction():
    corpus = boolean_corpus()
    projection = fixtures.project_cases(BOOLEAN_FORMAT, corpus)[36]
    case = corpus["cases"][36]
    assert case["id"] == "entirely_outside__union"
    assert case["result"]["vector_area_empty"] is False
    assert all(row == "0" * 13 for row in case["result"]["mask_rows"])
    assert projection["input"]["reference_annotations"]["result"] == {
        "bounds_xywh": case["result"]["bounds_xywh"], "vector_area_empty": False,
    }
    assert projection["expected"]["result"] == {"mask_rows": ["0" * 13] * 9}
    assert all(set(value) == {"mask_rows"} for value in projection["expected"]["operands"])


def test_boolean_reserved_annotation_field_cannot_silently_drop_captured_input():
    corpus = boolean_corpus()
    corpus["cases"][0]["reference_annotations"] = {"captured": "original field"}
    with pytest.raises(ValueError, match="reserved reference-annotation"):
        fixtures.project_cases(BOOLEAN_FORMAT, corpus)


def test_boolean_embedded_identity_accepts_exact_nine_source_and_two_license_pins():
    corpus, declaration = boolean_corpus(), boolean_declaration()
    before = deepcopy(corpus)
    fixtures._embedded_identity(corpus, declaration, BOOLEAN_FORMAT, "Boolean embedded identity")
    assert corpus == before


@pytest.mark.parametrize("mutation", [
    "source_missing", "source_extra", "source_hash", "source_bytes", "license_missing",
    "license_hash", "license_bytes", "source_substitution", "generator_hash", "version",
])
def test_boolean_embedded_identity_rejects_wrong_or_ambiguous_source_members(mutation):
    corpus, declaration = boolean_corpus(), deepcopy(boolean_declaration())
    known = corpus["known_source_members"]
    if mutation == "source_missing":
        del known["ij/gui/ShapeRoi.java"]
    elif mutation == "source_extra":
        known["ij/gui/Unknown.java"] = {"bytes": 1, "sha256": "0" * 64}
    elif mutation == "source_hash":
        known["ij/gui/ShapeRoi.java"]["sha256"] = "0" * 64
    elif mutation == "source_bytes":
        known["ij/gui/ShapeRoi.java"]["bytes"] = True
    elif mutation == "license_missing":
        del known["LICENSE.txt"]
    elif mutation == "license_hash":
        known["LICENSE.txt"]["sha256"] = "0" * 64
    elif mutation == "license_bytes":
        known["pom.xml"]["bytes"] += 1
    elif mutation == "source_substitution":
        declaration["reference"]["source_members"][0]["path"] = (
            "ImageJ-" + declaration["reference"]["revision"] + "/ij/Unknown.java"
        )
    elif mutation == "generator_hash":
        corpus["generator_sha256"] = "0" * 64
    else:
        corpus["imagej_version"] = "1.54s"
    with pytest.raises(ValueError):
        fixtures._embedded_identity(corpus, declaration, BOOLEAN_FORMAT, "Boolean embedded identity")


def axis_corpus_and_fixture():
    path = ROOT / "tests/fixtures/axis-plane-imagej154r.json"
    payload = path.read_bytes()
    assert digest(payload) == "05cb09b95b2dcfd2e1e492150d3883497c0bb5a203c8dde3b5b5b1830f3b5d08"
    corpus = json.loads(payload)
    manifest = json.loads((ROOT / "docs/parity/fixtures.json").read_text(encoding="utf-8"))
    declaration, = [row for row in manifest["fixtures"] if row["id"] == "F.G029.axis-plane.pixels"]
    return corpus, deepcopy(declaration)


def independent_axis_projection(corpus):
    result = []
    assert [case["case_id"] for case in corpus["cases"]] == list(AXIS_CASE_IDS)
    for locator, case in enumerate(corpus["cases"]):
        dtype = case["dtype"]
        output = case["output"]
        source = {key: deepcopy(value) for key, value in case.items()
                  if key not in {"source_before", "source_after", "output"}}
        source["reference_annotations"] = {
            "input_contract": deepcopy(corpus["input_contract"]),
            **{role: {key: deepcopy(value) for key, value in case[role].items() if key != "raw_hex"}
               for role in ("source_before", "source_after", "output")},
        }
        expected = {
            "source_before": {"raw_hex": case["source_before"]["raw_hex"]},
            "source_after": {"raw_hex": case["source_after"]["raw_hex"]},
            "output": {"shape": [1, 1, 1, output["height"], output["width"], 3 if dtype == "rgb" else 1],
                       "pixel_type": {"u8": "UInt8", "u16": "UInt16", "f32": "Float32", "rgb": "UInt8"}[dtype],
                       "S": 3 if dtype == "rgb" else 1, "raw_hex": output["raw_hex"],
                       "spacing": deepcopy(output["calibration"]["spacing"])},
        }
        result.append({"locator": locator, "input": source, "expected": expected})
    return result


def test_axis_fixture_full_and_raw_envelope_preserve_exact_84_projection_pairs():
    corpus, declaration = axis_corpus_and_fixture()
    before = deepcopy(corpus)
    expected = independent_axis_projection(corpus)
    fixtures._embedded_identity(corpus, declaration, "imagej_axis_plane_v1", "axis embedded identity")
    actual = fixtures.project_cases("imagej_axis_plane_v1", corpus)
    raw = {"schema_version": 1, "cases": [json.loads(json.dumps(case)) for case in corpus["cases"]]}
    assert actual == expected == fixtures.project_cases("imagej_axis_plane_v1", raw)
    assert tuple(fixtures.AXIS_OUTPUT_PATHS) == AXIS_SELECTORS
    assert len(actual) * len(AXIS_SELECTORS) == 84
    assert all(len(row["expected"]["output"]) == 5 for row in actual)
    assert all(row["input"]["reference_annotations"]["output"]["calibration"]["frame_interval"] == 0.0
               for row in actual)
    assert all(row["input"]["ui_executed"] is False for row in actual)
    assert corpus == before


@pytest.mark.parametrize("mutation", [
    "order", "missing", "duplicate", "dtype", "plane", "coordinate", "method", "encoding",
    "byte_length", "uppercase_hex", "nonfinite_bytes", "shape", "source_changed", "options",
    "source_calibration", "output_origin", "ui_claim", "input_contract", "input_contract_sha",
])
def test_axis_fixture_rejects_changed_construction_and_observations(mutation):
    corpus, _ = axis_corpus_and_fixture()
    case = corpus["cases"][7]  # Float32 XZ upper, exercising byte finiteness and options.
    if mutation == "order":
        corpus["cases"][0], corpus["cases"][1] = corpus["cases"][1], corpus["cases"][0]
    elif mutation == "missing":
        corpus["cases"].pop()
    elif mutation == "duplicate":
        corpus["cases"][-1] = deepcopy(corpus["cases"][-2])
    elif mutation in {"dtype", "plane", "coordinate", "method"}:
        key, value = {"dtype": ("dtype", "rgb"), "plane": ("plane", "xy"),
                      "coordinate": ("coordinate_zero_based", 15),
                      "method": ("method", "Slicer.showDialog")}[mutation]
        case[key] = value
    elif mutation == "encoding":
        case["output"]["encoding"] = "UInt16_le"
    elif mutation == "byte_length":
        case["output"]["raw_hex"] = case["output"]["raw_hex"][:-2]
    elif mutation == "uppercase_hex":
        case["output"]["raw_hex"] = case["output"]["raw_hex"].upper()
    elif mutation == "nonfinite_bytes":
        case["output"]["raw_hex"] = "0000c07f" + case["output"]["raw_hex"][8:]
    elif mutation == "shape":
        case["output"]["height"] = 8
    elif mutation == "source_changed":
        case["source_after"]["raw_hex"] = "00000000" + case["source_after"]["raw_hex"][8:]
    elif mutation == "options":
        case["configured"]["flip"] = True
    elif mutation == "source_calibration":
        case["source_before"]["calibration"]["spacing"][1] = 2.4
    elif mutation == "output_origin":
        case["output"]["calibration"]["origin_pixels"] = [0.0, 0.0, 0.0]
    elif mutation == "ui_claim":
        case["ui_executed"] = True
    elif mutation == "input_contract":
        corpus["input_contract"]["formulas"]["u8"] = "x"
    else:
        corpus["input_contract_sha256"] = "0" * 64
    with pytest.raises(ValueError):
        fixtures.project_cases("imagej_axis_plane_v1", corpus)


@pytest.mark.parametrize("mutation", [
    "short_generator", "full_generator", "generator_hash", "fixture_scope", "registered_scope",
    "member_missing", "member_hash", "member_bytes", "license_hash", "driver_hash", "version",
    "original_capture", "commercial_review",
])
def test_axis_embedded_identity_binds_known_representation_and_sixteen_members(mutation):
    corpus, declaration = axis_corpus_and_fixture()
    if mutation == "short_generator":
        corpus["generator"] = declaration["generator"]["path"]
    elif mutation == "full_generator":
        declaration["generator"]["path"] = "capture_reference.py"
    elif mutation == "generator_hash":
        corpus["generator_sha256"] = "0" * 64
    elif mutation == "fixture_scope":
        corpus["scope"] = declaration["scope"]
    elif mutation == "registered_scope":
        declaration["reference"]["scope"] = corpus["scope"]
    elif mutation == "member_missing":
        del corpus["known_source_members"]["ij/plugin/Slicer.java"]
    elif mutation == "member_hash":
        corpus["known_source_members"]["ij/plugin/Slicer.java"]["sha256"] = "0" * 64
    elif mutation == "member_bytes":
        corpus["known_source_members"]["ij/plugin/Slicer.java"]["bytes"] += 1
    elif mutation == "license_hash":
        corpus["known_source_members"]["LICENSE.txt"]["sha256"] = "0" * 64
    elif mutation == "driver_hash":
        corpus["driver_sha256"] = "0" * 64
    elif mutation == "version":
        corpus["imagej_version"] = "1.54s"
    else:
        corpus["reference_capture" if mutation == "original_capture" else mutation]["sha256"] = "0" * 64
    with pytest.raises(ValueError):
        fixtures._embedded_identity(corpus, declaration, "imagej_axis_plane_v1", "axis embedded identity")


def test_axis_unselected_annotations_change_complete_input_fingerprints():
    corpus, _ = axis_corpus_and_fixture()
    original = fixtures.project_cases("imagej_axis_plane_v1", corpus)
    corpus["cases"][9]["output"]["storage_raw_hex"] = (
        corpus["cases"][9]["output"]["storage_raw_hex"][:6] + "ff" +
        corpus["cases"][9]["output"]["storage_raw_hex"][8:])
    changed = fixtures.project_cases("imagej_axis_plane_v1", corpus)
    assert original[9]["expected"] == changed[9]["expected"]
    assert fingerprint(original[9]["input"]) != fingerprint(changed[9]["input"])
    assert all(original[index] == changed[index] for index in range(12) if index != 9)


def independent_axis_contract(corpus, declaration, child):
    projections = independent_axis_projection(corpus)
    cases = [{"case_id": case_id, "locator": index,
              "input_sha256": fingerprint(projections[index]["input"]),
              "expected_sha256": fingerprint(projections[index]["expected"]),
              "required_output_paths": list(AXIS_SELECTORS)}
             for index, case_id in enumerate(AXIS_CASE_IDS)]
    value = {
        "id": "FC.G029.axis-plane.pixels", "fixture_id": declaration["id"],
        "format": "imagej_axis_plane_v1",
        "fixture_snapshot": independent_fixture_snapshot(declaration, [child]),
        "case_set": cases,
        "requirement_scopes": [{"requirement_id": child["id"], "snapshot": child_snapshot(child),
            "required": [{"case_id": case_id, "path": path}
                         for case_id in AXIS_CASE_IDS for path in AXIS_SELECTORS],
            "exclusions": ["Owned affine/temporal/owner/UI contracts remain separate."]}],
        "comparison": {**declaration["comparison"], "numeric_paths": [], "units": "typed bytes and scalar spacing",
                       "rationale": "Twelve captured integer-axis cases only.", "nonfinite": "reject"},
    }
    value["sha256"] = contract_digest(value)
    return value


@pytest.mark.parametrize("mutation", [None, "missing_selector", "extra_selector", "selector_order", "case_id"])
def test_axis_contract_requires_complete_ordered_twelve_by_seven_selector_denominator(mutation):
    corpus, declaration = axis_corpus_and_fixture()
    requirements = real_documents()["commands.json"]["requirements"]
    child, = [row for row in requirements if row["id"] == "G029.axis_plane_export"]
    contract = independent_axis_contract(corpus, declaration, child)
    if mutation == "missing_selector":
        contract["case_set"][0]["required_output_paths"].pop()
    elif mutation == "extra_selector":
        contract["case_set"][0]["required_output_paths"].append("/output")
    elif mutation == "selector_order":
        contract["case_set"][0]["required_output_paths"].reverse()
    elif mutation == "case_id":
        contract["case_set"][0]["case_id"] = "changed.xy.lower"
    contract["sha256"] = contract_digest(contract)
    result = {**{key: [] for key in GAPS}, **{key: 0 for key in COUNTS}}
    arguments = ({"fixture_contracts": [contract]}, {declaration["id"]: declaration},
                 {child["id"]: child}, {declaration["id"]: independent_axis_projection(corpus)}, result)
    if mutation is None:
        actual = fixtures._validate_contracts(*arguments)
        assert list(actual) == [contract["id"]]
        assert result["required_case_assertions"] == 84
        assert result["contracts_validated"] == 1
    else:
        with pytest.raises(ValueError):
            fixtures._validate_contracts(*arguments)


@pytest.mark.parametrize("mutation", [None, "path", "directory", "dynamic", "reassigned", "mode", "artifact"])
def test_axis_external_driver_is_narrowly_bound_and_unavailable_bytes_do_not_gain_credit(mutation):
    _, declaration = axis_corpus_and_fixture()
    primary = b'HERE = Path(__file__).resolve().parent\nDRIVER = HERE / "AxisPlaneReference.java"\n'
    driver = {"source_mode": "same_directory_python_path_v1", "text_sha256": fixtures.AXIS_DRIVER["sha256"],
              "artifact": deepcopy(fixtures.AXIS_DRIVER)}
    if mutation == "path":
        primary = primary.replace(b'"AxisPlaneReference.java"', b'"Other.java"')
    elif mutation == "directory":
        primary = primary.replace(b"HERE /", b"HERE.parent /")
    elif mutation == "dynamic":
        primary = primary.replace(b'DRIVER = HERE / "AxisPlaneReference.java"', b'DRIVER = get_driver()')
    elif mutation == "reassigned":
        primary += b'DRIVER = HERE / "AxisPlaneReference.java"\n'
    elif mutation == "mode":
        driver["source_mode"] = "literal"
    elif mutation == "artifact":
        driver["artifact"]["path"] = "build/Other.java"
    if mutation is None:
        assert fixtures._axis_driver_text(primary, driver, declaration, "axis driver", None) is None
    else:
        with pytest.raises(ValueError):
            fixtures._axis_driver_text(primary, driver, declaration, "axis driver", None)


RANGE_IDS = tuple(f"{kind}.{plane}.{profile}" for kind in ("u8", "u16", "f32", "rgb")
                  for plane in ("xy", "xz", "yz")
                  for profile in ("lower", "full", "interior_stride", "upper_stride"))
RANGE_SELECTORS = ("/source_before/raw_hex", "/source_after/raw_hex", "/output/shape",
                   "/output/pixel_type", "/output/S", "/output/raw_hex", "/output/in_plane_spacing")


def range_corpus_and_fixture():
    path = ROOT / "build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001/capture-001/corpus.json"
    payload = path.read_bytes()
    assert digest(payload) == "8ddde981534e0ccf55840316485c29c3dbcf61eef03dacee291b83477b4a8347"
    corpus = json.loads(payload)
    declaration, = [row for row in real_documents()["fixtures.json"]["fixtures"]
                     if row["id"] == "F.G029.integer-range.pixels"]
    return corpus, deepcopy(declaration)


def independent_range_projection(corpus):
    assert [case["case_id"] for case in corpus["cases"]] == list(RANGE_IDS)
    result = []
    roles = ("source_before", "source_after", "contiguous_output", "output")
    for index, case in enumerate(corpus["cases"]):
        output, kind = case["output"], case["dtype"]
        inputs = {key: deepcopy(value) for key, value in case.items() if key not in roles}
        inputs["reference_annotations"] = {role: {
            key: deepcopy(value) for key, value in case[role].items()
            if key != "raw_hex" or role == "contiguous_output"} for role in roles}
        expected = {"source_before": {"raw_hex": case["source_before"]["raw_hex"]},
                    "source_after": {"raw_hex": case["source_after"]["raw_hex"]},
                    "output": {"shape": [1, 1, case["requested_plane_count"], output["height"],
                                         output["width"], 3 if kind == "rgb" else 1],
                               "pixel_type": {"u8": "UInt8", "u16": "UInt16", "f32": "Float32", "rgb": "UInt8"}[kind],
                               "S": 3 if kind == "rgb" else 1, "raw_hex": output["raw_hex"],
                               "in_plane_spacing": deepcopy(output["calibration"]["spacing"][:2])}}
        result.append({"locator": index, "input": inputs, "expected": expected})
    return result


def test_range_fixture_full_and_raw_envelope_preserve_48_by_seven_scientific_pairs():
    corpus, declaration = range_corpus_and_fixture()
    before = deepcopy(corpus)
    fixtures._embedded_identity(corpus, declaration, "imagej_axis_range_v1", "range identity")
    expected = independent_range_projection(corpus)
    assert fixtures.project_cases("imagej_axis_range_v1", corpus) == expected
    assert fixtures.project_cases("imagej_axis_range_v1", {"schema_version": 1, "cases": corpus["cases"]}) == expected
    assert fixtures.RANGE_OUTPUT_PATHS == RANGE_SELECTORS
    assert len(expected) * len(RANGE_SELECTORS) == 336
    assert all(set(row["expected"]["output"]) == {"shape", "pixel_type", "S", "raw_hex", "in_plane_spacing"}
               for row in expected)
    assert corpus == before


@pytest.mark.parametrize("mutation", [
    "order", "missing", "duplicate", "case_id", "dtype", "plane", "profile",
    "first", "last", "step", "step_boolean", "coordinates", "indices", "count",
    "method", "selection_method", "output_bytes", "contiguous_bytes", "byte_length",
    "source_changed", "source_formula", "source_state", "stride_spacing", "origin_rebase",
    "interpolation", "nonfinite_bytes", "ui_claim", "annotations_collision",
])
def test_range_fixture_refuses_changed_protocol_typed_samples_and_source_state(mutation):
    corpus, _ = range_corpus_and_fixture()
    case = corpus["cases"][30]  # Float32 XZ positive stride.
    if mutation == "order":
        corpus["cases"][0], corpus["cases"][1] = corpus["cases"][1], corpus["cases"][0]
    elif mutation == "missing":
        corpus["cases"].pop()
    elif mutation == "duplicate":
        corpus["cases"][-1] = deepcopy(corpus["cases"][-2])
    elif mutation in {"case_id", "dtype", "plane", "profile", "first", "last", "step",
                      "step_boolean", "coordinates", "indices", "count", "method", "selection_method"}:
        key, value = {
            "case_id": ("case_id", "f32.xz.other"), "dtype": ("dtype", "rgb"),
            "plane": ("plane", "xy"), "profile": ("profile", "full"),
            "first": ("first_zero_based", 0), "last": ("last_inclusive_zero_based", 16),
            "step": ("positive_step", 2), "step_boolean": ("positive_step", True),
            "coordinates": ("source_coordinates_zero_based", [1, 4, 7, 10, 14]),
            "indices": ("selected_contiguous_indices_one_based", [2, 5, 8, 11, 14]),
            "count": ("requested_plane_count", 6), "method": ("contiguous_method", "spacing option stride"),
            "selection_method": ("selection_method", "Synthetic slice pixels"),
        }[mutation]
        case[key] = value
    elif mutation in {"output_bytes", "contiguous_bytes", "nonfinite_bytes"}:
        role = "contiguous_output" if mutation == "contiguous_bytes" else "output"
        raw = case[role]["raw_hex"]
        case[role]["raw_hex"] = ("0000c07f" if mutation == "nonfinite_bytes" else "00000000") + raw[8:]
        case[role]["storage_raw_hex"] = case[role]["raw_hex"]
    elif mutation == "byte_length":
        case["output"]["raw_hex"] = case["output"]["raw_hex"][:-2]
    elif mutation == "source_changed":
        case["source_after"]["raw_hex"] = "00000000" + case["source_after"]["raw_hex"][8:]
    elif mutation == "source_formula":
        for role in ("source_before", "source_after"):
            case[role]["raw_hex"] = case[role]["storage_raw_hex"] = "00000000" + case[role]["raw_hex"][8:]
    elif mutation == "source_state":
        case["source_after"]["title"] = "Lost original title"
    elif mutation == "stride_spacing":
        case["output"]["calibration"]["spacing"][2] *= 3
    elif mutation == "origin_rebase":
        case["output"]["calibration"]["origin_pixels"] = [0., 0., 0.]
    elif mutation == "interpolation":
        case["configured"]["nointerpolate"] = False
    elif mutation == "ui_claim":
        case["ui_executed"] = True
    else:
        case["reference_annotations"] = {"original": "cannot silently discard"}
    with pytest.raises(ValueError):
        fixtures.project_cases("imagej_axis_range_v1", corpus)


@pytest.mark.parametrize("mutation", ["fixture_path", "fixture_sha", "scope", "generator", "driver",
                                      "members", "member_hash", "revision", "original_review", "variant"])
def test_range_embedded_identity_refuses_unpinned_or_broadened_reference(mutation):
    corpus, declaration = range_corpus_and_fixture()
    if mutation == "fixture_path": declaration["path"] = "owned/recaptured.json"
    elif mutation == "fixture_sha": declaration["sha256"] = "0" * 64
    elif mutation == "scope": declaration["reference"]["scope"] += " Full affine parity."
    elif mutation == "generator": declaration["generator"]["sha256"] = "0" * 64
    elif mutation == "driver": corpus["driver"]["sha256"] = "0" * 64
    elif mutation == "members": declaration["reference"]["source_members"].pop()
    elif mutation == "member_hash": declaration["reference"]["source_members"][0]["sha256"] = "0" * 64
    elif mutation == "revision": corpus["imagej_commit"] = "0" * 40
    elif mutation == "original_review": corpus["existing_eligible_commercial_review"]["sha256"] = "0" * 64
    else: corpus["reference_variant"]["sha256"] = "0" * 64
    with pytest.raises(ValueError):
        fixtures._embedded_identity(corpus, declaration, "imagej_axis_range_v1", "range identity")


def independent_range_contract(corpus, declaration, child):
    projected = independent_range_projection(corpus)
    value = {"id": "FC.G029.integer-range.pixels", "fixture_id": declaration["id"],
        "format": "imagej_axis_range_v1", "fixture_snapshot": independent_fixture_snapshot(declaration, [child]),
        "case_set": [{"case_id": identifier, "locator": index,
                      "input_sha256": fingerprint(projected[index]["input"]),
                      "expected_sha256": fingerprint(projected[index]["expected"]),
                      "required_output_paths": list(RANGE_SELECTORS)} for index, identifier in enumerate(RANGE_IDS)],
        "requirement_scopes": [{"requirement_id": child["id"], "snapshot": child_snapshot(child),
            "required": [{"case_id": identifier, "path": path} for identifier in RANGE_IDS for path in RANGE_SELECTORS],
            "exclusions": ["Native third-axis stride basis, full affine/time/owner/history and UI are owned policies, not ImageJ calibration parity."]}],
        "comparison": {**declaration["comparison"], "numeric_paths": [], "units": "typed bytes and two in-plane scalar spacings",
            "rationale": "48 actual contiguous/positive-stride reference cases; copied third spacing/origins excluded.", "nonfinite": "reject"}}
    value["sha256"] = contract_digest(value)
    return value


@pytest.mark.parametrize("mutation", [None, "missing", "extra", "full_spacing", "order", "case", "scope_short"])
def test_range_contract_requires_exact_48_by_seven_scientific_selector_denominator(mutation):
    corpus, declaration = range_corpus_and_fixture()
    child, = [row for row in real_documents()["commands.json"]["requirements"] if row["id"] == "G029.integer_range_reslice"]
    contract = independent_range_contract(corpus, declaration, child)
    paths = contract["case_set"][0]["required_output_paths"]
    if mutation == "missing": paths.pop()
    elif mutation == "extra": paths.append("/output/origin")
    elif mutation == "full_spacing": paths[-1] = "/output/spacing"
    elif mutation == "order": paths.reverse()
    elif mutation == "case": contract["case_set"][0]["case_id"] = "missing.case"
    elif mutation == "scope_short": contract["requirement_scopes"][0]["required"].pop()
    contract["sha256"] = contract_digest(contract)
    result = {**{key: [] for key in GAPS}, **{key: 0 for key in COUNTS}}
    args = ({"fixture_contracts": [contract]}, {declaration["id"]: declaration},
            {child["id"]: child}, {declaration["id"]: independent_range_projection(corpus)}, result)
    if mutation is None:
        assert list(fixtures._validate_contracts(*args)) == [contract["id"]]
        assert result["required_case_assertions"] == 336 and result["contracts_validated"] == 1
    else:
        with pytest.raises(ValueError): fixtures._validate_contracts(*args)


@pytest.mark.parametrize("mutation", [None, "failed_reference", "timeout", "shell", "closure_short", "driver"])
def test_range_normalization_uses_original_reference_provenance_only(mutation):
    corpus, declaration = range_corpus_and_fixture()
    before = deepcopy(corpus)
    if mutation == "failed_reference": corpus["processes"]["reference"]["exit_code"] = 1
    elif mutation == "timeout": corpus["processes"]["compile"]["timed_out"] = True
    elif mutation == "shell": corpus["processes"]["reference"]["shell"] = True
    elif mutation == "closure_short": corpus["reuse"]["classpath"].pop(next(iter(corpus["reuse"]["classpath"])))
    elif mutation == "driver": corpus["driver"]["sha256"] = "0" * 64
    if mutation is not None:
        with pytest.raises(ValueError): fixtures.axis_range_capture_receipt(declaration, corpus)
        return
    receipt = fixtures.axis_range_capture_receipt(declaration, corpus)
    assert receipt["case_count"] == 48 and receipt["raw_output"] == corpus["processes"]["reference"]["stdout"]
    assert receipt["generator_sources"] == [corpus["script"]]
    assert receipt["driver"]["artifact"] == corpus["driver"]
    assert len(receipt["reference_members"]) == 16
    assert receipt["range_capture_provenance"]["original_capture"] == fixtures.RANGE_REFERENCE_CAPTURE
    assert receipt["range_capture_provenance"]["independent_formula_verification"] == fixtures.RANGE_FORMULA
    assert "receipt" not in receipt and "axis_capture_provenance" not in receipt
    assert corpus == before


@pytest.mark.parametrize("mutation", [None, "changed_receipt", "missing_receipt"])
def test_range_capture_audit_requires_pinned_normalized_receipt_without_recapture(tmp_path, mutation):
    corpus, declaration = range_corpus_and_fixture()
    body = fixtures.axis_range_capture_receipt(declaration, corpus)
    if mutation == "changed_receipt": body["range_capture_provenance"]["reference_driver_only"] = False
    path = tmp_path / "owned-normalization.json"
    payload = json_bytes(body)
    path.write_bytes(payload)
    capture = {**body, "receipt": {"path": path.relative_to(ROOT).as_posix(), "sha256": digest(payload), "bytes": len(payload)}}
    if mutation == "missing_receipt": path.unlink()
    result = {**{key: [] for key in GAPS}, **{key: 0 for key in COUNTS}}
    contract = {"id": "FC.G029.integer-range.pixels", "format": "imagej_axis_range_v1", "capture": capture}
    if mutation == "changed_receipt":
        with pytest.raises(ValueError, match="normalized receipt body"):
            fixtures._axis_validate_capture(ROOT, declaration, contract, corpus, result)
    else:
        assert fixtures._axis_validate_capture(ROOT, declaration, contract, corpus, result) is (mutation is None)
        assert bool(result["unavailable_evidence"]) is (mutation == "missing_receipt")


def owned_range_sdk_record(case):
    """Synthetic protocol input only: no producer, target or process claim."""
    kind, plane, _ = case["case_id"].split(".")
    samples = 3 if kind == "rgb" else 1
    dtype = {"u8": "UInt8", "u16": "UInt16", "f32": "Float32", "rgb": "UInt8"}[kind]
    encoding = {"u8": "uint8", "u16": "uint16_le", "f32": "float32_le", "rgb": "rgb_interleaved_uint8"}[kind]
    first, last, step = (case[key] for key in ("first_zero_based", "last_inclusive_zero_based", "positive_step"))
    request = {"plane": plane.upper(), "first": first, "last": last, "step": step,
               "t": 0, "c": 0, "max_working_bytes": 512 * 1024**2}
    base = {"pixel_type": dtype, "S": samples, "encoding": encoding, "sample_policy": "FiniteOnly"}
    source_dataset = {**base, "shape": [1, 1, 9, 17, 32, samples],
        "raw_hex": case["source_before"]["raw_hex"], "calibration": {"spacing": [1., 1., 1.]}}
    source = {"document_id": 1, "data_identity": 2, "dirty": True, "revisions": {"pixels": 1, "metadata": 1, "selection": 1, "display": 1},
        "history": {"undo_steps": 1, "redo_steps": 0}, "dataset": source_dataset,
        "title": case["case_id"], "properties": {"synthetic": True}, "extensions": {},
        "record_layout": {}, "selection": None, "previous_selection": None,
        "rois": [], "overlays": [], "spectrum": None, "display": {"t": 0, "c": 0}}
    captured = case["output"]
    output = {**base, "shape": [1, 1, case["requested_plane_count"], captured["height"], captured["width"], samples],
              "raw_hex": captured["raw_hex"], "calibration": {
                  "spacing": captured["calibration"]["spacing"][:2] + [987.0],
                  "origin": [11., -7., 19.], "direction": [1., .25, 0., .5, -1., 0., 0., .75, 1.],
                  "time_origin": 12.5}}
    return {"schema_version": 1, "context": "cpp_api", "case_id": case["case_id"],
        "captured_request": request, "source_before": source, "source_after": deepcopy(source),
        "direct_result": {"request": deepcopy(request), "dataset": deepcopy(output)},
        "output_snapshot": {"dataset": deepcopy(output)},
        **{key: deepcopy(output[key]) for key in ("shape", "pixel_type", "S", "encoding", "raw_hex")},
        "spacing": deepcopy(output["calibration"]["spacing"])}


def test_range_sdk_projection_uses_actual_fields_and_excludes_owned_world_geometry():
    corpus, _ = range_corpus_and_fixture()
    expected = independent_range_projection(corpus)
    for index, case in enumerate(corpus["cases"]):
        row = owned_range_sdk_record(case)
        before = deepcopy(row)
        observation = fixtures.axis_range_sdk_observation(row)
        assert observation == expected[index]["expected"]
        assert row == before
        assert "spacing" not in observation["output"] and "origin" not in observation["output"]
        assert row["spacing"][2] == 987.0  # Never mistaken for copied ImageJ third spacing.


@pytest.mark.parametrize("mutation", ["context", "case_id", "request", "request_boolean", "direct_request",
    "source_state_missing", "source_changed", "source_shape", "output_shape", "output_type", "encoding",
    "top_bytes", "direct_bytes", "nonfinite_hex", "spacing_count", "spacing_boolean", "missing_record"])
def test_range_sdk_projection_refuses_incomplete_or_contradictory_actual_records(mutation):
    corpus, _ = range_corpus_and_fixture()
    row = owned_range_sdk_record(corpus["cases"][30])
    if mutation == "context": row["context"] = "imgui_cpp"
    elif mutation == "case_id": row["case_id"] = "unknown.case"
    elif mutation == "request": row["captured_request"]["step"] = 1
    elif mutation == "request_boolean": row["captured_request"]["t"] = False
    elif mutation == "direct_request": row["direct_result"]["request"]["last"] = 16
    elif mutation == "source_state_missing":
        del row["source_before"]["properties"]
        del row["source_after"]["properties"]
    elif mutation == "source_changed": row["source_after"]["dirty"] = False
    elif mutation == "source_shape": row["source_before"]["dataset"]["shape"][2] = 8
    elif mutation == "output_shape": row["output_snapshot"]["dataset"]["shape"][2] = 6
    elif mutation == "output_type": row["output_snapshot"]["dataset"]["pixel_type"] = "Float64"
    elif mutation == "encoding": row["output_snapshot"]["dataset"]["encoding"] = "float64_le"
    elif mutation == "top_bytes": row["raw_hex"] = "00000000" + row["raw_hex"][8:]
    elif mutation == "direct_bytes": row["direct_result"]["dataset"]["raw_hex"] = "00000000" + row["raw_hex"][8:]
    elif mutation == "nonfinite_hex":
        raw = "0000c07f" + row["raw_hex"][8:]
        row["raw_hex"] = row["direct_result"]["dataset"]["raw_hex"] = row["output_snapshot"]["dataset"]["raw_hex"] = raw
    elif mutation == "spacing_count": row["output_snapshot"]["dataset"]["calibration"]["spacing"] = [1., 2.]
    elif mutation == "spacing_boolean": row["output_snapshot"]["dataset"]["calibration"]["spacing"][0] = True
    else: del row["output_snapshot"]
    with pytest.raises(ValueError): fixtures.axis_range_sdk_observation(row)


@pytest.mark.parametrize("mutation", ["source_samples", "output_samples", "in_plane_spacing"])
def test_range_scientific_comparison_refuses_self_consistent_wrong_actual_values(mutation):
    corpus, _ = range_corpus_and_fixture()
    case = corpus["cases"][30]
    expected = independent_range_projection(corpus)[30]["expected"]
    row = owned_range_sdk_record(case)
    if mutation == "source_samples":
        for role in ("source_before", "source_after"):
            value = row[role]["dataset"]
            value["raw_hex"] = "00000000" + value["raw_hex"][8:]
    elif mutation == "output_samples":
        raw = "00000000" + row["raw_hex"][8:]
        row["raw_hex"] = row["direct_result"]["dataset"]["raw_hex"] = row["output_snapshot"]["dataset"]["raw_hex"] = raw
    else:
        row["spacing"][0] *= 2
        for value in (row["direct_result"]["dataset"], row["output_snapshot"]["dataset"]):
            value["calibration"]["spacing"] = deepcopy(row["spacing"])
    observation = fixtures.axis_range_sdk_observation(row)
    with pytest.raises(ValueError):
        fixtures._compare(expected, observation, {"method": "exact", "atol": 0, "rtol": 0}, False, "owned wrong actual")


@pytest.mark.parametrize("mutation", [None, "missing", "extra", "reorder", "duplicate"])
def test_range_sdk_aggregate_enforces_declared_48_case_execution_order(mutation):
    corpus, _ = range_corpus_and_fixture()
    rows = [owned_range_sdk_record(case) for case in corpus["cases"]]
    if mutation == "missing": rows.pop()
    elif mutation == "extra": rows.append(deepcopy(rows[-1]))
    elif mutation == "reorder": rows[0], rows[1] = rows[1], rows[0]
    elif mutation == "duplicate": rows[-1] = deepcopy(rows[-2])
    if mutation is None:
        assert fixtures.axis_range_sdk_observations(rows) == [row["expected"] for row in independent_range_projection(corpus)]
    else:
        with pytest.raises(ValueError, match="exact ordered forty-eight"):
            fixtures.axis_range_sdk_observations(rows)


def original_tooling_case(root, *, mapping=None):
    """Independent sealed helper/config chain; helper code must never execute."""
    if root.drive and not str(root).startswith("\\\\?\\"):
        root = Path("\\\\?\\" + str(root.resolve()))
    logical = "scripts/parity_fixtures.py"
    archive = "build/plan-implementation/p05f-axis-plane-export-001/source-preparation-019/scripts/parity_fixtures.py"
    original = b"# independent historical tool source\n"
    snapshot = dict(pin(root, archive, original), bytes=len(original))
    live_pin = {"path": logical, "sha256": digest(original), "bytes": len(original)}
    pin(root, logical, b"# changed current source\n")
    helper_data = ("PINS = " + repr({"adapter": snapshot if mapping is None else mapping(snapshot)})
                   + "\nraise RuntimeError('This helper must never execute')\n").encode("utf-8")
    helper = dict(pin(root, "historical/helper.py", helper_data), bytes=len(helper_data))
    config_data = json_bytes({"helper": helper})
    config = dict(pin(root, "historical/config.json", config_data), bytes=len(config_data))
    producer = {"sources": [live_pin, helper], "artifacts": []}
    return root, live_pin, {"identity": {"configuration": config}}, producer, snapshot


def test_tool_source_resolves_only_original_pinned_tooling_without_execution(tmp_path):
    workspace, live, run, producer, snapshot = original_tooling_case(tmp_path)
    result = {"unavailable_evidence": []}
    data = fixtures._producer_bytes(workspace, live, run, producer, "owned tooling", result,
                                   "sources", run_id="owned", trace_id="owned.trace")
    assert data == b"# independent historical tool source\n"
    assert result["unavailable_evidence"] == []
    assert result["archived_tool_sources"] == [{
        "run_id": "owned", "trace_id": "owned.trace", "kind": "archived_tool_source",
        "path": live["path"], "snapshot": snapshot,
        "configuration": run["identity"]["configuration"], "helper": producer["sources"][1],
    }]
    assert (tmp_path / live["path"]).read_bytes() == b"# changed current source\n"


@pytest.mark.parametrize("mutation", [
    "missing", "corrupt", "wrong_mapping", "wrong_sha_mapping", "wrong_byte_mapping",
    "unlisted", "non_source", "config_helper_mismatch", "config_corrupt",
    "helper_corrupt", "no_configuration",
])
def test_tool_source_refuses_unpinned_or_changed_original_tooling(tmp_path, mutation):
    mapping = None
    if mutation == "wrong_mapping":
        mapping = lambda value: dict(value, path="arbitrary/later-copy.py")
    elif mutation == "wrong_sha_mapping":
        mapping = lambda value: dict(value, sha256="f" * 64)
    elif mutation == "wrong_byte_mapping":
        mapping = lambda value: dict(value, bytes=value["bytes"] + 1)
    workspace, live, run, producer, snapshot = original_tooling_case(tmp_path, mapping=mapping)
    kind = "sources"
    if mutation == "missing":
        (workspace / snapshot["path"]).unlink()
    elif mutation == "corrupt":
        (workspace / snapshot["path"]).write_bytes(b"# corrupt old source\n")
    elif mutation == "unlisted":
        live = dict(live, path="scripts/unlisted.py")
        pin(workspace, live["path"], b"# changed unlisted source\n")
    elif mutation == "non_source":
        kind = "artifacts"
    elif mutation == "config_helper_mismatch":
        producer["sources"] = [live]
    elif mutation == "config_corrupt":
        (workspace / run["identity"]["configuration"]["path"]).write_bytes(b"{}\n")
    elif mutation == "helper_corrupt":
        (workspace / producer["sources"][1]["path"]).write_bytes(b"PINS = {}\n")
    elif mutation == "no_configuration":
        run["identity"] = {}
    result = {"unavailable_evidence": []}
    with pytest.raises(ValueError):
        fixtures._producer_bytes(workspace, live, run, producer, "owned tooling refusal", result, kind)
    assert "archived_tool_sources" not in result


# Orientation additions validate metadata adapters only. No target/reference
# executable, product import or accepted proof is invoked by these tests.
ORIENTATION_FORMAT = "imagej_axis_range_orientation_v1"
ORIENTATION_IDS = tuple(
    f"{kind}.{plane}.{group}.reverse{reverse}.flip{flip}.transpose{transpose}"
    for group in ("interior_stride_unaligned", "full", "upper_singleton_stride")
    for kind in (("u8", "u16", "f32", "rgb") if group == "interior_stride_unaligned" else ("u8",))
    for plane in ("xz", "yz") for reverse in (0, 1) for flip in (0, 1) for transpose in (0, 1)
)
ORIENTATION_SELECTORS = (
    "/source_before/raw_hex", "/source_after/raw_hex", "/output/shape",
    "/output/pixel_type", "/output/S", "/output/raw_hex", "/output/in_plane_spacing",
)


def orientation_workspace():
    # Exact held mirror location; after adoption the ordinary repository ROOT
    # remains the source of immutable reference observations.
    return ROOT if (ROOT / "docs/parity/fixtures.json").is_file() else ROOT.parents[3]


@lru_cache(maxsize=1)
def orientation_original_inputs():
    workspace = orientation_workspace()
    prefix = "build/plan-implementation/p05f-axis-plane-export-001/"
    raw = (workspace / (prefix + "next-range-orientation-reference-001/capture-001/corpus.json")).read_bytes()
    assert len(raw) == 21527205 and digest(raw) == "990734ef132a40aafbaf7c177b3c5950d5f11ea20125e4234471afb561db9f9d"
    corpus = json.loads(raw)
    manifest = json.loads((workspace / "docs/parity/fixtures.json").read_bytes())
    declaration, = [row for row in manifest["fixtures"] if row["id"] == "F.G029.axis-range-orientation.pixels"]
    commands = json.loads((workspace / "docs/parity/commands.json").read_bytes())
    child, = [row for row in commands["requirements"] if row["id"] == "G029.axis_range_orientation"]
    return corpus, declaration, child


def orientation_inputs():
    return deepcopy(orientation_original_inputs())


def independent_orientation_expected(corpus):
    assert [row["case_id"] for row in corpus["cases"]] == list(ORIENTATION_IDS)
    expected = []
    for case in corpus["cases"]:
        kind, output = case["dtype"], case["output"]
        expected.append({
            "source_before": {"raw_hex": case["source_before"]["raw_hex"]},
            "source_after": {"raw_hex": case["source_after"]["raw_hex"]},
            "output": {
                "shape": [1, 1, case["requested_plane_count"], output["height"], output["width"], 3 if kind == "rgb" else 1],
                "pixel_type": {"u8": "UInt8", "u16": "UInt16", "f32": "Float32", "rgb": "UInt8"}[kind],
                "S": 3 if kind == "rgb" else 1, "raw_hex": output["raw_hex"],
                "in_plane_spacing": deepcopy(output["calibration"]["spacing"][:2]),
            },
        })
    return expected


def test_orientation_fixture_exact96_by7_keeps_original_reference_scope():
    corpus, declaration, _ = orientation_inputs()
    before = deepcopy(corpus)
    fixtures._embedded_identity(corpus, declaration, ORIENTATION_FORMAT, "orientation identity")
    projected = fixtures.project_cases(ORIENTATION_FORMAT, corpus)
    assert [row["locator"] for row in projected] == list(range(96))
    assert [row["expected"] for row in projected] == independent_orientation_expected(corpus)
    assert len(projected) * len(ORIENTATION_SELECTORS) == 672
    assert all(set(row["expected"]["output"]) == {"shape", "pixel_type", "S", "raw_hex", "in_plane_spacing"} for row in projected)
    assert corpus == before


@pytest.mark.parametrize("mutation", [
    "missing", "extra", "reorder", "duplicate", "wrong_id", "reverse", "flip", "transpose",
    "boolean_flag", "last_selected", "reverse_roi_end", "indices", "coordinates", "count",
    "source_before", "source_after", "source_original", "source_restored", "contiguous",
    "selected_output", "storage", "shape", "source_calibration", "output_spacing",
    "start_side", "rotate_option", "flip_option", "preference_restoration", "restoration_actions",
])
def test_orientation_fixture_refuses_changed_mapping_typed_bytes_and_restoration(mutation):
    corpus, _, _ = orientation_inputs()
    # An interior unaligned reverse+flip+transpose case exercises every index
    # mapping; the singleton-specific check below covers otherwise equal pixels.
    case = corpus["cases"][7]
    if mutation == "missing": corpus["cases"].pop()
    elif mutation == "extra": corpus["cases"].append(deepcopy(case))
    elif mutation == "reorder": corpus["cases"][0], corpus["cases"][1] = corpus["cases"][1], corpus["cases"][0]
    elif mutation == "duplicate": corpus["cases"][-1] = deepcopy(corpus["cases"][-2])
    elif mutation == "wrong_id": case["case_id"] = "owned.xy.f64.s1"
    elif mutation in ("reverse", "flip", "transpose"):
        key = {"reverse": "reverse_order", "flip": "flip_vertical", "transpose": "transpose"}[mutation]
        case[key] = not case[key]
    elif mutation == "boolean_flag": case["transpose"] = 1
    elif mutation == "last_selected": case["last_selected_zero_based"] = case["last_inclusive_zero_based"]
    elif mutation == "reverse_roi_end": case["reference_roi_last_zero_based"] = case["last_inclusive_zero_based"]
    elif mutation == "indices": case["selected_contiguous_indices_one_based"][0] = 2
    elif mutation == "coordinates": case["source_coordinates_zero_based"][0] += 1
    elif mutation == "count": case["requested_plane_count"] += 1
    elif mutation in ("source_before", "source_after", "source_original", "source_restored", "contiguous", "selected_output"):
        role = {"contiguous": "contiguous_output", "selected_output": "output"}.get(mutation, mutation)
        raw = case[role]["raw_hex"]
        case[role]["raw_hex"] = ("00" if raw[:2] != "00" else "01") + raw[2:]
    elif mutation == "storage": case["output"]["storage_raw_hex"] = "00"
    elif mutation == "shape": case["output"]["width"] += 1
    elif mutation == "source_calibration": case["source_restored"]["calibration"]["spacing"][0] += .5
    elif mutation == "output_spacing": case["output"]["calibration"]["spacing"][0] += .5
    elif mutation == "start_side": case["configured"]["startAt"] = "Top"
    elif mutation == "rotate_option": case["completed"]["rotate"] = False
    elif mutation == "flip_option": case["completed"]["flip"] = False
    elif mutation == "preference_restoration": case["preference_after"] = not case["preference_before"]
    elif mutation == "restoration_actions": case["restoration_actions"].pop()
    with pytest.raises(ValueError):
        fixtures.project_cases(ORIENTATION_FORMAT, corpus)


def test_orientation_singleton_identical_pixels_do_not_hide_changed_reverse_flag():
    corpus, _, _ = orientation_inputs()
    case = corpus["cases"][-1]
    assert case["requested_plane_count"] == 1 and case["reverse_order"] is True
    case["reverse_order"] = False
    with pytest.raises(ValueError): fixtures.project_cases(ORIENTATION_FORMAT, corpus)


def independent_orientation_contract(corpus, declaration, child):
    projected = fixtures.project_cases(ORIENTATION_FORMAT, corpus)
    expected = independent_orientation_expected(corpus)
    value = {"id": "FC.G029.axis-range-orientation.pixels", "fixture_id": declaration["id"],
        "format": ORIENTATION_FORMAT, "fixture_snapshot": independent_fixture_snapshot(declaration, [child]),
        "case_set": [{"case_id": identifier, "locator": index,
                      "input_sha256": fingerprint(projected[index]["input"]),
                      "expected_sha256": fingerprint(expected[index]),
                      "required_output_paths": list(ORIENTATION_SELECTORS)} for index, identifier in enumerate(ORIENTATION_IDS)],
        "requirement_scopes": [{"requirement_id": child["id"], "snapshot": child_snapshot(child),
            "required": [{"case_id": identifier, "path": path} for identifier in ORIENTATION_IDS for path in ORIENTATION_SELECTORS],
            "exclusions": ["Full signed affine/time/third-stride/XY/F64/RGBA/C2/C5/admission/publication/UI remain owned."]}],
        "comparison": {**declaration["comparison"], "numeric_paths": [], "units": "typed bytes and two positive in-plane scalar spacings",
            "rationale": "96 actual oriented finite reference cases, seven exact scientific selectors each.", "nonfinite": "reject"}}
    value["sha256"] = contract_digest(value)
    return value


@pytest.mark.parametrize("mutation", [None, "missing", "extra", "full_spacing", "order", "case", "scope_short", "owned_case"])
def test_orientation_contract_exact672_rejects_owned_scope_or_selector_substitution(mutation):
    corpus, declaration, child = orientation_inputs()
    # This synthetic contract exercises the original single-child 672-selector
    # scope. The repository declaration now also names a scientific child;
    # its separate coverage remains required by the real-corpus audit.
    declaration = deepcopy(declaration)
    declaration["requirement_ids"] = [child["id"]]
    contract = independent_orientation_contract(corpus, declaration, child)
    paths = contract["case_set"][0]["required_output_paths"]
    if mutation == "missing": paths.pop()
    elif mutation == "extra": paths.append("/output/direction")
    elif mutation == "full_spacing": paths[-1] = "/output/spacing"
    elif mutation == "order": paths.reverse()
    elif mutation == "case": contract["case_set"][0]["case_id"] = "missing.case"
    elif mutation == "scope_short": contract["requirement_scopes"][0]["required"].pop()
    elif mutation == "owned_case": contract["case_set"][0]["case_id"] = "owned.xy.f64.s1"
    contract["sha256"] = contract_digest(contract)
    result = {**{key: [] for key in GAPS}, **{key: 0 for key in COUNTS}}
    args = ({"fixture_contracts": [contract]}, {declaration["id"]: declaration}, {child["id"]: child},
            {declaration["id"]: fixtures.project_cases(ORIENTATION_FORMAT, corpus)}, result)
    if mutation is None:
        assert list(fixtures._validate_contracts(*args)) == [contract["id"]]
        assert result["required_case_assertions"] == 672 and result["contracts_validated"] == 1
    else:
        with pytest.raises(ValueError): fixtures._validate_contracts(*args)


@lru_cache(maxsize=1)
def orientation_original_sdk():
    workspace = orientation_workspace()
    comparison_path = workspace / "build/plan-implementation/p02a-gaussian-command-001/p05h-sdk117-comparison-001.json"
    comparison_bytes = comparison_path.read_bytes()
    assert len(comparison_bytes) == 486315 and digest(comparison_bytes) == "d93a12d2524415c7dcf2b84e8ad513f6d61aae6170f9fad80c9aa092c99ee3ba"
    comparison = json.loads(comparison_bytes)
    index_bytes = (workspace / comparison["index"]["path"]).read_bytes()
    assert len(index_bytes) == 1138428 and digest(index_bytes) == "5b7a545840203b0692add6c510993466a8634b98150e84cd435e7eaf7eb600ef"
    raw = {}
    for name, identity in comparison["actual_raw_storage"]["members"].items():
        content = (workspace / identity["path"]).read_bytes()
        assert len(content) == identity["bytes"] and digest(content) == identity["sha256"]
        raw[name] = content
    assert len(raw) == 116 and sum(map(len, raw.values())) == 807340
    return json.loads(index_bytes), raw


def orientation_sdk_case(tmp_path):
    index, raw = orientation_original_sdk()
    index = deepcopy(index)
    members = {name: dict(pin(tmp_path, name, content), bytes=len(content)) for name, content in raw.items()}
    index_pin = dict(pin(tmp_path, "index.json", json_bytes(index)), bytes=len(json_bytes(index)))
    return index, index_pin, members


def orientation_repin_index(tmp_path, index):
    content = json_bytes(index)
    return dict(pin(tmp_path, "index.json", content), bytes=len(content))


def test_orientation_sdk_projection_reads_actual_sidecars_not_comparison_status(tmp_path):
    index, index_pin, members = orientation_sdk_case(tmp_path)
    observed = fixtures.axis_range_orientation_sdk_observations(tmp_path, index_pin, members)
    corpus, _, _ = orientation_inputs()
    assert observed == independent_orientation_expected(corpus)
    assert len(observed) == 96 and len(observed) * len(ORIENTATION_SELECTORS) == 672
    assert all("origin" not in row["output"] and "direction" not in row["output"] for row in observed)


@pytest.mark.parametrize("mutation", [
    "missing_case", "extra_case", "reorder", "duplicate_case", "owned_as_reference", "nested_request",
    "direct_request", "flags", "source_metadata", "source_raw", "source_policy", "source_type",
    "output_shape", "output_S", "output_encoding", "output_policy", "output_length", "direct_mismatch",
    "nonfinite_spacing", "negative_spacing", "missing_raw", "corrupt_raw", "extra_raw", "wrong_pin",
    "wrong_index_pin", "traversing_raw", "aliased_raw", "duplicate_content_file",
])
def test_orientation_sdk_projection_refuses_incomplete_or_retargeted_actual_observations(tmp_path, monkeypatch, mutation):
    index, index_pin, members = orientation_sdk_case(tmp_path)
    row = index["reference_cases"][0]
    before = row["source_before"]["dataset"]
    output = row["output_snapshot"]["dataset"]
    if mutation == "missing_case": index["reference_cases"].pop()
    elif mutation == "extra_case": index["reference_cases"].append(deepcopy(row))
    elif mutation == "reorder": index["reference_cases"][0], index["reference_cases"][1] = index["reference_cases"][1], index["reference_cases"][0]
    elif mutation == "duplicate_case": index["reference_cases"][-1] = deepcopy(row)
    elif mutation == "owned_as_reference": index["reference_cases"][0] = deepcopy(index["owned_cases"][0])
    elif mutation == "nested_request": row["captured_request"]["range"]["first"] += 1
    elif mutation == "direct_request": row["direct_result"]["request"]["range"]["t"] = 1
    elif mutation == "flags": row["captured_request"]["transpose"] = True
    elif mutation == "source_metadata": row["source_after"]["properties"]["changed"] = True
    elif mutation == "source_raw": row["source_after"]["dataset"]["raw"] = deepcopy(output["raw"])
    elif mutation == "source_policy": before["sample_policy"] = "PreserveQuietNaNFloat32"
    elif mutation == "source_type": before["pixel_type"] = "Float64"
    elif mutation == "output_shape": output["shape"][-2] += 1
    elif mutation == "output_S": output["S"] = 4
    elif mutation == "output_encoding": output["encoding"] = "float64_le"
    elif mutation == "output_policy": output["sample_policy"] = "PreserveQuietNaNFloat32"
    elif mutation == "output_length": output["raw"]["bytes"] -= 1
    elif mutation == "direct_mismatch": row["direct_result"]["dataset"]["shape"][2] += 1
    elif mutation == "nonfinite_spacing": output["calibration"]["spacing"][0] = "NaN"
    elif mutation == "negative_spacing": output["calibration"]["spacing"][0] = -1
    elif mutation == "missing_raw": (tmp_path / output["raw"]["path"]).unlink()
    elif mutation == "corrupt_raw": (tmp_path / output["raw"]["path"]).write_bytes(b"bad")
    elif mutation == "extra_raw": (tmp_path / "raw/999999.bin").write_bytes(b"extra")
    elif mutation == "wrong_pin": members[output["raw"]["path"]]["sha256"] = "f" * 64
    elif mutation == "wrong_index_pin": index_pin["sha256"] = "f" * 64
    elif mutation == "traversing_raw": output["raw"]["path"] = "../escape.bin"
    elif mutation == "aliased_raw":
        target = tmp_path / output["raw"]["path"]
        original = Path.is_symlink
        monkeypatch.setattr(Path, "is_symlink", lambda path: path == target or original(path))
    elif mutation == "duplicate_content_file":
        content = (tmp_path / output["raw"]["path"]).read_bytes()
        name = "raw/999999.bin"
        members[name] = dict(pin(tmp_path, name, content), bytes=len(content))
        index["raw_member_count"] += 1
        index["raw_bytes_written"] += len(content)
    if mutation != "wrong_index_pin": index_pin = orientation_repin_index(tmp_path, index)
    with pytest.raises(ValueError):
        fixtures.axis_range_orientation_sdk_observations(tmp_path, index_pin, members)


@pytest.mark.parametrize("mutation", ["count_only", "hash_only", "self_consistent_wrong_pixels", "self_consistent_wrong_spacing"])
def test_orientation_scientific_values_cannot_be_replaced_by_summary_status_or_hashes(tmp_path, mutation):
    index, index_pin, members = orientation_sdk_case(tmp_path)
    output = index["reference_cases"][0]["output_snapshot"]["dataset"]
    if mutation == "count_only":
        index["reference_cases"] = [{"case_id": case_id, "matched_selectors": 7} for case_id in ORIENTATION_IDS]
        index["scientific_selectors_matched"] = 672
    elif mutation == "hash_only":
        output["raw"] = {"sha256": members[output["raw"]["path"]]["sha256"], "bytes": output["raw"]["bytes"]}
    elif mutation == "self_consistent_wrong_pixels":
        name = output["raw"]["path"]
        content = bytearray((tmp_path / name).read_bytes())
        content[0] ^= 1
        members[name] = dict(pin(tmp_path, name, bytes(content)), bytes=len(content))
    else:
        row = index["reference_cases"][0]
        for state in (row["output_snapshot"], row["repeat_publication"]["output_before"], row["repeat_publication"]["output_after"]):
            state["dataset"]["calibration"]["spacing"][0] = .5
        row["direct_result"]["dataset"]["calibration"]["spacing"][0] = .5
    index_pin = orientation_repin_index(tmp_path, index)
    corpus, _, _ = orientation_inputs()
    with pytest.raises(ValueError):
        observed = fixtures.axis_range_orientation_sdk_observations(tmp_path, index_pin, members)
        fixtures._compare(independent_orientation_expected(corpus), observed,
                          {"method": "exact", "atol": 0, "rtol": 0}, False, "owned changed actual science")


@pytest.mark.parametrize("mutation", ["fixture_path", "fixture_sha", "scope", "format", "generator",
    "driver", "members", "member_sha", "revision", "original_review", "variant", "capture_entry"])
def test_orientation_embedded_identity_refuses_unpinned_or_widened_reference(mutation):
    corpus, declaration, _ = orientation_inputs()
    if mutation == "fixture_path": declaration["path"] = "owned/recaptured.json"
    elif mutation == "fixture_sha": declaration["sha256"] = "0" * 64
    elif mutation == "scope": declaration["reference"]["scope"] += " Full signed native affine."
    elif mutation == "format": declaration["reference_format"] = "imagej_axis_range_v1"
    elif mutation == "generator": declaration["generator"]["sha256"] = "0" * 64
    elif mutation == "driver": corpus["driver"]["sha256"] = "0" * 64
    elif mutation == "members": declaration["reference"]["source_members"].pop()
    elif mutation == "member_sha": declaration["reference"]["source_members"][0]["sha256"] = "0" * 64
    elif mutation == "revision": corpus["imagej_commit"] = "0" * 40
    elif mutation == "original_review": corpus["existing_eligible_commercial_review"]["sha256"] = "0" * 64
    elif mutation == "variant": corpus["reference_variant"]["sha256"] = "0" * 64
    else: corpus["reviewed_entry"]["sha256"] = "0" * 64
    with pytest.raises(ValueError):
        fixtures._embedded_identity(corpus, declaration, ORIENTATION_FORMAT, "orientation embedded refusal")


@pytest.mark.parametrize("mutation", [None, "failed_reference", "timeout", "shell", "closure_short",
    "source_short", "jdk_short", "driver", "input_record_missing", "input_record_sha", "elapsed"])
def test_orientation_normalization_preserves_original_process_and_complete_closure(mutation):
    corpus, declaration, _ = orientation_inputs()
    before = deepcopy(corpus)
    if mutation == "failed_reference": corpus["processes"]["reference"]["exit_code"] = 1
    elif mutation == "timeout": corpus["processes"]["compile"]["timed_out"] = True
    elif mutation == "shell": corpus["processes"]["reference"]["shell"] = True
    elif mutation in ("closure_short", "source_short", "jdk_short"):
        role = {"closure_short": "classpath", "source_short": "sources", "jdk_short": "runtime"}[mutation]
        corpus["reuse"][role].pop(next(iter(corpus["reuse"][role])))
    elif mutation == "driver": corpus["driver"]["sha256"] = "0" * 64
    elif mutation == "input_record_missing": del corpus["process_records"]["reference"]["inputs"]
    elif mutation == "input_record_sha": corpus["process_records"]["reference"]["inputs"]["sha256"] = "invalid"
    elif mutation == "elapsed": corpus["processes"]["reference"]["elapsed_seconds"] = 120.01
    if mutation is not None:
        with pytest.raises(ValueError): fixtures.axis_range_orientation_capture_receipt(declaration, corpus)
        return
    receipt = fixtures.axis_range_orientation_capture_receipt(declaration, corpus)
    assert receipt["case_count"] == 96 and receipt["raw_output"] == corpus["processes"]["reference"]["stdout"]
    assert receipt["generator_sources"] == [corpus["script"]]
    assert receipt["driver"]["artifact"] == corpus["driver"] and len(receipt["reference_members"]) == 16
    provenance = receipt["orientation_capture_provenance"]
    assert provenance["original_capture"] == fixtures.ORIENTATION_REFERENCE_CAPTURE
    assert provenance["independent_formula_verification"] == fixtures.ORIENTATION_FORMULA
    assert provenance["process_records"] == corpus["process_records"]
    assert len(provenance["source_inputs"]) == 519
    assert len(receipt["runtime"]["classpath"]) == 472 and len(receipt["runtime"]["resources"]) == 579
    assert "receipt" not in receipt and "range_capture_provenance" not in receipt
    assert corpus == before


@pytest.mark.parametrize("role", ["receipt", "raw_output", "generator_sources", "reference_archive",
    "reference_members", "driver", "runtime"])
def test_orientation_capture_audit_refuses_each_missing_original_role(role):
    corpus, declaration, _ = orientation_inputs()
    body = fixtures.axis_range_orientation_capture_receipt(declaration, corpus)
    body["receipt"] = {"path": "synthetic/not-read.json", "sha256": "0" * 64, "bytes": 0}
    del body[role]
    contract = {"id": "FC.G029.axis-range-orientation.pixels", "format": ORIENTATION_FORMAT, "capture": body}
    result = {**{key: [] for key in GAPS}, **{key: 0 for key in COUNTS}}
    assert fixtures._orientation_validate_capture(orientation_workspace(), declaration, contract, corpus, result) is False
    assert result["capture_gaps"]


@pytest.mark.parametrize("mutation", ["changed_receipt", "missing_receipt", "corrupt_receipt_pin"])
def test_orientation_capture_audit_requires_original_normalized_receipt_bytes(tmp_path, mutation):
    corpus, declaration, _ = orientation_inputs()
    body = fixtures.axis_range_orientation_capture_receipt(declaration, corpus)
    if mutation == "changed_receipt": body["orientation_capture_provenance"]["reference_driver_only"] = False
    payload = json_bytes(body)
    path = tmp_path / "orientation-normalization.json"
    path.write_bytes(payload)
    capture = {**body, "receipt": {"path": path.relative_to(orientation_workspace()).as_posix(),
                                  "sha256": digest(payload), "bytes": len(payload)}}
    if mutation == "missing_receipt": path.unlink()
    elif mutation == "corrupt_receipt_pin": capture["receipt"]["sha256"] = "f" * 64
    result = {**{key: [] for key in GAPS}, **{key: 0 for key in COUNTS}}
    contract = {"id": "FC.G029.axis-range-orientation.pixels", "format": ORIENTATION_FORMAT, "capture": capture}
    if mutation == "missing_receipt":
        assert fixtures._orientation_validate_capture(orientation_workspace(), declaration, contract, corpus, result) is False
        assert result["unavailable_evidence"]
    else:
        with pytest.raises(ValueError):
            fixtures._orientation_validate_capture(orientation_workspace(), declaration, contract, corpus, result)


@pytest.mark.parametrize("mutation", [None, "missing", "corrupt", "shadow", "wrong_pin", "wrong_member_path",
    "traversal", "alias", "byte_budget"])
def test_orientation_original_tree_audit_requires_complete_safe_actual_members(tmp_path, monkeypatch, mutation):
    # Small independent classifier fixture, never a replacement ImageJ closure.
    first = dict(pin(tmp_path, "original/classes/ij/A.class", b"literal class A"), bytes=15)
    second = dict(pin(tmp_path, "original/classes/ij/sub/B.class", b"literal class B"), bytes=15)
    members = {"ij/A.class": first, "ij/sub/B.class": second}
    target = tmp_path / first["path"]
    if mutation == "missing": target.unlink()
    elif mutation == "corrupt": target.write_bytes(b"changed class A")
    elif mutation == "shadow": pin(tmp_path, "original/classes/ij/Shadow.class", b"unlisted shadow")
    elif mutation == "wrong_pin": first["sha256"] = "f" * 64
    elif mutation == "wrong_member_path": first["path"] = second["path"]
    elif mutation == "traversal": first["path"] = "original/classes/../escape.class"
    elif mutation == "alias":
        original = Path.is_symlink
        monkeypatch.setattr(Path, "is_symlink", lambda path: path == target or original(path))
    audit = fixtures._OrientationAudit(tmp_path, "synthetic closure", byte_limit=1 if mutation == "byte_budget" else 128)
    if mutation is None:
        assert fixtures._orientation_original_tree(audit, members, "classpath") is True
        assert audit.total == 30
    else:
        with pytest.raises(ValueError): fixtures._orientation_original_tree(audit, members, "classpath")


def fixture_source_archive_case(root):
    """Independent literal old header and declared snapshot; no code executes."""
    if root.drive and not str(root).startswith("\\\\?\\"):
        root = Path("\\\\?\\" + str(root.resolve()))
    logical = "include/imagejxx/execution.hpp"
    original = b"#pragma once\n// Specific historical accepted API.\n"
    current = original + b"// Additive current API.\n"
    member = "historical/source-snapshot/" + logical
    snapshot_pin = dict(pin(root, member, original), bytes=len(original))
    live_pin = {"path": logical, "sha256": digest(original), "bytes": len(original)}
    pin(root, logical, current)
    freeze = {"schema_version": 1, "sources": {logical: digest(original)},
              "source_snapshot": "historical/source-snapshot"}
    payload = json_bytes(freeze)
    freeze_pin = dict(pin(root, "historical/source-freeze.json", payload), bytes=len(payload))
    run = {"identity": {"source_freeze": freeze_pin}}
    producer = {"sources": [deepcopy(live_pin)], "artifacts": [deepcopy(live_pin)]}
    return root, live_pin, run, producer, freeze, snapshot_pin, original, current


@pytest.mark.parametrize("kind", ["sources", "artifacts"])
@pytest.mark.parametrize("live_state", ["additive_current", "missing_current"])
def test_fixture_producer_resolves_exact_declared_old_header_without_rewriting_inputs(tmp_path, kind, live_state):
    workspace, header, run, producer, _, snapshot, original, current = fixture_source_archive_case(tmp_path)
    live = workspace / header["path"]
    if live_state == "missing_current":
        live.unlink()
    metadata_before = deepcopy((header, run, producer, snapshot))
    files = {p: p.read_bytes() for p in (workspace / snapshot["path"], workspace / run["identity"]["source_freeze"]["path"])}
    result = {"unavailable_evidence": []}
    observed = fixtures._producer_bytes(workspace, header, run, producer, "owned frozen header", result, kind,
                                       run_id="owned", trace_id="owned.trace", contract_id="owned.contract")
    assert observed == original
    assert result["unavailable_evidence"] == []
    assert result["archived_sources"] == [{
        "run_id": "owned", "trace_id": "owned.trace", "contract_id": "owned.contract",
        "kind": "producer_source" if kind == "sources" else "producer_artifact",
        "path": header["path"], "snapshot_path": snapshot["path"],
        "sha256": digest(original), "bytes": len(original), "source_freeze": run["identity"]["source_freeze"],
    }]
    assert (header, run, producer, snapshot) == metadata_before
    assert {p: p.read_bytes() for p in files} == files
    assert live.exists() is (live_state == "additive_current")
    if live.exists():
        assert live.read_bytes() == current


def test_fixture_matching_live_header_does_not_require_or_consult_source_archive(tmp_path):
    workspace, header, run, producer, _, snapshot, original, _ = fixture_source_archive_case(tmp_path)
    (workspace / header["path"]).write_bytes(original)
    (workspace / snapshot["path"]).unlink()
    (workspace / run["identity"]["source_freeze"]["path"]).unlink()
    result = {"unavailable_evidence": []}
    assert fixtures._producer_bytes(workspace, header, run, producer, "owned exact live", result, "artifacts") == original
    assert result == {"unavailable_evidence": []}


@pytest.mark.parametrize("mutation", [
    "missing_freeze", "changed_freeze", "wrong_freeze_sha", "wrong_freeze_bytes", "invalid_json",
    "wrong_member_sha", "unlisted_member", "missing_member", "changed_member", "wrong_snapshot",
    "escaping_snapshot", "unsafe_source_key", "wrong_sources_type", "wrong_pin_sha", "wrong_pin_bytes",
    "wrong_pin_path", "no_declared_freeze",
])
def test_fixture_source_archive_requires_exact_declared_freeze_member_pin_and_bytes(tmp_path, mutation):
    workspace, header, run, producer, freeze, snapshot, original, _ = fixture_source_archive_case(tmp_path)
    freeze_path = workspace / run["identity"]["source_freeze"]["path"]
    member = workspace / snapshot["path"]
    if mutation == "missing_freeze":
        freeze_path.unlink()
    elif mutation == "changed_freeze":
        freeze_path.write_bytes(freeze_path.read_bytes() + b" ")
    elif mutation == "wrong_freeze_sha":
        run["identity"]["source_freeze"]["sha256"] = "f" * 64
    elif mutation == "wrong_freeze_bytes":
        run["identity"]["source_freeze"]["bytes"] += 1
    elif mutation == "invalid_json":
        payload = b'{"schema_version":1,"sources":'
        run["identity"]["source_freeze"] = dict(pin(workspace, "historical/source-freeze.json", payload), bytes=len(payload))
    elif mutation == "missing_member":
        member.unlink()
        # An arbitrary exact copy cannot substitute for the declared snapshot.
        pin(workspace, "historical/other-copy/" + header["path"], original)
    elif mutation == "changed_member":
        member.write_bytes(original + b"Changed historical bytes.\n")
    elif mutation == "wrong_pin_sha":
        header["sha256"] = "f" * 64
    elif mutation == "wrong_pin_bytes":
        header["bytes"] += 1
    elif mutation == "wrong_pin_path":
        header["path"] = "include/imagejxx/other.hpp"
        pin(workspace, header["path"], b"Different current header.\n")
    elif mutation == "no_declared_freeze":
        run["identity"] = {}  # Do not infer the nearby source-freeze.json.
    else:
        if mutation == "wrong_member_sha":
            freeze["sources"][header["path"]] = "f" * 64
        elif mutation == "unlisted_member":
            freeze["sources"] = {}
        elif mutation == "wrong_snapshot":
            freeze["source_snapshot"] = "historical/unrelated-snapshot"
        elif mutation == "escaping_snapshot":
            freeze["source_snapshot"] = "../outside-snapshot"
        elif mutation == "unsafe_source_key":
            freeze["sources"]["../outside.hpp"] = digest(original)
        elif mutation == "wrong_sources_type":
            freeze["sources"] = [header]
        payload = json_bytes(freeze)
        run["identity"]["source_freeze"] = dict(pin(workspace, "historical/source-freeze.json", payload), bytes=len(payload))
    result = {"unavailable_evidence": []}
    with pytest.raises(ValueError):
        fixtures._producer_bytes(workspace, header, run, producer, "owned archive refusal", result, "artifacts")
    assert "archived_sources" not in result


@pytest.mark.parametrize("alias", ["symlink", "junction", "resolved_escape"])
def test_fixture_source_archive_retains_snapshot_namespace_guards(tmp_path, monkeypatch, alias):
    workspace, header, run, producer, _, snapshot, original, _ = fixture_source_archive_case(tmp_path)
    member = workspace / snapshot["path"]
    if alias in {"symlink", "junction"}:
        method = "is_symlink" if alias == "symlink" else "is_junction"
        previous = getattr(Path, method, lambda self: False)
        monkeypatch.setattr(Path, method, lambda self: self == member.parent or previous(self), raising=False)
    else:
        outside = workspace / "outside.hpp"
        outside.write_bytes(original)
        previous = Path.resolve
        monkeypatch.setattr(Path, "resolve", lambda self, *args, **kwargs: outside if self == member else previous(self, *args, **kwargs))
    result = {"unavailable_evidence": []}
    with pytest.raises(ValueError):
        fixtures._producer_bytes(workspace, header, run, producer, "owned archive namespace", result, "artifacts")
    assert "archived_sources" not in result


def test_fixture_archive_excluded_tool_keeps_original_declared_tooling_chain(tmp_path):
    workspace, live, run, producer, snapshot = original_tooling_case(tmp_path)
    freeze = {"schema_version": 1, "sources": {}, "source_snapshot": "historical/source-snapshot"}
    payload = json_bytes(freeze)
    run["identity"]["source_freeze"] = dict(pin(workspace, "historical/source-freeze.json", payload), bytes=len(payload))
    result = {"unavailable_evidence": []}
    assert fixtures._producer_bytes(workspace, live, run, producer, "owned excluded tooling", result, "sources") == b"# independent historical tool source\n"
    assert "archived_sources" not in result
    assert result["archived_tool_sources"][0]["snapshot"] == snapshot


@pytest.mark.parametrize("role",["fixture","generator","helper","runtime","runtime_source","reference_archive","receipt","raw_output","report","producer_source","producer_artifact","trace"])
def test_unreadable_fixture_evidence_retains_gaps_and_strict_rejection(tmp_path,monkeypatch,role):
    graph=owned_set(tmp_path);target=graph[0]/evidence_pin(graph,role)["path"];original=Path.read_bytes
    def denied(self,*args,**kwargs):
        if self==target:raise PermissionError("owned denied fixture evidence")
        return original(self,*args,**kwargs)
    monkeypatch.setattr(Path,"read_bytes",denied)
    result=validate(graph)
    assert result["verified_case_assertions"]<result["required_case_assertions"] and any(result[key] for key in GAPS)
    for options in ({"require_evidence":True},{"require_complete":True}):
        with pytest.raises(ValueError):validate(graph,**options)
