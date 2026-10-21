"""Owned synthetic contracts for additive, partial parity manifests."""

from copy import deepcopy
import importlib.util
import json
import math
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "owned_parity_schema_tests", ROOT / "scripts" / "parity_schema.py"
)
schema = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(schema)
DISCOVERY_SPEC = importlib.util.spec_from_file_location(
    "owned_reference_regeneration_tests", ROOT / "scripts" / "reference_inventory.py"
)
discovery = importlib.util.module_from_spec(DISCOVERY_SPEC)
DISCOVERY_SPEC.loader.exec_module(discovery)

REFERENCE_ID = "owned-synthetic-parity"
COMPONENT_ID = "fiji:jars/owned-editor.jar"
CHILD_ID = "G052.owned-binary.neighbors"
FIXTURE_ID = "F.owned-binary"


def legacy_documents():
    return {
        "references.json": {
            "schema_version": 1,
            "reference_id": REFERENCE_ID,
            "imagej": {"commit": "a" * 40, "sha256": "b" * 64},
            "fiji": {"sha256": "c" * 64},
            "owned_note": {"retain": ["unknown reference metadata"]},
        },
        "fiji-components.json": {
            "schema_version": 1,
            "reference_id": REFERENCE_ID,
            "archive_sha256": "c" * 64,
            "files": [
                {
                    "id": COMPONENT_ID,
                    "commercial_review": "pending",
                    "resources": [{"path": "owned.txt", "decision": "pending"}],
                    "owned_note": "preserve component metadata",
                }
            ],
        },
        "commands.json": {
            "schema_version": 1,
            "reference_id": REFERENCE_ID,
            "status": "partial",
            "candidates": [
                {
                    "id": "R-owned-editor",
                    "component_id": COMPONENT_ID,
                    "commercial_review": "pending",
                    "evidence": {"archive_member": "Fiji/jars/owned-editor.jar"},
                    "owned_note": {"unknown": [1, "retain"]},
                }
            ],
            "owned_note": "retain discovery envelope",
        },
    }


def pinned_reference(component_id="imagej-core"):
    return {
        "reference_id": REFERENCE_ID,
        "component_id": component_id,
        "archive_sha256": ("b" if component_id == "imagej-core" else "c") * 64,
        "revision": "a" * 40 if component_id == "imagej-core" else "owned-release-1",
        "scope": "Owned synthetic processor contract; no plugin/UI certification.",
        "source_members": [{"path": "owned/Processor.java", "sha256": "d" * 64}],
    }


def requirement():
    return {
        "id": CHILD_ID,
        "parent_id": "G052",
        "milestones": ["M0", "M2"],
        "kind": "option",
        "title": "Owned neighbor count",
        "required_work": "Enumerate and verify the processor and UI contracts.",
        "candidate_ids": ["R-owned-editor"],
        "reference": pinned_reference(),
        "contract": {
            "parameters": [
                {
                    "name": "neighbors",
                    "type": "integer",
                    "default": 1,
                    "units": "",
                    "minimum": 1,
                    "maximum": 8,
                    "enabled_when": "always",
                }
            ],
            "pixel_types": ["u8"],
            "samples": ["grayscale"],
            "axes": ["XY"],
            "targets": ["whole_plane"],
            "roi": ["whole_plane_only"],
            "border": ["constant_background"],
            "nonfinite": "Not applicable to unsigned integer input.",
            "geometry": "Preserves shape and axes.",
        },
        "interfaces": {
            name: {
                "applicable": True,
                "entry_points": [f"owned_{name}_neighbors"],
                "evidence": [],
            }
            for name in ("cpp", "python", "imgui")
        },
        "settings_consumers": [
            {"key": "owned_neighbors", "consumer": "owned processor options"}
        ],
        "fixture_ids": [FIXTURE_ID],
        "verification_run_ids": ["owned-run-1"],
        "implementation_status": "partial",
        "verification_status": "partial",
        "commercial": {
            "decision": "pending",
            "evidence": [],
            "reason": "Owned equivalent does not approve upstream incorporation.",
        },
        "provenance": {
            "implementation": "owned_native_equivalent",
            "incorporated_components": [],
        },
        "remaining": ["Runtime option enumeration and interface evidence."],
    }


def fixture_record():
    return {
        "id": FIXTURE_ID,
        "requirement_ids": [CHILD_ID],
        "path": "tests/fixtures/owned-processor.json",
        "sha256": "e" * 64,
        "case_count": 2,
        "reference": pinned_reference(),
        "generator": {
            "path": "scripts/owned_capture.py",
            "sha256": "f" * 64,
            "driver_sha256": "1" * 64,
        },
        "scope": "Owned synthetic two-case capture; structure only in these tests.",
        "comparison": {"method": "exact", "atol": 0, "rtol": 0},
        "capture_status": "captured",
        "match_status": "matched",
        "verification_run_ids": ["owned-run-1"],
        "license": {
            "decision": "pending",
            "reason": "Fixture licensing review remains separate.",
            "evidence": [],
        },
        "remaining": ["Broader fixture enumeration and licensing evidence."],
    }


def addition():
    return {
        "id": "A.owned-extension",
        "title": "Owned native extension",
        "required_work": "Verify the extension without narrowing existing families.",
        "milestones": ["M2"],
        "remaining": ["Extension interface verification."],
    }


def full_documents():
    documents = schema.migrate_documents(
        legacy_documents(), requirements=[requirement()], fixtures=[fixture_record()]
    )
    documents["formats.json"]["formats"] = [
        {
            "id": "IO.owned",
            "parent_id": "G052",
            "name": "Owned format",
            "variant": "Owned scalar data",
            "directions": ["read", "write"],
            "axes": ["XY"],
            "metadata": ["pixel type"],
            "dependency_ids": [],
            "fixture_ids": [FIXTURE_ID],
            "status": "partial",
            "remaining": ["Broader format enumeration."],
        }
    ]
    documents["benchmarks.json"]["benchmarks"] = [
        {
            "id": "B.owned",
            "requirement_ids": [CHILD_ID],
            "workload": "Owned small XY morphology workload",
            "fixture_ids": [FIXTURE_ID],
            "hardware": "Owned synthetic hardware description",
            "build": "Owned synthetic C++17 release configuration",
            "metrics": [
                {
                    "name": "latency",
                    "unit": "ms",
                    "budget": 5,
                    "budget_status": "proposed",
                }
            ],
            "verification_run_ids": [],
            "status": "open",
            "remaining": ["Ratify a measured budget."],
        }
    ]
    documents["platforms.json"]["platforms"] = [
        {
            "id": "P.owned-windows",
            "os": "Windows",
            "architecture": "x64",
            "compiler": "Owned synthetic compiler configuration",
            "python_abis": ["cp312"],
            "products": ["cpp_core", "python", "imgui"],
            "graphics": "Owned synthetic OpenGL configuration",
            "checks": ["installed_sdk"],
            "verification_run_ids": [],
            "status": "open",
            "remaining": ["Actual installed platform verification."],
        }
    ]
    for name in ("formats.json", "benchmarks.json", "platforms.json"):
        documents[name]["enumeration_status"] = "partial"
    return documents


def replace_at(document, keys, value):
    owner = document
    for key in keys[:-1]:
        owner = owner[key]
    owner[keys[-1]] = value


def file_bytes(directory):
    return {
        path.name: path.read_bytes() for path in directory.iterdir() if path.is_file()
    }


def discovery_documents():
    documents = legacy_documents()
    component = documents["fiji-components.json"]["files"][0]
    component["commercial_review"] = {"decision": "pending", "evidence": []}
    component["resources"][0].update(license_review="pending", license_evidence=[])
    candidate = documents["commands.json"]["candidates"][0]
    candidate.update(
        status="candidate",
        assignment=None,
        option_inventory=None,
        runtime_reconciliation="pending",
    )
    return documents


def test_legacy_partial_validation_does_not_invent_requirements_or_change_inputs():
    documents = legacy_documents()
    retained = deepcopy(documents)
    assert schema.validate_documents(documents) == {"requirements": 0, "additions": 0}
    assert documents == retained
    assert "requirements" not in documents["commands.json"]


def test_all_four_manifests_are_valid_partial_structures_not_completion():
    documents = full_documents()
    assert schema.validate_documents(documents) == {
        "requirements": 1,
        "additions": 0,
        "formats": 1,
        "fixtures": 1,
        "benchmarks": 1,
        "platforms": 1,
    }
    assert documents["commands.json"]["enumeration_status"] == "partial"
    child = documents["commands.json"]["requirements"][0]
    assert child["implementation_status"] == child["verification_status"] == "partial"
    assert child["commercial"]["decision"] == "pending"
    assert (
        documents["fiji-components.json"]["files"][0]["commercial_review"] == "pending"
    )


@pytest.mark.parametrize("version", [True, False, 0, 2, 1.0, "1", None])
@pytest.mark.parametrize(
    "path",
    [
        (name, "schema_version")
        for name in schema.REFERENCE_FILES + tuple(schema.COLLECTIONS)
    ]
    + [("commands.json", "requirements_schema_version")],
)
def test_schema_versions_require_supported_integers(path, version):
    documents = full_documents()
    replace_at(documents, path, version)
    with pytest.raises(ValueError, match="unsupported schema version"):
        schema.validate_documents(documents)


@pytest.mark.parametrize(
    "collection",
    [
        ("commands.json", "candidates"),
        ("commands.json", "requirements"),
        ("fiji-components.json", "files"),
        *schema.COLLECTIONS.items(),
    ],
)
def test_duplicate_record_identity_is_rejected(collection):
    documents = full_documents()
    records = documents[collection[0]][collection[1]]
    records.append(deepcopy(records[0]))
    with pytest.raises(ValueError, match="duplicate stable ID"):
        schema.validate_documents(documents)


@pytest.mark.parametrize(
    "keys,value",
    [
        (("commands.json", "requirements", 0, "id"), "G052.invalid/id"),
        (("commands.json", "requirements", 0, "id"), "G007.owned-binary.neighbors"),
        (("commands.json", "requirements", 0, "parent_id"), "G100"),
        (("commands.json", "requirements", 0, "milestones"), ["M99"]),
        (("commands.json", "requirements", 0, "candidate_ids"), ["R-missing"]),
        (("commands.json", "candidates", 0, "component_id"), "fiji:jars/missing.jar"),
        (("commands.json", "candidates", 0, "evidence"), {}),
        (("references.json", "imagej", "commit"), "moving-branch"),
        (("references.json", "fiji", "sha256"), "short"),
        (("fiji-components.json", "archive_sha256"), "9" * 64),
        (("commands.json", "requirements", 0, "reference", "reference_id"), "other"),
        (("commands.json", "requirements", 0, "reference", "component_id"), "missing"),
        (("commands.json", "requirements", 0, "reference", "archive_sha256"), "9" * 64),
        (("commands.json", "requirements", 0, "reference", "revision"), "0" * 40),
        (("commands.json", "requirements", 0, "reference", "source_members"), []),
        (
            (
                "commands.json",
                "requirements",
                0,
                "reference",
                "source_members",
                0,
                "sha256",
            ),
            "D" * 64,
        ),
        (
            (
                "commands.json",
                "requirements",
                0,
                "reference",
                "source_members",
                0,
                "path",
            ),
            "../owned.java",
        ),
        (("platforms.json", "reference_id"), "other"),
    ],
)
def test_orphaned_malformed_or_moving_identities_are_rejected(keys, value):
    documents = full_documents()
    replace_at(documents, keys, value)
    with pytest.raises(ValueError):
        schema.validate_documents(documents)


def test_source_member_identity_is_unique():
    documents = full_documents()
    members = documents["commands.json"]["requirements"][0]["reference"][
        "source_members"
    ]
    members.append(deepcopy(members[0]))
    with pytest.raises(ValueError, match="duplicate source member"):
        schema.validate_documents(documents)


def test_candidate_less_processor_requirement_needs_an_explicit_scope_reason():
    documents = full_documents()
    child = documents["commands.json"]["requirements"][0]
    child["candidate_ids"] = []
    with pytest.raises(ValueError):
        schema.validate_documents(documents)
    child["candidate_link_reason"] = (
        "Direct processor source; runtime candidate enumeration pending."
    )
    schema.validate_documents(documents)


def test_added_capability_is_an_explicit_parent_without_replacing_family_ids():
    documents = legacy_documents()
    child = requirement()
    child.update(id="A.owned-extension.option", parent_id="A.owned-extension")
    migrated = schema.migrate_documents(
        documents, requirements=[child], additions=[addition()]
    )
    assert schema.validate_documents(migrated)["additions"] == 1
    assert (
        migrated["commands.json"]["requirements"][0]["parent_id"] == "A.owned-extension"
    )
    assert documents == legacy_documents()
    migrated["commands.json"]["added_capabilities"] = []
    with pytest.raises(ValueError, match="orphaned or mismatched parent"):
        schema.validate_documents(migrated)


@pytest.mark.parametrize(
    "parameter",
    [
        {"name": "flag", "type": "boolean", "default": True, "choices": [True, False]},
        {"name": "count", "type": "integer", "default": 2, "minimum": 1, "maximum": 3},
        {
            "name": "weight",
            "type": "number",
            "default": 0.5,
            "minimum": 0,
            "maximum": 1,
        },
        {"name": "mode", "type": "string", "default": "", "choices": ["", "owned"]},
    ],
)
def test_supported_scalar_controls_accept_correct_defaults_and_constraints(parameter):
    documents = full_documents()
    parameter = dict(parameter, units="", enabled_when="always")
    documents["commands.json"]["requirements"][0]["contract"]["parameters"] = [
        parameter
    ]
    schema.validate_documents(documents)


@pytest.mark.parametrize(
    "patch",
    [
        {"type": "boolean", "default": 1},
        {"type": "integer", "default": True},
        {"type": "integer", "default": 1.0},
        {"type": "number", "default": float("nan")},
        {"type": "number", "default": float("inf")},
        {"type": "number", "default": True},
        {"type": "string", "default": 1},
        {"type": "array", "default": []},
        {"default": 0},
        {"default": 9},
        {"minimum": 9},
        {"maximum": 0},
        {"minimum": 1.0},
        {"choices": []},
        {"choices": [1, 1]},
        {"choices": [2, 3]},
        {"choices": [1, True]},
        {"units": None},
        {"enabled_when": ""},
    ],
)
def test_invalid_scalar_controls_are_rejected(patch):
    documents = full_documents()
    control = documents["commands.json"]["requirements"][0]["contract"]["parameters"][0]
    control.update(patch)
    with pytest.raises(ValueError):
        schema.validate_documents(documents)


def test_duplicate_controls_and_missing_defaults_are_rejected():
    documents = full_documents()
    controls = documents["commands.json"]["requirements"][0]["contract"]["parameters"]
    controls.append(deepcopy(controls[0]))
    with pytest.raises(ValueError, match="duplicate parameter"):
        schema.validate_documents(documents)
    controls.pop()
    del controls[0]["default"]
    with pytest.raises(ValueError, match="missing default"):
        schema.validate_documents(documents)


@pytest.mark.parametrize(
    "keys,value",
    [
        (("kind",), "family"),
        (("implementation_status",), "done"),
        (("verification_status",), "complete"),
        (("commercial", "decision"), "approved"),
        (("commercial", "decision"), "eligible"),
        (("provenance", "implementation"), "downloaded"),
        (("provenance", "incorporated_components"), ["missing"]),
        (("interfaces", "cpp", "applicable"), 1),
        (("interfaces", "cpp", "entry_points"), []),
        (("interfaces", "imgui", "applicable"), False),
        (("contract", "axes"), []),
        (("contract", "nonfinite"), ""),
        (("settings_consumers", 0, "consumer"), ""),
    ],
)
def test_interface_contract_and_status_structure_is_explicit(keys, value):
    documents = full_documents()
    child = documents["commands.json"]["requirements"][0]
    replace_at(child, keys, value)
    with pytest.raises(ValueError):
        schema.validate_documents(documents)


def test_inapplicable_interface_requires_a_reason_and_statuses_remain_independent():
    documents = full_documents()
    child = documents["commands.json"]["requirements"][0]
    child["interfaces"]["imgui"] = {
        "applicable": False,
        "entry_points": [],
        "evidence": [],
        "inapplicable_reason": "Owned headless extension has no interactive control.",
    }
    child["implementation_status"] = "complete"
    child["verification_status"] = "unverified"
    schema.validate_documents(documents)
    assert child["commercial"]["decision"] == "pending"


@pytest.mark.parametrize(
    "keys,value",
    [
        (("formats.json", "formats", 0, "parent_id"), "G100"),
        (("formats.json", "formats", 0, "directions"), []),
        (("formats.json", "formats", 0, "directions"), ["execute"]),
        (("formats.json", "formats", 0, "status"), "captured"),
        (("fixtures.json", "fixtures", 0, "case_count"), True),
        (("fixtures.json", "fixtures", 0, "case_count"), 0),
        (("fixtures.json", "fixtures", 0, "case_count"), 2.0),
        (("fixtures.json", "fixtures", 0, "requirement_ids"), []),
        (("fixtures.json", "fixtures", 0, "path"), "C:/fixture.json"),
        (("fixtures.json", "fixtures", 0, "generator", "path"), "owned\\capture.py"),
        (("fixtures.json", "fixtures", 0, "generator", "sha256"), "short"),
        (("fixtures.json", "fixtures", 0, "comparison", "atol"), -1),
        (("fixtures.json", "fixtures", 0, "comparison", "rtol"), float("nan")),
        (("fixtures.json", "fixtures", 0, "comparison", "atol"), 0.1),
        (("fixtures.json", "fixtures", 0, "capture_status"), "pending"),
        (("fixtures.json", "fixtures", 0, "match_status"), "verified"),
        (("fixtures.json", "fixtures", 0, "license", "decision"), "eligible"),
        (("benchmarks.json", "benchmarks", 0, "metrics"), []),
        (("benchmarks.json", "benchmarks", 0, "metrics", 0, "budget"), -1),
        (("benchmarks.json", "benchmarks", 0, "metrics", 0, "budget"), float("inf")),
        (("benchmarks.json", "benchmarks", 0, "metrics", 0, "budget_status"), "passed"),
        (("platforms.json", "platforms", 0, "products"), []),
        (("platforms.json", "platforms", 0, "compiler"), ""),
        (("platforms.json", "platforms", 0, "python_abis"), "cp312"),
        (("platforms.json", "enumeration_status"), "complete"),
    ],
)
def test_malformed_new_manifest_records_are_rejected(keys, value):
    documents = full_documents()
    replace_at(documents, keys, value)
    with pytest.raises(ValueError):
        schema.validate_documents(documents)


def test_migration_is_lossless_idempotent_and_deterministic_with_reviewed_metadata(
    tmp_path,
):
    legacy = legacy_documents()
    retained = deepcopy(legacy)
    migrated = schema.migrate_documents(
        legacy, requirements=[requirement()], fixtures=[fixture_record()]
    )
    assert legacy == retained
    for name in ("references.json", "fiji-components.json"):
        assert migrated[name] == retained[name]
    for key, value in retained["commands.json"].items():
        assert migrated["commands.json"][key] == value

    child = migrated["commands.json"]["requirements"][0]
    child["commercial"] = {
        "decision": "eligible",
        "reason": "Owned human review",
        "evidence": ["owned-review-1"],
    }
    child["owned_review_metadata"] = {"decision": "retain", "values": [1, 2]}
    corpus = migrated["fixtures.json"]["fixtures"][0]
    corpus["owned_review_metadata"] = "retain fixture review"
    migrated["fixtures.json"]["owned_manifest_metadata"] = ["retain envelope"]
    repeated = schema.migrate_documents(
        migrated, requirements=[requirement()], fixtures=[fixture_record()]
    )
    assert repeated == migrated
    assert schema.migrate_documents(repeated) == repeated
    assert (
        repeated["commands.json"]["candidates"]
        == retained["commands.json"]["candidates"]
    )
    assert (
        repeated["fiji-components.json"]["files"]
        == retained["fiji-components.json"]["files"]
    )

    first, second = tmp_path / "first", tmp_path / "second"
    schema.write_documents(first, repeated)
    schema.write_documents(second, deepcopy(repeated))
    assert file_bytes(first) == file_bytes(second)
    assert schema.write_documents(first, repeated) == []
    round_trip = {
        name: json.loads(payload) for name, payload in file_bytes(first).items()
    }
    assert round_trip == repeated


def test_seed_order_does_not_change_migrated_bytes_and_children_keep_stable_ids():
    first = requirement()
    second = deepcopy(first)
    second["id"] = "G052.owned-binary.iterations"
    second["contract"]["parameters"][0]["name"] = "iterations"
    left = schema.migrate_documents(legacy_documents(), requirements=[first, second])
    right = schema.migrate_documents(legacy_documents(), requirements=[second, first])
    assert left == right
    assert schema.encoded(left["commands.json"]) == schema.encoded(
        right["commands.json"]
    )
    assert {row["id"] for row in left["commands.json"]["requirements"]} == {
        first["id"],
        second["id"],
    }


@pytest.mark.parametrize("seed_kind", ["requirements", "fixtures", "additions"])
def test_invalid_seed_is_rejected_even_when_its_id_already_exists(seed_kind):
    documents = schema.migrate_documents(
        legacy_documents(),
        requirements=[requirement()],
        fixtures=[fixture_record()],
        additions=[addition()],
    )
    retained = deepcopy(documents)
    if seed_kind == "requirements":
        incoming = requirement()
        incoming["contract"]["parameters"][0]["default"] = 99
    elif seed_kind == "fixtures":
        incoming = fixture_record()
        incoming["case_count"] = 0
    else:
        incoming = addition()
        incoming["milestones"] = ["M99"]
    with pytest.raises(ValueError):
        schema.migrate_documents(documents, **{seed_kind: [incoming]})
    assert documents == retained


@pytest.mark.parametrize("change", ["revision", "source_member"])
def test_existing_seed_cannot_replace_valid_stable_reference_identity(change):
    child = requirement()
    child["reference"] = pinned_reference(COMPONENT_ID)
    documents = schema.migrate_documents(legacy_documents(), requirements=[child])
    incoming = deepcopy(child)
    if change == "revision":
        incoming["reference"]["revision"] = "owned-release-2"
    else:
        incoming["reference"]["source_members"][0]["sha256"] = "2" * 64
    # Each source identity is structurally valid in isolation; a retained child
    # cannot silently switch identity on a later seed operation.
    schema.migrate_documents(legacy_documents(), requirements=[incoming])
    retained = deepcopy(documents)
    with pytest.raises(ValueError, match="stable .*reference identity"):
        schema.migrate_documents(documents, requirements=[incoming])
    assert documents == retained


def test_choices_must_respect_numeric_bounds_even_when_default_is_valid():
    documents = full_documents()
    control = documents["commands.json"]["requirements"][0]["contract"]["parameters"][0]
    control.update(default=1, minimum=0, maximum=2, choices=[1, 3])
    with pytest.raises(ValueError):
        schema.validate_documents(documents)


def test_legacy_nonfinite_discovery_fact_survives_migration_and_encoded_round_trip():
    legacy = legacy_documents()
    legacy["commands.json"]["candidates"][0]["descriptor"] = {
        "values": {"priority": math.inf}
    }
    retained = deepcopy(legacy["commands.json"]["candidates"])
    migrated = schema.migrate_documents(legacy, requirements=[requirement()])
    assert migrated["commands.json"]["candidates"] == retained
    decoded = json.loads(schema.encoded(migrated["commands.json"]))
    assert decoded["candidates"] == retained
    assert decoded["candidates"][0]["descriptor"]["values"]["priority"] == math.inf


def test_incomplete_requirements_extension_is_rejected_without_guessing_defaults():
    documents = legacy_documents()
    documents["commands.json"]["requirements_schema_version"] = 1
    with pytest.raises(ValueError, match="incomplete requirements extension"):
        schema.validate_documents(documents)


def test_check_mode_preserves_nonmatching_bytes_and_does_not_create_missing_directory(
    tmp_path,
):
    documents = full_documents()
    directory = tmp_path / "existing"
    schema.write_documents(directory, documents)
    retained = file_bytes(directory)
    changed = deepcopy(documents)
    changed["commands.json"]["owned_note"] = "new proposed metadata"
    with pytest.raises(ValueError, match="manifests need migration"):
        schema.write_documents(directory, changed, check=True)
    assert file_bytes(directory) == retained
    assert schema.write_documents(directory, documents, check=True) == []
    assert file_bytes(directory) == retained
    missing = tmp_path / "not-created"
    with pytest.raises(ValueError, match="manifests need migration"):
        schema.write_documents(missing, documents, check=True)
    assert not missing.exists()


@pytest.mark.parametrize(
    "failure", ["invalid_contract", "invalid_encoding", "unknown_manifest"]
)
def test_invalid_write_stages_nothing_and_preserves_every_existing_byte(
    tmp_path, failure
):
    documents = full_documents()
    schema.write_documents(tmp_path, documents)
    retained = file_bytes(tmp_path)
    invalid = deepcopy(documents)
    invalid["references.json"]["owned_note"] = (
        "valid earlier change must not be written"
    )
    if failure == "invalid_contract":
        invalid["commands.json"]["requirements"][0]["contract"]["parameters"][0][
            "default"
        ] = 99
    elif failure == "invalid_encoding":
        invalid["platforms.json"]["owned_unknown_value"] = "\ud800"
    else:
        invalid["not-a-manifest.json"] = {"owned": True}
    with pytest.raises(ValueError):
        schema.write_documents(tmp_path, invalid)
    assert file_bytes(tmp_path) == retained
    assert not list(tmp_path.glob(".parity-*"))


def test_invalid_write_does_not_create_a_destination_directory(tmp_path):
    documents = full_documents()
    documents["fixtures.json"]["fixtures"][0]["case_count"] = 0
    destination = tmp_path / "not-created"
    with pytest.raises(ValueError):
        schema.write_documents(destination, documents)
    assert not destination.exists()


@pytest.mark.parametrize(
    "enrichment", ["top_level", "nested_candidate", "nested_resource"]
)
def test_discovery_regeneration_cannot_erase_unknown_enriched_metadata(
    tmp_path, enrichment
):
    documents = discovery_documents()
    discovery.write_inventory(tmp_path, documents)
    filename = (
        "fiji-components.json" if enrichment == "nested_resource" else "commands.json"
    )
    target = tmp_path / filename
    retained = json.loads(target.read_bytes())
    if enrichment == "top_level":
        retained["owned_added_metadata"] = {"review": "retain"}
    elif enrichment == "nested_candidate":
        retained["candidates"][0]["evidence"]["owned_added_metadata"] = ["retain"]
    else:
        retained["files"][0]["resources"][0]["owned_added_metadata"] = "retain"
    target.write_bytes(discovery.encoded_manifest(retained))
    before = file_bytes(tmp_path)
    with pytest.raises(ValueError, match="explicit merge"):
        discovery.write_inventory(tmp_path, documents)
    assert file_bytes(tmp_path) == before
    assert not list(tmp_path.glob(".reference-*"))


@pytest.mark.parametrize("extension_in_discovery", [False, True])
def test_discovery_refuses_versioned_even_empty_requirements(
    tmp_path, extension_in_discovery
):
    documents = discovery_documents()
    retained = deepcopy(documents)
    retained["commands.json"].update(
        requirements_schema_version=1,
        enumeration_status="partial",
        requirements=[],
        added_capabilities=[],
        remaining=["Owned option enumeration remains."],
    )
    discovery.write_inventory(tmp_path, retained)
    before = file_bytes(tmp_path)
    incoming = retained if extension_in_discovery else documents
    with pytest.raises(ValueError, match="explicit merge"):
        discovery.write_inventory(tmp_path, incoming)
    assert file_bytes(tmp_path) == before


def test_reviewed_resource_is_preserved_while_component_review_is_still_pending(
    tmp_path,
):
    documents = discovery_documents()
    discovery.write_inventory(tmp_path, documents)
    target = tmp_path / "fiji-components.json"
    retained = json.loads(target.read_bytes())
    component = retained["files"][0]
    component["resources"][0]["license_review"] = "excluded"
    assert component["commercial_review"]["decision"] == "pending"
    target.write_bytes(discovery.encoded_manifest(retained))
    before = file_bytes(tmp_path)
    proposed = deepcopy(documents)
    proposed["references.json"]["owned_note"]["retain"] = [
        "proposed new reference metadata"
    ]
    with pytest.raises(ValueError, match="explicit merge"):
        discovery.write_inventory(tmp_path, proposed)
    assert file_bytes(tmp_path) == before


def test_component_code_review_is_preserved_before_final_commercial_decision(tmp_path):
    documents = discovery_documents()
    documents["fiji-components.json"]["files"][0]["commercial_review"]["code"] = (
        "pending"
    )
    discovery.write_inventory(tmp_path, documents)
    target = tmp_path / "fiji-components.json"
    retained = json.loads(target.read_bytes())
    review = retained["files"][0]["commercial_review"]
    review["code"] = "reviewed"
    assert review["decision"] == "pending"
    assert review["evidence"] == []
    target.write_bytes(discovery.encoded_manifest(retained))
    before = file_bytes(tmp_path)
    with pytest.raises(ValueError, match="explicit merge"):
        discovery.write_inventory(tmp_path, documents)
    assert file_bytes(tmp_path) == before
    assert not list(tmp_path.glob(".reference-*"))


def test_discovery_check_drift_is_read_only_for_all_retained_bytes(tmp_path):
    documents = discovery_documents()
    discovery.write_inventory(tmp_path, documents)
    discovery.write_inventory(tmp_path, documents, check=True)
    target = tmp_path / "commands.json"
    target.write_bytes(b"owned drift intentionally not valid JSON\n")
    before = file_bytes(tmp_path)
    with pytest.raises(ValueError, match="not reproducible"):
        discovery.write_inventory(tmp_path, documents, check=True)
    assert file_bytes(tmp_path) == before
    assert not list(tmp_path.glob(".reference-*"))
