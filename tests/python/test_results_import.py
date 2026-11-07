"""CSV transport preserves explicit cells, exact IDs and managed-row provenance."""
import csv
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


def editor_at(tmp_path):
    return Editor(preferences=tmp_path / "prefs.json")


def source(tmp_path, text, name="input.csv"):
    path = tmp_path / name
    path.write_bytes(text.encode("utf-8") if isinstance(text, str) else text)
    return path


def load(editor, path, **options):
    editor.action("import_results", path=str(path), **options)


def csv_rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.reader(stream))


def table_state(editor):
    return json.loads(editor._editor.results_table_state)


def snapshot(editor):
    return (editor.results, editor.results_view_info, table_state(editor),
            editor.settings.to_dict())


def test_quoted_utf8_exact_ids_and_visible_column_order_without_document(tmp_path):
    editor = editor_at(tmp_path)
    path = source(tmp_path, '\ufefflabel,id,value,blank\r\n"α,\r\n""x""",18446744073709551615,-1.25e2,\r\nlast,001,NaN,""')
    load(editor, path)
    assert editor.results == [dict(label='α,\r\n"x"', id=2**64-1, value=-125.0, blank=""),
                              dict(label="last", id="001", value="NaN", blank="")]
    assert editor.settings["measurements"] == ["label", "id", "value", "blank"]
    editor.action("save_results_view", path=str(tmp_path / "out.csv"))
    assert csv_rows(tmp_path / "out.csv")[0] == ["label", "id", "value", "blank"]
    assert csv_rows(tmp_path / "out.csv")[1][:2] == ['α,\r\n"x"', str(2**64-1)]
    assert editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("text, expected", [
    ("0", 0), ("-9223372036854775808", -(2**63)),
    ("9223372036854775808", 2**63), ("18446744073709551615", 2**64-1),
    ("9007199254740993", 9007199254740993), ("1.25e2", 125.0),
    ("-0.0", -0.0), ("001", "001"), ("+1", "+1"), (" 1", " 1"),
    ("NaN", "NaN"), ("Infinity", "Infinity"), ("1e999", "1e999"),
    ("1e-999", "1e-999"), ("18446744073709551616", "18446744073709551616"),
    ("-9223372036854775809", "-9223372036854775809"), ("", ""),
])
def test_auto_conversion_has_explicit_exact_boundaries(tmp_path, text, expected):
    editor = editor_at(tmp_path)
    load(editor, source(tmp_path, 'value\n"' + text + '"'))
    assert editor.results[0]["value"] == expected
    assert type(editor.results[0]["value"]) is type(expected)
    if text == "-0.0": assert np.signbit(editor.results[0]["value"])


def test_explicit_text_number_and_blank_policies(tmp_path):
    editor = editor_at(tmp_path)
    load(editor, source(tmp_path, "id,value,blank\n123,1.5,"),
         column_types=dict(id="text", value="number", blank="number"))
    assert editor.results == [dict(id="123", value=1.5, blank="")]


@pytest.mark.parametrize("text", ["001", "+1", "word", "NaN", "Infinity", "1e999", "1e-999", "18446744073709551616"])
def test_number_refuses_atomically(tmp_path, text):
    editor = editor_at(tmp_path)
    editor.results = [dict(value=7)]
    editor.action("set_results_view", sort_column="value")
    before = snapshot(editor)
    with pytest.raises((ValueError, RuntimeError)):
        load(editor, source(tmp_path, "value\n" + text), column_types=dict(value="number"))
    assert snapshot(editor) == before and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("text", [b"", b"\xef\xbb\xbf", b"a,a\n1,2", b"a,\n1,2", b"a\n1,2",
    b"a,b\n1", b'a\n"unclosed', b'a\n"closed"x', b'a\nx"y', b"a\r1", b"a\n\xff", b"a\n\x00"])
def test_bad_csv_never_replaces_existing_table(tmp_path, text):
    editor = editor_at(tmp_path)
    editor.results = [dict(value=7)]
    before = snapshot(editor)
    with pytest.raises((ValueError, RuntimeError)):
        load(editor, source(tmp_path, text))
    assert snapshot(editor) == before and editor.render_memory_info["working_bytes"] == 0


@pytest.mark.parametrize("options", [dict(mode="bad"), dict(mode=True), dict(column_types=[]),
    dict(column_types={"value": "float"}), dict(column_types={"absent": "text"}),
    dict(show_columns=1), dict(unknown=True)])
def test_strict_options_are_rejected_before_mutation(tmp_path, options):
    editor = editor_at(tmp_path)
    editor.results = [dict(value=7)]
    before = snapshot(editor)
    with pytest.raises((ValueError, RuntimeError)):
        load(editor, source(tmp_path, "value\n1"), **options)
    assert snapshot(editor) == before


def test_append_preserves_old_values_columns_and_view_then_replace_resets(tmp_path):
    editor = editor_at(tmp_path)
    editor.results = [dict(value=3, old="keep")]
    editor.update_settings(measurements=["old", "value"], persist=False)
    editor.action("set_results_view", sort_column="value")
    path = source(tmp_path, "value,new\n2,yes\n1,no")
    load(editor, path, mode="append")
    assert editor.results == [dict(value=3, old="keep"), dict(value=2, new="yes"), dict(value=1, new="no")]
    assert editor.results_view_info["source_rows"] == [2, 1, 0]
    assert editor.settings["measurements"] == ["old", "value", "new"]
    load(editor, path)
    assert editor.results == [dict(value=2, new="yes"), dict(value=1, new="no")]
    assert not editor.results_view_info["active"] and editor.settings["measurements"] == ["value", "new"]


def test_header_only_and_show_columns_opt_out(tmp_path):
    editor = editor_at(tmp_path)
    load(editor, source(tmp_path, "z,a\n"))
    assert editor.results == [] and editor.settings["measurements"] == ["z", "a"]
    editor.action("save_results_view", path=str(tmp_path / "empty.csv"))
    assert csv_rows(tmp_path / "empty.csv") == [["z", "a"]]
    long_name = "x" * 300
    path = source(tmp_path, long_name + "\n1")
    before = snapshot(editor)
    with pytest.raises(ValueError): load(editor, path)
    assert snapshot(editor) == before
    load(editor, path, show_columns=False)
    assert editor.results == [{long_name: 1}] and editor.settings["measurements"] == ["z", "a"]


def test_imported_rows_stay_opaque_between_real_measurements(tmp_path):
    editor = editor_at(tmp_path)
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    before = owner.snapshot()
    editor.update_settings(measurements=["mean"], persist=False)
    editor.action("measure"); editor.wait()
    load(editor, source(tmp_path, "note\nexternal"), mode="append", show_columns=False)
    editor.update_settings(measurements=["mean", "Skew"], measurement_nan_empty_cells=True, persist=False)
    editor.action("measure"); editor.wait()
    state = ij.ResultsTableState.from_record(table_state(editor))
    assert state.row_count == 3 and np.isnan(state.default_value(0, "Skew"))
    assert state.default_value(1, "Skew") is None and editor.results[1] == dict(note="external")
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    assert editor.render_memory_info["working_bytes"] == 0


def test_scripted_measurement_is_published_before_following_import(tmp_path):
    editor = editor_at(tmp_path)
    owner = ij.Document(ij.Dataset(np.arange(4, dtype=np.uint8).reshape(2, 2), axes="YX"))
    editor.add_native_document(owner)
    path = source(tmp_path, "value\n1")
    editor.action("measure")
    assert len(editor.results) == 1 and editor.results[0]["mean"] == 1.5
    load(editor, path, mode="append", show_columns=False)
    assert len(editor.results) == 2 and editor.results[0]["mean"] == 1.5 and editor.results[1] == dict(value=1)


@pytest.mark.parametrize("route", ["open", "startup"])
def test_workspace_preserves_imported_cells_opaque_state_and_current_view(tmp_path, route):
    editor = editor_at(tmp_path)
    load(editor, source(tmp_path, "id,value\n18446744073709551615,2\n001,1"))
    editor.action("set_results_view", sort_column="value")
    rows, view, state = editor.results, editor.results_view_info, table_state(editor)
    path = tmp_path / "import.ijx"
    editor.action("save_workspace", path=str(path))
    assert ij.load_session(str(path)).results == rows
    if route == "open":
        editor.results = []
        editor.open_file(path)
    else: editor = Editor(preferences=tmp_path / "new-prefs.json", workspace=path)
    assert (editor.results, editor.results_view_info, table_state(editor)) == (rows, view, state)


def test_input_admission_failure_and_small_success_release_working_memory(tmp_path):
    editor = editor_at(tmp_path)
    editor.update_settings(processing_memory_mb=1, persist=False)
    editor.results = [dict(value=7)]
    before = snapshot(editor)
    with pytest.raises((ValueError, RuntimeError)):
        load(editor, source(tmp_path, "value\n" + "x" * 100000))
    assert snapshot(editor) == before and editor.render_memory_info["working_bytes"] == 0
    load(editor, source(tmp_path, "value\n2\n1", "small.csv"))
    editor.action("set_results_view", sort_column="value")
    assert editor.results_view_info["source_rows"] == [1, 0]
    assert editor.render_memory_info["working_bytes"] == 0
    editor.results = []
    editor.action("reset_results_view")
    assert editor.render_memory_info["retained_bytes"] == 0


def assert_host(report, context, directory):
    assert report["contract"] == "results-import-ui.001" and report["context"] == context
    assert report["state"] == "observed" and not report["acceptance_claim"]
    assert all(report[name] for name in ["native_exact_vectors", "pending_preserved", "cancel_preserved", "invalid_preserved", "stale_preserved", "source_protected"])
    assert report["append_rows"] == [dict(Label="original", id=9, value=10),
        dict(Label='alpha,\n"quoted"', id=2**64-1, value=2.5), dict(Label="beta", id="001", value=1)]
    assert report["append_view"]["source_rows"] == [2, 1, 0]
    assert report["replace_rows"] == report["append_rows"][1:] and not report["replace_view"]["active"]
    assert report["review"]["values"]["mode"] == "Append" and report["review"]["values"]["column_2"] == "Number"
    assert report["copied_csv"] == (directory / "view.csv").read_text(encoding="utf-8")
    assert csv_rows(directory / "view.csv") == [["Label", "id", "value"], ["beta", "001", "1"],
        ['alpha,\n"quoted"', str(2**64-1), "2.500"], ["original", "9", "10"]]
    assert any(c["source_row"] == 2 and c["name"] == "value" and c["text"] == "1" for c in report["visible_table"]["cells"])
    reader = Editor(preferences=directory / "reader.json", workspace=directory / "import.ijx")
    assert reader.results == report["append_rows"] and reader.results_view_info == report["append_view"]
    assert report["final_working_bytes"] == 0
    names = {row["name"] for row in report["controls"]}
    assert {"results.import_csv", "file.submit", "dialog.apply", "dialog.cancel", "dialog.mode.choice.Append", "dialog.column_2.choice.Number", "results.copy_csv", "results.save_csv"} <= names


def test_python_actual_results_import(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "prefs.json"))
    directory = tmp_path / "actual"
    assert_host(json.loads(editor._results_import_ui_case(str(directory))), "imgui_python", directory)


def test_standalone_actual_results_import():
    path = Path(os.environ["IMAGEJXX_RESULTS_IMPORT_CPP_REPORT"])
    assert_host(json.loads(path.read_bytes()), "imgui_cpp", path.parent)
