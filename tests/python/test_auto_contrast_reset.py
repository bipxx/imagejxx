"""Atomic native display/Auto-cycle reset; installed Python API evidence only.

Literal cycle input and ranges are test-owned. Generic snapshot/READY helpers
come from the independent Auto API tests. No UI, provider read-count or custom
DocumentValidator claim is made: those latter seams are native C++ contracts.
"""
import copy

import imagejxx as ij
from imagejxx import _native
import numpy as np
import pytest

from test_auto_contrast import _ready, _stable, _state


STALE_KINDS = ("pixels", "metadata", "selection", "display")
INVALID_KINDS = ("nan_zoom", "outside_t", "outside_z", "channel_count", "reversed_range")
EXPECTED_COLLECTION_IDS = (
    "test_default_controller_equal_display",
    "test_active_equal_display_resets_cycle",
    "test_active_reset_invalidates_ready_job",
    "test_inactive_reset_preserves_active_ready_job",
    "test_inactive_reset_before_controller_exists",
    *(f"test_stale_dependency_refusal_is_atomic[{kind}]" for kind in STALE_KINDS),
    *(f"test_invalid_display_refusal_is_atomic[{kind}]" for kind in INVALID_KINDS),
    "test_foreign_owner_refusal_preserves_active_work",
    "test_closed_owner_refusal_preserves_active_work",
)
EXPECTED_COLLECTION_COUNT = 16


def _owner():
    # N=4096: qualifying literal bounds are10..50 at5000,20..50 at2500.
    values = np.repeat([10, 20, 30, 40, 50, 60], [1, 3, 200, 200, 200, 3492]).astype(np.uint8)
    plane = values.reshape(64, 64)
    pixels = np.broadcast_to(plane[None, None, None, :, :, None], (2, 3, 2, 64, 64, 1)).copy()
    calibration = ij.SpatialCalibration(
        spacing=(.5, 1.25, 2.5), origin=(11, -7, 19),
        direction=(-1, .25, .5, 0, 1, .5, .5, 0, 1), unit="um",
        frame_interval=.75, time_origin=2.25, time_unit="s")
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS", calibration=calibration), "Atomic reset")
    selection = ij.Selection("rectangle", [(0, 0), (64, 64)],
                             attributes={"retained": {"exact": 2**64-1}})
    previous = ij.Selection("ellipse", [(1, 2), (8, 9)], attributes={"prior": True})
    edit = owner.begin_edit()
    edit.select(selection)
    edit.set_previous_selection(previous)
    edit.set_rois([ij.ROI("positioned", previous, t=1, c=1, z=1,
                         attributes={"retained": [True, 7, "native"]})])
    edit.set_overlays([{"retained": {"text": "opaque overlay", "exact": 2**64-1}}])
    edit.set_property("retained", {"domain": "raw", "exact": 2**64-1})
    edit.set_extension("retained", [True, 7, "extension"])
    owner.commit(edit)
    # A real retained redo branch catches accidental data/history publication.
    temporary = owner.begin_edit()
    temporary.set_property("temporary", True)
    owner.commit(temporary)
    owner.undo()
    owner.mark_saved()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [2, 3, 1]
    view.zoom, view.orthogonal, view.linked_zoom = 2.5, True, False
    view.plane_zoom, view.pan = [1.5, 2., 3.], [[1., 2.], [3., 4.], [5., 6.]]
    view.properties = {"channel_display": _state(3, version=2, projection="min"),
                       "retained": {"exact": 2**64-1, "negative_zero": -0.0}}
    owner.set_display(view)
    assert not owner.snapshot().dirty and owner.redo_steps == 1
    return owner


def _app(owner):
    app = ij.Application()
    assert app.adopt(owner) == owner.id
    return app


def _history(owner):
    history = owner.history()
    pixels = history.pixels
    return (history.undo_steps, history.redo_steps, history.step_limit, history.pixel_budget,
            pixels.logical_bytes, pixels.current_bytes, pixels.retained_bytes,
            pixels.obsolete_bytes, pixels.retained_planes, pixels.exact)


def _display(view, revision=None):
    return (view.t, view.c, tuple(view.crosshair), view.orthogonal, view.linked_zoom,
            view.zoom, tuple(view.plane_zoom), tuple(tuple(p) for p in view.pan),
            copy.deepcopy(view.properties),
            float(view.properties["retained"]["negative_zero"]).hex(),
            view.revision if revision is None else revision)


def _full(owner):
    snapshot = owner.snapshot()
    r = snapshot.revisions
    return (_stable(owner), _history(owner), (r.pixels, r.metadata, r.selection, r.display),
            _display(snapshot.display))


def _same_data(owner, snapshot, stable, history):
    current = owner.snapshot()
    assert current.id == snapshot.id
    assert current.shares_data(snapshot) and current.data_identity == snapshot.data_identity
    assert current.dirty == snapshot.dirty and _stable(owner) == stable and _history(owner) == history


def _publish(app, previous, used, low):
    job = _ready(app)
    command = app.publish_auto_contrast(job)
    assert (command.cycle.previous, command.cycle.used, command.cycle.next) == (previous, used, used)
    assert command.indices == [low, 50]
    assert command.resolved_range == command.applied_range == [float(low), 50.]
    assert app.auto_contrast_divisor() == used and job.info.state == ij.JobState.COMPLETED
    return command


def _publish_ready(app, job, previous, used, low):
    command = app.publish_auto_contrast(job)
    assert (command.cycle.previous, command.cycle.used, command.cycle.next) == (previous, used, used)
    assert command.indices == [low, 50] and command.resolved_range == [float(low), 50.]
    assert app.auto_contrast_divisor() == used and job.info.state == ij.JobState.COMPLETED
    return command


def _changed_range(view):
    properties = copy.deepcopy(view.properties)
    properties["channel_display"]["channels"][1]["range"] = [0., 200.]
    view.properties = properties
    return view


def test_default_controller_equal_display():
    assert ij.Application is _native.Application
    assert callable(_native.Application.set_display_and_reset_auto_contrast)
    owner = _owner()
    app = _app(owner)
    before = owner.snapshot()
    stable, history = _stable(owner), _history(owner)
    assert app.auto_contrast_divisor() == 0
    assert app.set_display_and_reset_auto_contrast(before, before.display) is None
    after = owner.snapshot()
    _same_data(owner, before, stable, history)
    assert after.revisions.display == before.revisions.display+1
    assert _display(after.display) == _display(before.display, before.display.revision+1)
    assert app.auto_contrast_divisor() == 0
    _publish(app, 0, 5000, 10)


def test_active_equal_display_resets_cycle():
    owner = _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    before = owner.snapshot()
    stable, history = _stable(owner), _history(owner)
    app.set_display_and_reset_auto_contrast(before, before.display)
    _same_data(owner, before, stable, history)
    after = owner.snapshot()
    assert after.revisions.display == before.revisions.display+1
    assert _display(after.display) == _display(before.display, before.display.revision+1)
    assert app.auto_contrast_divisor() == 0
    # Equal-state success resets the live cycle, rather than continuing to2500.
    _publish(app, 0, 5000, 10)


def test_active_reset_invalidates_ready_job():
    owner = _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    job = _ready(app)
    assert job.resolved.cycle.previous == 5000 and job.resolved.cycle.used == 2500
    before = owner.snapshot()
    stable, history = _stable(owner), _history(owner)
    proposed = _changed_range(before.display)
    app.set_display_and_reset_auto_contrast(before, proposed)
    _same_data(owner, before, stable, history)
    assert _display(owner.snapshot().display) == _display(proposed, before.display.revision+1)
    assert owner.snapshot().revisions.display == before.revisions.display+1
    assert app.auto_contrast_divisor() == 0
    reset = _full(owner)
    with pytest.raises(ij.RevisionConflict):
        app.publish_auto_contrast(job)
    assert job.info.state == ij.JobState.FAILED and _full(owner) == reset
    assert app.auto_contrast_divisor() == 0
    _publish(app, 0, 5000, 10)


def test_inactive_reset_preserves_active_ready_job():
    owner, other = _owner(), _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    app.adopt(other, make_active=False)
    job = _ready(app)
    active = _full(owner)
    before = other.snapshot()
    stable, history = _stable(other), _history(other)
    proposed = _changed_range(before.display)
    proposed.zoom = 4.
    app.set_display_and_reset_auto_contrast(before, proposed)
    _same_data(other, before, stable, history)
    assert _display(other.snapshot().display) == _display(proposed, before.display.revision+1)
    assert other.snapshot().revisions.display == before.revisions.display+1
    assert app.active_document_id == owner.id and _full(owner) == active
    assert app.auto_contrast_divisor() == 5000 and job.info.state == ij.JobState.READY
    inactive = _full(other)
    _publish_ready(app, job, 5000, 2500, 20)
    assert _full(other) == inactive


def test_inactive_reset_before_controller_exists():
    owner, other = _owner(), _owner()
    app = _app(owner)
    app.adopt(other, make_active=False)
    active = _full(owner)
    before = other.snapshot()
    stable, history = _stable(other), _history(other)
    app.set_display_and_reset_auto_contrast(before, before.display)
    _same_data(other, before, stable, history)
    assert _display(other.snapshot().display) == _display(before.display, before.display.revision+1)
    assert app.active_document_id == owner.id and _full(owner) == active
    assert app.auto_contrast_divisor() == 0
    _publish(app, 0, 5000, 10)


@pytest.mark.parametrize("kind", STALE_KINDS)
def test_stale_dependency_refusal_is_atomic(kind):
    owner = _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    old = owner.snapshot()
    if kind == "pixels":
        edit = owner.begin_edit()
        edit.replace_plane(np.full((64, 64), 7, np.uint8), t=1, c=1, z=1)
        owner.commit(edit)
    elif kind == "metadata":
        edit = owner.begin_edit()
        edit.set_title("Metadata revision changed")
        owner.commit(edit)
    elif kind == "selection":
        owner.select(ij.Selection("point", [(2, 3)]))
    else:
        view = owner.snapshot().display
        view.zoom += .5
        owner.set_display(view)
    previous = 0 if kind in ("pixels", "metadata") else 5000
    assert app.auto_contrast_divisor() == previous
    job = _ready(app)
    before = _full(owner)
    with pytest.raises(ij.RevisionConflict):
        app.set_display_and_reset_auto_contrast(old, old.display)
    assert _full(owner) == before and app.auto_contrast_divisor() == previous
    assert job.info.state == ij.JobState.READY
    command = app.publish_auto_contrast(job)
    assert command.cycle.previous == previous
    if kind == "pixels":
        assert command.outcome == ij.AutoContrastOutcome.ResetFallback
        assert command.resolved_range == [0., 255.] and command.cycle.next == 0
    else:
        used, low = (5000, 10) if kind == "metadata" else (2500, 20)
        assert command.cycle.used == command.cycle.next == used
        assert command.resolved_range == [float(low), 50.]
    assert app.auto_contrast_divisor() == command.cycle.next


@pytest.mark.parametrize("kind", INVALID_KINDS)
def test_invalid_display_refusal_is_atomic(kind):
    owner = _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    job = _ready(app)
    expected = owner.snapshot()
    proposed = expected.display
    if kind == "nan_zoom":
        proposed.zoom = float("nan")
    elif kind == "outside_t":
        proposed.t = 2
    elif kind == "outside_z":
        proposed.crosshair = [2, 3, 2]
    else:
        properties = copy.deepcopy(proposed.properties)
        if kind == "channel_count":
            properties["channel_display"]["channels"].pop()
        else:
            properties["channel_display"]["channels"][1]["range"] = [201., 1.]
        proposed.properties = properties
    before = _full(owner)
    expected_error = IndexError if kind in ("outside_t", "outside_z") else ValueError
    with pytest.raises(expected_error):
        app.set_display_and_reset_auto_contrast(expected, proposed)
    assert _full(owner) == before and app.auto_contrast_divisor() == 5000
    assert job.info.state == ij.JobState.READY
    _publish_ready(app, job, 5000, 2500, 20)


def test_foreign_owner_refusal_preserves_active_work():
    owner, foreign = _owner(), _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    job = _ready(app)
    before, other = _full(owner), _full(foreign)
    members = list(app.document_ids)
    with pytest.raises(ij.DocumentUnavailable):
        app.set_display_and_reset_auto_contrast(foreign.snapshot(), foreign.snapshot().display)
    assert _full(owner) == before and _full(foreign) == other
    assert app.document_ids == members and app.active_document_id == owner.id
    assert app.auto_contrast_divisor() == 5000 and job.info.state == ij.JobState.READY
    _publish_ready(app, job, 5000, 2500, 20)


def test_closed_owner_refusal_preserves_active_work():
    owner, closed = _owner(), _owner()
    app = _app(owner)
    _publish(app, 0, 5000, 10)
    app.adopt(closed, make_active=False)
    captured = closed.snapshot()
    app.close(closed.id)
    job = _ready(app)
    before, other = _full(owner), _full(closed)
    members = list(app.document_ids)
    with pytest.raises(ij.DocumentUnavailable):
        app.set_display_and_reset_auto_contrast(captured, captured.display)
    assert _full(owner) == before and _full(closed) == other
    assert app.document_ids == members and app.active_document_id == owner.id and not app.contains(closed.id)
    assert app.auto_contrast_divisor() == 5000 and job.info.state == ij.JobState.READY
    _publish_ready(app, job, 5000, 2500, 20)
