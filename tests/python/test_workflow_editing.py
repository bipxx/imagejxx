"""Authored order and enabled rows survive editing, persistence and execution."""
import copy
import csv
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_edit_analysis_chain import setup, pixels, TARGET


def wrap(record, enabled=True):
    return dict(schema="imagejxx.workflow-entry", version=1, enabled=enabled, record=record)


def edit(editor, command, **choices):
    editor.action(command, **choices)


def test_draft_cancel_apply_preserves_exact_records_and_image_history(tmp_path):
    owner, clone, editor = setup(tmp_path)
    original = editor.workflow
    before = owner.snapshot()
    for apply in [False, True]:
        edit(editor, "show_workflow_editor")
        edit(editor, "workflow_move", from_index=4, to_index=0)
        edit(editor, "workflow_move", from_index=0, to_index=0)
        edit(editor, "workflow_set_enabled", index=0, enabled=False)
        edit(editor, "workflow_remove", index=2)
        assert editor.workflow == original
        edit(editor, "apply_workflow_editor" if apply else "cancel_workflow_editor")
        expected = [wrap(original[4], False)] + [wrap(original[i]) for i in [0, 2, 3]]
        assert editor.workflow == (expected if apply else original)
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 2 and clone.undo_steps == 0
        assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("command,choices", [
    ("workflow_move", dict(from_index=-1, to_index=0)),
    ("workflow_move", dict(from_index=True, to_index=0)),
    ("workflow_move", dict(from_index=0.0, to_index=0)),
    ("workflow_move", dict(from_index=0, to_index=5)),
    ("workflow_move", dict(from_index=0)),
    ("workflow_remove", dict(index=5)),
    ("workflow_remove", dict(index=0, extra=1)),
    ("workflow_set_enabled", dict(index=0, enabled=1)),
    ("workflow_set_enabled", dict(index=0, enabled="false")),
    ("show_workflow_editor", dict(extra=1)),
])
def test_invalid_draft_action_is_atomic(command, choices, tmp_path):
    _, _, editor = setup(tmp_path)
    original = editor.workflow
    edit(editor, "show_workflow_editor")
    with pytest.raises((ValueError, RuntimeError)):
        edit(editor, command, **choices)
    edit(editor, "apply_workflow_editor")
    assert editor.workflow == [wrap(record) for record in original]
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("change", ["owner", "table", "recording"])
def test_stale_draft_cannot_overwrite_current_state(change, tmp_path):
    _, _, editor = setup(tmp_path)
    edit(editor, "show_workflow_editor")
    edit(editor, "workflow_remove", index=0)
    if change == "owner": editor.active_index = 1
    if change == "table": editor.workflow = []
    if change == "recording": editor.recording = True
    current = editor.workflow
    with pytest.raises(ValueError): edit(editor, "apply_workflow_editor")
    assert editor.workflow == current
    edit(editor, "cancel_workflow_editor")
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("persistence", ["file", "workspace", "startup"])
def test_authored_subset_runs_with_original_export_indices(persistence, tmp_path):
    owner, clone, editor = setup(tmp_path)
    original = editor.workflow
    # Disabled export at row 1; Gaussian, Measure, Profile, enabled PNG at 2..5.
    editor.workflow = [original[4]] + original
    edit(editor, "show_workflow_editor")
    edit(editor, "workflow_set_enabled", index=0, enabled=False)
    edit(editor, "workflow_remove", index=2)
    edit(editor, "apply_workflow_editor")
    expected = editor.workflow
    if persistence == "file":
        path = tmp_path / "workflow.json"
        editor.action("save_workflow", path=str(path))
        assert json.loads(path.read_bytes())["version"] == 2
        editor.workflow = []
        editor.action("load_workflow", path=str(path))
    else:
        path = tmp_path / "workspace.ijx"
        editor.action("save_workspace", path=str(path))
        if persistence == "startup":
            editor = Editor(preferences=tmp_path / "restored-prefs.json", workspace=path)
        else:
            editor.workflow = []
            editor.open_file(path)
        clone = editor._editor.native_document(json.loads(editor._editor.document_infos())[-1]["id"])
    assert editor.workflow == expected
    editor.active_index = len(json.loads(editor._editor.document_infos())) - 1
    sentinel = tmp_path / "manual.png"
    sentinel.write_bytes(b"disabled destination sentinel")
    editor.action("run_workflow", rebind=True, restore_settings=True, gaussian_rebind=TARGET,
                  export_options={"5": {"path": str(tmp_path / "subset.png")}})
    assert editor.workflow_replay_info["active_step"] == 2
    editor.wait()
    assert editor.workflow_replay_info == dict(state="completed", completed=4, total=4, active_step=0, job_id=0, error="")
    np.testing.assert_array_equal(pixels(clone), [[77, 178]])
    assert clone.undo_steps == 1 and sentinel.read_bytes() == b"disabled destination sentinel"
    assert (tmp_path / "subset.png").is_file() and editor.workflow == expected
    editor.action("export_plot_csv", path=str(tmp_path / "profile.csv"))
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("count", [0, 1])
def test_empty_and_single_enabled_execution(count, tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    editor.workflow = [wrap(editor.workflow[1], bool(count))]
    if not count:
        with pytest.raises(ValueError): editor.action("run_workflow", rebind=True)
        assert clone.undo_steps == 0
    else:
        editor.action("run_workflow", rebind=True)
        editor.wait()
        np.testing.assert_array_equal(pixels(clone), [[255, 0]])
        assert editor.workflow_replay_info["completed"] == 1 and clone.undo_steps == 1
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("mutation", ["nested", "enabled", "extra", "version", "raw-v2", "wrapped-v1"])
def test_invalid_file_load_preserves_table(mutation, tmp_path):
    _, _, editor = setup(tmp_path)
    original = editor.workflow
    row = wrap(original[1])
    root = dict(format="imagejxx-workflow", version=2, steps=[row])
    if mutation == "nested": row["record"] = wrap(row["record"])
    if mutation == "enabled": row["enabled"] = 0
    if mutation == "extra": row["extra"] = True
    if mutation == "version": row["version"] = 2
    if mutation == "raw-v2": root["steps"] = [original[1]]
    if mutation == "wrapped-v1": root["version"] = 1
    path = tmp_path / "bad.json"
    path.write_text(json.dumps(root))
    with pytest.raises((ValueError, RuntimeError)): editor.action("load_workflow", path=str(path))
    assert editor.workflow == original and editor.render_memory_info["working_bytes"] == 0


def test_v1_compatibility_and_record_append(tmp_path):
    _, _, editor = setup(tmp_path)
    original = editor.workflow
    path = tmp_path / "old.json"
    editor.action("save_workflow", path=str(path))
    assert json.loads(path.read_bytes())["version"] == 1
    editor.workflow = []
    editor.action("load_workflow", path=str(path))
    assert editor.workflow == original
    edit(editor, "show_workflow_editor")
    edit(editor, "workflow_set_enabled", index=0, enabled=False)
    edit(editor, "apply_workflow_editor")
    editor.recording = True
    editor.action("measure")
    editor.wait()
    editor.recording = False
    assert editor.workflow[:5] == [wrap(record, i != 0) for i, record in enumerate(original)]
    assert editor.workflow[-1]["enabled"] is True and editor.workflow[-1]["record"]["schema"] == original[2]["schema"]


def test_malformed_workspace_refuses_before_publication(tmp_path):
    owner, _, editor = setup(tmp_path)
    valid, bad = tmp_path / "valid.ijx", tmp_path / "bad.ijx"
    editor.action("save_workspace", path=str(valid))
    saved = ij.load_session(str(valid))
    malformed = wrap(editor.workflow[1])
    malformed["enabled"] = 1
    ij.save_session(str(bad), ij.SessionCapture(saved.documents, results=saved.results,
                    workflow=[malformed], extensions=saved.extensions))
    before, rows, steps, count = owner.snapshot(), editor.results, editor.workflow, len(editor.documents)
    with pytest.raises((ValueError, RuntimeError)): editor.open_file(bad)
    assert len(editor.documents) == count and editor.results == rows and editor.workflow == steps
    assert owner.snapshot().shares_data(before) and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("endpoint", ["cancel", "disabled-override", "busy-draft"])
def test_enabled_subset_guards_and_cancel(endpoint, tmp_path):
    _, clone, editor = setup(tmp_path)
    editor.active_index = 1
    original = editor.workflow
    editor.workflow = [wrap(original[4], False), wrap(original[0]), wrap(original[2])]
    choices = dict(rebind=True, restore_settings=True, gaussian_rebind=TARGET)
    if endpoint == "disabled-override":
        with pytest.raises(ValueError):
            editor.action("run_workflow", export_options={"1": {"path": str(tmp_path / "forbidden.png")}}, **choices)
    else:
        editor.action("run_workflow", **choices)
        assert editor.workflow_replay_info["active_step"] == 2
        if endpoint == "busy-draft":
            with pytest.raises(ValueError): editor.action("show_workflow_editor")
        editor.cancel()
        editor.wait()
        assert editor.workflow_replay_info["state"] == "canceled"
    assert clone.undo_steps == 0 and editor.render_memory_info["working_bytes"] == 0
    np.testing.assert_array_equal(pixels(clone), [[0, 255]])


def assert_host(report, context):
    assert report["contract"] == "workflow-editing-ui.001" and report["context"] == context
    assert report["state"] == "observed" and report["cancel_preserved"] and report["saved_equal"]
    assert report["pixels"] == [[77, 178]] and report["undo_steps"] == 1
    assert report["status"]["completed"] == 4 and report["status"]["total"] == 4
    assert report["disabled_destination_protected"] and report["final_working_bytes"] == 0
    names = {x["name"] for x in report["controls"]}
    assert {"workflow.edit", "dialog.row.0.down", "dialog.row.1.up", "dialog.row.1.enabled", "dialog.row.1.remove", "dialog.apply", "dialog.cancel", "workflow.run"} <= names
    assert report["profile"]["series_x"] == [0, 1] and report["profile"]["series_y"] == [77, 178]
    assert report["profile"]["source_title"] == "Edit clone"
    expected = copy.deepcopy(report["rows"][0])
    expected["Image"] = "Edit clone"
    assert report["rows"] == [report["rows"][0], expected]
    assert expected["count"] == 2 and expected["sum"] == 255 and expected["mean"] == 127.5
    directory = Path(report["workflow"][0]["record"]["file"]["path"]).parent
    assert (directory / "disabled.png").read_bytes() == b"disabled"
    rows = list(csv.DictReader((directory / "profile.csv").read_text().splitlines()))
    assert [(float(row["x"]), float(row["y"])) for row in rows] == [(0, 77), (1, 178)]
    reader = Editor(preferences=directory / "reader-prefs.json")
    reader.open_file(directory / "subset.png")
    restored = reader._editor.native_document(json.loads(reader._editor.document_infos())[0]["id"])
    # Captured display includes the active line (#ffd54bff), covering both pixels.
    np.testing.assert_array_equal(restored.snapshot().dataset.numpy("YXS"), [[[255, 213, 75]] * 2])
    saved = ij.load_session(str(directory / "authored.ijx"))
    assert saved.workflow == report["workflow"] and saved.results == report["rows"]


def test_python_actual_workflow_editing(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    assert_host(json.loads(editor._workflow_editing_ui_case(str(tmp_path / "actual"))), "imgui_python")


def test_standalone_actual_workflow_editing():
    assert_host(json.loads(Path(os.environ["IMAGEJXX_WORKFLOW_EDITING_CPP_REPORT"]).read_bytes()), "imgui_cpp")
