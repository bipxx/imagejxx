"""Installed shared ImGui panel consumes actual ImageJ navigation checkpoints."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_rgb_contrast_session import REFERENCE, PIXELS, sha
from test_rgb_contrast_session_editor import position
from test_contrast_apply import rgb_stack_selection


@pytest.mark.parametrize("row", REFERENCE["cases"], ids=lambda r:f"a{r['axis']}-o{r['origin']}-c{r['channels']}-s{r['roi']}")
def test_shared_panel_matches_every_imagej_navigation_trace_and_complete_apply(row, tmp_path):
    owner = ij.Document(ij.Dataset(PIXELS, axes="TCZYXS"))
    a = [row["origin"]]*3
    b = a.copy()
    b[row["axis"]] = 1-row["origin"]
    position(owner, a)
    owner.select(rgb_stack_selection(row["roi"]))
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)

    def mapping(lo, hi):
        editor.action("contrast_range", minimum=lo, maximum=hi)
        editor.action("wait_jobs")

    def check(key):
        editor.action("show_channel_display")
        editor.action("wait_jobs")
        report = editor.render_headless(frames=2)
        readback = report["ordinary_pixels"]["readback"]
        assert readback["available"]
        assert sha(np.array(readback["source"]["pixels"], np.uint8)) == row[key+"_sha256"]
        return report["contrast_panel"]

    try:
        editor.action("show_contrast")
        editor.action("contrast_rgb_stack", enabled=True)
        editor.action("contrast_rgb_channels", channels=row["channels"])
        mapping(10.9, 40.1)
        check("first_map_a")
        owner.select(rgb_stack_selection(row["changed_roi"]))
        editor.action("wait_jobs")
        mapping(20.25, 200.75)
        check("changed_selection_map_a")
        editor.action("contrast_reset")
        check("original_a")
        mapping(20.25, 200.75)
        position(owner, b)
        check("original_b")
        mapping(-30.25, 400.5)
        check("mapped_b")
        position(owner, a)
        editor.action("wait_jobs")
        editor.action("contrast_reset")
        report = check("return_reset_a")
        assert report["rgb"]["session_active"] and report["rgb"]["session_visits"] == 3
        mapping(-10.2, 160.9)
        check("return_remap_a")
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), PIXELS)
        editor.action("contrast_rgb_stack", enabled=False)
        editor.action("wait_jobs")
        before = owner.snapshot()
        history = owner.undo_steps
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["remapped_stack_sha256"]
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        assert len(record["visits"]) == 3 and not record["entire_stack"]
        assert owner.snapshot().selection == before.selection
        assert owner.snapshot().display.crosshair == before.display.crosshair
        if owner.undo_steps > history:
            assert owner.undo_steps == history+1
            owner.undo()
            assert owner.snapshot().shares_data(before)
            owner.redo()
            assert sha(owner.snapshot().dataset.numpy("TCZYXS")) == row["remapped_stack_sha256"]
        else:
            assert row["changed_roi"] == 3
    finally:
        editor.action("close_contrast")


@pytest.mark.parametrize("stack", [False, True])
def test_session_replay_preserves_saved_history_and_rejects_unused_overrides(stack, tmp_path):
    pixels = np.full((1, 1, 2, 2, 4, 3), 30, np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        editor.action("contrast_range", minimum=10, maximum=40)
        position(owner, [0, 0, 1])
        editor.action("wait_jobs")
        editor.action("contrast_range", minimum=20, maximum=60)
        editor.action("contrast_rgb_stack", enabled=stack)
        editor.action("wait_jobs")
        before = owner.snapshot()
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        record = editor.workflow[-1]
        expected = owner.snapshot().dataset.numpy("TCZYXS").copy()
        assert record["schema"] == "imagejxx.rgb-contrast-session-command"
        owner.undo()
        with pytest.raises((ValueError, RuntimeError)):
            editor.action("replay_contrast_apply", record=record,
                          contrast_rebind={"roi": "whole_plane"})
        editor.action("replay_contrast_apply", record=record, contrast_rebind={})
        editor.action("wait_jobs")
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
        assert owner.snapshot().selection == before.selection
        assert owner.snapshot().display.crosshair == before.display.crosshair
    finally:
        editor.action("close_contrast")


def test_initial_resource_refusal_recovers_after_budget_change(tmp_path):
    owner = ij.Document(ij.Dataset(np.full((2, 4, 3), 30, np.uint8), axes="YXS"))
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor.update_settings(processing_memory_mb=1, persist=False)
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        report = editor.render_headless()["contrast_panel"]
        assert report["rgb"]["session_refused"] and report["error"]
        editor.update_settings(processing_memory_mb=512, persist=False)
        editor.action("wait_jobs")
        editor.action("contrast_range", minimum=10, maximum=40)
        editor.action("wait_jobs")
        report = editor.render_headless()["contrast_panel"]
        assert not report["rgb"]["session_refused"] and report["rgb"]["ready"]
        assert report["range"] == [10., 40.] and owner.undo_steps == 0
    finally:
        editor.action("close_contrast")


def test_uniform_target_refusal_recovers_when_original_navigation_returns(tmp_path):
    owner = ij.Document(ij.Dataset(np.full((1, 1, 2, 2, 4, 3), 30, np.uint8), axes="TCZYXS"))
    editor = Editor(preferences=tmp_path/"prefs.json")
    editor._editor.add_virtual_document(owner)
    try:
        editor.action("show_contrast")
        editor.action("configure_contrast_target", target={"scope": "positions",
                      "positions": [[0, 0, 0], [0, 0, 1]], "roi": "whole_plane"})
        editor.action("contrast_range", minimum=10, maximum=40)
        editor.action("wait_jobs")
        position(owner, [0, 0, 1])
        editor.action("wait_jobs")
        assert editor.render_headless()["contrast_panel"]["rgb"]["session_refused"]
        position(owner, [0, 0, 0])
        editor.action("wait_jobs")
        editor.action("contrast_range", minimum=20, maximum=60)
        editor.action("wait_jobs")
        assert not editor.render_headless()["contrast_panel"]["rgb"]["session_refused"]
        editor.recording = True
        editor.action("contrast_apply")
        editor.action("wait_jobs")
        record = editor.workflow[-1]
        assert record["schema"] == "imagejxx.rgb-contrast-apply-command"
        assert record["target"]["positions"] == [[0, 0, 0], [0, 0, 1]]
    finally:
        editor.action("close_contrast")
