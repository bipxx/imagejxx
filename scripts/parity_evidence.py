"""Validate partial requirement ownership and historical accepted-run identity.

This checks declared relationships and supported report bytes. It does not
certify option coverage, surfaces, settings consumers, fixtures or artifacts.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "imagejxx_evidence_schema", Path(__file__).with_name("parity_schema.py")
)
schema = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(schema)
TOTAL_KEYS = {"passed", "failed", "skipped", "errors", "total"}
REPORT_FORMATS = {
    "native": "ctest_log_v1",
    "python": "pytest_junit_v1",
    "sdk": "ctest_log_v1",
}


def run_fingerprint(run):
    payload = json.dumps(
        run, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def totals(value, where):
    schema.obj(value, where)
    if set(value) != TOTAL_KEYS:
        schema.fail(where, "requires exact passed/failed/skipped/errors/total counts")
    for key in TOTAL_KEYS:
        if type(value[key]) is not int or value[key] < 0:
            schema.fail(where, "totals require nonnegative integers")
    if value["total"] != sum(value[k] for k in TOTAL_KEYS - {"total"}):
        schema.fail(where, "inconsistent report totals")
    if value["passed"] <= 0 or any(value[k] for k in ("failed", "skipped", "errors")):
        schema.fail(
            where, "requires nonempty successful evidence without failures/errors/skips"
        )
    return value


def junit_children(element, allowed, where):
    """The supported pytest format uses direct outcomes and plain metadata."""
    for child in element:
        if child.tag not in allowed:
            schema.fail(where, "unsupported JUnit child/outcome placement")
        if child.tag == "properties":
            if any(prop.tag != "property" or len(prop) for prop in child):
                schema.fail(where, "unsupported JUnit properties/outcome placement")
        elif child.tag != "testcase" and len(child):
            schema.fail(where, "nested JUnit metadata/outcome placement is unsupported")


def _pytest_junit_cases(data, where):
    try:
        root = ET.fromstring(data)
    except (ET.ParseError, LookupError, UnicodeError) as error:
        schema.fail(where, f"invalid JUnit XML: {error}")
    if root.tag == "testsuite":
        suites = [root]
    elif root.tag == "testsuites":
        suites = list(root)
        if any(s.tag != "testsuite" for s in suites):
            schema.fail(where, "unsupported JUnit collection structure")
    else:
        schema.fail(where, "unsupported JUnit root")
    if not suites:
        schema.fail(where, "empty JUnit suites")
    result = dict.fromkeys(TOTAL_KEYS, 0)
    extracted = []
    seen = set()
    for suite in suites:
        junit_children(
            suite, {"properties", "testcase", "system-out", "system-err"}, where
        )
        if suite.findall("testsuite") or suite.findall("testsuites"):
            schema.fail(where, "nested JUnit suites are unsupported")
        declared = {}
        for key in ("tests", "failures", "errors", "skipped"):
            value = suite.get(key, "")
            if not re.fullmatch(r"\d+", value):
                schema.fail(where, "missing/invalid JUnit suite counts")
            declared[key] = int(value)
        cases = suite.findall("testcase")
        actual = dict.fromkeys(TOTAL_KEYS, 0)
        for case in cases:
            junit_children(
                case,
                {
                    "properties",
                    "failure",
                    "error",
                    "skipped",
                    "system-out",
                    "system-err",
                },
                where,
            )
            name = schema.text(case.get("name"), where)
            identity = (suite.get("name", ""), case.get("classname", ""), name)
            if identity in seen:
                schema.fail(where, "duplicate JUnit testcase identity")
            seen.add(identity)
            outcomes = [
                tag for tag in ("failure", "error", "skipped") if case.findall(tag)
            ]
            if len(outcomes) > 1 or any(len(case.findall(tag)) > 1 for tag in outcomes):
                schema.fail(where, "ambiguous JUnit testcase outcome")
            actual[
                {"failure": "failed", "error": "errors", "skipped": "skipped"}.get(
                    outcomes[0] if outcomes else "", "passed"
                )
            ] += 1
            actual["total"] += 1
            extracted.append(
                {
                    "identity": {
                        "suite": identity[0],
                        "classname": identity[1],
                        "name": identity[2],
                    },
                    "outcome": {
                        "failure": "failed",
                        "error": "errors",
                        "skipped": "skipped",
                    }.get(outcomes[0] if outcomes else "", "passed"),
                }
            )
        if (
            declared["tests"] != actual["total"]
            or declared["failures"] != actual["failed"]
            or declared["errors"] != actual["errors"]
            or declared["skipped"] != actual["skipped"]
        ):
            schema.fail(
                where, "JUnit declared totals differ from actual testcase outcomes"
            )
        for key in TOTAL_KEYS:
            result[key] += actual[key]
    if root.tag == "testsuites":
        for key, count_key in (
            ("tests", "total"),
            ("failures", "failed"),
            ("errors", "errors"),
            ("skipped", "skipped"),
        ):
            if key in root.attrib and root.attrib[key] != str(result[count_key]):
                schema.fail(where, "JUnit collection totals disagree with suites")
    return {"totals": totals(result, where), "cases": extracted}


def pytest_junit(data, where):
    """Keep the public totals API; exact cases share all rejection checks."""
    return _pytest_junit_cases(data, where)["totals"]


def _ctest_log_cases(data, where):
    try:
        content = data.decode("utf-8-sig")
    except UnicodeError:
        schema.fail(where, "CTest log must be UTF-8")
    summaries = re.findall(
        r"(?m)^\s*(\d+)% tests passed, (\d+) tests failed out of (\d+)\s*$", content
    )
    if len(summaries) != 1:
        schema.fail(where, "requires exactly one unambiguous CTest summary")
    percent, failed, count = map(int, summaries[0])
    if percent != 100 or failed or count <= 0:
        schema.fail(where, "failed/empty CTest summary")
    lines = [
        line
        for line in content.splitlines()
        if re.match(r"^\s*\d+/\d+\s+Test\s+#", line)
    ]
    if len(lines) != count:
        schema.fail(where, "CTest totals differ from actual test lines")
    indices, registered_ids, names = set(), set(), set()
    extracted = []
    for line in lines:
        match = re.fullmatch(
            r"\s*(\d+)/(\d+)\s+Test\s+#(\d+):\s+(.+?)\s+\.{2,}\s+Passed\s+\d+(?:\.\d+)?\s+sec\s*",
            line,
        )
        if match is None:
            schema.fail(where, "nonpassed or unsupported CTest test line")
        index, denominator, test_id = map(int, match.group(1, 2, 3))
        name = match.group(4)
        if (
            denominator != count
            or test_id <= 0
            or test_id in registered_ids
            or index in indices
            or name in names
        ):
            schema.fail(where, "duplicate/mismatched CTest test identity")
        indices.add(index)
        registered_ids.add(test_id)
        names.add(name)
        extracted.append(
            {
                "identity": {"registered_test_id": test_id, "name": name},
                "outcome": "passed",
            }
        )
    if indices != set(range(1, count + 1)):
        schema.fail(where, "incomplete CTest test identities")
    result = totals(
        {"passed": count, "failed": 0, "skipped": 0, "errors": 0, "total": count}, where
    )
    return {"totals": result, "cases": extracted}


def ctest_log(data, where):
    """Keep the public totals API; registered IDs are independent of ordinals."""
    return _ctest_log_cases(data, where)["totals"]


def extract_report_cases(data, format_name, where):
    """Return exact successful case identities using the existing strict parsers."""
    if format_name == "pytest_junit_v1":
        return _pytest_junit_cases(data, where)
    if format_name == "ctest_log_v1":
        return _ctest_log_cases(data, where)
    schema.fail(where, "unsupported report format remains an evidence gap")


def parse_report(data, format_name, where):
    if format_name == "pytest_junit_v1":
        return pytest_junit(data, where)
    if format_name == "ctest_log_v1":
        return ctest_log(data, where)
    schema.fail(where, "unsupported report format remains an evidence gap")


def historical_bytes(target):
    """Read actual evidence, or return None for missing/inaccessible bytes.

    Callers validate the declared path first and enforce required-mode failures.
    No claimed hash substitutes for bytes; readable corruption remains an error.
    Unexpected I/O errors propagate. Seeding new identities still requires a
    successful direct read and never uses this optional historical reader.
    """
    try:
        return target.read_bytes() if target.is_file() else None
    except (FileNotFoundError, PermissionError):
        return None


def report_path(root, value, where):
    schema.path(value, where)
    root = Path(root).resolve()
    target = (root / value).resolve()
    if not target.is_relative_to(root):
        schema.fail(where, "report path escapes workspace")
    if target.exists() and not target.is_file():
        schema.fail(where, "report path is not a file")
    return target


def historical_evidence_bytes(root, value, where):
    """Unreadable path resolution grants no path access and no evidence credit."""
    try:
        target = report_path(root, value, where)
    except (FileNotFoundError, PermissionError):
        return None
    return historical_bytes(target)


def expected_reports(run):
    reports = {}
    for role in ("native", "python"):
        if role in run:
            suite = schema.obj(run[role], run["id"] + " " + role)
            passed, failed = suite.get("passed"), suite.get("failed")
            skipped, errors = suite.get("skipped", 0), suite.get("errors", 0)
            value = {
                "passed": passed,
                "failed": failed,
                "skipped": skipped,
                "errors": errors,
                "total": sum(
                    v for v in (passed, failed, skipped, errors) if type(v) is int
                ),
            }
            reports[role] = (
                schema.text(suite.get("report"), run["id"]),
                totals(value, run["id"]),
            )
    if "sdk_report" in run or "sdk_consumers_passed" in run:
        passed = run.get("sdk_consumers_passed")
        value = {
            "passed": passed,
            "failed": 0,
            "skipped": 0,
            "errors": 0,
            "total": passed,
        }
        reports["sdk"] = (
            schema.text(run.get("sdk_report"), run["id"]),
            totals(value, run["id"]),
        )
    if not reports:
        schema.fail(run["id"], "no supported successful suite reports; evidence gap")
    return reports


def expected_reference(run):
    if "upstream_reference" not in run:
        return None
    source = schema.obj(run["upstream_reference"], run["id"])
    result = {
        "commit": source.get("commit"),
        "fixture_path": source.get("fixture"),
        "fixture_sha256": source.get("fixture_sha256"),
        "matched_cases": source.get("matched_cases"),
        "mismatches": source.get("mismatches"),
        "reference_errors": source.get("reference_errors"),
    }
    if not re.fullmatch(r"[0-9a-f]{40}", schema.text(result["commit"], run["id"])):
        schema.fail(run["id"], "invalid accepted reference commit")
    schema.path(result["fixture_path"], run["id"])
    schema.sha(result["fixture_sha256"], run["id"])
    for key in ("matched_cases", "mismatches", "reference_errors"):
        if type(result[key]) is not int or result[key] < 0:
            schema.fail(run["id"], "invalid accepted reference totals")
    if (
        not result["matched_cases"]
        or result["mismatches"]
        or result["reference_errors"]
    ):
        schema.fail(run["id"], "failed/empty accepted processor reference")
    return result


def inventory_data(root, inventory):
    if inventory is None:
        inventory = json.loads(
            (Path(root) / "docs/parity/inventory.json").read_text(encoding="utf-8")
        )
    schema.obj(inventory, "inventory.json")
    schema.version(inventory.get("schema_version"), "inventory.json")
    return inventory


def source_identity(reference):
    return (
        reference["reference_id"],
        reference["component_id"],
        reference["archive_sha256"],
        reference["revision"],
        sorted((m["path"], m["sha256"]) for m in reference["source_members"]),
    )


def fixture_identity(fixture):
    return (
        fixture["reference"]["revision"],
        fixture["path"],
        fixture["sha256"],
        fixture["case_count"],
    )


def accepted_fixture_identity(source):
    return (
        source["commit"],
        source["fixture_path"],
        source["fixture_sha256"],
        source["matched_cases"],
    )


def relationship_graph(documents, inventory):
    commands = documents["commands.json"]
    requirements = {r["id"]: r for r in commands.get("requirements", [])}
    fixtures = {
        r["id"]: r for r in documents.get("fixtures.json", {}).get("fixtures", [])
    }
    families = {
        r["id"]: r
        for r in schema.rows(inventory.get("capabilities"), "inventory families")
    }
    parents = {
        **families,
        **{r["id"]: r for r in commands.get("added_capabilities", [])},
    }
    runs = {
        r["id"]: r
        for r in schema.rows(inventory.get("verification_runs"), "accepted runs")
    }
    linked_runs = set()
    for child in requirements.values():
        where = child["id"]
        parent = parents.get(child["parent_id"])
        if parent is None or not set(child["milestones"]) <= set(
            schema.texts(parent.get("milestones"), where, True)
        ):
            schema.fail(
                where,
                "child milestones must belong to its actual owning family/addition",
            )
        for fixture_id in child["fixture_ids"]:
            fixture = fixtures.get(fixture_id)
            if fixture is None or where not in fixture["requirement_ids"]:
                schema.fail(where, "orphaned or nonreciprocal fixture link")
            # Scope and unknown annotations carry limits, not identity changes.
            if source_identity(child["reference"]) != source_identity(
                fixture["reference"]
            ):
                schema.fail(where, "child/fixture pinned source identity differs")
        for run_id in child["verification_run_ids"]:
            if run_id not in runs:
                schema.fail(where, "unknown accepted run")
            linked_runs.add(run_id)
            source = expected_reference(runs[run_id])
            if (
                source
                and child["reference"]["component_id"] == "imagej-core"
                and child["reference"]["revision"] != source["commit"]
            ):
                schema.fail(where, "child uses a different accepted reference commit")
            if (
                source
                and child["fixture_ids"]
                and not any(
                    run_id in fixtures[f]["verification_run_ids"]
                    and fixture_identity(fixtures[f])
                    == accepted_fixture_identity(source)
                    for f in child["fixture_ids"]
                )
            ):
                schema.fail(
                    where,
                    "accepted processor run does not identify a declared child fixture",
                )
    for fixture in fixtures.values():
        where = fixture["id"]
        for child_id in fixture["requirement_ids"]:
            if (
                child_id not in requirements
                or where not in requirements[child_id]["fixture_ids"]
            ):
                schema.fail(where, "orphaned or nonreciprocal requirement link")
        for run_id in fixture["verification_run_ids"]:
            if run_id not in runs:
                schema.fail(where, "unknown accepted fixture run")
            linked_runs.add(run_id)
            source = expected_reference(runs[run_id])
            if source is None or fixture_identity(fixture) != accepted_fixture_identity(
                source
            ):
                schema.fail(
                    where, "fixture differs from accepted run reference identity/count"
                )
    return requirements, runs, linked_runs


def validate_evidence(root, documents, inventory=None, require_reports=False):
    schema.validate_documents(documents)
    commands = documents["commands.json"]
    extension = {"run_bindings_schema_version", "run_bindings"}
    result = {
        "requirements": len(commands.get("requirements", [])),
        "bindings": 0,
        "reports_verified": 0,
        "unavailable_reports": [],
    }
    if (
        not commands.get("requirements")
        and not extension & commands.keys()
        and not documents.get("fixtures.json", {}).get("fixtures")
    ):
        return result  # Legacy candidate-only graphs do not invent inventory/evidence.
    inventory = inventory_data(root, inventory)
    _, runs, linked_runs = relationship_graph(documents, inventory)
    if extension & commands.keys():
        if not extension <= commands.keys():
            schema.fail("commands.json", "incomplete run bindings extension")
        schema.version(commands["run_bindings_schema_version"], "run bindings")
    elif linked_runs:
        schema.fail(
            "commands.json", "linked accepted runs require immutable report bindings"
        )
    bindings = schema.array(commands.get("run_bindings", []), "run bindings")
    ids = set()
    for binding in bindings:
        schema.obj(binding, "run binding")
        run_id = schema.identity(binding.get("run_id"), "run binding")
        if run_id in ids or run_id not in runs:
            schema.fail(run_id, "duplicate or unknown bound accepted run")
        ids.add(run_id)
        run = runs[run_id]
        if binding.get("inventory_run_sha256") != run_fingerprint(run):
            schema.fail(run_id, "accepted run fingerprint changed")
        expected_source = expected_reference(run)
        bound_source = binding.get("reference")
        if expected_source is None:
            if bound_source is not None or "reference" not in binding:
                schema.fail(
                    run_id, "bound reference identity differs from accepted run"
                )
        elif not isinstance(bound_source, dict) or any(
            key not in bound_source
            or type(bound_source[key]) is not type(value)
            or bound_source[key] != value
            for key, value in expected_source.items()
        ):
            schema.fail(run_id, "bound reference identity differs from accepted run")
        expected = expected_reports(run)
        reports = schema.array(binding.get("reports"), run_id)
        roles = set()
        for report in reports:
            schema.obj(report, run_id)
            role = report.get("role")
            if role not in expected or role in roles:
                schema.fail(run_id, "duplicate/unsupported report role")
            roles.add(role)
            where = run_id + " " + role
            expected_path, expected_totals = expected[role]
            if (
                report.get("path") != expected_path
                or report.get("format") != REPORT_FORMATS[role]
            ):
                schema.fail(where, "report path/format differs from accepted suite")
            schema.sha(report.get("sha256"), where)
            if totals(report.get("totals"), where) != expected_totals:
                schema.fail(where, "report totals differ from accepted run")
            data = historical_evidence_bytes(root, report["path"], where)
            if data is None:
                if require_reports:
                    schema.fail(
                        where,
                        "historical report bytes are unavailable; evidence required",
                    )
                result["unavailable_reports"].append(
                    {
                        "run_id": run_id,
                        "role": role,
                        "path": report["path"],
                        "reason": "Historical report bytes are unavailable; fingerprint is not an executed pass",
                    }
                )
                continue
            if hashlib.sha256(data).hexdigest() != report["sha256"]:
                schema.fail(where, "historical report byte fingerprint changed")
            if parse_report(data, report["format"], where) != report["totals"]:
                schema.fail(
                    where, "actual report totals differ from bound/accepted totals"
                )
            result["reports_verified"] += 1
        if roles != expected.keys():
            schema.fail(run_id, "missing accepted suite report binding")
    if not linked_runs <= ids:
        schema.fail("run bindings", "linked accepted run lacks report fingerprints")
    result["bindings"] = len(bindings)
    return result


def seed_bindings(root, documents, inventory=None):
    schema.validate_documents(documents)
    inventory = inventory_data(root, inventory)
    _, runs, linked_runs = relationship_graph(documents, inventory)
    proposed = deepcopy(documents)
    commands = proposed["commands.json"]
    commands.setdefault("run_bindings_schema_version", 1)
    commands.setdefault("run_bindings", [])
    existing = {r["run_id"] for r in commands["run_bindings"]}
    for run_id in sorted(linked_runs - existing):
        run = runs[run_id]
        binding = {
            "run_id": run_id,
            "inventory_run_sha256": run_fingerprint(run),
            "reference": expected_reference(run),
            "reports": [],
        }
        for role, (path, expected) in expected_reports(run).items():
            target = report_path(root, path, run_id)
            if not target.is_file():
                schema.fail(
                    run_id, "cannot seed fingerprints without actual report bytes"
                )
            data = target.read_bytes()
            actual = parse_report(data, REPORT_FORMATS[role], run_id)
            if actual != expected:
                schema.fail(run_id, "actual report totals differ from accepted run")
            binding["reports"].append(
                {
                    "role": role,
                    "path": path,
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "format": REPORT_FORMATS[role],
                    "totals": actual,
                }
            )
        commands["run_bindings"].append(binding)
    commands["run_bindings"].sort(key=lambda r: r["run_id"])
    validate_evidence(root, proposed, inventory, require_reports=True)
    return proposed


def write_bindings(root, documents, inventory=None, check=False):
    validate_evidence(root, documents, inventory, require_reports=True)
    target = Path(root) / "docs/parity/commands.json"
    previous = (
        json.loads(target.read_text(encoding="utf-8")) if target.is_file() else None
    )
    proposed = documents["commands.json"]
    if previous is not None:
        for key, value in previous.items():
            if key not in {"run_bindings_schema_version", "run_bindings"} and (
                key not in proposed
                or schema.encoded(proposed[key]) != schema.encoded(value)
            ):
                schema.fail(
                    "commands.json",
                    "run binding writes must preserve all existing metadata",
                )
        by_id = {r["run_id"]: r for r in proposed.get("run_bindings", [])}
        for binding in previous.get("run_bindings", []):
            if binding["run_id"] not in by_id or schema.encoded(
                binding
            ) != schema.encoded(by_id[binding["run_id"]]):
                schema.fail(
                    binding["run_id"],
                    "cannot replace existing immutable/enriched run binding",
                )
    payload = schema.encoded(proposed)
    if target.is_file() and target.read_bytes() == payload:
        return False
    if check:
        schema.fail("commands.json", "run bindings require an additive write")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".parity-evidence-", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--require-reports", action="store_true")
    parser.add_argument(
        "--seed",
        action="store_true",
        help="Add missing fingerprints using actual report bytes",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Read-only; when seeding, reject necessary writes",
    )
    args = parser.parse_args()
    documents = {
        name: json.loads((args.root / "docs/parity" / name).read_text(encoding="utf-8"))
        for name in (*schema.REFERENCE_FILES, *schema.COLLECTIONS)
        if (args.root / "docs/parity" / name).is_file()
    }
    if args.seed:
        documents = seed_bindings(args.root, documents)
        changed = write_bindings(args.root, documents, check=args.check)
        print(f"Changed command manifest: {changed}")
    result = validate_evidence(
        args.root, documents, require_reports=args.require_reports
    )
    print(json.dumps(result, sort_keys=True))
    print(
        "Historical identity checks do not certify option/surface/consumer/fixture/commercial/platform/artifact coverage."
    )


if __name__ == "__main__":
    main()
