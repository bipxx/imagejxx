"""Mixed analysis recorder sequences use existing creator-owned job publication."""
import copy
import csv
import io
import json
import os
from pathlib import Path
import time

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


def document(offset=0):
    pixels = np.arange(6, dtype=np.uint8).reshape(2, 3) + offset
    owner = ij.Document(ij.Dataset(np.stack([pixels, pixels + 10]), axes="ZYX",
        calibration=ij.SpatialCalibration(spacing=[3, 4, 1], unit="local")), "Mixed source")
    line = ij.Selection("polyline", [[0, 0], [2, 0], [2, 1]])
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("area", ij.Selection("rectangle", [[0, 0], [2, 1]])), ij.ROI("path", line)])
    owner.commit(edit)
    owner.select(line)
    view = owner.snapshot().display
    view.crosshair = [1, 1, 1]
    owner.set_display(view)
    return owner


def setup(tmp_path):
    owner = document()
    editor = Editor(preferences=tmp_path / "prefs.json")
    editor.add_native_document(owner)
    editor.update_settings(measurements=["Area", "mean", "Length", "Label"], profile_samples=3, persist=False)
    editor.recording = True
    for command in ["measure", "measure_rois", "profile"]:
        editor.action(command)
        editor.wait()
    editor.recording = False
    assert [step["schema"] for step in editor.workflow] == ["imagejxx.measurement-command", "imagejxx.measurement-command", "imagejxx.profile-command"]
    assert [step["version"] for step in editor.workflow] == [1, 2, 1]
    assert len(editor.results) == 3
    return owner, editor


def profile_csv(editor, path):
    editor.action("export_plot_csv", path=str(path))
    return path.read_bytes()


def wait_for_step(editor, completed):
    deadline = time.monotonic() + 10
    while editor.workflow_replay_info["completed"] < completed and time.monotonic() < deadline:
        assert editor.workflow_replay_info["state"] == "running"
        editor.poll()
    assert editor.workflow_replay_info["completed"] == completed
    assert editor.workflow_replay_info["active_step"] == completed + 1


@pytest.mark.parametrize("restore", [False, True])
def test_default_sequence_matches_manual_and_preserves_intent(restore, tmp_path):
    owner, editor = setup(tmp_path)
    rows, steps = copy.deepcopy(editor.results), copy.deepcopy(editor.workflow)
    curve = profile_csv(editor, tmp_path / "before.csv")
    source, history = owner.snapshot(), owner.undo_steps
    editor.action("run_workflow", restore_settings=restore)
    started = editor.workflow_replay_info
    assert editor.busy and started["state"] == "running" and started["active_step"] == 1
    editor.wait()
    assert editor.workflow_replay_info == dict(state="completed", completed=3, total=3, active_step=0, job_id=0, error="")
    assert editor.results == rows + rows and editor.workflow == steps
    assert profile_csv(editor, tmp_path / "after.csv") == curve
    assert owner.snapshot().shares_data(source) and owner.undo_steps == history
    assert not editor.busy and editor.render_memory_info["working_bytes"] == 0


def test_save_load_explicit_rebind_settings_and_workspace(tmp_path):
    owner, editor = setup(tmp_path)
    rows, steps = copy.deepcopy(editor.results), copy.deepcopy(editor.workflow)
    curve = profile_csv(editor, tmp_path / "manual.csv")
    workflow = tmp_path / "workflow.json"
    editor.action("save_workflow", path=str(workflow))
    editor.workflow = []
    editor.action("load_workflow", path=str(workflow))
    clone = document()
    editor.add_native_document(clone)
    editor.active_index = 1
    editor.update_settings(profile_samples=5, measurements=["mean"], persist=False)
    with pytest.raises((ValueError, RuntimeError), match="settings|sampling"):
        editor.action("run_workflow", rebind=True, restore_settings=False)
    assert editor.results == rows
    editor.action("run_workflow", rebind=True, restore_settings=True)
    editor.wait()
    assert editor.workflow == steps and editor.settings["profile_samples"] == 3
    assert profile_csv(editor, tmp_path / "clone.csv") == curve
    for actual, expected in zip(editor.results[-3:], rows):
        for name in ["Area", "mean", "Length", "Label"]:
            assert actual.get(name) == expected.get(name)
    workspace = tmp_path / "workspace.ijx"
    editor.action("save_workspace", path=str(workspace))
    reopened = Editor(preferences=tmp_path / "reopened.json", workspace=workspace)
    reopened.action("run_workflow", rebind=True, restore_settings=True)
    reopened.wait()
    assert reopened.workflow == steps and reopened.workflow_replay_info["completed"] == 3
    assert profile_csv(reopened, tmp_path / "reopened.csv") == curve


@pytest.mark.parametrize("mutation", ["unknown-family", "version", "boolean-setting", "too-many"])
def test_known_incompatibility_refuses_before_any_publication(mutation, tmp_path):
    owner, editor = setup(tmp_path)
    steps = copy.deepcopy(editor.workflow)
    if mutation == "unknown-family": steps.append({"operation": "invert"})
    if mutation == "version": steps[1]["version"] = 99
    if mutation == "boolean-setting": steps[2]["profile_samples"] = True
    if mutation == "too-many": steps = steps * 86
    editor.workflow = steps
    rows, source, history = copy.deepcopy(editor.results), owner.snapshot(), owner.undo_steps
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("run_workflow", restore_settings=True)
    assert editor.results == rows and editor.workflow == steps
    assert owner.snapshot().shares_data(source) and owner.undo_steps == history
    assert not editor.busy and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("endpoint", ["cancel", "workflow", "recording"])
def test_first_step_terminal_endpoint_publishes_nothing(endpoint, tmp_path):
    owner, editor = setup(tmp_path)
    rows, source, history = copy.deepcopy(editor.results), owner.snapshot(), owner.undo_steps
    editor.action("run_workflow", restore_settings=True)
    if endpoint == "cancel": editor.cancel()
    if endpoint == "workflow": editor.workflow = []
    if endpoint == "recording": editor.recording = True
    if endpoint == "cancel": editor.wait()
    else:
        with pytest.raises(RuntimeError, match="changed"):
            editor.wait()
    status = editor.workflow_replay_info
    assert status["state"] == ("canceled" if endpoint == "cancel" else "failed")
    assert status["completed"] == 0 and status["active_step"] == 1
    assert editor.results == rows and owner.snapshot().shares_data(source) and owner.undo_steps == history
    assert editor.render_memory_info["working_bytes"] == 0 and not editor.busy


@pytest.mark.parametrize("endpoint", ["cancel", "workflow", "sampling", "source-close"])
def test_later_failure_preserves_preceding_success(endpoint, tmp_path):
    owner, editor = setup(tmp_path)
    rows = copy.deepcopy(editor.results)
    completed = 2 if endpoint == "sampling" else 1
    editor.action("run_workflow", restore_settings=True)
    wait_for_step(editor, completed)
    committed = copy.deepcopy(editor.results)
    assert len(committed) == len(rows) + (3 if completed == 2 else 1)
    if endpoint == "cancel": editor.cancel()
    if endpoint == "workflow": editor.workflow = []
    if endpoint == "sampling": editor.update_settings(profile_samples=5, persist=False)
    if endpoint == "source-close": editor.close_document(discard=True)
    if endpoint in ["cancel", "source-close"]: editor.wait()
    else:
        with pytest.raises(RuntimeError): editor.wait()
    assert editor.workflow_replay_info["completed"] == completed
    assert editor.workflow_replay_info["state"] in ["canceled", "failed"]
    assert editor.results == committed and not editor.busy
    assert editor.render_memory_info["working_bytes"] == 0


def test_recording_must_stop_before_run(tmp_path):
    _, editor = setup(tmp_path)
    before = copy.deepcopy(editor.workflow)
    editor.recording = True
    with pytest.raises((ValueError, RuntimeError), match="Stop recording"):
        editor.action("run_workflow", restore_settings=True)
    assert editor.workflow == before and not editor.busy


def assert_actual_report(report, context):
    assert report["contract"] == "ordered-mixed-replay-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["error"] and not report["acceptance_claim"]
    assert report["review_summary"]["read_only"] is True
    summary = report["review_summary"]["text"]
    assert summary.index("1. Measure") < summary.index("2. Measure") < summary.index("3. Profile")
    assert "samples per segment: 3" in summary
    assert "ROI Manager entries" in summary and "area" in summary and "path" in summary
    manual = report["manual_rows"]
    assert len(manual) == 3 and report["default_rows"] == manual + manual
    assert all(row["Redirect document"] == report["source_id"] for row in manual)
    rebound = copy.deepcopy(manual)
    for row in rebound:
        row["Redirect document"] = report["clone_id"]
        row["Image"] = row["Redirect image"] = "Mixed clone"
    assert report["rebind_rows"] == manual + manual + rebound
    assert report["later_rows"] == manual + manual + rebound + rebound
    owners = report["rebound_owners"]
    assert owners["source"] == owners["sampling"] == report["clone_id"]
    assert owners["choice"] == "2. Mixed clone" and owners["control"]["enabled"] is True
    assert any(item["name"] == "dialog.sampling.choice." + owners["choice"] and item["control"] == owners["control"] for item in report["controls"])

    for key in ["default_status", "rebind_status"]:
        assert report[key] == dict(state="completed", completed=3, total=3, active_step=0, job_id=0, error="")
    assert report["cancel_status"]["state"] == "canceled" and report["cancel_status"]["completed"] == 0
    assert report["cancel_status"]["active_step"] == 1
    assert report["later_failure_status"]["state"] == "failed" and report["later_failure_status"]["completed"] == 2
    assert report["later_failure_status"]["active_step"] == 3 and report["later_failure_status"]["error"]
    assert report["busy_record_disabled"] is True and report["review_stale_refused"] is True
    assert report["source_history_protected"] is True and report["final_working_bytes"] == 0
    plot = report["manual_plot"]
    assert plot["series_x"] == [0, 3, 6, 8, 10]
    assert plot["series_y"] == [10, 11, 12, 13.5, 15]
    assert report["rebound_plot"]["series_x"] == plot["series_x"]
    assert report["rebound_plot"]["series_y"] == plot["series_y"]
    assert report["rebound_plot"]["source_title"] == "Mixed clone"
    assert report["rebound_plot"]["source_id"] == report["clone_id"] and plot["source_id"] == report["source_id"]
    assert [step["schema"] for step in report["workflow"]] == ["imagejxx.measurement-command", "imagejxx.measurement-command", "imagejxx.profile-command"]
    assert report["results_csv"] == report["copied_results_csv"]
    exported = list(csv.DictReader(io.StringIO(report["results_csv"])))
    assert len(exported) == 12 and report["results_table"]["cells"]
    for cell in report["results_table"]["cells"]:
        assert cell["text"] == exported[cell["row"]][cell["name"]]
    assert report["workspace_roundtrip"] is True and report["reopened_status"]["state"] == "completed"
    names = {row["name"] for row in report["controls"]}
    assert {"workflow.record", "workflow.save", "workflow.load", "workflow.clear", "workflow.run", "workflow.cancel_replay", "dialog.cancel", "dialog.rebind", "dialog.restore_settings", "dialog.apply"} <= names
    assert all(row["control"]["enabled"] and row["control"]["id"] for row in report["controls"])


def test_actual_python_mixed_replay(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    assert_actual_report(json.loads(editor._mixed_replay_ui_case(str(tmp_path / "actual"))), "imgui_python")


def test_standalone_mixed_replay_report():
    assert_actual_report(json.loads(Path(os.environ["IMAGEJXX_MIXED_REPLAY_CPP_REPORT"]).read_bytes()), "imgui_cpp")


@pytest.mark.parametrize("sampling", ["self", "foreign"])
def test_mixed_redirect_bindings_and_unredirected_profile(sampling, tmp_path):
    source, editor = setup(tmp_path)
    sample = source if sampling == "self" else document(100)
    if sample is not source:
        editor.add_native_document(sample)
        editor.active_index = 0
    editor.workflow, editor.results = [], []
    editor.action("measurement_redirect", document_id=sample.id)
    editor.recording = True
    for command in ["measure", "measure_rois", "profile"]:
        editor.action(command)
        editor.wait()
    editor.recording = False
    manual, steps = copy.deepcopy(editor.results), copy.deepcopy(editor.workflow)
    curve = profile_csv(editor, tmp_path / "manual-redirect.csv")
    assert all(row["Redirect document"] == sample.id for row in manual)
    editor.action("run_workflow", restore_settings=True)
    editor.wait()
    assert editor.results == manual + manual
    clone = document()
    editor.add_native_document(clone)
    editor.active_index = len(editor.documents) - 1
    with pytest.raises((ValueError, RuntimeError), match="sampling"):
        editor.action("run_workflow", rebind=True, restore_settings=True)
    assert editor.results == manual + manual
    editor.action("run_workflow", rebind=True, restore_settings=True, redirect_id=source.id)
    editor.wait()
    for actual, expected in zip(editor.results[-3:], manual):
        assert actual["Redirect document"] == source.id
        assert actual.get("mean") == pytest.approx(expected.get("mean") - (100 if sampling == "foreign" else 0))
    assert editor.workflow == steps and profile_csv(editor, tmp_path / "unredirected.csv") == curve
    assert editor.workflow_replay_info["state"] == "completed"


def test_default_sequence_activates_each_declared_source(tmp_path):
    source, editor = setup(tmp_path)
    clone = document()
    editor.add_native_document(clone)
    editor.active_index = 1
    editor.recording = True
    editor.action("measure_rois")
    editor.wait()
    editor.recording = False
    baseline, steps = copy.deepcopy(editor.results), copy.deepcopy(editor.workflow)
    assert steps[0]["source"]["document_id"] == source.id and steps[-1]["source"]["document_id"] == clone.id
    editor.action("run_workflow", restore_settings=True)
    editor.wait()
    assert editor.results == baseline + baseline and editor.workflow == steps
    assert editor.workflow_replay_info["completed"] == 4 and editor.active_index == 1
