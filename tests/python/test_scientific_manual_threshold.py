"""Captured scientific threshold targets with independent pixel expectations."""
import copy
import json

import imagejxx as ij
import numpy as np
import pytest


def owner(dtype=np.uint16):
    t, c, z, y, x = np.indices((2, 2, 3, 5, 7))
    pixels = (10000*t + 1000*c + 100*z + 10*y + x).astype(dtype)
    native = ij.Document(ij.Dataset(pixels, axes="TCZYX"), "Captured threshold")
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [3, 2, 2]
    native.set_display(view)
    native.select(ij.Selection("rectangle", [(1, 1), (5, 4)]))
    native.mark_saved()
    return native, pixels


def position(command):
    p = command.target.position(0)
    return p.t, p.c, p.z


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_command_capture_strict_replay_and_explicit_rebind(dtype):
    native, pixels = owner(dtype)
    source = native.snapshot()
    command = ij.resolve_manual_threshold_command(source, ij.ManualThresholdRange(11212, 11233))
    record = command.record()
    assert position(command) == (1, 1, 2)
    assert record["selection"]["kind"] == "rectangle"
    assert record["target"]["positions"] == [[1, 1, 2]]
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 0]
    native.set_display(view)
    assert position(ij.manual_threshold_command_from_record(native.snapshot(), record)) == (1, 1, 2)
    other, _ = owner(dtype)
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.manual_threshold_command_from_record(other.snapshot(), record)
    native.select(ij.Selection("ellipse", [(0, 0), (3, 3)]))
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.manual_threshold_command_from_record(native.snapshot(), record)
    rebound = ij.rebind_manual_threshold_command(native.snapshot(), record, ij.TargetRequest())
    assert position(rebound) == (0, 0, 0)
    assert rebound.record()["selection"]["kind"] == "ellipse"
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), pixels)


@pytest.mark.parametrize("patch", [
    {"schema_version": True}, {"extra": 1}, {"lower": True},
    {"lower": 2**53+1}, {"lower": float("nan")}, {"upper": -1},
    {"profile": "unknown"}, {"selection": {"kind": "rectangle"}},
])
def test_record_rejects_malformed_before_rebinding(patch):
    native, _ = owner()
    record = ij.resolve_manual_threshold_command(native.snapshot(), ij.ManualThresholdRange(1, 2)).record()
    record.update(patch)
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.validate_manual_threshold_command_record(record)
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.rebind_manual_threshold_command(native.snapshot(), record, ij.TargetRequest())


def test_command_frozen_roi_apply_history_stale_and_cancel():
    native, pixels = owner()
    app = ij.Application()
    app.adopt(native)
    command = ij.resolve_manual_threshold_command(native.snapshot(), ij.ManualThresholdRange(11212, 11233))
    prepared = app.prepare("threshold", command.target, command.parameters())
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 0]
    native.set_display(view)
    before = native.history().undo_steps
    app.publish(prepared)
    expected = pixels.copy()
    for y in range(1, 4):
        for x in range(1, 5):
            expected[1, 1, 2, y, x] = 255 if 11212 <= pixels[1, 1, 2, y, x] <= 11233 else 0
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), expected)
    assert native.history().undo_steps == before + 1
    assert native.snapshot().display.t == 0
    native.undo()
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), pixels)
    native.redo()
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), expected)
    command = ij.resolve_manual_threshold_command(native.snapshot(), ij.ManualThresholdRange(0, 255))
    prepared = app.prepare("threshold", command.target, command.parameters())
    native.select(ij.Selection("rectangle", [(0, 0), (2, 2)]))
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    token = ij.CancellationToken()
    token.cancel()
    command = ij.resolve_manual_threshold_command(native.snapshot(), ij.ManualThresholdRange(0, 255))
    with pytest.raises(ij.OperationCancelled):
        app.prepare("threshold", command.target, command.parameters(), cancellation=token)
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), expected)


def test_scientific_to_selection_rebases_and_refuses_stale_source(tmp_path):
    from imagejxx import _desktop
    native, pixels = owner()
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    editor.add_native_document(native)
    editor.action("show_manual_threshold", "{}")
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 0]
    native.set_display(view)
    editor.action("manual_threshold_to_selection", '{"lower":11212,"upper":11233}')
    expected = ((pixels[1, 1, 2] >= 11212) & (pixels[1, 1, 2] <= 11233)).astype(np.uint8)*255
    np.testing.assert_array_equal(native.snapshot().selection.mask((5, 7)), expected)
    revision = native.snapshot().revisions.selection
    editor.action("manual_threshold_to_selection", '{"lower":11212,"upper":11233}')
    assert native.snapshot().revisions.selection == revision and native.undo_steps == 0
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), pixels)
    native.select(ij.Selection("ellipse", [(0, 0), (2, 2)]))
    with pytest.raises(ij.RevisionConflict):
        editor.action("apply_manual_threshold", '{"lower":11212,"upper":11233}')
    assert not editor.busy and native.undo_steps == 0


@pytest.mark.parametrize("layer", ["binding", "wrapper"])
def test_scientific_threshold_desktop_capture_record_replay_save(tmp_path, layer):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    wrapper = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    editor = wrapper if layer == "wrapper" else wrapper._editor
    native, pixels = owner()
    editor.add_native_document(native)
    def action(name, **parameters):
        if layer == "wrapper": editor.action(name, **parameters)
        else: editor.action(name, json.dumps(parameters))
    action("show_manual_threshold")
    action("set_manual_threshold", lower=11212, upper=11233)
    state = native.snapshot().display.properties["manual_threshold"]
    assert (state["t"], state["c"], state["z"]) == (1, 1, 2)
    view = native.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 0]
    native.set_display(view)
    editor.recording = True
    action("apply_manual_threshold", lower=11212, upper=11233)
    editor.wait()
    expected = pixels.copy()
    for y in range(1, 4):
        for x in range(1, 5):
            expected[1, 1, 2, y, x] = 255 if 11212 <= pixels[1, 1, 2, y, x] <= 11233 else 0
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), expected)
    workflow = editor.workflow
    if isinstance(workflow, str): workflow = json.loads(workflow)
    assert len(workflow) == 1
    record = workflow[0]
    assert record["target"]["positions"] == [[1, 1, 2]]
    assert "manual_threshold" not in native.snapshot().display.properties
    with pytest.raises((ValueError, ij.RevisionConflict)):
        action("replay_manual_threshold", record=record)
    action("undo")
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), pixels)
    # Explicit destination request is required after Undo changes revisions.
    action("replay_manual_threshold", record=record, threshold_rebind={
        "scope": "positions", "positions": [[1, 1, 2]], "roi": "active_selection"})
    editor.wait()
    np.testing.assert_array_equal(native.snapshot().dataset.numpy("TCZYX"), expected)
    path = tmp_path / "threshold.ijx"
    action("save_document", path=str(path))
    reopened = Editor(preferences=Preferences(tmp_path / "reopened.json"))
    reopened.open_file(str(path))
    np.testing.assert_array_equal(reopened._editor.native_snapshot().dataset.numpy("TCZYX"), expected)
