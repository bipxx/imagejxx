"""Everyday editor composition, using physical widgets and an independent oracle.

The fixed two-column UInt8 sigma1 fixture is [0,255] -> [77,178].
An area selection writes only the first column; no native processing function
generates expected pixels. This is a workflow check, not full Gaussian parity.
"""
import csv
import json
import os
from pathlib import Path

import numpy as np
import pytest
from imagejxx import _desktop
from test_binary_options import click, typing


def action(editor, name, **parameters):
    editor.action(name, json.dumps(parameters))


def frame(editor, events=()):
    result = json.loads(editor.render_headless(events=json.dumps(list(events))))
    assert not result["window_error"]
    return result


def pixels(editor):
    return editor.native_snapshot().dataset.numpy("YX")


@pytest.mark.parametrize("save_action", ["save_document", "save_workspace"])
def test_open_select_preview_apply_undo_measure_csv_save_reopen(tmp_path, save_action):
    source = np.tile(np.array([0, 255], np.uint8), (8, 1))
    expected = np.tile(np.array([77, 255], np.uint8), (8, 1))
    source_path = tmp_path / "source.pgm"
    source_path.write_bytes(b"P5\n2 8\n255\n" + source.tobytes())
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    editor.open_file(str(source_path))
    editor.select(json.dumps({"kind": "rectangle", "points": [[0, 0], [1, 8]]}))
    before = editor.native_snapshot()
    action(editor, "show_operation", operation="gaussian_blur_imagej")
    panel = frame(editor)["gaussian_panel"]
    # Gaussian's Enter activates the default OK. Tab leaves the numeric field
    # so Preview can be exercised before accepting the processing command.
    events = typing(panel["sigma_point"], "1")[:-2]
    events += [{"type": "key", "name": "Tab", "down": True},
               {"type": "key", "name": "Tab", "down": False}]
    panel = frame(editor, events)["gaussian_panel"]
    assert panel["open"] and panel["sigma"] == 1
    frame(editor, click(panel["preview_point"]))
    editor.wait()
    preview = frame(editor)
    assert preview["gaussian_panel"]["preview"]
    np.testing.assert_array_equal(pixels(editor), source)
    assert editor.native_snapshot().shares_data(before)

    editor.recording = True
    frame(editor, click(preview["gaussian_panel"]["ok_point"]))
    editor.wait()
    np.testing.assert_array_equal(pixels(editor), expected)
    assert editor.native_document().undo_steps == 1
    recorded = json.loads(editor.workflow)
    assert len(recorded) == 1
    action(editor, "undo")
    np.testing.assert_array_equal(pixels(editor), source)
    action(editor, "redo")
    np.testing.assert_array_equal(pixels(editor), expected)

    action(editor, "measure")
    rows = json.loads(editor.results)
    assert len(rows) == 1 and rows[0]["count"] == 8
    assert rows[0]["mean"] == 77 and rows[0]["sum"] == 616
    assert rows[0]["Area"] == 8
    csv_path = tmp_path / "measurements.csv"
    action(editor, "save_results", path=str(csv_path))
    with csv_path.open(newline="", encoding="utf-8") as f:
        exported = list(csv.DictReader(f))
    assert len(exported) == 1 and float(exported[0]["mean"]) == 77

    session = tmp_path / "edited.ijx"
    action(editor, save_action, path=str(session))
    reopened = _desktop.Editor(str(tmp_path / "reopened-preferences.json"))
    reopened.open_file(str(session))
    np.testing.assert_array_equal(pixels(reopened), expected)
    assert reopened.native_snapshot().selection.kind == "rectangle"
    assert json.loads(reopened.results) == rows
    assert json.loads(reopened.workflow) == recorded
    assert json.loads(editor.render_memory_info())["working_bytes"] == 0


@pytest.mark.parametrize("save_action", ["save_document", "save_workspace"])
def test_standalone_everyday_workflow_observations(save_action):
    root = os.environ.get("IMAGEJXX_EVERYDAY_CPP_ROOT")
    if not root:
        pytest.skip("Requires the standalone native everyday workflow capture")
    report = json.loads((Path(root) / save_action / "workflow.json").read_bytes())
    assert report["context"] == "imgui_cpp" and report["save_action"] == save_action
    assert report["pixels"] == [77, 255] * 8
    assert len(report["measurements"]) == 1
    assert report["measurements"][0]["count"] == 8
    assert report["measurements"][0]["mean"] == 77
    assert report["measurements"][0]["sum"] == 616
    assert len(report["workflow"]) == 1
    command = report["workflow"][0]
    assert command["operation"] == "gaussian_blur_imagej"
    assert command["options"] == {"sigma": 1, "scaled_units": False}
    assert command["target"]["roi_policy"] == "active_selection"
    events = [event for frame_events in report["events"] for event in frame_events]
    assert {event["type"] for event in events} >= {"position", "button", "text", "key"}
    assert any(event.get("value") == "1" for event in events)
