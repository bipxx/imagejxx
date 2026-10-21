"""Validate plan/inventory traceability; this does not certify feature parity."""

from __future__ import annotations

import argparse
from collections import Counter
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

# Load the sibling explicitly: tests and consumers also load this script by path.
_parity_spec = importlib.util.spec_from_file_location(
    "imagejxx_parity_schema", Path(__file__).with_name("parity_schema.py")
)
_parity_schema = importlib.util.module_from_spec(_parity_spec)
_parity_spec.loader.exec_module(_parity_schema)
_evidence_spec = importlib.util.spec_from_file_location(
    "imagejxx_parity_evidence", Path(__file__).with_name("parity_evidence.py")
)
_parity_evidence = importlib.util.module_from_spec(_evidence_spec)
_evidence_spec.loader.exec_module(_parity_evidence)
_execution_spec = importlib.util.spec_from_file_location(
    "imagejxx_parity_execution", Path(__file__).with_name("parity_execution.py")
)
_parity_execution = importlib.util.module_from_spec(_execution_spec)
_execution_spec.loader.exec_module(_parity_execution)
_consumer_spec = importlib.util.spec_from_file_location(
    "imagejxx_parity_consumers", Path(__file__).with_name("parity_consumers.py")
)
_parity_consumers = importlib.util.module_from_spec(_consumer_spec)
_consumer_spec.loader.exec_module(_parity_consumers)
_fixture_spec = importlib.util.spec_from_file_location(
    "imagejxx_parity_fixtures", Path(__file__).with_name("parity_fixtures.py")
)
_parity_fixtures = importlib.util.module_from_spec(_fixture_spec)
_fixture_spec.loader.exec_module(_parity_fixtures)

REFERENCE_MANIFESTS = ("references.json", "fiji-components.json", "commands.json",
                       "formats.json", "fixtures.json", "benchmarks.json", "platforms.json")


def validate_references(require_complete=False):
    """Check current reference facts without certifying missing review schemas."""
    directory = ROOT / "docs/parity"
    missing = [name for name in REFERENCE_MANIFESTS if not (directory / name).is_file()]
    if "references.json" in missing:
        if require_complete:
            raise ValueError(f"Missing reference manifests: {', '.join(missing)}")
        return {"missing": missing, "components_pending": 0, "static_candidates": 0}
    documents = {}
    for name in REFERENCE_MANIFESTS:
        if name in missing:
            continue
        value = json.loads((directory / name).read_text(encoding="utf-8"))
        if value.get("schema_version") != 1:
            raise ValueError(f"{name}: unsupported reference schema")
        documents[name] = value
    refs = documents["references.json"]
    reference_id = refs.get("reference_id")
    if not reference_id or any(d.get("reference_id") != reference_id for d in documents.values()):
        raise ValueError("Reference manifests must describe the same pinned baseline")
    for key in ("imagej", "fiji"):
        if not re.fullmatch(r"[0-9a-f]{64}", refs[key].get("sha256", "")):
            raise ValueError(f"{key}: missing concrete reference archive checksum")
    if not re.fullmatch(r"[0-9a-f]{40}", refs["imagej"].get("commit", "")):
        raise ValueError("ImageJ requires an immutable commit pin")
    components = documents.get("fiji-components.json")
    commands = documents.get("commands.json")
    if components is None or commands is None:
        raise ValueError("Pinned references require component and command candidate manifests")
    if components.get("archive_sha256") != refs["fiji"]["sha256"]:
        raise ValueError("Fiji component facts use a different archive")
    rows = components["files"]
    identities = {row["id"] for row in rows}
    if len(identities) != len(rows):
        raise ValueError("Reference component IDs must be unique")
    pending = 0
    for row in rows:
        if not re.fullmatch(r"[0-9a-f]{64}", row.get("sha256", "")) or row.get("bytes", -1) < 0:
            raise ValueError(f"{row['id']}: invalid byte inventory")
        review = row["commercial_review"]
        decision = review["decision"]
        if decision not in {"pending", "eligible", "excluded"}:
            raise ValueError(f"{row['id']}: unknown commercial decision")
        if decision == "pending":
            pending += 1
            if review["incorporation"] != "blocked_until_review":
                raise ValueError(f"{row['id']}: unresolved material cannot be incorporated")
        elif not review.get("evidence") or not review.get("reason"):
            raise ValueError(f"{row['id']}: a commercial decision requires exact evidence and rationale")
        elif decision == "eligible" and any(review[key] == "pending" for key in ("code", "dependencies", "models_assets")):
            raise ValueError(f"{row['id']}: eligibility requires transitive code/model/asset review")
        resources = row.get("resources", [])
        resource_ids = set()
        for resource in resources:
            identity = (resource["member"], resource["entry_occurrence"])
            if identity in resource_ids:
                raise ValueError(f"{row['id']}: duplicate resource occurrence")
            resource_ids.add(identity)
            if (not re.fullmatch(r"[0-9a-f]{64}", resource.get("sha256", ""))
                    or resource.get("bytes", -1) < 0 or resource["entry_occurrence"] < 0):
                raise ValueError(f"{row['id']}: invalid resource byte inventory")
            resource_decision = resource["license_review"]
            if resource_decision not in {"pending", "eligible", "excluded"}:
                raise ValueError(f"{row['id']}: unknown resource license decision")
            if resource_decision != "pending" and not resource.get("license_evidence"):
                raise ValueError(f"{row['id']}: resource license decision requires evidence")
            if decision == "eligible" and resource_decision != "eligible":
                raise ValueError(f"{row['id']}: incorporated resources require eligibility review")
    candidates = commands["candidates"]
    if len({row["id"] for row in candidates}) != len(candidates):
        raise ValueError("Reference candidate IDs must be unique")
    for row in candidates:
        if row["component_id"] not in identities | {"imagej-core"}:
            raise ValueError(f"{row['id']}: orphaned command/provider candidate")
        if not row.get("evidence"):
            raise ValueError(f"{row['id']}: missing candidate provenance")
    # Partial structure/identity validation is additive. It does not enable the
    # strict certification gate, whose fail-closed fallback remains below.
    _parity_schema.validate_documents(documents)
    evidence = _parity_evidence.validate_evidence(ROOT, documents)
    execution = _parity_execution.validate_execution(ROOT, documents)
    consumers = _parity_consumers.validate_consumers(ROOT, documents)
    fixtures = _parity_fixtures.validate_fixtures(ROOT, documents)
    if require_complete:
        if missing:
            raise ValueError(f"Missing reference manifests: {', '.join(missing)}")
        if pending or any(row["status"] == "candidate" for row in candidates):
            raise ValueError("Reference commercial/runtime/option review is incomplete")
        _parity_fixtures.validate_fixtures(ROOT, documents, require_complete=True)
        raise ValueError("Full reference certification still requires the format/fixture/performance/platform and option-level validators")
    return {"missing": missing, "components_pending": pending, "static_candidates": len(candidates),
            "reports_verified": evidence["reports_verified"],
            "unavailable_reports": evidence["unavailable_reports"],
            "surfaces_verified": execution["verified_contexts"],
            "surfaces_required": execution["required_contexts"],
            "missing_surface_proofs": execution["missing_proofs"],
            "unavailable_surface_evidence": execution["unavailable_evidence"],
            "settings_consumers": consumers,
            "fixture_evidence": fixtures}


def audit_rows():
    text = (ROOT / "docs/DESKTOP.md").read_text(encoding="utf-8")
    audit = text.split("## Editor completeness audit (2026-10-09)", 1)[1]
    audit = audit.split("### Implementation order", 1)[0]
    result = []
    for line in audit.splitlines():
        if not line.startswith("| "):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells[0] in {"Capability", "Family and reference"} or cells[0].startswith(
            "---"
        ):
            continue
        name = re.sub(r"\[([^]]+)\]\([^)]+\)", r"\1", cells[0])
        result.append((name, cells[2]))
    return result


def validate_inventory(require_complete=False):
    plan = (ROOT / "docs/PLAN.md").read_text(encoding="utf-8")
    mappings = [
        match.groups()
        for match in re.finditer(r"(?m)^\| (G\d{3}) \| (.*?) \| (.*?) \|$", plan)
    ]
    inventory = json.loads(
        (ROOT / "docs/parity/inventory.json").read_text(encoding="utf-8")
    )
    if inventory.get("schema_version") != 1:
        raise ValueError("Unsupported inventory schema")
    run_ids = [run["id"] for run in inventory.get("verification_runs", [])]
    if len(run_ids) != len(set(run_ids)):
        raise ValueError("Verification run IDs must be unique")
    rows = inventory["capabilities"]
    expected = audit_rows()
    if len(rows) != len(expected) or len(rows) != len(mappings):
        raise ValueError("Inventory must retain every audit and plan entry")
    states = Counter()
    for row, reference, mapping in zip(rows, expected, mappings):
        gap, name, milestones = mapping
        if (row["id"], row["name"], row["required_work"]) != (gap, *reference):
            raise ValueError(f"{gap}: audit requirements changed or were dropped")
        if name != row["name"] or milestones.split(", ") != row["milestones"]:
            raise ValueError(f"{gap}: plan assignments changed")
        state = row["status"]
        if state not in {"open", "in_progress", "complete"}:
            raise ValueError(f"{gap}: unknown implementation status")
        states[state] += 1
        for run_id in row.get("verification", []):
            if run_id not in run_ids:
                raise ValueError(f"{gap}: unknown verification run {run_id}")
        for category in ("implementation", "tests", "documentation"):
            for evidence in row["evidence"].get(category, []):
                target = (ROOT / evidence).resolve()
                if not target.is_relative_to(ROOT) or not target.is_file():
                    raise ValueError(f"{gap}: missing {category} evidence: {evidence}")
        if state == "complete":
            if row["remaining"] or not row.get("verification"):
                raise ValueError(
                    f"{gap}: completion requires no remaining work and verification records"
                )
            if not all(
                row["evidence"].get(key)
                for key in ("implementation", "tests", "documentation")
            ):
                raise ValueError(
                    f"{gap}: completion needs implementation, test and documentation evidence"
                )
    if require_complete and states["complete"] != len(rows):
        raise ValueError(f"Parity is incomplete: {dict(states)}")
    return len(rows), states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--require-reference-complete", action="store_true")
    args = parser.parse_args()
    try:
        count, states = validate_inventory(args.require_complete)
        references = validate_references(args.require_reference_complete or args.require_complete)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"Plan inventory check failed: {error}\n")
    print(f"Coverage retained: {count} areas; implementation states: {dict(states)}")
    print("Traceability validation does not certify numerical or UI parity.")
    print(f"Reference candidates: {references['static_candidates']}; pending commercial components: {references['components_pending']}; missing manifests: {', '.join(references['missing']) or 'none'}")
    unavailable = references.get("unavailable_reports", [])
    print(f"Historical reports: {references.get('reports_verified', 0)} verified; {len(unavailable)} unavailable")
    for report in unavailable:
        print(f"Unresolved evidence: {report['run_id']} {report['role']}: {report['path']} ({report['reason']})")
    print(f"Executed surfaces: {references.get('surfaces_verified', 0)}/{references.get('surfaces_required', 0)} verified; {len(references.get('missing_surface_proofs', []))} missing proofs; {len(references.get('unavailable_surface_evidence', []))} unavailable evidence files")
    consumers = references.get("settings_consumers", {})
    print(f"Settings consumers: {len(consumers.get('exposed_keys', []))} exposed keys; {consumers.get('claimed_consumers', 0)}/{consumers.get('required_consumers', 0)} contracts; {consumers.get('verified_contexts', 0)}/{consumers.get('required_contexts', 0)} contexts verified; {len(consumers.get('unresolved_enumeration', []))} enumeration gaps; {len(consumers.get('missing_contracts', []))} missing contracts; {len(consumers.get('missing_proofs', []))} missing proofs; {len(consumers.get('unavailable_evidence', []))} unavailable evidence")
    fixtures = references.get("fixture_evidence", {})
    print(f"Fixture evidence: {fixtures.get('fixtures_declared', 0)} declared; {fixtures.get('contracts_validated', 0)}/{fixtures.get('contracts_required', 0)} contracts; {fixtures.get('verified_case_assertions', 0)}/{fixtures.get('required_case_assertions', 0)} case assertions verified; {len(fixtures.get('missing_contracts', []))} missing contracts; {len(fixtures.get('capture_gaps', []))} capture gaps; {len(fixtures.get('unavailable_evidence', []))} unavailable evidence; {len(fixtures.get('unsupported_methods', []))} unsupported methods; {len(fixtures.get('enumeration_gaps', []))} enumeration gaps")


if __name__ == "__main__":
    main()
