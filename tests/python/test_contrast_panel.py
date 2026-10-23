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
    editor.action("contrast_rgb_channels", channels=row["channels"])
    editor.action("contrast_range", minimum=low, maximum=high)
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
    editor.action("contrast_rgb_channels", channels=channels)
    editor.action("contrast_range", minimum=10., maximum=40.)
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
    editor.action("contrast_rgb_channels", channels=4)
    editor.action("contrast_range", minimum=10., maximum=40.)
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


from test_contrast_apply_command import SEQUENCE_FIXTURE


def promoted_rgb_editor(tmp_path,row=None):
    row=row or next(r for r in SEQUENCE_FIXTURE["rows"] if r["range_index"]==0 and r["first_channels"]==4 and r["second_channels"]==2 and r["roi"]==0)
    owner=ij.Document(ij.Dataset(rgb_auto_input(0),axes="YXS"));owner.select(rgb_auto_selection(row["roi"]))
    view=owner.snapshot().display;view.properties={"display_range":[12.,220.]};owner.set_display(view)
    editor=Editor(preferences=tmp_path/"preferences.json");editor._editor.add_virtual_document(owner)
    editor.action("show_contrast");editor.action("contrast_rgb_channels",channels=row["first_channels"])
    return owner,editor,row


def rgb_editor_hash(editor):
    editor.action("show_channel_display")
    return hashlib.sha256(np.array(editor.render_headless(frames=2)["ordinary_pixels"]["readback"]["source"]["pixels"],np.uint8).tobytes()).hexdigest()


@pytest.mark.parametrize("mode",["reset","initial","moved","auto"])
@pytest.mark.parametrize("row",SEQUENCE_FIXTURE["rows"],ids=lambda r:f'promoted-ui-r{r["range_index"]}-a{r["first_channels"]}-b{r["second_channels"]}-roi{r["roi"]}')
def test_rgb_editor_component_promotion_matches_actual_imagej_and_records_complete_sequence(row,mode,tmp_path):
    owner,editor,_=promoted_rgb_editor(tmp_path,row);before=owner.snapshot()
    editor.action("contrast_range",minimum=SEQUENCE_FIXTURE["first_ranges"][row["range_index"]][0],maximum=SEQUENCE_FIXTURE["first_ranges"][row["range_index"]][1]);editor.action("wait_jobs")
    editor.action("contrast_rgb_channels",channels=row["second_channels"]);editor.action("wait_jobs")
    report=editor.render_headless()["contrast_panel"];assert report["range"]==[0.,255.] and report["rgb"]["promoted_stages"]==1
    if mode!="reset":
        editor.action("contrast_range",minimum=SEQUENCE_FIXTURE["initial_range"][0],maximum=SEQUENCE_FIXTURE["initial_range"][1])
        if mode in ["moved","auto"]:editor.action("contrast_range",minimum=SEQUENCE_FIXTURE["moved_range"][0],maximum=SEQUENCE_FIXTURE["moved_range"][1])
    if mode=="auto":
        editor.action("contrast_rgb_channels",channels=row["first_channels"])
        editor.action("contrast_auto")
    editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert report["rgb"]["ready"] and report["rgb"]["promoted_stages"]==(2 if mode=="auto" else 1)
    if mode=="auto":assert report["range"]==row["auto_range"] and report["rgb"]["auto_divisor"]==row["auto_divisor"]
    expected=row[{"reset":"promoted_sha256","initial":"initial_sha256","moved":"moved_sha256","auto":"auto_sha256"}[mode]]
    assert rgb_editor_hash(editor)==expected
    assert before.shares_data(owner.snapshot()) and owner.snapshot().revisions.display==before.revisions.display and owner.undo_steps==0
    ready=editor.render_headless()["contrast_panel"]["rgb"]["job_id"];editor.recording=True;editor.action("contrast_apply");assert editor.job_info.id==ready;editor.action("wait_jobs")
    assert owner.undo_steps==1 and len(editor.workflow)==1 and hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest()==expected
    record=editor.workflow[0];program=ij.NativeWorkflowProgram.from_record(record);assert len(program.steps)==(3 if mode=="auto" else 2)
    assert record["steps"][0]["original"]["options"]["channels"]==row["first_channels"]
    replay=ij.Document(before);replay.commit(ij.prepare_native_workflow(replay.snapshot(),program.steps).begin_edit());assert hashlib.sha256(replay.snapshot().dataset.numpy("YXS").tobytes()).hexdigest()==expected
    owner.undo();assert owner.snapshot().shares_data(before);owner.redo();assert hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest()==expected
    editor.action("close_contrast")


@pytest.mark.parametrize("wait_before_switch",[False,True])
def test_rgb_editor_component_switch_keeps_latest_pending_range_once(wait_before_switch,tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path)
    for low,high in [(1,50),(2,60),(10.9,40.1)]:editor.action("contrast_range",minimum=low,maximum=high)
    if wait_before_switch:editor.action("wait_jobs")
    editor.action("contrast_rgb_channels",channels=2)
    for low,high in [(5,80),(20.25,200.75),(-10.2,160.9)]:editor.action("contrast_range",minimum=low,maximum=high)
    editor.action("wait_jobs");assert rgb_editor_hash(editor)==row["moved_sha256"]
    assert editor.render_headless()["contrast_panel"]["rgb"]["promoted_stages"]==1 and owner.undo_steps==0
    editor.action("close_contrast")


@pytest.mark.parametrize("channels",[1,4,7])
def test_rgb_editor_reselecting_current_component_preserves_auto_cycle_and_preview(channels,tmp_path):
    row=next(r for r in SEQUENCE_FIXTURE["rows"] if r["range_index"]==0 and r["first_channels"]==channels and r["second_channels"]!=channels and r["roi"]==0)
    owner,editor,_=promoted_rgb_editor(tmp_path,row);editor.action("contrast_auto");editor.action("wait_jobs")
    before=editor.render_headless()["contrast_panel"]
    editor.action("contrast_rgb_channels",channels=channels);editor.action("wait_jobs");after=editor.render_headless()["contrast_panel"]
    for key in ["auto_divisor","promoted_stages","job_id"]:assert after["rgb"][key]==before["rgb"][key]
    assert after["range"]==before["range"] and owner.undo_steps==0;editor.action("close_contrast")


@pytest.mark.parametrize("change",["zoom","title","display_range"])
def test_rgb_editor_promoted_draft_and_target_survive_compatible_source_changes(change,tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path);editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2)
    editor.action("configure_contrast_target",target={"scope":"positions","positions":[[0,0,0]],"roi":"whole_plane"})
    editor.action("contrast_range",minimum=-10.2,maximum=160.9);editor.action("wait_jobs")
    if change=="title":
        edit=owner.begin_edit();edit.set_title("changed title");owner.commit(edit)
    else:
        view=owner.snapshot().display
        if change=="zoom":view.zoom=2
        else:view.properties={"display_range":[20.,80.]}
        owner.set_display(view)
    stable=owner.snapshot();steps=owner.undo_steps;editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert report["range"]==[-10.2,160.9] and report["rgb"]["promoted_stages"]==1 and rgb_editor_hash(editor)==row["moved_sha256"]
    editor.recording=True;editor.action("contrast_apply");editor.action("wait_jobs")
    assert editor.workflow[0]["steps"][1]["target"]["scope"]==int(ij.TargetScope.POSITIONS) and owner.undo_steps==steps+1
    owner.undo();assert owner.snapshot().shares_data(stable);editor.action("close_contrast")


@pytest.mark.parametrize("failure",["cancel","close","range","channels","weights","selection"])
def test_rgb_editor_pending_promoted_auto_cannot_publish_after_new_intent(failure,tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path);before=owner.snapshot()
    editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2);editor.action("contrast_auto")
    if failure=="cancel":editor.cancel()
    elif failure=="close":editor.action("close_contrast")
    elif failure=="range":editor.action("contrast_range",minimum=30.,maximum=90.)
    elif failure=="channels":editor.action("contrast_rgb_channels",channels=1)
    elif failure=="weights":editor.update_settings(conversion_weighted_rgb=True,persist=False)
    else:owner.select(ij.Selection("rectangle",[(1,0),(4,3)]))
    editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert not report["rgb"]["auto_pending"] and report["rgb"]["auto_divisor"]==0 and owner.undo_steps==0
    if failure!="selection":assert owner.snapshot().shares_data(before)
    else:assert owner.snapshot().dataset.shares_pixels(before.dataset)
    if failure=="range":assert report["range"]==[30.,90.]
    editor.action("close_contrast")


@pytest.mark.parametrize("virtual",[False,True])
@pytest.mark.parametrize("xyz",[False,True])
def test_rgb_editor_union_preview_keeps_prior_plane_when_current_target_excludes_it(virtual,xyz,tmp_path):
    pixels=np.tile(np.array([[[10,20,30],[20,30,40],[30,40,50],[40,50,60]]]*3,np.uint8),(1,1,3,1,1,1))
    dataset=ij.Dataset(pixels,axes="TCZYXS")
    if virtual:
        from test_virtual_tiff import write
        from imagejxx import scientific_io as io
        path=tmp_path/"union.tif";write(path,pixels,io.ImageFileFormat.SCIENTIFIC_TIFF_RGB8_V1);dataset=io.open_virtual_image_series(path).dataset
    owner=ij.Document(dataset);view=owner.snapshot().display;view.crosshair,view.orthogonal=[1,1,1],xyz;view.properties={"display_range":[10.,40.]};owner.set_display(view)
    editor=Editor(preferences=tmp_path/"preferences.json");editor.update_settings(async_viewport=True,persist=False);editor._editor.add_virtual_document(owner)
    editor.action("show_contrast");editor.action("contrast_rgb_channels",channels=4);editor.action("contrast_range",minimum=10.,maximum=40.);editor.action("wait_jobs")
    editor.action("contrast_rgb_channels",channels=2);editor.action("configure_contrast_target",target={"scope":"positions","positions":[[0,0,0]],"roi":"whole_plane"});editor.action("contrast_range",minimum=20.,maximum=80.);editor.action("wait_jobs");editor.action("show_channel_display")
    expected=pixels.copy();expected[0,0,1,:,:,0]=np.clip(np.trunc((pixels[0,0,1,:,:,0].astype(float)-10)*256/30),0,255);expected[0,0,0,:,:,1]=np.clip(np.trunc((pixels[0,0,0,:,:,1].astype(float)-20)*256/60),0,255)
    end=time.monotonic()+10
    while True:
        report=editor.render_headless(frames=8);planes=report["orthogonal_planes"] if xyz else [report["ordinary_pixels"]]
        if len(planes)==(3 if xyz else 1) and all(p["readback"]["available"] for p in planes):break
        assert time.monotonic()<end
    for i,plane in enumerate(planes):
        wanted=expected[0,0,1] if i==0 else expected[0,0,:,1] if i==1 else expected[0,0,:,:,1]
        for field in ["source","rendered_rgb"]:assert plane["readback"][field]["pixels"]==wanted.ravel().tolist()
    assert owner.undo_steps==0;editor.action("contrast_apply");editor.action("wait_jobs");np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"),expected)
    editor.action("close_contrast")


def test_rgb_editor_component_promotion_budget_refusal_keeps_previous_intent(tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path);editor.update_settings(processing_memory_mb=8,persist=False)
    editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2);editor.action("contrast_range",minimum=-10.2,maximum=160.9);editor.action("wait_jobs")
    before=editor.render_headless()["contrast_panel"]
    with pytest.raises((ValueError,RuntimeError)):editor.action("contrast_rgb_channels",channels=1)
    after=editor.render_headless()["contrast_panel"]
    assert after["range"]==before["range"] and after["rgb"]["channels"]==2 and after["rgb"]["promoted_stages"]==1 and after["rgb"]["job_id"]==before["rgb"]["job_id"]
    assert owner.undo_steps==0;editor.action("close_contrast")


@pytest.mark.parametrize("roi",[0,1,2])
@pytest.mark.parametrize("change",["zoom","cancel_refresh"])
def test_rgb_editor_promoted_auto_preserves_whole_plane_policy_across_view_refresh(roi,change,tmp_path):
    row=next(r for r in SEQUENCE_FIXTURE["rows"] if r["range_index"]==0 and r["first_channels"]==4 and r["second_channels"]==2 and r["roi"]==roi)
    owner,editor,_=promoted_rgb_editor(tmp_path,row)
    editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2);editor.action("contrast_range",minimum=-10.2,maximum=160.9);editor.action("contrast_rgb_channels",channels=4);editor.action("contrast_auto");editor.action("wait_jobs")
    assert rgb_editor_hash(editor)==row["auto_sha256"]
    if change=="zoom":
        view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    else:editor.action("cancel_rgb_contrast_preview");editor.action("refresh_rgb_contrast_preview")
    editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert report["rgb"]["auto_whole_planes"] and report["rgb"]["promoted_stages"]==2 and report["range"]==row["auto_range"] and rgb_editor_hash(editor)==row["auto_sha256"]
    assert owner.undo_steps==0;editor.action("close_contrast")


from test_contrast_apply import STACK_FIXTURE, rgb_stack_owner


@pytest.mark.parametrize("row",[r for r in STACK_FIXTURE["rows"] if not r["whole_plane"]],ids=lambda r:f'stack-panel-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}-a{r["anchor"]}-p{int(r["prefix"])}')
def test_rgb_entire_stack_panel_matches_actual_imagej_and_records_complete_capture(row,tmp_path):
    owner=rgb_stack_owner(row);editor=Editor(preferences=tmp_path/"preferences.json");editor._editor.add_virtual_document(owner);before=owner.snapshot()
    editor.action("show_contrast");editor.action("contrast_rgb_stack",enabled=True)
    if row["prefix"]:
        editor.action("contrast_range",minimum=STACK_FIXTURE["prefix_range"][0],maximum=STACK_FIXTURE["prefix_range"][1])
        editor.action("contrast_rgb_channels",channels=2 if row["channels"]==7 else row["channels"])
    editor.action("contrast_rgb_channels",channels=row["channels"])
    low,high=STACK_FIXTURE["ranges"][row["range_index"]];editor.action("contrast_range",minimum=low,maximum=high);editor.action("wait_jobs")
    assert rgb_editor_hash(editor)==row["active_sha256"] and owner.snapshot().shares_data(before) and owner.undo_steps==0
    report=editor.render_headless()["contrast_panel"];assert report["rgb"]["entire_stack"] and report["rgb"]["promoted_stages"]==int(row["prefix"])
    ready=report["rgb"]["job_id"];editor.recording=True;editor.action("contrast_apply");assert editor.job_info.id==ready;editor.action("wait_jobs")
    assert hashlib.sha256(owner.snapshot().dataset.numpy("TCZYXS").tobytes()).hexdigest()==row["stack_sha256"] and owner.undo_steps==int(row["roi"]!=3)
    record=editor.workflow[0];ij.validate_rgb_contrast_stack_command_record(record);assert record["capture"]["reset_anchor"]==STACK_FIXTURE["anchors"][row["anchor"]] and len(record["stages"])==1+int(row["prefix"])
    assert record["stages"][-1]=={"minimum":low,"maximum":high,"channels":row["channels"],"whole_plane":False}
    replay=ij.Document(before);request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(*STACK_FIXTURE["anchors"][row["anchor"]])])
    step=ij.NativeWorkflowOperation("rgb_contrast_stack",{},request,original=record);replay.commit(ij.prepare_native_workflow(replay.snapshot(),[step]).begin_edit())
    assert hashlib.sha256(replay.snapshot().dataset.numpy("TCZYXS").tobytes()).hexdigest()==row["stack_sha256"]
    if row["roi"]!=3:
        owner.undo();assert owner.snapshot().shares_data(before);owner.redo();assert hashlib.sha256(owner.snapshot().dataset.numpy("TCZYXS").tobytes()).hexdigest()==row["stack_sha256"]
    else:assert owner.snapshot().shares_data(before) and owner.snapshot().display.properties==before.display.properties
    editor.action("close_contrast")


@pytest.mark.parametrize("roi",[0,1,2])
@pytest.mark.parametrize("first",[1,4,7])
def test_rgb_entire_stack_promoted_auto_matches_actual_imagej_and_records_whole_policy(roi,first,tmp_path):
    second=2 if first!=2 else 4
    row=next(r for r in SEQUENCE_FIXTURE["rows"] if r["range_index"]==0 and r["first_channels"]==first and r["second_channels"]==second and r["roi"]==roi)
    owner,editor,_=promoted_rgb_editor(tmp_path,row);before=owner.snapshot();editor.action("contrast_rgb_stack",enabled=True)
    editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=second)
    editor.action("contrast_range",minimum=-10.2,maximum=160.9);editor.action("contrast_rgb_channels",channels=first)
    editor.action("contrast_auto");editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert report["range"]==row["auto_range"] and report["rgb"]["auto_whole_planes"] and report["rgb"]["promoted_stages"]==2 and rgb_editor_hash(editor)==row["auto_sha256"]
    editor.recording=True;editor.action("contrast_apply");editor.action("wait_jobs");record=editor.workflow[0]
    assert len(record["stages"])==3 and record["stages"][-1]["whole_plane"] and record["stages"][-1]["channels"]==first
    assert hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest()==row["auto_sha256"]
    owner.undo();assert owner.snapshot().shares_data(before);editor.action("close_contrast")


@pytest.mark.parametrize("virtual",[False,True])
@pytest.mark.parametrize("xyz",[False,True])
def test_rgb_entire_stack_preview_covers_all_planes_on_eager_virtual_xy_xyz(virtual,xyz,tmp_path):
    pixels=np.tile(np.array([20,30,40],np.uint8),(2,2,3,3,4,1));dataset=ij.Dataset(pixels,axes="TCZYXS")
    if virtual:
        from test_virtual_tiff import write
        from imagejxx import scientific_io as io
        path=tmp_path/"stack.tif";write(path,pixels,io.ImageFileFormat.SCIENTIFIC_TIFF_RGB8_V1);dataset=io.open_virtual_image_series(path).dataset
    owner=ij.Document(dataset);owner.select(ij.Selection("rectangle",[(1,0),(3,2)]));view=owner.snapshot().display;view.t=view.c=1;view.crosshair=[1,1,1];view.orthogonal=xyz;view.properties={"display_range":[12.,220.]};owner.set_display(view)
    before=owner.snapshot();editor=Editor(preferences=tmp_path/"preferences.json");editor.update_settings(async_viewport=True,persist=False);editor._editor.add_virtual_document(owner)
    editor.action("show_contrast");editor.action("contrast_rgb_stack",enabled=True);editor.action("contrast_range",minimum=10.,maximum=40.);editor.action("contrast_rgb_channels",channels=4);editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("wait_jobs");editor.action("show_channel_display")
    expected=pixels.copy();expected[:,:,:,:2,1:3,0]=85;expected[1,1,1,:2,1:3]=[255,170,255]
    end=time.monotonic()+10
    while True:
        report=editor.render_headless(frames=8);planes=report["orthogonal_planes"] if xyz else [report["ordinary_pixels"]]
        if len(planes)==(3 if xyz else 1) and all(p["readback"]["available"] for p in planes):break
        assert time.monotonic()<end
    for i,plane in enumerate(planes):
        wanted=expected[1,1,1] if i==0 else expected[1,1,:,1] if i==1 else expected[1,1,:,:,1]
        for field in ["source","rendered_rgb"]:assert plane["readback"][field]["pixels"]==wanted.ravel().tolist()
    assert owner.snapshot().shares_data(before);editor.action("contrast_apply");editor.action("wait_jobs");np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"),expected)
    owner.undo();assert owner.snapshot().shares_data(before);editor.action("close_contrast")


@pytest.mark.parametrize("before_switch",[False,True])
def test_rgb_entire_stack_mode_switch_preserves_current_plane_promotions(before_switch,tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path)
    editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2);editor.action("contrast_range",minimum=-10.2,maximum=160.9)
    if before_switch:editor.action("wait_jobs")
    editor.action("contrast_rgb_stack",enabled=True);editor.action("wait_jobs");assert rgb_editor_hash(editor)==row["moved_sha256"]
    report=editor.render_headless()["contrast_panel"];ready=report["rgb"]["job_id"];editor.action("contrast_rgb_stack",enabled=True)
    assert editor.render_headless()["contrast_panel"]["rgb"]["job_id"]==ready
    editor.action("contrast_rgb_stack",enabled=False);editor.action("wait_jobs");assert rgb_editor_hash(editor)==row["moved_sha256"] and owner.undo_steps==0
    editor.action("close_contrast")


def test_rgb_entire_stack_mode_rejects_noncurrent_promoted_targets_without_changing_intent(tmp_path):
    pixels=np.tile(np.array([20,30,40],np.uint8),(1,1,2,2,4,1));owner=ij.Document(ij.Dataset(pixels,axes="TCZYXS"));view=owner.snapshot().display;view.crosshair=[0,0,1];owner.set_display(view)
    editor=Editor(preferences=tmp_path/"preferences.json");editor._editor.add_virtual_document(owner);editor.action("show_contrast")
    editor.action("configure_contrast_target",target={"scope":"positions","positions":[[0,0,0]],"roi":"whole_plane"});editor.action("contrast_range",minimum=10.,maximum=40.);editor.action("contrast_rgb_channels",channels=4);editor.action("wait_jobs")
    before=editor.render_headless()["contrast_panel"]
    with pytest.raises((ValueError,RuntimeError)):editor.action("contrast_rgb_stack",enabled=True)
    after=editor.render_headless()["contrast_panel"];assert not after["rgb"]["entire_stack"]
    for field in ["job_id","promoted_stages","channels"]:assert after["rgb"][field]==before["rgb"][field]
    assert after["range"]==before["range"] and owner.undo_steps==0;editor.action("close_contrast")


@pytest.mark.parametrize("bad",[1,0,"true",None])
def test_rgb_entire_stack_mode_requires_boolean(bad,tmp_path):
    owner,editor,_=promoted_rgb_editor(tmp_path)
    with pytest.raises((ValueError,RuntimeError)):editor.action("contrast_rgb_stack",enabled=bad)
    assert not editor.render_headless()["contrast_panel"]["rgb"]["entire_stack"] and owner.undo_steps==0;editor.action("close_contrast")


@pytest.mark.parametrize("failure",["cancel_refresh","close","selection","range","mode"])
def test_rgb_entire_stack_pending_intent_cannot_publish_old_pixels(failure,tmp_path):
    owner,editor,_=promoted_rgb_editor(tmp_path);before=owner.snapshot();editor.action("contrast_rgb_stack",enabled=True);editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2)
    if failure=="cancel_refresh":editor.cancel();editor.action("wait_jobs");editor.action("refresh_rgb_contrast_preview")
    elif failure=="close":editor.action("close_contrast")
    elif failure=="selection":owner.select(ij.Selection("rectangle",[(1,0),(4,2)]))
    elif failure=="range":editor.action("contrast_range",minimum=20.,maximum=80.)
    else:editor.action("contrast_rgb_stack",enabled=False)
    editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert owner.undo_steps==0 and not editor.workflow
    if failure!="selection":assert owner.snapshot().shares_data(before)
    else:assert owner.snapshot().dataset.shares_pixels(before.dataset)
    if failure=="close":assert not report["open"] and not report["rgb"]["entire_stack"]
    elif failure=="mode":assert not report["rgb"]["entire_stack"]
    else:assert report["rgb"]["entire_stack"] and report["rgb"]["ready"]
    editor.action("close_contrast")


def test_rgb_entire_stack_capture_replays_with_explicit_current_source_rebind(tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path);before=owner.snapshot();editor.action("contrast_rgb_stack",enabled=True);editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("wait_jobs");editor.recording=True;editor.action("contrast_apply");editor.action("wait_jobs")
    record=editor.workflow[0];expected=owner.snapshot().dataset.numpy("YXS").copy();owner.undo();editor.action("close_contrast")
    with pytest.raises(ij.RevisionConflict):editor.action("replay_contrast_apply",record=record)
    with pytest.raises((ValueError,RuntimeError)):editor.action("replay_contrast_apply",record=record,contrast_rebind={"scope":"all_z"})
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    editor.action("replay_contrast_apply",record=record,contrast_rebind={});editor.action("wait_jobs");np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"),expected)
    assert owner.undo_steps==1 and len(editor.workflow)==2


def test_rgb_entire_stack_cumulative_budget_refusal_keeps_ready_uniform_preview(tmp_path):
    pixels=np.tile(np.array([20,30,40],np.uint8),(2,3,64,8,256,1));owner=ij.Document(ij.Dataset(pixels,axes="TCZYXS"));editor=Editor(preferences=tmp_path/"preferences.json");editor.update_settings(processing_memory_mb=8,persist=False);editor._editor.add_virtual_document(owner)
    editor.action("show_contrast");editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("wait_jobs");before=editor.render_headless()["contrast_panel"]
    assert before["rgb"]["ready"]
    with pytest.raises((ValueError,RuntimeError)):editor.action("contrast_rgb_stack",enabled=True)
    after=editor.render_headless()["contrast_panel"];assert not after["rgb"]["entire_stack"] and after["rgb"]["job_id"]==before["rgb"]["job_id"] and after["range"]==before["range"] and owner.undo_steps==0
    editor.action("close_contrast")


def test_rgb_entire_stack_rejects_unused_uniform_target_configuration(tmp_path):
    owner,editor,_=promoted_rgb_editor(tmp_path);editor.action("contrast_rgb_stack",enabled=True);editor.action("wait_jobs");before=editor.render_headless()["contrast_panel"]
    with pytest.raises((ValueError,RuntimeError)):editor.action("configure_contrast_target",target={"scope":"all_z","roi":"whole_plane"})
    after=editor.render_headless()["contrast_panel"];assert after["rgb"]["job_id"]==before["rgb"]["job_id"] and after["rgb"]["entire_stack"] and owner.undo_steps==0;editor.action("close_contrast")



def test_rgb_entire_stack_compact_promotions_admit_64_stages_and_refuse_65_without_replacing_preview(tmp_path):
    owner,editor,_=promoted_rgb_editor(tmp_path);before=owner.snapshot();editor.action("contrast_rgb_stack",enabled=True);channels=4
    for _ in range(63):
        editor.action("contrast_range",minimum=10.9,maximum=40.1);channels=2 if channels==4 else 4;editor.action("contrast_rgb_channels",channels=channels)
    editor.action("contrast_range",minimum=20.,maximum=200.);editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert report["rgb"]["promoted_stages"]==63 and report["rgb"]["ready"] and owner.snapshot().shares_data(before)
    with pytest.raises((ValueError,RuntimeError)):editor.action("contrast_rgb_channels",channels=2 if channels==4 else 4)
    after=editor.render_headless()["contrast_panel"]
    assert after["rgb"]["job_id"]==report["rgb"]["job_id"] and after["rgb"]["promoted_stages"]==63 and after["range"]==report["range"]
    editor.recording=True;editor.action("contrast_apply");editor.action("wait_jobs");assert len(editor.workflow[0]["stages"])==64 and owner.undo_steps==1
    owner.undo();assert owner.snapshot().shares_data(before);editor.action("close_contrast")


@pytest.mark.parametrize("change",["zoom","title","display_range"])
def test_rgb_entire_stack_compact_promotions_survive_compatible_source_changes(change,tmp_path):
    owner,editor,row=promoted_rgb_editor(tmp_path);editor.action("contrast_rgb_stack",enabled=True);editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2);editor.action("contrast_range",minimum=-10.2,maximum=160.9);editor.action("wait_jobs")
    if change=="title":
        edit=owner.begin_edit();edit.set_title("stack title");owner.commit(edit)
    else:
        view=owner.snapshot().display
        if change=="zoom":view.zoom=2
        else:view.properties={"display_range":[20.,80.]}
        owner.set_display(view)
    stable=owner.snapshot();history=owner.undo_steps;editor.action("wait_jobs");report=editor.render_headless()["contrast_panel"]
    assert report["rgb"]["entire_stack"] and report["rgb"]["promoted_stages"]==1 and report["range"]==[-10.2,160.9] and rgb_editor_hash(editor)==row["moved_sha256"]
    editor.action("contrast_apply");editor.action("wait_jobs");assert owner.undo_steps==history+1;owner.undo();assert owner.snapshot().shares_data(stable);editor.action("close_contrast")



@pytest.mark.parametrize("stack",[False,True])
def test_rgb_session_stack_switch_preserves_frozen_nonarea_prefix(stack,tmp_path):
    owner,editor,_=promoted_rgb_editor(tmp_path);owner.select(ij.Selection("line",[(0,0),(3,1)]));editor.action("wait_jobs")
    if stack:editor.action("contrast_rgb_stack",enabled=True)
    editor.action("contrast_range",minimum=10.9,maximum=40.1);editor.action("contrast_rgb_channels",channels=2);editor.action("wait_jobs");before=editor.render_headless()["contrast_panel"]
    expected=rgb_editor_hash(editor)
    assert before["rgb"]["promoted_stages"]==1 and before["rgb"]["session_active"]
    editor.action("contrast_rgb_stack",enabled=not stack);editor.action("wait_jobs");after=editor.render_headless()["contrast_panel"]
    assert after["rgb"]["entire_stack"]==(not stack) and after["range"]==before["range"]
    assert after["rgb"]["promoted_stages"]==1 and rgb_editor_hash(editor)==expected
    editor.recording=True;editor.action("contrast_apply");editor.action("wait_jobs")
    record=editor.workflow[-1]
    assert record["schema"]=="imagejxx.rgb-contrast-session-command" and len(record["visits"])==1
    assert len(record["visits"][0]["stages"])==2
    assert all(stage["selection"]["kind"]=="line" for stage in record["visits"][0]["stages"])
    assert hashlib.sha256(owner.snapshot().dataset.numpy("YXS").tobytes()).hexdigest()==expected
    editor.action("close_contrast")
