"""Availability assertions do not constitute product/parity acceptance."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "verification_evidence_inventory", ROOT / "scripts/verification_evidence_inventory.py")
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


def entry(payload=b"original producer bytes"):
    return {"requirement_id": "G016", "run_id": "original-run", "role": "native",
            "path": "build/original.json", "sha256": hashlib.sha256(payload).hexdigest()}


def test_missing_retired_bytes_do_not_become_verified(tmp_path):
    row = evidence.audit(tmp_path, [entry()], ["build"])
    assert row["entries"][0]["reason"] == "retired"
    assert row["counts"] == {"unavailable": 1}
    assert row["acceptance"] is False


def test_presence_without_original_hash_is_not_identity_proof(tmp_path):
    (tmp_path / "present").write_bytes(b"bytes")
    row = evidence.inspect_entry(tmp_path, {"path": "present"})
    assert row["availability"] == "present"
    assert row["state"] == row["identity"] == "not_checked"


@pytest.mark.parametrize("matches", [False, True])
def test_exact_original_fingerprint_is_required(tmp_path, matches):
    (tmp_path / "build").mkdir()
    path = tmp_path / "build/original.json"
    payload = b"original producer bytes"
    path.write_bytes(payload if matches else b"changed producer bytes")
    row = evidence.inspect_entry(tmp_path, entry())
    assert row["state"] == ("present_verified" if matches else "mismatch")
    assert row["path"] == "build/original.json"
    assert path.read_bytes() == (payload if matches else b"changed producer bytes")


@pytest.mark.parametrize("kind", ["zip", "tar.gz"])
def test_archive_member_can_preserve_original_identity_without_restoring_build(tmp_path, kind):
    payload = b"original producer bytes"
    path = tmp_path / ("capture." + kind)
    if kind == "zip":
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("reports/native.json", payload)
    else:
        with tarfile.open(path, "w:gz") as archive:
            member = tarfile.TarInfo("reports/native.json")
            member.size = len(payload)
            archive.addfile(member, io.BytesIO(payload))
    row = evidence.inspect_entry(tmp_path, {**entry(), "archive": {
        "path": path.name, "member": "reports/native.json"}})
    assert row["state"] == "present_verified"
    assert not (tmp_path / "build").exists()


@pytest.mark.parametrize("member", ["absent", "../escape"])
def test_missing_or_escaping_member_is_not_evidence(tmp_path, member):
    with zipfile.ZipFile(tmp_path / "capture.zip", "w") as archive:
        archive.writestr("original", b"original producer bytes")
    row = evidence.inspect_entry(tmp_path, {**entry(), "archive": {
        "path": "capture.zip", "member": member}})
    assert row["state"] == "unavailable"
    assert row["reason"] == "invalid_locator_or_evidence"


def test_inventory_retains_requirement_and_run_links():
    inventory = {"capabilities": [{"id": "G016", "evidence": {
        "tests": ["build/checkpoint.json"]}}], "verification_runs": [{
        "id": "old", "native": {"report": "build/native.xml"},
        "scope": "prose build/missing.json is not a file link"}]}
    before = json.dumps(inventory)
    rows = evidence.inventory_entries(inventory)
    assert rows == [{"requirement_id": "G016", "role": "tests", "path": "build/checkpoint.json"},
                    {"run_id": "old", "role": "native.report", "path": "build/native.xml"}]
    assert json.dumps(inventory) == before


def test_shared_evidence_is_hashed_once_without_losing_requirement_links():
    value = {"capabilities": [{"id": f"G{index:03d}", "evidence": {
        "tests": ["build/common.json"]}} for index in range(1, 100)]}
    rows = evidence.inventory_entries(value)
    assert len(rows) == 1
    assert rows[0]["path"] == "build/common.json"
    assert [row["requirement_id"] for row in rows[0]["references"]] == [
        f"G{index:03d}" for index in range(1, 100)]


@pytest.mark.parametrize("path", ["../outside", "/absolute", "C:/outside", "build//report"])
def test_original_path_cannot_escape_workspace(tmp_path, path):
    with pytest.raises(ValueError):
        evidence.inspect_entry(tmp_path, {"path": path})
