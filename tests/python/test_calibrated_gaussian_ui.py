"""Shared physical Gaussian UI integration; numerical reference tests remain separate."""
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


def initial(mode):
    return np.broadcast_to(np.array([0, 255], np.uint8),
                           (1, 1, 3, 1, 2) if mode == "stack" else (2, 2, 3, 1, 2)).copy()


def check_source(state, pixels, undo=None):
    assert state["shape"] == list(pixels.shape) + [1]
    assert state["spacing"] == [3, 4, 5] and state["unit"] == "px"
    assert state["density"]["mode"] == "none" and state["density"]["value_unit"] == "Gray Value"
    np.testing.assert_array_equal(state["pixels"], pixels.reshape(-1, 2))
    if undo is not None:
        assert state["undo"] == undo


def check_record(record, *, width=.5, bypass=False, local=False, scope="positions", ranges=None):
    ij.validate_calibrated_command_record(record)
    assert record["operation"] == "gaussian_blur_imagej" and record["binding"] == "application"
    assert record["bypass"] == bypass
    assert record["source"]["calibration"]["spacing"] == [3, 4, 5]
    if bypass or local:
        assert record["effective"] is None
    else:
        assert record["effective"]["spatial"]["spacing"] == [width, 1.25, 2]
        assert record["effective"]["intensity"]["value_unit"] == "OD"
    target = record["descriptor"]["command"]["target"]
    assert target["scope"] == scope
    assert target["ranges"] == ([] if scope == "positions" else
                                 [[1, 2, 1], [1, 2, 1], [1, 2, 1]] if ranges is None else ranges)
    assert target["positions"] == ([[1, 1, 1]] if scope == "positions" else [])
    assert target["destination"] == "in_place"


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
@pytest.mark.parametrize("mode", ["flow", "stale", "stack"])
def test_calibrated_gaussian_controls_and_jobs(context, mode, tmp_path):
    if context == "imgui_cpp":
        report = json.loads(Path(os.environ[
            "IMAGEJXX_CALIBRATED_GAUSSIAN_" + mode.upper() + "_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        report = json.loads(editor._calibrated_gaussian_ui_case(mode, str(tmp_path / "capture")))
    assert report["contract"] == "g034.calibrated-gaussian.ui.001"
    assert report["context"] == context and report["mode"] == mode
    assert report["state"] == "observed" and not report["acceptance_claim"] and not report["error"]
    assert 0 < len(report["frames"]) <= 128
    for frame in report["frames"]:
        assert frame["events"] == frame["rendered"]["input_frame"]["raw_inputs"]
    controls = {v["name"] for v in report["controls"]}
    pixels = initial(mode)
    check_source(report["before"], pixels, 0)
    if mode == "flow":
        assert {"sigma", "scaled", "preview", "preview.cancel", "workflow.replay", "workflow.stale_source",
                "workflow.rebind", "rebind.apply", "bypass.apply"} <= controls
        panel = report["configured"]
        assert panel["sigma"] == .5 and panel["scaled_units"] and panel["scaled_available"]
        assert panel["calibration_unit"] == "um" and panel["calibration_current"]
        lane = report["preview_jobs"]["preview"]
        record = lane["command_record"]
        check_record(record)
        assert record["descriptor"]["command"]["effective_sigma_xy"] == [1, .4]
        assert lane["job"]["resource_bytes"] >= 1024**2
        assert report["preview_source"]["workflow_count"] == 0
        check_source(report["preview_source"], pixels, 0)
        pixels[1, 1, 1] = [[77, 178]]  # Independent owned two-pixel analytic expectation.
        check_source(report["preview_pixels"], pixels)
        check_source(report["replayed"]["state"], pixels, 1)
        assert 0 < report["replay_memory_budget"] < record["limits"]["max_working_bytes"]
        assert report["replayed"]["jobs"]["apply"]["job"]["resource_bytes"] <= report["replay_memory_budget"]
        assert report["replayed"]["record"] == record  # Frozen limits/provenance survive strict replay.
        assert report["mismatch"]["error"]
        check_source(report["mismatch"]["state"], pixels, 1)
        panel = report["rebind_panel"]
        assert panel["rebind"] and panel["sigma"] == .5 and panel["scaled_units"]
        assert panel["calibration_unit"] == "um" and panel["calibration_current"]
        rebound = report["rebound"]
        check_record(rebound["record"], width=.75, scope="ranges")
        assert rebound["record"]["descriptor"]["command"]["effective_sigma_xy"] == [2/3, .4]
        # Existing raw kernel is the integration oracle for the second/third edits.
        pixels[1, 1, 1] = ij.gaussian_blur_imagej(pixels[1, 1, 1], (.4, 2/3))
        check_source(rebound["state"], pixels, 2)
        panel = report["bypass_panel"]
        assert not panel["scaled_available"] and not panel["scaled_units"] and panel["calibration_unit"] == "px"
        bypass = report["bypassed"]
        check_record(bypass["record"], bypass=True, scope="ranges")
        assert bypass["record"]["descriptor"]["command"]["options"] == {"sigma": .5, "scaled_units": False}
        pixels[1, 1, 1] = ij.gaussian_blur_imagej(pixels[1, 1, 1], .5)
        check_source(bypass["state"], pixels, 3)
        assert bypass["state"]["workflow_count"] == 4 and bypass["state"]["document_count"] == 1
        local = report["local_rebound"]
        check_record(local["record"], local=True, scope="ranges")
        pixels[1, 1, 1] = ij.gaussian_blur_imagej(pixels[1, 1, 1], .5)
        check_source(local["state"], pixels, 4)
        ordinary = report["ordinary_local"]
        assert "schema" not in ordinary["record"] and ordinary["record"]["schema_version"] == 1
        ij.validate_gaussian_command_record(ordinary["record"])
        assert ordinary["jobs"]["apply"]["job"]["resource_bytes"] < 1024**2
        pixels[1, 1, 1] = ij.gaussian_blur_imagej(pixels[1, 1, 1], .5)
        check_source(ordinary["state"], pixels, 5)
        check_source(report["reopened"]["state"], pixels)
        assert len(report["reopened"]["workflow"]) == 6
        for saved in report["reopened"]["workflow"]:
            if saved.get("schema") == "imagejxx.calibrated-command":
                ij.validate_calibrated_command_record(saved)
            else:
                ij.validate_gaussian_command_record(saved)
    elif mode == "stack":
        assert {"sigma", "scaled", "stack.initial.ok", "stack.stale.cancel", "stack.no", "stack.yes"} <= controls
        stale = report["stale_stack"]
        assert stale["stack_prompt"] and not stale["calibration_current"] and stale["calibration_error"]
        assert not stale["control_states"]["stack_yes"]["enabled"]
        assert not stale["control_states"]["stack_no"]["enabled"]
        one = report["stack_no"]
        check_record(one["record"], width=.75, scope="ranges", ranges=[[0, 1, 1], [0, 1, 1], [1, 2, 1]])
        pixels[0, 0, 1] = ij.gaussian_blur_imagej(pixels[0, 0, 1], (.4, 2/3))
        check_source(one["state"], pixels, 1)
        all_z = report["stack_yes"]
        check_record(all_z["record"], scope="ranges", ranges=[[0, 1, 1], [0, 1, 1], [0, 3, 1]])
        for z in range(3):
            pixels[0, 0, z] = ij.gaussian_blur_imagej(pixels[0, 0, z], (.4, 1))
        np.testing.assert_array_equal(pixels[0, 0, 0], [[77, 178]])
        check_source(all_z["state"], pixels, 2)
        assert all_z["state"]["workflow_count"] == 2 and all_z["state"]["document_count"] == 1
    else:
        assert {"ready.preview", "stale.cancel", "local.queued.apply"} <= controls
        assert report["ready_before"]["active_installed_generation"] is not None
        assert report["ready_before"]["resource_usage"]["reserved_bytes"] >= 1024**2
        stale = report["stale_panel"]
        assert not stale["calibration_current"] and stale["calibration_error"]
        assert not stale["control_states"]["ok"]["enabled"]
        assert not stale["control_states"]["preview"]["enabled"]
        assert report["ready_after"]["active_installed_generation"] is None
        assert report["ready_after"]["resource_usage"]["reserved_bytes"] == 0
        queued = report["queued_stale"]
        assert queued["error"] and queued["reserved_bytes"] == 0
        check_source(queued["state"], pixels, 0)
        assert queued["state"]["workflow_count"] == 0 and queued["state"]["document_count"] == 1
        legacy = queued["jobs"]["apply"]["command_record"]
        assert "schema" not in legacy and legacy["schema_version"] == 1
        ij.validate_gaussian_command_record(legacy)
