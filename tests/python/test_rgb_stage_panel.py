"""Saved same-component stages in the installed shared ImGui contrast panel."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


def click(editor, name):
    editor.action("show_contrast")
    editor.action("wait_jobs")
    control = editor.render_headless(frames=2)["contrast_panel"]["controls"][name]
    assert control["visible"] and control["enabled"]
    editor.render_headless(frames=2, events=[
        {"type": "action", "name": "show_contrast", "frames": 2},
        {"type": "position", "space": "screen", "point": control["point"]},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False}])
    editor.action("wait_jobs")


@pytest.mark.parametrize("channels", range(1, 8))
@pytest.mark.parametrize("stack", [False, True])
@pytest.mark.parametrize("selection", [False, True])
@pytest.mark.parametrize("physical", [False, True])
def test_keep_same_components_freezes_selection_and_reset_preserves_saved_stage(
        channels, stack, selection, physical, tmp_path):
    pixels = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    if selection:
        owner.select(ij.Selection("rectangle", [(0, 0), (2, 1)]))
    before = owner.snapshot()
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)

    def report():
        editor.action("wait_jobs")
        return editor.render_headless(frames=2)["contrast_panel"]

    def mapping(lo, hi):
        editor.action("contrast_range", minimum=lo, maximum=hi)
        editor.action("wait_jobs")

    try:
        editor.action("show_contrast")
        editor.action("contrast_rgb_channels", channels=channels)
        editor.action("contrast_rgb_stack", enabled=stack)
        mapping(10, 40)  # Exact byte result: 30 -> 170.
        if physical:
            click(editor, "keep_rgb_adjustment")
        else:
            editor.action("promote_rgb_contrast_stage")
        saved = report()
        assert saved["rgb"]["session_active"]
        assert saved["rgb"]["promoted_stages"] == 1
        assert saved["range"] == [0, 255]
        assert saved["rgb"]["channels"] == channels
        assert saved["rgb"]["entire_stack"] == stack
        if selection:
            owner.select(ij.Selection("rectangle", [(2, 1), (4, 2)]))
            editor.action("wait_jobs")
        mapping(0, 170)
        editor.action("contrast_reset")
        assert report()["range"] == [0, 255]
        assert report()["rgb"]["promoted_stages"] == 1
        mapping(0, 170)
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), pixels)
        assert owner.undo_steps == 0
        before_apply = owner.snapshot()
        selection_before = owner.snapshot().selection
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        expected = pixels.copy()
        for sample, bit in enumerate((4, 2, 1)):
            if channels & bit:
                if selection:
                    expected[0, 0, 0, 0, :2, sample] = 170
                    expected[0, 0, 0, 1, 2:, sample] = 45
                else:
                    expected[0, 0, 0, :, :, sample] = 255
                if stack:
                    if selection:
                        expected[0, 0, 1, 1, 2:, sample] = 45
                    else:
                        expected[0, 0, 1, :, :, sample] = 45
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
        assert owner.snapshot().selection == selection_before
        assert owner.undo_steps == 1
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        assert len(record["visits"]) == 1
        assert len(record["visits"][0]["stages"]) == 2
        assert record["entire_stack"] == stack
        owner.undo()
        assert owner.snapshot().shares_data(before_apply)
        owner.redo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
    finally:
        editor.action("close_contrast")


@pytest.mark.parametrize("kind", ["scalar", "uniform", "fields"])
def test_keep_rejects_inapplicable_sources_and_unknown_fields(kind, tmp_path):
    pixels = np.full((1, 1, 2, 2, 4, 1 if kind == "scalar" else 3), 30, np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        if kind == "uniform":
            editor.action("configure_contrast_target", target={"scope": "positions",
                          "positions": [[0, 0, 0], [0, 0, 1]], "roi": "whole_plane"})
        editor.action("wait_jobs")
        before = editor.render_headless()["contrast_panel"]
        if kind == "uniform":
            assert not before["controls"]["keep_rgb_adjustment"]["enabled"]
        with pytest.raises((ValueError, RuntimeError)):
            editor.action("promote_rgb_contrast_stage", **({"extra": 1} if kind == "fields" else {}))
        after = editor.render_headless()["contrast_panel"]
        assert after["range"] == before["range"]
        assert after["rgb"]["promoted_stages"] == before["rgb"]["promoted_stages"]
        assert not after["rgb"]["session_active"]
        assert owner.undo_steps == 0
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), pixels)
    finally:
        editor.action("close_contrast")
