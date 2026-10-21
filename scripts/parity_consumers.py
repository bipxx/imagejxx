"""Validate independently assigned setting effects through frozen B2 surfaces.

This additive validator certifies consistency of reviewed contracts and owned
runtime observations, not commercial, reference, platform or whole-family parity.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "imagejxx_consumer_execution", Path(__file__).with_name("parity_execution.py")
)
execution = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(execution)
evidence = execution.evidence
schema = execution.schema
EXTENSION = {
    "settings_consumers_schema_version",
    "settings_definition_bindings",
    "settings_consumer_assignments",
    "settings_consumer_contracts",
    "settings_consumer_proofs",
}
SCENARIOS = {"default", "changed", "frozen", "override", "persisted"}
MODES = {"detached_options", "live_state", "configuration_file"}
KINDS = {"bool", "int", "float", "choice", "color", "columns", "text"}


def contract_fingerprint(contract):
    return hashlib.sha256(
        execution.canonical(
            {
                key: contract[key]
                for key in (
                    "id", "assignment_id", "assignment", "definition_binding",
                    "definition", "contexts",
                )
            }
        )
    ).hexdigest()


def typed_value(definition, value, where):
    """Mirror native setting kinds; bounds do not imply native-runtime coverage."""
    kind = definition["kind"]
    if kind == "bool" and type(value) is not bool:
        schema.fail(where, "bool setting requires bool")
    if kind == "int":
        schema.number(value, where, integer=True)
    if kind == "float":
        if type(value) is not float:
            schema.fail(where, "float setting requires a typed float")
        schema.number(value, where)
    if kind in {"int", "float"}:
        if not definition["minimum"] <= value <= definition["maximum"]:
            schema.fail(where, "setting value is outside native bounds")
    if kind in {"choice", "color"}:
        schema.text(value, where, empty=True)
        if kind == "choice" and value not in definition["choices"]:
            schema.fail(where, "setting value is outside native choices")
        if kind == "color" and not re.fullmatch(r"#[0-9a-f]{6}([0-9a-f]{2})?", value):
            schema.fail(where, "color requires normalized native hexadecimal text")
    if kind == "columns":
        schema.texts(value, where)
        if len(value) > 1024 or any(len(item) > 256 for item in value):
            schema.fail(where, "columns exceed native limits")
    if kind == "text":
        schema.text(value, where, empty=True)
        try:
            encoded = value.encode("utf-8", errors="strict")
        except UnicodeEncodeError:
            schema.fail(where, "text setting requires strict UTF-8")
        if "\0" in value or len(encoded) > 4096:
            schema.fail(where, "text exceeds native UTF-8/NUL limits")
    if definition["name"] == "wand_connectivity" and value not in {4, 8}:
        schema.fail(where, "wand connectivity requires four or eight")
    if definition["name"] == "profile_samples" and value == 1:
        schema.fail(where, "profile samples excludes one")


def parse_export(value, where):
    schema.obj(value, where)
    execution.finite_json(value, where)
    schema.version(value.get("schema_version"), where)
    definitions = {}
    for row in schema.array(value.get("definitions"), where + " definitions"):
        schema.obj(row, where)
        name = schema.identity(row.get("name"), where)
        if name in definitions:
            schema.fail(where, "duplicate definition name")
        for field in ("group", "label", "description"):
            schema.text(row.get(field), where)
        kind = row.get("kind")
        schema.choice(kind, KINDS, where)
        for field in ("default", "minimum", "maximum", "choices"):
            if field not in row:
                schema.fail(where, "missing native definition field")
        schema.texts(row["choices"], where)
        if kind in {"int", "float"}:
            schema.number(row["minimum"], where)
            schema.number(row["maximum"], where)
            if row["minimum"] > row["maximum"]:
                schema.fail(where, "native definition bounds are reversed")
        elif row["minimum"] is not None or row["maximum"] is not None:
            schema.fail(where, "inapplicable native bounds must be null")
        if kind == "choice" and not row["choices"]:
            schema.fail(where, "native choice definition has no choices")
        if kind not in {"choice", "columns"} and row["choices"]:
            schema.fail(where, "native definition has inapplicable choices")
        typed_value(row, row["default"], where)
        definitions[name] = row
    defaults = schema.obj(value.get("defaults"), where + " defaults")
    if not definitions or set(defaults) != definitions.keys():
        schema.fail(where, "definition/default key sets differ or are empty")
    for name, row in definitions.items():
        typed_value(row, defaults[name], where)
        if not execution.same(row["default"], defaults[name]):
            schema.fail(where, "native definition/default typed values differ")
    return definitions


def export_record(run, where):
    record = schema.obj(run.get("settings_export"), where + " settings_export")
    identities = schema.obj(record.get("source_library_executable_output_sha256"), where)
    if not identities:
        schema.fail(where, "accepted settings export has no identity set")
    for path, digest in identities.items():
        schema.path(path, where)
        schema.sha(digest, where)
    output = record.get("output")
    schema.path(output, where)
    if output not in identities:
        schema.fail(where, "accepted output is absent from complete identity set")
    # These roles follow the native exporter identity conventions, rather than
    # caller-assigned labels. All remaining identities are also mandatory.
    names = [Path(path).name for path in identities]
    if "settings.cpp" not in names or "settings.hpp" not in names:
        schema.fail(where, "accepted export omits native definition source/header")
    if not any(path.endswith((".lib", ".a", ".so", ".dylib")) for path in identities):
        schema.fail(where, "accepted export omits native library provenance")
    if not any(
        Path(path).name in {"imagejxx_settings_schema_example", "imagejxx_settings_schema_example.exe"}
        for path in identities
    ):
        schema.fail(where, "accepted export omits native exporter executable provenance")
    for field in ("definitions_compared", "defaults_compared"):
        schema.number(record.get(field), where, integer=True)
        if record[field] <= 0:
            schema.fail(where, "accepted native definition/default counts must be positive")
    return record, identities


def provenance_roles(record, identities, where):
    """Fresh accepted exports freeze roles; historical maps alone do not qualify."""
    value = schema.obj(record.get("identity_roles"), where)
    if set(value) != {"definition_sources", "producer_sources", "libraries", "executables", "output"}:
        schema.fail(where, "accepted exporter requires complete provenance role categories")
    paths = []
    for role in ("definition_sources", "producer_sources", "libraries", "executables"):
        items = schema.texts(value[role], where, nonempty=True)
        for path in items:
            schema.path(path, where)
        paths.extend(items)
    schema.path(value["output"], where)
    paths.append(value["output"])
    if len(set(paths)) != len(paths) or set(paths) != identities.keys() or value["output"] != record["output"]:
        schema.fail(where, "accepted provenance roles omit or duplicate complete identity set")
    if not {"settings.cpp", "settings.hpp"} <= {Path(path).name for path in value["definition_sources"]}:
        schema.fail(where, "accepted definition source roles omit native source/header")
    if any(not path.endswith((".cpp", ".hpp", ".h")) for path in value["definition_sources"]):
        schema.fail(where, "definition role is not native definition source")
    if any(not path.endswith((".lib", ".a", ".so", ".dylib")) for path in value["libraries"]):
        schema.fail(where, "library role is not a native library artifact")
    if any(Path(path).name not in {"imagejxx_settings_schema_example", "imagejxx_settings_schema_example.exe"} for path in value["executables"]):
        schema.fail(where, "executable role is not native exporter artifact")
    return value


def ownership_snapshot(requirements):
    return [
        {
            "requirement_id": child["id"],
            "settings_consumers": child["settings_consumers"],
            "interfaces": execution.frozen_snapshot(child)["interfaces"],
        }
        for child in requirements.values()
        if child.get("settings_consumers")
    ]


def parameter_value(assignment, requirement, definition, value, where):
    """Validate direct fields or an independently frozen converted API schema."""
    parameter = assignment.get("parameter_schema")
    if parameter is None:
        parameter = next(
            (row for row in requirement["contract"]["parameters"] if row["name"] == assignment["consumer"]["member"]),
            None,
        )
    if parameter is None:
        typed_value(definition, value, where)
    else:
        schema.parameter(dict(parameter, default=value), where)


def effective_value(assignment, definition, value, where):
    """Derive consumed API values from an independently frozen conversion."""
    relation = assignment.get("parameter_conversion")
    if relation is None:
        return value
    rule = relation["rule"]
    if rule == "identity":
        return value
    if rule == "scale_offset":
        schema.number(value, where)
        result = value * relation["scale"] + relation["offset"]
        schema.number(result, where)
        return result
    for row in relation["entries"]:
        if execution.same(row["setting_value"], value):
            return row["effective_parameter_value"]
    schema.fail(where, "converted setting has no deterministic lookup entry")


def conversion_relation(assignment, requirement, definition, where):
    relation = schema.obj(assignment.get("parameter_conversion"), where)
    rule = relation.get("rule")
    schema.choice(rule, {"identity", "scale_offset", "lookup"}, where)
    fields = {"rule", "setting_key", "member"}
    if rule == "scale_offset":
        fields |= {"scale", "offset"}
        schema.number(relation.get("scale"), where)
        schema.number(relation.get("offset"), where)
        if definition["kind"] not in {"int", "float"}:
            schema.fail(where, "numeric conversion requires a native numeric setting")
    if rule == "lookup":
        fields.add("entries")
    if set(relation) != fields or relation["setting_key"] != assignment["setting_key"] or relation["member"] != assignment["consumer"]["member"]:
        schema.fail(where, "parameter conversion differs from consumed key/member or shape")
    if rule == "lookup":
        rows = schema.array(relation["entries"], where)
        if not rows:
            schema.fail(where, "parameter lookup requires entries")
        inputs = set()
        for row in rows:
            schema.obj(row, where)
            if set(row) != {"setting_value", "effective_parameter_value"}:
                schema.fail(where, "parameter lookup requires exact typed input/output fields")
            typed_value(definition, row["setting_value"], where)
            parameter_value(assignment, requirement, definition, row["effective_parameter_value"], where)
            key = execution.canonical(row["setting_value"])
            if key in inputs:
                schema.fail(where, "duplicate typed lookup input")
            inputs.add(key)
        choices = definition["choices"] if definition["kind"] == "choice" else ([False, True] if definition["kind"] == "bool" else None)
        if choices is not None and inputs != {execution.canonical(value) for value in choices}:
            schema.fail(where, "parameter lookup omits native choice/boolean values")
    default = effective_value(assignment, definition, definition["default"], where)
    parameter_value(assignment, requirement, definition, default, where)
    if not execution.same(default, assignment["parameter_schema"]["default"]):
        schema.fail(where, "converted native default differs from frozen parameter default")


def validate_consumers(root, documents, inventory=None, require_consumers=False):
    """Return explicit required-owner gaps; absent bytes never verify effects."""
    surface_result = execution.validate_execution(root, documents, inventory)
    commands = documents["commands.json"]
    requirements = {row["id"]: row for row in commands.get("requirements", [])}
    result = {
        "exposed_keys": [], "claimed_consumers": 0, "required_consumers": 0,
        "required_contexts": 0, "verified_contexts": 0,
        "unresolved_enumeration": [], "missing_contracts": [], "missing_proofs": [],
        "unavailable_evidence": [],
    }
    present = EXTENSION & commands.keys()
    if present and not EXTENSION <= commands.keys():
        schema.fail("commands.json", "incomplete settings consumer extension")
    if not requirements and not present and inventory is None:
        result["unresolved_enumeration"].append({"reason": "Independent native definition and required-consumer enumeration are unknown"})
        if require_consumers:
            schema.fail("settings consumers", "evidence required: independent native definition and consumer enumeration are unknown")
        return result
    inventory = evidence.inventory_data(root, inventory)
    _, runs, _ = evidence.relationship_graph(documents, inventory)
    run_bindings = {row["run_id"]: row for row in commands.get("run_bindings", [])}

    def bytes_for(file, where, kind):
        execution.file_identity(file, where)
        data = evidence.historical_evidence_bytes(root, file["path"], where)
        if data is None:
            result["unavailable_evidence"].append(
                {"id": where, "kind": kind, "path": file["path"], "reason": "Declared consumer evidence bytes are unavailable"}
            )
            return None
        if hashlib.sha256(data).hexdigest() != file["sha256"]:
            schema.fail(where, f"{kind} byte fingerprint changed")
        return data

    # Exposed native definitions are independent of consumer contracts. Inspect
    # all accepted exports, including a newer export not chosen by a binding.
    exposed = {}
    for run_id, run in runs.items():
        if "settings_export" not in run:
            continue
        record, identities = export_record(run, run_id)
        payload = bytes_for(
            {"path": record["output"], "sha256": identities[record["output"]]},
            run_id, "definition_export",
        )
        if payload is None:
            result["unresolved_enumeration"].append({"run_id": run_id, "reason": "Accepted definition export is unavailable"})
            continue
        value = execution.strict_json(payload, run_id)
        definitions = parse_export(value, run_id)
        if len(definitions) != record["definitions_compared"] or len(value["defaults"]) != record["defaults_compared"]:
            schema.fail(run_id, "accepted export definition/default counts differ")
        exposed.update(definitions)
    result["exposed_keys"] = sorted(exposed)
    if not exposed:
        result["unresolved_enumeration"].append({"reason": "Native definition enumeration is unavailable or empty"})
    if not present:
        result["unresolved_enumeration"].append({"reason": "Independent required-consumer enumeration is missing"})
        for child in requirements.values():
            for owner in child.get("settings_consumers", []):
                result["required_consumers"] += 1
                result["required_contexts"] += len(execution.required_contexts(child))
                result["missing_contracts"].append({"requirement_id": child["id"], "setting_key": owner["key"], "reason": "Consumer contract and independent owner assignment are missing"})
        owned_keys = {owner["key"] for child in requirements.values() for owner in child.get("settings_consumers", [])}
        for key in sorted(exposed.keys() - owned_keys):
            result["unresolved_enumeration"].append({"setting_key": key, "reason": "Exposed native definition has no current reviewed consumer owner"})
        if require_consumers and any(result[key] for key in ("unresolved_enumeration", "missing_contracts", "unavailable_evidence")):
            schema.fail("settings consumers", "evidence required: independent enumeration and consumer effects are incomplete")
        return result
    schema.version(commands["settings_consumers_schema_version"], "settings consumers")

    bindings = {}
    binding_available = {}
    definitions_by_binding = {}
    for row in schema.rows(commands["settings_definition_bindings"], "definition bindings"):
        where = row["id"]
        run_id = schema.identity(row.get("run_id"), where)
        run = runs.get(run_id)
        if run is None or run_id not in run_bindings:
            schema.fail(where, "definition binding requires an immutable accepted run")
        record, identities = export_record(run, where)
        if not execution.same(row.get("identities"), identities):
            schema.fail(where, "definition binding omits or changes complete accepted identity set")
        file = row.get("export")
        if not execution.same(file, {"path": record["output"], "sha256": identities[record["output"]]}):
            schema.fail(where, "definition binding differs from accepted export identity")
        role = schema.text(row.get("report_role"), where)
        available = True
        accepted_fields = {"report_role", "report_case_identity", "identity_roles"}
        if accepted_fields & record.keys() and not accepted_fields <= record.keys():
            schema.fail(where, "incomplete accepted exporter case/provenance declaration")
        if not accepted_fields <= record.keys():
            available = False
            result["unavailable_evidence"].append({"id": where, "kind": "exporter_declaration", "path": record["output"], "reason": "Historical export lacks independently accepted exact report case and categorized provenance"})
        else:
            roles = provenance_roles(record, identities, where)
            if not execution.same(row.get("identity_roles"), roles) or role != record["report_role"] or not execution.same(row.get("report_case_identity"), record["report_case_identity"]):
                schema.fail(where, "definition binding differs from exact accepted exporter case/roles")
        report = next((r for r in run_bindings[run_id]["reports"] if r["role"] == role), None)
        if report is None:
            schema.fail(where, "definition binding lacks bound accepted report role")
        execution.case_identity(row.get("report_case_identity"), report["format"], where)
        report_payload = bytes_for(report, where, "definition_report")
        if report_payload is None:
            available = False
        elif not any(execution.same(row["report_case_identity"], case["identity"]) for case in evidence.extract_report_cases(report_payload, report["format"], where)["cases"]):
            schema.fail(where, "definition binding exact passing report case is missing")
        for path, digest in identities.items():
            if bytes_for({"path": path, "sha256": digest}, where, "definition_provenance") is None:
                available = False
        frozen = row.get("frozen_export")
        definitions = parse_export(frozen, where)
        if len(definitions) != record["definitions_compared"] or len(frozen["defaults"]) != record["defaults_compared"]:
            schema.fail(where, "frozen export truncates accepted native definitions/defaults")
        payload = bytes_for(file, where, "definition_export")
        if payload is None:
            available = False
        elif not execution.same(execution.strict_json(payload, where), frozen):
            schema.fail(where, "frozen export differs from actual accepted bytes")
        for name, definition in definitions.items():
            if name in exposed and not execution.same(definition, exposed[name]):
                result["unresolved_enumeration"].append({"setting_key": name, "binding_id": where, "reason": "Binding does not cover the latest exposed native definition"})
        bindings[where] = row
        binding_available[where] = available
        definitions_by_binding[where] = definitions

    scope = schema.obj(commands["settings_consumer_assignments"], "consumer assignments")
    snapshot_run = schema.identity(scope.get("run_id"), "consumer assignments")
    run = runs.get(snapshot_run)
    if run is None or snapshot_run not in run_bindings:
        schema.fail("consumer assignments", "independent snapshot requires immutable accepted run")
    identity = scope.get("snapshot")
    if not execution.same(identity, run.get("settings_consumer_assignment_snapshot")):
        schema.fail("consumer assignments", "snapshot differs from independent accepted identity")
    payload = bytes_for(identity, "consumer assignments", "consumer_snapshot")
    entries = schema.rows(scope.get("entries"), "consumer assignments")
    sources = None
    scope_available = payload is not None
    if payload is None:
        result["unresolved_enumeration"].append({"reason": "Independent consumer snapshot is unavailable"})
    else:
        snapshot = schema.obj(execution.strict_json(payload, "consumer snapshot"), "consumer snapshot")
        schema.version(snapshot.get("schema_version"), "consumer snapshot")
        schema.choice(snapshot.get("enumeration_status"), {"partial", "complete"}, "consumer snapshot")
        if snapshot["enumeration_status"] != "complete":
            result["unresolved_enumeration"].append({"reason": "Independent consumer enumeration remains partial"})
        names = schema.texts(snapshot.get("exposed_definition_names"), "consumer snapshot", nonempty=True)
        if set(names) != exposed.keys():
            result["unresolved_enumeration"].append({"reason": "Independent enumeration does not cover all exposed native definitions", "missing_keys": sorted(exposed.keys() - set(names)), "unknown_keys": sorted(set(names) - exposed.keys())})
        owners = schema.array(snapshot.get("ownership"), "consumer ownership")
        if not execution.same(owners, ownership_snapshot(requirements)):
            schema.fail("consumer snapshot", "current child ownership text or frozen interfaces differ")
        if not execution.same(snapshot.get("assignments"), entries):
            schema.fail("consumer assignments", "required assignments differ from independent snapshot")
        sources = {}
        for file in schema.array(snapshot.get("sources"), "consumer snapshot sources"):
            execution.file_identity(file, "consumer snapshot")
            if file["path"] in sources:
                schema.fail("consumer snapshot", "duplicate consumer source path")
            sources[file["path"]] = file["sha256"]
            if bytes_for(file, "consumer snapshot", "consumer_source") is None:
                scope_available = False
        if not sources:
            schema.fail("consumer snapshot", "consumer enumeration requires pinned sources")

    def source_refs(value, where):
        refs = schema.array(value, where + " source refs")
        if not refs:
            schema.fail(where, "consumer requires pinned source references")
        paths = set()
        for file in refs:
            execution.file_identity(file, where)
            if file["path"] in paths:
                schema.fail(where, "duplicate consumer source reference")
            paths.add(file["path"])
            if sources is not None and sources.get(file["path"]) != file["sha256"]:
                schema.fail(where, "consumer source reference differs from independent pin")
            bytes_for(file, where, "consumer_source_reference")

    assignments = {}
    semantic_keys = set()
    coverage = set()
    assigned_names = set()
    for row in entries:
        where = row["id"]
        binding_id = schema.identity(row.get("definition_binding_id"), where)
        name = schema.identity(row.get("setting_key"), where)
        if binding_id not in bindings or name not in definitions_by_binding[binding_id]:
            schema.fail(where, "consumer references unknown native definition binding/key")
        consumer = schema.obj(row.get("consumer"), where)
        if set(consumer) != {"api", "member", "operation"}:
            schema.fail(where, "consumer requires exact API/member/operation")
        for field in consumer:
            schema.text(consumer[field], where)
        owner_ids = schema.texts(row.get("requirement_ids"), where, nonempty=True)
        if len(owner_ids) != 1:
            schema.fail(where, "each consumer assignment must have exactly one owning requirement")
        if any(owner not in requirements for owner in owner_ids):
            schema.fail(where, "consumer has orphaned owning requirement")
        operation_entries = {
            entry.split(":", 1)[1].strip()
            for interface in requirements[owner_ids[0]]["interfaces"].values()
            for entry in interface["entry_points"]
            if entry.startswith("Operation form:")
        }
        if operation_entries and consumer["operation"] not in operation_entries:
            schema.fail(where, "concrete consumer operation differs from frozen owning interface")
        key = name, execution.canonical(consumer), tuple(sorted(owner_ids))
        if key in semantic_keys:
            schema.fail(where, "duplicate semantic consumer")
        semantic_keys.add(key)
        source_refs(row.get("source_refs"), where)
        owning_requirement = requirements[owner_ids[0]]
        member = consumer["member"]
        declared = next((item for item in owning_requirement["contract"]["parameters"] if item["name"] == member), None)
        if member != name and declared is None:
            schema.fail(where, "unknown consuming member is not a frozen owning parameter")
        if ("parameter_schema" in row) != ("parameter_conversion" in row):
            schema.fail(where, "converted parameter requires both schema and deterministic relation")
        if "parameter_schema" in row:
            schema.parameter(row["parameter_schema"], where)
            if row["parameter_schema"]["name"] != consumer["member"]:
                schema.fail(where, "parameter schema name differs from consuming member")
            if declared is not None and not execution.same(row["parameter_schema"], declared):
                schema.fail(where, "parameter schema differs from owning frozen API parameter")
            conversion_relation(row, owning_requirement, definitions_by_binding[binding_id][name], where)
        mode = row.get("consumption_mode")
        schema.choice(mode, MODES, where)
        contexts = schema.obj(row.get("contexts"), where)
        required = set().union(*(execution.required_contexts(requirements[owner]) for owner in owner_ids))
        if set(contexts) != required:
            schema.fail(where, "consumer contexts reduce owning frozen interface applicability")
        for context, value in contexts.items():
            execution.context(context, where)
            scenarios = schema.obj(value.get("scenarios"), where)
            if set(scenarios) != SCENARIOS:
                schema.fail(where, "consumer must retain all five scenario declarations")
            required_scenarios = {"default", "changed"}
            if mode == "detached_options":
                required_scenarios |= {"frozen", "override"}
            if mode == "configuration_file":
                required_scenarios.add("persisted")
            owner_text = " ".join(
                item["consumer"].lower()
                for owner in owner_ids
                for item in requirements[owner].get("settings_consumers", [])
                if item["key"] == name
            )
            if re.search(r"snapshot|frozen|draft|workflow", owner_text):
                required_scenarios |= {"frozen", "override"}
            if re.search(r"preference|persist|reopen|configuration.file", owner_text):
                required_scenarios.add("persisted")
            for scenario, rule in scenarios.items():
                schema.obj(rule, where)
                if type(rule.get("applicable")) is not bool:
                    schema.fail(where, "scenario applicability requires bool")
                if scenario in required_scenarios and not rule["applicable"]:
                    schema.fail(where, "required native consumer scenario cannot be made inapplicable")
                if not rule["applicable"]:
                    schema.text(rule.get("reason"), where)
                    source_refs(rule.get("source_refs"), where)
                elif "reason" in rule:
                    schema.fail(where, "applicable scenario cannot declare inapplicability reason")
        for owner in owner_ids:
            coverage.add((owner, name))
        assigned_names.add(name)
        assignments[where] = row
    # The independent ownership snapshot retains required consumers before
    # their concrete API/member/conversion assignments have been reviewed.
    # A first executed consumer cannot reduce the known owner/context scope.
    pending_owners = [
        (child, owner)
        for child in requirements.values()
        for owner in child.get("settings_consumers", [])
        if (child["id"], owner["key"]) not in coverage
    ]
    owned_names = {
        owner["key"]
        for child in requirements.values()
        for owner in child.get("settings_consumers", [])
    }
    result["required_consumers"] = len(assignments) + len(pending_owners)
    result["required_contexts"] = (
        sum(len(row["contexts"]) for row in assignments.values())
        + sum(len(execution.required_contexts(child)) for child, _ in pending_owners)
    )
    for name in sorted(exposed.keys() - assigned_names - owned_names):
        result["unresolved_enumeration"].append({"setting_key": name, "reason": "Exposed native definition has no independently assigned owner"})
    for child, owner in pending_owners:
        identity = {"requirement_id": child["id"], "setting_key": owner["key"]}
        result["missing_contracts"].append({**identity, "reason": "Known setting consumer lacks a concrete assignment and contract"})
        for context in sorted(execution.required_contexts(child)):
            result["missing_proofs"].append({**identity, "context": context, "reason": "Known setting consumer context lacks a concrete assignment and proof"})

    surfaces = {row["id"]: row for row in commands.get("surface_contracts", [])}
    contracts = {}
    by_assignment = {}
    for row in schema.rows(commands["settings_consumer_contracts"], "consumer contracts"):
        where = row["id"]
        assignment_id = schema.identity(row.get("assignment_id"), where)
        assignment = assignments.get(assignment_id)
        if assignment is None or assignment_id in by_assignment:
            schema.fail(where, "orphaned or duplicate assignment consumer contract")
        binding = bindings[assignment["definition_binding_id"]]
        definition = definitions_by_binding[binding["id"]][assignment["setting_key"]]
        for field, expected in (("assignment", assignment), ("definition_binding", binding), ("definition", definition)):
            if not execution.same(row.get(field), expected):
                schema.fail(where, "frozen consumer assignment/definition/binding changed")
        contexts = schema.obj(row.get("contexts"), where)
        if set(contexts) != set(assignment["contexts"]):
            schema.fail(where, "consumer contract omits required context")
        for context, value in contexts.items():
            owner = schema.identity(value.get("requirement_id"), where)
            surface = surfaces.get(value.get("surface_contract_id"))
            if owner not in assignment["requirement_ids"] or surface is None or surface["requirement_id"] != owner or context not in surface["contexts"] or value.get("surface_contract_sha256") != surface["sha256"]:
                schema.fail(where, "consumer references wrong frozen surface requirement/context/digest")
            scenarios = schema.obj(value.get("scenarios"), where)
            expected_scenarios = {name for name, rule in assignment["contexts"][context]["scenarios"].items() if rule["applicable"]}
            if set(scenarios) != expected_scenarios:
                schema.fail(where, "consumer contract omits applicable scenarios")
            rules = {a["id"]: a for a in surface["contexts"][context]}
            used_ids = set()
            effects = {}
            for scenario, item in scenarios.items():
                schema.obj(item, where)
                for field in ("setting_value", "current_setting_value", "effective_parameter_value"):
                    if field not in item:
                        schema.fail(where, "consumer scenario lacks typed values")
                    execution.finite_json(item[field], where)
                typed_value(definition, item["setting_value"], where)
                typed_value(definition, item["current_setting_value"], where)
                parameter_value(assignment, requirements[owner], definition, item["effective_parameter_value"], where)
                if scenario != "override" and not execution.same(item["effective_parameter_value"], effective_value(assignment, definition, item["setting_value"], where)):
                    schema.fail(where, "effective parameter differs from consumed setting relation")
                mapping = schema.obj(item.get("assertion_ids"), where)
                required_ids = {"setting", "current", "effective", "effect"}
                if scenario == "override":
                    if "override_value" not in item:
                        schema.fail(where, "override scenario lacks explicit value")
                    execution.finite_json(item["override_value"], where)
                    parameter_value(assignment, requirements[owner], definition, item["override_value"], where)
                    required_ids.add("override")
                if scenario == "frozen":
                    schema.choice(item.get("captured_scenario"), {"default", "changed"}, where)
                if scenario == "persisted":
                    required_ids |= {"persistence_file", "reopened_setting"}
                    execution.file_identity(item.get("persistence"), where)
                    bytes_for(item["persistence"], where, "persisted_file")
                if set(mapping) != required_ids:
                    schema.fail(where, "consumer scenario omits or adds assertion mappings")
                for assertion_id in mapping.values():
                    schema.identity(assertion_id, where)
                    if assertion_id not in rules or assertion_id in used_ids:
                        schema.fail(where, "missing or reused consumer scenario assertion ID")
                    used_ids.add(assertion_id)
                values = {"setting": item["setting_value"], "current": item["current_setting_value"], "effective": item["effective_parameter_value"]}
                if scenario == "override":
                    values["override"] = item["override_value"]
                if scenario == "persisted":
                    values["reopened_setting"] = item["current_setting_value"]
                    rule = rules[mapping["persistence_file"]]
                    if rule["rule"] != "sha256_equal" or rule["expected"] != item["persistence"]["sha256"]:
                        schema.fail(where, "persisted file assertion differs from concrete bytes")
                for field, expected in values.items():
                    rule = rules[mapping[field]]
                    if isinstance(expected, list):
                        if rule["rule"] != "sha256_equal" or rule["expected"] != hashlib.sha256(execution.canonical(expected)).hexdigest():
                            schema.fail(where, "typed consumer list differs from canonical digest assertion")
                    elif rule["rule"] != "scalar_equal" or not execution.same(rule["expected"], expected):
                        schema.fail(where, "typed consumer value differs from frozen assertion")
                effect = rules[mapping["effect"]]
                if effect.get("observation") != "behavior" or (effect["rule"] == "scalar_equal" and effect["value_type"] in {"boolean", "null"}):
                    schema.fail(where, "consumer effect requires computed nonboolean behavior")
                if mapping["effect"] in {mapping["setting"], mapping["current"], mapping["effective"]}:
                    schema.fail(where, "setting readback cannot replace computed effect")
                effects[scenario] = effect["rule"], effect.get("value_type"), effect["expected"]
            default, changed = scenarios["default"], scenarios["changed"]
            if not execution.same(default["setting_value"], definition["default"]) or not execution.same(default["current_setting_value"], definition["default"]):
                schema.fail(where, "default scenario differs from native default")
            if execution.same(changed["setting_value"], definition["default"]) or not execution.same(changed["setting_value"], changed["current_setting_value"]) or execution.same(effects["default"], effects["changed"]):
                schema.fail(where, "changed scenario must change setting and computed effect")
            if "frozen" in scenarios:
                frozen = scenarios["frozen"]
                captured_name = frozen["captured_scenario"]
                captured = scenarios[captured_name]
                if not execution.same(frozen["setting_value"], captured["setting_value"]) or execution.same(frozen["current_setting_value"], captured["setting_value"]) or not execution.same(frozen["effective_parameter_value"], captured["effective_parameter_value"]) or not execution.same(effects["frozen"], effects[captured_name]):
                    schema.fail(where, "frozen scenario does not retain captured value/effect after settings change")
            if "override" in scenarios:
                override = scenarios["override"]
                baseline = scenarios.get("frozen", default)
                if not execution.same(override["setting_value"], baseline["setting_value"]) or not execution.same(override["effective_parameter_value"], override["override_value"]) or execution.same(override["override_value"], effective_value(assignment, definition, override["current_setting_value"], where)) or execution.same(override["override_value"], baseline["effective_parameter_value"]) or execution.same(effects["override"], effects.get("frozen", effects["default"])):
                    schema.fail(where, "explicit override scenario does not retain override value/result")
            if "persisted" in scenarios:
                persisted = scenarios["persisted"]
                if not execution.same(persisted["setting_value"], changed["setting_value"]) or not execution.same(persisted["current_setting_value"], changed["setting_value"]) or not execution.same(persisted["effective_parameter_value"], changed["effective_parameter_value"]) or not execution.same(effects["persisted"], effects["changed"]):
                    schema.fail(where, "persisted scenario differs from changed setting/effect")
        schema.sha(row.get("sha256"), where)
        if row["sha256"] != contract_fingerprint(row):
            schema.fail(where, "frozen consumer contract fingerprint changed")
        contracts[where] = row
        by_assignment[assignment_id] = row
    result["claimed_consumers"] = len(contracts)

    surface_proofs = {row["id"]: row for row in commands.get("surface_proofs", [])}
    proof_keys = set()
    selected = {}
    for row in schema.rows(commands["settings_consumer_proofs"], "consumer proofs"):
        where = row["id"]
        contract = contracts.get(row.get("contract_id"))
        assignment_id = schema.identity(row.get("assignment_id"), where)
        context = execution.context(row.get("context"), where)
        if contract is None or contract["assignment_id"] != assignment_id or context not in contract["contexts"]:
            schema.fail(where, "orphaned or wrong-context consumer proof")
        key = assignment_id, context
        if key in proof_keys:
            schema.fail(where, "duplicate consumer assignment/context proof")
        proof_keys.add(key)
        surface = surface_proofs.get(row.get("surface_proof_id"))
        target = contract["contexts"][context]
        if surface is None or surface["requirement_id"] != target["requirement_id"] or surface["context"] != context or surface["contract_id"] != target["surface_contract_id"]:
            schema.fail(where, "consumer proof references wrong exact surface proof")
        selected[key] = surface

    for assignment_id, assignment in assignments.items():
        contract = by_assignment.get(assignment_id)
        if contract is None:
            result["missing_contracts"].append({"assignment_id": assignment_id, "setting_key": assignment["setting_key"], "reason": "Required consumer contract is missing"})
        for context in assignment["contexts"]:
            surface = selected.get((assignment_id, context))
            if surface is None:
                result["missing_proofs"].append({"assignment_id": assignment_id, "context": context, "reason": "Required consumer context proof is missing"})
                continue
            unavailable = [gap for gap in surface_result["unavailable_evidence"] if gap.get("requirement_id") == surface["requirement_id"] and gap.get("context") == context]
            result["unavailable_evidence"].extend({"assignment_id": assignment_id, **gap} for gap in unavailable)
            catalogue = next(entry for entry in runs[surface["run_id"]]["surface_traces"] if entry["id"] == surface["trace_id"])
            for scenario in contract["contexts"][context]["scenarios"].values():
                if "persistence" in scenario:
                    file = scenario["persistence"]
                    if not any(execution.same(file, artifact) for artifact in catalogue["producer"]["artifacts"]):
                        schema.fail(assignment_id, "persisted file lacks accepted producer artifact identity")
                    if bytes_for(file, assignment_id, "persisted_file") is None:
                        unavailable.append({"kind": "persisted_file"})
            if scope_available and binding_available[assignment["definition_binding_id"]] and not unavailable:
                result["verified_contexts"] += 1
    if require_consumers and any(result[key] for key in ("unresolved_enumeration", "missing_contracts", "missing_proofs", "unavailable_evidence")):
        schema.fail("settings consumers", "evidence required: native definition/owner/scenario/context coverage is incomplete")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--require-consumers", action="store_true")
    args = parser.parse_args()
    try:
        documents = {
            # Raw discovery descriptors retain SciJava's Infinity priority, as
            # in the existing schema/checker/B2 manifest loaders. Export,
            # snapshot and runtime evidence have their separate strict parsers;
            # typed contracts still reject nonfinite values in validation.
            name: json.loads((args.root / "docs/parity" / name).read_text(encoding="utf-8"))
            for name in schema.REFERENCE_FILES + tuple(schema.COLLECTIONS)
            if (args.root / "docs/parity" / name).is_file()
        }
        result = validate_consumers(args.root, documents, require_consumers=args.require_consumers)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Settings consumer check failed: {error}\n")
    print(f"Settings consumers: {len(result['exposed_keys'])} exposed keys; {result['claimed_consumers']}/{result['required_consumers']} contracts; {result['verified_contexts']}/{result['required_contexts']} contexts verified; {len(result['unresolved_enumeration'])} enumeration gaps; {len(result['missing_proofs'])} missing proofs; {len(result['unavailable_evidence'])} unavailable evidence")


if __name__ == "__main__":
    main()
