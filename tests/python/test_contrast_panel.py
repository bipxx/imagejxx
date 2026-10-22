"""Installed Python interface to native contrast controls; no Python processing."""
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor


def seed(channels=1):
    return {"schema": "imagejxx.channel-display", "version": 1, "mode": "color",
            "channels": [{"visible": True, "range": [0., 255.],
                          "lut": {"name": "Grays"}} for _ in range(channels)]}


def image(channels=1):
    pixels = np.arange(channels * 48, dtype=np.uint8).reshape(1, channels, 1, 6, 8, 1)
    return ij.Document(ij.Dataset(pixels, axes="TCZYXS"))


def test_seeded_auto_python_mapping_and_atomic_publication():
    owner = image()
    app = ij.Application()
    app.adopt(owner)
    before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError)):
        app.submit_auto_contrast()
    initial = seed()
    job = app.submit_seeded_auto_contrast(initial)
    initial["channels"][0]["range"] = [100., 200.]
    assert job.wait(5000)
    assert owner.snapshot().revisions.display == before.revisions.display
    assert "channel_display" not in owner.snapshot().display.properties
    result = app.publish_auto_contrast(job)
    assert result.applied_range == [0., 47.]
    assert app.auto_contrast_divisor() == 5000
    assert not owner.snapshot().dirty and owner.undo_steps == 0
    repeat = app.submit_auto_contrast()
    assert repeat.wait(5000)
    assert app.publish_auto_contrast(repeat).cycle.used == 2500
    with pytest.raises((ValueError, RuntimeError)):
        app.submit_seeded_auto_contrast(seed())


@pytest.mark.parametrize("failure", ["channels", "budget", "opaque"])
def test_seeded_auto_rejection_preserves_owner(failure):
    owner = image()
    app = ij.Application()
    app.adopt(owner)
    initial, limits = seed(), ij.AutoContrastLimits()
    if failure == "channels":
        initial = seed(2)
    elif failure == "budget":
        limits.max_record_bytes = 1
    else:
        view = owner.snapshot().display
        view.properties = {"channel_display": {"unknown": 42}}
        owner.set_display(view)
    before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError)):
        app.submit_seeded_auto_contrast(initial, limits=limits)
    after = owner.snapshot()
    assert after.display.properties == before.display.properties
    assert after.revisions.display == before.revisions.display
    assert app.auto_contrast_divisor() == 0


def test_seeded_ready_cancellation_and_stale_display_are_atomic():
    for cancel in (True, False):
        owner = image()
        app = ij.Application()
        app.adopt(owner)
        job = app.submit_seeded_auto_contrast(seed())
        assert job.wait(5000)
        if cancel:
            job.cancel()
        else:
            view = owner.snapshot().display
            view.zoom = 2.
            owner.set_display(view)
        before = owner.snapshot()
        with pytest.raises((ij.OperationCancelled, ij.RevisionConflict)):
            app.publish_auto_contrast(job)
        assert owner.snapshot().display.properties == before.display.properties
        assert owner.snapshot().revisions.display == before.revisions.display
        assert app.auto_contrast_divisor() == 0


def test_modeless_histogram_and_display_controls(tmp_path):
    owner = image(2)
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    editor.action("wait_jobs")
    report = editor.render_headless()["contrast_panel"]
    assert report["open"] and not report["histogram_pending"]
    assert report["histogram"]["finite_count"] == 48
    assert report["histogram"]["counts"] == [1] * 48 + [0] * 208
    submitted = report["submitted"]
    before = owner.snapshot()
    editor.action("contrast_window_level", window=40., level=25.)
    assert owner.snapshot().display.properties["display_range"] == [5., 45.]
    editor.action("contrast_range", minimum=12.5, maximum=45.25)
    assert owner.snapshot().display.properties["display_range"] == [12.5, 45.25]
    editor.action("contrast_reset")
    editor.action("contrast_brightness", value=.75)
    assert owner.snapshot().display.properties["display_range"] == [-63.75, 191.25]
    editor.action("contrast_contrast", value=.5)
    assert owner.snapshot().display.properties["display_range"] == [-63.75, 191.25]
    assert editor.render_headless()["contrast_panel"]["submitted"] == submitted
    editor.action("view", c=1)
    editor.action("wait_jobs")
    report = editor.render_headless()["contrast_panel"]
    assert report["source"]["tcz"] == [0, 1, 0]
    assert report["histogram"]["counts"] == [0] * 48 + [1] * 48 + [0] * 160
    editor.action("contrast_auto")
    editor.action("wait_jobs")
    state = owner.snapshot().display.properties["channel_display"]
    assert state["channels"][1]["range"] == [48., 95.]
    other = state["channels"][0]
    editor.action("contrast_window_level", window=20., level=50.)
    state = owner.snapshot().display.properties["channel_display"]
    assert state["channels"][0] == other
    assert state["channels"][1]["range"] == [40., 60.]
    assert owner.snapshot().revisions.pixels == before.revisions.pixels
    assert not owner.snapshot().dirty and owner.undo_steps == 0
    editor.action("close_contrast")
    assert not editor.render_headless()["contrast_panel"]["open"]


@pytest.mark.parametrize("action,params", [
    ("contrast_window_level", {"window": 0, "level": 1}),
    ("contrast_range", {"minimum": 9, "maximum": 1}),
    ("contrast_range", {"minimum": 9, "maximum": 9}),
    ("contrast_brightness", {"value": 1.1}),
    ("contrast_contrast", {"value": float("nan")}),
    ("contrast_range", {"minimum": 0, "maximum": 1, "extra": 3}),
])
def test_invalid_contrast_controls_are_atomic(action, params, tmp_path):
    owner = image()
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError)):
        editor.action(action, **params)
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.snapshot().revisions.display == before.revisions.display
    editor.action("close_contrast")


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16])
@pytest.mark.parametrize("roi", ["whole_plane", "active_selection"])
@pytest.mark.parametrize("scope", ["all_z", "ranges"])
def test_editor_apply_target_settings_record_and_history(dtype, roi, scope, tmp_path):
    pixels = np.tile(np.array([0, 10, 20, 40], dtype), (2, 2, 2, 2, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX"))
    owner.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]] * 2, np.uint8)))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.update_settings(conversion_unsigned16_range="12", persist=False)
    editor.action("show_contrast")
    editor.action("wait_jobs")
    editor.action("view", c=1)
    editor.action("contrast_range", minimum=10., maximum=40.)
    target = {"scope": scope, "roi": roi}
    if scope == "ranges":
        target.update(t={"start": 0, "stop": 2}, c={"start": 1, "stop": 2},
                      z={"start": 0, "stop": 2})
    editor.action("configure_contrast_target", target=target)
    editor.recording = True
    before = owner.snapshot()
    editor.action("contrast_apply")
    editor.recording = False
    editor.update_settings(conversion_unsigned16_range="auto", persist=False)
    editor.action("wait_jobs")
    expected = pixels.copy()
    mapped = [0, 0, 85, 255] if dtype == np.uint8 else [0, 0, 1365, 4094]
    for t in range(1 if scope == "all_z" else 2):
        for z in range(2):
            if roi == "whole_plane":
                expected[t, 1, z] = mapped
            else:
                expected[t, 1, z, :, 1:3] = mapped[1:3]
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)
    assert owner.undo_steps == 1 and len(editor.workflow) == 1
    record = editor.workflow[0]
    assert record["schema"] == "imagejxx.contrast-apply-command"
    assert record["options"]["unsigned16_bits"] == 12
    assert owner.snapshot().display.properties["display_range"] == [0., 255. if dtype == np.uint8 else 4095.]
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), pixels)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)
    editor.action("close_contrast")


def test_editor_apply_strict_and_explicit_rebind(tmp_path):
    owner = image()
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    options = ij.ContrastApplyOptions()
    options.minimum, options.maximum = 10., 40.
    record = ij.resolve_contrast_apply_command(owner.snapshot(), options).record()
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("replay_contrast_apply", record=record, extra=True)
    editor.action("replay_contrast_apply", record=record)
    editor.action("wait_jobs")
    assert owner.undo_steps == 1
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("replay_contrast_apply", record=record)
    before = owner.snapshot()
    malformed = dict(record, extra=True)
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("replay_contrast_apply", record=malformed, contrast_rebind={"roi": "whole_plane"})
    assert owner.snapshot().revisions.pixels == before.revisions.pixels
    editor.action("replay_contrast_apply", record=record, contrast_rebind={"roi": "whole_plane"})
    editor.action("wait_jobs")
    assert owner.undo_steps == 2


@pytest.mark.parametrize("dtype", [np.float32, np.float64])
def test_editor_floating_apply_disabled_without_changes(dtype, tmp_path):
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=dtype).reshape(6, 8), axes="YX"))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    editor.action("contrast_range", minimum=10., maximum=40.)
    before = owner.snapshot()
    report = editor.render_headless()
    assert not report["contrast_panel"]["controls"]["apply"]["enabled"]
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("contrast_apply")
    assert owner.snapshot().revisions.pixels == before.revisions.pixels
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.undo_steps == 0 and not editor.workflow
    editor.action("close_contrast")


def saved_apply_record():
    source = ij.Document(ij.Dataset(np.array([[0, 10, 20, 40]], np.uint16), axes="YX"))
    options = ij.ContrastApplyOptions()
    options.minimum, options.maximum, options.unsigned16_bits = 10., 40., 12
    return ij.resolve_contrast_apply_command(source.snapshot(), options).record()


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16])
@pytest.mark.parametrize("roi", ["whole_plane", "active_selection", "positioned_rois"])
@pytest.mark.parametrize("scope", ["all_z", "ranges"])
def test_reviewed_apply_uses_destination_targets_and_saved_settings(dtype, roi, scope, tmp_path):
    import copy
    record = saved_apply_record()
    saved = copy.deepcopy(record)
    pixels = np.tile(np.array([0, 10, 20, 40], dtype), (2, 2, 2, 2, 1))
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX").observe_reads(observer))
    owner.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]] * 2, np.uint8)))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("destination", ij.Selection("rectangle", [(1, 0), (3, 2)]), t=0, c=1, z=1)])
    owner.commit(edit)
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("view", c=1)
    before, steps = owner.snapshot(), owner.undo_steps
    editor.action("show_contrast_apply_rebind", record=record)
    record["options"]["minimum"] = 999.
    target = {"scope": scope, "roi": roi}
    if scope == "ranges":
        target.update(t={"start": 0, "stop": 2}, c={"start": 1, "stop": 2}, z={"start": 0, "stop": 2})
    editor.action("configure_contrast_rebind_target", target=target)
    assert observer.counts().plane_reads == 0 and observer.counts().region_reads == 0
    assert owner.snapshot().shares_data(before) and not editor.busy
    editor.update_settings(conversion_unsigned16_range="auto", persist=False)
    editor.recording = True
    editor.action("apply_contrast_rebind")
    editor.recording = False
    editor.action("wait_jobs")
    expected = pixels.copy()
    mapped = [0, 0, 85, 255] if dtype == np.uint8 else [0, 0, 1365, 4094]
    if roi == "positioned_rois":
        expected[0, 1, 1, :, 1:3] = mapped[1:3]
    else:
        for t in range(1 if scope == "all_z" else 2):
            if roi == "whole_plane":
                expected[t, 1] = mapped
            else:
                expected[t, 1, :, :, 1:3] = mapped[1:3]
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)
    assert owner.undo_steps == steps + 1 and len(editor.workflow) == 1
    assert editor.workflow[0]["options"] == saved["options"]
    assert editor.workflow[0]["source"] != saved["source"]
    assert not editor.render_headless()["contrast_apply_rebind"]["open"]
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), pixels)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)


@pytest.mark.parametrize("change", ["display", "selection", "pixels", "metadata", "owner"])
def test_rebind_review_rejects_changed_destination(change, tmp_path):
    owner = image()
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast_apply_rebind", record=saved_apply_record())
    if change == "display":
        view = owner.snapshot().display
        view.zoom = 2.
        owner.set_display(view)
    elif change == "selection":
        owner.select(ij.Selection("rectangle", [(0, 0), (2, 2)]))
    elif change == "owner":
        editor._editor.add_virtual_document(image())
        editor.active_index = 1
    else:
        edit = owner.begin_edit()
        if change == "pixels":
            edit.replace_plane(np.full((6, 8), 99, np.uint8))
        else:
            edit.set_title("changed destination")
        owner.commit(edit)
    stable, steps = owner.snapshot(), owner.undo_steps
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("apply_contrast_rebind")
    report = editor.render_headless()["contrast_apply_rebind"]
    assert report["open"] and not report["valid"] and not report["controls"]["apply"]["enabled"]
    assert owner.snapshot().shares_data(stable) and owner.undo_steps == steps and not editor.workflow
    editor.action("cancel_contrast_rebind")


@pytest.mark.parametrize("target", [
    {"scope": "future"}, {"mode": "volume"}, {"destination": "new_document"},
    {"scope": "positions", "positions": [[0, 0, 0]]},
    {"scope": "ranges", "c": {"start": 0, "stop": 99}}, {"roi": "future"},
])
def test_rebind_review_bad_target_keeps_capture_without_reads(target, tmp_path):
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=np.uint8).reshape(6, 8), axes="YX").observe_reads(observer))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast_apply_rebind", record=saved_apply_record())
    before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError, IndexError)):
        editor.action("configure_contrast_rebind_target", target=target)
    assert observer.counts().plane_reads == 0 and observer.counts().region_reads == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0 and not editor.workflow
    editor.action("configure_contrast_rebind_target", target={"roi": "whole_plane"})
    editor.action("apply_contrast_rebind")
    editor.action("wait_jobs")
    assert owner.undo_steps == 1


@pytest.mark.parametrize("change", ["schema", "profile", "options", "source", "reset", "extra"])
def test_rebind_review_validates_complete_original_before_replace(change, tmp_path):
    import copy
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=np.uint8).reshape(6, 8), axes="YX").observe_reads(observer))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    record = saved_apply_record()
    editor.action("show_contrast_apply_rebind", record=record)
    bad = copy.deepcopy(record)
    if change in ("schema", "profile"):
        bad[change] = "future"
    elif change == "options":
        bad[change]["minimum"] = True
    elif change == "source":
        bad[change]["pixel_type"] = "future"
    elif change == "reset":
        bad[change]["policy"] = "future"
    else:
        bad["extra"] = True
    with pytest.raises((ValueError, RuntimeError, TypeError)):
        editor.action("show_contrast_apply_rebind", record=bad)
    assert observer.counts().plane_reads == 0 and observer.counts().region_reads == 0
    editor.action("configure_contrast_rebind_target", target={"roi": "whole_plane"})
    editor.action("apply_contrast_rebind")
    editor.action("wait_jobs")
    assert owner.undo_steps == 1


@pytest.mark.parametrize("dismiss", ["cancel", "close", "global"])
def test_rebind_review_dismissal_leaves_source_untouched(dismiss, tmp_path):
    owner = image()
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast_apply_rebind", record=saved_apply_record())
    before = owner.snapshot()
    if dismiss == "close":
        editor.close_document(owner.id, discard=True)
    elif dismiss == "global":
        editor.cancel()
    else:
        editor.action("cancel_contrast_rebind")
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("apply_contrast_rebind")
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0 and not editor.workflow
    assert not editor.render_headless()["contrast_apply_rebind"]["open"]


def test_rebind_review_can_override_nonarea_selection(tmp_path):
    owner = image()
    owner.select(ij.Selection("line", [(0, 0), (3, 1)]))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast_apply_rebind", record=saved_apply_record())
    editor.action("configure_contrast_rebind_target", target={"roi": "whole_plane"})
    editor.action("apply_contrast_rebind")
    editor.action("wait_jobs")
    assert owner.undo_steps == 1 and owner.snapshot().selection.kind == "line"


# RGB modeless controls operate on immutable source pixels until atomic Apply.
import hashlib
import time
from test_contrast_apply import RGB_FIXTURE, rgb_input, rgb_options


@pytest.mark.parametrize("row", RGB_FIXTURE["rows"], ids=lambda r: f'rgb-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}')
def test_rgb_editor_preview_apply_and_next_baseline_match_imagej(row, tmp_path):
    pixels = rgb_input()
    owner = ij.Document(ij.Dataset(pixels, axes="YXS"))
    if row["roi"]:
        mask = np.zeros((2, 256), np.uint8)
        mask[:, 17:236] = 1
        if row["roi"] == 2:
            y, x = np.indices(mask.shape)
            mask[(x + y) % 3 == 0] = 0
        owner.select(ij.Selection.from_mask(mask))
    # The live display range must never map the already-mapped preview again.
    view = owner.snapshot().display
    view.properties = {"display_range": [10., 40.]}
    owner.set_display(view)
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    low, high = RGB_FIXTURE["ranges"][row["range_index"]]
    editor.action("contrast_range", minimum=low, maximum=high)
    editor.action("contrast_rgb_channels", channels=row["channels"])
    before = owner.snapshot()
    editor.action("wait_jobs")
    editor.action("show_channel_display")  # Enables native pixel-readback diagnostics.
    report = editor.render_headless()
    assert report["contrast_panel"]["rgb"]["ready"]
    readback = report["ordinary_pixels"]["readback"]
    assert readback["available"]
    for field in ["source", "rendered_rgb"]:
        assert hashlib.sha256(np.array(readback[field]["pixels"], np.uint8).tobytes()).hexdigest() == row["sha256"]
    assert owner.snapshot().shares_data(before) and owner.snapshot().revisions.display == before.revisions.display
    assert owner.undo_steps == 0
    job_id = report["contrast_panel"]["rgb"]["job_id"]
    editor.recording = True
    editor.action("contrast_apply")
    assert editor.job_info.id == job_id
    editor.recording = False
    editor.action("wait_jobs")
    assert owner.undo_steps == 1 and len(editor.workflow) == 1
    record = editor.workflow[0]
    assert record["schema"] == "imagejxx.rgb-contrast-apply-command"
    assert record["options"]["channels"] == row["channels"]
    assert hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest() == row["sha256"]
    assert owner.snapshot().display.properties == {"display_range": [0., 255.]}
    owner.select(None)
    editor.action("contrast_range", minimum=low, maximum=high)
    editor.action("wait_jobs")
    editor.action("contrast_apply")
    editor.action("wait_jobs")
    assert hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest() == row["next_sha256"]
    owner.undo()
    assert hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest() == row["sha256"]
    editor.action("close_contrast")


@pytest.mark.parametrize("channels", [1, 4, 7])
@pytest.mark.parametrize("scope", ["current_plane", "all_z", "ranges", "positions"])
@pytest.mark.parametrize("roi", ["whole_plane", "active_selection", "positioned_rois"])
def test_rgb_editor_native_target_extension_uses_independent_axes(channels, scope, roi, tmp_path):
    pixels = np.tile(np.array([[[10, 20, 30], [20, 30, 40], [30, 40, 50], [40, 50, 60]]], np.uint8), (2, 2, 2, 2, 1, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    owner.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]] * 2, np.uint8)))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("rgb", ij.Selection("rectangle", [(1, 0), (3, 2)]), t=1, c=1, z=0)])
    owner.commit(edit)
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("view", t=1, c=1, crosshair=[0, 0, 1])
    editor.action("show_contrast")
    editor.action("contrast_range", minimum=10., maximum=40.)
    editor.action("contrast_rgb_channels", channels=channels)
    request = {"scope": scope, "roi": roi}
    positions = [(1, 1, 1)]
    if scope == "all_z": positions = [(1, 1, 0), (1, 1, 1)]
    if scope == "ranges":
        request.update(t={"start": 0, "stop": 2}, c={"start": 0, "stop": 2}, z={"start": 0, "stop": 2})
        positions = [(t, c, z) for t in range(2) for c in range(2) for z in range(2)]
    if scope == "positions":
        positions = [(1, 1, 0), (0, 0, 1)]
        request["positions"] = [list(p) for p in positions]
    editor.action("configure_contrast_target", target=request)
    editor.action("wait_jobs")
    before, steps = owner.snapshot(), owner.undo_steps
    editor.action("contrast_apply")
    editor.action("wait_jobs")
    expected = pixels.copy()
    changed = False
    for p in positions:
        if roi == "positioned_rois" and p != (1, 1, 0): continue
        changed = True
        columns = slice(None) if roi == "whole_plane" else slice(1, 3)
        for c, bit in enumerate([4, 2, 1]):
            if channels & bit:
                expected[p][:, columns, c] = np.clip(np.trunc((pixels[p][:, columns, c].astype(float) - 10) * 256 / 30), 0, 255)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
    assert owner.undo_steps == steps + changed
    if changed:
        owner.undo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), pixels)
        assert owner.snapshot().display.properties == before.display.properties
    editor.action("close_contrast")


@pytest.mark.parametrize("bad", [True, 1., 0, -1, 8, "red", None, 2**63])
def test_rgb_editor_component_validation_preserves_intent(bad, tmp_path):
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS"))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    editor.action("wait_jobs")
    before = owner.snapshot()
    with pytest.raises((ValueError, TypeError, RuntimeError, OverflowError)):
        editor.action("contrast_rgb_channels", channels=bad)
    report = editor.render_headless()["contrast_panel"]
    assert report["rgb"]["channels"] == 7 and report["rgb"]["ready"]
    assert before.shares_data(owner.snapshot()) and owner.undo_steps == 0
    editor.action("close_contrast")


@pytest.mark.parametrize("change", ["zoom", "selection", "navigation", "cancel", "close"])
def test_rgb_editor_stale_preview_and_close_never_publish(change, tmp_path):
    pixels = np.tile(np.array([[[10, 20, 30], [40, 50, 60]]], np.uint8), (2, 1, 1, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="ZYXS"))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    editor.action("contrast_range", minimum=10., maximum=40.)
    editor.action("wait_jobs")
    before = owner.snapshot()
    if change == "zoom":
        view = owner.snapshot().display
        view.zoom = 2.
        owner.set_display(view)
    elif change == "selection": owner.select(ij.Selection.from_mask(np.zeros((1, 2), np.uint8)))
    elif change == "navigation": editor.action("view", crosshair=[0, 0, 1])
    elif change == "cancel": editor.cancel()
    else: editor.action("close_contrast")
    stable, steps = owner.snapshot(), owner.undo_steps
    # Stale/reopened intent needs a fresh READY result before Apply.
    with pytest.raises((ValueError, RuntimeError)):
        editor.action("contrast_apply")
    assert owner.snapshot().shares_data(stable) and owner.undo_steps == steps
    assert owner.snapshot().dataset.shares_pixels(before.dataset)
    editor.action("close_contrast")


@pytest.mark.parametrize("channels", [1, 4, 7])
@pytest.mark.parametrize("review", [False, True])
def test_rgb_editor_saved_record_replay_and_reviewed_rebind(channels, review, tmp_path):
    pixels = np.array([[[10, 20, 30], [40, 50, 60]]], np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="YXS"))
    original = ij.resolve_rgb_contrast_apply_command(owner.snapshot(), rgb_options(10, 40, channels)).record()
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    if review:
        editor.action("show_contrast_apply_rebind", record=original)
        report = editor.render_headless()["contrast_apply_rebind"]
        assert report["open"] and report["valid"] and report["rgb_channels"] == channels
        editor.action("configure_contrast_rebind_target", target={"roi": "whole_plane"})
        editor.action("apply_contrast_rebind")
    else: editor.action("replay_contrast_apply", record=original)
    editor.action("wait_jobs")
    expected = pixels.copy()
    for c, bit in enumerate([4, 2, 1]):
        if channels & bit: expected[..., c] = np.clip(np.trunc((pixels[..., c].astype(float) - 10) * 256 / 30), 0, 255)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"), expected)
    assert owner.undo_steps == 1


@pytest.mark.parametrize("xyz", [False, True])
def test_rgb_editor_virtual_background_preview_uses_neutral_display_in_xyz(xyz, tmp_path):
    from test_virtual_tiff import write
    from imagejxx import scientific_io as io
    pixels = np.tile(np.array([[[10, 20, 30], [20, 30, 40], [30, 40, 50], [40, 50, 60]]] * 3, np.uint8), (1, 1, 3, 1, 1, 1))
    path = tmp_path / "rgb.tif"
    write(path, pixels, io.ImageFileFormat.SCIENTIFIC_TIFF_RGB8_V1)
    owner = ij.Document(io.open_virtual_image_series(path).dataset)
    view = owner.snapshot().display
    view.crosshair, view.orthogonal = [1, 1, 1], xyz
    view.properties = {"display_range": [10., 40.]}
    owner.set_display(view)
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor.update_settings(async_viewport=True, persist=False)
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    editor.action("contrast_range", minimum=10., maximum=40.)
    editor.action("contrast_rgb_channels", channels=4)
    editor.action("configure_contrast_target", target={"scope": "all_z", "roi": "whole_plane"})
    editor.action("wait_jobs")
    editor.action("show_channel_display")
    expected = pixels.copy()
    expected[..., 0] = np.clip(np.trunc((pixels[..., 0].astype(float) - 10) * 256 / 30), 0, 255)
    end = time.monotonic() + 10
    while True:
        report = editor.render_headless(frames=8)
        planes = report["orthogonal_planes"] if xyz else [report["ordinary_pixels"]]
        if len(planes) == (3 if xyz else 1) and all(p["readback"]["available"] for p in planes): break
        assert time.monotonic() < end
    for i, plane in enumerate(planes):
        wanted = expected[0, 0, 1] if i == 0 else expected[0, 0, :, 1] if i == 1 else expected[0, 0, :, :, 1]
        for field in ["source", "rendered_rgb"]:
            assert plane["readback"][field]["pixels"] == wanted.ravel().tolist()
    assert owner.undo_steps == 0
    editor.action("close_contrast")


def test_rgb_cancel_pending_preview_does_not_restart_until_explicit_refresh(tmp_path):
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS"))
    editor = Editor(preferences=tmp_path / "preferences.json")
    editor._editor.add_virtual_document(owner)
    editor.action("show_contrast")
    editor.action("wait_jobs")
    report = editor.render_headless()["contrast_panel"]["rgb"]
    submitted = report["submitted"]
    editor.action("contrast_range", minimum=10., maximum=40.)
    editor.cancel()
    before = owner.snapshot()
    report = editor.render_headless(frames=8)["contrast_panel"]["rgb"]
    assert report["submitted"] == submitted and not report["ready"] and not report["pending"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    editor.action("refresh_rgb_contrast_preview")
    editor.action("wait_jobs")
    assert editor.render_headless()["contrast_panel"]["rgb"]["ready"]
    editor.action("cancel_rgb_contrast_preview")
    report = editor.render_headless(frames=8)["contrast_panel"]["rgb"]
    assert not report["ready"] and not report["pending"]
    editor.action("close_contrast")


from test_contrast_apply import RGB_AUTO_FIXTURE,rgb_auto_input,rgb_auto_selection,histogram_hash


@pytest.mark.parametrize('row',[r for r in RGB_AUTO_FIXTURE['rows'] if r['previous']==0],ids=lambda r:f'rgb-ui-auto-s{r["scenario"]}-w{r["weights"]}-r{r["roi"]}-c{r["channels"]}')
def test_rgb_editor_auto_baseline_weights_whole_mapping_and_plot_match_imagej(row,tmp_path):
    owner=ij.Document(ij.Dataset(rgb_auto_input(row['scenario']),axes='YXS'));owner.select(rgb_auto_selection(row['roi']))
    editor=Editor(preferences=tmp_path/'preferences.json');editor._editor.add_virtual_document(owner)
    weights=RGB_AUTO_FIXTURE['weights'][row['weights']]
    editor.update_settings(conversion_weighted_rgb=True,conversion_red_weight=weights[0],conversion_green_weight=weights[1],conversion_blue_weight=weights[2],persist=False)
    editor.action('show_contrast');editor.action('contrast_rgb_channels',channels=row['channels']);editor.action('wait_jobs')
    report=editor.render_headless()['contrast_panel'];assert report['rgb']['ready'] and report['histogram']['counts']
    assert hashlib.sha256(np.array(report['histogram']['counts'],dtype='<u8').tobytes()).hexdigest()==row['plot_before_sha256']
    before=owner.snapshot();editor.action('contrast_auto');editor.action('wait_jobs');report=editor.render_headless()['contrast_panel']
    assert report['range']==row['range'] and report['rgb']['auto_divisor']==row['next'] and report['rgb']['auto_whole_planes']
    assert owner.snapshot().shares_data(before) and owner.snapshot().revisions.display==before.revisions.display and owner.undo_steps==0
    assert hashlib.sha256(np.array(report['histogram']['counts'],dtype='<u8').tobytes()).hexdigest()==row['plot_after_sha256']
    editor.recording=True;editor.action('contrast_apply');editor.action('wait_jobs')
    assert hashlib.sha256(owner.snapshot().dataset.numpy('YXS').tobytes()).hexdigest()==row['pixels_sha256']
    assert editor.workflow[0]['target']['roi_policy']=='whole_plane' and editor.workflow[0]['options']['channels']==row['channels']
    assert owner.undo_steps==1;owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YXS'),before.dataset.numpy('YXS'))
    editor.action('close_contrast')


def test_rgb_editor_auto_repeats_baseline_cycle_and_reset(tmp_path):
    row=next(r for r in RGB_AUTO_FIXTURE['rows'] if r['scenario']==0 and r['weights']==0 and r['roi']==0 and r['channels']==4 and r['previous']==0)
    owner=ij.Document(ij.Dataset(rgb_auto_input(0),axes='YXS'));editor=Editor(preferences=tmp_path/'preferences.json');editor._editor.add_virtual_document(owner)
    editor.action('show_contrast');editor.action('contrast_rgb_channels',channels=4)
    previous=0;before=owner.snapshot()
    for _ in range(12):
        expected=next(r for r in RGB_AUTO_FIXTURE['rows'] if r['scenario']==0 and r['weights']==0 and r['roi']==0 and r['channels']==4 and r['previous']==previous)
        editor.action('contrast_auto');editor.action('wait_jobs');report=editor.render_headless()['contrast_panel']
        assert report['range']==expected['range'] and report['rgb']['auto_divisor']==expected['next'];previous=expected['next']
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    editor.action('contrast_reset');editor.action('wait_jobs');report=editor.render_headless()['contrast_panel']
    assert report['rgb']['auto_divisor']==0 and not report['rgb']['auto_whole_planes'] and report['range']==[0.,255.]
    editor.action('close_contrast')


@pytest.mark.parametrize('failure',['pending_cancel','manual_range','close','weights'])
def test_rgb_editor_pending_auto_cannot_publish_over_newer_intent(failure,tmp_path):
    owner=ij.Document(ij.Dataset(rgb_auto_input(0),axes='YXS'));editor=Editor(preferences=tmp_path/'preferences.json');editor._editor.add_virtual_document(owner)
    editor.action('show_contrast');editor.action('wait_jobs');before=owner.snapshot();editor.action('contrast_auto')
    if failure=='pending_cancel':editor.cancel()
    elif failure=='manual_range':editor.action('contrast_range',minimum=12.,maximum=200.)
    elif failure=='close':editor.action('close_contrast')
    else:editor.update_settings(conversion_weighted_rgb=True,persist=False)
    editor.action('wait_jobs');report=editor.render_headless()['contrast_panel']
    assert not report['rgb']['auto_pending'] and report['rgb']['auto_divisor']==0
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    if failure=='manual_range':assert report['range']==[12.,200.]
    editor.action('close_contrast')
