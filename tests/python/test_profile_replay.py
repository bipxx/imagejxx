"""Configured Profile intent and explicit source rebind on existing plot owner."""
import csv
import copy
import json

import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop


def document():
    pixels = np.arange(6, dtype=np.uint8).reshape(2, 3)
    owner = ij.Document(ij.Dataset(np.stack([pixels, pixels+10]), axes="ZYX",
        calibration=ij.SpatialCalibration(spacing=[3, 4, 1], unit="local")))
    edit = owner.begin_edit()
    edit.set_title("Profile source")
    owner.commit(edit)
    owner.select(ij.Selection("polyline", [[0, 0], [2, 0], [2, 1]]))
    view = owner.snapshot().display
    view.crosshair = [1, 1, 1]
    owner.set_display(view)
    return owner


def exported(editor, path, plot_id=0):
    editor.action("export_plot_csv", json.dumps({"path": str(path), "plot_id": plot_id}))
    with path.open(encoding="utf-8", newline="") as stream:
        return [(float(row["x"]), float(row["y"])) for row in csv.DictReader(stream)]


@pytest.mark.parametrize("samples,expected", [(0, [(0, 10), (3, 11), (6, 12), (10, 15)]),
    (3, [(0, 10), (3, 11), (6, 12), (8, 13.5), (10, 15)]),
    (5, [(0, 10), (1.5, 10.5), (3, 11), (4.5, 11.5), (6, 12), (7, 12.75), (8, 13.5), (9, 14.25), (10, 15)])])
def test_record_save_load_replay_restore_rebind_workspace(tmp_path, samples, expected):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    source = document()
    editor.add_native_document(source)
    editor.update_settings(json.dumps({"profile_samples": samples}), False)
    before, undo = source.snapshot(), source.undo_steps
    editor.recording = True
    editor.action("profile", "{}")
    editor.wait()
    assert not editor.error
    editor.recording = False
    step = json.loads(editor.workflow)[0]
    assert step["schema"] == "imagejxx.profile-command" and step["profile_samples"] == samples
    assert exported(editor, tmp_path/"initial.csv") == expected
    editor.action("keep_plot_snapshot", "{}")
    frozen = json.loads(editor.plot_snapshots_info())[0]["plot_id"]
    file = tmp_path/"profile.json"
    editor.action("save_workflow", json.dumps({"path": str(file)}))
    editor.workflow = "[]"
    editor.action("load_workflow", json.dumps({"path": str(file)}))
    editor.action("replay_profile", json.dumps({"record": step}))
    editor.wait()
    assert not editor.error and exported(editor, tmp_path/"repeat.csv") == expected
    editor.update_settings(json.dumps({"profile_samples": 2 if samples != 2 else 3}), False)
    with pytest.raises(ValueError, match="sampling differs"):
        editor.action("replay_profile", json.dumps({"record": step}))
    editor.action("replay_profile", json.dumps({"record": step, "restore_sampling": True}))
    editor.wait()
    assert not editor.error and exported(editor, tmp_path/"restored.csv") == expected
    view = source.snapshot().display
    view = ij.set_manual_threshold_state(view, source.snapshot().dataset, ij.ManualThresholdRange(0, 1), ij.PlanePosition(0, 0, 1))
    source.set_display(view)
    editor.action("replay_profile", json.dumps({"record": step}))
    editor.wait()
    assert not editor.error and exported(editor, tmp_path/"threshold-unused.csv") == expected
    assert source.snapshot().shares_data(before) and source.undo_steps == undo
    rebound = document()
    editor.add_native_document(rebound)
    editor.action("run_workflow", json.dumps({"rebind": True, "document_id": rebound.id}))
    editor.wait()
    assert not editor.error and exported(editor, tmp_path/"rebound.csv") == expected
    assert exported(editor, tmp_path/"frozen.csv", frozen) == expected and json.loads(editor.workflow) == [step]
    file = tmp_path/"profile.ijx"
    editor.action("save_workspace", json.dumps({"path": str(file)}))
    reopened = _desktop.Editor(str(tmp_path/"reopened-preferences.json"))
    reopened.open_legacy_file(str(file))
    assert json.loads(reopened.workflow) == [step]
    reopened.action("run_workflow", json.dumps({"rebind": True, "restore_sampling": True}))
    reopened.wait()
    assert not reopened.error and exported(reopened, tmp_path/"reopened.csv") == expected


@pytest.mark.parametrize("field,value", [("version", 2), ("profile_samples", 1), ("profile_samples", True),
    ("profile_samples", -1), ("profile_samples", 1000001), ("profile_samples", 2.5)],
    ids=["version", "one", "boolean", "negative", "large", "fraction"])
def test_bad_profile_record_preserves_live_frozen_history(tmp_path, field, value):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    source = document()
    editor.add_native_document(source)
    editor.recording = True
    editor.action("profile", "{}")
    editor.wait()
    editor.recording = False
    assert not editor.error
    step = json.loads(editor.workflow)[0]
    before, undo = source.snapshot(), source.undo_steps
    original = exported(editor, tmp_path/"before.csv")
    editor.action("keep_plot_snapshot", "{}")
    snapshot = editor.plot_snapshots_info()
    bad = copy.deepcopy(step)
    bad[field] = value
    with pytest.raises(ValueError):
        editor.action("replay_profile", json.dumps({"record": bad}))
    assert source.snapshot().shares_data(before) and source.undo_steps == undo
    assert exported(editor, tmp_path/"after.csv") == original and editor.plot_snapshots_info() == snapshot
    assert json.loads(editor.workflow) == [step]


def test_actual_profile_replay_controls_outputs_and_lifecycle(tmp_path):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    report = json.loads(editor._profile_replay_ui_case(str(tmp_path/"actual")))
    assert_actual_profile_replay_report(report)


def assert_actual_profile_replay_report(report):
    assert report["state"] == "observed"
    assert all(report[key] is True for key in ["source_preserved", "frozen_preserved", "resource_guards", "workspace_intent_preserved"])
    # Same local configured polyline oracle as capturedProfileUiCase.
    expected_x = [0, 1.5, 3, 4.5, 6, 7, 8, 9, 10]
    expected_y = [10, 10.5, 11, 11.5, 12, 12.75, 13.5, 14.25, 15]
    for key in ["first_plot", "rebound_plot"]:
        assert report[key]["series_x"] == expected_x and report[key]["series_y"] == expected_y
        assert report[key]["x_label"] == "Distance (local)" and report[key]["y_label"] == "Pixel value (raw)"
        assert report[key]["visible"]
    assert report["record"]["source"]["document_id"] == report["source_id"]
    assert report["record"]["profile_samples"] == 5 and report["record"]["source"]["threshold"] is None
    assert report["rebound_plot"]["source_title"] == "Profile clone"
    assert set(report["review_refusals"]) == {"pose", "line", "global", "active_owner"}
    assert set(report["job_refusals"]) == {"cancel", "record_toggle", "workflow_clear", "sampling", "plot_close", "pose", "line", "global", "source_close"}
    controls = {row["name"] for row in report["controls"]}
    assert {"workflow.record", "menu.action.profile", "plot.snapshot", "workflow.profile.0.replay",
            "workflow.run", "workflow.profile.0.review", "dialog.apply", "dialog.cancel", "plot.close"} <= controls
    assert [(float(row["x"]), float(row["y"])) for row in csv.DictReader(report["csv"].splitlines())] == list(zip(expected_x, expected_y))


def test_standalone_profile_replay_report():
    import os
    from pathlib import Path

    report = os.environ.get("IMAGEJXX_PROFILE_REPLAY_CPP_REPORT")
    if not report:
        pytest.skip("Standalone Profile replay report not supplied")
    assert_actual_profile_replay_report(json.loads(Path(report).read_bytes()))
