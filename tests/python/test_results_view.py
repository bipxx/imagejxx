"""A live row projection preserves raw cells, numeric defaults and CSV semantics."""
import copy
import csv
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


ROWS = [
    dict(id=18446744073709551615, value=9007199254740993, label='a,"\nx'),
    dict(id=1, value=9007199254740992.0, label="Zulu"),
    dict(id=2, value=-1, label="alpha"),
    dict(id=3, value=0, label="beta"),
    dict(id=4, value=-0.0, label="αβ"),
    dict(id=5, value=None, label="null"),
    dict(id=6, label="missing"),
    dict(id=7, value="2", label="Abc"),
    dict(id=8, value=True, label="bool"),
    dict(id=9, value=[1, 2], label="array"),
    dict(id=10, value={"k": 1}, label="object"),
]


def make_editor(tmp_path, rows=ROWS):
    editor = Editor(preferences=tmp_path / "prefs.json")
    editor.update_settings(measurements=["id", "value", "label"], persist=True)
    editor.results = copy.deepcopy(rows)
    return editor


def source_rows(editor):
    return editor.results_view_info["source_rows"]


def view(editor, **choices):
    editor.action("set_results_view", **choices)


def csv_rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.reader(stream))


def test_heterogeneous_stable_sort_reset_and_raw_export_compatibility(tmp_path):
    editor = make_editor(tmp_path)
    baseline = editor.results
    editor.action("save_results", path=str(tmp_path / "raw.csv"))
    view(editor, sort_column="value")
    assert source_rows(editor) == [2, 3, 4, 1, 0, 7, 8, 9, 10, 5, 6]
    assert editor.results == baseline
    editor.action("save_results_view", path=str(tmp_path / "view.csv"))
    expected = [csv_rows(tmp_path / "raw.csv")[i + 1] for i in source_rows(editor)]
    assert csv_rows(tmp_path / "view.csv")[1:] == expected
    assert expected[4][0] == "18446744073709551615" and expected[4][2] == 'a,"\nx'
    view(editor, sort_column="value", descending=True)
    assert source_rows(editor) == [6, 5, 9, 10, 8, 7, 0, 1, 3, 4, 2]
    editor.action("save_results", path=str(tmp_path / "raw-again.csv"))
    assert (tmp_path / "raw.csv").read_bytes() == (tmp_path / "raw-again.csv").read_bytes()
    editor.action("reset_results_view")
    assert source_rows(editor) == list(range(len(ROWS))) and not editor.results_view_info["active"]
    assert editor.results == baseline and editor.render_memory_info["working_bytes"] == 0


def test_exact_cross_numeric_types_at_signed_and_unsigned_boundaries(tmp_path):
    values = [-(2**63), float(-(2**63)), -.5, -0.0, 0, .5, 2**53 + 1, float(2**53),
              2**63 - 1, float(2**63), 2**64 - 1, float(2**64)]
    editor = make_editor(tmp_path, [dict(value=x) for x in values])
    view(editor, sort_column="value")
    assert source_rows(editor) == sorted(range(len(values)), key=values.__getitem__)
    view(editor, filter=dict(column="value", operator="gt", value=2**53))
    assert source_rows(editor) == [i for i, x in enumerate(values) if x > 2**53]
    view(editor, filter=dict(column="value", operator="eq", value=2**64 - 1))
    assert source_rows(editor) == [10]


@pytest.mark.parametrize("operator,value,expected", [
    ("gt", 2**53, [0]), ("ge", 0, [0, 1, 3, 4]), ("lt", 0, [2]),
    ("le", 0, [2, 3, 4]), ("eq", 0, [3, 4]), ("ne", 0, [0, 1, 2]),
    ("is_missing", None, [6]), ("is_null", None, [5]), ("is_nan", None, []),
])
def test_numeric_and_missing_filters_do_not_coerce_other_raw_types(operator, value, expected, tmp_path):
    editor = make_editor(tmp_path)
    criterion = dict(column="value", operator=operator)
    if not operator.startswith("is_"): criterion["value"] = value
    view(editor, filter=criterion)
    assert source_rows(editor) == expected and editor.results == ROWS


@pytest.mark.parametrize("operator,value,expected", [
    ("contains", "a", [0, 2, 3, 9]), ("contains", "α", [4]),
    ("equals_text", 'a,"\nx', [0]), ("contains", "absent", []),
])
def test_literal_text_matching_and_no_matches(operator, value, expected, tmp_path):
    editor = make_editor(tmp_path)
    view(editor, filter=dict(column="label", operator=operator, value=value))
    assert source_rows(editor) == expected
    editor.action("save_results_view", path=str(tmp_path / "filtered.csv"))
    assert len(csv_rows(tmp_path / "filtered.csv")) == len(expected) + 1


@pytest.mark.parametrize("choices", [
    dict(extra=1), dict(descending=1), dict(sort_column=True), dict(sort_column="absent"),
    dict(filter=dict(column="value", operator="gt", value=True)),
    dict(filter=dict(column="value", operator="gt", value="1")),
    dict(filter=dict(column="value", operator="gt")),
    dict(filter=dict(column="value", operator="is_null", value=None)),
    dict(filter=dict(column="value", operator="unknown")),
    dict(filter=dict(column="label", operator="contains", value=2)),
    dict(filter=dict(column="label", operator="contains", value="x" * 4097)),
])
def test_invalid_criteria_are_atomic(choices, tmp_path):
    editor = make_editor(tmp_path)
    view(editor, sort_column="value", filter=dict(column="label", operator="contains", value="a"))
    before = editor.results_view_info
    with pytest.raises((ValueError, RuntimeError)):
        view(editor, **choices)
    assert editor.results_view_info == before and editor.results == ROWS
    assert editor.render_memory_info["working_bytes"] == 0


def test_replacement_clear_precision_and_column_changes_refresh_current_view(tmp_path):
    editor = make_editor(tmp_path, [dict(value=2, label="a"), dict(value=1, label="b")])
    view(editor, sort_column="value")
    assert source_rows(editor) == [1, 0]
    editor.results = [dict(value=0, label="updated"), dict(value=3, label="b"), dict(value=-1, label="new")]
    assert source_rows(editor) == [2, 0, 1]
    editor.update_settings(measurements=["label", "value"], measurement_precision=1, persist=False)
    editor.action("save_results_view", path=str(tmp_path / "latest.csv"))
    assert csv_rows(tmp_path / "latest.csv") == [["label", "value"], ["new", "-1"], ["updated", "0"], ["b", "3"]]
    editor.results = []
    assert source_rows(editor) == [] and editor.results_view_info["active"]
    editor.results = [dict(value=4), dict(value=2)]
    assert source_rows(editor) == [1, 0]


def test_real_measure_append_and_existing_numeric_default_provenance(tmp_path):
    editor = Editor(preferences=tmp_path / "prefs.json")
    owners = [ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2) + offset, axes="YX"), str(offset)) for offset in [0, 10]]
    for owner in owners: editor.add_native_document(owner)
    editor.active_index = 0
    before = [owner.snapshot() for owner in owners]
    editor.update_settings(measurements=["mean"], persist=False)
    editor.action("measure"); editor.wait()
    view(editor, sort_column="mean", descending=True)
    editor.update_settings(measurements=["mean", "Skew"], measurement_nan_empty_cells=True, persist=False)
    editor.active_index = 1
    editor.action("measure"); editor.wait()
    assert source_rows(editor) == [1, 0]
    view(editor, filter=dict(column="Skew", operator="is_nan"))
    assert source_rows(editor) == [0]
    assert editor._editor.results_cell(0, "Skew") == '"NaN"'
    assert all(owner.snapshot().shares_data(snapshot) and owner.undo_steps == 0 for owner, snapshot in zip(owners, before))
    assert editor.render_memory_info["working_bytes"] == 0


def test_view_can_be_configured_before_pending_measurement_publication(tmp_path):
    editor = Editor(preferences=tmp_path / "prefs.json")
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    editor.update_settings(measurements=["mean"], persist=False)
    editor.action("measure")
    view(editor, sort_column="mean", filter=dict(column="mean", operator="gt", value=1))
    editor.wait()
    assert source_rows(editor) == [0] and editor.results[0]["mean"] == 1.5
    assert owner.undo_steps == 0 and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("route", ["open", "startup"])
def test_workspace_preserves_view_but_keeps_raw_results(route, tmp_path):
    editor = make_editor(tmp_path)
    view(editor, sort_column="value", filter=dict(column="value", operator="ge", value=0))
    expected = editor.results_view_info
    path = tmp_path / "view.ijx"
    editor.action("save_workspace", path=str(path))
    assert ij.load_session(str(path)).results == ROWS
    if route == "startup": editor = Editor(preferences=tmp_path / "prefs.json", workspace=path)
    else:
        editor.action("reset_results_view")
        editor.open_file(path)
    assert editor.results_view_info == expected and editor.results == ROWS


@pytest.mark.parametrize("mutation", ["version", "descending", "extra", "operator", "long"])
def test_bad_workspace_view_does_not_replace_current_results(mutation, tmp_path):
    editor = make_editor(tmp_path)
    view(editor, sort_column="value")
    valid, bad = tmp_path / "valid.ijx", tmp_path / "bad.ijx"
    editor.action("save_workspace", path=str(valid))
    saved = ij.load_session(str(valid)); extensions = copy.deepcopy(saved.extensions)
    record = extensions["imagejxx.results_view"]
    if mutation == "version": record["version"] = 2
    if mutation == "descending": record["descending"] = 1
    if mutation == "extra": record["extra"] = True
    if mutation == "operator": record["filter"] = dict(column="value", operator="sql")
    if mutation == "long": record["sort_column"] = "x" * 4097
    ij.save_session(str(bad), ij.SessionCapture(saved.documents, results=saved.results, workflow=saved.workflow, extensions=extensions))
    before = editor.results_view_info
    with pytest.raises((ValueError, RuntimeError)): editor.open_file(bad)
    assert editor.results_view_info == before and editor.results == ROWS


def test_legacy_workspace_resets_view_and_preserves_unrelated_extensions(tmp_path):
    editor = make_editor(tmp_path)
    view(editor, sort_column="value")
    path = tmp_path / "legacy.ijx"
    ij.save_session(str(path), ij.SessionCapture([], results=ROWS, extensions={"unrelated": {"keep": True}}))
    editor.open_file(path)
    assert not editor.results_view_info["active"] and source_rows(editor) == list(range(len(ROWS)))
    editor.action("save_workspace", path=str(tmp_path / "again.ijx"))
    assert ij.load_session(str(tmp_path / "again.ijx")).extensions["unrelated"] == {"keep": True}


def test_projection_admission_failure_keeps_current_view_and_reset_releases_cache(tmp_path):
    editor = make_editor(tmp_path, [dict(value=2), dict(value=1)])
    view(editor, sort_column="value")
    editor.update_settings(processing_memory_mb=1, persist=False)
    editor.results = [dict(value=2, label="a" * 300000), dict(value=1, label="b" * 300000)]
    before = editor.results_view_info
    with pytest.raises((ValueError, RuntimeError)):
        view(editor, sort_column="label")
    assert editor.results_view_info == before and editor.render_memory_info["working_bytes"] == 0
    editor.action("reset_results_view")
    assert editor.render_memory_info["retained_bytes"] == 0


def test_small_view_csv_can_share_one_megabyte_budget_with_its_projection(tmp_path):
    editor = make_editor(tmp_path, [dict(value=2), dict(value=1)])
    editor.update_settings(measurements=["value"], processing_memory_mb=1, persist=False)
    view(editor, sort_column="value")
    editor.action("save_results_view", path=str(tmp_path / "small.csv"))
    assert csv_rows(tmp_path / "small.csv") == [["value"], ["1"], ["2"]]
    assert editor.render_memory_info["working_bytes"] == 0


def assert_host(report, context, directory):
    assert report["contract"] == "results-view-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert report["native_exact_vectors"] and report["direct_cell_refresh"] and report["csv_admission_preserved"]
    assert report["cancel_preserved"] and report["invalid_preserved"] and report["reset_preserved"]
    assert report["source_protected"] and report["final_working_bytes"] == 0
    assert report["view"]["source_rows"] == [2, 1]
    assert report["view"]["source_row_count"] == 3 and report["view"]["visible_row_count"] == 2
    assert report["visible_table"]["view"]["visible_row_count"] == 2
    assert any(cell["row"] == 0 and cell["source_row"] == 2 and cell["name"] == "mean" and cell["text"] == "6.500" for cell in report["visible_table"]["cells"])
    assert report["copied_csv"] == (directory / "view.csv").read_text(encoding="utf-8")
    assert csv_rows(directory / "view.csv") == [["Image", "mean", "count"], ["Third", "6.500", "4"], ['V,"two\nlines', "11.500", "4"]]
    assert [row["mean"] for row in report["raw_rows"]] == [1.5, 11.5, 6.5]
    assert [row[1] for row in csv_rows(directory / "all.csv")[1:]] == ["1.500", "11.500", "6.500"]
    reopened = Editor(preferences=directory / "reader.json", workspace=directory / "view.ijx")
    assert reopened.results == report["raw_rows"] and reopened.results_view_info == report["view"]
    names = {row["name"] for row in report["controls"]}
    assert {"results.view", "results.reset_view", "results.copy_csv", "results.save_csv", "results.save_all_csv", "dialog.apply", "dialog.cancel", "dialog.match.choice.Greater than"} <= names


def test_python_actual_results_view(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    directory = tmp_path / "actual"
    assert_host(json.loads(editor._results_view_ui_case(str(directory))), "imgui_python", directory)


def test_standalone_actual_results_view():
    path = Path(os.environ["IMAGEJXX_RESULTS_VIEW_CPP_REPORT"])
    assert_host(json.loads(path.read_bytes()), "imgui_cpp", path.parent)
