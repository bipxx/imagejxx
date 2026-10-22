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
