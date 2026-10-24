"""Pinned reference integrity/reader checks; these do not certify native parity."""
import copy
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[2]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


reader = module("verify_path_filter_reference")
driver = module("capture_path_filter_reference")
FIXTURE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-filter-targets-imagej154r.json.gz").read_bytes()))
PRIMARY = FIXTURE["cases"]
CASES = PRIMARY + FIXTURE["degenerate_cases"]
COMPOSITE = json.loads((ROOT / "tests/fixtures/imagej-roi-composite-imagej154r.json").read_bytes())
RANK_FIXTURE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-rank-targets-imagej154r.json.gz").read_bytes()))


def test_reference_catalog_and_provenance_are_complete():
    assert [row["request"] for row in PRIMARY] == driver.catalog()
    assert [row["request"] for row in FIXTURE["degenerate_cases"]] == driver.degenerate_catalog()
    assert len({row["id"] for row in CASES}) == 125
    assert FIXTURE["imagej_commit"] == COMPOSITE["imagej_commit"]
    assert FIXTURE["runtime_pin_fixture_sha256"] == hashlib.sha256(
        (ROOT / "tests/fixtures/tophat-mask-imagej154r.json").read_bytes()).hexdigest()
    assert FIXTURE.get("verified_recapture_observer_sha256", FIXTURE["observer_sha256"]) == hashlib.sha256(driver.OBSERVER.read_bytes()).hexdigest()
    assert hashlib.sha256(FIXTURE["original_observer_source_utf8"].encode()).hexdigest() == FIXTURE["observer_sha256"]


@pytest.mark.parametrize("row", CASES, ids=[row["id"] for row in CASES])
def test_observation_masks_inputs_and_snapshot_restoration(row):
    previous = None
    if row["id"].startswith("composite-"):
        previous = COMPOSITE["cases"][int(row["id"].split("-")[1])]["mask"]
    checked = reader.validate_observation(row["observation"], row["request"], previous)
    assert checked.get("raw_reference_unselected_changes", 0) == (
        FIXTURE["reference_deviations"]["unselected_writes"].get(row["id"], 0))


def case(name):
    return copy.deepcopy(next(row for row in CASES if row["id"] == name))


@pytest.mark.parametrize("mutation,diagnostic", [
    ("input", "Input sample words"),
    ("mask", "Full/clipped masks"),
    ("output", "Snapshot restoration"),
    ("null-reset", "Null-mask restoration"),
    ("nonword", "Malformed F32 word"),
])
def test_reader_rejects_corrupted_observations(mutation, diagnostic):
    row = case("fractional-triangle-sigma1")
    observed = row["observation"]
    if mutation == "input":
        observed["input"]["f32_words"][0] = "0x00000000"
    elif mutation == "mask":
        mask = observed["processor_mask"]
        mask["u8_hex"] = "00" + mask["u8_hex"][2:]
    elif mutation == "output":
        observed["gaussian"]["after_mask_reset_f32_words"][0] = "0x3f800000"
    elif mutation == "null-reset":
        observed["gaussian"]["mask_reset_performed"] = False
    else:
        observed["gaussian"]["after_mask_reset_f32_words"][0] = "NaN"
    with pytest.raises(ValueError, match=diagnostic):
        reader.validate_observation(observed, row["request"])


def test_processor_bounds_use_reviewed_runtime_extrema_and_move_normalization():
    # Literal observed bounds, independently distinct from both control hull and
    # occupied pixel bounds. These anchor a future native processing planner.
    for name, bounds in [
        ("fractional-triangle-sigma1", (2, 1, 8, 8)),
        ("curve-control-hull-sigma1", (0, 0, 13, 12)),
        ("consecutive-moves-sigma1", (2, 2, 4, 4)),
        ("negative-billion-metadata", (-1000000000, 1, 1000000050, 5)),
    ]:
        assert reader.rectangle(case(name)["observation"]["roi_before_attachment"]["bounds"]) == bounds


def test_empty_attachment_is_distinct_from_off_canvas_intersection():
    cleared = case("composite-07-sigma1")["observation"]
    outside = case("outside-canvas-sigma1")["observation"]
    assert cleared["image_retains_same_roi"] is False
    assert reader.rectangle(cleared["processor_roi_before_mask"]) == (0, 0, 12, 10)
    assert outside["image_retains_same_roi"] is True
    assert reader.rectangle(outside["processor_roi_before_mask"]) == (0, 0, 0, 0)
    assert outside["roi_mask"] is not None and outside["processor_mask"] is None


def test_metadata_reader_rejects_processing_data():
    row = case("negative-billion-metadata")
    row["observation"]["processor_mask"] = None
    with pytest.raises(ValueError, match="Metadata-only"):
        reader.validate_observation(row["observation"], row["request"])


@pytest.mark.parametrize("fault", ["missing", "duplicate", "declared-omission"])
def test_capture_reader_rejects_incomplete_case_ledger(tmp_path, fault):
    names = [row["id"] for row in PRIMARY]
    receipt = {"schema": "p13i.reference.capture.001", "state": "observed",
               "required_cases": names, "selected_cases": names,
               "complete_catalog_observed": True,
               "cases": [{"id": name} for name in names]}
    if fault == "missing":
        receipt["cases"].pop()
    elif fault == "duplicate":
        receipt["cases"][-1] = receipt["cases"][0]
    else:
        receipt["required_cases"] = names[:-1]
        receipt["selected_cases"] = names[:-1]
        receipt["cases"].pop()
    path = tmp_path / "receipt.json"
    path.write_text(json.dumps(receipt))
    with pytest.raises(ValueError, match="catalog|Duplicate"):
        reader.validate_capture(path, ROOT / "tests/fixtures/imagej-roi-composite-imagej154r.json")


def prepared_fixture(tmp_path):
    classes = tmp_path / "classes"
    classes.mkdir()
    path = tmp_path / "prepared.json"
    path.write_text(json.dumps({"schema": "p13i.reference.prepare.001", "state": "prepared",
        "classes_root": str(classes), "classes": [], "inputs": {
            "source_archive": {}, "observer": {}, "source_files": [],
            "runtime_files": [{"path": str(tmp_path / "bin/java.exe")}],
            "pin_documents": []}}))
    return path


def test_capture_timeout_retains_failed_attempt(tmp_path, monkeypatch):
    prepared = prepared_fixture(tmp_path)
    monkeypatch.setattr(driver, "check", lambda _: None)

    def timeout(*args, **kwargs):
        raise subprocess.TimeoutExpired(args[0], kwargs["timeout"])

    monkeypatch.setattr(driver.subprocess, "run", timeout)
    out = tmp_path / "capture"
    with pytest.raises(subprocess.TimeoutExpired):
        driver.capture(SimpleNamespace(prepared_receipt=prepared, output=out,
                                      case=["fractional-triangle-sigma1"], catalog="primary"))
    receipt = json.loads((out / "receipt.json").read_bytes())
    assert receipt["state"] == "failed" and receipt["acceptance"] is False
    assert receipt["error"]["type"] == "TimeoutExpired"
    assert receipt["cases"] == []
    assert receipt["attempts"][0]["ended"] >= receipt["attempts"][0]["started"]
    assert receipt["attempts"][0]["exit_code"] is None


def test_capture_rejects_duplicate_explicit_selection(tmp_path, monkeypatch):
    prepared = prepared_fixture(tmp_path)
    monkeypatch.setattr(driver, "check", lambda _: None)
    with pytest.raises(ValueError, match="Duplicate"):
        driver.capture(SimpleNamespace(prepared_receipt=prepared, output=tmp_path / "capture",
            case=["fractional-triangle-sigma1"] * 2, catalog="primary"))
    assert not (tmp_path / "capture").exists()


def test_capture_rejects_unpinned_class_membership(tmp_path, monkeypatch):
    prepared = prepared_fixture(tmp_path)
    (tmp_path / "classes/Injected.class").write_bytes(b"not a reviewed class")
    monkeypatch.setattr(driver, "check", lambda _: None)
    with pytest.raises(ValueError, match="class membership"):
        driver.capture(SimpleNamespace(prepared_receipt=prepared, output=tmp_path / "capture",
            case=["fractional-triangle-sigma1"], catalog="primary"))
    assert not (tmp_path / "capture").exists()


@pytest.mark.parametrize("limit", ["stdout", "stderr", "combined"])
def test_capture_output_refusal_is_recorded(tmp_path, monkeypatch, limit):
    prepared = prepared_fixture(tmp_path)
    monkeypatch.setattr(driver, "check", lambda _: None)
    monkeypatch.setattr(driver, {"stdout": "MAX_CASE_BYTES", "stderr": "MAX_STDERR_BYTES",
                                "combined": "MAX_CAPTURE_BYTES"}[limit], 8)

    def output(*args, **kwargs):
        kwargs["stdout"].write(b"{}" * 16)
        if limit != "stdout":
            kwargs["stderr"].write(b"synthetic diagnostic")
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(driver.subprocess, "run", output)
    out = tmp_path / "capture"
    with pytest.raises(ValueError, match="output bounds"):
        driver.capture(SimpleNamespace(prepared_receipt=prepared, output=out,
            case=["fractional-triangle-sigma1"], catalog="primary"))
    receipt = json.loads((out / "receipt.json").read_bytes())
    assert receipt["state"] == "failed" and receipt["cases"] == []
    assert receipt["attempts"][0]["exit_code"] == 0
    assert receipt["attempts"][0]["stdout"]["bytes"] == 32


def test_prepare_timeout_retains_failed_receipt(tmp_path, monkeypatch):
    source, runtime = tmp_path / "source", tmp_path / "runtime"
    (source / "ij").mkdir(parents=True)
    (source / "ij/Example.java").write_text("// synthetic tool-failure fixture")
    runtime.mkdir()
    monkeypatch.setattr(driver, "verify_inputs", lambda *args: {"synthetic": True})

    def timeout(*args, **kwargs):
        raise subprocess.TimeoutExpired(args[0], kwargs["timeout"])

    monkeypatch.setattr(driver.subprocess, "run", timeout)
    archive = tmp_path / "source.zip"
    archive.write_bytes(b"synthetic; not an executable reference")
    out = tmp_path / "prepare"
    with pytest.raises(subprocess.TimeoutExpired):
        driver.prepare(SimpleNamespace(output=out, source_root=source, runtime_root=runtime,
                                       classes_root=tmp_path / "classes", source_archive=archive))
    receipt = json.loads((out / "receipt.json").read_bytes())
    assert receipt["state"] == "failed" and receipt["classes"] == []
    assert receipt["error"]["type"] == "TimeoutExpired"


def test_typed_rank_reference_matrix_and_source_pins():
    rows = RANK_FIXTURE["cases"]
    assert [row["request"] for row in rows] == driver.rank_catalog()
    assert len({row["id"] for row in rows}) == 253
    assert RANK_FIXTURE["observer_sha256"] == hashlib.sha256(driver.RANK_OBSERVER.read_bytes()).hexdigest()
    assert RANK_FIXTURE["shared_observer_sha256"] == hashlib.sha256(driver.OBSERVER.read_bytes()).hexdigest()
    keys = {(r["operation"], r["pixel_type"]) for r in (row["request"] for row in rows)}
    assert keys == {(operation, kind) for operation in
        ("minimum", "maximum", "mean", "variance", "median", "despeckle", "outliers", "top_hat")
        for kind in ("u8", "u16", "f32")} | {("remove_nans", "f32")}
    top = [row["request"] for row in rows if row["request"]["operation"] == "top_hat"]
    assert {(r["light_background"], r["dont_subtract"], r["inverted_lut"], r["pixel_type"])
            for r in top} == {(light, dont, inverted, kind) for light in (False, True)
                for dont in (False, True) for inverted in (False, True) for kind in ("u8", "u16", "f32")}
    assert {row["request"]["input_pattern"] for row in rows
            if row["request"]["operation"] == "remove_nans"} == {"finite", "quiet_nan", "ieee"}


@pytest.mark.parametrize("row", RANK_FIXTURE["cases"], ids=lambda row: row["id"])
def test_typed_rank_reference_inputs_masks_and_restoration(row):
    old = next((old["mask"] for old in COMPOSITE["cases"]
                if old["path"] == row["request"]["commands"]), None)
    result = reader.validate_rank_observation(row["observation"], row["request"], old)
    assert result["raw_reference_unselected_changes"] == 0


@pytest.mark.parametrize("fault", ["word", "nan-payload", "option", "restoration"])
def test_typed_rank_reader_rejects_corruption(fault):
    row = copy.deepcopy(next(row for row in RANK_FIXTURE["cases"]
        if row["request"]["operation"] == "remove_nans" and row["request"]["input_pattern"] == "ieee"))
    observed = row["observation"]
    if fault == "word":
        observed["input"]["sample_words"][0] = "Infinity"
    elif fault == "nan-payload":
        observed["input"]["sample_words"][2*row["request"]["width"]+3] = "0x7fc00000"
    elif fault == "option":
        observed["options"]["radius"] = -1
    else:
        # First word is outside these paths and remains an explicit +Infinity.
        observed["after_mask_reset"]["sample_words"][0] = "0x00000000"
    with pytest.raises(ValueError):
        reader.validate_rank_observation(observed, row["request"])


SUPPLEMENTAL_FIXTURES = {
    name: json.loads(gzip.decompress((ROOT / "tests/fixtures" / filename).read_bytes()))
    for name, filename in {
        "typed-gaussian": "path-typed-gaussian-targets-imagej154r.json.gz",
        "rank-scope": "path-rank-scope-targets-imagej154r.json.gz",
        "scientific": "path-scientific-targets-imagej154r.json.gz",
    }.items()
}


@pytest.mark.parametrize("name,catalog,observer,count", [
    ("typed-gaussian", driver.typed_gaussian_catalog, driver.TYPED_GAUSSIAN_OBSERVER, 80),
    ("rank-scope", driver.rank_scope_catalog, driver.RANK_OBSERVER, 27),
    ("scientific", driver.scientific_catalog, driver.SCIENTIFIC_OBSERVER, 18),
])
def test_supplemental_catalogs_and_observer_provenance(name, catalog, observer, count):
    fixture = SUPPLEMENTAL_FIXTURES[name]
    rows = fixture["cases"]
    assert len(rows) == len({row["id"] for row in rows}) == count
    assert [row["request"] for row in rows] == catalog()
    assert fixture["observer_sha256"] == hashlib.sha256(observer.read_bytes()).hexdigest()
    assert fixture["shared_observer_sha256"] == hashlib.sha256(driver.OBSERVER.read_bytes()).hexdigest()
    assert fixture["imagej_commit"] == FIXTURE["imagej_commit"]
    if name != "scientific":
        assert fixture["source_archive_sha256"] == json.loads(driver.PINS.read_bytes())["imagej"]["sha256"]
        assert fixture["runtime_pin_fixture_sha256"] == FIXTURE["runtime_pin_fixture_sha256"]


SUPPLEMENTAL_ROWS = [(name, row) for name, fixture in SUPPLEMENTAL_FIXTURES.items()
                     for row in fixture["cases"]]


@pytest.mark.parametrize("name,row", SUPPLEMENTAL_ROWS,
                         ids=[row["id"] for _, row in SUPPLEMENTAL_ROWS])
def test_supplemental_reference_inputs_masks_and_restoration(name, row):
    validate = {"typed-gaussian": reader.validate_typed_gaussian_observation,
                "rank-scope": reader.validate_rank_observation,
                "scientific": reader.validate_scientific_observation}[name]
    result = validate(row["observation"], row["request"])
    assert result["raw_reference_unselected_changes"] == SUPPLEMENTAL_FIXTURES[name][
        "raw_reference_unselected_changes"].get(row["id"], 0)
    if name == "rank-scope":
        assert result["selected"] == 120
    if name == "scientific":
        assert result["selected"] == (50 if row["request"]["region"] == "union" else 28)


@pytest.mark.parametrize("fault", ["plane", "mask", "operand", "restore", "option"])
def test_scientific_reference_reader_rejects_corruption(fault):
    row = copy.deepcopy(next(row for row in SUPPLEMENTAL_FIXTURES["scientific"]["cases"]
                             if row["request"]["region"] == "union"))
    observation, request = row["observation"], row["request"]
    if fault == "plane":
        request["position"] = [0, 0, 0]
    elif fault == "mask":
        observation["processor_mask"]["u8_hex"] = "00" + observation["processor_mask"]["u8_hex"][2:]
    elif fault == "operand":
        observation["operands"][1]["commands_f32_words"][1] = "0x00000000"
    elif fault == "restore":
        # Cell (2,1) is selected; choose (12,1) in the bounding box outside the union.
        observation["after_mask_reset_f32_words"][1*23+12] = "0x00000000"
    else:
        observation["options"]["sigma"] = 12
    with pytest.raises(ValueError):
        reader.validate_scientific_observation(observation, request)


BUDGET_FIXTURE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-budget-targets-imagej154r.json.gz").read_bytes()))


def test_budget_reference_catalog_and_provenance():
    assert [row["request"] for row in BUDGET_FIXTURE["cases"]] == (
        driver.budget_catalog("gaussian") + driver.budget_catalog("rank"))
    assert BUDGET_FIXTURE["imagej_commit"] == FIXTURE["imagej_commit"]
    assert BUDGET_FIXTURE["source_archive_sha256"] == json.loads(driver.PINS.read_bytes())["imagej"]["sha256"]
    for key, observer in [("observer", driver.OBSERVER), ("rank_observer", driver.RANK_OBSERVER),
                          ("typed_gaussian_observer", driver.TYPED_GAUSSIAN_OBSERVER)]:
        assert BUDGET_FIXTURE["observers"][key] == hashlib.sha256(observer.read_bytes()).hexdigest()
    assert BUDGET_FIXTURE["runtime_pin_fixture_sha256"] == FIXTURE["runtime_pin_fixture_sha256"]


@pytest.mark.parametrize("row", BUDGET_FIXTURE["cases"], ids=lambda row: row["id"])
def test_budget_reference_inputs_masks_and_restoration(row):
    validate = reader.validate_typed_gaussian_observation if row["id"] == "budget-gaussian" else reader.validate_rank_observation
    checked = validate(row["observation"], row["request"])
    assert checked == {"pixels": 32768, "selected": 28, "raw_reference_unselected_changes": 0}


def test_budget_catalog_refuses_unknown_operation():
    with pytest.raises(ValueError, match="Unknown budget reference operation"):
        driver.budget_catalog("unreviewed")


TOP_HAT_DISPLAY_FIXTURE = json.loads(gzip.decompress(
    (ROOT / "tests/fixtures/path-top-hat-display-imagej154r.json.gz").read_bytes()))


def test_top_hat_display_catalog_and_provenance():
    rows = TOP_HAT_DISPLAY_FIXTURE["cases"]
    assert len(rows) == len({row["id"] for row in rows}) == 16
    assert [row["request"] for row in rows] == driver.top_hat_display_catalog()
    assert TOP_HAT_DISPLAY_FIXTURE["observer_sha256"] == hashlib.sha256(driver.TOP_HAT_DISPLAY_OBSERVER.read_bytes()).hexdigest()
    assert TOP_HAT_DISPLAY_FIXTURE["shared_observer_sha256"] == hashlib.sha256(driver.OBSERVER.read_bytes()).hexdigest()
    assert TOP_HAT_DISPLAY_FIXTURE["runtime_pin_fixture_sha256"] == FIXTURE["runtime_pin_fixture_sha256"]
    assert TOP_HAT_DISPLAY_FIXTURE["imagej_commit"] == FIXTURE["imagej_commit"]
    assert TOP_HAT_DISPLAY_FIXTURE["rank_reference_fixture_sha256"] == hashlib.sha256(
        (ROOT / "tests/fixtures/path-rank-targets-imagej154r.json.gz").read_bytes()).hexdigest()


@pytest.mark.parametrize("row", TOP_HAT_DISPLAY_FIXTURE["cases"], ids=lambda row: row["id"])
def test_top_hat_display_phase_inputs_masks_and_reference_binding(row):
    rank_row = next(old for old in RANK_FIXTURE["cases"] if old["id"] == row["rank_reference_id"])
    checked = reader.validate_top_hat_display_observation(row["observation"], row["request"], rank_row)
    assert checked["raw_reference_unselected_changes"] == TOP_HAT_DISPLAY_FIXTURE["raw_reference_unselected_changes"].get(row["id"],0)
    assert checked["pixels"] == 391 and checked["selected"] == 28


@pytest.mark.parametrize("fault", ["input", "selected", "mask", "phase", "option", "reset", "range", "roi"])
def test_top_hat_display_reader_rejects_corruption(fault):
    row = copy.deepcopy(TOP_HAT_DISPLAY_FIXTURE["cases"][0])
    observation = row["observation"]
    rank_row = next(old for old in RANK_FIXTURE["cases"] if old["id"] == row["rank_reference_id"])
    if fault == "input":observation["input_f32_words"][0] = "0x00000000"
    elif fault == "selected":observation["after_mask_reset_f32_words"][25] = "0x7f800000"
    elif fault == "mask":observation["processor_mask"]["u8_hex"] = "00"+observation["processor_mask"]["u8_hex"][2:]
    elif fault == "phase":observation["phase"] = "apply"
    elif fault == "option":observation["effective_options"]["dont_subtract"] = True
    elif fault == "reset":observation["mask_reset_performed"] = True
    elif fault == "range":observation["final_range"] = [-8,float("nan")]
    else:observation["processor_roi_after_processing"]["dimensions"] = [8,8]
    with pytest.raises(ValueError):reader.validate_top_hat_display_observation(observation,row["request"],rank_row)


def test_top_hat_capture_refuses_unprepared_observer(tmp_path, monkeypatch):
    prepared = prepared_fixture(tmp_path)
    monkeypatch.setattr(driver,"check",lambda _:None)
    with pytest.raises(ValueError,match="Top Hat display observer"):
        driver.capture(SimpleNamespace(prepared_receipt=prepared,output=tmp_path/"capture",case=None,catalog="top-hat-display"))
    assert not (tmp_path/"capture").exists()
