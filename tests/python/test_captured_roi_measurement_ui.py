"""Actual menus/manager button and atomic captured line/ROI-table adoption."""
import csv
import json
import math
import os
from pathlib import Path

import pytest
import imagejxx as ij
from test_target_measurements import fixture


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_line_and_manager_measurement_controls(context, tmp_path):
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_CAPTURED_ROI_MEASUREMENT_CPP_REPORT"])
        report = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        path = tmp_path / "capture/case.json"
        report = json.loads(editor._captured_roi_measurement_ui_case(str(path.parent)))
    assert report["contract"] == "g066.captured-roi-measurement.ui.001"
    assert report["context"] == context and report["state"] == "observed"
    assert not report["acceptance_claim"] and report["source_unchanged"]
    assert 0 < len(report["frames"]) < 96
    for frame in report["frames"]:
        assert frame["events"] == frame["input_frame"]["raw_inputs"]
    controls = [row["name"] for row in report["controls"]]
    assert controls.count("menu.action.measure") == 2
    assert "menu.action.measure_rois" in controls and "manager.measure_rois" in controls
    for row in report["controls"]:
        assert row["control"]["enabled"] and row["control"]["id"] > 0
    line = report["global_line"]
    assert line["count"] == 5  # profile_samples=11 is deliberately unrelated.
    assert line["mean"] == 221 and line["Area"] == 5
    assert line["Length"] == pytest.approx(math.sqrt(18.25))
    assert line["Angle"] == pytest.approx(-math.degrees(math.atan2(4, 1.5)))
    assert [line["X"], line["Y"], line["centroid_z"]] == [.75, 2, 0]
    assert line["Unit"] == "um" and line["Value unit"] == "OD"
    bypassed = report["bypassed_line"]
    assert bypassed["mean"] == 105.5 and bypassed["Area"] == 60
    assert bypassed["Length"] == pytest.approx(math.sqrt(145)) and bypassed["Unit"] == "local"
    manager = report["menu_rows"][-4:]
    assert [row["ROI"] for row in manager] == ["area A", "line B", "same plane", "wildcard"]
    assert [row["ROI index"] for row in manager] == [0, 1, 2, 3]
    assert [(row["T"], row["C"], row["Z"]) for row in manager] == [(2, 1, 2), (1, 2, 1), (2, 1, 2), (2, 2, 1)]
    assert [row["mean"] for row in manager] == [235, 81, 235, 270]
    assert report["panel_rows"][-4:] == manager
    assert report["batch_reads"]["after"] - report["batch_reads"]["before"] == 3
    assert report["pending_clear"]["rows"] == 0
    assert report["pending_clear"]["reads_before"] == report["pending_clear"]["reads_after"]
    assert report["navigated_rows"] == manager
    assert report["stale_rois_error"] and report["stale_rois_preserved"]
    assert report["ready_line_error"] and report["ready_line_preserved"]
    assert report["closed_count"] == 0 and report["closed_rows"] == manager
    assert report["jobs_reserved_after"] == 0 and report["workflow_count"] == 0
    with path.with_name("manager.csv").open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 6 and rows[0]["mean"] == "221.000" and rows[-1]["mean"] == "270.000"
    with path.with_name("closed.csv").open(newline="", encoding="utf-8") as f:
        assert len(list(csv.DictReader(f))) == 4


def test_scripted_line_and_manager_actions_keep_immediate_results(tmp_path):
    from imagejxx import _desktop
    _, owner, reads, _ = fixture()
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    editor.add_native_document(owner)
    editor.update_settings('{"profile_samples":11}', False)
    before, undo = owner.snapshot(), owner.undo_steps
    editor.action("measure", "{}")
    assert not editor.busy
    line = json.loads(editor.results)[0]
    assert line["count"] == 5 and line["mean"] == 105.5
    n = reads.counts().plane_reads
    editor.action("measure_rois", "{}")
    assert not editor.busy and reads.counts().plane_reads == n + 3
    assert len(json.loads(editor.results)) == 5
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo


def test_late_row_failure_never_appends_an_earlier_area_row(tmp_path):
    from imagejxx import _desktop
    from test_calibrated_analysis import density
    _, owner, _, _ = fixture()
    ij.set_imagej_intensity_calibration(owner, density("all_nan", "uint8"))
    before, undo = owner.snapshot(), owner.undo_steps
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    editor.add_native_document(owner)
    editor.results = '[{"previous":1}]'
    editor.action("measure_rois", "{}")
    assert editor.error and json.loads(editor.results) == [{"previous":1}]
    assert json.loads(editor.render_memory_info())["working_bytes"] == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo
