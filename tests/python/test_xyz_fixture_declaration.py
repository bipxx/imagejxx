"""Owned scientific fixtures must never masquerade as upstream captures."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def documents():
    return {p.name: json.loads(p.read_text(encoding="utf-8"))
            for p in (ROOT / "docs/parity").glob("*.json")}


def test_owned_xyz_generator_is_reproducible_and_has_independent_canonical_pins():
    module = load_script("generate_xyz_fixture")
    actual = module.generate()
    assert actual == module.generate()
    assert actual == json.loads((ROOT / "tests/fixtures/xyz-navigation-owned.json").read_text(encoding="utf-8"))
    assert actual["reference_execution"] is False
    assert actual["source_sha256"] == "b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c"
    assert len(actual["cases"]) == 43
    assert actual["cases"][0]["expected"] == {
        "crosshair_xyz": [7, 5, 3], "landmark_value": 30507,
        "world_xyz": [4.0, -2.2, 11.0],
        "plane_sha256": {
            "XY": "00e642b096d4464a9be44cf649d867331f59532ef0aff46244ef65614f2f2091",
            "XZ": "28de57c30d6e32e46829805f59e7a3d211c8678100aeff5b9bdffa0753052b68",
            "YZ": "ae1c1a123840804ff5fe4e860eec68f7d45b48d72eb5b086e690a6e5ffa51add",
        },
    }


def test_generated_fixture_is_declared_separately_from_reference_execution():
    value = documents()
    row = next(row for row in value["fixtures.json"]["fixtures"] if row["id"] == "F.xyz-navigation-owned")
    assert row["capture_status"] == "generated"
    assert row["origin"] == "owned_deterministic"
    assert row["reference_execution"] is False
    assert row["match_status"] == "unverified"
    assert row["verification_run_ids"] == []
    load_script("parity_schema").validate_documents(value)


@pytest.mark.parametrize("patch", [
    {"origin": None}, {"origin": "reference"},
    {"reference_execution": None}, {"reference_execution": True},
    {"reference_execution": 0},
])
def test_generated_fixture_rejects_ambiguous_or_claimed_reference_provenance(patch):
    value = deepcopy(documents())
    row = next(row for row in value["fixtures.json"]["fixtures"] if row["id"] == "F.xyz-navigation-owned")
    row.update(patch)
    with pytest.raises(ValueError, match="generated fixture requires owned deterministic provenance"):
        load_script("parity_schema").validate_documents(value)
