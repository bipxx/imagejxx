"""Physical shared-host calibration integration; numerical reference gates remain separate."""
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


def source_pixels():
    t, c, z, y, x = np.indices((2, 2, 3, 4, 5))
    return (100*t + 30*c + 10*z + 5*y + x).astype(np.uint8)


def check_state(actual, expected, density=None):
    dataset = expected.dataset
    cal = dataset.calibration
    assert actual["shape"] == list(dataset.shape)
    for field in ("spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit"):
        assert actual[field] == getattr(cal, field)
    planes = dataset.numpy("TCZYXS").reshape(-1, dataset.shape[3]*dataset.shape[4]*dataset.shape[5])
    np.testing.assert_array_equal(actual["pixels"], planes)
    assert actual["density"] == (ij.read_imagej_intensity_calibration(expected).record if density is None else density)
    assert actual["undo"] == 0


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_calibrated_live_dialogs_and_workflow(context, tmp_path):
    if context == "imgui_cpp":
        report = json.loads(Path(os.environ["IMAGEJXX_CALIBRATED_COMMAND_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        report = json.loads(editor._calibrated_command_ui_case(str(tmp_path / "capture")))
    assert report["contract"] == "g034.calibrated-command.ui.001"
    assert report["context"] == context and report["state"] == "observed"
    assert not report["error"] and not report["acceptance_claim"]
    assert 0 < len(report["frames"]) <= 128
    for frame in report["frames"]:
        assert frame["events"] == frame["rendered"]["input_frame"]["raw_inputs"]
    operations = ("axis_plane_export", "axis_range_reslice", "axis_range_orientation",
                  "axis_range_spacing", "axis_range_slicer_resize", "project_imagej")
    assert [v["operation"] for v in report["results"]] == list(operations)
    controls = {v["name"] for v in report["controls"]}
    assert {op + ".apply" for op in operations} <= controls
    assert {"axis_range_orientation.transpose", "workflow.replay", "workflow.context_mismatch",
            "workflow.rebind", "workflow.rebind.apply", "bypass.apply", "stale_dialog.cancel", "queued.apply"} <= controls

    cal = ij.SpatialCalibration(spacing=[.5, 1.25, 2], origin=[11, -7, 19], unit="um",
                                frame_interval=.25, time_origin=8, time_unit="min")
    oracle = ij.Document(ij.Dataset(source_pixels(), axes="TCZYX", calibration=cal))
    ij.set_imagej_intensity_calibration(oracle, ij.ImageJIntensityCalibration.function(
        "uint8", "straight_line", [10, 2], "OD"))
    display = oracle.snapshot().display
    display.t = display.c = 1
    display.crosshair = [2, 1, 1]
    oracle.set_display(display)
    local_source_id = report["source"]["id"]
    for item in report["results"]:
        op, record, actual = item["operation"], item["record"], item["state"]
        ij.validate_calibrated_command_record(record)
        assert record["source"]["document_id"] == local_source_id != actual["id"]
        assert record["source"]["calibration"]["spacing"] == [3, 4, 5]
        assert record["effective"]["spatial"]["spacing"] == [.5, 1.25, 2]
        assert record["binding"] == "application" and not record["bypass"]
        r = record["descriptor"]["request"]
        if op == "project_imagej":
            assert r["first"] == 0 and r["last"] == 2 and r["method"] == "maximum"
            assert r["all_time_frames"] and r["all_channels"]
            request = r
            factory = "prepare_imagej_projection"
        elif op == "axis_plane_export":
            assert r["plane"].lower() == "xy" and r["coordinate"] == 1
            request = ij.AxisPlaneExportRequest("xy", 1, t=1, c=1)
            factory = "prepare_" + op
        else:
            assert r["plane"].lower() == "xz" and (r["first"], r["last"], r["step"]) == (0, 3, 1)
            request = ij.AxisRangeResliceRequest("xz", 0, 3, t=1, c=1)
            if op != "axis_range_reslice":
                assert not r["reverse_order"] and not r["flip_vertical"]
                assert r["transpose"] == (op == "axis_range_orientation")
                request = ij.AxisRangeOrientationRequest(request, transpose=op == "axis_range_orientation")
            if op == "axis_range_spacing":
                assert r["physical_vertical_spacing"] == 2 and r["sampling"] == "nearest"
                request = ij.AxisRangeSpacingRequest(request, physical_vertical_spacing=2,
                                                      sampling=ij.AxisRangeSampling.Nearest)
            elif op == "axis_range_slicer_resize":
                request = ij.AxisRangeSlicerResizeRequest(request)
            factory = "prepare_" + op
        if op != "project_imagej":
            assert (r["t"], r["c"]) == (1, 1)
        expected = getattr(ij, factory)(oracle.snapshot(), request).prepared_new_document().snapshot()
        check_state(actual, expected, None if op == "project_imagej" else
                    ij.read_imagej_intensity_calibration(oracle.snapshot()).record)
        if op == "axis_plane_export":
            np.testing.assert_array_equal(actual["pixels"], source_pixels()[1, 1, 1].reshape(1, -1))
        elif op == "axis_range_reslice":
            np.testing.assert_array_equal(actual["pixels"], source_pixels()[1, 1].transpose(1, 0, 2).reshape(4, -1))

    for field in ("shape", "pixels", "spacing", "origin", "density"):
        assert report["replayed"][field] == report["results"][0]["state"][field]
    mismatch = report["replay_mismatch"]
    assert mismatch["error"] and mismatch["before"] == mismatch["after"]
    rebound = report["rebound"]
    assert rebound["record"]["effective"]["spatial"]["spacing"] == [.75, 1.25, 2]
    assert rebound["state"]["spacing"] == [.75, 1.25, 2]
    assert set(report["rebind_dialogs"]) == {"spacing", "slicer"}
    bypass = report["bypassed"]
    assert bypass["record"]["bypass"] and bypass["record"]["effective"] is None
    assert bypass["state"]["spacing"] == [3, 4, 5]
    assert not report["stale_dialog"]["dialog"]["controls"]["apply"]["enabled"]
    queued = report["queued_stale"]
    assert queued["error"] and queued["documents_before"] == queued["documents_after"]
    assert queued["workflow_before"] == queued["workflow_after"] and queued["reserved_bytes"] == 0
    for actual in (report["source"], report["reopened"]["state"]):
        assert actual["spacing"] == [3, 4, 5] and actual["undo"] == 0
        np.testing.assert_array_equal(actual["pixels"], source_pixels().reshape(12, 20))
    assert len(report["reopened"]["workflow"]) == 9
    for record in report["reopened"]["workflow"]:
        ij.validate_calibrated_command_record(record)
