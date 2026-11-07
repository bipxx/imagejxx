"""Portable Measure recording, explicit two-owner rebind and atomic refusals."""
import copy
import csv
import json

import pytest
from imagejxx import _desktop
from test_mixed_measurements import COLUMNS, document


def setup(tmp_path, redirect="none"):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    source, sample = document(), document(True)
    editor.add_native_document(source)
    editor.add_native_document(sample)
    editor.active_index = 0
    settings = {"measurements": COLUMNS, "measurement_precision": 3, "measurement_scientific": True,
                "measurement_invert_y": True, "measurement_nan_empty_cells": True,
                "measurement_add_to_overlay": True, "measurement_limit_to_threshold": True, "reflex_angle": False}
    editor.update_settings(json.dumps(settings), False)
    if redirect != "none":
        editor.action("measurement_redirect", json.dumps({"document_id": source.id if redirect == "self" else sample.id}))
    editor.recording = True
    editor.action("measure", "{}")
    editor.wait()
    assert not editor.error
    editor.recording = False
    workflow = json.loads(editor.workflow)
    assert len(workflow) == 1 and workflow[0]["schema"] == "imagejxx.measurement-command"
    assert workflow[0]["settings"] == settings
    return editor, source, sample, workflow[0]


@pytest.mark.parametrize("redirect", ["none", "other", "self"])
def test_record_save_load_default_replay_and_explicit_rebind(tmp_path, redirect):
    editor, source, sample, step = setup(tmp_path, redirect)
    initial = json.loads(editor.results)
    assert len(initial) == 1 and source.snapshot().overlays[0]["name"] == "1"
    workflow_file = tmp_path/"measure-workflow.json"
    editor.action("save_workflow", json.dumps({"path": str(workflow_file)}))
    assert json.loads(workflow_file.read_bytes())["steps"] == [step]
    editor.workflow = "[]"
    editor.action("load_workflow", json.dumps({"path": str(workflow_file)}))
    assert json.loads(editor.workflow) == [step]
    before = source.snapshot()
    editor.update_settings(json.dumps({"measurement_precision": 1, "measurements": ["mean"]}), False)
    with pytest.raises(ValueError, match="settings differ"):
        editor.action("replay_measurement", json.dumps({"record": step}))
    assert source.snapshot().shares_data(before) and json.loads(editor.results) == initial
    editor.action("replay_measurement", json.dumps({"record": step, "restore_settings": True}))
    editor.wait()
    assert not editor.error
    rows = json.loads(editor.results)
    assert rows == initial*2
    assert [row["name"] for row in source.snapshot().overlays] == ["1", "2"]
    assert json.loads(editor.workflow) == [step]
    # Own annotation output is excluded from the input fingerprint: the same
    # immutable saved record remains usable after multiple overlay publications.
    editor.action("replay_measurement", json.dumps({"record": step}))
    editor.wait()
    assert not editor.error and json.loads(editor.results) == initial*3
    assert [row["name"] for row in source.snapshot().overlays] == ["1", "2", "3"]
    rows = json.loads(editor.results)
    stable_source, stable_sample = source.snapshot(), sample.snapshot()
    rebound, rebound_sample = document(), document(True)
    editor.add_native_document(rebound)
    editor.add_native_document(rebound_sample)
    editor.active_index = 2
    request = {"record": step, "rebind": True, "document_id": rebound.id, "restore_settings": True}
    if redirect != "none":
        request["redirect_id"] = rebound.id if redirect == "self" else rebound_sample.id
    editor.action("run_workflow", json.dumps({key: value for key, value in request.items() if key != "record"}))
    editor.wait()
    assert not editor.error
    final = json.loads(editor.results)
    assert len(final) == 4 and final[:3] == rows
    expected = copy.deepcopy(initial[0])
    if redirect != "none":
        expected["Redirect document"] = request["redirect_id"]
    assert final[-1] == expected
    assert source.snapshot().shares_data(stable_source) and sample.snapshot().shares_data(stable_sample)
    assert rebound.snapshot().overlays[0]["name"] == "4" and json.loads(editor.workflow) == [step]
    output = tmp_path/"measure.csv"
    editor.action("save_results", json.dumps({"path": str(output)}))
    with output.open(encoding="utf-8", newline="") as stream:
        exported = list(csv.DictReader(stream))
    assert len(exported) == 4 and exported[0]["mean"] == exported[3]["mean"]
    assert "E" in exported[3]["mean"] and exported[3]["Label"] == expected["Label"]
    workspace = tmp_path/"recorded.ijx"
    editor.action("save_workspace", json.dumps({"path": str(workspace)}))
    reopened = _desktop.Editor(str(tmp_path/"reopened-preferences.json"))
    reopened.open_legacy_file(str(workspace))
    assert json.loads(reopened.workflow) == [step] and json.loads(reopened.results) == final


@pytest.mark.parametrize("path,value", [
    (("version",), 2), (("version",), True), (("source", "document_id"), 0),
    (("source", "position"), [0, 0, 99]), (("source", "shape"), [2, 2, 2, 6, 8, 3]),
    (("source", "pixel_type"), 3), (("settings", "measurement_invert_y"), "true"),
    (("extra",), "a"*131065),
    (("source", "shape"), [2, 2, 2, 7, 8, 1]),
    (("source", "input_fingerprint"), "A"*64),
], ids=["version", "version-type", "empty-owner", "outside-position", "rgb", "float64", "boolean-type", "text-limit", "mismatched-shape", "fingerprint"])
def test_malformed_intent_preserves_source_results_and_recorder(tmp_path, path, value):
    editor, source, sample, step = setup(tmp_path)
    before, other_before = source.snapshot(), sample.snapshot()
    rows, workflow = editor.results, editor.workflow
    bad = copy.deepcopy(step)
    cursor = bad
    for name in path[:-1]:
        cursor = cursor[name]
    cursor[path[-1]] = value
    with pytest.raises(ValueError):
        editor.action("replay_measurement", json.dumps({"record": bad, "restore_settings": True}))
    assert editor.results == rows and editor.workflow == workflow
    assert source.snapshot().shares_data(before) and sample.snapshot().shares_data(other_before)


def test_invalid_loaded_command_leaves_existing_workflow_and_closed_owner_rebind(tmp_path):
    editor, source, sample, step = setup(tmp_path, "other")
    previous = editor.workflow
    bad = copy.deepcopy(step)
    bad["version"] = 2
    file = tmp_path/"unsupported.json"
    file.write_text(json.dumps({"format": "imagejxx-workflow", "version": 1, "steps": [bad]}), encoding="utf-8")
    with pytest.raises(ValueError):
        editor.action("load_workflow", json.dumps({"path": str(file)}))
    assert editor.workflow == previous
    editor.close_document(sample.id, True)
    before, rows = source.snapshot(), editor.results
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("replay_measurement", json.dumps({"record": step}))
    assert editor.results == rows and source.snapshot().shares_data(before)


def test_actual_measurement_replay_controls_outputs_and_lifecycle(tmp_path):
    editor = _desktop.Editor(str(tmp_path/"preferences.json"))
    report = json.loads(editor._measurement_replay_ui_case(str(tmp_path/"actual")))
    assert_actual_measurement_replay_report(report)


def assert_actual_measurement_replay_report(report):
    """Shared original/output checks for either maintained ImGui host receipt."""
    from test_mixed_measurements import REFERENCE, same

    assert report["state"] == "observed" and len(report["rows"]) == 4
    assert all(report[key] for key in ["saved_loaded", "settings_review", "review_cancel", "undo_redo",
                                     "workspace_preserved", "resource_guards", "source_preserved"])
    assert set(report["review_refusals"]) == {"source_pose", "sampling_pose", "source_threshold",
                                             "sampling_threshold", "global_calibration", "active_owner"}
    assert set(report["job_refusals"]) == {"cancel", "record_toggle", "workflow_clear", "source_pose",
        "sampling_threshold", "global_calibration", "settings", "results_clear", "sampling_close", "source_close"}
    original = next(case for case in REFERENCE["cases"] if case["id"] == "combined-redirect")["rows"][0]
    selected, rebound = report["recorded_owners"], report["rebound_owners"]
    assert report["record"]["source"]["document_id"] == selected["source"]
    assert report["record"]["redirect"]["document_id"] == selected["sampling"]
    assert report["rows"][:3] == [report["rows"][0]]*3
    for index, row in enumerate(report["rows"]):
        assert row["Redirect document"] == (selected if index < 3 else rebound)["sampling"]
        assert row["Label"] == original["label"]
        for field, expected in original["raw"].items():
            key = {"Mean": "mean", "Ch": "C", "Frame": "T", "Slice": "Z"}.get(field, field)
            if key in row:
                same(row[key], expected)
    assert [row["name"] for row in report["source_overlays"]] == ["1", "2", "3"]
    assert [row["name"] for row in report["rebound_overlays"]] == ["4"]
    assert any(c["name"] == f'measurement_redirect.{selected["sampling"]}' and c["control"] == selected["control"]
               for c in report["controls"])
    assert any(c["name"] == "dialog.sampling.choice."+rebound["choice"] and c["control"] == rebound["control"]
               for c in report["controls"])
    assert report["csv"] == report["copied_csv"]
    exported = list(csv.DictReader(report["csv"].splitlines()))
    assert len(exported) == 4 and report["results_table"]["cells"]
    for cell in report["results_table"]["cells"]:
        assert cell["text"] == exported[cell["row"]][cell["name"]]


def test_standalone_measurement_replay_report():
    import os
    from pathlib import Path

    report = os.environ.get("IMAGEJXX_MEASUREMENT_REPLAY_CPP_REPORT")
    if not report:
        pytest.skip("Standalone replay report not supplied")
    assert_actual_measurement_replay_report(json.loads(Path(report).read_bytes()))
