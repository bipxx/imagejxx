"""Pixel publication, immutable intent and explicit mixed workflow baselines."""
import copy
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_mixed_replay import wait_for_step


TARGET = dict(scope="current_plane", roi="whole_plane", mode="planes", destination="in_place")


def document(title="Edit source"):
    owner = ij.Document(ij.Dataset(np.array([[0, 255]], np.uint8), axes="YX"), title)
    owner.select(ij.Selection("line", [[0, 0], [1, 0]]))
    return owner


def pixels(owner):
    return owner.snapshot().dataset.numpy("YX")


def setup(tmp_path):
    owner, clone = document(), document("Edit clone")
    editor = Editor(preferences=tmp_path / "prefs.json")
    editor.add_native_document(owner)
    editor.add_native_document(clone)
    editor.active_index = 0
    editor.update_settings(measurements=["Area", "mean", "Length"], profile_samples=2, persist=True)
    editor.recording = True
    editor.apply_gaussian(ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE), ij.GaussianBlurCommandOptions(1), wait=True)
    np.testing.assert_array_equal(pixels(owner), [[77, 178]])
    editor.apply_operation("invert", asynchronous=False)
    for command in ["measure", "profile"]:
        editor.action(command)
        editor.wait()
    editor.action("export_display", path=str(tmp_path / "manual.png"))
    editor.recording = False
    assert len(editor.workflow) == 5 and owner.undo_steps == 2
    np.testing.assert_array_equal(pixels(owner), [[178, 77]])
    return owner, clone, editor


def run(editor, tmp_path, **changes):
    choices = dict(rebind=True, restore_settings=True, gaussian_rebind=TARGET,
                   export_options={"5": {"path": str(tmp_path / "replay.png")}})
    choices.update(changes)
    editor.action("run_workflow", **choices)


@pytest.mark.parametrize("mode", ["selected", "workspace"])
def test_edit_analysis_export_matches_manual_with_independent_baseline_and_history(mode, tmp_path):
    owner, clone, editor = setup(tmp_path)
    rows, steps, source = editor.results, editor.workflow, owner.snapshot()
    editor.action("export_plot_csv", path=str(tmp_path / "manual.csv"))
    curve = (tmp_path / "manual.csv").read_bytes()
    editor.action("save_workflow", path=str(tmp_path / "workflow.json"))
    editor.workflow = []
    editor.action("load_workflow", path=str(tmp_path / "workflow.json"))
    if mode == "workspace":
        editor.action("save_workspace", path=str(tmp_path / "workspace.ijx"))
        editor = Editor(preferences=tmp_path / "prefs.json", workspace=tmp_path / "workspace.ijx")
        clone = editor._editor.native_document(json.loads(editor._editor.document_infos())[1]["id"])
    editor.active_index = 1
    run(editor, tmp_path)
    editor.wait()
    assert editor.workflow_replay_info == dict(state="completed", completed=5, total=5, active_step=0, job_id=0, error="")
    np.testing.assert_array_equal(pixels(clone), [[178, 77]])
    assert clone.undo_steps == 2 and clone.redo_steps == 0 and editor.workflow == steps
    expected = copy.deepcopy(rows)
    for row in expected: row["Image"] = "Edit clone"
    assert editor.results == rows + expected
    assert (tmp_path / "replay.png").read_bytes() == (tmp_path / "manual.png").read_bytes()
    editor.action("export_plot_csv", path=str(tmp_path / "replay.csv"))
    assert (tmp_path / "replay.csv").read_bytes() == curve
    assert owner.undo_steps == 2 and owner.snapshot().shares_data(source)
    editor.action("undo")
    np.testing.assert_array_equal(pixels(clone), [[77, 178]])
    editor.action("undo")
    np.testing.assert_array_equal(pixels(clone), [[0, 255]])
    editor.action("redo")
    editor.action("redo")
    np.testing.assert_array_equal(pixels(clone), [[178, 77]])
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("mutation", ["baseline", "target-missing", "new-document", "range", "schema", "unknown-operation", "geometry", "parameter", "all-flag", "slice"])
def test_invalid_or_implicit_edit_intent_refuses_before_source_change(mutation, tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    steps, rows = editor.workflow, editor.results
    target, choices = dict(TARGET), {}
    if mutation == "baseline": choices["rebind"] = False
    if mutation == "target-missing": choices["gaussian_rebind"] = None
    if mutation == "new-document": target["destination"] = "new_document"
    if mutation == "range": target["scope"] = "ranges"
    if mutation == "schema": steps[0]["schema_version"] = 99
    if mutation == "unknown-operation": steps[1]["operation"] = "missing"
    if mutation == "geometry": steps[1] = dict(operation="resize", parameters={})
    if mutation == "parameter": steps[1]["parameters"]["missing"] = 1
    if mutation == "all-flag": steps[1]["all_slices"] = 1
    if mutation == "slice": steps[1]["slice_index"] = 99
    editor.workflow = steps
    choices.setdefault("gaussian_rebind", target)
    with pytest.raises((ValueError, RuntimeError)):
        run(editor, tmp_path, **choices)
        editor.wait()
    np.testing.assert_array_equal(pixels(clone), [[0, 255]])
    assert clone.undo_steps == 0 and editor.results == rows and editor.workflow == steps
    assert not (tmp_path / "replay.png").exists() and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("endpoint", ["cancel", "source-close", "pixels", "title", "selection", "active-owner", "workflow", "recording", "budget"])
def test_frozen_baseline_changes_cannot_publish_pending_first_edit(endpoint, tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    rows = copy.deepcopy(editor.results)
    run(editor, tmp_path)
    if endpoint == "cancel": editor.cancel()
    if endpoint == "source-close":
        with pytest.raises(ValueError, match="Wait for the current edit"):
            editor.close_document(discard=True)
        assert editor.active_index == 1 and editor.busy
        editor.cancel()
    if endpoint in ["pixels", "title"]:
        edit = clone.begin_edit()
        if endpoint == "pixels": edit.replace_dataset(ij.Dataset(np.array([[1, 254]], np.uint8), axes="YX"))
        else: edit.set_title("external")
        clone.commit(edit)
    if endpoint == "selection": clone.select(ij.Selection("rectangle", [[0, 0], [1, 1]]))
    if endpoint == "active-owner":
        with pytest.raises(ValueError, match="Wait for the current edit"):
            editor.active_index = 0
        assert editor.active_index == 1 and editor.busy
        editor.cancel()
    if endpoint == "workflow": editor.workflow = []
    if endpoint == "recording": editor.recording = True
    if endpoint == "budget": editor.update_settings(processing_memory_mb=128, persist=False)
    if endpoint in ["cancel", "source-close", "active-owner"]: editor.wait()
    else:
        with pytest.raises(RuntimeError): editor.wait()
    assert editor.workflow_replay_info["completed"] == 0
    assert editor.results == rows and editor.render_memory_info["working_bytes"] == 0
    np.testing.assert_array_equal(pixels(clone), [[1, 254]] if endpoint == "pixels" else [[0, 255]])
    if endpoint == "source-close": editor.close_document(discard=True)
    if endpoint == "active-owner": editor.active_index = 0


def test_later_failure_retains_committed_pixels_and_undo_history(tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    rows, steps = editor.results, editor.workflow
    run(editor, tmp_path)
    wait_for_step(editor, 2)
    editor.workflow = []
    with pytest.raises(RuntimeError): editor.wait()
    assert editor.workflow_replay_info["completed"] == 2 and editor.results == rows
    np.testing.assert_array_equal(pixels(clone), [[178, 77]])
    assert clone.undo_steps == 2 and editor.render_memory_info["working_bytes"] == 0
    editor.workflow = steps


def test_future_sampling_owner_is_frozen_before_first_pixel_publication(tmp_path):
    owner, clone, editor = setup(tmp_path)
    editor.active_index = 0
    editor.workflow = [editor.workflow[0]]
    editor.recording = True
    editor.action("measurement_redirect", document_id=clone.id)
    editor.action("measure")
    editor.wait()
    editor.recording = False
    editor.active_index = 1
    before = clone.snapshot()
    editor.action("run_workflow", rebind=True, restore_settings=True, gaussian_rebind=TARGET, redirect_id=owner.id)
    edit = owner.begin_edit()
    edit.set_title("changed future sampler")
    owner.commit(edit)
    with pytest.raises(RuntimeError): editor.wait()
    assert editor.workflow_replay_info["completed"] == 0 and clone.snapshot().shares_data(before)
    assert clone.undo_steps == 0 and editor.render_memory_info["working_bytes"] == 0


def test_copied_calibration_text_is_admitted_before_private_state(tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    editor.workflow = editor.workflow[1:3]
    calibration = ij.SpatialCalibration(time_unit="s" * 50000)
    edit = clone.begin_edit()
    edit.set_calibration(calibration)
    clone.commit(edit)
    history, rows = clone.undo_steps, editor.results
    editor.update_settings(processing_memory_mb=1, persist=False)
    with pytest.raises((ValueError, RuntimeError), match="budget|metadata|limit"):
        editor.action("run_workflow", rebind=True, restore_settings=True)
        editor.wait()
    np.testing.assert_array_equal(pixels(clone), [[0, 255]])
    assert clone.undo_steps == history and editor.results == rows and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("family", ["rank", "outliers", "top-hat", "remove-nans"])
def test_rank_envelopes_reuse_existing_pixel_workflow_dispatch(family, tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    if family == "rank": record = ij.resolve_rank_command(clone.snapshot(), ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE), ij.RankFilterCommandOptions()).record()
    if family == "outliers": record = ij.resolve_remove_outliers_command(clone.snapshot()).record()
    if family == "top-hat": record = ij.resolve_top_hat_command(clone.snapshot()).record()
    if family == "remove-nans":
        clone = ij.Document(ij.Dataset(np.array([[0, 255]], np.float32), axes="YX"), "Float source")
        clone.select(ij.Selection("line", [[0, 0], [1, 0]]))
        editor.add_native_document(clone)
        editor.active_index = 2
        record = ij.resolve_remove_nans_command(clone.snapshot()).record()
        editor.workflow = []
        editor.recording = True
        editor.action("measure")
        editor.wait()
        editor.recording = False
        measure = editor.workflow[0]
    else: measure = editor.workflow[2]
    editor.workflow = [record, measure]
    editor.action("run_workflow", rebind=True, restore_settings=True, rank_rebind=TARGET)
    editor.wait()
    assert editor.workflow_replay_info["state"] == "completed" and editor.workflow_replay_info["completed"] == 2
    assert editor.results[-1]["count"] == 2 and editor.render_memory_info["working_bytes"] == 0


def assert_host(report, context):
    assert report["contract"] == "edit-analysis-chain-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert report["pixels"] == [[178, 77]] and report["gaussian_pixels"] == [[77, 178]]
    assert report["status"]["state"] == "completed" and report["status"]["completed"] == 5
    assert report["clone_undo"] == 2 and report["history_roundtrip"] and report["png_equal"]
    assert report["intent_protected"] and report["default_refused"] and report["final_working_bytes"] == 0
    assert report["review"]["read_only"] and "Pixel edit" in report["review"]["text"]
    assert report["chosen_target"] == TARGET
    assert report["profile"]["series_x"] == [0, 1] and report["profile"]["series_y"] == [178, 77]
    assert report["profile"]["source_title"] == "Edit clone"
    assert report["cancel_status"]["state"] == "canceled" and report["cancel_status"]["completed"] == 0
    names = {x["name"] for x in report["controls"]}
    assert {"workflow.record", "workflow.save", "workflow.load", "workflow.clear", "workflow.run", "dialog.rebind", "dialog.gaussian_rebind", "dialog.apply", "dialog.cancel", "workflow.cancel_replay"} <= names


def test_python_actual_edit_chain(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    assert_host(json.loads(editor._edit_analysis_chain_ui_case(str(tmp_path / "actual"))), "imgui_python")


def test_standalone_actual_edit_chain():
    assert_host(json.loads(Path(os.environ["IMAGEJXX_EDIT_ANALYSIS_CPP_REPORT"]).read_bytes()), "imgui_cpp")
