"""Targeted Results edits preserve source indices and numeric allocation state."""
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
    editor.results = [dict(value=3, label="first", other=[1, 2]), dict(value=1, label="second")]
    editor.update_settings(measurements=["value", "label", "other"], persist=False)
    return editor


def state(editor):
    return json.loads(editor._editor.results_table_state)


def snapshot(editor):
    return editor.results, state(editor), editor.results_view_info, editor.settings.to_dict()


def edit(editor, row=0, column="value", kind="number", **kwargs):
    editor.action("edit_result_cell", row=row, column=column, kind=kind, **kwargs)


def csv_rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.reader(stream))


def test_raw_source_index_edit_moves_and_hides_projected_rows(tmp_path):
    editor = make_editor(tmp_path)
    editor.action("set_results_view", sort_column="value", filter=dict(column="value", operator="gt", value=0))
    assert editor.results_view_info["source_rows"] == [1, 0]
    before_state = state(editor)
    edit(editor, row=1, value="4")
    assert editor.results == [dict(value=3, label="first", other=[1, 2]), dict(value=4, label="second")]
    assert editor.results_view_info["source_rows"] == [0, 1]
    edit(editor, value="-1")
    assert editor.results_view_info["source_rows"] == [1] and state(editor) == before_state
    editor.action("save_results_view", path=str(tmp_path / "view.csv"))
    assert csv_rows(tmp_path / "view.csv")[1] == ["4", "second", ""]


@pytest.mark.parametrize("text, expected", [("0", 0), ("-9223372036854775808", -(2**63)),
    ("9223372036854775808", 2**63), ("18446744073709551615", 2**64-1),
    ("9007199254740993", 9007199254740993), ("1.25e2", 125.0), ("-0.0", -0.0)])
def test_exact_finite_decimal_entry(tmp_path, text, expected):
    editor = make_editor(tmp_path)
    edit(editor, value=text)
    assert editor.results[0]["value"] == expected and type(editor.results[0]["value"]) is type(expected)
    if text == "-0.0": assert np.signbit(editor.results[0]["value"])
    assert editor.results[0]["other"] == [1, 2] and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("text", ["", "001", "+1", " 1", "NaN", "Infinity", "1e999", "1e-999", "18446744073709551616"])
def test_invalid_number_preserves_all_consumed_state(tmp_path, text):
    editor = make_editor(tmp_path)
    before = snapshot(editor)
    with pytest.raises((ValueError, RuntimeError)): edit(editor, value=text)
    assert snapshot(editor) == before and editor.render_memory_info["working_bytes"] == 0


def test_text_boolean_null_missing_and_keep_are_distinct(tmp_path):
    editor = make_editor(tmp_path)
    before_state = state(editor)
    edit(editor, kind="text", value='New,\n"β"')
    assert editor.results[0]["value"] == 'New,\n"β"'
    edit(editor, kind="boolean", value=True)
    assert editor.results[0]["value"] is True
    edit(editor, kind="null")
    assert editor.results[0]["value"] is None
    editor.action("set_results_view", filter=dict(column="value", operator="is_null"))
    assert editor.results_view_info["source_rows"] == [0]
    edit(editor, kind="missing")
    assert "value" not in editor.results[0] and editor.results_view_info["source_rows"] == []
    editor.action("set_results_view", filter=dict(column="value", operator="is_missing"))
    assert editor.results_view_info["source_rows"] == [0]
    before = snapshot(editor)
    edit(editor, column="other", kind="keep")
    assert snapshot(editor) == before and state(editor) == before_state


@pytest.mark.parametrize("parameters", [
    dict(row=True, column="value", kind="null"), dict(row=0.0, column="value", kind="null"),
    dict(row=-1, column="value", kind="null"), dict(row=9, column="value", kind="null"),
    dict(row=2**64-1, column="value", kind="null"), dict(row=0, column="unknown", kind="null"),
    dict(row=0, column="value", kind="number", value=2**64),
    dict(row=0, column="value", kind="boolean", value=1),
    dict(row=0, column="value", kind="null", value=None),
    dict(row=0, column="value", kind="missing", value=""),
    dict(row=0, column="value", kind="keep", value=""),
    dict(row=0, column="value", kind="text"), dict(row=0, column="value", kind="array", value=[]),
    dict(row=0, column="value", kind="text", value="bad\0text"),
    dict(row=0, column="value", kind="null", extra=True), dict(row=0, kind="null"),
])
def test_strict_kind_address_and_field_validation(tmp_path, parameters):
    editor = make_editor(tmp_path)
    before = snapshot(editor)
    with pytest.raises((ValueError, RuntimeError)): editor.action("edit_result_cell", **parameters)
    assert snapshot(editor) == before


def test_object_row_and_review_schema_are_required(tmp_path):
    editor = make_editor(tmp_path)
    editor.results = [42]
    with pytest.raises(ValueError): edit(editor, kind="null")
    assert editor.results == [42]
    with pytest.raises(ValueError): editor.action("show_result_cell_editor", row=0, column="value", extra=True)


def test_repeated_edit_lease_is_replaced_and_noop_is_free(tmp_path):
    editor = make_editor(tmp_path)
    editor.update_settings(processing_memory_mb=1, persist=False)
    edit(editor, kind="text", value="x" * 64)
    # Warm through bounded history eviction before checking per-row replacement.
    for i in range(editor.results_history_info["step_limit"] + 2):
        edit(editor, kind="text", value=("x" if i % 2 else "y") * 64)
    retained = editor.render_memory_info["retained_bytes"]
    for i in range(50):
        edit(editor, kind="text", value=("x" if i % 2 else "y") * 64)
        assert editor.render_memory_info["retained_bytes"] == retained
        assert editor.render_memory_info["working_bytes"] == 0
    before = editor.render_memory_info["retained_bytes"]
    edit(editor, kind="keep")
    assert editor.render_memory_info["retained_bytes"] == before
    editor.results = []
    assert editor.render_memory_info["retained_bytes"] == 0


def test_resource_denial_preserves_old_row_and_releases_working(tmp_path):
    editor = make_editor(tmp_path)
    editor.update_settings(processing_memory_mb=1, persist=False)
    edit(editor, value="5")
    before, retained = snapshot(editor), editor.render_memory_info["retained_bytes"]
    with pytest.raises((ValueError, RuntimeError)): edit(editor, kind="text", value="x" * 100000)
    assert snapshot(editor) == before
    assert editor.render_memory_info["retained_bytes"] == retained and editor.render_memory_info["working_bytes"] == 0


def test_import_append_keeps_cell_owner_and_replace_retires_it(tmp_path):
    editor = make_editor(tmp_path)
    edit(editor, kind="text", value="x" * 50000)
    path = tmp_path / "input.csv"
    path.write_text("value\n7", encoding="utf-8")
    editor.action("import_results", path=str(path), mode="append", show_columns=False)
    assert editor.results[0]["value"] == "x" * 50000 and editor.results[-1]["value"] == 7
    edit(editor, value="2")
    editor.action("import_results", path=str(path))
    reference = Editor(preferences=tmp_path / "reference.json")
    reference.action("import_results", path=str(path))
    assert editor.results == reference.results
    assert editor.render_memory_info["retained_bytes"] == reference.render_memory_info["retained_bytes"]


def test_managed_default_override_removal_and_imported_opaque_row(tmp_path):
    editor = Editor(preferences=tmp_path / "prefs.json")
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    source = owner.snapshot()
    editor.update_settings(measurements=["mean"], persist=False)
    editor.action("measure")  # The installed scripted binding waits.
    path = tmp_path / "import.csv"
    path.write_text("note\nexternal", encoding="utf-8")
    editor.action("import_results", path=str(path), mode="append", show_columns=False)
    editor.update_settings(measurements=["mean", "Skew"], measurement_nan_empty_cells=True, persist=False)
    editor.action("measure")
    before = state(editor)
    edit(editor, column="Skew", value="7")
    assert editor.results[0]["Skew"] == 7
    edit(editor, column="Skew", kind="missing")
    assert "Skew" not in editor.results[0]
    assert editor._editor.results_cell(0, "Skew") == '"NaN"'
    edit(editor, row=1, column="Skew", value="9")
    edit(editor, row=1, column="Skew", kind="missing")
    assert editor.results[1] == dict(note="external") and editor._editor.results_cell(1, "Skew") == "null"
    assert state(editor) == before and owner.snapshot().shares_data(source) and owner.undo_steps == 0


@pytest.mark.parametrize("route", ["open", "startup"])
def test_cell_values_default_state_and_view_survive_workspace(tmp_path, route):
    editor = make_editor(tmp_path)
    edit(editor, value=str(2**64-1))
    editor.action("set_results_view", sort_column="value")
    expected = editor.results, state(editor), editor.results_view_info
    path = tmp_path / "cells.ijx"
    editor.action("save_workspace", path=str(path))
    if route == "open":
        editor.results = []
        editor.open_file(path)
    else: editor = Editor(preferences=tmp_path / "reopened.json", workspace=path)
    assert (editor.results, state(editor), editor.results_view_info) == expected
    assert editor.render_memory_info["working_bytes"] == 0


def assert_host(report, context, directory):
    assert report["contract"] == "results-cells-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert all(report[name] for name in ["native_exact_vectors", "pending_preserved", "busy_cancel_preserved", "cancel_preserved", "invalid_preserved", "stale_preserved", "managed_default_preserved", "source_protected"])
    assert report["moved_view"]["source_rows"] == [2, 0, 1] and report["view"]["source_rows"] == [0, 1]
    assert report["rows"] == [dict(id=1, value=3, label='New,\n"beta"'),
        dict(id=2, value=4, label="Second"), dict(id=2**64-1, value=-1, label="Third")]
    assert csv_rows(directory / "view.csv") == [["id", "value", "label"], ["1", "3", 'New,\n"beta"'], ["2", "4", "Second"]]
    assert report["copied_csv"] == (directory / "view.csv").read_text(encoding="utf-8")
    assert any(c["source_row"] == 1 and c["name"] == "value" and c["text"] == "4" and c["control"]["id"] for c in report["visible_table"]["cells"])
    reader = Editor(preferences=directory / "reader.json", workspace=directory / "cells.ijx")
    assert reader.results == report["rows"] and reader.results_view_info == report["view"]
    names = {item["name"] for item in report["controls"]}
    assert {"cell.1.value", "cell.0.label", "cell.0.Skew", "dialog.kind.choice.Number", "dialog.apply", "dialog.cancel", "results.copy_csv", "results.save_csv"} <= names
    assert report["final_working_bytes"] == 0


def test_python_actual_results_cells(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    directory = tmp_path / "actual"
    assert_host(json.loads(editor._results_cells_ui_case(str(directory))), "imgui_python", directory)


def test_standalone_actual_results_cells():
    path = Path(os.environ["IMAGEJXX_RESULTS_CELLS_CPP_REPORT"])
    assert_host(json.loads(path.read_bytes()), "imgui_cpp", path.parent)
