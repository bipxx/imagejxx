"""Results-only deltas restore exact cells without touching image history."""
import copy
import csv
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


def make_editor(tmp_path):
    editor = Editor(preferences=tmp_path / "prefs.json")
    editor.results = [dict(value=0, label="first"), dict(value=2, label="second")]
    editor.update_settings(measurements=["value", "label"], persist=False)
    return editor


def edit(editor, value=None, *, kind="number", row=0, column="value"):
    args = dict(row=row, column=column, kind=kind)
    if kind in ("text", "number", "boolean"): args["value"] = value
    editor.action("edit_result_cell", **args)


def numeric_state(editor):
    return json.loads(editor._editor.results_table_state)


def data_state(editor):
    return editor.results, numeric_state(editor), editor.results_view_info


def test_status_is_detached_readonly_and_empty_actions_are_atomic(tmp_path):
    editor = make_editor(tmp_path)
    info = editor.results_history_info
    assert info["undo_steps"] == info["redo_steps"] == info["retained_bytes"] == 0
    assert not info["can_undo"] and not info["can_redo"] and info["undo_conflict"] == "empty"
    assert info["step_limit"] == editor.settings["history_limit"]
    info["undo_steps"] = 99
    assert editor.results_history_info["undo_steps"] == 0
    with pytest.raises(AttributeError): editor.results_history_info = {}
    before = data_state(editor)
    for action in ["undo_results_cell", "redo_results_cell"]:
        with pytest.raises(ValueError, match="empty"): editor.action(action)
    assert data_state(editor) == before


@pytest.mark.parametrize("kind,value,expected", [
    ("number", "18446744073709551615", 2**64-1),
    ("number", "-9223372036854775808", -(2**63)),
    ("number", "9007199254740993", 9007199254740993),
    ("number", "1.25e2", 125.0), ("number", "-0.0", -0.0),
    ("text", 'New,\n"β"', 'New,\n"β"'), ("boolean", True, True),
    ("boolean", False, False), ("null", None, None), ("missing", None, None),
])
def test_exact_typed_delta_undo_and_redo(tmp_path, kind, value, expected):
    editor = make_editor(tmp_path)
    original = data_state(editor)
    edit(editor, value, kind=kind)
    if kind == "missing": assert "value" not in editor.results[0]
    else:
        assert editor.results[0]["value"] == expected and type(editor.results[0]["value"]) is type(expected)
        if value == "-0.0": assert np.signbit(editor.results[0]["value"])
    changed = data_state(editor)
    info = editor.results_history_info
    assert info["undo_steps"] == 1 and info["can_undo"] and info["undo_target"] == dict(row=0, column="value")
    editor.action("undo_results_cell")
    assert data_state(editor) == original and editor.results_history_info["can_redo"]
    editor.action("redo_results_cell")
    assert data_state(editor) == changed and editor.results_history_info["redo_steps"] == 0
    assert editor.render_memory_info["working_bytes"] == 0


def test_missing_and_null_restore_different_explicit_presence(tmp_path):
    editor = make_editor(tmp_path)
    editor.results = [dict(label="absent"), dict(value=None)]
    edit(editor, "7")
    edit(editor, "8", row=1)
    editor.action("undo_results_cell")
    assert editor.results == [dict(label="absent", value=7), dict(value=None)]
    editor.action("undo_results_cell")
    assert editor.results == [dict(label="absent"), dict(value=None)]
    editor.action("redo_results_cell"); editor.action("redo_results_cell")
    assert editor.results == [dict(label="absent", value=7), dict(value=8)]


def test_sorted_filtered_history_restores_source_rows_and_keeps_current_view(tmp_path):
    editor = make_editor(tmp_path)
    editor.action("set_results_view", sort_column="value", filter=dict(column="value", operator="gt", value=0))
    edit(editor, "4")
    assert editor.results_view_info["source_rows"] == [1, 0]
    editor.action("undo_results_cell")
    assert editor.results_view_info["source_rows"] == [1]
    editor.action("set_results_view", sort_column="value", descending=True)
    editor.action("redo_results_cell")
    assert editor.results_view_info["source_rows"] == [0, 1]
    assert editor.results_view_info["criteria"]["descending"] is True


def test_branch_truncation_noop_keep_invalid_and_cancel_preserve_cursor(tmp_path):
    editor = make_editor(tmp_path)
    edit(editor, "1"); edit(editor, "3")
    editor.action("undo_results_cell")
    before, info = data_state(editor), editor.results_history_info
    edit(editor, "1")
    edit(editor, kind="keep")
    with pytest.raises(ValueError): edit(editor, "NaN")
    assert data_state(editor) == before and editor.results_history_info == info
    edit(editor, "5", row=1)
    assert editor.results_history_info["redo_steps"] == 0
    assert editor.results_history_info["undo_steps"] == 2
    editor.action("undo_results_cell"); editor.action("undo_results_cell")
    assert editor.results == [dict(value=0, label="first"), dict(value=2, label="second")]


@pytest.mark.parametrize("action", ["undo_results_cell", "redo_results_cell", "clear_results_cell_history"])
def test_history_actions_reject_parameters(tmp_path, action):
    editor = make_editor(tmp_path)
    edit(editor, "1")
    before, info = data_state(editor), editor.results_history_info
    with pytest.raises(ValueError): editor.action(action, row=0)
    assert data_state(editor) == before and editor.results_history_info == info


def test_step_eviction_and_reducing_limit_preserve_current_values(tmp_path):
    editor = make_editor(tmp_path)
    editor.update_settings(history_limit=3, persist=False)
    for value in ["1", "2", "3", "4"]: edit(editor, value)
    assert editor.results_history_info["undo_steps"] == 3 and editor.results_history_info["evicted_steps"] == 1
    editor.action("undo_results_cell"); editor.action("undo_results_cell")
    assert editor.results[0]["value"] == 2
    before = copy.deepcopy(editor.results)
    editor.update_settings(history_limit=1, persist=False)
    assert editor.results == before
    assert editor.results_history_info["undo_steps"] + editor.results_history_info["redo_steps"] == 1
    editor.action("undo_results_cell")
    assert editor.results[0]["value"] == 1


def test_byte_eviction_and_repeated_travel_are_bounded(tmp_path):
    editor = make_editor(tmp_path)
    editor.update_settings(history_limit=128, processing_memory_mb=1, persist=False)
    for i in range(32): edit(editor, ("x" if i % 2 else "y") * 20000, kind="text")
    info, retained = editor.results_history_info, editor.render_memory_info["retained_bytes"]
    assert info["evicted_steps"] > 0 and info["retained_bytes"] <= info["byte_limit"]
    for _ in range(30):
        editor.action("undo_results_cell"); editor.action("redo_results_cell")
        assert editor.render_memory_info["retained_bytes"] == retained
        assert editor.render_memory_info["working_bytes"] == 0
    before = data_state(editor)
    editor.action("clear_results_cell_history")
    assert data_state(editor) == before and editor.results_history_info["retained_bytes"] == 0
    assert editor.render_memory_info["retained_bytes"] == retained - info["retained_bytes"]


def test_restore_admission_failure_keeps_data_cursor_and_redo(tmp_path):
    editor = make_editor(tmp_path)
    edit(editor, "x" * 100000, kind="text")
    editor.update_settings(processing_memory_mb=1, persist=False)
    before, info = data_state(editor), editor.results_history_info
    with pytest.raises((ValueError, RuntimeError)): editor.action("undo_results_cell")
    assert data_state(editor) == before and editor.results_history_info == info
    assert editor.render_memory_info["working_bytes"] == 0
    editor.update_settings(processing_memory_mb=8, persist=False)
    editor.action("undo_results_cell")
    assert editor.results[0]["value"] == 0


def test_denied_edit_does_not_discard_redo(tmp_path):
    editor = make_editor(tmp_path)
    edit(editor, "1"); editor.action("undo_results_cell")
    editor.update_settings(processing_memory_mb=1, persist=False)
    before, info = data_state(editor), editor.results_history_info
    with pytest.raises((ValueError, RuntimeError)): edit(editor, "x" * 100000, kind="text")
    assert data_state(editor) == before and editor.results_history_info == info
    editor.action("redo_results_cell")
    assert editor.results[0]["value"] == 1


@pytest.mark.parametrize("route", ["clear", "setter", "replace", "open", "startup"])
def test_whole_table_replacement_invalidates_runtime_history(tmp_path, route):
    editor = make_editor(tmp_path)
    edit(editor, "7")
    session = tmp_path / "state.ijx"
    editor.action("save_workspace", path=str(session))
    if route == "clear": editor.results = []  # Maintained raw-table API; no clear_results action.
    elif route == "setter": editor.results = [dict(value=8)]
    elif route == "replace":
        path = tmp_path / "replace.csv"; path.write_text("value\n9", encoding="utf-8")
        editor.action("import_results", path=str(path))
    elif route == "open": editor.open_file(session)
    else: editor = Editor(preferences=tmp_path / "reopened.json", workspace=session)
    assert editor.results_history_info["undo_steps"] == editor.results_history_info["redo_steps"] == 0
    assert editor.results_history_info["retained_bytes"] == 0


def test_nonconflicting_import_and_measure_appends_preserve_cell_history(tmp_path):
    editor = make_editor(tmp_path)
    edit(editor, "7")
    path = tmp_path / "append.csv"; path.write_text("value\n9", encoding="utf-8")
    editor.action("import_results", path=str(path), mode="append", show_columns=False)
    editor.action("undo_results_cell")
    assert editor.results[0]["value"] == 0 and editor.results[2]["value"] == 9
    editor.action("redo_results_cell")
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    editor.action("measure")  # The installed sampled binding waits.
    editor.action("undo_results_cell")
    assert editor.results[0]["value"] == 0 and len(editor.results) == 4 and editor.results[-1]["mean"] == 1.5
    assert owner.undo_steps == 0


def test_managed_default_and_opaque_missing_are_not_reconstructed(tmp_path):
    editor = Editor(preferences=tmp_path / "prefs.json")
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    editor.update_settings(measurements=["mean"], persist=False); editor.action("measure")
    path = tmp_path / "opaque.csv"; path.write_text("note\nexternal", encoding="utf-8")
    editor.action("import_results", path=str(path), mode="append", show_columns=False)
    editor.update_settings(measurements=["mean", "Skew"], measurement_nan_empty_cells=True, persist=False)
    editor.action("measure")
    state = numeric_state(editor)
    edit(editor, "7", column="Skew"); edit(editor, "8", row=1, column="Skew")
    editor.action("undo_results_cell"); editor.action("undo_results_cell")
    assert editor._editor.results_cell(0, "Skew") == '"NaN"'
    assert editor.results[1] == dict(note="external") and editor._editor.results_cell(1, "Skew") == "null"
    editor.action("redo_results_cell"); editor.action("redo_results_cell")
    assert [row["Skew"] for row in editor.results[:2]] == [7, 8] and numeric_state(editor) == state


def test_changed_numeric_default_blocks_history_without_cursor_change(tmp_path):
    editor = Editor(preferences=tmp_path / "prefs.json")
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    editor.update_settings(measurements=["mean"], persist=False)
    editor.action("measure")  # A managed row, before Skew is allocated.
    editor.update_settings(measurements=["mean", "Skew"], persist=False)
    edit(editor, "7", column="Skew")  # The managed default is still absent.
    editor.update_settings(measurement_nan_empty_cells=True, persist=False)
    editor.action("measure")
    assert editor.results_history_info["undo_conflict"] == "default_changed"
    before = data_state(editor), editor.results_history_info
    with pytest.raises(ValueError, match="default_changed"): editor.action("undo_results_cell")
    assert (data_state(editor), editor.results_history_info) == before


def test_image_undo_and_results_undo_are_independent(tmp_path):
    editor = make_editor(tmp_path)
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    live = editor.add_native_document(owner)
    live.apply_operation("add", {"value": 5}, asynchronous=False)
    pixels = np.asarray(owner.snapshot().dataset.numpy("YX")).copy()
    edit(editor, "7"); editor.action("undo_results_cell")
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), pixels)
    assert owner.undo_steps == 1 and owner.redo_steps == 0 and editor.results[0]["value"] == 0
    editor.action("undo")
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), np.arange(4).reshape(2, 2))
    editor.action("redo_results_cell")
    assert editor.results[0]["value"] == 7 and owner.redo_steps == 1


def assert_host(report, context, directory):
    assert report["contract"] == "results-history-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert all(report[key] for key in ["native_exact_vectors", "invalid_preserved", "managed_default_preserved",
        "pending_and_append_preserved", "stale_preserved", "eviction_preserved", "source_protected"])
    assert report["edited_history"]["undo_steps"] == 1 and report["undone_history"]["redo_steps"] == 1
    assert report["branch_history"]["undo_steps"] == 2 and report["branch_history"]["redo_steps"] == 0
    assert report["text_rows"][0]["label"] == 'New,\n"beta"'
    assert report["rows"] == [dict(id=1, value=3, label="First"), dict(id=2, value=4, label="Second"),
                              dict(id=2**64-1, value=-1, label="Third")]
    with (directory / "view.csv").open(encoding="utf-8", newline="") as stream:
        assert list(csv.reader(stream)) == [["id", "value", "label"], ["1", "3", "First"], ["2", "4", "Second"]]
    assert report["copied_csv"] == (directory / "view.csv").read_text(encoding="utf-8")
    assert report["visible_table"]["history"]["undo_steps"] == 2
    reader = Editor(preferences=directory / "reader.json", workspace=directory / "history.ijx")
    assert reader.results == report["rows"] and reader.results_view_info == report["view"]
    assert reader.results_history_info["undo_steps"] == reader.results_history_info["redo_steps"] == 0
    names = {row["name"] for row in report["controls"]}
    assert {"results.undo_cell", "results.redo_cell", "results.clear_cell_history", "dialog.apply", "dialog.cancel", "cell.1.value"} <= names
    assert not report["initial_controls"]["undo"]["enabled"] and not report["initial_controls"]["redo"]["enabled"]
    assert report["final_working_bytes"] == 0


def test_python_actual_results_history(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    directory = tmp_path / "actual"
    assert_host(json.loads(editor._results_history_ui_case(str(directory))), "imgui_python", directory)


def test_standalone_actual_results_history():
    path = Path(os.environ["IMAGEJXX_RESULTS_HISTORY_CPP_REPORT"])
    assert_host(json.loads(path.read_bytes()), "imgui_cpp", path.parent)


def test_open_cell_review_blocks_travel_but_idle_history_clear_keeps_draft(tmp_path):
    editor = make_editor(tmp_path)
    edit(editor, "7")
    editor.action("show_result_cell_editor", row=0, column="value")
    before = data_state(editor)
    info = editor.results_history_info
    assert info["review_open"] and not info["can_undo"]
    for action in ["undo_results_cell", "redo_results_cell"]:
        with pytest.raises(ValueError, match="review"): editor.action(action)
        assert data_state(editor) == before and editor.results_history_info == info
    editor.action("clear_results_cell_history")
    assert data_state(editor) == before
    assert editor.results_history_info["review_open"]
    assert editor.results_history_info["undo_steps"] == editor.results_history_info["retained_bytes"] == 0
