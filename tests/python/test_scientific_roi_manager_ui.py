"""Literal independent assertions over actual shared-ImGui manager inputs.

Private setup/navigation/session calls are disclosed; this is not an OS-input
test. C++ reports come from the matching standalone executable, Python reports
from the optional native desktop module. Neither substitutes for the other.
"""
import json
import os
from pathlib import Path

import pytest


def assert_observed(report, context):
    assert report["contract"] == "p06k.scientific-roi-manager.ui.001"
    assert report["state"] == "observed" and report["context"] == context
    assert report["acceptance_claim"] is False
    assert 0 < len(report["frames"]) < 160
    states = report["states"]
    assert set(states) == {"initial", "created", "stored", "styled", "reselected", "edited", "reopened"}
    pixels = [[10000*t + 1000*c + 100*z + 32*y + x for y in range(24) for x in range(32)]
              for t in range(2) for c in range(2) for z in range(3)]
    for state in states.values():
        assert state["shape"] == [2, 2, 3, 24, 32, 1]
        assert state["pixels"] == pixels
        assert state["extensions"] == {"kept": [True, 7, "opaque"]}
    assert states["initial"]["selection"] is None
    created = states["created"]
    assert created["selection"]["kind"] == "rectangle"
    assert created["selection"]["points"] == [[4., 4.], [14., 12.]]
    assert created["rois"] == [] and created["undo_steps"] == 0 and created["dirty"] is False
    stored = states["stored"]
    assert len(stored["rois"]) == 1 and stored["undo_steps"] == 1
    row = stored["rois"][0]
    assert (row["t"], row["c"], row["z"]) == (1, 1, 2)
    assert row["selection"]["points"] == [[4., 4.], [14., 12.]]
    styled = states["styled"]
    assert styled["undo_steps"] == 2 and styled["rois"][0]["name"] == "Managed area"
    assert styled["rois"][0]["selection"]["imagejxx.roi_style"]["stroke_color"] == [255, 0, 0, 255]
    reselected = states["reselected"]
    assert reselected["tcz"] == [1, 1, 2]
    assert reselected["undo_steps"] == 2 and reselected["rois"] == styled["rois"]
    edited = states["edited"]
    assert edited["undo_steps"] == 3
    assert edited["rois"][0]["selection"]["points"] == [[6., 6.], [16., 14.]]
    assert (edited["rois"][0]["t"], edited["rois"][0]["c"], edited["rois"][0]["z"]) == (1, 1, 2)
    assert states["reopened"]["rois"] == edited["rois"] and states["reopened"]["tcz"] == edited["tcz"]
    names = {item["name"] for item in report["controls"]}
    assert names >= {"manager.create_roi", "manager.add_roi", "manager.roi_properties", "properties.name",
                     "properties.stroke_color", "properties.apply", "manager.row.0", "manager.row.0.edit_saved_area"}
    for frame in report["frames"]:
        assert frame["events"] == frame["rendered"]["input_frame"]["raw_inputs"]
    for item in report["controls"]:
        assert item["control"]["enabled"] is True and item["control"]["id"] > 0


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_scientific_roi_manager_actual_input(context, tmp_path):
    if context == "imgui_cpp":
        report = json.loads(Path(os.environ["IMAGEJXX_ROI_MANAGER_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        report = json.loads(editor._scientific_roi_manager_ui_case(str(tmp_path / "capture")))
    assert_observed(report, context)
