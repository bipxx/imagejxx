"""Versioned ordered-manager Measure intent on the existing editor owner."""
import copy
import csv
import json

import imagejxx as ij
import pytest
from imagejxx import _desktop
from test_mixed_measurements import COLUMNS, document

COLUMNS = [name for name in COLUMNS if name not in {"XM", "YM", "BX", "BY", "Width", "Height"}]


def setup(tmp_path, indices=None, redirect="none"):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    source, sample = document(), document(True)
    editor.add_native_document(source)
    editor.add_native_document(sample)
    editor.active_index = 0
    editor.update_settings(json.dumps({"measurements": COLUMNS, "measurement_precision": 3,
        "measurement_scientific": True, "measurement_invert_y": True,
        "measurement_nan_empty_cells": True, "measurement_add_to_overlay": True,
        "measurement_limit_to_threshold": True, "reflex_angle": False}), False)
    if redirect != "none":
        editor.action("measurement_redirect", json.dumps({"document_id": source.id if redirect == "self" else sample.id}))
    before, undo = source.snapshot(), source.undo_steps
    editor.recording = True
    editor.action("measure_rois", json.dumps({} if indices is None else {"indices": indices}))
    editor.wait()
    assert not editor.error
    editor.recording = False
    step = json.loads(editor.workflow)[0]
    assert step["version"] == 2 and step["manager"]["indices"] == (indices or [])
    assert source.snapshot().shares_data(before) and source.undo_steps == undo and not source.snapshot().overlays
    return editor, source, sample, step


@pytest.mark.parametrize("indices", [None, [0, 5], [5, 0]], ids=["all", "subset", "ordered"])
@pytest.mark.parametrize("redirect", ["none", "other", "self"])
def test_record_repeated_replay_save_load_and_rebind(tmp_path, indices, redirect):
    editor, source, sample, step = setup(tmp_path, indices, redirect)
    original = json.loads(editor.results)
    assert len(original) == (7 if indices is None else 3)
    file = tmp_path/"manager.json"
    editor.action("save_workflow", json.dumps({"path": str(file)}))
    editor.workflow = "[]"
    editor.action("load_workflow", json.dumps({"path": str(file)}))
    for count in [2, 3]:
        editor.action("replay_measurement", json.dumps({"record": step}))
        editor.wait()
        assert not editor.error and json.loads(editor.results) == original*count
    # Active selection is not an input of whole-plane manager measurement.
    source.select(None)
    editor.action("replay_measurement", json.dumps({"record": step}))
    editor.wait()
    assert not editor.error and json.loads(editor.results) == original*4
    rebound, sampled = document(), document(True)
    editor.add_native_document(rebound)
    editor.add_native_document(sampled)
    editor.active_index = 2
    request = {"rebind": True, "document_id": rebound.id, "restore_settings": True}
    if redirect != "none":
        request["redirect_id"] = rebound.id if redirect == "self" else sampled.id
    editor.action("run_workflow", json.dumps(request))
    editor.wait()
    assert not editor.error
    expected = copy.deepcopy(original)
    if redirect != "none":
        for row in expected:
            row["Redirect document"] = request["redirect_id"]
    assert json.loads(editor.results) == original*4+expected and json.loads(editor.workflow) == [step]
    file = tmp_path/"manager.ijx"
    editor.action("save_workspace", json.dumps({"path": str(file)}))
    reopened = _desktop.Editor(str(tmp_path/"reopened-preferences.json"))
    reopened.open_legacy_file(str(file))
    assert json.loads(reopened.workflow) == [step] and reopened.results == editor.results
    editor.action("save_results", json.dumps({"path": str(tmp_path/"manager.csv")}))
    with (tmp_path/"manager.csv").open(encoding="utf-8", newline="") as stream:
        assert len(list(csv.DictReader(stream))) == len(original)*5


@pytest.mark.parametrize("change", ["name", "order", "geometry", "position", "attributes"])
def test_changed_manager_refuses_default_and_can_explicitly_adopt(tmp_path, change):
    editor, source, sample, step = setup(tmp_path)
    rows, intent = editor.results, editor.workflow
    rois = list(source.snapshot().rois)
    old = rois[0]
    name, selection, z, attributes = old.name, old.selection, old.z, old.attributes
    if change == "name":
        name = "changed"
    elif change == "order":
        rois[0], rois[1] = rois[1], rois[0]
    elif change == "geometry":
        selection = ij.Selection("rectangle", [[1, 1], [6, 5]])
    elif change == "position":
        z = 0
    else:
        attributes = {"retained": True}
    if change != "order":
        rois[0] = ij.ROI(name, selection, t=old.t, c=old.c, z=z, attributes=attributes)
    edit = source.begin_edit()
    edit.set_rois(rois)
    source.commit(edit)
    changed, undo = source.snapshot(), source.undo_steps
    with pytest.raises(ValueError, match="Manager changed"):
        editor.action("replay_measurement", json.dumps({"record": step}))
    assert editor.results == rows and editor.workflow == intent and source.snapshot().shares_data(changed)
    editor.action("replay_measurement", json.dumps({"record": step, "rebind": True}))
    editor.wait()
    assert not editor.error and len(json.loads(editor.results)) == 14
    assert source.snapshot().shares_data(changed) and source.undo_steps == undo and editor.workflow == intent


@pytest.mark.parametrize("field,value", [("indices", [0, 0]), ("indices", [6]), ("indices", [True]),
    ("indices", False), ("total", 0), ("fingerprint", "bad")], ids=["duplicate", "outside", "boolean", "array", "empty", "fingerprint"])
def test_invalid_manager_intent_is_atomic(tmp_path, field, value):
    editor, source, sample, step = setup(tmp_path)
    before, rows, intent = source.snapshot(), editor.results, editor.workflow
    bad = copy.deepcopy(step)
    bad["manager"][field] = value
    with pytest.raises(ValueError):
        editor.action("replay_measurement", json.dumps({"record": bad}))
    assert source.snapshot().shares_data(before) and editor.results == rows and editor.workflow == intent


def test_actual_manager_replay_controls_outputs_and_lifecycle(tmp_path):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    report = json.loads(editor._manager_measurement_replay_ui_case(str(tmp_path/"actual")))
    assert_actual_manager_replay_report(report)


def assert_actual_manager_replay_report(report):
    from test_mixed_measurements import REFERENCE, same

    assert report["state"] == "observed" and len(report["rows"]) == 32
    assert all(report[key] for key in ["source_preserved", "manager_overlay_noop", "resource_guards",
                                     "workspace_preserved", "selected_controls"])
    expected = next(case for case in REFERENCE["cases"] if case["id"] == "combined-manager")["rows"]
    assert len(report["original_manager"]) == len(expected) == 7
    for row, reference in zip(report["original_manager"], expected):
        assert row["Label"] == reference["label"]
        for field, value in reference["raw"].items():
            key = {"Mean": "mean", "Ch": "C", "Frame": "T", "Slice": "Z"}.get(field, field)
            if field in {"Ch", "Slice", "Frame"} and value == "nan":
                # Existing M4 contract: native raw TCZ stays captured; original
                # delayed table-column NaNs remain the separately owned T09 gap.
                assert row[key] == 2
                continue
            if key in row:
                same(row[key], value)
    selected, rebound = report["recorded_owners"], report["rebound_owners"]
    assert report["record"]["version"] == report["selected_record"]["version"] == 2
    assert report["record"]["manager"]["indices"] == []
    assert report["selected_record"]["manager"]["indices"] == [0, 2]
    assert report["record"]["source"]["document_id"] == selected["source"]
    assert report["record"]["redirect"]["document_id"] == selected["sampling"]
    assert report["selected_record"]["source"]["document_id"] == rebound["source"]
    assert report["selected_record"]["redirect"]["document_id"] == rebound["sampling"]
    assert report["rows"][:7] == report["rows"][7:14] == report["rows"][14:21]
    assert report["rows"][28:30] == report["rows"][30:32]
    assert report["all_review"].index("stored-0") < report["all_review"].index("stored-5")
    assert report["selected_review"].index("[1]") < report["selected_review"].index("[3]")
    assert set(report["review_refusals"]) == {"manager_name", "manager_order", "manager_geometry", "manager_position"}
    assert set(report["job_refusals"]) == {"manager_name", "manager_order", "manager_geometry", "manager_position",
        "cancel", "record_toggle", "workflow_clear", "settings", "results_clear", "sampling_close", "source_close"}
    for record, name in [(selected, f'measurement_redirect.{selected["sampling"]}'),
                         (rebound, "dialog.sampling.choice."+rebound["choice"])]:
        assert any(control["name"] == name and control["control"] == record["control"] for control in report["controls"])
    assert any(control["name"] == "manager.measure_selected" for control in report["controls"])
    assert report["csv"] == report["copied_csv"]
    exported = list(csv.DictReader(report["csv"].splitlines()))
    assert len(exported) == 32 and report["results_table"]["cells"]
    for cell in report["results_table"]["cells"]:
        assert cell["text"] == exported[cell["row"]][cell["name"]]


def test_standalone_manager_measurement_replay_report():
    import os
    from pathlib import Path

    report = os.environ.get("IMAGEJXX_MANAGER_MEASUREMENT_REPLAY_CPP_REPORT")
    if not report:
        pytest.skip("Standalone manager replay report not supplied")
    assert_actual_manager_replay_report(json.loads(Path(report).read_bytes()))
