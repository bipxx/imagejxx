"""Actual Measure controls consume Analyzer path and refreshed angle contracts."""
import csv
import json
import os
from pathlib import Path

import pytest


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_path_angle_and_mixed_manager_controls(context, tmp_path):
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_CAPTURED_PATH_MEASUREMENT_CPP_REPORT"])
        report = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        path = tmp_path / "capture/case.json"
        report = json.loads(editor._captured_path_measurement_ui_case(str(path.parent)))
    assert report["contract"] == "g066.captured-path-measurement.ui.001"
    assert report["state"] == "observed" and report["context"] == context
    assert not report["acceptance_claim"] and not report["default_reflex"]
    assert report["path_source_unchanged"] and report["angle_source_unchanged"]
    assert 0 < len(report["frames"]) < 96
    for frame in report["frames"]:
        assert frame["events"] == frame["input_frame"]["raw_inputs"]
    controls = [row["name"] for row in report["controls"]]
    assert controls.count("menu.action.measure") == 4
    assert "menu.action.measure_rois" in controls and "manager.measure_rois" in controls
    for row in report["controls"]:
        assert row["control"]["enabled"] and row["control"]["id"] > 0
    row = report["path"]
    assert row["count"] == 3 and row["mean"] == 5 and row["Area"] == 3
    assert row["Length"] == 5 and row["Bounds"] == [0, 0, 3, 3]
    assert row["Unit"] == "um" and row["X"] == row["Y"] == 0
    assert report["angle"]["Angle"] == pytest.approx(53.13010235415598)
    assert report["reflex_angle"]["Angle"] == pytest.approx(306.86989764584405)
    assert report["angle"]["Bounds"] == [0, 0, 5, 2]
    assert report["angle_reads"]["before"] == report["angle_reads"]["after"]
    manager = report["menu_rows"][-3:]
    assert [r["ROI"] for r in manager] == ["path", "angle", "area"]
    assert manager[0]["mean"] == 5 and manager[1]["Angle"] == report["reflex_angle"]["Angle"]
    assert manager[2]["mean"] == 5.5 and manager[2]["count"] == 4
    assert report["panel_rows"][-3:] == manager
    assert len(report["panel_rows"]) == len(report["menu_rows"]) + 3
    assert report["batch_reads"]["after"] - report["batch_reads"]["before"] == 1
    assert report["jobs_reserved_after"] == report["workflow_count"] == 0
    with path.with_name("paths.csv").open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 13 and rows[0]["mean"] == "5.000"
    assert report["preferences"]["values"]["reflex_angle"] is True
    assert report["persisted_reflex"] is True
    assert report["point_source_unchanged"]
    points = report["point_rows"]
    assert [r["Point index"] for r in points] == [0, 1, 2]
    assert [r["mean"] for r in points] == [11, 22, 11]
    assert [r["Area"] for r in points] == [0, 0, 0]
    assert [r["integrated_density"] for r in points] == [11, 22, 11]
    assert [r["X"] for r in points] == [.25, 1.125, .25]
    selected = report["selected_rows"]
    assert len(selected) == report["selected_before_count"] + 4
    assert [r["ROI index"] for r in selected[-4:]] == [0, 0, 0, 2]
    assert "manager.measure_selected" in controls
    with path.with_name("points.csv").open(newline="", encoding="utf-8") as f:
        exported = list(csv.DictReader(f))
    assert len(exported) == len(selected)
    assert [r["Point index"] for r in exported[-4:]] == ["0", "1", "2", ""]
    assert exported[-4]["X"] == "0.250" and exported[-4]["Area"] == "0.000"
