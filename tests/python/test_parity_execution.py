"""Owned runtime traces qualify exact contracts and independent launch contexts."""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def load_exact(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


execution = load_exact(
    "owned_parity_execution_tests", ROOT / "scripts/parity_execution.py"
)
base = load_exact(
    "owned_surface_graph_builder", ROOT / "tests/python/test_parity_evidence.py"
)
evidence = base.evidence
CONTRACT = "SC.owned-neighbors"
CONTEXTS = ("cpp_api", "python_api", "imgui_cpp", "imgui_python")
OUTPUT_SHA = hashlib.sha256(bytes([0, 255])).hexdigest()
CASE_IDENTITIES = {
    "cpp_api": {"registered_test_id": 1, "name": "owned_native_one"},
    "imgui_cpp": {"registered_test_id": 2, "name": "owned_native_two"},
    "python_api": {"suite": "owned", "classname": "owned", "name": "one[u8]"},
    "imgui_python": {"suite": "owned", "classname": "owned", "name": "two[python]"},
}


def encoded(value):
    return (
        json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n"
    ).encode()


def digest(value):
    return hashlib.sha256(value).hexdigest()


def rules_for(context):
    rules = [
        {
            "id": "default_neighbors",
            "rule": "scalar_equal",
            "value_type": "integer",
            "expected": 1,
            "observation": "behavior",
        },
        {
            "id": "output_pixels",
            "rule": "sha256_equal",
            "expected": OUTPUT_SHA,
            "observation": "behavior",
        },
    ]
    if context.startswith("imgui_"):
        rules.append(
            {
                "id": "selection_state",
                "rule": "scalar_equal",
                "value_type": "string",
                "expected": "owned selected plane",
                "ui_state": True,
                "observation": "behavior",
            }
        )
    return rules


def ui_inputs():
    return [
        {"type": "pointer", "position": [20.5, 30.5], "button": "left", "down": True},
        {"type": "key", "key": "Enter", "down": True},
        {"type": "text", "text": "1"},
    ]


def owned_execution(tmp_path, imgui=True):
    root, documents, inventory = base.owned_graph(tmp_path)
    child = documents["commands.json"]["requirements"][0]
    if not imgui:
        child["interfaces"]["imgui"] = {
            "applicable": False,
            "entry_points": [],
            "evidence": [],
            "inapplicable_reason": "Owned headless contract has no interactive control.",
        }
    contexts = CONTEXTS if imgui else CONTEXTS[:2]
    python_report = base.PYTHON_REPORT.replace(
        b'name="one"', b'name="one[u8]"'
    ).replace(b'name="two"', b'name="two[python]"')
    (root / base.PATHS["python"]).write_bytes(python_report)
    contract = {
        "id": CONTRACT,
        "requirement_id": base.CHILD,
        "snapshot": execution.frozen_snapshot(child),
        "contexts": {context: rules_for(context) for context in contexts},
    }
    contract["sha256"] = execution.contract_fingerprint(contract)
    documents["commands.json"].update(
        surface_evidence_schema_version=1,
        surface_contracts=[contract],
        surface_proofs=[],
    )
    run = inventory["verification_runs"][0]
    run.update(surface_traces_schema_version=1, surface_traces=[])
    for context in contexts:
        role = "python" if context in {"python_api", "imgui_python"} else "native"
        producer_id = f"producer.{context}"
        trace_id = f"trace.{context}"
        trace_case_id = f"case.{context}"
        source_path = f"owned/producers/{context}.cpp"
        artifact_path = f"owned/artifacts/{context}.bin"
        producer_source = f"Owned synthetic {context} producer source.\n".encode()
        producer_artifact = f"Owned synthetic tested {context} artifact.\n".encode()
        for path, payload in (
            (source_path, producer_source),
            (artifact_path, producer_artifact),
        ):
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
        assertions = [
            dict(rule, observed=rule["expected"]) for rule in rules_for(context)
        ]
        case = {
            "id": trace_case_id,
            "requirement_id": base.CHILD,
            "context": context,
            "contract_id": CONTRACT,
            "contract_sha256": contract["sha256"],
            "report_role": role,
            "report_case_identity": deepcopy(CASE_IDENTITIES[context]),
            "assertions": assertions,
            "inputs": ui_inputs() if context.startswith("imgui_") else [],
        }
        trace_path = f"build/owned-evidence/{context}-trace.json"
        trace_payload = encoded(
            {
                "schema_version": 1,
                "run_id": base.RUN,
                "producer_id": producer_id,
                "cases": [case],
            }
        )
        (root / trace_path).write_bytes(trace_payload)
        run["surface_traces"].append(
            {
                "id": trace_id,
                "path": trace_path,
                "sha256": digest(trace_payload),
                "trace_case_id": trace_case_id,
                "report_role": role,
                "report_case_identity": deepcopy(CASE_IDENTITIES[context]),
                "producer": {
                    "id": producer_id,
                    "context": context,
                    "sources": [
                        {"path": source_path, "sha256": digest(producer_source)}
                    ],
                    "artifacts": [
                        {"path": artifact_path, "sha256": digest(producer_artifact)}
                    ],
                },
            }
        )
        documents["commands.json"]["surface_proofs"].append(
            {
                "id": f"proof.{context}",
                "requirement_id": base.CHILD,
                "context": context,
                "contract_id": CONTRACT,
                "run_id": base.RUN,
                "report_role": role,
                "report_case_identity": deepcopy(CASE_IDENTITIES[context]),
                "trace_id": trace_id,
                "trace_case_id": trace_case_id,
                "producer_id": producer_id,
            }
        )
    documents = evidence.seed_bindings(root, documents, inventory=inventory)
    return root, documents, inventory


def trace_entry(inventory, context):
    return next(
        row
        for row in inventory["verification_runs"][0]["surface_traces"]
        if row["producer"]["context"] == context
    )


def proof_entry(documents, context):
    return next(
        row
        for row in documents["commands.json"]["surface_proofs"]
        if row["context"] == context
    )


def trace_value(root, inventory, context):
    return json.loads((root / trace_entry(inventory, context)["path"]).read_bytes())


def revise_trace(root, documents, inventory, context, mutation=None, raw=None):
    entry = trace_entry(inventory, context)
    if raw is None:
        value = trace_value(root, inventory, context)
        mutation(value)
        raw = encoded(value)
    (root / entry["path"]).write_bytes(raw)
    entry["sha256"] = digest(raw)
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
        evidence.run_fingerprint(inventory["verification_runs"][0])
    )


def align_traces_with_contract(root, documents, inventory):
    """Build internally consistent owned evidence for a revised test contract."""
    contract = documents["commands.json"]["surface_contracts"][0]
    for context in contract["contexts"]:

        def mutation(trace):
            case = trace["cases"][0]
            case["contract_sha256"] = contract["sha256"]
            case["assertions"] = [
                deepcopy(dict(rule, observed=rule["expected"]))
                for rule in contract["contexts"][context]
            ]

        revise_trace(root, documents, inventory, context, mutation=mutation)


def set_at(value, keys, replacement):
    for key in keys[:-1]:
        value = value[key]
    value[keys[-1]] = replacement


def validate(root, documents, inventory, required=False):
    return execution.validate_execution(
        root, documents, inventory=inventory, require_surfaces=required
    )


def pin_owned_freeze(root, documents, inventory, freeze, raw=None):
    """Reseal only this test's independently authored accepted run."""
    path = "build/owned-evidence/source-freeze.json"
    payload = encoded(freeze) if raw is None else raw
    (root / path).write_bytes(payload)
    run = inventory["verification_runs"][0]
    run.setdefault("identity", {})["source_freeze"] = {
        "path": path,
        "sha256": digest(payload),
        "bytes": len(payload),
    }
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
        evidence.run_fingerprint(run)
    )


def owned_historical_source(tmp_path, kind="producer_source"):
    root, documents, inventory = owned_execution(tmp_path)
    producer = trace_entry(inventory, "cpp_api")["producer"]
    if kind == "producer_artifact":
        path = "owned/includes/executed.hpp"
        payload = b"// Independently authored executed source header.\n"
        member = root / path
        member.parent.mkdir(parents=True)
        member.write_bytes(payload)
        producer["artifacts"][0] = {"path": path, "sha256": digest(payload)}
    file = producer["sources" if kind == "producer_source" else "artifacts"][0]
    freeze = {
        "schema_version": 1,
        "source_snapshot": "build/owned-evidence/source-snapshot",
        "sources": {file["path"]: file["sha256"]},
    }
    archived = root / freeze["source_snapshot"] / file["path"]
    archived.parent.mkdir(parents=True)
    archived.write_bytes((root / file["path"]).read_bytes())
    pin_owned_freeze(root, documents, inventory, freeze)
    return root, documents, inventory, freeze, file, archived


def test_complete_owned_graph_proves_four_independent_contexts_without_promoting_status(
    tmp_path,
):
    root, documents, inventory = owned_execution(tmp_path)
    before = base.bytes_under(root)
    assert validate(root, documents, inventory, required=True) == {
        "requirements": 1,
        "required_contexts": 4,
        "verified_contexts": 4,
        "missing_proofs": [],
        "unavailable_evidence": [],
    }
    child = documents["commands.json"]["requirements"][0]
    assert child["verification_status"] == child["implementation_status"] == "partial"
    assert child["commercial"]["decision"] == "pending"
    assert base.bytes_under(root) == before


def test_inapplicable_imgui_preserves_reason_and_requires_only_api_contexts(tmp_path):
    root, documents, inventory = owned_execution(tmp_path, imgui=False)
    result = validate(root, documents, inventory, required=True)
    assert result["required_contexts"] == result["verified_contexts"] == 2
    interface = documents["commands.json"]["surface_contracts"][0]["snapshot"][
        "interfaces"
    ]["imgui"]
    assert interface["applicable"] is False
    assert (
        interface["inapplicable_reason"]
        == "Owned headless contract has no interactive control."
    )


def test_contract_hash_covers_fixed_identity_snapshot_and_rules_but_not_review_metadata(
    tmp_path,
):
    _, documents, _ = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    canonical = {
        key: contract[key] for key in ("id", "requirement_id", "snapshot", "contexts")
    }
    expected = digest(
        json.dumps(
            canonical,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode()
    )
    assert execution.contract_fingerprint(contract) == expected == contract["sha256"]
    enriched = deepcopy(contract)
    enriched.update(sha256="0" * 64, owned_review={"retain": "metadata"})
    assert execution.contract_fingerprint(enriched) == expected
    enriched["contexts"]["cpp_api"][0]["expected"] = 2
    assert execution.contract_fingerprint(enriched) != expected


def test_frozen_snapshot_excludes_mutable_evidence_status_and_consumers(tmp_path):
    _, documents, _ = owned_execution(tmp_path)
    child = documents["commands.json"]["requirements"][0]
    snapshot = execution.frozen_snapshot(child)
    changed = deepcopy(child)
    changed.update(
        implementation_status="complete",
        verification_status="verified",
        fixture_ids=[],
        verification_run_ids=[],
        remaining=[],
        settings_consumers=[{"key": "owned", "consumer": "later consumer"}],
    )
    changed["commercial"]["reason"] = "Later commercial review."
    changed["interfaces"]["cpp"]["evidence"] = ["later evidence"]
    assert execution.frozen_snapshot(changed) == snapshot
    changed["reference"]["scope"] += " Changed scope."
    assert execution.frozen_snapshot(changed) != snapshot


def test_legacy_candidate_only_graph_has_no_invented_execution_requirements(tmp_path):
    root, _, inventory = base.owned_graph(tmp_path)
    assert validate(root, base.legacy_documents(), inventory, required=True) == {
        "requirements": 0,
        "required_contexts": 0,
        "verified_contexts": 0,
        "missing_proofs": [],
        "unavailable_evidence": [],
    }


@pytest.mark.parametrize("implementation_status", ["partial", "complete"])
def test_partial_graph_reports_missing_proofs_independently_of_implementation_status(
    tmp_path, implementation_status
):
    root, documents, inventory = base.seeded_graph(tmp_path)
    documents["commands.json"]["requirements"][0]["implementation_status"] = (
        implementation_status
    )
    result = validate(root, documents, inventory)
    assert result["required_contexts"] == 4
    assert result["verified_contexts"] == 0
    assert {gap["context"] for gap in result["missing_proofs"]} == set(CONTEXTS)
    assert all(
        gap["requirement_id"] == base.CHILD and gap["reason"]
        for gap in result["missing_proofs"]
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory, required=True)


def test_verified_child_cannot_have_missing_structural_proofs(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    documents["commands.json"]["requirements"][0]["verification_status"] = "verified"
    documents["commands.json"]["surface_proofs"].pop()
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("context", CONTEXTS)
def test_each_missing_launch_context_remains_a_gap(tmp_path, context):
    root, documents, inventory = owned_execution(tmp_path)
    documents["commands.json"]["surface_proofs"].remove(proof_entry(documents, context))
    result = validate(root, documents, inventory)
    assert result["verified_contexts"] == 3
    assert len(result["missing_proofs"]) == 1
    assert result["missing_proofs"][0]["context"] == context
    with pytest.raises(ValueError):
        validate(root, documents, inventory, required=True)


@pytest.mark.parametrize(
    "case",
    [
        "duplicate_contract",
        "duplicate_proof",
        "duplicate_context",
        "orphan_requirement",
        "orphan_contract",
        "unknown_context",
        "unknown_run",
        "unknown_trace",
        "wrong_producer",
        "wrong_trace_case",
        "unlinked_run",
    ],
)
def test_duplicate_or_wrong_surface_links_reject_in_partial_mode(tmp_path, case):
    root, documents, inventory = owned_execution(tmp_path)
    commands = documents["commands.json"]
    proof = proof_entry(documents, "cpp_api")
    if case == "duplicate_contract":
        commands["surface_contracts"].append(deepcopy(commands["surface_contracts"][0]))
    elif case == "duplicate_proof":
        commands["surface_proofs"].append(deepcopy(proof))
    elif case == "duplicate_context":
        extra = deepcopy(proof)
        extra["id"] = "proof.other"
        commands["surface_proofs"].append(extra)
    elif case == "orphan_requirement":
        proof["requirement_id"] = "G052.missing"
    elif case == "orphan_contract":
        proof["contract_id"] = "SC.missing"
    elif case == "unknown_context":
        proof["context"] = "render_smoke"
    elif case == "unknown_run":
        proof["run_id"] = "missing-run"
    elif case == "unknown_trace":
        proof["trace_id"] = "trace.missing"
    elif case == "wrong_producer":
        proof["producer_id"] = "producer.imgui_cpp"
    elif case == "wrong_trace_case":
        proof["trace_case_id"] = "case.missing"
    else:
        commands["requirements"][0]["verification_run_ids"] = []
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize(
    "keys,value",
    [
        (("snapshot", "contract", "parameters", 0, "default"), 2),
        (("snapshot", "contract", "axes"), ["XYZ"]),
        (("snapshot", "reference", "scope"), "Relabelled upstream scope."),
        (("snapshot", "interfaces", "python", "entry_points"), ["different_api"]),
        (("sha256",), "0" * 64),
    ],
)
def test_stale_behavior_or_reference_snapshot_and_digest_reject(tmp_path, keys, value):
    root, documents, inventory = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    set_at(contract, keys, value)
    if keys != ("sha256",):
        contract["sha256"] = execution.contract_fingerprint(contract)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_case_extractor_preserves_exact_parametrized_and_registered_identities(
    tmp_path,
):
    root, _, _ = owned_execution(tmp_path)
    python_bytes = (root / base.PATHS["python"]).read_bytes()
    extracted = evidence.extract_report_cases(python_bytes, "pytest_junit_v1", "owned")
    assert extracted["totals"] == evidence.parse_report(
        python_bytes, "pytest_junit_v1", "owned"
    )
    assert extracted["cases"] == [
        {"identity": CASE_IDENTITIES["python_api"], "outcome": "passed"},
        {"identity": CASE_IDENTITIES["imgui_python"], "outcome": "passed"},
    ]
    native = b"1/2 Test #9: owned_a ... Passed 0.01 sec\n2/2 Test #4: owned_b ... Passed 0.02 sec\n100% tests passed, 0 tests failed out of 2\n"
    extracted = evidence.extract_report_cases(native, "ctest_log_v1", "owned")
    assert extracted["totals"] == evidence.parse_report(native, "ctest_log_v1", "owned")
    assert extracted["cases"] == [
        {"identity": {"registered_test_id": 9, "name": "owned_a"}, "outcome": "passed"},
        {"identity": {"registered_test_id": 4, "name": "owned_b"}, "outcome": "passed"},
    ]


@pytest.mark.parametrize(
    "format_name,payload",
    [
        (
            "pytest_junit_v1",
            base.PYTHON_REPORT.replace(b'failures="0"', b'failures="1"'),
        ),
        (
            "pytest_junit_v1",
            base.PYTHON_REPORT.replace(
                b'<testcase classname="owned" name="one" time="0.01"/>',
                b'<testcase name="one"><skipped/></testcase>',
            ),
        ),
        ("pytest_junit_v1", base.PYTHON_REPORT.replace(b'name="two"', b'name="one"')),
        ("ctest_log_v1", base.NATIVE_REPORT.replace(b"Test #2", b"Test #1")),
    ],
)
def test_case_extraction_reuses_failed_skipped_and_duplicate_outcome_rejection(
    format_name, payload
):
    with pytest.raises(ValueError):
        evidence.extract_report_cases(payload, format_name, "owned")
    with pytest.raises(ValueError):
        evidence.parse_report(payload, format_name, "owned")


@pytest.mark.parametrize(
    "case",
    [
        "substring_name",
        "wrong_class",
        "boolean_registered_id",
        "wrong_role",
        "reuse_cpp_ui",
    ],
)
def test_report_identity_is_exact_and_one_launch_cannot_stand_in_for_another(
    tmp_path, case
):
    root, documents, inventory = owned_execution(tmp_path)
    proof = proof_entry(documents, "python_api")
    if case == "substring_name":
        proof["report_case_identity"]["name"] = "one"
    elif case == "wrong_class":
        proof["report_case_identity"]["classname"] = "Owned"
    elif case == "boolean_registered_id":
        proof_entry(documents, "cpp_api")["report_case_identity"][
            "registered_test_id"
        ] = True
    elif case == "wrong_role":
        proof["report_role"] = "native"
    else:
        replacement = deepcopy(proof_entry(documents, "imgui_cpp"))
        replacement.update(id="proof.imgui_python", context="imgui_python")
        documents["commands.json"]["surface_proofs"].remove(
            proof_entry(documents, "imgui_python")
        )
        documents["commands.json"]["surface_proofs"].append(replacement)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize(
    "case",
    [
        "missing",
        "extra",
        "duplicate",
        "wrong_rule",
        "wrong_expected",
        "failed_observed",
        "bool_as_integer",
        "nonfinite",
        "wrong_sha",
    ],
)
def test_runtime_assertions_exactly_satisfy_frozen_ids_rules_and_typed_values(
    tmp_path, case
):
    root, documents, inventory = owned_execution(tmp_path)

    def mutation(value):
        assertions = value["cases"][0]["assertions"]
        if case == "missing":
            assertions.pop()
        elif case == "extra":
            assertions.append(
                {
                    "id": "extra",
                    "rule": "scalar_equal",
                    "value_type": "integer",
                    "expected": 1,
                    "observed": 1,
                }
            )
        elif case == "duplicate":
            assertions.append(deepcopy(assertions[0]))
        elif case == "wrong_rule":
            assertions[0]["rule"] = "truthy"
        elif case == "wrong_expected":
            assertions[0]["expected"] = 2
        elif case == "failed_observed":
            assertions[0]["observed"] = 2
        elif case == "bool_as_integer":
            assertions[0]["observed"] = True
        elif case == "nonfinite":
            assertions[0]["observed"] = float("nan")
        else:
            assertions[1]["observed"] = "0" * 64

    if case == "nonfinite":
        value = trace_value(root, inventory, "cpp_api")
        mutation(value)
        revise_trace(
            root, documents, inventory, "cpp_api", raw=json.dumps(value).encode()
        )
    else:
        revise_trace(root, documents, inventory, "cpp_api", mutation=mutation)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize(
    "keys,value",
    [
        (("run_id",), "wrong-run"),
        (("producer_id",), "producer.python_api"),
        (("schema_version",), True),
        (("cases", 0, "requirement_id"), "G052.wrong"),
        (("cases", 0, "context"), "python_api"),
        (("cases", 0, "contract_id"), "SC.wrong"),
        (("cases", 0, "contract_sha256"), "0" * 64),
        (("cases", 0, "report_case_identity", "registered_test_id"), True),
    ],
)
def test_runtime_trace_repeats_exact_frozen_run_case_contract_and_producer(
    tmp_path, keys, value
):
    root, documents, inventory = owned_execution(tmp_path)
    revise_trace(
        root,
        documents,
        inventory,
        "cpp_api",
        mutation=lambda trace: set_at(trace, keys, value),
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_duplicate_json_keys_cannot_be_hidden_by_standard_last_value_decoding(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    value = trace_value(root, inventory, "cpp_api")
    payload = json.dumps(value)[:-1] + ', "run_id": ' + json.dumps(base.RUN) + "}"
    revise_trace(root, documents, inventory, "cpp_api", raw=payload.encode())
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize(
    "inputs",
    [
        [],
        [{"type": "action", "name": "Apply"}],
        [{"type": "render", "ok": True}],
        [{"type": "pointer", "position": [0, 1], "button": "left", "down": 1}],
        [{"type": "pointer", "position": [0], "button": "left", "down": True}],
        [{"type": "key", "key": "any-key", "down": True}],
        [{"type": "text", "text": ""}],
    ],
)
def test_ui_requires_concrete_supported_input_not_action_or_render_markers(
    tmp_path, inputs
):
    root, documents, inventory = owned_execution(tmp_path)
    revise_trace(
        root,
        documents,
        inventory,
        "imgui_cpp",
        mutation=lambda value: value["cases"][0].update(inputs=inputs),
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_ui_contract_requires_asserted_resulting_state(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    for rule in contract["contexts"]["imgui_cpp"]:
        rule.pop("ui_state", None)
    contract["sha256"] = execution.contract_fingerprint(contract)
    align_traces_with_contract(root, documents, inventory)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_boolean_success_marker_alone_cannot_define_a_behavior_proof(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    contract["contexts"]["cpp_api"] = [
        {
            "id": "success",
            "rule": "scalar_equal",
            "value_type": "boolean",
            "expected": True,
        }
    ]
    contract["sha256"] = execution.contract_fingerprint(contract)
    align_traces_with_contract(root, documents, inventory)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("kind", ["trace", "source", "artifact", "report"])
def test_present_trace_producer_or_report_byte_drift_rejects_in_every_mode(
    tmp_path, kind
):
    root, documents, inventory = owned_execution(tmp_path)
    entry = trace_entry(inventory, "cpp_api")
    path = (
        entry["path"]
        if kind == "trace"
        else (
            entry["producer"]["sources" if kind == "source" else "artifacts"][0]["path"]
            if kind != "report"
            else base.PATHS["native"]
        )
    )
    target = root / path
    target.write_bytes(target.read_bytes() + b"Owned byte drift.\n")
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("kind", ["producer_source", "producer_artifact"])
@pytest.mark.parametrize("live_state", ["changed", "missing"])
def test_pinned_historical_source_qualifies_original_bytes_without_rewriting_evidence(
    tmp_path, kind, live_state
):
    root, documents, inventory, _, file, archived = owned_historical_source(
        tmp_path, kind
    )
    live = root / file["path"]
    if live_state == "changed":
        live.write_bytes(b"// Later implementation with different bytes.\n")
    else:
        live.unlink()
    before = deepcopy((documents, inventory))
    retained = {
        path: path.read_bytes() for path in root.rglob("*") if path.is_file()
    }
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == result["required_contexts"] == 4
    assert result["unavailable_evidence"] == []
    assert result["archived_sources"] == [
        {
            "run_id": base.RUN,
            "trace_id": "trace.cpp_api",
            "kind": kind,
            "path": file["path"],
            "snapshot_path": archived.relative_to(root).as_posix(),
            "sha256": file["sha256"],
            "source_freeze": inventory["verification_runs"][0]["identity"][
                "source_freeze"
            ],
        }
    ]
    assert (documents, inventory) == before
    assert {path: path.read_bytes() for path in retained} == retained
    assert live.exists() is (live_state == "changed")


def test_matching_live_source_keeps_default_result_without_consulting_archive(tmp_path):
    root, documents, inventory, freeze, _, archived = owned_historical_source(tmp_path)
    archived.unlink()
    (root / "build/owned-evidence/source-freeze.json").unlink()
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == 4
    assert "archived_sources" not in result
    assert freeze["sources"]  # Exact live bytes do not need historical resolution.


@pytest.mark.parametrize(
    "case,message",
    [
        ("missing_freeze", "pinned source freeze bytes are unavailable"),
        ("changed_freeze", "pinned source freeze byte fingerprint changed"),
        ("wrong_freeze_bytes", "pinned source freeze byte count changed"),
        ("invalid_freeze_json", "JSON"),
        ("wrong_member_sha", "producer/source freeze member fingerprint differs"),
        ("unlisted_member", "producer_source byte fingerprint changed"),
        ("missing_member", "pinned historical source snapshot bytes are unavailable"),
        ("changed_member", "historical source snapshot byte fingerprint changed"),
        ("wrong_snapshot", "pinned historical source snapshot bytes are unavailable"),
        ("escaping_snapshot", "requires a safe relative path"),
        ("unsafe_source_member", "requires a safe relative path"),
        ("wrong_sources_type", "requires an object"),
    ],
)
def test_historical_source_requires_exact_freeze_membership_and_snapshot(
    tmp_path, case, message
):
    root, documents, inventory, freeze, file, archived = owned_historical_source(tmp_path)
    (root / file["path"]).write_bytes(b"// Later source.\n")
    freeze_path = root / "build/owned-evidence/source-freeze.json"
    if case == "missing_freeze":
        freeze_path.unlink()
    elif case == "changed_freeze":
        freeze_path.write_bytes(freeze_path.read_bytes() + b" ")
    elif case == "wrong_freeze_bytes":
        run = inventory["verification_runs"][0]
        run["identity"]["source_freeze"]["bytes"] += 1
        documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
            evidence.run_fingerprint(run)
        )
    elif case == "invalid_freeze_json":
        pin_owned_freeze(root, documents, inventory, freeze, raw=b'{"sources":')
    elif case == "missing_member":
        archived.unlink()
    elif case == "changed_member":
        archived.write_bytes(b"// Incorrect historical bytes.\n")
    else:
        if case == "wrong_member_sha":
            freeze["sources"][file["path"]] = digest(b"Different original source.")
        elif case == "unlisted_member":
            freeze["sources"] = {}
        elif case == "wrong_snapshot":
            freeze["source_snapshot"] = "build/owned-evidence/other-snapshot"
        elif case == "escaping_snapshot":
            freeze["source_snapshot"] = "../outside-snapshot"
        elif case == "unsafe_source_member":
            freeze["sources"]["../outside.cpp"] = file["sha256"]
        elif case == "wrong_sources_type":
            freeze["sources"] = [file]
        pin_owned_freeze(root, documents, inventory, freeze)
    with pytest.raises(ValueError, match=message):
        validate(root, documents, inventory)


@pytest.mark.parametrize("alias", ["symlink", "junction", "resolved_escape"])
def test_historical_snapshot_cannot_resolve_through_alias_or_outside_workspace(
    tmp_path, monkeypatch, alias
):
    root, documents, inventory, _, file, archived = owned_historical_source(tmp_path)
    (root / file["path"]).write_bytes(b"// Later source.\n")
    # Model the filesystem seam without requiring Windows link-creation privileges.
    if alias in {"symlink", "junction"}:
        method = "is_symlink" if alias == "symlink" else "is_junction"
        original = getattr(Path, method, lambda self: False)
        monkeypatch.setattr(
            Path,
            method,
            lambda self: self == archived.parent or original(self),
            raising=False,
        )
        message = "historical source snapshot contains an aliased path"
    else:
        outside = tmp_path / "outside.cpp"
        outside.write_bytes(archived.read_bytes())
        original = Path.resolve
        monkeypatch.setattr(
            Path,
            "resolve",
            lambda self, *args, **kwargs: (
                outside if self == archived else original(self, *args, **kwargs)
            ),
        )
        message = "historical source snapshot escapes declared directory"
    with pytest.raises(ValueError, match=message):
        validate(root, documents, inventory)


@pytest.mark.parametrize("kind", ["binary_artifact", "raw_artifact", "trace", "report"])
def test_source_archive_never_replaces_non_source_evidence(tmp_path, kind):
    root, documents, inventory, freeze, _, _ = owned_historical_source(tmp_path)
    entry = trace_entry(inventory, "cpp_api")
    if kind in {"binary_artifact", "raw_artifact"}:
        artifact = entry["producer"]["artifacts"][0]
        if kind == "raw_artifact":
            payload = b"\x00\xff\x01\xfe"
            artifact.update(path="owned/capture/result.raw", sha256=digest(payload))
            target = root / artifact["path"]
            target.parent.mkdir(parents=True)
            target.write_bytes(payload)
            documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
                evidence.run_fingerprint(inventory["verification_runs"][0])
            )
        path = artifact["path"]
    elif kind == "trace":
        path = entry["path"]
    else:
        path = base.PATHS["native"]
    live = root / path
    archived = root / freeze["source_snapshot"] / path
    archived.parent.mkdir(parents=True, exist_ok=True)
    archived.write_bytes(live.read_bytes())
    assert path not in freeze["sources"]
    live.write_bytes(live.read_bytes() + b"Changed executed evidence.\n")
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("kind", ["trace", "source", "artifact"])
def test_missing_historical_execution_bytes_are_unavailable_and_never_verified(
    tmp_path, kind
):
    root, documents, inventory = owned_execution(tmp_path)
    entry = trace_entry(inventory, "cpp_api")
    path = (
        entry["path"]
        if kind == "trace"
        else entry["producer"]["sources" if kind == "source" else "artifacts"][0][
            "path"
        ]
    )
    (root / path).unlink()
    result = validate(root, documents, inventory)
    assert result["verified_contexts"] == 3
    assert any(
        item["context"] == "cpp_api"
        and item["path"] == path
        and item["kind"]
        and item["reason"]
        for item in result["unavailable_evidence"]
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory, required=True)


def test_missing_native_report_cannot_verify_either_cpp_context(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    (root / base.PATHS["native"]).unlink()
    result = validate(root, documents, inventory)
    assert result["verified_contexts"] == 2
    assert {item["context"] for item in result["unavailable_evidence"]} >= {
        "cpp_api",
        "imgui_cpp",
    }
    with pytest.raises(ValueError):
        validate(root, documents, inventory, required=True)


def test_historical_run_cannot_acquire_trace_catalogue_after_binding(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    inventory["verification_runs"][0]["surface_traces"].append(
        deepcopy(inventory["verification_runs"][0]["surface_traces"][0])
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_group_success_without_accepted_trace_catalogue_is_not_option_execution(
    tmp_path,
):
    root, documents, inventory = owned_execution(tmp_path)
    run = inventory["verification_runs"][0]
    run.pop("surface_traces_schema_version")
    run.pop("surface_traces")
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
        evidence.run_fingerprint(run)
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_clean_checkout_diagnoses_missing_execution_bytes_without_promoting_counts(
    tmp_path,
):
    _, documents, inventory = owned_execution(tmp_path)
    clean = tmp_path / "clean-source"
    clean.mkdir()
    result = validate(clean, documents, inventory)
    assert result["verified_contexts"] == 0
    assert result["unavailable_evidence"]
    with pytest.raises(ValueError):
        validate(clean, documents, inventory, required=True)
    assert not list(clean.iterdir())


@pytest.mark.parametrize(
    "value_type,expected",
    [
        ("boolean", False),
        ("integer", 1),
        ("number", 0.5),
        ("string", ""),
        ("null", None),
    ],
)
def test_supported_typed_scalar_observations_are_values_not_success_markers(
    tmp_path,
    value_type,
    expected,
):
    root, documents, inventory = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    contract["contexts"]["cpp_api"].append(
        {
            "id": "owned_typed_value",
            "rule": "scalar_equal",
            "value_type": value_type,
            "expected": expected,
        }
    )
    contract["sha256"] = execution.contract_fingerprint(contract)
    align_traces_with_contract(root, documents, inventory)
    assert validate(root, documents, inventory, required=True)["verified_contexts"] == 4


def test_runtime_other_child_and_contract_cannot_satisfy_selected_proof(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    commands = documents["commands.json"]
    other_child = deepcopy(commands["requirements"][0])
    other_child["id"] = "G052.owned-other"
    commands["requirements"].append(other_child)
    documents["fixtures.json"]["fixtures"][0]["requirement_ids"].append(
        other_child["id"]
    )
    other_contract = deepcopy(commands["surface_contracts"][0])
    other_contract.update(
        id="SC.owned-other",
        requirement_id=other_child["id"],
        snapshot=execution.frozen_snapshot(other_child),
    )
    other_contract["sha256"] = execution.contract_fingerprint(other_contract)
    commands["surface_contracts"].append(other_contract)
    assert validate(root, documents, inventory)["verified_contexts"] == 4
    revise_trace(
        root,
        documents,
        inventory,
        "cpp_api",
        mutation=lambda value: value["cases"][0].update(
            requirement_id=other_child["id"],
            contract_id=other_contract["id"],
            contract_sha256=other_contract["sha256"],
        ),
    )
    # A valid runtime subcase for the other child/contract cannot qualify the
    # original selected proof's requirement and frozen behavior.
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize(
    "case",
    [
        "duplicate_source",
        "duplicate_artifact",
        "shared_source_artifact_path",
        "missing_sources",
        "missing_artifacts",
        "unsafe_source",
        "unsafe_trace",
    ],
)
def test_catalogue_producer_and_trace_paths_are_nonempty_unique_and_safe(
    tmp_path, case
):
    root, documents, inventory = owned_execution(tmp_path)
    entry = trace_entry(inventory, "cpp_api")
    producer = entry["producer"]
    if case == "duplicate_source":
        producer["sources"].append(deepcopy(producer["sources"][0]))
    elif case == "duplicate_artifact":
        producer["artifacts"].append(deepcopy(producer["artifacts"][0]))
    elif case == "shared_source_artifact_path":
        producer["artifacts"][0] = deepcopy(producer["sources"][0])
    elif case == "missing_sources":
        producer["sources"] = []
    elif case == "missing_artifacts":
        producer["artifacts"] = []
    elif case == "unsafe_source":
        producer["sources"][0]["path"] = "../outside.cpp"
    else:
        entry["path"] = "../outside-trace.json"
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
        evidence.run_fingerprint(inventory["verification_runs"][0])
    )
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("location", ["commands", "accepted_run"])
def test_surface_versions_reject_boolean_schema_identity(tmp_path, location):
    root, documents, inventory = owned_execution(tmp_path)
    if location == "commands":
        documents["commands.json"]["surface_evidence_schema_version"] = True
    else:
        inventory["verification_runs"][0]["surface_traces_schema_version"] = True
        documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = (
            evidence.run_fingerprint(inventory["verification_runs"][0])
        )
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_exponent_overflow_is_not_a_finite_runtime_observation(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    trace = trace_value(root, inventory, "cpp_api")
    trace["cases"][0]["assertions"][0]["observed"] = "owned-exponent-placeholder"
    raw = json.dumps(trace).replace('"owned-exponent-placeholder"', "1e999").encode()
    revise_trace(root, documents, inventory, "cpp_api", raw=raw)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_unsupported_rule_rejects_even_with_matching_contract_and_runtime_rule(
    tmp_path,
):
    root, documents, inventory = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    contract["contexts"]["cpp_api"][0]["rule"] = "unsupported_compute"
    contract["sha256"] = execution.contract_fingerprint(contract)
    align_traces_with_contract(root, documents, inventory)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_verified_claim_with_missing_historical_reports_verifies_zero_contexts(
    tmp_path,
):
    root, documents, inventory = owned_execution(tmp_path)
    child = documents["commands.json"]["requirements"][0]
    child["verification_status"] = "verified"
    for role in ("native", "python"):
        (root / base.PATHS[role]).unlink()
    result = validate(root, documents, inventory)
    assert result["verified_contexts"] == 0
    assert result["missing_proofs"] == []
    assert {item["context"] for item in result["unavailable_evidence"]} == set(CONTEXTS)
    with pytest.raises(ValueError):
        validate(root, documents, inventory, required=True)
    assert (
        child["verification_status"] == "verified"
    )  # Validator never rewrites claims.


def test_candidate_only_partial_graph_does_not_require_invented_inventory_file(
    tmp_path,
):
    root = tmp_path / "legacy-partial"
    root.mkdir()
    result = execution.validate_execution(
        root, base.legacy_documents(), require_surfaces=True
    )
    assert result["required_contexts"] == result["verified_contexts"] == 0
    assert result["missing_proofs"] == result["unavailable_evidence"] == []
    assert not list(root.iterdir())


def test_nonboolean_identity_constants_do_not_qualify_behavior_observations(tmp_path):
    root, documents, inventory = owned_execution(tmp_path)
    contract = documents["commands.json"]["surface_contracts"][0]
    contract["contexts"]["cpp_api"] = [
        {
            "id": "producer_identity",
            "rule": "scalar_equal",
            "value_type": "string",
            "expected": "producer.cpp_api",
            "observation": "identity",
        },
        {
            "id": "identity_digest",
            "rule": "sha256_equal",
            "expected": digest(b"Owned producer identity."),
            "observation": "identity",
        },
    ]
    contract["sha256"] = execution.contract_fingerprint(contract)
    align_traces_with_contract(root, documents, inventory)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("change", ["missing", "identity_instead_of_behavior"])
def test_runtime_observation_classification_repeats_frozen_declaration(
    tmp_path, change
):
    root, documents, inventory = owned_execution(tmp_path)

    def mutation(value):
        assertion = value["cases"][0]["assertions"][0]
        if change == "missing":
            assertion.pop("observation")
        else:
            assertion["observation"] = "identity"

    revise_trace(root, documents, inventory, "cpp_api", mutation=mutation)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("change", ["missing", "false", "integer_true"])
def test_runtime_ui_state_repeats_frozen_known_semantic_flag(tmp_path, change):
    root, documents, inventory = owned_execution(tmp_path)

    def mutation(value):
        assertion = value["cases"][0]["assertions"][-1]
        if change == "missing":
            assertion.pop("ui_state")
        else:
            assertion["ui_state"] = False if change == "false" else 1

    revise_trace(root, documents, inventory, "imgui_cpp", mutation=mutation)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("kind", ["trace", "source", "artifact", "report"])
def test_unused_declared_catalogue_missing_bytes_remain_explicit_gaps(tmp_path, kind):
    root, documents, inventory = owned_execution(tmp_path)
    context = "cpp_api"
    entry = trace_entry(inventory, context)
    documents["commands.json"]["surface_proofs"].remove(proof_entry(documents, context))
    if kind == "trace":
        path = entry["path"]
    elif kind == "report":
        path = base.PATHS["native"]
    else:
        path = entry["producer"]["sources" if kind == "source" else "artifacts"][0][
            "path"
        ]
    (root / path).unlink()
    result = validate(root, documents, inventory)
    gap = next(
        item
        for item in result["unavailable_evidence"]
        if item.get("trace_id") == entry["id"]
    )
    assert gap["run_id"] == base.RUN
    assert gap["context"] == context
    assert gap["path"] == path
    assert gap["requirement_id"] == (None if kind == "trace" else base.CHILD)
    assert result["verified_contexts"] < result["required_contexts"]
    with pytest.raises(ValueError):
        validate(root, documents, inventory, required=True)


@pytest.mark.parametrize(
    "encoding", ["utf-16", "utf-16-le", "utf-16-be", "utf-32", "utf-32-le", "utf-32-be"]
)
def test_runtime_trace_rejects_non_utf8_json_with_consistent_byte_identities(
    tmp_path, encoding
):
    root, documents, inventory = owned_execution(tmp_path)
    value = trace_value(root, inventory, "cpp_api")
    payload = json.dumps(value).encode(encoding)
    # The JSON parser alone accepts this encoding. A trace's matching hashes
    # cannot make that representation satisfy the declared UTF-8 contract.
    assert json.loads(payload) == value
    revise_trace(root, documents, inventory, "cpp_api", raw=payload)
    with pytest.raises(ValueError, match="invalid assertion trace JSON"):
        validate(root, documents, inventory)


@pytest.mark.parametrize("invalid_bytes", [b"\xff", b"\xc3", b"\xed\xa0\x80"])
def test_runtime_trace_rejects_malformed_utf8_with_consistent_byte_identities(
    tmp_path, invalid_bytes
):
    root, documents, inventory = owned_execution(tmp_path)
    value = trace_value(root, inventory, "cpp_api")
    value["encoding_note"] = "owned-encoding-placeholder"
    payload = encoded(value).replace(b"owned-encoding-placeholder", invalid_bytes)
    revise_trace(root, documents, inventory, "cpp_api", raw=payload)
    with pytest.raises(ValueError, match="invalid assertion trace JSON"):
        validate(root, documents, inventory)


@pytest.mark.parametrize("encoding", ["utf-8", "utf-8-sig"])
def test_runtime_trace_valid_utf8_and_optional_bom_keep_exact_execution_proof(
    tmp_path, encoding
):
    root, documents, inventory = owned_execution(tmp_path)
    value = trace_value(root, inventory, "cpp_api")
    value["encoding_note"] = "Owned UTF-8: café, α and 中文."
    payload = json.dumps(value, ensure_ascii=False, allow_nan=False).encode(encoding)
    revise_trace(root, documents, inventory, "cpp_api", raw=payload)
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == result["required_contexts"] == 4
    assert result["missing_proofs"] == result["unavailable_evidence"] == []


def owned_original_surface_tooling(tmp_path, *, fixture_role="artifacts", mapping=None):
    if tmp_path.drive and not str(tmp_path).startswith("\\\\?\\"):
        tmp_path = Path("\\\\?\\" + str(tmp_path.resolve()))
    root, documents, inventory = owned_execution(tmp_path)

    def write(path, payload):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        return {"path": path, "sha256": digest(payload), "bytes": len(payload)}

    archive = "build/plan-implementation/p05f-axis-plane-export-001/source-preparation-019/scripts/parity_fixtures.py"
    original = b"# Independently authored original Python tooling.\n"
    snapshot = write(archive, original)
    live = {**snapshot, "path": "scripts/parity_fixtures.py"}
    write(live["path"], b"# Changed live Python tooling.\n")
    helper = write("original/surface_helper.py", b"# Original surface helper; never executed by validation.\n")
    mapped = snapshot if mapping is None else mapping(snapshot)
    fixture_data = ("PINS = " + repr({"adapter": mapped}) + "\nraise RuntimeError('Never execute helper code')\n").encode()
    fixture_helper = write("original/fixture_helper.py", fixture_data)
    configuration = write("original/configuration.json", encoded({
        "helper": helper, "prerequisite_pins": {"adapter019": live, "fixture_helper": fixture_helper}}))
    run = inventory["verification_runs"][0]
    run.setdefault("identity", {})["configuration"] = configuration
    producer = trace_entry(inventory, "cpp_api")["producer"]
    producer["sources"].append(helper)
    producer["artifacts"][0] = live
    producer[fixture_role].append(fixture_helper)
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(run)
    return root, documents, inventory, live, snapshot, configuration, helper, fixture_helper


@pytest.mark.parametrize("fixture_role", ["sources", "artifacts"])
@pytest.mark.parametrize("live_state", ["changed", "missing"])
def test_surface_tooling_artifact_resolves_original_explicit_chain_without_execution(tmp_path, fixture_role, live_state):
    root, documents, inventory, live, snapshot, config, helper, fixture_helper = owned_original_surface_tooling(tmp_path, fixture_role=fixture_role)
    if live_state == "missing":
        (root / live["path"]).unlink()
    before = deepcopy((documents, inventory))
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == result["required_contexts"] == 4
    assert result["missing_proofs"] == result["unavailable_evidence"] == []
    assert result["archived_tool_sources"] == [{
        "run_id": base.RUN, "trace_id": "trace.cpp_api", "kind": "archived_tool_source",
        "producer_kind": "producer_artifact", "path": live["path"], "snapshot": snapshot,
        "configuration": config, "helper": helper, "fixture_helper": fixture_helper,
    }]
    assert "archived_sources" not in result
    assert (documents, inventory) == before


@pytest.mark.parametrize("mutation", [
    "snapshot_missing", "snapshot_corrupt", "config_missing", "config_corrupt",
    "helper_missing", "helper_corrupt", "helper_mismatch", "fixture_helper_unlisted",
    "fixture_helper_corrupt", "prerequisite_changed", "unlisted_python", "non_tool_artifact",
    "wrong_mapping", "wrong_sha", "escaping_mapping", "source_role",
])
def test_surface_tooling_artifact_refuses_missing_corrupt_unlisted_or_wrong_role_chain(tmp_path, mutation):
    mapping = None
    if mutation == "wrong_mapping":
        mapping = lambda value: dict(value, path="arbitrary/later-copy.py")
    elif mutation == "wrong_sha":
        mapping = lambda value: dict(value, sha256="f" * 64)
    elif mutation == "escaping_mapping":
        mapping = lambda value: dict(value, path="../outside.py")
    root, documents, inventory, live, snapshot, config, helper, fixture_helper = owned_original_surface_tooling(tmp_path, mapping=mapping)
    producer = trace_entry(inventory, "cpp_api")["producer"]
    if mutation.endswith("_missing"):
        identity = {"snapshot_missing": snapshot, "config_missing": config, "helper_missing": helper}[mutation]
        (root / identity["path"]).unlink()
    elif mutation.endswith("_corrupt"):
        identity = {"snapshot_corrupt": snapshot, "config_corrupt": config, "helper_corrupt": helper,
                    "fixture_helper_corrupt": fixture_helper}[mutation]
        (root / identity["path"]).write_bytes(b"corrupt original bytes\n")
    elif mutation == "helper_mismatch":
        producer["sources"] = [row for row in producer["sources"] if row != helper]
    elif mutation == "fixture_helper_unlisted":
        producer["artifacts"] = [row for row in producer["artifacts"] if row != fixture_helper]
    elif mutation == "prerequisite_changed":
        payload = encoded({"helper": helper, "prerequisite_pins": {"adapter019": dict(live, bytes=live["bytes"] + 1), "fixture_helper": fixture_helper}})
        (root / config["path"]).write_bytes(payload)
        inventory["verification_runs"][0]["identity"]["configuration"] = {"path": config["path"], "sha256": digest(payload), "bytes": len(payload)}
    elif mutation in {"unlisted_python", "non_tool_artifact"}:
        path = "scripts/unlisted.py" if mutation == "unlisted_python" else "owned/artifacts/unlisted.bin"
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"changed unrelated bytes\n")
        producer["artifacts"][0] = dict(live, path=path)
    elif mutation == "source_role":
        producer["sources"].append(producer["artifacts"].pop(0))
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(inventory["verification_runs"][0])
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def owned_relocated_execution_tooling(tmp_path):
    root, documents, inventory, _, _, _, old_helper, _ = owned_original_surface_tooling(tmp_path)
    producer = trace_entry(inventory, "cpp_api")["producer"]
    sha = "506cd9d2437dc9a5659941454fcd3642e8c55484c96b2b37eda9d1ec6660912a"
    path = "build/plan-implementation/p05f-axis-plane-export-001/range-formal-preparation-001/tooling-repair-001/content-archive/" + sha + "/parity_execution.py"
    data = (ROOT / path).read_bytes()
    assert len(data) == 32419 and digest(data) == sha
    target = root / path
    target.parent.mkdir(parents=True)
    target.write_bytes(data)
    archived = {"path": path, "sha256": sha, "bytes": len(data)}
    file = dict(archived, path="scripts/parity_execution.py")
    (root / file["path"]).write_bytes(b"# Later execution validator source.\n")
    helper_path = "build/plan-implementation/p05f-axis-plane-export-001/source-preparation-029/formal_axis_plane_surfaces.py"
    helper_data = (ROOT / helper_path).read_bytes()
    helper_target = root / helper_path
    helper_target.parent.mkdir(parents=True, exist_ok=True)
    helper_target.write_bytes(helper_data)
    helper = {"path": helper_path, "sha256": digest(helper_data), "bytes": len(helper_data)}
    producer["sources"] = [item for item in producer["sources"] if item != old_helper]
    producer["sources"].append(helper)
    producer["artifacts"][0] = file
    config_data = encoded({"helper": helper, "prerequisite_pins": {"execution_helper": file}})
    config = {"path": "original/execution_config.json", "sha256": digest(config_data), "bytes": len(config_data)}
    (root / config["path"]).write_bytes(config_data)
    inventory["verification_runs"][0]["identity"]["configuration"] = config
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(inventory["verification_runs"][0])
    return root, documents, inventory, file, archived, helper


@pytest.mark.parametrize("live_state", ["changed", "missing"])
def test_surface_tooling_content_relocation_preserves_original_declared_identity_only(tmp_path, live_state):
    root, documents, inventory, file, archived, _ = owned_relocated_execution_tooling(tmp_path)
    if live_state == "missing":
        (root / file["path"]).unlink()
    before = deepcopy((documents, inventory))
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == result["required_contexts"] == 4
    relocated, = result["archived_tool_sources"]
    assert relocated["path"] == file["path"] and relocated["snapshot"] == archived
    assert relocated["archive_origin"] == "content_relocated_after_accepted_execution"
    assert relocated["original_processing_or_runtime_origin_requalified"] is False
    assert (documents, inventory) == before


@pytest.mark.parametrize("mutation", [
    "missing_archive", "corrupt_archive", "wrong_declared_sha", "wrong_declared_bytes",
    "helper_not_declared", "wrong_helper_identity", "source_role", "unlisted_path",
])
def test_surface_tooling_content_relocation_refuses_other_bytes_roles_or_membership(tmp_path, mutation):
    root, documents, inventory, file, archived, helper = owned_relocated_execution_tooling(tmp_path)
    producer = trace_entry(inventory, "cpp_api")["producer"]
    if mutation == "missing_archive":
        (root / archived["path"]).unlink()
    elif mutation == "corrupt_archive":
        (root / archived["path"]).write_bytes(b"corrupt archival content\n")
    elif mutation in {"wrong_declared_sha", "wrong_declared_bytes"}:
        changed = dict(file, **({"sha256": "f" * 64} if mutation == "wrong_declared_sha" else {"bytes": file["bytes"] + 1}))
        producer["artifacts"][0] = changed
        run = inventory["verification_runs"][0]
        old_config = run["identity"]["configuration"]
        payload = encoded({"helper": helper, "prerequisite_pins": {"execution_helper": changed}})
        (root / old_config["path"]).write_bytes(payload)
        run["identity"]["configuration"] = {"path": old_config["path"], "sha256": digest(payload), "bytes": len(payload)}
    elif mutation == "helper_not_declared":
        producer["sources"] = [item for item in producer["sources"] if item != helper]
    elif mutation == "wrong_helper_identity":
        payload = b"# Unrelated Python helper.\n"
        (root / helper["path"]).write_bytes(payload)
    elif mutation == "source_role":
        producer["sources"].append(producer["artifacts"].pop(0))
    else:
        path = "scripts/unlisted_execution.py"
        (root / path).write_bytes(b"changed unrelated tooling\n")
        producer["artifacts"][0] = dict(file, path=path)
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(inventory["verification_runs"][0])
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def owned_configured_surface_tooling(tmp_path, logical="scripts/parity_fixtures.py", with_header=False):
    """Literal explicit config pair; no Python helper code is executed."""
    if tmp_path.drive and not str(tmp_path).startswith("\\\\?\\"):
        tmp_path = Path("\\\\?\\" + str(tmp_path.resolve()))
    if with_header:
        root, documents, inventory, _, header, header_archive = owned_historical_source(tmp_path, "producer_artifact")
        (root / header["path"]).write_bytes(b"// Additive current header.\n")
    else:
        root, documents, inventory = owned_execution(tmp_path)
        header_archive = None

    def write(path, data):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return {"path": path, "sha256": digest(data), "bytes": len(data)}

    original = b"# Specific historical accepted Python tooling.\n"
    archive = write("original/surfaces/tool-source/" + logical, original)
    live = {**archive, "path": logical}
    write(logical, b"# Additive current Python tooling.\n")
    helper = write("original/surfaces/helper.py", b"raise RuntimeError('Never execute this helper')\n")
    body = {"helper": helper, "current_tooling_pins": {logical: live}, "tool_sources": [archive]}
    config = write("original/surfaces/configuration.json", encoded(body))
    run = inventory["verification_runs"][0]
    run.setdefault("identity", {})["configuration"] = config
    producer = trace_entry(inventory, "cpp_api")["producer"]
    producer["sources"].extend([helper, archive])
    if with_header:
        producer["artifacts"].append(live)
    else:
        producer["artifacts"][0] = live
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(run)
    return root, documents, inventory, live, archive, config, helper, body, header_archive


@pytest.mark.parametrize("logical", ["scripts/parity_fixtures.py", "scripts/parity_execution.py"])
@pytest.mark.parametrize("live_state", ["changed", "missing"])
def test_surface_declared_tool_source_pair_preserves_exact_original_bytes_and_all_four_contexts(tmp_path, logical, live_state):
    root, documents, inventory, live, archive, config, helper, _, _ = owned_configured_surface_tooling(tmp_path, logical)
    if live_state == "missing":
        (root / live["path"]).unlink()
    before = deepcopy((documents, inventory))
    retained = {path: (root / path).read_bytes() for path in (archive["path"], config["path"], helper["path"])}
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == result["required_contexts"] == 4
    assert result["missing_proofs"] == result["unavailable_evidence"] == []
    assert result["archived_tool_sources"] == [{
        "run_id": base.RUN, "trace_id": "trace.cpp_api", "kind": "archived_tool_source",
        "producer_kind": "producer_artifact", "path": logical, "snapshot": archive,
        "configuration": config, "helper": helper, "archive_origin": "declared_configuration_tool_sources",
    }]
    assert (documents, inventory) == before
    assert retained == {path: (root / path).read_bytes() for path in retained}
    if live_state == "changed":
        assert (root / logical).read_bytes() == b"# Additive current Python tooling.\n"
    else:
        assert not (root / logical).exists()


def test_surface_matching_live_tool_does_not_consult_declared_archive_pair(tmp_path):
    root, documents, inventory, live, archive, config, _, _, _ = owned_configured_surface_tooling(tmp_path)
    (root / live["path"]).write_bytes((root / archive["path"]).read_bytes())
    (root / archive["path"]).unlink()
    producer = trace_entry(inventory, "cpp_api")["producer"]
    producer["sources"] = [p for p in producer["sources"] if p != archive]
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(inventory["verification_runs"][0])
    # Only the logical current tool needs no archive; its config need not be
    # consulted for resolution. Other declared members remain exact.
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == 4
    assert "archived_tool_sources" not in result
    assert result["unavailable_evidence"] == []


@pytest.mark.parametrize("mutation", [
    "config_missing", "config_corrupt", "logical_mismatch", "logical_unlisted", "pair_incomplete",
    "archive_missing", "archive_corrupt", "wrong_archive_sha", "wrong_archive_bytes", "archive_path_wrong",
    "archive_duplicate", "archive_unlisted_source", "helper_missing", "helper_corrupt", "helper_unlisted_source",
    "source_role", "compiled_artifact", "unsupported_python_path", "no_configuration",
    "wrong_tool_sources_type", "wrong_current_tooling_type",
])
def test_surface_declared_tool_source_pair_rejects_unbound_roles_members_bytes_or_configuration(tmp_path, mutation):
    root, documents, inventory, live, archive, config, helper, body, _ = owned_configured_surface_tooling(tmp_path)
    producer = trace_entry(inventory, "cpp_api")["producer"]
    run = inventory["verification_runs"][0]
    rewrite = False
    if mutation in {"config_missing", "archive_missing", "helper_missing"}:
        p = {"config_missing": config, "archive_missing": archive, "helper_missing": helper}[mutation]
        (root / p["path"]).unlink()
        if mutation == "archive_missing":
            other = root / "unlisted/exact-old-copy.py"
            other.parent.mkdir(parents=True)
            other.write_bytes(b"# Specific historical accepted Python tooling.\n")
    elif mutation in {"config_corrupt", "archive_corrupt", "helper_corrupt"}:
        p = {"config_corrupt": config, "archive_corrupt": archive, "helper_corrupt": helper}[mutation]
        (root / p["path"]).write_bytes(b"Changed accepted member.\n")
    elif mutation == "logical_mismatch":
        body["current_tooling_pins"][live["path"]] = dict(live, bytes=live["bytes"] + 1)
        rewrite = True
    elif mutation == "logical_unlisted":
        body["current_tooling_pins"] = {}
        rewrite = True
    elif mutation == "pair_incomplete":
        del body["tool_sources"]
        rewrite = True
    elif mutation in {"wrong_archive_sha", "wrong_archive_bytes", "archive_path_wrong"}:
        key, value = {"wrong_archive_sha": ("sha256", "f" * 64),
                      "wrong_archive_bytes": ("bytes", archive["bytes"] + 1),
                      "archive_path_wrong": ("path", "arbitrary/old-copy.py")}[mutation]
        body["tool_sources"] = [dict(archive, **{key: value})]
        rewrite = True
    elif mutation == "archive_duplicate":
        body["tool_sources"].append(deepcopy(archive))
        rewrite = True
    elif mutation in {"archive_unlisted_source", "helper_unlisted_source"}:
        p = archive if mutation == "archive_unlisted_source" else helper
        producer["sources"] = [row for row in producer["sources"] if row != p]
    elif mutation == "source_role":
        producer["sources"].append(producer["artifacts"].pop(0))
    elif mutation in {"compiled_artifact", "unsupported_python_path"}:
        path = "owned/compiled/runtime.dll" if mutation == "compiled_artifact" else "scripts/unlisted.py"
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"Changed unrelated artifact.\n")
        producer["artifacts"][0] = dict(live, path=path)
    elif mutation == "no_configuration":
        del run["identity"]["configuration"]
    elif mutation == "wrong_tool_sources_type":
        body["tool_sources"] = {"member": archive}
        rewrite = True
    elif mutation == "wrong_current_tooling_type":
        body["current_tooling_pins"] = [live]
        rewrite = True
    if rewrite:
        payload = encoded(body)
        (root / config["path"]).write_bytes(payload)
        run["identity"]["configuration"] = dict(config, sha256=digest(payload), bytes=len(payload))
    documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = evidence.run_fingerprint(run)
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


@pytest.mark.parametrize("alias", ["symlink", "junction", "resolved_escape"])
def test_surface_declared_tool_source_pair_retains_original_namespace_guards(tmp_path, monkeypatch, alias):
    root, documents, inventory, _, archive, _, _, _, _ = owned_configured_surface_tooling(tmp_path)
    member = root / archive["path"]
    if alias in {"symlink", "junction"}:
        method = "is_symlink" if alias == "symlink" else "is_junction"
        previous = getattr(Path, method, lambda self: False)
        monkeypatch.setattr(Path, method, lambda self: self == member.parent or previous(self), raising=False)
    else:
        outside = root.parent / "outside-tool.py"
        outside.write_bytes(member.read_bytes())
        previous = Path.resolve
        monkeypatch.setattr(Path, "resolve", lambda self, *args, **kwargs: outside if self == member else previous(self, *args, **kwargs))
    with pytest.raises(ValueError):
        validate(root, documents, inventory)


def test_surface_frozen_header_and_explicit_tool_pair_preserve_distinct_original_identities(tmp_path):
    root, documents, inventory, live, archive, _, _, _, header_archive = owned_configured_surface_tooling(tmp_path, with_header=True)
    before = deepcopy((documents, inventory))
    result = validate(root, documents, inventory, required=True)
    assert result["verified_contexts"] == result["required_contexts"] == 4
    assert len(result["archived_sources"]) == len(result["archived_tool_sources"]) == 1
    assert result["archived_sources"][0]["kind"] == "producer_artifact"
    assert result["archived_sources"][0]["snapshot_path"] == header_archive.relative_to(root).as_posix()
    assert result["archived_tool_sources"][0]["path"] == live["path"]
    assert result["archived_tool_sources"][0]["snapshot"] == archive
    assert result["unavailable_evidence"] == []
    assert (documents, inventory) == before


@pytest.mark.parametrize("kind", ["trace", "source", "artifact", "report"])
def test_unreadable_historical_execution_retains_gap_and_strict_rejection(tmp_path,monkeypatch,kind):
    root,documents,inventory=owned_execution(tmp_path)
    entry=trace_entry(inventory,"cpp_api")
    path=base.PATHS["native"] if kind=="report" else entry["path"] if kind=="trace" else entry["producer"]["sources" if kind=="source" else "artifacts"][0]["path"]
    target=root/path;original=Path.read_bytes
    def denied(self,*args,**kwargs):
        if self==target:raise PermissionError("owned denied evidence")
        return original(self,*args,**kwargs)
    monkeypatch.setattr(Path,"read_bytes",denied)
    result=validate(root,documents,inventory)
    assert result["verified_contexts"]==(2 if kind=="report" else 3)
    assert any(row["path"]==path for row in result["unavailable_evidence"])
    with pytest.raises(ValueError):validate(root,documents,inventory,required=True)


@pytest.mark.parametrize("mutation",["none","corrupt","wrong_size","wrong_role","other_path","missing"])
def test_only_exact_historical_evidence_tooling_preimage_is_recovered(tmp_path,mutation):
    data=b"# owned old audit-helper bytes\n";sha=digest(data)
    file={"path":"scripts/parity_evidence.py","sha256":sha,"bytes":len(data)}
    stored=tmp_path/"tests/fixtures/tooling-preimages"/sha/"parity_evidence.py.txt"
    stored.parent.mkdir(parents=True);stored.write_bytes(data)
    result={};kind="producer_artifact"
    if mutation=="corrupt":stored.write_bytes(b"changed bytes")
    elif mutation=="wrong_size":file["bytes"]+=1
    elif mutation=="wrong_role":kind="producer_source"
    elif mutation=="other_path":file["path"]="owned/current.exe"
    elif mutation=="missing":stored.unlink()
    if mutation in ("corrupt","wrong_size"):
        with pytest.raises(ValueError):execution.stored_evidence_tool_source(tmp_path,file,kind,"owned",result)
    else:
        actual=execution.stored_evidence_tool_source(tmp_path,file,kind,"owned",result)
        if mutation=="none":
            assert actual==data and result["archived_tool_sources"][0]["original_processing_or_runtime_origin_requalified"] is False
        else:assert actual is None and result=={}


def test_oversized_historical_tooling_preimage_is_rejected_before_read(tmp_path, monkeypatch):
    data = b"# owned old audit-helper bytes\n"
    sha = digest(data)
    stored = tmp_path / "tests/fixtures/tooling-preimages" / sha / "parity_evidence.py.txt"
    stored.parent.mkdir(parents=True)
    with stored.open("wb") as stream:
        stream.truncate(2 * 1024 * 1024 + 1)

    def forbidden_read(*args, **kwargs):
        pytest.fail("oversized historical helper must not be read")

    monkeypatch.setattr(execution.evidence, "historical_bytes", forbidden_read)
    with pytest.raises(ValueError, match="exceeds byte limit"):
        execution.stored_evidence_tool_source(
            tmp_path, {"path": "scripts/parity_evidence.py", "sha256": sha},
            "producer_artifact", "owned", {},
        )
