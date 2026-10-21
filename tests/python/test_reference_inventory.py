"""Reference integrity, candidate retention and commercial-review boundaries."""
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import warnings
import zipfile

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


reference = load_script("reference_inventory")
checker = load_script("check_plan")


def make_zip(entries, compression=zipfile.ZIP_STORED):
    output = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(output, "w", compression=compression) as archive:
            for name, payload in entries:
                archive.writestr(name, payload)
    return output.getvalue()


def inputs(tmp_path):
    commit = "a" * 40
    source = make_zip([(f"ImageJ-{commit}/IJ_Props.txt", b'# File/New submenu\nnew01="Image",ij.Commands("new")\n')])
    pom = b'<project xmlns="http://maven.apache.org/POM/4.0.0"><groupId>example</groupId><artifactId>demo</artifactId><version>1</version><licenses><license><name>BSD-2-Clause</name></license></licenses><dependencies><dependency><artifactId>unreviewed-model</artifactId></dependency></dependencies></project>'
    jar = make_zip([("META-INF/maven/example/demo/pom.xml", pom),
                    ("META-INF/json/org.scijava.plugin.Plugin", b'{"class":"example.Editor","values":{"menuPath":"Plugins>Demo"}}{"class":"example.Provider","values":{"type":"Service"}}'),
                    ("plugins.config", b'Plugins, "Demo", example.Editor("a,b")\n'),
                    ("example/Editor.class", b"synthetic owned test bytes")])
    # This synthetic publisher fingerprint is an independent byte concatenation.
    fingerprint = hashlib.sha1(b"META-INF/json/org.scijava.plugin.Plugin" +
                              b'{"class":"example.Editor","values":{"menuPath":"Plugins>Demo"}}{"class":"example.Provider","values":{"type":"Service"}}' +
                              b"META-INF/maven/example/demo/pom.xml" + pom +
                              b"example/Editor.classsynthetic owned test bytes" +
                              b'plugins.configPlugins, "Demo", example.Editor("a,b")\n').hexdigest()
    database = f'<pluginRecords><update-site name="Pinned" url="https://example.invalid/" timestamp="20261007000000"/><disabled-update-site name="Optional" url="https://example.invalid/optional/"/><plugin filename="jars/demo-1.jar" update-site="Pinned"><version checksum="{fingerprint}" filesize="{len(jar)}" timestamp="20261007000000"><dependency filename="jars/absent.jar" timestamp="1"/></version></plugin></pluginRecords>'.encode()
    fiji = make_zip([("Fiji/db.xml.gz", gzip.compress(database, mtime=0)),
                     ("Fiji/jars/demo-1.jar", jar), ("Fiji/plugins/demo.ijm", b'print("Synthetic reference");')])
    fiji_path, source_path = tmp_path / "fiji.zip", tmp_path / "imagej.zip"
    fiji_path.write_bytes(fiji)
    source_path.write_bytes(source)
    pins = {"reference_id": "owned-synthetic-fixture", "acquired": "2026-10-10",
            "fiji": {"root": "Fiji", "sha256": hashlib.sha256(fiji).hexdigest()},
            "imagej": {"commit": commit, "sha256": hashlib.sha256(source).hexdigest()}}
    return fiji_path, source_path, pins


def test_import_retains_all_candidates_and_declared_dependencies_without_approving_licenses(tmp_path):
    documents = reference.build_inventory(*inputs(tmp_path))
    rows = documents["fiji-components.json"]["files"]
    assert len(rows) == 3 and all(row["commercial_review"]["decision"] == "pending" for row in rows)
    component = next(row for row in rows if row["kind"] == "jar")
    assert component["poms"][0]["declared_licenses"][0]["name"] == "BSD-2-Clause"
    assert component["poms"][0]["declared_dependencies"][0]["artifactId"] == "unreviewed-model"
    assert component["updater_record"]["dependencies"][0]["filename"] == "jars/absent.jar"
    assert component["updater_reconciliation"]["status"] == "matched"
    candidates = documents["commands.json"]["candidates"]
    assert len(candidates) == 5 and {row["kind"] for row in candidates} == {"imagej_property", "legacy_menu", "scijava_plugin", "script"}
    assert all(row["assignment"] is None and row["option_inventory"] is None for row in candidates)
    assert any(row["descriptor"].get("class") == "example.Provider" for row in candidates)
    db = documents["references.json"]["fiji"]["database"]
    assert len(db["enabled_sites"]) == 1 and len(db["disabled_sites"]) == 1


def test_archive_checksum_mismatch_cannot_publish_inventory(tmp_path):
    fiji, source, pins = inputs(tmp_path)
    pins["fiji"]["sha256"] = "0" * 64
    destination = tmp_path / "out"
    with pytest.raises(ValueError, match="checksum mismatch"):
        reference.write_inventory(destination, reference.build_inventory(fiji, source, pins))
    assert not destination.exists()


@pytest.mark.parametrize("name", ["../escape", "/absolute", "Fiji/../escape", "Fiji\\escape", "Fiji/C:/escape"])
def test_outer_archive_paths_are_validated_before_inventory(name):
    # ZipInfo normalizes Windows separators on writing; construct the raw ZIP
    # header name so the test covers the original name on reading as well.
    payload = make_zip([(name.replace("\\", "/"), b"data")])
    if "\\" in name:
        payload = payload.replace(name.replace("\\", "/").encode(), name.encode())
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        with pytest.raises(ValueError, match="Unsafe"):
            reference.zip_members(archive)


def test_duplicate_outer_members_are_rejected_and_nested_resources_retained():
    with zipfile.ZipFile(io.BytesIO(make_zip([("same", b"one"), ("same", b"two")]))) as archive:
        with pytest.raises(ValueError, match="Duplicate"):
            reference.zip_members(archive)
    first = b'<project><artifactId>one</artifactId></project>'
    second = b'<project><artifactId>two</artifactId></project>'
    name = "META-INF/maven/example/shaded/pom.xml"
    metadata, _ = reference.jar_metadata(make_zip([(name, first), (name, second), ("package/resource:method.RData", b"data")]), "jar", "reference.jar")
    assert [p["coordinates"]["artifactId"] for p in metadata["poms"]] == ["one", "two"]
    assert [p["entry_occurrence"] for p in metadata["poms"]] == [0, 1]
    assert metadata["duplicate_jar_members"] == {name: 2}
    assert {row["member"] for row in metadata["resources"]} == {name, "package/resource:method.RData"}
    assert all(row["license_review"] == "pending" for row in metadata["resources"])
    assert len({row["sha256"] for row in metadata["resources"] if row["member"] == name}) == 2


def test_updater_fingerprint_is_stable_across_zip_packing_and_comment_dates():
    first = [("META-INF/MANIFEST.MF", b"Created-By: first\r\nMain-Class: First\r\n"),
             ("META-INF/", b""), ("settings.properties", b"# timestamp one\na=1\n"), ("A.class", b"owned")]
    second = [(name, payload.replace(b"timestamp one", b"timestamp two").replace(b"First", b"Other")) for name, payload in reversed(first)]
    with zipfile.ZipFile(io.BytesIO(make_zip(first))) as a, zipfile.ZipFile(io.BytesIO(make_zip(second, zipfile.ZIP_DEFLATED))) as b:
        aa, bb = reference.updater_jar_digests(a), reference.updater_jar_digests(b)
    expected = hashlib.sha1(b"A.classownedMETA-INF/META-INF/MANIFEST.MFsettings.properties\na=1\n").hexdigest()
    assert aa["current"] == bb["current"] == expected
    assert aa["legacy_raw"] != bb["legacy_raw"]


def test_pinned_properties_filter_keeps_comments_after_blank_lines():
    payload = b"#one\n\n#two\n#\n#three\nkey=value\r\n #four\n"
    expected = b"\n\n#two\n\n#three\nkey=value\r\n #four\n"
    assert reference.updater_properties(payload) == expected
    assert reference.updater_properties(b"key=1\n#unterminated comment") == b"key=1\n"


def test_updater_duplicate_streams_use_last_entry_while_resource_hashes_preserve_each():
    payload = make_zip([("data", b"first"), ("data", b"last")])
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        digests = reference.updater_jar_digests(archive)
    assert digests["current"] == hashlib.sha1(b"datalastdatalast").hexdigest()
    metadata, _ = reference.jar_metadata(payload, "jar", "reference.jar")
    assert [r["sha256"] for r in metadata["resources"]] == [
        hashlib.sha256(b"first").hexdigest(), hashlib.sha256(b"last").hexdigest()]


@pytest.mark.parametrize("damage", ["checksum", "duplicate"])
def test_resource_integrity_cannot_pass_traceability(tmp_path, monkeypatch, damage):
    documents = reference.build_inventory(*inputs(tmp_path))
    jar = next(row for row in documents["fiji-components.json"]["files"] if row["kind"] == "jar")
    if damage == "checksum":
        jar["resources"][0]["sha256"] = "missing"
    else:
        jar["resources"].append(dict(jar["resources"][0]))
    reference.write_inventory(tmp_path / "docs/parity", documents)
    monkeypatch.setattr(checker, "ROOT", tmp_path)
    with pytest.raises(ValueError, match="resource"):
        checker.validate_references()


def test_component_approval_cannot_skip_embedded_resource_review(tmp_path, monkeypatch):
    documents = reference.build_inventory(*inputs(tmp_path))
    jar = next(row for row in documents["fiji-components.json"]["files"] if row["kind"] == "jar")
    jar["commercial_review"].update(decision="eligible", code="reviewed", dependencies="reviewed",
                                    models_assets="reviewed", evidence=["owned synthetic evidence"],
                                    reason="synthetic review", incorporation="allowed")
    reference.write_inventory(tmp_path / "docs/parity", documents)
    monkeypatch.setattr(checker, "ROOT", tmp_path)
    with pytest.raises(ValueError, match="resources require eligibility"):
        checker.validate_references()


def test_concatenated_plugin_indexes_are_not_treated_as_lines_or_commands_only():
    assert len(list(reference.json_objects(b'\xef\xbb\xbf{"class":"one"}{"class":"two"}\n'))) == 2
    with pytest.raises(ValueError):
        list(reference.json_objects(b'{"class":"one"}[]'))


def test_reproduction_check_does_not_modify_drifted_manifest(tmp_path):
    documents = reference.build_inventory(*inputs(tmp_path))
    destination = tmp_path / "out"
    reference.write_inventory(destination, documents)
    reference.write_inventory(destination, documents, check=True)
    target = destination / "commands.json"
    target.write_bytes(b"modified evidence")
    with pytest.raises(ValueError, match="not reproducible"):
        reference.write_inventory(destination, documents, check=True)
    assert target.read_bytes() == b"modified evidence"


def test_reference_gate_reports_missing_artifacts_and_rejects_unreviewed_approval(tmp_path, monkeypatch):
    documents = reference.build_inventory(*inputs(tmp_path))
    destination = tmp_path / "docs/parity"
    reference.write_inventory(destination, documents)
    monkeypatch.setattr(checker, "ROOT", tmp_path)
    counts = checker.validate_references()
    assert counts["components_pending"] == 3 and counts["static_candidates"] == 5
    assert counts["missing"] == ["formats.json", "fixtures.json", "benchmarks.json", "platforms.json"]
    with pytest.raises(ValueError, match="Missing reference manifests"):
        checker.validate_references(require_complete=True)
    rows = documents["fiji-components.json"]["files"]
    rows[0]["commercial_review"].update(decision="eligible", evidence=["BSD declaration"], reason="permissive")
    reference.write_inventory(destination, documents)
    with pytest.raises(ValueError, match="transitive code/model/asset"):
        checker.validate_references()


def test_orphan_candidate_cannot_pass_traceability(tmp_path, monkeypatch):
    documents = reference.build_inventory(*inputs(tmp_path))
    documents["commands.json"]["candidates"][0]["component_id"] = "missing-component"
    reference.write_inventory(tmp_path / "docs/parity", documents)
    monkeypatch.setattr(checker, "ROOT", tmp_path)
    with pytest.raises(ValueError, match="orphaned"):
        checker.validate_references()


def test_reference_checker_rejects_changed_legacy_fixture_bytes(tmp_path, monkeypatch):
    """Historical captured/matched labels cannot hide changed reference content."""
    destination = tmp_path / "docs/parity"
    destination.mkdir(parents=True)
    for source in (ROOT / "docs/parity").glob("*.json"):
        shutil.copyfile(source, destination / source.name)
    fixtures = json.loads((destination / "fixtures.json").read_text(encoding="utf-8"))["fixtures"]
    for fixture in fixtures:
        for name in (fixture["path"], fixture["generator"]["path"]):
            target = tmp_path / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
    monkeypatch.setattr(checker, "ROOT", tmp_path)
    result = checker.validate_references()["fixture_evidence"]
    assert result["fixtures_declared"] == 4
    assert len(result["missing_contracts"]) == 4
    target = tmp_path / fixtures[0]["path"]
    target.write_bytes(target.read_bytes() + b"\n")
    with pytest.raises(ValueError, match="(?i)fixture|fingerprint|sha256|byte"):
        checker.validate_references()


def test_regeneration_preserves_existing_reviewed_decisions(tmp_path):
    documents = reference.build_inventory(*inputs(tmp_path))
    destination = tmp_path / "out"
    reference.write_inventory(destination, documents)
    target = destination / "fiji-components.json"
    data = json.loads(target.read_text(encoding="utf-8"))
    data["files"][0]["commercial_review"]["decision"] = "excluded"
    target.write_text(json.dumps(data), encoding="utf-8")
    before = {name: (destination / name).read_bytes() for name in documents}
    with pytest.raises(ValueError, match="explicit merge"):
        reference.write_inventory(destination, documents)
    assert before == {name: (destination / name).read_bytes() for name in documents}
