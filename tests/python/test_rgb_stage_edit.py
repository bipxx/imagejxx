"""Historical RGB editing through admitted native and shared-panel interfaces."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_contrast_apply import rgb_options


def source():
    reads = ij.DatasetReadObserver()
    pixels = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
    return ij.Document(ij.Dataset(pixels, axes="TCZYXS").observe_reads(reads)), reads


def prepared(owner):
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 70)
    editor.promote()
    editor.set_range(0, 170)
    editor.promote()
    return editor


@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("operation", ["update", "move", "remove"])
def test_native_historical_edit_order_and_removal_have_exact_bytes(channels, operation):
    owner, reads = source()
    editor = prepared(owner)
    saved = editor.command.record()
    editor.set_saved_stage(0, 0, rgb_options(10, 70, channels))
    if operation in ("move", "remove"):
        editor.move_saved_stage(0, 0, 1)
    if operation == "remove":
        editor.remove_saved_stage(0, 1)
    assert reads.counts().plane_reads == 0
    assert owner.undo_steps == 0
    assert saved["visits"][0]["stages"][0]["channels"] == 7
    expected = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
    for sample, bit in enumerate((4, 2, 1)):
        expected[0, 0, 0, :, :, sample] = (
            45 if operation == "remove" or not channels & bit else
            128 if operation == "update" else 149)
    result = ij.execute_rgb_contrast_session_command(editor.command).preview()
    np.testing.assert_array_equal(result.dataset.numpy("TCZYXS"), expected)
    assert owner.undo_steps == 0


@pytest.mark.parametrize("method,args", [
    ("set_saved_stage", (0, 2, rgb_options())),
    ("set_saved_stage", (1, 0, rgb_options())),
    ("set_saved_stage", (0, 0, rgb_options(70, 10))),
    ("set_saved_stage", (0, 0, rgb_options(0, float("nan")))),
    ("set_saved_stage", (0, 0, rgb_options(0, 255, 0))),
    ("remove_saved_stage", (0, 2)),
    ("move_saved_stage", (0, 0, 2)),
    ("move_saved_stage", (0, 2, 0)),
    ("move_saved_stage", (0, -1, 0)),
    ("remove_saved_stage", (True, 0)),
    ("remove_saved_stage", (0, 1.5)),
    ("remove_saved_stage", (0, 2**100)),
])
def test_native_saved_edit_refusal_is_atomic_and_sample_free(method, args):
    owner, reads = source()
    editor = prepared(owner)
    before = editor.command.record()
    with pytest.raises((ValueError, TypeError, IndexError, OverflowError)):
        getattr(editor, method)(*args)
    assert editor.command.record() == before
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0


def test_historical_visit_keeps_geometry_and_cannot_become_empty():
    owner, reads = source()
    owner.select(ij.Selection("rectangle", [(0, 0), (2, 1)]))
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 70, whole_plane=True)
    view = owner.snapshot().display
    view.crosshair = [0, 0, 1]
    owner.set_display(view)
    owner.select(ij.Selection("rectangle", [(2, 1), (4, 2)]))
    editor.synchronize(owner.snapshot())
    original = editor.plan.visits[0].stages[0]
    editor.set_saved_stage(0, 0, rgb_options(0, 60, 4))
    stage = editor.plan.visits[0].stages[0]
    assert stage.selection == original.selection and stage.whole_plane
    before = editor.command.record()
    with pytest.raises(ValueError):
        editor.remove_saved_stage(0, 0)
    assert editor.command.record() == before
    assert editor.plan.visits[-1].stages[-1].options.minimum == 0
    assert editor.source.selection == owner.snapshot().selection
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("operation", ["update", "move", "remove"])
def test_shared_panel_saved_edits_record_complete_history(channels, operation, tmp_path):
    owner, _ = source()
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        for low, high in [(10, 70), (0, 170)]:
            editor.action("contrast_range", minimum=low, maximum=high)
            editor.action("promote_rgb_contrast_stage")
        editor.action("update_rgb_contrast_stage", visit=0, stage=0,
                      minimum=10, maximum=70, channels=channels)
        if operation in ("move", "remove"):
            editor.action("move_rgb_contrast_stage", visit=0, stage=0, destination=1)
        if operation == "remove":
            editor.action("remove_rgb_contrast_stage", visit=0, stage=1)
        editor.action("wait_jobs")
        report = editor.render_headless()["contrast_panel"]
        assert report["range"] == [0, 255]
        assert report["rgb"]["promoted_stages"] == (1 if operation == "remove" else 2)
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        expected = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
        for sample, bit in enumerate((4, 2, 1)):
            expected[0, 0, 0, :, :, sample] = (
                45 if operation == "remove" or not channels & bit else
                128 if operation == "update" else 149)
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        assert len(record["visits"][0]["stages"]) == (2 if operation == "remove" else 3)
        assert owner.undo_steps == 1
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
    finally:
        editor.action("close_contrast")


@pytest.mark.parametrize("action,parameters", [
    ("select_rgb_contrast_stage", {"visit": 0, "stage": 2}),
    ("select_rgb_contrast_stage", {"visit": True, "stage": 0}),
    ("update_rgb_contrast_stage", {"visit": 0, "stage": 0, "minimum": 70, "maximum": 10, "channels": 7}),
    ("update_rgb_contrast_stage", {"visit": 0, "stage": 0, "minimum": 10, "maximum": 70, "channels": True}),
    ("update_rgb_contrast_stage", {"visit": 0, "stage": 0, "minimum": 10, "maximum": 70, "channels": 7.0}),
    ("remove_rgb_contrast_stage", {"visit": 0, "stage": 2}),
    ("remove_rgb_contrast_stage", {"visit": 0, "stage": -1}),
    ("move_rgb_contrast_stage", {"visit": 0, "stage": 0, "destination": 2}),
    ("move_rgb_contrast_stage", {"visit": 0, "stage": 0, "destination": 1.0}),
    ("move_rgb_contrast_stage", {"visit": 0, "stage": 0, "destination": 1, "extra": 0}),
])
def test_shared_panel_rejects_invalid_saved_changes_without_losing_intent(action, parameters, tmp_path):
    owner, _ = source()
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        for low, high in [(10, 70), (0, 170)]:
            editor.action("contrast_range", minimum=low, maximum=high)
            editor.action("promote_rgb_contrast_stage")
        with pytest.raises((ValueError, TypeError, IndexError, RuntimeError)):
            editor.action(action, **parameters)
        editor.action("wait_jobs")
        report = editor.render_headless()["contrast_panel"]
        assert report["rgb"]["promoted_stages"] == 2 and report["range"] == [0, 255]
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        expected = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
        expected[0, 0, 0] = 128
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
        owner.undo()
        assert owner.snapshot().shares_data(before)
    finally:
        editor.action("close_contrast")
