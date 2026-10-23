"""Explicit saved-stage geometry is admitted in C++, never processed in Python."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


def area(kind):
    if kind == "none":
        return None
    return ij.Selection("rectangle", [(0, 0), (2, 1)] if kind == "left" else [(2, 1), (4, 2)])


def source():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.full((1, 1, 2, 2, 4, 3), 30, np.uint8),
                                   axes="TCZYXS").observe_reads(reads))
    owner.select(area("left"))
    return owner, reads


def expected_pixels(channels, kind, whole):
    expected = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
    for sample, bit in enumerate((4, 2, 1)):
        if channels & bit:
            if whole or kind == "none":
                expected[0, 0, 0, :, :, sample] = 170
            elif kind == "left":
                expected[0, 0, 0, 0, :2, sample] = 170
            else:
                expected[0, 0, 0, 1, 2:, sample] = 170
    return expected


@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("kind", ["left", "right", "none"])
@pytest.mark.parametrize("whole", [False, True])
@pytest.mark.parametrize("historical_visit", [False, True])
def test_native_saved_area_freezes_geometry_and_preserves_mapping_and_navigation(
        channels, kind, whole, historical_visit):
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_channels(channels)
    editor.set_range(10, 40)
    editor.promote()
    if historical_visit:
        view = owner.snapshot().display
        view.crosshair = [0, 0, 1]
        owner.set_display(view)
        editor.synchronize(owner.snapshot())
    old = editor.command.record()
    selected = area(kind)
    editor.set_saved_selection(0, 0, selected, whole_plane=whole)
    stage = editor.plan.visits[0].stages[0]
    assert stage.selection == selected and stage.whole_plane == whole
    assert (stage.options.minimum, stage.options.maximum, stage.options.channels) == (10, 40, channels)
    assert editor.plan.visits[0].position.z == 0
    assert editor.source.display.crosshair == owner.snapshot().display.crosshair
    assert old["visits"][0]["stages"][0]["whole_plane"] is False
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0
    result = ij.execute_rgb_contrast_session_command(editor.command).preview()
    np.testing.assert_array_equal(result.dataset.numpy("TCZYXS"), expected_pixels(channels, kind, whole))
    assert owner.snapshot().selection == area("left")
    assert owner.undo_steps == 0


@pytest.mark.parametrize("whole", [False, True])
def test_invalid_even_ignored_historical_mask_refuses_atomically(whole):
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.set_range(10, 40)
    editor.promote()
    invalid = ij.Selection("rectangle", [(float("nan"), 0), (2, 1)])
    before = editor.command.record()
    with pytest.raises((ValueError, RuntimeError)):
        editor.set_saved_selection(0, 0, invalid, whole_plane=whole)
    assert editor.command.record() == before
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("visit,stage,whole", [(0, 1, False), (1, 0, False),
                                               (True, 0, False), (0, 0, 1),
                                               (0, -1, False), (0, 0, "false")])
def test_saved_area_addressing_and_policy_types_are_strict(visit, stage, whole):
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.promote()
    before = editor.command.record()
    with pytest.raises((ValueError, TypeError, IndexError, OverflowError)):
        editor.set_saved_selection(visit, stage, area("right"), whole_plane=whole)
    assert editor.command.record() == before
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize("whole", [False, True])
def test_saved_area_record_budget_refusal_keeps_old_geometry_without_reads(whole):
    owner, reads = source()
    editor = ij.RgbContrastSessionEditor(owner.snapshot())
    editor.promote()
    limits = editor.limits
    limits.max_record_bytes = 262144
    editor.set_limits(limits)
    before = editor.command.record()
    large = ij.Selection("polygon", [(0, 0), (4, 0), (4, 2), (0, 2)]*1500)
    with pytest.raises(ValueError):
        editor.set_saved_selection(0, 0, large, whole_plane=whole)
    assert editor.command.record() == before
    assert reads.counts().plane_reads == 0 and owner.undo_steps == 0


@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("whole", [False, True])
@pytest.mark.parametrize("historical_visit", [False, True])
def test_shared_panel_area_action_records_frozen_geometry_and_paired_history(
        channels, whole, historical_visit, tmp_path):
    owner, _ = source()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        editor.action("contrast_rgb_channels", channels=channels)
        editor.action("contrast_range", minimum=10, maximum=40)
        editor.action("promote_rgb_contrast_stage")
        owner.select(area("right"))
        if historical_visit:
            view = owner.snapshot().display
            view.crosshair = [0, 0, 1]
            owner.set_display(view)
        editor.action("wait_jobs")
        before = owner.snapshot()
        editor.action("update_rgb_contrast_stage_area", visit=0, stage=0,
                      policy="whole_plane" if whole else "current_selection")
        editor.action("wait_jobs")
        assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
        assert editor.render_headless()["contrast_panel"]["range"] == [0, 255]
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected_pixels(channels, "right", whole))
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        assert record["visits"][0]["stages"][0]["whole_plane"] == whole
        assert owner.snapshot().selection == before.selection
        assert owner.snapshot().display.crosshair == before.display.crosshair
        assert owner.undo_steps == 1
        owner.undo()
        assert owner.snapshot().shares_data(before)
        owner.redo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected_pixels(channels, "right", whole))
    finally:
        editor.action("close_contrast")


@pytest.mark.parametrize("parameters", [
    {"visit": 0, "stage": 1, "policy": "current_selection"},
    {"visit": 0, "stage": 0, "policy": "invalid"},
    {"visit": 0, "stage": 0, "policy": True},
    {"visit": 0, "stage": 0, "policy": "whole_plane", "extra": 0},
])
def test_shared_panel_area_refusal_preserves_saved_intent(parameters, tmp_path):
    owner, _ = source()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        editor.action("contrast_range", minimum=10, maximum=40)
        editor.action("promote_rgb_contrast_stage")
        with pytest.raises((ValueError, TypeError, IndexError, RuntimeError)):
            editor.action("update_rgb_contrast_stage_area", **parameters)
        editor.action("wait_jobs")
        assert owner.undo_steps == 0
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected_pixels(7, "left", False))
    finally:
        editor.action("close_contrast")
