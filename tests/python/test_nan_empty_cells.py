"""Allocation-time numeric defaults and real measurement/session consumers."""
import csv
import json
import math
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1]/"fixtures/nan-empty-cells-imagej154r.json").read_bytes())


def equal(actual, expected):
    if expected == "NaN":
        assert math.isnan(actual)
    else:
        assert actual == expected


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_original_allocation_and_normal_continuation(case):
    table = ij.ResultsTableState()
    table.nan_empty_cells = case["initial"]
    if case.get("reset"):
        table.append_rows([ij.ResultsTableRows(120, ["Old"])], case["initial"])
        table.clear()
        assert table.capacity == 100 and not table.column_names
        assert table.nan_empty_cells == case["initial"]
        table.append_rows([ij.ResultsTableRows(4, ["New"])], table.nan_empty_cells)
        equal(table.default_value(3, "New"), case["missing"])
        return
    flag = case["initial"]
    for row in range(case["rows"]):
        if row == case["toggle_before"]:
            flag = not flag
        columns = ["Counter"]
        if row == 0:
            columns.append("Early")
        if row == case["late_at"]:
            columns.append("Late")
        table.append_rows([ij.ResultsTableRows(1, columns)], flag)
    assert table.row_count == case["rows"] and table.capacity == case["capacity"]
    for cell in case["cells"]:
        if cell["row"] != 0:
            equal(table.default_value(cell["row"], "Early"), cell["early"])
        if cell["row"] != case["late_at"]:
            equal(table.default_value(cell["row"], "Late"), cell["late"])
    saved = table.record
    restored = ij.ResultsTableState.from_record(saved)
    restored.append_rows([ij.ResultsTableRows()], flag)
    equal(restored.default_value(case["rows"], "Early"), case["next_early"])
    equal(restored.default_value(case["rows"], "Late"), case["next_late"])
    assert table.record == saved


def test_strict_inputs_opaque_rows_limits_and_atomicity():
    table = ij.ResultsTableState()
    assert ij.ResultsTableState.from_record(table.record).record == table.record
    table.append_rows([ij.ResultsTableRows(1, [], False), ij.ResultsTableRows(1, ["X"])], True)
    assert table.default_value(0, "X") is None and math.isnan(table.default_value(1, "X"))
    assert table.default_value(1, "Other") is None
    for row in [True, 1.0, "1"]:
        with pytest.raises(TypeError):
            table.default_value(row, "X")
    with pytest.raises(IndexError):
        table.default_value(2, "X")
    class ChangingList(list):
        def __iter__(self):
            raise AssertionError("custom sequence must not run during admission")
    with pytest.raises(TypeError):
        table.append_rows(ChangingList([ij.ResultsTableRows()]), False)
    broken = table.record
    broken["columns"] = ChangingList(broken["columns"])
    with pytest.raises(TypeError):
        ij.ResultsTableState.from_record(broken)
    original = table.record
    with pytest.raises(ValueError):
        table.append_rows([ij.ResultsTableRows(1, ["X", "X"])], False)
    assert table.record == original
    for name, value in [("version", 2), ("rows", True), ("capacity", 200)]:
        broken = dict(original, **{name: value})
        with pytest.raises(ValueError):
            ij.ResultsTableState.from_record(broken)
    limits = ij.ResultsTableStateLimits()
    limits.max_working_bytes = 1024
    small = ij.ResultsTableState(limits)
    with pytest.raises(ValueError):
        small.append_rows([ij.ResultsTableRows(1, ["X"])], True)
    assert small.row_count == 0 and not small.nan_empty_cells


def test_measurement_defaults_csv_and_workspace_continuation(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=np.uint8).reshape(6, 8), axes="YX"))
    editor.add_native_document(owner)
    before = owner.snapshot()
    def settings(**values):
        editor.update_settings(json.dumps(values), False)
    def measure():
        editor.action("measure", "{}")
        editor.wait()
    def cell(row, column):
        return json.loads(editor.results_cell(row, column))
    assert ij.Settings()["measurement_nan_empty_cells"] is False
    settings(measurements=["mean"], measurement_precision=3)
    measure()
    settings(measurement_nan_empty_cells=True, measurements=["mean", "Skew"])
    measure()
    assert cell(0, "Skew") == "NaN"
    settings(measurement_nan_empty_cells=False, measurements=["mean"])
    measure()
    assert cell(0, "Skew") == "NaN" and cell(2, "Skew") == "NaN"
    settings(measurements=["mean", "Skew"])
    output = tmp_path/"results.csv"
    editor.action("save_results", json.dumps({"path": str(output)}))
    with output.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.reader(stream))
    assert rows[0] == ["mean", "Skew"] and rows[1][1] == rows[3][1] == "NaN"
    report = json.loads(editor.render_headless())
    assert next(c["text"] for c in report["results_table"]["cells"] if c["name"] == "Skew" and c["row"] == 0) == "NaN"
    workspace = tmp_path/"state.ijx"
    editor.action("save_workspace", json.dumps({"path": str(workspace)}))
    reopened = _desktop.Editor(str(tmp_path/"reopened-preferences.json"))
    reopened.open_legacy_file(str(workspace))
    assert json.loads(reopened.results_table_state) == json.loads(editor.results_table_state)
    assert json.loads(reopened.results_cell(0, "Skew")) == "NaN"
    reopened.update_settings(json.dumps({"measurement_nan_empty_cells": False, "measurements": ["mean"]}), False)
    reopened.action("measure", "{}")
    reopened.wait()
    assert json.loads(reopened.results_cell(3, "Skew")) == "NaN"
    assert owner.snapshot().shares_data(before)
    # Arbitrary raw replacement is opaque, even at the same row count.
    editor.results = json.dumps([{"Skew": None}, {"mean": True}, {"mean": {"x": 1}}])
    assert cell(0, "Skew") is None and cell(1, "Skew") is None
    assert cell(1, "mean") is True and cell(2, "mean") == {"x": 1}
    editor.results = "[]"
    state = json.loads(editor.results_table_state)
    assert state["rows"] == 0 and state["capacity"] == 100 and state["columns"] == []


def test_real_manager_growth_keeps_old_capacity_and_initializes_new_tail(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=np.uint8).reshape(6, 8), axes="YX"))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI(str(i), ij.Selection("rectangle", [[0, 0], [4, 2]])) for i in range(198)])
    owner.commit(edit)
    editor.add_native_document(owner)
    before = owner.snapshot()
    history = owner.undo_steps
    def measure(columns, nan, action="measure"):
        editor.update_settings(json.dumps({"measurements": columns, "measurement_nan_empty_cells": nan}), False)
        editor.action(action, "{}")
        editor.wait()
    measure(["mean"], False)
    measure(["mean", "Skew"], True)
    measure(["mean"], False, "measure_rois")
    assert len(json.loads(editor.results)) == 200
    assert json.loads(editor.results_cell(99, "Skew")) == "NaN"
    assert json.loads(editor.results_cell(100, "Skew")) == 0
    assert json.loads(editor.results_cell(199, "Skew")) == 0
    assert json.loads(editor.results_table_state)["capacity"] == 400
    measure(["mean"], True)
    # Capacity200 grew while flagfalse; the full new tail keeps zero after toggle.
    assert json.loads(editor.results_cell(200, "Skew")) == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == history


def test_actual_nan_preferences_and_outputs(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    report = json.loads(editor._nan_empty_cells_ui_case(str(tmp_path/"actual")))
    assert all(report[k] for k in ["workspace_preserved", "cancel_preserved", "sampling_close_preserved", "stale_setting_refused", "clear_preserved", "source_unchanged", "resource_guards", "opaque_replacement_preserved"])
    assert report["csv"] == report["copied_csv"]
    assert report["table_state"]["rows"] == 102


@pytest.mark.parametrize("field,value", [("version", 2), ("rows", 2), ("capacity", 99), ("nan_empty_cells", "true")])
def test_malformed_owned_session_state_refuses_before_document_adoption(tmp_path, field, value):
    from imagejxx import _desktop
    incoming = ij.Document(ij.Dataset(np.ones((2, 3), np.uint8), axes="YX"))
    state = ij.ResultsTableState()
    state.append_rows([ij.ResultsTableRows()], False)
    record = state.record
    record[field] = value
    path = tmp_path/"invalid.ijx"
    ij.save_session(str(path), ij.SessionCapture([incoming], results=[{"mean": 1.0}],
        extensions={"imagejxx.results_table_state": record, "custom": {"keep": "yes"}}))
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    existing = ij.Document(ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX"))
    editor.add_native_document(existing)
    editor.results = '[{"old": 1}]'
    before = editor.documents()
    with pytest.raises(ValueError):
        editor.open_legacy_file(str(path))
    assert editor.documents() == before and editor.results == '[{"old":1}]'


def test_legacy_session_keeps_missing_cells_opaque_and_unknown_extensions(tmp_path):
    from imagejxx import _desktop
    incoming = ij.Document(ij.Dataset(np.ones((2, 3), np.uint8), axes="YX"))
    path = tmp_path/"legacy.ijx"
    ij.save_session(str(path), ij.SessionCapture([incoming], results=[{"Skew": None}, {}],
        extensions={"custom": {"keep": "yes"}}))
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    editor.open_legacy_file(str(path))
    assert json.loads(editor.results_cell(0, "Skew")) is None
    assert json.loads(editor.results_cell(1, "Skew")) is None
    saved = tmp_path/"saved.ijx"
    editor.action("save_workspace", json.dumps({"path": str(saved)}))
    assert ij.load_session(str(saved)).extensions["custom"] == {"keep": "yes"}
