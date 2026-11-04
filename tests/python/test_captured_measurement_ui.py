"""Actual menu delivery and captured measurement lifecycle in both ImGui hosts."""
import csv
import json
import math
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_captured_measurement_menu_units_and_stale_cancel(context, tmp_path):
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_CAPTURED_MEASUREMENT_CPP_REPORT"])
        report = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        path = tmp_path / "capture/case.json"
        report = json.loads(editor._captured_measurement_ui_case(str(path.parent)))
    assert report["contract"] == "g034.captured-measurement.ui.001"
    assert report["state"] == "observed" and report["context"] == context
    assert not report["acceptance_claim"] and report["source_unchanged"]
    assert 0 < len(report["frames"]) < 96
    for frame in report["frames"]:
        assert frame["events"] == frame["input_frame"]["raw_inputs"]
    controls = report["controls"]
    assert [r["name"] for r in controls] == ["menu.Analyze", "menu.action.measure"] * 2
    for row in controls:
        assert row["control"]["enabled"] and row["control"]["id"] > 0
    row = report["global_rows"][0]
    assert row["Image"] == "Captured measurement" and row["Bounds"] == [0, 0, 2, 2]
    assert [row[key] for key in ["T", "C", "Z", "Slice"]] == [2, 2, 2, 2]
    assert row["count"] == 4 and row["Area"] == 2
    assert [row[key] for key in ["minimum", "maximum", "mean", "median"]] == [250, 260, 255, 258]
    assert row["stddev"] == pytest.approx(math.sqrt(68 / 3))
    assert row["sum"] == 1020 and row["integrated_density"] == 510
    assert row["Unit"] == "um" and row["Value unit"] == "OD"
    bypassed = report["bypassed_rows"][1]
    assert bypassed["mean"] == 122.5 and bypassed["Area"] == 48
    assert bypassed["Unit"] == "local" and bypassed["Value unit"] == "Gray Value"
    assert report["cancel"]["reads_before"] == report["cancel"]["reads_after"]
    assert report["cancel"]["rows"] == 2
    assert report["stale_error"] and report["stale_rows"] == report["bypassed_rows"]
    assert report["pending_clear"]["rows"] == 0
    assert report["pending_clear"]["reads_before"] == report["pending_clear"]["reads_after"]
    assert report["workflow_count"] == 0 and report["jobs_reserved_after"] == 0
    with path.with_name("global.csv").open(newline="", encoding="utf-8") as f:
        csv_rows = list(csv.DictReader(f))
    assert list(csv_rows[0]) == ["Image", "mean", "stddev", "Area", "Unit", "Value unit", "T", "C", "Z", "Bounds"]
    assert csv_rows[0]["mean"] == "255.000" and csv_rows[0]["stddev"] == "4.761"
    assert csv_rows[0]["Area"] == "2.000"


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32])
@pytest.mark.parametrize("selected", [False, True])
def test_current_area_measurement_action_matches_independent_statistics(dtype, selected, tmp_path):
    from imagejxx import _desktop
    pixels = np.arange(20, dtype=dtype).reshape(4, 5)
    calibration = ij.SpatialCalibration(spacing=[.5, 3, 1], unit="um")
    owner = ij.Document(ij.Dataset(pixels, axes="YX", calibration=calibration))
    if selected:
        edit = owner.begin_edit()
        edit.select(ij.Selection("rectangle", [(0, 0), (2, 2)]))
        owner.commit(edit)
    original = owner.snapshot()
    undo = owner.undo_steps
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    editor.add_native_document(owner)
    editor.action("measure", "{}")
    editor.wait()
    # Editor results are a serialized copy of the creator-owned result table.
    rows = json.loads(editor.results)
    values = pixels[:2, :2].ravel() if selected else pixels.ravel()
    assert len(rows) == 1
    row = rows[0]
    assert row["count"] == values.size
    assert row["mean"] == float(values.astype(np.float64).mean())
    assert row["stddev"] == pytest.approx(values.astype(np.float64).std(ddof=1))
    assert row["Area"] == values.size * 1.5
    assert row["Unit"] == "um" and owner.snapshot().shares_data(original)
    assert owner.undo_steps == undo


def test_measurement_row_reservation_released_on_clear(tmp_path):
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX"))
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    editor.add_native_document(owner)
    before = json.loads(editor.render_memory_info())["retained_bytes"]
    editor.action("measure", "{}")
    editor.wait()
    assert json.loads(editor.render_memory_info())["retained_bytes"] > before
    editor.results = "[]"
    editor.wait()
    memory = json.loads(editor.render_memory_info())
    assert memory["retained_bytes"] == before and memory["working_bytes"] == 0


def test_frontend_row_limit_rejects_before_plane_reads(tmp_path):
    from imagejxx import _desktop
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX").observe_reads(reads),
                        "x" * 200000)
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    editor.add_native_document(owner)
    editor.update_settings('{"processing_memory_mb":1}', False)
    before = reads.counts().plane_reads
    with pytest.raises(ValueError, match="Measurement exceeds"):
        editor.action("measure", "{}")
    assert reads.counts().plane_reads == before and json.loads(editor.results) == []


def test_nonfinite_density_measurement_is_explicit_in_json(tmp_path):
    from imagejxx import _desktop
    from test_calibrated_analysis import density
    owner = ij.Document(ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX"))
    ij.set_imagej_intensity_calibration(owner, density("all_nan", "uint8"))
    before = owner.snapshot()
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    editor.add_native_document(owner)
    editor.action("measure", "{}")
    editor.wait()
    row = json.loads(editor.results)[0]
    assert row["count"] == 0 and row["Area"] == 0
    for field in ["minimum", "maximum", "mean", "median", "sum", "integrated_density"]:
        assert row[field] == "nan"
    assert owner.snapshot().shares_data(before)
