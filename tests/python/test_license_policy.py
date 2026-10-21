"""Dependency policy rejects restricted or unreviewed transitive additions."""
import importlib.util
import hashlib
import json
from pathlib import Path
import shutil
from types import SimpleNamespace

import pytest


@pytest.mark.parametrize("change", [
    {"commercial_use": False},
    {"license": "CC-BY-NC-4.0"},
    {"source": ""},
    {"version": "2.0"},
    None,
])
def test_transitive_dependency_policy_rejects_restricted_or_unreviewed_entries(tmp_path, monkeypatch, change):
    spec = importlib.util.spec_from_file_location(
        "license_policy", Path(__file__).resolve().parents[2] / "scripts/check_licenses.py"
    )
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    record = {"version": "1.0", "license": "MIT", "commercial_use": True,
              "source": "https://example.org/license"}
    inventory = {"example-root": record.copy(), "example-child": record.copy()}
    if change is None:
        del inventory["example-child"]
    else:
        inventory["example-child"].update(change)
    (tmp_path / "licenses").mkdir()
    (tmp_path / "licenses/dependencies.json").write_text(json.dumps({"dependencies": inventory}), encoding="utf-8")
    (tmp_path / "pyproject.toml").write_text(
        '[project]\ndependencies=["example-root==1.0"]\n[build-system]\nrequires=[]\n',
        encoding="utf-8",
    )
    monkeypatch.setattr(gate, "ROOT", tmp_path)
    monkeypatch.setattr(gate.metadata, "distribution", lambda name: SimpleNamespace(
        version="1.0", requires=["example-child==1.0"] if name == "example-root" else []
    ))
    assert gate.check() == []
    errors = gate.check(installed=True)
    assert any("example-child" in error for error in errors)


@pytest.mark.parametrize("change", [
    "none", "missing_entry", "noncommercial", "wrong_version", "wrong_notice",
    "manifest_hash", "changed_header", "missing_header", "extra_header", "traversal",
    "zlib_noncommercial", "zlib_wrong_version", "zlib_wrong_notice",
    "zlib_manifest_hash", "zlib_changed_header", "zlib_missing_header",
    "zlib_extra_header", "zlib_traversal", "zlib_archive_pin", "zlib_changed_notice",
])
def test_native_review_retains_complete_private_source_tree(tmp_path, monkeypatch, change):
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location("native_license_policy", root / "scripts/check_licenses.py")
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    (tmp_path / "licenses").mkdir()
    shutil.copyfile(root / "licenses/nlohmann-json.txt", tmp_path / "licenses/nlohmann-json.txt")
    shutil.copyfile(root / "licenses/zlib.txt", tmp_path / "licenses/zlib.txt")
    vendor = tmp_path / "third_party/nlohmann_json"
    shutil.copytree(root / "third_party/nlohmann_json", vendor)
    zlib_vendor = tmp_path / "third_party/zlib"
    shutil.copytree(root / "third_party/zlib", zlib_vendor)
    inventory = json.loads((root / "licenses/native-dependencies.json").read_text(encoding="utf-8"))
    entry = inventory["dependencies"]["nlohmann-json"]
    header = vendor / "include/nlohmann/json.hpp"
    if change == "missing_entry":
        inventory["dependencies"].clear()
    elif change == "noncommercial":
        entry["commercial_use"] = False
    elif change == "wrong_version":
        entry["tested_version"] = "3.11.3"
    elif change == "wrong_notice":
        entry["notice"] = ""
    elif change == "manifest_hash":
        entry["vendor_manifest_sha256"] = "0" * 64
    elif change == "changed_header":
        header.write_bytes(header.read_bytes() + b"\n// unreviewed input\n")
    elif change == "missing_header":
        header.unlink()
    elif change == "extra_header":
        (header.parent / "unreviewed.hpp").write_text("// extra input\n", encoding="utf-8")
    elif change == "traversal":
        manifest_path = vendor / "SOURCE.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["files_sha256"]["../../outside.hpp"] = "0" * 64
        payload = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
        manifest_path.write_bytes(payload)
        entry["vendor_manifest_sha256"] = hashlib.sha256(payload).hexdigest()
    elif change.startswith("zlib_"):
        zlib = inventory["dependencies"]["zlib"]
        if change == "zlib_noncommercial":
            zlib["commercial_use"] = False
        elif change == "zlib_wrong_version":
            zlib["tested_version"] = "1.2.13"
        elif change == "zlib_wrong_notice":
            zlib["notice"] = "../outside.txt"
        elif change == "zlib_manifest_hash":
            zlib["vendor_manifest_sha256"] = "0" * 64
        elif change == "zlib_changed_header":
            header = zlib_vendor / "gzguts.h"
            header.write_bytes(header.read_bytes() + b"\n/* unreviewed input */\n")
        elif change == "zlib_missing_header":
            (zlib_vendor / "gzguts.h").unlink()
        elif change == "zlib_extra_header":
            (zlib_vendor / "unreviewed.h").write_text("/* extra input */\n", encoding="utf-8")
        elif change in {"zlib_traversal", "zlib_archive_pin"}:
            manifest_path = zlib_vendor / "SOURCE.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if change == "zlib_traversal":
                manifest["files_sha256"]["../../outside.h"] = "0" * 64
            else:
                manifest["archive_sha256"] = "0" * 64
            payload = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
            manifest_path.write_bytes(payload)
            zlib["vendor_manifest_sha256"] = hashlib.sha256(payload).hexdigest()
        elif change == "zlib_changed_notice":
            (tmp_path / "licenses/zlib.txt").write_text("Different retained license\n", encoding="utf-8")
    (tmp_path / "licenses/native-dependencies.json").write_text(json.dumps(inventory), encoding="utf-8")
    monkeypatch.setattr(gate, "ROOT", tmp_path)
    errors = gate.check_native()
    assert not errors if change == "none" else errors
