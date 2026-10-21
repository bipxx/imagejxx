"""Independent expectations over both actual shared-ImGui threshold hosts."""
import json
import os
from pathlib import Path

import pytest


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_scientific_threshold_actual_inputs(context, tmp_path):
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_THRESHOLD_CPP_REPORT"])
        assert path.is_file() and path.stat().st_size <= 16*1024*1024
        report = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        report = json.loads(editor._manual_threshold_ui_case("T10", context, str(tmp_path / "capture")))
    assert report["contract_id"] == "p06l.scientific-manual-threshold.ui.001"
    assert report["context"] == context and report["observation_state"] == "observed"
    assert not report["window_error"] and report["acceptance_claim"] is False
    assert 0 < report["frame_count"] < 180
    for frame in report["frames"]:
        assert frame["requested_events"] == frame["rendered"]["input_frame"]["raw_inputs"]
    assert {"menu.Process", "menu.manual_threshold", "threshold.lower", "threshold.upper",
            "threshold.set", "threshold.apply"} <= {
                control["selector"] for control in report["control_observations"]}
    states = report["states"]
    before = states["before"]["source"]
    pixels = [[10000*t+1000*c+100*z+10*y+x for y in range(9) for x in range(9)]
              for t in range(2) for c in range(2) for z in range(3)]
    assert before["shape"] == [2, 2, 3, 9, 9, 1] and before["pixels"] == pixels
    overlay = states["overlay_set"]["source"]
    assert overlay["pixels"] == pixels and overlay["undo_steps"] == before["undo_steps"]
    assert overlay["display"]["properties"]["manual_threshold"] == {
        "version": 1, "lower": 11212, "upper": 11253, "t": 1, "c": 1, "z": 2}
    navigated = states["navigated"]["source"]
    assert (navigated["display"]["t"], navigated["display"]["c"], navigated["display"]["crosshair"][2]) == (0, 0, 0)
    expected = [plane.copy() for plane in pixels]
    rows = ("000000000", "011111000", "010001000", "010101000", "010001000",
            "011111000", "000000010", "000000000", "000000000")
    for y, row in enumerate(rows):
        for x, bit in enumerate(row):
            if bit == "1":
                expected[11][9*y+x] = 255 if 11212 <= pixels[11][9*y+x] <= 11253 else 0
    applied = states["applied"]["source"]
    assert applied["dtype"] == "uint16" and applied["pixels"] == expected
    assert applied["selection"] == before["selection"]
    assert applied["undo_steps"] == before["undo_steps"]+1
    assert "manual_threshold" not in applied["display"]["properties"]
    record = states["applied"]["workflow"][0]
    assert record["profile"] == "manual_raw_inclusive_v1"
    assert record["target"]["positions"] == [[1, 1, 2]]
    assert states["undone"]["source"]["pixels"] == pixels
    assert states["undone"]["source"]["undo_steps"] == before["undo_steps"]
    rebound = [plane.copy() for plane in pixels]
    for y, row in enumerate(rows):
        for x, bit in enumerate(row):
            if bit == "1": rebound[0][9*y+x] = 0
    assert states["rebound"]["source"]["pixels"] == rebound
    assert states["rebound"]["workflow"][1]["target"]["positions"] == [[0, 0, 0]]
    assert states["rebound_undone"]["source"]["pixels"] == pixels
    assert "workflow.manual_threshold.0.rebind" in {c["selector"] for c in report["control_observations"]}
