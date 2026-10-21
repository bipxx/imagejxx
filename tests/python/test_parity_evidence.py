"""Owned relationship graphs and immutable accepted-report identity checks."""

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "owned_parity_evidence_tests", ROOT / "scripts" / "parity_evidence.py"
)
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)

BASELINE = "owned-evidence-reference"
CHILD = "G052.owned-neighbors"
CORPUS = "F.owned-neighbors"
RUN = "owned-accepted-run"
PATHS = {
    "native": "build/owned-evidence/native.log",
    "python": "build/owned-evidence/python.xml",
    "sdk": "build/owned-evidence/sdk.log",
}
NATIVE_REPORT = (
    "1/2 Test #1: owned_native_one ... Passed 0.01 sec\n"
    "2/2 Test #2: owned_native_two ... Passed 0.02 sec\n"
    "100% tests passed, 0 tests failed out of 2\n"
).encode()
PYTHON_REPORT = (
    '<testsuites><testsuite name="owned" tests="2" failures="0" errors="0" skipped="0">'
    '<testcase classname="owned" name="one" time="0.01"/>'
    '<testcase classname="owned" name="two" time="0.02"/>'
    "</testsuite></testsuites>"
).encode()
SDK_REPORT = (
    "1/1 Test #1: owned_sdk ... Passed 0.01 sec\n"
    "100% tests passed, 0 tests failed out of 1\n"
).encode()


def digest(payload):
    return hashlib.sha256(payload).hexdigest()


def independent_run_fingerprint(run):
    return digest(
        json.dumps(
            run,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    )


def source_reference():
    return {
        "reference_id": BASELINE,
        "component_id": "imagej-core",
        "archive_sha256": "b" * 64,
        "revision": "a" * 40,
        "scope": "Owned direct processor scope; UI and plugin behavior pending.",
        "source_members": [{"path": "owned/Processor.java", "sha256": "d" * 64}],
    }


def legacy_documents():
    return {
        "references.json": {
            "schema_version": 1,
            "reference_id": BASELINE,
            "imagej": {"commit": "a" * 40, "sha256": "b" * 64},
            "fiji": {"sha256": "c" * 64},
        },
        "fiji-components.json": {
            "schema_version": 1,
            "reference_id": BASELINE,
            "archive_sha256": "c" * 64,
            "files": [{"id": "fiji:jars/owned.jar", "commercial_review": "pending"}],
        },
        "commands.json": {
            "schema_version": 1,
            "reference_id": BASELINE,
            "candidates": [
                {
                    "id": "R-owned",
                    "component_id": "imagej-core",
                    "evidence": {"archive_member": "owned/Processor.java"},
                    "commercial_review": "pending",
                }
            ],
            "owned_metadata": {"review": "retain", "values": [1, 2]},
        },
    }


def requirement():
    return {
        "id": CHILD,
        "parent_id": "G052",
        "milestones": ["M2"],
        "kind": "option",
        "title": "Owned neighbors",
        "required_work": "Verify the declared neighbor behavior and interfaces.",
        "candidate_ids": ["R-owned"],
        "reference": source_reference(),
        "contract": {
            "parameters": [
                {
                    "name": "neighbors",
                    "type": "integer",
                    "default": 1,
                    "minimum": 1,
                    "maximum": 8,
                    "units": "",
                    "enabled_when": "always",
                }
            ],
            "pixel_types": ["u8"],
            "samples": ["gray"],
            "axes": ["XY"],
            "targets": ["whole_plane"],
            "roi": ["whole_plane"],
            "border": ["constant_background"],
            "nonfinite": "Unsigned input only.",
            "geometry": "Preserves dimensions.",
        },
        "interfaces": {
            name: {
                "applicable": True,
                "entry_points": [f"owned_{name}"],
                "evidence": [],
            }
            for name in ("cpp", "python", "imgui")
        },
        "settings_consumers": [],
        "fixture_ids": [CORPUS],
        "verification_run_ids": [RUN],
        "implementation_status": "partial",
        "verification_status": "partial",
        "commercial": {
            "decision": "pending",
            "evidence": [],
            "reason": "Review pending.",
        },
        "provenance": {
            "implementation": "owned_native_equivalent",
            "incorporated_components": [],
        },
        "remaining": ["Option, interface and commercial closure."],
    }


def owned_graph(tmp_path):
    root = tmp_path / "workspace"
    root.mkdir()
    for role, payload in (
        ("native", NATIVE_REPORT),
        ("python", PYTHON_REPORT),
        ("sdk", SDK_REPORT),
    ):
        target = root / PATHS[role]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    fixture_path = "tests/fixtures/owned-neighbors.json"
    fixture_payload = (
        b'{"owned_cases":[{"input":0,"output":0},{"input":255,"output":255}]}\n'
    )
    fixture_file = root / fixture_path
    fixture_file.parent.mkdir(parents=True)
    fixture_file.write_bytes(fixture_payload)
    fixture_sha = digest(fixture_payload)
    documents = legacy_documents()
    documents["commands.json"].update(
        requirements_schema_version=1,
        enumeration_status="partial",
        requirements=[requirement()],
        added_capabilities=[],
        remaining=["Full enumeration and certification remain pending."],
    )
    documents["fixtures.json"] = {
        "schema_version": 1,
        "reference_id": BASELINE,
        "enumeration_status": "partial",
        "remaining": ["Broader fixture enumeration."],
        "fixtures": [
            {
                "id": CORPUS,
                "requirement_ids": [CHILD],
                "path": fixture_path,
                "sha256": fixture_sha,
                "case_count": 2,
                "reference": source_reference(),
                "generator": {"path": "scripts/owned_capture.py", "sha256": "e" * 64},
                "scope": "Owned processor fixture; no UI claim.",
                "comparison": {"method": "exact", "atol": 0, "rtol": 0},
                "capture_status": "captured",
                "match_status": "matched",
                "verification_run_ids": [RUN],
                "license": {
                    "decision": "pending",
                    "evidence": [],
                    "reason": "Review pending.",
                },
                "remaining": ["Fixture licensing and broader coverage."],
            }
        ],
    }
    inventory = {
        "schema_version": 1,
        "capabilities": [
            {
                "id": "G052",
                "name": "Owned morphology family",
                "milestones": ["M2", "M6"],
                "required_work": "Original owned family contract remains intact.",
                "status": "in_progress",
            }
        ],
        "verification_runs": [
            {
                "id": RUN,
                "date": "2026-10-10",
                "scope": "Owned accepted report identity only.",
                "platform": "Owned synthetic Windows x64",
                "native": {"passed": 2, "failed": 0, "report": PATHS["native"]},
                "python": {
                    "passed": 2,
                    "failed": 0,
                    "skipped": 0,
                    "report": PATHS["python"],
                },
                "sdk_consumers_passed": 1,
                "sdk_report": PATHS["sdk"],
                "upstream_reference": {
                    "baseline": "Owned ImageJ source pin",
                    "commit": "a" * 40,
                    "fixture": fixture_path,
                    "fixture_sha256": fixture_sha,
                    "matched_cases": 2,
                    "mismatches": 0,
                    "reference_errors": 0,
                    "scope": "Owned direct processor comparison.",
                },
                "owned_metadata": {"retain": "accepted inventory facts"},
            }
        ],
    }
    commands_file = root / "docs/parity/commands.json"
    commands_file.parent.mkdir(parents=True)
    commands_file.write_text(
        json.dumps(documents["commands.json"], indent=2) + "\n", encoding="utf-8"
    )
    return root, documents, inventory


def seeded_graph(tmp_path):
    root, documents, inventory = owned_graph(tmp_path)
    return root, evidence.seed_bindings(root, documents, inventory=inventory), inventory


def bytes_under(root):
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file()
    }


def mutate_at(value, keys, replacement):
    for key in keys[:-1]:
        value = value[key]
    value[keys[-1]] = replacement


def bound_report(documents, role):
    return next(
        row
        for row in documents["commands.json"]["run_bindings"][0]["reports"]
        if row["role"] == role
    )


def rewrite_bound_report(root, documents, role, payload):
    report = bound_report(documents, role)
    (root / report["path"]).write_bytes(payload)
    report["sha256"] = digest(payload)


def test_valid_graph_binds_exact_accepted_run_without_promoting_partial_status(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    result = evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    )
    assert result == {
        "requirements": 1,
        "bindings": 1,
        "reports_verified": 3,
        "unavailable_reports": [],
    }
    binding = documents["commands.json"]["run_bindings"][0]
    run = inventory["verification_runs"][0]
    assert binding["inventory_run_sha256"] == independent_run_fingerprint(run)
    assert binding["reference"] == {
        "commit": "a" * 40,
        "fixture_path": run["upstream_reference"]["fixture"],
        "fixture_sha256": run["upstream_reference"]["fixture_sha256"],
        "matched_cases": 2,
        "mismatches": 0,
        "reference_errors": 0,
    }
    assert {row["role"] for row in binding["reports"]} == {"native", "python", "sdk"}
    assert bound_report(documents, "python")["totals"] == {
        "passed": 2,
        "failed": 0,
        "skipped": 0,
        "errors": 0,
        "total": 2,
    }
    child = documents["commands.json"]["requirements"][0]
    assert child["implementation_status"] == child["verification_status"] == "partial"
    assert child["commercial"]["decision"] == "pending"


def test_legacy_candidate_only_graph_remains_valid_partial_without_bindings(tmp_path):
    root, _, inventory = owned_graph(tmp_path)
    documents = legacy_documents()
    retained = deepcopy(documents)
    assert evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    ) == {
        "requirements": 0,
        "bindings": 0,
        "reports_verified": 0,
        "unavailable_reports": [],
    }
    assert documents == retained


def test_full_accepted_run_fingerprint_is_canonical_and_covers_unknown_metadata(
    tmp_path,
):
    _, _, inventory = owned_graph(tmp_path)
    run = inventory["verification_runs"][0]
    assert evidence.run_fingerprint(run) == independent_run_fingerprint(run)
    assert evidence.run_fingerprint(
        dict(reversed(list(run.items())))
    ) == evidence.run_fingerprint(run)
    changed = deepcopy(run)
    changed["owned_metadata"]["retain"] = "changed accepted fact"
    assert evidence.run_fingerprint(changed) != evidence.run_fingerprint(run)


@pytest.mark.parametrize(
    "case", ["missing_family", "family_milestone", "addition_milestone"]
)
def test_milestones_belong_to_actual_owning_family_or_declared_addition(tmp_path, case):
    root, documents, inventory = seeded_graph(tmp_path)
    child = documents["commands.json"]["requirements"][0]
    if case == "missing_family":
        inventory["capabilities"] = []
    elif case == "family_milestone":
        child["milestones"] = ["M0"]  # Globally valid, absent from this family.
    else:
        child.update(
            id="A.owned-extra.neighbors", parent_id="A.owned-extra", milestones=["M2"]
        )
        documents["fixtures.json"]["fixtures"][0]["requirement_ids"] = [child["id"]]
        documents["commands.json"]["added_capabilities"] = [
            {
                "id": "A.owned-extra",
                "title": "Owned addition",
                "required_work": "Verify added contract.",
                "milestones": ["M3"],
                "remaining": ["Partial addition."],
            }
        ]
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


def test_declared_addition_uses_its_own_milestones_and_preferences_can_have_no_fixture(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    child = documents["commands.json"]["requirements"][0]
    child.update(
        id="A.owned-extra.setting",
        parent_id="A.owned-extra",
        milestones=["M3"],
        kind="setting",
        fixture_ids=[],
    )
    documents["fixtures.json"]["fixtures"] = []
    documents["commands.json"]["added_capabilities"] = [
        {
            "id": "A.owned-extra",
            "title": "Owned addition",
            "required_work": "Verify added setting.",
            "milestones": ["M3"],
            "remaining": ["Partial setting evidence."],
        }
    ]
    evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    )


@pytest.mark.parametrize(
    "case", ["unknown_fixture", "unknown_child", "only_child", "only_fixture"]
)
def test_declared_requirement_and_fixture_links_are_reciprocal(tmp_path, case):
    root, documents, inventory = seeded_graph(tmp_path)
    child = documents["commands.json"]["requirements"][0]
    fixture = documents["fixtures.json"]["fixtures"][0]
    if case == "unknown_fixture":
        child["fixture_ids"] = ["F.missing"]
    elif case == "unknown_child":
        fixture["requirement_ids"] = ["G052.missing"]
    elif case == "only_child":
        other = deepcopy(child)
        other.update(id="G052.owned-other", fixture_ids=[])
        documents["commands.json"]["requirements"].append(other)
        fixture["requirement_ids"] = [other["id"]]
    else:
        child["fixture_ids"] = []
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


def test_multiple_children_can_share_one_known_run_and_reciprocal_fixture(tmp_path):
    root, documents, inventory = seeded_graph(tmp_path)
    other = deepcopy(documents["commands.json"]["requirements"][0])
    other["id"] = "G052.owned-iterations"
    documents["commands.json"]["requirements"].append(other)
    documents["fixtures.json"]["fixtures"][0]["requirement_ids"].append(other["id"])
    result = evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    )
    assert result["requirements"] == 2
    assert result["bindings"] == 1
    assert result["reports_verified"] == 3


@pytest.mark.parametrize(
    "case",
    [
        "unknown_child_run",
        "unknown_fixture_run",
        "duplicate_child_run",
        "duplicate_inventory_run",
        "unknown_binding",
        "duplicate_binding",
        "missing_binding",
    ],
)
def test_run_links_are_known_unique_and_bound(tmp_path, case):
    root, documents, inventory = seeded_graph(tmp_path)
    commands = documents["commands.json"]
    if case == "unknown_child_run":
        commands["requirements"][0]["verification_run_ids"] = ["owned-missing-run"]
    elif case == "unknown_fixture_run":
        documents["fixtures.json"]["fixtures"][0]["verification_run_ids"] = [
            "owned-missing-run"
        ]
    elif case == "duplicate_child_run":
        commands["requirements"][0]["verification_run_ids"] = [RUN, RUN]
    elif case == "duplicate_inventory_run":
        inventory["verification_runs"].append(
            deepcopy(inventory["verification_runs"][0])
        )
    elif case == "unknown_binding":
        commands["run_bindings"][0]["run_id"] = "owned-missing-run"
    elif case == "duplicate_binding":
        commands["run_bindings"].append(deepcopy(commands["run_bindings"][0]))
    else:
        commands["run_bindings"] = []
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


@pytest.mark.parametrize("version", [True, False, 0, 2, "1", 1.0])
def test_run_binding_version_is_a_supported_integer(tmp_path, version):
    root, documents, inventory = seeded_graph(tmp_path)
    documents["commands.json"]["run_bindings_schema_version"] = version
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


@pytest.mark.parametrize(
    "case",
    [
        "accepted_fact",
        "run_hash",
        "report_hash",
        "report_role",
        "report_path",
        "report_format",
        "duplicate_role",
        "missing_role",
        "totals",
    ],
)
def test_bound_run_and_report_identity_cannot_drift(tmp_path, case):
    root, documents, inventory = seeded_graph(tmp_path)
    binding = documents["commands.json"]["run_bindings"][0]
    report = bound_report(documents, "native")
    if case == "accepted_fact":
        inventory["verification_runs"][0]["owned_metadata"]["retain"] = (
            "altered accepted fact"
        )
    elif case == "run_hash":
        binding["inventory_run_sha256"] = "0" * 64
    elif case == "report_hash":
        report["sha256"] = "0" * 64
    elif case == "report_role":
        report["role"] = "reference_capture"
    elif case == "report_path":
        report["path"] = PATHS["sdk"]
    elif case == "report_format":
        report["format"] = "unknown_format"
    elif case == "duplicate_role":
        binding["reports"].append(deepcopy(report))
    elif case == "missing_role":
        binding["reports"].remove(report)
    else:
        report["totals"]["total"] = 99
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


@pytest.mark.parametrize(
    "unsafe_path",
    [
        "../outside.log",
        "/outside.log",
        "C:/outside.log",
        "build/../outside.log",
        "build\\outside.log",
    ],
)
def test_report_paths_stay_inside_workspace(tmp_path, unsafe_path):
    root, documents, inventory = owned_graph(tmp_path)
    inventory["verification_runs"][0]["native"]["report"] = unsafe_path
    with pytest.raises(ValueError):
        evidence.seed_bindings(root, documents, inventory=inventory)


def test_missing_historical_report_is_explicit_unavailable_and_required_mode_rejects(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    (root / PATHS["native"]).unlink()
    result = evidence.validate_evidence(root, documents, inventory=inventory)
    assert result["reports_verified"] == 2
    assert len(result["unavailable_reports"]) == 1
    missing = result["unavailable_reports"][0]
    assert missing["run_id"] == RUN
    assert missing["role"] == "native"
    assert missing["path"] == PATHS["native"]
    assert missing["reason"]
    with pytest.raises(ValueError):
        evidence.validate_evidence(
            root, documents, inventory=inventory, require_reports=True
        )


def test_present_changed_bytes_always_reject_even_when_historical_reports_are_optional(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    (root / PATHS["native"]).write_bytes(NATIVE_REPORT + b"owned added bytes\n")
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


@pytest.mark.parametrize(
    "payload",
    [
        b"",
        b"owned text is not JUnit XML",
        b"<testsuites/>",
        b'<testsuites><testsuite tests="0" failures="0" errors="0" skipped="0"/></testsuites>',
        PYTHON_REPORT.replace(b'tests="2"', b'tests="99"'),
        PYTHON_REPORT.replace(
            b'<testcase classname="owned" name="one" time="0.01"/>',
            b'<testcase name="one"><failure message="owned failure"/></testcase>',
        ),
        PYTHON_REPORT.replace(
            b'<testcase classname="owned" name="one" time="0.01"/>',
            b'<testcase name="one"><error message="owned error"/></testcase>',
        ),
        PYTHON_REPORT.replace(
            b'<testcase classname="owned" name="one" time="0.01"/>',
            b'<testcase name="one"><skipped/></testcase>',
        ),
        PYTHON_REPORT.replace(b'failures="0"', b'failures="1"'),
    ],
)
def test_empty_failed_or_internally_inconsistent_junit_cannot_support_accepted_run(
    tmp_path, payload
):
    root, documents, inventory = seeded_graph(tmp_path)
    rewrite_bound_report(root, documents, "python", payload)
    with pytest.raises(ValueError):
        evidence.validate_evidence(
            root, documents, inventory=inventory, require_reports=True
        )


@pytest.mark.parametrize(
    "payload",
    [
        b"",
        b"owned unsupported native output\n",
        b"100% tests passed, 0 tests failed out of 0\n",
        b"100% tests passed, 0 tests failed out of 2\n",
        NATIVE_REPORT.replace(b"2/2 Test #2", b"2/2 Test #1"),
        NATIVE_REPORT.replace(b"2/2 Test #2", b"1/2 Test #2"),
        NATIVE_REPORT + b"100% tests passed, 0 tests failed out of 2\n",
        NATIVE_REPORT.replace(b"0 tests failed", b"1 tests failed"),
        NATIVE_REPORT.replace(
            b"owned_native_two ... Passed", b"owned_native_two ... Failed"
        ),
        NATIVE_REPORT.replace(b"out of 2", b"out of 3"),
    ],
)
def test_ctest_requires_successful_unambiguous_summary_and_unique_complete_test_lines(
    tmp_path, payload
):
    root, documents, inventory = seeded_graph(tmp_path)
    rewrite_bound_report(root, documents, "native", payload)
    with pytest.raises(ValueError):
        evidence.validate_evidence(
            root, documents, inventory=inventory, require_reports=True
        )


def test_multiple_junit_suites_sum_actual_cases_without_counting_wrapper_twice(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    payload = (
        '<testsuites tests="2" failures="0" errors="0" skipped="0">'
        '<testsuite name="one" tests="1" failures="0" errors="0" skipped="0"><testcase name="one"/></testsuite>'
        '<testsuite name="two" tests="1" failures="0" errors="0" skipped="0"><testcase name="two"/></testsuite>'
        "</testsuites>"
    ).encode()
    rewrite_bound_report(root, documents, "python", payload)
    result = evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    )
    assert result["reports_verified"] == 3


@pytest.mark.parametrize("registered_ids", [(4, 9), (9, 4)])
def test_filtered_or_parallel_ctest_registered_ids_are_independent_of_progress_ordinals(
    tmp_path,
    registered_ids,
):
    root, documents, inventory = seeded_graph(tmp_path)
    payload = (
        f"1/2 Test #{registered_ids[0]}: owned_first ... Passed 0.01 sec\n"
        f"2/2 Test #{registered_ids[1]}: owned_second ... Passed 0.02 sec\n"
        "100% tests passed, 0 tests failed out of 2\n"
    ).encode()
    rewrite_bound_report(root, documents, "native", payload)
    result = evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    )
    assert result["reports_verified"] == 3


@pytest.mark.parametrize(
    "payload",
    [
        PYTHON_REPORT.replace(
            b"</testsuite>", b'<error message="owned suite-level error"/></testsuite>'
        ),
        PYTHON_REPORT.replace(
            b'<testcase classname="owned" name="one" time="0.01"/>',
            b'<testcase name="one"><unexpected><failure/></unexpected></testcase>',
        ),
        PYTHON_REPORT.replace(
            b'<testcase classname="owned" name="one" time="0.01"/>',
            b'<testcase name="one"><properties><property name="owned"><failure/></property></properties></testcase>',
        ),
        PYTHON_REPORT.replace(
            b"</testsuite>", b"<system-out><error/></system-out></testsuite>"
        ),
    ],
)
def test_junit_outcomes_cannot_hide_at_suite_level_or_inside_unknown_or_metadata_nodes(
    tmp_path,
    payload,
):
    root, documents, inventory = seeded_graph(tmp_path)
    rewrite_bound_report(root, documents, "python", payload)
    with pytest.raises(ValueError):
        evidence.validate_evidence(
            root, documents, inventory=inventory, require_reports=True
        )


def test_genuine_junit_suite_and_case_metadata_do_not_change_successful_outcome_totals(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    payload = (
        '<testsuites><testsuite name="owned" tests="2" failures="0" errors="0" skipped="0">'
        '<properties><property name="owned_suite" value="true"/></properties>'
        "<system-out>Literal &lt;failure&gt; in owned stdout.</system-out>"
        "<system-err>Owned suite stderr text.</system-err>"
        '<testcase classname="owned" name="one">'
        '<properties><property name="owned_case" value="yes">Owned text.</property></properties>'
        "<system-out>Owned case stdout.</system-out><system-err>Owned case stderr.</system-err>"
        '</testcase><testcase classname="owned" name="two"/>'
        "</testsuite></testsuites>"
    ).encode()
    rewrite_bound_report(root, documents, "python", payload)
    result = evidence.validate_evidence(
        root, documents, inventory=inventory, require_reports=True
    )
    assert result["reports_verified"] == 3


@pytest.mark.parametrize("role", ["native", "python", "sdk"])
def test_actual_successful_totals_must_match_accepted_inventory_totals(tmp_path, role):
    root, documents, inventory = owned_graph(tmp_path)
    run = inventory["verification_runs"][0]
    if role == "sdk":
        run["sdk_consumers_passed"] = 9
    else:
        run[role]["passed"] = 9
    with pytest.raises(ValueError):
        evidence.seed_bindings(root, documents, inventory=inventory)


@pytest.mark.parametrize(
    "key,value",
    [
        ("commit", "f" * 40),
        ("fixture", "tests/fixtures/wrong.json"),
        ("fixture_sha256", "f" * 64),
        ("matched_cases", 3),
        ("mismatches", 1),
        ("reference_errors", 1),
    ],
)
def test_run_upstream_tuple_must_match_pinned_fixture_identity(tmp_path, key, value):
    root, documents, inventory = owned_graph(tmp_path)
    inventory["verification_runs"][0]["upstream_reference"][key] = value
    with pytest.raises(ValueError):
        evidence.seed_bindings(root, documents, inventory=inventory)


@pytest.mark.parametrize(
    "key,value",
    [
        ("commit", "f" * 40),
        ("fixture_path", "tests/fixtures/wrong.json"),
        ("fixture_sha256", "f" * 64),
        ("matched_cases", 3),
        ("mismatches", 1),
        ("reference_errors", 1),
    ],
)
def test_bound_upstream_tuple_cannot_be_relabelled(tmp_path, key, value):
    root, documents, inventory = seeded_graph(tmp_path)
    documents["commands.json"]["run_bindings"][0]["reference"][key] = value
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


@pytest.mark.parametrize("key", ["mismatches", "reference_errors"])
def test_boolean_zero_cannot_replace_bound_integer_reference_count(tmp_path, key):
    root, documents, inventory = seeded_graph(tmp_path)
    documents["commands.json"]["run_bindings"][0]["reference"][key] = False
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


def test_boolean_one_cannot_replace_bound_integer_matched_case_count(tmp_path):
    root, documents, inventory = owned_graph(tmp_path)
    fixture = documents["fixtures.json"]["fixtures"][0]
    payload = b'{"owned_cases":[{"input":0,"output":0}]}\n'
    (root / fixture["path"]).write_bytes(payload)
    fixture.update(case_count=1, sha256=digest(payload))
    inventory["verification_runs"][0]["upstream_reference"].update(
        matched_cases=1,
        fixture_sha256=fixture["sha256"],
    )
    seeded = evidence.seed_bindings(root, documents, inventory=inventory)
    seeded["commands.json"]["run_bindings"][0]["reference"]["matched_cases"] = True
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, seeded, inventory=inventory)


def test_known_unlinked_run_cannot_use_json_integer_as_reference_commit(tmp_path):
    root, documents, inventory = seeded_graph(tmp_path)
    extra_run = deepcopy(inventory["verification_runs"][0])
    extra_run["id"] = "owned-unlinked-run"
    extra_run["upstream_reference"]["commit"] = int("1" * 40)
    inventory["verification_runs"].append(extra_run)
    extra_binding = deepcopy(documents["commands.json"]["run_bindings"][0])
    extra_binding.update(
        run_id=extra_run["id"],
        inventory_run_sha256=independent_run_fingerprint(extra_run),
    )
    extra_binding["reference"]["commit"] = int("1" * 40)
    documents["commands.json"]["run_bindings"].append(extra_binding)
    # This known extra run has no child or fixture link. Its fingerprint and
    # report bytes agree, but a numeric scalar is not an immutable commit pin.
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, documents, inventory=inventory)


def test_fixture_and_child_structured_source_members_agree_while_scope_text_can_differ(
    tmp_path,
):
    root, documents, inventory = owned_graph(tmp_path)
    child_source = documents["commands.json"]["requirements"][0]["reference"]
    fixture_source = documents["fixtures.json"]["fixtures"][0]["reference"]
    child_source["scope"] += " Owned setting-specific limits."
    child_source["owned_annotation"] = {"limits": ["UI remains pending"]}
    extra_member = {"path": "owned/Helpers.java", "sha256": "f" * 64}
    child_source["source_members"].append(deepcopy(extra_member))
    fixture_source["source_members"].insert(0, deepcopy(extra_member))
    child_source["source_members"][0]["owned_annotation"] = "Retain source note."
    seeded = evidence.seed_bindings(root, documents, inventory=inventory)
    evidence.validate_evidence(root, seeded, inventory=inventory, require_reports=True)
    retained_source = seeded["commands.json"]["requirements"][0]["reference"]
    assert retained_source["owned_annotation"] == {"limits": ["UI remains pending"]}
    assert (
        retained_source["source_members"][0]["owned_annotation"]
        == "Retain source note."
    )
    seeded["commands.json"]["requirements"][0]["reference"]["source_members"][0][
        "sha256"
    ] = "f" * 64
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, seeded, inventory=inventory)


def test_known_bound_run_for_other_fixture_cannot_support_childs_declared_fixture(
    tmp_path,
):
    root, documents, inventory = owned_graph(tmp_path)
    other_child = deepcopy(documents["commands.json"]["requirements"][0])
    other_child.update(
        id="G052.owned-other-fixture",
        fixture_ids=["F.owned-other"],
        verification_run_ids=["owned-other-run"],
    )
    other_fixture = deepcopy(documents["fixtures.json"]["fixtures"][0])
    other_fixture.update(
        id="F.owned-other",
        requirement_ids=[other_child["id"]],
        path="tests/fixtures/owned-other.json",
        verification_run_ids=["owned-other-run"],
    )
    other_payload = b'{"owned_cases":[{"input":1,"output":1},{"input":2,"output":2}]}\n'
    (root / other_fixture["path"]).write_bytes(other_payload)
    other_fixture["sha256"] = digest(other_payload)
    other_run = deepcopy(inventory["verification_runs"][0])
    other_run["id"] = "owned-other-run"
    other_run["upstream_reference"].update(
        fixture=other_fixture["path"],
        fixture_sha256=other_fixture["sha256"],
    )
    documents["commands.json"]["requirements"].append(other_child)
    documents["fixtures.json"]["fixtures"].append(other_fixture)
    inventory["verification_runs"].append(other_run)
    seeded = evidence.seed_bindings(root, documents, inventory=inventory)
    assert (
        evidence.validate_evidence(
            root, seeded, inventory=inventory, require_reports=True
        )["bindings"]
        == 2
    )
    # Both runs, fixtures and source commits are valid. The first child declares
    # fixture A but cites run B, whose upstream tuple identifies only fixture B.
    seeded["commands.json"]["requirements"][0]["verification_run_ids"] = [
        "owned-other-run"
    ]
    with pytest.raises(ValueError):
        evidence.validate_evidence(root, seeded, inventory=inventory)


def test_nonreference_run_has_null_reference_and_does_not_invent_processor_coverage(
    tmp_path,
):
    root, documents, inventory = owned_graph(tmp_path)
    del inventory["verification_runs"][0]["upstream_reference"]
    documents["commands.json"]["requirements"][0]["fixture_ids"] = []
    documents["fixtures.json"]["fixtures"] = []
    seeded = evidence.seed_bindings(root, documents, inventory=inventory)
    assert seeded["commands.json"]["run_bindings"][0]["reference"] is None
    evidence.validate_evidence(root, seeded, inventory=inventory, require_reports=True)


def test_seeding_is_additive_idempotent_and_preserves_human_metadata_without_writes(
    tmp_path,
):
    root, documents, inventory = owned_graph(tmp_path)
    retained = deepcopy(documents)
    before = bytes_under(root)
    seeded = evidence.seed_bindings(root, documents, inventory=inventory)
    assert documents == retained
    for name, original in retained.items():
        if name != "commands.json":
            assert seeded[name] == original
        else:
            for key, value in original.items():
                assert seeded[name][key] == value
    seeded["commands.json"]["run_bindings"][0]["owned_human_note"] = {
        "retain": "review"
    }
    repeated = evidence.seed_bindings(root, seeded, inventory=inventory)
    assert repeated == seeded
    assert bytes_under(root) == before


def test_existing_binding_identity_changes_reject_seeding_without_mutating_inputs(
    tmp_path,
):
    root, documents, inventory = seeded_graph(tmp_path)
    retained = deepcopy(documents)
    (root / PATHS["native"]).write_bytes(
        NATIVE_REPORT + b"owned harmless report drift\n"
    )
    before = bytes_under(root)
    with pytest.raises(ValueError):
        evidence.seed_bindings(root, documents, inventory=inventory)
    assert documents == retained
    assert bytes_under(root) == before


def test_write_bindings_changes_only_commands_and_repeated_seed_is_noop(tmp_path):
    root, documents, inventory = seeded_graph(tmp_path)
    before = bytes_under(root)
    assert evidence.write_bindings(root, documents, inventory=inventory) is True
    after = bytes_under(root)
    assert {name for name in before if before[name] != after[name]} == {
        "docs/parity/commands.json"
    }
    persisted = deepcopy(documents)
    persisted["commands.json"] = json.loads(
        (root / "docs/parity/commands.json").read_bytes()
    )
    assert evidence.write_bindings(root, persisted, inventory=inventory) is False
    assert (
        evidence.write_bindings(root, persisted, inventory=inventory, check=True)
        is False
    )
    assert bytes_under(root) == after


def test_check_mode_reports_required_binding_addition_without_writing(tmp_path):
    root, documents, inventory = seeded_graph(tmp_path)
    before = bytes_under(root)
    with pytest.raises(ValueError):
        evidence.write_bindings(root, documents, inventory=inventory, check=True)
    assert bytes_under(root) == before


def test_valid_new_report_digest_cannot_replace_previously_written_binding(tmp_path):
    root, documents, inventory = seeded_graph(tmp_path)
    assert evidence.write_bindings(root, documents, inventory=inventory) is True
    proposed = deepcopy(documents)
    rewrite_bound_report(
        root, proposed, "native", NATIVE_REPORT + b"Owned report annotation.\n"
    )
    # Current bytes and parsed totals are valid; persisted run/report identity
    # still cannot be replaced under the same accepted run ID.
    evidence.validate_evidence(
        root, proposed, inventory=inventory, require_reports=True
    )
    before = bytes_under(root)
    with pytest.raises(ValueError):
        evidence.write_bindings(root, proposed, inventory=inventory)
    assert bytes_under(root) == before


@pytest.mark.parametrize("change", ["delete_null", "integer_to_boolean"])
def test_retained_unknown_metadata_presence_and_exact_types_cannot_change_on_write(
    tmp_path,
    change,
):
    root, documents, inventory = seeded_graph(tmp_path)
    documents["commands.json"].update(owned_null_metadata=None, owned_number_metadata=1)
    commands_file = root / "docs/parity/commands.json"
    commands_file.write_text(
        json.dumps(documents["commands.json"], indent=2) + "\n", encoding="utf-8"
    )
    proposed = deepcopy(documents)
    if change == "delete_null":
        del proposed["commands.json"]["owned_null_metadata"]
    else:
        proposed["commands.json"]["owned_number_metadata"] = True
    evidence.validate_evidence(
        root, proposed, inventory=inventory, require_reports=True
    )
    before = bytes_under(root)
    with pytest.raises(ValueError):
        evidence.write_bindings(root, proposed, inventory=inventory)
    assert bytes_under(root) == before


@pytest.mark.parametrize(
    "failure",
    ["invalid_graph", "binding_drift", "accepted_fact_drift", "missing_report"],
)
def test_invalid_or_changed_bound_identity_cannot_write_any_workspace_bytes(
    tmp_path, failure
):
    root, documents, inventory = seeded_graph(tmp_path)
    commands_file = root / "docs/parity/commands.json"
    commands_file.write_text(
        json.dumps(documents["commands.json"], indent=2) + "\n", encoding="utf-8"
    )
    if failure == "invalid_graph":
        documents["commands.json"]["requirements"][0]["milestones"] = ["M0"]
    elif failure == "binding_drift":
        documents["commands.json"]["run_bindings"][0]["inventory_run_sha256"] = "0" * 64
    elif failure == "accepted_fact_drift":
        inventory["verification_runs"][0]["owned_metadata"]["retain"] = (
            "changed accepted fact"
        )
    else:
        (root / PATHS["python"]).unlink()
    before = bytes_under(root)
    with pytest.raises(ValueError):
        evidence.write_bindings(root, documents, inventory=inventory)
    assert bytes_under(root) == before


def test_clean_source_checker_reports_missing_historical_bytes_without_certifying_them(
    tmp_path,
    monkeypatch,
):
    _, documents, inventory = seeded_graph(tmp_path)
    component = documents["fiji-components.json"]["files"][0]
    component.update(
        sha256="f" * 64,
        bytes=0,
        commercial_review={
            "decision": "pending",
            "incorporation": "blocked_until_review",
            "code": "pending",
            "dependencies": "pending",
            "models_assets": "pending",
            "evidence": [],
            "reason": "Owned synthetic review remains pending.",
        },
    )
    documents["commands.json"]["candidates"][0].update(
        status="candidate",
        assignment=None,
        option_inventory=None,
        runtime_reconciliation="pending",
    )
    for name, collection in (
        ("formats.json", "formats"),
        ("benchmarks.json", "benchmarks"),
        ("platforms.json", "platforms"),
    ):
        documents[name] = {
            "schema_version": 1,
            "reference_id": BASELINE,
            "enumeration_status": "pending",
            collection: [],
            "remaining": ["Owned synthetic enumeration remains pending."],
        }
    clean_root = tmp_path / "clean-source"
    parity_directory = clean_root / "docs/parity"
    parity_directory.mkdir(parents=True)
    for name, value in {**documents, "inventory.json": inventory}.items():
        (parity_directory / name).write_text(
            json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    before = bytes_under(clean_root)
    assert not (clean_root / "build").exists()
    assert all(name.startswith("docs/parity/") for name in before)

    checker_spec = importlib.util.spec_from_file_location(
        "owned_clean_source_checker_tests", ROOT / "scripts/check_plan.py"
    )
    checker = importlib.util.module_from_spec(checker_spec)
    checker_spec.loader.exec_module(checker)
    monkeypatch.setattr(checker, "ROOT", clean_root)
    result = checker.validate_references()
    assert result["missing"] == []
    assert result["components_pending"] == result["static_candidates"] == 1
    assert result["reports_verified"] == 0
    assert len(result["unavailable_reports"]) == 3
    assert {report["role"] for report in result["unavailable_reports"]} == {
        "native",
        "python",
        "sdk",
    }
    with pytest.raises(
        ValueError, match="commercial/runtime/option review is incomplete"
    ):
        checker.validate_references(require_complete=True)
    with pytest.raises(ValueError):
        evidence.validate_evidence(clean_root, documents, require_reports=True)
    assert bytes_under(clean_root) == before
    assert not (clean_root / "build").exists()


def test_unreadable_historical_report_has_no_credit_and_cannot_seed(tmp_path,monkeypatch):
    root,documents,inventory=seeded_graph(tmp_path)
    target=root/PATHS["native"];original=Path.read_bytes
    def denied(self,*args,**kwargs):
        if self==target:raise PermissionError("owned denied evidence")
        return original(self,*args,**kwargs)
    monkeypatch.setattr(Path,"read_bytes",denied)
    result=evidence.validate_evidence(root,documents,inventory=inventory)
    assert result["reports_verified"]==2 and result["unavailable_reports"][0]["path"]==PATHS["native"]
    with pytest.raises(ValueError):evidence.validate_evidence(root,documents,inventory=inventory,require_reports=True)
    unseeded=deepcopy(documents);unseeded["commands.json"]["run_bindings"]=[]
    with pytest.raises((PermissionError,ValueError)):evidence.seed_bindings(root,unseeded,inventory=inventory)


def test_unexpected_io_failure_is_not_silently_treated_as_missing(tmp_path,monkeypatch):
    target=tmp_path/"evidence";target.write_bytes(b"actual")
    def broken(self,*args,**kwargs):raise OSError("owned unexpected I/O failure")
    monkeypatch.setattr(Path,"read_bytes",broken)
    with pytest.raises(OSError,match="unexpected I/O"):evidence.historical_bytes(target)


def test_unreadable_resolution_never_reads_an_unvalidated_path(tmp_path,monkeypatch):
    target=tmp_path/PATHS["native"];original=Path.resolve
    def denied(self,*args,**kwargs):
        if self==target:raise PermissionError("owned denied resolution")
        return original(self,*args,**kwargs)
    reads=[]
    def forbidden_read(self,*args,**kwargs):reads.append(self);raise AssertionError("Unvalidated path read")
    monkeypatch.setattr(Path,"resolve",denied);monkeypatch.setattr(Path,"read_bytes",forbidden_read)
    assert evidence.historical_evidence_bytes(tmp_path,PATHS["native"],"owned")==None and reads==[]
    with pytest.raises(ValueError):evidence.historical_evidence_bytes(tmp_path,"../escape","owned")
