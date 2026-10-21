"""Validate frozen executed surfaces without certifying whole-feature parity.

The optional P01b2b extension binds reviewed assertions to exact passing report
cases, runtime traces and the tested producer bytes in immutable accepted runs.
Missing bytes remain explicit gaps in a clean checkout; present invalid bytes
always reject. No historical assertion traces are inferred or generated here.
"""

from __future__ import annotations

import argparse
import ast
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "imagejxx_execution_evidence", Path(__file__).with_name("parity_evidence.py")
)
evidence = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(evidence)
schema = evidence.schema
CONTEXTS = {"cpp_api", "python_api", "imgui_cpp", "imgui_python"}
EXTENSION = {"surface_evidence_schema_version", "surface_contracts", "surface_proofs"}
SCALAR_TYPES = {"boolean", "integer", "number", "string", "null"}
NAMED_KEYS = {
    "Ctrl",
    "Shift",
    "Alt",
    "Super",
    "Enter",
    "Escape",
    "Tab",
    "Backspace",
    "Delete",
    "Space",
    "Home",
    "End",
    "PageUp",
    "PageDown",
    "Left",
    "Right",
    "Up",
    "Down",
    *{f"F{i}" for i in range(1, 25)},
}


def canonical(value):
    """Canonical typed JSON: bool/int and int/float identities stay distinct."""
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def same(left, right):
    return canonical(left) == canonical(right)


def finite_json(value, where):
    if isinstance(value, dict):
        for key, item in value.items():
            schema.text(key, where, empty=True)
            finite_json(item, where)
    elif isinstance(value, list):
        for item in value:
            finite_json(item, where)
    elif type(value) in {int, float}:
        schema.number(value, where)
    elif type(value) not in {str, bool, type(None)}:
        schema.fail(where, "unsupported JSON scalar type")


def strict_json(data, where):
    def pairs(items):
        result = {}
        for key, item in items:
            if key in result:
                schema.fail(where, "duplicate JSON object key")
            result[key] = item
        return result

    def constant(value):
        schema.fail(where, "nonfinite JSON numeric value")

    try:
        # Decode explicitly: json.loads(bytes) also auto-detects UTF-16/UTF-32.
        # Retain the previously supported optional UTF-8 BOM.
        content = data.decode("utf-8-sig")
        result = json.loads(content, object_pairs_hook=pairs, parse_constant=constant)
    except (json.JSONDecodeError, UnicodeError) as error:
        schema.fail(where, f"invalid assertion trace JSON: {error}")
    finite_json(result, where)
    return result


def required_contexts(child):
    result = set()
    interfaces = child["interfaces"]
    if interfaces["cpp"]["applicable"]:
        result.add("cpp_api")
    if interfaces["python"]["applicable"]:
        result.add("python_api")
    if interfaces["imgui"]["applicable"]:
        result.update({"imgui_cpp", "imgui_python"})
    return result


def frozen_snapshot(child):
    interfaces = {}
    for name in ("cpp", "python", "imgui"):
        source = child["interfaces"][name]
        interfaces[name] = {
            key: deepcopy(source[key])
            for key in ("applicable", "entry_points", "inapplicable_reason")
            if key in source
        }
    return {
        "reference": deepcopy(child["reference"]),
        "contract": deepcopy(child["contract"]),
        "interfaces": interfaces,
    }


def contract_fingerprint(contract):
    return hashlib.sha256(
        canonical(
            {
                key: contract[key]
                for key in ("id", "requirement_id", "snapshot", "contexts")
            }
        )
    ).hexdigest()


def context(value, where):
    schema.choice(value, CONTEXTS, where)
    return value


def case_identity(value, format_name, where):
    value = schema.obj(value, where)
    if format_name == "pytest_junit_v1":
        if set(value) != {"suite", "classname", "name"}:
            schema.fail(where, "requires exact JUnit suite/classname/name identity")
        schema.text(value["suite"], where, empty=True)
        schema.text(value["classname"], where, empty=True)
        schema.text(value["name"], where)
    elif format_name == "ctest_log_v1":
        if set(value) != {"registered_test_id", "name"}:
            schema.fail(where, "requires exact CTest registered test ID/name identity")
        if (
            type(value["registered_test_id"]) is not int
            or value["registered_test_id"] <= 0
        ):
            schema.fail(where, "CTest registered test ID must be a positive integer")
        schema.text(value["name"], where)
    else:
        schema.fail(where, "unsupported exact-case report format")
    return value


def assertion_rule(value, where, observed=False):
    schema.obj(value, where)
    schema.identity(value.get("id"), where)
    rule = value.get("rule")
    schema.choice(rule, {"scalar_equal", "sha256_equal"}, where)
    keys = ("expected", "observed") if observed else ("expected",)
    if rule == "scalar_equal":
        kind = value.get("value_type")
        schema.choice(kind, SCALAR_TYPES, where)
        for key in keys:
            if key not in value:
                schema.fail(where, "missing assertion value")
            item = value[key]
            if kind == "boolean" and type(item) is not bool:
                schema.fail(where, "boolean assertion requires bool")
            if kind == "integer":
                schema.number(item, where, integer=True)
            if kind == "number":
                schema.number(item, where)
            if kind == "string":
                schema.text(item, where, empty=True)
            if kind == "null" and item is not None:
                schema.fail(where, "null assertion requires null")
    else:
        if "value_type" in value:
            schema.fail(where, "digest assertion cannot declare scalar value_type")
        for key in keys:
            schema.sha(value.get(key), where)
    if "ui_state" in value and type(value["ui_state"]) is not bool:
        schema.fail(where, "ui_state requires bool")
    if "observation" in value:
        schema.choice(value["observation"], {"behavior", "identity"}, where)
    if observed and not same(value["expected"], value["observed"]):
        schema.fail(where, "runtime assertion observation differs from expected value")


def validate_contracts(commands, requirements):
    contracts = {}
    children = set()
    for row in schema.rows(commands.get("surface_contracts", []), "surface contracts"):
        where = row["id"]
        child_id = schema.identity(row.get("requirement_id"), where)
        if child_id not in requirements or child_id in children:
            schema.fail(where, "orphaned or duplicate requirement surface contract")
        children.add(child_id)
        child = requirements[child_id]
        snapshot = schema.obj(row.get("snapshot"), where)
        finite_json(snapshot, where)
        if not same(snapshot, frozen_snapshot(child)):
            schema.fail(
                where, "frozen surface snapshot differs from current requirement"
            )
        declared = schema.obj(row.get("contexts"), where)
        if set(declared) != required_contexts(child):
            schema.fail(where, "contract contexts differ from interface applicability")
        for name, assertions in declared.items():
            context(name, where)
            assertions = schema.rows(assertions, where + " " + name)
            if not assertions:
                schema.fail(where, "each context requires frozen assertions")
            for assertion in assertions:
                assertion_rule(assertion, where)
                finite_json(assertion, where)
            if not any(
                assertion.get("observation") == "behavior"
                and (
                    assertion["rule"] == "sha256_equal"
                    or assertion["value_type"] != "boolean"
                )
                for assertion in assertions
            ):
                schema.fail(
                    where,
                    "context requires a non-boolean computed behavior observation; pass/identity markers are insufficient",
                )
            if name.startswith("imgui_") and not any(
                a.get("ui_state") is True and a.get("observation") == "behavior"
                for a in assertions
            ):
                schema.fail(
                    where, "UI contract requires a frozen resulting-state assertion"
                )
        schema.sha(row.get("sha256"), where)
        if row["sha256"] != contract_fingerprint(row):
            schema.fail(where, "frozen surface contract fingerprint changed")
        contracts[where] = row
    return contracts


def file_identity(value, where):
    schema.obj(value, where)
    schema.path(value.get("path"), where)
    schema.sha(value.get("sha256"), where)


def producer_identity(value, where):
    schema.obj(value, where)
    schema.identity(value.get("id"), where)
    context(value.get("context"), where)
    names = set()
    for kind in ("sources", "artifacts"):
        rows = schema.array(value.get(kind), where)
        if not rows:
            schema.fail(where, "producer source/artifact identities must be nonempty")
        for row in rows:
            file_identity(row, where)
            if row["path"] in names:
                schema.fail(where, "duplicate producer source/artifact path identity")
            names.add(row["path"])


def stored_evidence_tool_source(root, file, kind, where, result, **identity):
    """Read an exact old audit-helper preimage; never runtime/trace artifacts.

    The accepted run already binds its logical path/hash. The content store is
    data only and cannot redefine that identity or certify current processing.
    """
    if kind != "producer_artifact" or file["path"] != "scripts/parity_evidence.py":
        return None
    file_identity(file, where)
    workspace = Path(root).resolve()
    relative = "tests/fixtures/tooling-preimages/" + file["sha256"] + "/parity_evidence.py.txt"
    member = workspace / relative
    for node in (member, *member.parents):
        if node == workspace:
            break
        if node.is_symlink() or getattr(node, "is_junction", lambda: False)():
            schema.fail(where, "historical tooling preimage contains an alias")
    try:
        target = evidence.report_path(workspace, relative, where)
        if target.stat().st_size > 2 * 1024 * 1024:
            schema.fail(where, "historical tooling preimage exceeds byte limit")
    except (FileNotFoundError, PermissionError):
        return None
    data = evidence.historical_bytes(target)
    if data is None:
        return None
    if len(data) > 2 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != file["sha256"]:
        schema.fail(where, "historical tooling preimage byte identity changed")
    if "bytes" in file and (type(file["bytes"]) is not int or len(data) != file["bytes"]):
        schema.fail(where, "historical tooling preimage byte count changed")
    result.setdefault("archived_tool_sources", []).append({
        **identity, "kind": kind, "path": file["path"], "sha256": file["sha256"],
        "snapshot": {"path": relative, "bytes": len(data), "sha256": file["sha256"]},
        "archive_origin": "exact_qualified_source_preimage",
        "original_processing_or_runtime_origin_requalified": False,
    })
    return data


def archived_tool_source(root, run, producer, file, kind, where, result, *, run_id, trace_id):
    """Resolve one original Python tooling artifact through its accepted config."""
    identity = run.get("identity", {})
    roles = {"scripts/parity_fixtures.py": "adapter019", "scripts/parity_execution.py": "execution_helper"}
    if kind != "producer_artifact" or file["path"] not in roles or not isinstance(identity, dict) or "configuration" not in identity:
        return None
    workspace = Path(root).resolve()

    def original_bytes(pin):
        file_identity(pin, where)
        schema.number(pin.get("bytes"), where, integer=True)
        if not 0 <= pin["bytes"] <= 2 * 1024 * 1024:
            schema.fail(where, "original tooling byte count exceeds its bound")
        member = workspace / pin["path"]
        for node in (member, *member.parents):
            if not node.is_relative_to(workspace):
                schema.fail(where, "original tooling path escapes workspace")
            if node.is_symlink() or getattr(node, "is_junction", lambda: False)():
                schema.fail(where, "original tooling path contains an alias")
            if node == workspace:
                break
        member = member.resolve()
        if not member.is_relative_to(workspace) or not member.is_file():
            schema.fail(where, "pinned original tooling bytes are unavailable")
        data = member.read_bytes()
        if len(data) != pin["bytes"] or hashlib.sha256(data).hexdigest() != pin["sha256"]:
            schema.fail(where, "pinned original tooling byte identity changed")
        return data

    configuration_pin = identity["configuration"]
    configuration = schema.obj(strict_json(original_bytes(configuration_pin), where), where)
    # Newer accepted surface configs explicitly pair a logical tooling pin with
    # its declared tool-source member. This is Python tooling, never a compiled
    # artifact/source-freeze guess. The older prerequisite chain below remains.
    if {"current_tooling_pins", "tool_sources"} & configuration.keys():
        if not {"current_tooling_pins", "tool_sources"} <= configuration.keys():
            schema.fail(where, "declared tooling archive pair is incomplete")
        current = schema.obj(configuration["current_tooling_pins"], where)
        if file["path"] not in current:
            return None
        logical_pin = schema.obj(current[file["path"]], where)
        file_identity(logical_pin, where)
        if not same(logical_pin, file):
            schema.fail(where, "declared tooling logical identity differs from producer artifact")
        helper_pin = schema.obj(configuration.get("helper"), where)
        if not helper_pin.get("path", "").endswith(".py") or not any(same(helper_pin, pin) for pin in producer["sources"]):
            schema.fail(where, "original configuration helper differs from accepted producer source")
        original_bytes(helper_pin)
        declared = schema.array(configuration["tool_sources"], where)
        names = set()
        for pin in declared:
            file_identity(pin, where)
            if pin["path"] in names:
                schema.fail(where, "duplicate declared tooling archive path")
            names.add(pin["path"])
        expected_path = (Path(helper_pin["path"]).parent / "tool-source" / file["path"]).as_posix()
        matched = [pin for pin in declared if pin["path"] == expected_path]
        if len(matched) != 1:
            schema.fail(where, "exact declared tooling archive member is missing")
        tool_pin = matched[0]
        if not same({**tool_pin, "path": file["path"]}, file):
            schema.fail(where, "declared tooling archive SHA or byte identity differs")
        if not any(same(tool_pin, pin) for pin in producer["sources"]):
            schema.fail(where, "declared tooling archive is not an accepted producer source")
        data = original_bytes(tool_pin)
        result.setdefault("archived_tool_sources", []).append({
            "run_id": run_id, "trace_id": trace_id, "kind": "archived_tool_source", "producer_kind": kind,
            "path": file["path"], "snapshot": deepcopy(tool_pin), "configuration": deepcopy(configuration_pin),
            "helper": deepcopy(helper_pin), "archive_origin": "declared_configuration_tool_sources",
        })
        return data
    prerequisites = configuration.get("prerequisite_pins")
    role = roles[file["path"]]
    if not isinstance(prerequisites, dict) or role not in prerequisites:
        return None
    if not same(prerequisites[role], file):
        schema.fail(where, "original tooling prerequisite differs from declared producer artifact")
    helper_pin = schema.obj(configuration.get("helper"), where)
    if not helper_pin.get("path", "").endswith(".py") or not any(same(helper_pin, pin) for pin in producer["sources"]):
        schema.fail(where, "original configuration helper differs from accepted producer source")
    original_bytes(helper_pin)
    if role == "execution_helper":
        original_sha = "506cd9d2437dc9a5659941454fcd3642e8c55484c96b2b37eda9d1ec6660912a"
        original_helper = {
            "path": "build/plan-implementation/p05f-axis-plane-export-001/source-preparation-029/formal_axis_plane_surfaces.py",
            "sha256": "9e82790e4b42558ab5fb01bd37effec3dcf6ac2d2b901bf21c1efb36119a77e1", "bytes": 31594,
        }
        if not same(file, {"path": "scripts/parity_execution.py", "sha256": original_sha, "bytes": 32419}) or not same(helper_pin, original_helper):
            schema.fail(where, "relocated tooling is not the original declared execution helper")
        tool_pin = {
            "path": "build/plan-implementation/p05f-axis-plane-export-001/range-formal-preparation-001/tooling-repair-001/content-archive/" + original_sha + "/parity_execution.py",
            "sha256": original_sha, "bytes": 32419,
        }
        data = original_bytes(tool_pin)
        result.setdefault("archived_tool_sources", []).append({
            "run_id": run_id, "trace_id": trace_id, "kind": "archived_tool_source", "producer_kind": kind,
            "path": file["path"], "snapshot": tool_pin, "configuration": deepcopy(configuration_pin),
            "helper": deepcopy(helper_pin), "archive_origin": "content_relocated_after_accepted_execution",
            "original_processing_or_runtime_origin_requalified": False,
        })
        return data
    if "fixture_helper" not in prerequisites:
        schema.fail(where, "original fixture helper mapping is missing")
    fixture_helper = schema.obj(prerequisites["fixture_helper"], where)
    if not fixture_helper.get("path", "").endswith(".py") or not any(same(fixture_helper, pin) for pin in producer["sources"] + producer["artifacts"]):
        schema.fail(where, "original fixture helper is not a declared tooling member")
    try:
        tree = ast.parse(original_bytes(fixture_helper).decode("utf-8"))
        values = [node.value for node in tree.body if isinstance(node, ast.Assign)
                  and any(isinstance(name, ast.Name) and name.id == "PINS" for name in node.targets)]
        if len(values) != 1:
            schema.fail(where, "original fixture helper requires one literal PINS mapping")
        pins = schema.obj(ast.literal_eval(values[0]), where)
    except (SyntaxError, UnicodeError, ValueError, TypeError) as error:
        schema.fail(where, f"original fixture helper PINS is not a literal: {error}")
    tool_pin = schema.obj(pins.get("adapter"), where)
    archive_path = "build/plan-implementation/p05f-axis-plane-export-001/source-preparation-019/scripts/parity_fixtures.py"
    if set(tool_pin) != {"path", "sha256", "bytes"} or tool_pin.get("path") != archive_path:
        schema.fail(where, "original tooling adapter is not the listed sealed source member")
    if tool_pin.get("sha256") != file["sha256"] or "bytes" in file and tool_pin.get("bytes") != file["bytes"]:
        schema.fail(where, "original tooling mapping differs from declared artifact identity")
    data = original_bytes(tool_pin)
    result.setdefault("archived_tool_sources", []).append({
        "run_id": run_id, "trace_id": trace_id, "kind": "archived_tool_source", "producer_kind": kind,
        "path": file["path"], "snapshot": deepcopy(tool_pin), "configuration": deepcopy(configuration_pin),
        "helper": deepcopy(helper_pin), "fixture_helper": deepcopy(fixture_helper),
    })
    return data


def input_events(value, where):
    events = schema.array(value, where)
    if not events:
        schema.fail(where, "UI execution requires concrete input events")
    for event in events:
        schema.obj(event, where)
        kind = event.get("type")
        if kind == "pointer":
            if set(event) != {"type", "position", "button", "down"}:
                schema.fail(
                    where, "pointer input requires concrete position/button/down"
                )
            position = schema.array(event["position"], where)
            if len(position) != 2:
                schema.fail(where, "pointer input requires two coordinates")
            for number in position:
                schema.number(number, where)
            schema.choice(event["button"], {"left", "middle", "right"}, where)
            if type(event["down"]) is not bool:
                schema.fail(where, "pointer down requires bool")
        elif kind == "key":
            if set(event) != {"type", "key", "down"}:
                schema.fail(where, "key input requires concrete key/down")
            key = schema.text(event["key"], where)
            if key not in NAMED_KEYS and not re.fullmatch(r"[A-Za-z0-9]", key):
                schema.fail(where, "unsupported concrete input key")
            if type(event["down"]) is not bool:
                schema.fail(where, "key down requires bool")
        elif kind == "text":
            if set(event) != {"type", "text"}:
                schema.fail(where, "text input requires concrete text")
            schema.text(event["text"], where)
        else:
            schema.fail(
                where, "unsupported input event; action/render markers are insufficient"
            )


def validate_execution(root, documents, inventory=None, require_surfaces=False):
    """Return explicit per-context gaps; never count missing bytes as verified."""
    evidence.validate_evidence(root, documents, inventory)
    commands = documents["commands.json"]
    requirements = {r["id"]: r for r in commands.get("requirements", [])}
    result = {
        "requirements": len(requirements),
        "required_contexts": sum(
            len(required_contexts(r)) for r in requirements.values()
        ),
        "verified_contexts": 0,
        "missing_proofs": [],
        "unavailable_evidence": [],
    }
    if EXTENSION & commands.keys():
        if not EXTENSION <= commands.keys():
            schema.fail("commands.json", "incomplete surface evidence extension")
        schema.version(commands["surface_evidence_schema_version"], "surface evidence")
    if not requirements and not EXTENSION & commands.keys() and inventory is None:
        return result
    inventory = evidence.inventory_data(root, inventory)
    _, runs, _ = evidence.relationship_graph(documents, inventory)
    contracts = validate_contracts(commands, requirements)
    bindings = {row["run_id"]: row for row in commands.get("run_bindings", [])}
    reports = {
        (run_id, row["role"]): row
        for run_id, binding in bindings.items()
        for row in binding["reports"]
    }
    extracted = {}

    def actual_case(run_id, role, identity, where):
        report = reports.get((run_id, role))
        if report is None:
            schema.fail(where, "trace/proof requires a bound accepted report role")
        case_identity(identity, report["format"], where)
        key = run_id, role
        if key not in extracted:
            data = evidence.historical_evidence_bytes(root, report["path"], where)
            extracted[key] = evidence.extract_report_cases(data, report["format"], where) if data is not None else None
        parsed = extracted[key]
        if parsed is not None and not any(
            same(identity, row["identity"]) and row["outcome"] == "passed"
            for row in parsed["cases"]
        ):
            schema.fail(where, "exact passing report-case identity is missing")
        return report, parsed is not None

    catalogues = {}
    producer_ids = {}
    trace_files = {}
    for run_id, run in runs.items():
        extension = {"surface_traces_schema_version", "surface_traces"}
        if not extension & run.keys():
            continue
        if not extension <= run.keys():
            schema.fail(run_id, "incomplete accepted surface trace catalogue")
        schema.version(run["surface_traces_schema_version"], run_id)
        if run_id not in bindings:
            schema.fail(
                run_id,
                "surface trace catalogue requires immutable accepted run binding",
            )
        path_cases = set()
        for row in schema.rows(run["surface_traces"], run_id + " surface traces"):
            where = run_id + " " + row["id"]
            file_identity(row, where)
            schema.identity(row.get("trace_case_id"), where)
            producer_identity(row.get("producer"), where)
            producer = row["producer"]
            previous = producer_ids.get((run_id, producer["id"]))
            if previous is not None and not same(previous, producer):
                schema.fail(where, "inconsistent tested producer identity")
            producer_ids[run_id, producer["id"]] = producer
            file_key = run_id, row["path"]
            file_value = row["sha256"], producer["id"]
            if file_key in trace_files and trace_files[file_key] != file_value:
                schema.fail(
                    where, "inconsistent trace file/producer catalogue identity"
                )
            trace_files[file_key] = file_value
            role = schema.text(row.get("report_role"), where)
            actual_case(run_id, role, row.get("report_case_identity"), where)
            path_case = row["path"], row["trace_case_id"]
            if path_case in path_cases:
                schema.fail(where, "duplicate trace path/subcase identity")
            path_cases.add(path_case)
            catalogues[run_id, row["id"]] = row

    proofs = {}
    for proof in schema.rows(commands.get("surface_proofs", []), "surface proofs"):
        where = proof["id"]
        child_id = schema.identity(proof.get("requirement_id"), where)
        name = context(proof.get("context"), where)
        child = requirements.get(child_id)
        if child is None or name not in required_contexts(child):
            schema.fail(where, "orphaned or inapplicable surface proof")
        key = child_id, name
        if key in proofs:
            schema.fail(where, "duplicate requirement/context surface proof")
        contract_id = schema.identity(proof.get("contract_id"), where)
        contract = contracts.get(contract_id)
        if contract is None or contract["requirement_id"] != child_id:
            schema.fail(where, "surface proof has wrong or missing frozen contract")
        run_id = schema.identity(proof.get("run_id"), where)
        if run_id not in child["verification_run_ids"] or run_id not in bindings:
            schema.fail(where, "proof run is not a linked immutable accepted run")
        trace_id = schema.identity(proof.get("trace_id"), where)
        trace_case_id = schema.identity(proof.get("trace_case_id"), where)
        producer_id = schema.identity(proof.get("producer_id"), where)
        catalogue = catalogues.get((run_id, trace_id))
        if catalogue is None:
            schema.fail(where, "proof lacks a fresh accepted trace catalogue entry")
        role = schema.text(proof.get("report_role"), where)
        actual_case(run_id, role, proof.get("report_case_identity"), where)
        if (
            trace_case_id != catalogue["trace_case_id"]
            or producer_id != catalogue["producer"]["id"]
            or name != catalogue["producer"]["context"]
            or role != catalogue["report_role"]
            or not same(
                proof["report_case_identity"], catalogue["report_case_identity"]
            )
        ):
            schema.fail(where, "proof/catalogue case/context/producer identity differs")
        proofs[key] = proof

    # Validate every declared catalogue, even one not selected by a proof.
    trace_cache = {}
    source_freezes = {}
    catalogue_available = {}
    runtime_cases = {}
    for (run_id, trace_id), row in catalogues.items():
        where = run_id + " " + trace_id
        unavailable = []

        def archived_source(file, kind):
            if kind not in {"producer_source", "producer_artifact"}:
                return None
            identity = runs[run_id].get("identity", {})
            if not isinstance(identity, dict) or "source_freeze" not in identity:
                return None
            if run_id not in source_freezes:
                freeze_identity = identity["source_freeze"]
                file_identity(freeze_identity, where)
                freeze_path = evidence.report_path(root, freeze_identity["path"], where)
                if not freeze_path.is_file():
                    schema.fail(where, "pinned source freeze bytes are unavailable")
                freeze_bytes = freeze_path.read_bytes()
                if hashlib.sha256(freeze_bytes).hexdigest() != freeze_identity["sha256"]:
                    schema.fail(where, "pinned source freeze byte fingerprint changed")
                if "bytes" in freeze_identity:
                    schema.number(freeze_identity["bytes"], where, integer=True)
                    if freeze_identity["bytes"] != len(freeze_bytes):
                        schema.fail(where, "pinned source freeze byte count changed")
                freeze = schema.obj(strict_json(freeze_bytes, where), where)
                schema.version(freeze.get("schema_version"), where)
                sources = schema.obj(freeze.get("sources"), where)
                for path, expected_sha in sources.items():
                    schema.path(path, where)
                    schema.sha(expected_sha, where)
                schema.path(freeze.get("source_snapshot"), where)
                source_freezes[run_id] = freeze_identity, freeze
            freeze_identity, freeze = source_freezes[run_id]
            expected_sha = freeze["sources"].get(file["path"])
            if expected_sha is None:
                return None
            if expected_sha != file["sha256"]:
                schema.fail(where, "producer/source freeze member fingerprint differs")
            workspace = Path(root).resolve()
            snapshot = workspace / freeze["source_snapshot"]
            member = snapshot / file["path"]
            for node in (member, *member.parents):
                if not node.is_relative_to(workspace):
                    schema.fail(where, "historical source snapshot escapes workspace")
                if node.is_symlink() or getattr(node, "is_junction", lambda: False)():
                    schema.fail(where, "historical source snapshot contains an aliased path")
                if node == workspace:
                    break
            snapshot = snapshot.resolve()
            member = member.resolve()
            if not snapshot.is_relative_to(workspace) or not member.is_relative_to(snapshot):
                schema.fail(where, "historical source snapshot escapes declared directory")
            if not snapshot.is_dir() or not member.is_file():
                schema.fail(where, "pinned historical source snapshot bytes are unavailable")
            data = member.read_bytes()
            if hashlib.sha256(data).hexdigest() != expected_sha:
                schema.fail(where, "historical source snapshot byte fingerprint changed")
            result.setdefault("archived_sources", []).append(
                {
                    "run_id": run_id,
                    "trace_id": trace_id,
                    "kind": kind,
                    "path": file["path"],
                    "snapshot_path": member.relative_to(workspace).as_posix(),
                    "sha256": expected_sha,
                    "source_freeze": deepcopy(freeze_identity),
                }
            )
            return data

        def bytes_for(file, kind):
            data = evidence.historical_evidence_bytes(root, file["path"], where)
            if data is not None and hashlib.sha256(data).hexdigest() == file["sha256"]:
                return data
            archived = archived_source(file, kind)
            if archived is not None:
                return archived
            tooling = archived_tool_source(root, runs[run_id], row["producer"], file, kind, where, result,
                                           run_id=run_id, trace_id=trace_id)
            if tooling is not None:
                return tooling
            stored = stored_evidence_tool_source(root, file, kind, where, result, run_id=run_id, trace_id=trace_id)
            if stored is not None:
                return stored
            if data is None:
                unavailable.append(
                    {
                        "kind": kind,
                        "path": file["path"],
                        "reason": "Declared executed evidence bytes are unavailable",
                    }
                )
                return None
            schema.fail(where, f"{kind} byte fingerprint changed")

        producer = row["producer"]
        for file in producer["sources"]:
            bytes_for(file, "producer_source")
        for file in producer["artifacts"]:
            bytes_for(file, "producer_artifact")
        data = bytes_for(row, "trace")
        _, available = actual_case(
            run_id, row["report_role"], row["report_case_identity"], where
        )
        if not available:
            unavailable.append(
                {
                    "kind": "report",
                    "path": reports[run_id, row["report_role"]]["path"],
                    "reason": "Exact passing report-case bytes are unavailable",
                }
            )
        if data is not None:
            cache_key = row["path"], row["sha256"]
            if cache_key not in trace_cache:
                trace = schema.obj(strict_json(data, where), where)
                schema.version(trace.get("schema_version"), where)
                schema.identity(trace.get("run_id"), where)
                schema.identity(trace.get("producer_id"), where)
                cases = schema.rows(trace.get("cases"), where + " trace cases")
                if not cases:
                    schema.fail(where, "runtime trace requires identifiable subcases")
                trace_cache[cache_key] = trace
            trace = trace_cache[cache_key]
            if trace["run_id"] != run_id or trace["producer_id"] != producer["id"]:
                schema.fail(where, "runtime trace run/producer identity differs")
            for case in trace["cases"]:
                declared = [
                    item
                    for (catalogue_run, _), item in catalogues.items()
                    if catalogue_run == run_id
                    and item["path"] == row["path"]
                    and item["sha256"] == row["sha256"]
                    and item["trace_case_id"] == case["id"]
                ]
                if len(declared) != 1:
                    schema.fail(where, "orphaned runtime trace subcase")
                entry = declared[0]
                child_id = schema.identity(case.get("requirement_id"), where)
                name = context(case.get("context"), where)
                child = requirements.get(child_id)
                if (
                    child is None
                    or name not in required_contexts(child)
                    or name != entry["producer"]["context"]
                ):
                    schema.fail(where, "runtime trace requirement/context differs")
                contract_id = schema.identity(case.get("contract_id"), where)
                contract = contracts.get(contract_id)
                schema.sha(case.get("contract_sha256"), where)
                if (
                    contract is None
                    or contract["requirement_id"] != child_id
                    or case["contract_sha256"] != contract["sha256"]
                ):
                    schema.fail(where, "runtime trace frozen contract differs")
                if run_id not in child["verification_run_ids"]:
                    schema.fail(where, "runtime trace run is not linked to requirement")
                role = schema.text(case.get("report_role"), where)
                actual_case(run_id, role, case.get("report_case_identity"), where)
                if role != entry["report_role"] or not same(
                    case["report_case_identity"], entry["report_case_identity"]
                ):
                    schema.fail(where, "runtime trace exact report case differs")
                assertions = schema.rows(case.get("assertions"), where + " assertions")
                frozen = {a["id"]: a for a in contract["contexts"][name]}
                if {a["id"] for a in assertions} != frozen.keys():
                    schema.fail(
                        where, "runtime assertions omit or add frozen assertion IDs"
                    )
                for assertion in assertions:
                    assertion_rule(assertion, where, observed=True)
                    expected = frozen[assertion["id"]]
                    if any(
                        (key in assertion) != (key in expected)
                        or key in expected
                        and not same(assertion[key], expected[key])
                        for key in (
                            "rule",
                            "value_type",
                            "expected",
                            "observation",
                            "ui_state",
                        )
                    ):
                        schema.fail(
                            where,
                            "runtime assertion differs from frozen rule/expected value",
                        )
                if name.startswith("imgui_"):
                    input_events(case.get("inputs"), where)
                elif "inputs" in case:
                    schema.array(case["inputs"], where)
                runtime_cases[run_id, entry["id"]] = case
            if not any(case["id"] == row["trace_case_id"] for case in trace["cases"]):
                schema.fail(where, "catalogued runtime trace subcase is missing")
        catalogue_available[run_id, trace_id] = unavailable

    for child_id, child in requirements.items():
        for name in sorted(required_contexts(child)):
            proof = proofs.get((child_id, name))
            if proof is None:
                result["missing_proofs"].append(
                    {
                        "requirement_id": child_id,
                        "context": name,
                        "reason": "Frozen contract and executed surface proof are missing",
                    }
                )
                if child["verification_status"] == "verified":
                    schema.fail(
                        child_id,
                        "verified child lacks structural executed surface proof",
                    )
                continue
            unavailable = catalogue_available[proof["run_id"], proof["trace_id"]]
            runtime_case = runtime_cases.get((proof["run_id"], proof["trace_id"]))
            if runtime_case is not None and any(
                not same(runtime_case[key], proof[key])
                for key in (
                    "requirement_id",
                    "context",
                    "contract_id",
                    "report_role",
                    "report_case_identity",
                )
            ):
                schema.fail(
                    proof["id"],
                    "runtime subcase differs from selected requirement/context/contract proof",
                )
            for item in unavailable:
                result["unavailable_evidence"].append(
                    {"requirement_id": child_id, "context": name, **item}
                )
            if not unavailable:
                result["verified_contexts"] += 1
    selected_catalogues = {
        (proof["run_id"], proof["trace_id"]) for proof in proofs.values()
    }
    for (run_id, trace_id), unavailable in catalogue_available.items():
        if (run_id, trace_id) in selected_catalogues:
            continue
        row = catalogues[run_id, trace_id]
        case = runtime_cases.get((run_id, trace_id))
        for item in unavailable:
            result["unavailable_evidence"].append(
                {
                    "requirement_id": case["requirement_id"]
                    if case is not None
                    else None,
                    "context": row["producer"]["context"],
                    "run_id": run_id,
                    "trace_id": trace_id,
                    **item,
                }
            )
    if require_surfaces and (
        result["missing_proofs"] or result["unavailable_evidence"]
    ):
        schema.fail(
            "executed surfaces",
            f"evidence required: {len(result['missing_proofs'])} missing proofs, {len(result['unavailable_evidence'])} unavailable evidence files",
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--require-surfaces", action="store_true")
    args = parser.parse_args()
    try:
        documents = {
            name: json.loads(
                (args.root / "docs/parity" / name).read_text(encoding="utf-8")
            )
            for name in (*schema.REFERENCE_FILES, *schema.COLLECTIONS)
            if (args.root / "docs/parity" / name).is_file()
        }
        result = validate_execution(
            args.root, documents, require_surfaces=args.require_surfaces
        )
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Executed surface check failed: {error}\n")
    print(
        f"Executed surfaces: {result['verified_contexts']}/{result['required_contexts']} verified; {len(result['missing_proofs'])} missing proofs; {len(result['unavailable_evidence'])} unavailable evidence files"
    )
    for gap in result["missing_proofs"] + result["unavailable_evidence"]:
        print(
            f"Unresolved surface: {gap['requirement_id']} {gap['context']}: {gap['reason']}"
            + (f" ({gap['path']})" if "path" in gap else "")
        )
    for archived in result.get("archived_sources", []):
        print(
            f"Archived source: {archived['run_id']} {archived['trace_id']} "
            f"{archived['kind']}: {archived['path']} -> {archived['snapshot_path']}"
        )
    for archived in result.get("archived_tool_sources", []):
        print(
            f"Archived tool source: {archived['run_id']} {archived['trace_id']} "
            f"{archived['producer_kind']}: {archived['path']} -> {archived['snapshot']['path']}"
        )


if __name__ == "__main__":
    main()
