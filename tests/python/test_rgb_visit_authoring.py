"""Admitted authored RGB histories preserve the live visit and frozen stages."""
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_contrast_apply import rgb_options


PIXELS = np.full((2, 2, 2, 2, 4, 3), 30, np.uint8)


def step(lo=0, hi=255, channels=7, selection=None):
    value = ij.RgbContrastSessionStep()
    value.options = rgb_options(lo, hi, channels)
    value.selection = selection
    return value


def source():
    reads = ij.DatasetReadObserver()
    return ij.Document(ij.Dataset(PIXELS, axes="TCZYXS").observe_reads(reads)), reads


def plane(axis):
    p = [0, 0, 0]
    p[axis] = 1
    return p


def expected(axis, channels, operation):
    output = PIXELS.copy()
    other = tuple(plane(axis))
    for c, bit in enumerate((4, 2, 1)):
        if channels & bit:
            output[other+(slice(None), slice(None), c)] = (
                128 if operation == "insert" else 149 if operation == "move" else 45)
            if operation in ("reposition", "transfer"):
                output[0, 0, 0, :, :, c] = 85
    return output


@pytest.mark.parametrize("axis", range(3))
@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("operation", ["insert", "move", "reposition", "remove", "transfer"])
def test_native_visit_authoring_exact_order_replay_and_history(axis, channels, operation):
    owner, reads = source()
    before = owner.snapshot()
    editor = ij.RgbContrastSessionEditor(before)
    p = ij.PlanePosition(*plane(axis))
    editor.insert_saved_visit(0, p, [step(10, 70, channels), step()])
    editor.insert_saved_visit(1, p, [step(0, 170, channels)])
    saved = editor.command.record()
    if operation == "move":
        editor.move_saved_visit(1, 0)
    elif operation == "reposition":
        editor.set_saved_visit_position(0, ij.PlanePosition())
    elif operation == "remove":
        editor.remove_saved_visit(0)
    elif operation == "transfer":
        editor.transfer_saved_stage(0, 0, 2, 0)
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    assert editor.plan.visits[-1].position.z == 0
    assert editor.plan.visits[-1].stages[-1].options.maximum == 255
    assert saved["visits"][0]["position"] == plane(axis)
    command = ij.rgb_contrast_session_command_from_record(before, json.loads(json.dumps(editor.command.record())))
    edit = ij.execute_rgb_contrast_session_command(command)
    np.testing.assert_array_equal(edit.preview().dataset.numpy("TCZYXS"), expected(axis, channels, operation))
    owner.commit(edit)
    assert owner.undo_steps == 1 and owner.snapshot().display.crosshair == before.display.crosshair
    owner.undo()
    assert owner.snapshot().shares_data(before)
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected(axis, channels, operation))


@pytest.mark.parametrize("method,args", [
    ("insert_saved_visit", (1, ij.PlanePosition(), [step()])),
    ("insert_saved_visit", (0, ij.PlanePosition(), [])),
    ("insert_saved_visit", (0, ij.PlanePosition(0, 0, 2), [step()])),
    ("insert_saved_visit", (0, ij.PlanePosition(), [step(40, 10)])),
    ("insert_saved_visit", (True, ij.PlanePosition(), [step()])),
    ("remove_saved_visit", (0,)),
    ("set_saved_visit_position", (0, ij.PlanePosition())),
    ("move_saved_visit", (0, 0)),
    ("move_saved_visit", (-1, 0)),
    ("transfer_saved_stage", (0, 0, 0, 0)),
])
def test_invalid_authoring_preserves_complete_command_without_reads(method, args):
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    before = editor.command.record()
    with pytest.raises((ValueError, TypeError, IndexError, OverflowError)):
        getattr(editor, method)(*args)
    assert editor.command.record() == before and reads.counts().plane_reads == 0


@pytest.mark.parametrize("failure", ["live", "empty", "destination", "draft", "same", "invalid_position", "budget"])
def test_historical_transfer_and_visit_refusals_are_atomic(failure):
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.insert_saved_visit(0, ij.PlanePosition(0, 0, 1), [step(10, 70)])
    if failure == "budget":
        limits = editor.limits
        limits.max_record_bytes = 262144
        editor.set_limits(limits)
    before = editor.command.record()
    with pytest.raises((ValueError, TypeError, IndexError, OverflowError)):
        if failure == "live": editor.remove_saved_visit(1)
        elif failure == "empty": editor.transfer_saved_stage(0, 0, 1, 0)
        elif failure == "destination": editor.transfer_saved_stage(0, 0, 2, 0)
        elif failure == "draft": editor.transfer_saved_stage(1, 0, 0, 0)
        elif failure == "same": editor.transfer_saved_stage(0, 0, 0, 0)
        elif failure == "invalid_position": editor.set_saved_visit_position(0, ij.PlanePosition(-1, 0, 0))
        else: editor.insert_saved_visit(0, ij.PlanePosition(), [step()]*1000)
    assert editor.command.record() == before and reads.counts().plane_reads == 0


def test_transfer_retains_frozen_selection_policy_and_detached_inputs():
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    selected = ij.Selection("rectangle", [(0, 0), (2, 1)])
    value = step(10, 70, 4, selected)
    value.whole_plane = True
    editor.insert_saved_visit(0, ij.PlanePosition(0, 0, 1), [value, step()])
    old = editor.command.record()
    editor.transfer_saved_stage(0, 0, 1, 0)
    transferred = editor.plan.visits[1].stages[0]
    assert transferred.selection == selected and transferred.whole_plane
    assert transferred.options.channels == 4 and transferred.options.minimum == 10
    assert old["visits"][0]["stages"][0]["whole_plane"]
    assert len(editor.plan.visits[0].stages) == 1 and len(editor.plan.visits[1].stages) == 2
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("axis", range(3))
@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("operation", ["insert", "move", "reposition", "remove", "transfer"])
def test_shared_panel_visit_authoring_exact_output_and_complete_record(axis, channels, operation, tmp_path):
    owner, _ = source()
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        for v, bounds in enumerate([(10, 70), (0, 170)]):
            editor.action("insert_rgb_contrast_visit", before=v, position=plane(axis))
            editor.action("update_rgb_contrast_stage", visit=v, stage=0, minimum=bounds[0], maximum=bounds[1], channels=channels)
        # A transferable source visit must retain an adjustment after a move.
        # Transfer a saved live adjustment instead, preserving the same pixels.
        if operation == "move": editor.action("move_rgb_contrast_visit", visit=1, destination=0)
        elif operation == "reposition": editor.action("update_rgb_contrast_visit", visit=0, position=[0, 0, 0])
        elif operation == "remove": editor.action("remove_rgb_contrast_visit", visit=0)
        elif operation == "transfer":
            editor.action("remove_rgb_contrast_visit", visit=0)
            editor.action("contrast_rgb_channels", channels=channels)
            editor.action("contrast_range", minimum=10, maximum=70)
            editor.action("promote_rgb_contrast_stage")
            editor.action("transfer_rgb_contrast_stage", visit=1, stage=0, destination_visit=0, before_stage=0)
            # Transfer puts A before B at the other plane: 30 -> 85 -> 128.
        editor.action("wait_jobs")
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        output = expected(axis, channels, "insert" if operation == "transfer" else operation)
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), output)
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        assert record["visits"][-1]["position"] == [0, 0, 0]
        assert owner.undo_steps == 1 and owner.snapshot().selection == before.selection
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), output)
    finally:
        editor.action("close_contrast")


@pytest.mark.parametrize("name,parameters", [
    ("insert_rgb_contrast_visit", {"before": 1, "position": [0, 0, 0]}),
    ("insert_rgb_contrast_visit", {"before": 0, "position": [False, 0, 0]}),
    ("insert_rgb_contrast_visit", {"before": 0, "position": [0, 0, 2]}),
    ("insert_rgb_contrast_visit", {"before": 0, "position": [0, 0, 0.0]}),
    ("insert_rgb_contrast_visit", {"before": 0, "position": [0, 0]}),
    ("select_rgb_contrast_visit", {"visit": 0}),
    ("remove_rgb_contrast_visit", {"visit": 0}),
    ("update_rgb_contrast_visit", {"visit": 0, "position": [0, 0, 1]}),
    ("move_rgb_contrast_visit", {"visit": 0, "destination": 0}),
    ("transfer_rgb_contrast_stage", {"visit": 0, "stage": 0, "destination_visit": 0, "before_stage": 0}),
])
def test_shared_panel_authoring_refusals_leave_live_draft_untouched(name, parameters, tmp_path):
    owner, _ = source()
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        with pytest.raises((ValueError, TypeError, IndexError, RuntimeError)):
            editor.action(name, **parameters)
        editor.action("wait_jobs")
        assert editor.render_headless()["contrast_panel"]["rgb"]["session_visits"] == 1
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    finally:
        editor.action("close_contrast")
