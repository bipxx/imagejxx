"""Actual configured calibrated profiles and retained-result lifecycle."""
import csv
import json
import os
from pathlib import Path

import numpy as np
import pytest


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_profile_menu_calibration_configuration_and_lifecycle(context, tmp_path):
    if context == "imgui_cpp":
        path = Path(os.environ["IMAGEJXX_CAPTURED_PROFILE_CPP_REPORT"])
        report = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        path = tmp_path / "capture/case.json"
        report = json.loads(editor._captured_profile_ui_case(str(path.parent)))
    assert report["contract"] == "g034.captured-profile.ui.001"
    assert report["state"] == "observed" and report["context"] == context
    assert not report["acceptance_claim"] and report["source_unchanged"]
    assert 0 < len(report["frames"]) < 96
    for frame in report["frames"]:
        assert frame["events"] == frame["input_frame"]["raw_inputs"]
    assert [row["name"] for row in report["controls"]] == [
        "menu.Analyze", "menu.action.profile", "plot.snapshot",
        "menu.Analyze", "menu.action.profile"]
    for row in report["controls"]:
        assert row["control"]["enabled"] and row["control"]["id"] > 0
    global_plot = report["global"]
    assert global_plot["series_x"] == [0, .5, 1, 2, 3]
    assert global_plot["series_y"] == [0, 1, 4, 14.5, 25]
    assert global_plot["x_label"] == "Distance (um)"
    assert global_plot["y_label"] == "Pixel value (sq)"
    assert report["panned"]["payload_id"] == global_plot["payload_id"]
    assert report["changed_global"] is None and report["changed_plane"] is None
    assert report["bypassed"]["series_x"] == [0, 3, 6, 8, 10]
    assert report["bypassed"]["series_y"] == [0, 1, 2, 3.5, 5]
    configured = report["configured"]
    assert configured["series_x"] == [0, 1.5, 3, 4.5, 6, 7, 8, 9, 10]
    assert configured["series_y"] == [10, 10.5, 11, 11.5, 12, 12.75, 13.5, 14.25, 15]
    assert configured["y_label"] == "Pixel value (raw)" and report["persisted_samples"] == 5
    assert report["bypassed_global_change"]["payload_id"] == configured["payload_id"]
    with path.with_name("profile.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    np.testing.assert_array_equal([float(row["x"]) for row in rows], configured["series_x"])
    np.testing.assert_array_equal([float(row["y"]) for row in rows], configured["series_y"])
    assert report["queued_cancel"]["queued"]
    assert report["queued_cancel"]["reads_before"] == report["queued_cancel"]["reads_after"]
    assert report["ready_stale_error"] and report["ready_stale_plot"] is None
    assert report["closed_count"] == 0 and report["jobs_reserved_after"] == 0
    for key in ["saved_after_global", "saved_after_close"]:
        saved = next(row["plot"] for row in report[key] if row["plot_id"] == report["snapshot_id"])
        assert saved["series_x"] == global_plot["series_x"]
        assert saved["series_y"] == global_plot["series_y"]
