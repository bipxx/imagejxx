"""Installed shared Window Auto APIs through binding and Python facade.

These are controller/API checks, not ImGui pointer, keyboard, rendering or
upstream GUI acceptance. Input pixels/ranges/records are independently fixed;
no Auto resolver, histogram or compositor supplies an expected result.
"""
import copy
import json
import time

import numpy as np
import pytest

import test_channel_display_projection_desktop_api as shared


LAYERS = ("binding", "wrapper")
TEST_NAMES = (
    "test_auto_captured_nonzero_tcz_area_and_exact_record",
    "test_auto_all_scope_and_native_color_gray_precision",
    "test_auto_configured_defaults_overrides_and_frozen_settings",
    "test_auto_repeated_cycle_selection_numeric_set_and_reset",
    "test_auto_recording_is_captured_at_submission",
    "test_auto_jobs_wait_and_both_cancellation_routes",
    "test_auto_close_cancels_source_without_redirecting",
    "test_auto_active_owner_and_away_back_guards",
    "test_auto_all_four_ready_stale_guards_are_atomic",
    "test_auto_strict_replay_uses_frozen_effect_and_preserves_cycle",
    "test_auto_complete_rebind_uses_destination_and_effective_divisor",
    "test_auto_exact_parameter_and_rebind_admission",
    "test_auto_malformed_original_precedes_closed_owner_and_rebind",
    "test_auto_stale_source_and_foreign_replay_are_atomic",
    "test_auto_unsupported_empty_and_no_finite_refusals",
    "test_auto_native_workspace_reopen_retains_records_and_resets_cycle",
)
EXPECTED_COLLECTION_COUNT = 32
EXPECTED_COLLECTION_IDS = tuple(f"{name}[{layer}]" for name in TEST_NAMES for layer in LAYERS)
ERRORS = (ValueError, TypeError, OverflowError)
BITS = (("auto", 0, 7.), ("8", 8, 255.), ("10", 10, 1023.), ("12", 12, 4095.),
        ("14", 14, 16383.), ("15", 15, 32767.), ("16", 16, 65535.))
VALUES = np.repeat([1, 2, 3, 4], [1, 10, 11, 78])
CYCLE_VALUES = np.repeat([10, 20, 30, 40, 50, 60], [1, 3, 200, 200, 200, 3492])
editor = shared.editor  # Required installed modules; this fixture has no skip/fallback.


@pytest.fixture(autouse=True)
def native_module(editor):
    global ij
    ij = shared.ij


def _state(mode="composite"):
    return {"schema": "imagejxx.channel-display", "version": 2, "mode": mode, "projection": "min",
            "channels": [{"visible": c != 1, "range": [10.*c, 99.+10*c],
                          "lut": {"name": ("Red", "Green", "Blue")[c]}} for c in range(3)]}


def _owner(editor, values=VALUES, dtype=np.uint8, *, mode="composite", selection=None,
           flat=False, cycle=False, quiet=False, enabled=True, samples=1):
    values = CYCLE_VALUES if cycle else np.asarray(values)
    height, width = (64, 64) if cycle else (1, len(values)) if flat else (10, 10)
    assert height*width == len(values)
    t, c, z, y, x, s = np.indices((2, 3, 2, height, width, samples), dtype=np.int64)
    pixels = (20*t+30*c+5*z+2*y+x+s).astype(dtype)
    pixels[1, 1, 1] = np.broadcast_to(np.asarray(values, dtype=dtype).reshape(height, width, 1), (height, width, samples))
    calibration = ij.SpatialCalibration(spacing=(.5, 1.25, 2.5), origin=(11, -7, 19),
        direction=(-1, .25, .5, 0, 1, .5, .5, 0, 1), unit="um",
        frame_interval=.75, time_origin=2.25, time_unit="s")
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if quiet else ij.FloatingSamplePolicy.FINITE_ONLY
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS", calibration=calibration, sample_policy=policy), "Desktop Auto")
    edit = owner.begin_edit()
    edit.set_property("source_only", {"nested": [True, 7, "retained"]})
    edit.set_extension("future_annotation", {"version": 37, "opaque": [3, 1, 4]})
    edit.set_record_layout({"source_presence": True})
    if selection is not None: edit.select(selection)
    previous = ij.Selection("line", [(0, 0), (min(2, width-1), min(2, height-1))])
    edit.set_previous_selection(previous)
    edit.set_rois([ij.ROI("stored", ij.Selection("rectangle", [(0, 0), (1, 1)]), t=0, c=0, z=0)])
    edit.set_overlays([{"future_annotation": {"version": 37, "opaque": [3, 1, 4]}}])
    owner.commit(edit)
    edit = owner.begin_edit(); edit.set_property("redo_probe", True); owner.commit(edit); owner.undo(); owner.mark_saved()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [min(2, width-1), min(3, height-1), 1]
    view.zoom, view.orthogonal, view.linked_zoom = 1.25, True, False
    view.plane_zoom, view.pan = [1.25, 2., 3.], [[3., -2.], [1., 4.], [-5., 2.]]
    view.properties = {"display_range": [7., 19.], "lut": "Ice",
                       "custom_lut": [[i, 255-i, 0] for i in range(256)],
                       **({"channel_display": _state(mode)} if enabled else {})}
    owner.set_display(view)
    editor.add_native_document(owner)
    assert owner.snapshot().dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()
    return owner


def _action(editor, layer, name, **parameters):
    original = copy.deepcopy(parameters)
    if layer == "wrapper":
        result = editor.action(name, **parameters)
    else:
        result = editor._editor.action(name, json.dumps(parameters, allow_nan=False))
    assert parameters == original and result is None


def _auto(editor, layer, owner=None, **options):
    return _action(editor, layer, "auto_contrast", **({"document_id": owner.id} if owner is not None else {}), **options)


def _replay(editor, layer, record, owner=None, **options):
    return _action(editor, layer, "replay_auto_contrast", record=record,
                   **({"document_id": owner.id} if owner is not None else {}), **options)


def _wait(editor, layer):
    target = editor if layer == "wrapper" else editor._editor
    assert target.wait() is None
    assert not editor.busy and editor.job_info is None and editor.error == ""


def _wait_failure(editor, layer):
    if layer == "wrapper":
        with pytest.raises(RuntimeError) as error: editor.wait()
        assert str(error.value) and editor.error == ""  # Wrapper consumes the native error.
    else:
        assert editor._editor.wait() is None
        assert editor.error
        editor._editor.clear_error()
    assert not editor.busy and editor.job_info is None and editor.error == ""


def _ready(editor, owner):
    # Inspect the real retained result without poll/wait publication. No timing
    # race is used to force cancellation, revision changes or owner activation.
    deadline = time.monotonic()+5
    while time.monotonic() < deadline:
        info = editor.job_info
        assert info is not None and info.document_id == owner.id and info.id > 0
        assert info.resource_bytes > 0 and editor.busy
        assert info.state in (ij.JobState.QUEUED, ij.JobState.RUNNING, ij.JobState.READY)
        if info.state == ij.JobState.READY:
            assert any(row.id == info.id and row.document_id == owner.id for row in editor.jobs)
            return info.id
        time.sleep(.001)
    pytest.fail("Auto did not reach READY within5000ms")


def _job(editor, job_id, state):
    matches = [row for row in editor.jobs if row.id == job_id]
    assert len(matches) == 1 and matches[0].state == state


def _ranges(owner):
    return [channel["range"] for channel in shared._stored(owner)["channels"]]


def _selection_wire(selection):
    if selection is None: return None
    return {**copy.deepcopy(selection.attributes), "kind": selection.kind,
            "points": [list(point) for point in selection.points], "width": selection.width}


def _record(owner, resolved=(1., 2.), *, state=None, channels=(1,), previous=0, bits=0,
            scope="active_channel", roi="active_selection", selection=None,
            effective="whole_plane", count=100, minimum=1., maximum=4.,
            plane_minimum=None, plane_maximum=None, plane_count=None,
            origin=0., bin_size=1., outcome="histogram_bounds", indices=(1, 2), candidate=(1., 2.), applied=None):
    snapshot = owner.snapshot(); revisions = snapshot.revisions
    expected = copy.deepcopy(_state() if state is None else state)
    applied = list(resolved if applied is None else applied)
    for channel in channels: expected["channels"][channel]["range"] = applied
    effect = {"schema": "imagejxx.channel-display-command", "version": 2, "action": "set",
        "source": {"id": owner.id, "pixels_revision": revisions.pixels, "metadata_revision": revisions.metadata,
                   "shape": list(snapshot.dataset.shape), "type": snapshot.dataset.dtype.name,
                   "sample_policy": "preserve_quiet_nan_float32" if snapshot.dataset.sample_policy == ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 else "finite_only"},
        "position": {"t": snapshot.display.t, "c": snapshot.display.c, "z": snapshot.display.crosshair[2]},
        "state": expected, "reset": {"channels": [], "empty_finite": []}, "display_dependency": None, "navigation": None}
    used = 5000 if previous < 10 else previous//2
    return {"schema": "imagejxx.auto-contrast-command", "version": 1, "effect": effect,
        "roi": {"policy": roi, "effective": effective, "selection_revision": revisions.selection, "selection": _selection_wire(selection)},
        "display_dependency": {"revision": revisions.display}, "cycle": {"previous": previous, "used": used, "next": 0 if outcome == "reset_fallback" else used},
        "scope": scope, "unsigned16_bits": bits, "channels": list(channels),
        "histogram": {"domain": "raw", "binning": "processor_raw", "bins": 256, "origin": origin, "bin_size": bin_size,
            "minimum": minimum, "maximum": maximum, "visited_count": count, "finite_count": count, "nan_count": 0,
            "plane_minimum": minimum if plane_minimum is None else plane_minimum,
            "plane_maximum": maximum if plane_maximum is None else plane_maximum,
            "plane_finite_count": count if plane_count is None else plane_count, "plane_nan_count": 0},
        "decision": {"outcome": outcome, "indices": None if indices is None else list(indices),
            "candidate_range": None if candidate is None else list(candidate), "resolved_range": list(resolved), "applied_range": applied}}


def _request(previous=0, bits=0, *, scope="active_channel", roi="active_selection"):
    return {"previous_divisor": previous, "scope": scope, "roi": roi, "unsigned16_bits": bits}


def _constant_record(owner, bits, maximum, *, state=None):
    return _record(owner, (0., maximum) if bits else (7., 7.), state=state,
        channels=(0, 1, 2) if bits else (1,), bits=bits, minimum=7., maximum=7., origin=7., bin_size=1/256,
        outcome="reset_fallback", indices=None, candidate=None)


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_captured_nonzero_tcz_area_and_exact_record(editor, layer):
    selection = ij.Selection("rectangle", [(0, 0), (10, 2)], width=2,
                             attributes={"keep": {"exact": 2**64-1, "text": "area"}})
    owner = _owner(editor, selection=selection)
    editor.recording, editor.results = True, [{"retained": 7}]
    before, snapshot, view = shared._data(owner), owner.snapshot(), owner.snapshot().display
    expected = _record(owner, (1., 3.), selection=selection, effective="area", count=20,
        minimum=1., maximum=3., plane_minimum=1., plane_maximum=4., plane_count=100,
        outcome="area_extrema", indices=(1, 1), candidate=(1., 1.))
    _auto(editor, layer)  # Omitted document_id captures the active nonzeroTCZ.
    job_id = _ready(editor, owner)
    assert shared._data(owner) == before and editor.workflow == [] and _ranges(owner)[1] == [10., 109.]
    _wait(editor, layer); _job(editor, job_id, ij.JobState.COMPLETED)
    assert editor.workflow == [expected] and shared._stored(owner) == expected["effect"]["state"]
    assert owner.snapshot().revisions.display == snapshot.revisions.display+1
    assert owner.snapshot().shares_data(snapshot) and shared._data(owner) == before
    assert shared._navigation(owner.snapshot().display) == shared._navigation(view) and editor.results == [{"retained": 7}]
    shared._same_owner(editor, owner)


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_all_scope_and_native_color_gray_precision(editor, layer):
    owner = _owner(editor); editor.recording = True
    expected = _record(owner, channels=(0, 1, 2), scope="composite_all")
    _auto(editor, layer, owner, all_channels=True, use_selection=False); _wait(editor, layer)
    expected["roi"]["policy"] = "whole_plane"
    assert editor.workflow[-1] == expected and _ranges(owner) == [[1., 2.]]*3
    # The adopted logical Color/Gray endpoints stay fractional and active-only.
    for mode in ("color", "grayscale"):
        other = _owner(editor, [1000+257*i for i in range(16)], np.uint16, mode=mode, flat=True)
        before = shared._data(other)
        for previous in (0, 5000):
            state = _state(mode)
            if previous: state["channels"][1]["range"] = [1000., 4840.9375]
            expected = _record(other, (1000., 4840.9375), state=state, scope="composite_all", previous=previous,
                count=16, minimum=1000., maximum=4855., origin=1000., bin_size=15.0625,
                indices=(0, 255), candidate=(1000., 4840.9375))
            _auto(editor, layer, other, all_channels=True); _wait(editor, layer)
            assert editor.workflow[-1] == expected and shared._data(other) == before
            assert shared._stored(other)["mode"] == mode and _ranges(other) == [[0., 99.], [1000., 4840.9375], [20., 119.]]


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_configured_defaults_overrides_and_frozen_settings(editor, layer):
    editor.recording = True
    for text, bits, maximum in BITS:
        owner = _owner(editor, [7]*100, np.uint16)
        editor.update_settings(conversion_unsigned16_range=text, persist=False)
        assert editor.settings["conversion_unsigned16_range"] == text
        expected = _constant_record(owner, bits, maximum)
        _auto(editor, layer, owner); _ready(editor, owner)
        editor.update_settings(conversion_unsigned16_range="12" if text != "12" else "auto", persist=False)
        _wait(editor, layer)
        assert editor.workflow[-1] == expected and shared._stored(owner) == expected["effect"]["state"]
        # An explicit0 overrides any configured choice; the settings key is not rewritten.
        editor.update_settings(conversion_unsigned16_range="12", persist=False)
        state = expected["effect"]["state"]
        explicit = _constant_record(owner, 0, 7., state=state)
        _auto(editor, layer, owner, unsigned16_bits=0); _wait(editor, layer)
        assert editor.workflow[-1] == explicit and editor.settings["conversion_unsigned16_range"] == "12"
    owner = _owner(editor, [7]*100, np.uint16)
    editor.update_settings(conversion_unsigned16_range="auto", persist=False)
    expected = _constant_record(owner, 12, 4095.)
    _auto(editor, layer, owner, unsigned16_bits=12); _wait(editor, layer)
    assert editor.workflow[-1] == expected and editor.settings["conversion_unsigned16_range"] == "auto"


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_repeated_cycle_selection_numeric_set_and_reset(editor, layer):
    owner = _owner(editor, cycle=True); editor.recording = True
    stable = shared._data(owner)
    for previous, low in ((0, 10), (5000, 20)):
        _auto(editor, layer, owner); _wait(editor, layer)
        record = editor.workflow[-1]
        assert record["cycle"] == {"previous": previous, "used": 5000 if previous == 0 else 2500, "next": 5000 if previous == 0 else 2500}
        assert record["decision"]["resolved_range"] == [float(low), 50.] and shared._data(owner) == stable
    owner.select(ij.Selection("point", [(2, 3)], attributes={"provenance": True}))
    _action(editor, layer, "display_range", minimum=0., maximum=200., document_id=owner.id)
    _auto(editor, layer, owner); _wait(editor, layer)
    assert editor.workflow[-1]["cycle"] == {"previous": 2500, "used": 1250, "next": 1250}
    assert editor.workflow[-1]["roi"]["effective"] == "whole_nonarea" and _ranges(owner)[1] == [30., 50.]
    reset = {"action": "reset_range", "state": copy.deepcopy(shared._stored(owner)), "channels": [1]}
    _action(editor, layer, "channel_display", request=reset, document_id=owner.id)
    assert _ranges(owner)[1] == [0., 255.]
    _auto(editor, layer, owner); _wait(editor, layer)
    assert editor.workflow[-1]["cycle"] == {"previous": 0, "used": 5000, "next": 5000} and _ranges(owner)[1] == [10., 50.]


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_recording_is_captured_at_submission(editor, layer):
    owner = _owner(editor); before = shared._data(owner)
    editor.recording = True; expected = _record(owner)
    _auto(editor, layer, owner); _ready(editor, owner); editor.recording = False; _wait(editor, layer)
    assert editor.workflow == [expected]
    _auto(editor, layer, owner); _ready(editor, owner); editor.recording = True; _wait(editor, layer)
    assert editor.workflow == [expected] and shared._data(owner) == before
    _auto(editor, layer, owner); _wait(editor, layer)
    assert len(editor.workflow) == 2 and editor.workflow[-1]["cycle"]["previous"] == 2500


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_jobs_wait_and_both_cancellation_routes(editor, layer):
    owner = _owner(editor, cycle=True); editor.recording = True
    for route in ("cancel", "cancel_job"):
        before = shared._full(owner)
        _auto(editor, layer, owner); job_id = _ready(editor, owner)
        with pytest.raises(ValueError): _auto(editor, layer, owner)
        assert shared._full(owner) == before and editor.workflow == []
        target = editor if layer == "wrapper" else editor._editor
        if route == "cancel": assert target.cancel() is None
        else: assert target.cancel_job(job_id) is True
        _wait(editor, layer); _job(editor, job_id, ij.JobState.CANCELED)
        assert shared._full(owner) == before and editor.workflow == [] and target.cancel_job(job_id) is False
    _auto(editor, layer, owner); job_id = _ready(editor, owner)
    _action(editor, layer, "wait_jobs", timeout_ms=5000)
    assert not editor.busy and editor.error == "" and _ranges(owner)[1] == [10., 50.]
    _job(editor, job_id, ij.JobState.COMPLETED)
    assert editor.workflow[-1]["cycle"]["previous"] == 0


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_close_cancels_source_without_redirecting(editor, layer):
    other = _owner(editor); owner = _owner(editor)
    editor.recording, editor.results = True, [{"retained": 9}]
    before, unrelated = shared._full(owner), shared._full(other)
    _auto(editor, layer, owner); job_id = _ready(editor, owner)
    target = editor if layer == "wrapper" else editor._editor
    target.close_document(owner.id, discard=True); _wait(editor, layer)
    assert [row["id"] for row in editor.document_infos] == [other.id]
    assert shared._full(owner) == before and shared._full(other) == unrelated and editor.workflow == [] and editor.results == [{"retained": 9}]
    _job(editor, job_id, ij.JobState.CANCELED)
    with pytest.raises(IndexError): _auto(editor, layer, owner)


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_active_owner_and_away_back_guards(editor, layer):
    owner = _owner(editor, cycle=True); other = _owner(editor)
    editor.recording = True
    before = shared._collection(editor)
    with pytest.raises(ValueError): _auto(editor, layer, owner)
    assert shared._collection(editor) == before
    target = editor if layer == "wrapper" else editor._editor
    target.active_index = 0
    _auto(editor, layer, owner); job_id = _ready(editor, owner)
    pending = shared._collection(editor)
    source_data, unrelated = shared._data(owner), shared._full(other)
    with pytest.raises(ValueError, match="^Wait for the current edit$"):
        target.active_index = 1
    assert shared._collection(editor) == pending and editor.active_index == 0
    assert editor.document_infos[editor.active_index]["id"] == owner.id
    assert editor.busy and editor.job_info.id == job_id
    assert editor.job_info.document_id == owner.id and editor.job_info.state == ij.JobState.READY
    _job(editor, job_id, ij.JobState.READY)
    _wait(editor, layer); _job(editor, job_id, ij.JobState.COMPLETED)
    assert shared._data(owner) == source_data and shared._full(other) == unrelated
    assert len(editor.workflow) == 1 and editor.workflow[0]["cycle"]["previous"] == 0
    assert _ranges(owner)[1] == [10., 50.]
    first_record = copy.deepcopy(editor.workflow[0])
    idle = shared._collection(editor)
    # The public facade permits idle activation. In-flight activation
    # invalidation remains covered by native Application and U06 preparation.
    target.active_index = 1
    assert editor.document_infos[editor.active_index]["id"] == other.id
    target.active_index = 0
    assert shared._collection(editor) == idle and editor.document_infos[editor.active_index]["id"] == owner.id
    _auto(editor, layer, owner); _wait(editor, layer)
    assert len(editor.workflow) == 2 and editor.workflow[0] == first_record
    assert editor.workflow[-1]["cycle"] == {"previous": 0, "used": 5000, "next": 5000}
    assert _ranges(owner)[1] == [10., 50.] and shared._data(owner) == source_data
    assert shared._full(other) == unrelated


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_all_four_ready_stale_guards_are_atomic(editor, layer):
    editor.recording = True
    for change in ("pixels", "metadata", "selection", "display"):
        owner = _owner(editor); _auto(editor, layer, owner); job_id = _ready(editor, owner)
        if change == "pixels":
            edit = owner.begin_edit(); edit.replace_plane(np.full((10, 10), 7, np.uint8), t=1, c=1, z=1); owner.commit(edit)
        elif change == "metadata":
            edit = owner.begin_edit(); edit.set_title("Changed during Auto"); owner.commit(edit)
        elif change == "selection": owner.select(ij.Selection("point", [(2, 3)]))
        else:
            view = owner.snapshot().display; view.zoom += .5; owner.set_display(view)
        before, workflow = shared._full(owner), copy.deepcopy(editor.workflow)
        _wait_failure(editor, layer); _job(editor, job_id, ij.JobState.FAILED)
        assert shared._full(owner) == before and editor.workflow == workflow
        _auto(editor, layer, owner); _wait(editor, layer)
        assert editor.workflow[-1]["cycle"]["previous"] == 0


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_strict_replay_uses_frozen_effect_and_preserves_cycle(editor, layer):
    owner = _owner(editor, cycle=True); editor.recording = True
    _auto(editor, layer, owner); _wait(editor, layer); original = copy.deepcopy(editor.workflow[-1])
    view = owner.snapshot().display; view.t, view.c, view.crosshair, view.zoom = 0, 2, [5, 6, 0], 3.75
    view.properties = {**view.properties, "channel_display": _state("color")}; owner.set_display(view)
    owner.select(ij.Selection("point", [(3, 2)]))
    before, navigation = shared._data(owner), shared._navigation(owner.snapshot().display)
    editor.recording = False
    _replay(editor, layer, original, owner)
    assert not editor.busy and shared._stored(owner) == original["effect"]["state"]
    assert shared._data(owner) == before and shared._navigation(owner.snapshot().display) == navigation and editor.workflow == [original]
    # Strict replay does not seed the different TCZ context or consume Auto's cycle.
    view = owner.snapshot().display; view.t, view.c, view.crosshair = 1, 1, [2, 3, 1]; owner.set_display(view)
    _replay(editor, layer, original, owner)
    editor.recording = True; _auto(editor, layer, owner); _wait(editor, layer)
    assert editor.workflow[-1]["cycle"]["previous"] == 5000 and _ranges(owner)[1] == [20., 50.]


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_complete_rebind_uses_destination_and_effective_divisor(editor, layer):
    source = _owner(editor); editor.recording = True
    _auto(editor, layer, source); _wait(editor, layer); original = copy.deepcopy(editor.workflow[-1])
    destination = _owner(editor, [7]*100, np.uint16)
    before = shared._collection(editor)
    with pytest.raises(ValueError): _replay(editor, layer, original, destination)
    assert shared._collection(editor) == before
    expected = _constant_record(destination, 12, 4095.)
    _replay(editor, layer, original, destination, rebind_request=_request(bits=12)); _wait(editor, layer)
    assert editor.workflow[-1] == expected and editor.workflow[0] == original and _ranges(destination) == [[0., 4095.]]*3
    before = shared._collection(editor)
    with pytest.raises(ij.RevisionConflict): _replay(editor, layer, original, destination, rebind_request=_request(previous=5000, bits=10))
    assert shared._collection(editor) == before
    # Rebinding a nonfallback owner must supply its effective5000 divisor.
    editor.active_index = 0
    _auto(editor, layer, source); _wait(editor, layer)
    before = shared._collection(editor)
    with pytest.raises(ij.RevisionConflict): _replay(editor, layer, original, source, rebind_request=_request())
    assert shared._collection(editor) == before
    _replay(editor, layer, original, source, rebind_request=_request(previous=5000)); _wait(editor, layer)
    assert editor.workflow[-1]["cycle"] == {"previous": 5000, "used": 2500, "next": 2500}


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_exact_parameter_and_rebind_admission(editor, layer):
    owner = _owner(editor); editor.recording = True
    invalid = ({"all_channels": 1}, {"all_channels": None}, {"use_selection": 0}, {"use_selection": "true"},
        {"unsigned16_bits": True}, {"unsigned16_bits": 12.}, {"unsigned16_bits": -1}, {"unsigned16_bits": 9},
        {"unsigned16_bits": 17}, {"unknown": None}, {"previous_divisor": 5000})
    for options in invalid:
        before = shared._collection(editor)
        with pytest.raises(ERRORS): _auto(editor, layer, owner, **options)
        assert shared._collection(editor) == before and editor.job_info is None
    _auto(editor, layer, owner); _wait(editor, layer); record = copy.deepcopy(editor.workflow[-1])
    invalid_requests = (None, True, {}, {"previous_divisor": 5000},
        {**_request(5000), "extra": 0}, {**_request(5000), "previous_divisor": True},
        {**_request(5000), "previous_divisor": 17}, {**_request(5000), "unsigned16_bits": False},
        {**_request(5000), "unsigned16_bits": 12.}, {**_request(5000), "scope": "all"},
        {**_request(5000), "roi": "selection"})
    for request in invalid_requests:
        before = shared._collection(editor)
        with pytest.raises(ERRORS): _replay(editor, layer, record, owner, rebind_request=request)
        assert shared._collection(editor) == before and editor.job_info is None


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_malformed_original_precedes_closed_owner_and_rebind(editor, layer):
    owner = _owner(editor); editor.recording = True
    _auto(editor, layer, owner); _wait(editor, layer); record = copy.deepcopy(editor.workflow[-1])
    editor.close_document(owner.id, discard=True)
    for corruption, diagnostic in (("outer", "record fields"), ("effect", "exact nonnegative integer"),
            ("cycle", "cycle transition"), ("histogram", "histogram counts"), ("selection", "ROI adaptation")):
        malformed = copy.deepcopy(record)
        if corruption == "outer": malformed["extra"] = None
        elif corruption == "effect": malformed["effect"]["source"]["pixels_revision"] = True
        elif corruption == "cycle": malformed["cycle"]["used"] = 9
        elif corruption == "histogram": malformed["histogram"]["visited_count"] = 0
        else: malformed["roi"]["effective"] = "area"
        before = shared._collection(editor)
        # ValueError proves original validation precedes closed-ID lookup;
        # IndexError would mean an owner was consulted too early.
        with pytest.raises(ERRORS, match=diagnostic): _replay(editor, layer, malformed, owner, rebind_request={})
        assert shared._collection(editor) == before
    with pytest.raises(IndexError): _replay(editor, layer, record, owner)


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_stale_source_and_foreign_replay_are_atomic(editor, layer):
    editor.recording = True
    for change in ("pixels", "metadata", "foreign"):
        owner = _owner(editor); _auto(editor, layer, owner); _wait(editor, layer)
        record = copy.deepcopy(editor.workflow[-1]); destination = owner
        if change == "foreign": destination = _owner(editor)
        else:
            edit = owner.begin_edit()
            if change == "pixels": edit.replace_plane(np.full((10, 10), 7, np.uint8), t=1, c=1, z=1)
            else: edit.set_property("changed", True)
            owner.commit(edit)
        before = shared._collection(editor)
        with pytest.raises(ValueError if change == "foreign" else ij.RevisionConflict): _replay(editor, layer, record, destination)
        assert shared._collection(editor) == before


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_unsupported_empty_and_no_finite_refusals(editor, layer):
    for dtype, samples in ((np.uint16, 1), (np.float64, 1), (np.uint8, 3), (np.float32, 4)):
        owner = _owner(editor, dtype=dtype, samples=samples, enabled=False)
        before = shared._collection(editor)
        with pytest.raises(ValueError): _auto(editor, layer, owner)
        assert shared._collection(editor) == before and editor.job_info is None
    editor.recording = True
    for kind in ("empty", "no_finite"):
        selection = ij.Selection.from_mask(np.zeros((10, 10), np.uint8)) if kind == "empty" else ij.Selection("rectangle", [(0, 0), (10, 10)])
        owner = _owner(editor, np.asarray([0x7fc12345]*100, np.uint32).view(np.float32) if kind == "no_finite" else VALUES,
            np.float32 if kind == "no_finite" else np.uint8, quiet=kind == "no_finite", selection=selection)
        before = shared._full(owner); workflow = copy.deepcopy(editor.workflow)
        _auto(editor, layer, owner); _wait_failure(editor, layer)
        assert shared._full(owner) == before and editor.workflow == workflow
    # A real, nonempty area can be explicitly ignored; no fallback to a
    # different ROI policy occurs on an empty-area submission.
    owner = _owner(editor, selection=ij.Selection("rectangle", [(0, 0), (10, 2)]))
    _auto(editor, layer, owner, use_selection=False); _wait(editor, layer)
    assert _ranges(owner)[1] == [1., 2.] and editor.workflow[-1]["roi"]["selection"] is None


@pytest.mark.parametrize("layer", LAYERS)
def test_auto_native_workspace_reopen_retains_records_and_resets_cycle(editor, layer, tmp_path):
    owner = _owner(editor, cycle=True); editor.recording = True; editor.results = [{"retained": 13}]
    _auto(editor, layer, owner); _wait(editor, layer)
    record = copy.deepcopy(editor.workflow[-1]); stored = copy.deepcopy(shared._stored(owner)); before = shared._data(owner)
    path = tmp_path/"auto-workspace.ijx"
    _action(editor, layer, "save_workspace", path=str(path))
    assert path.read_bytes()[:10] == b"IMAGEJXX3\n" and shared._data(owner) == before
    target = editor if layer == "wrapper" else editor._editor
    target.open_file(str(path))
    reopened = editor._editor.native_document(next(row["id"] for row in editor.document_infos if row["id"] != owner.id))
    assert reopened.id != owner.id and shared._stored(reopened) == stored and editor.workflow == [record]
    old, new = owner.snapshot(), reopened.snapshot()
    assert new.dataset.numpy("TCZYXS").tobytes() == old.dataset.numpy("TCZYXS").tobytes()
    assert new.metadata == old.metadata and new.record_layout == old.record_layout and new.overlays == old.overlays
    assert shared._selection(new.selection) == shared._selection(old.selection) and shared._selection(new.previous_selection) == shared._selection(old.previous_selection)
    assert not new.dirty and (reopened.history().undo_steps, reopened.history().redo_steps) == (0, 0)
    assert editor.results == [{"retained": 13}]
    before = shared._collection(editor)
    with pytest.raises(ValueError): _replay(editor, layer, record, reopened)
    assert shared._collection(editor) == before
    _auto(editor, layer, reopened); _wait(editor, layer)
    assert editor.workflow[-1]["cycle"]["previous"] == 0 and _ranges(reopened)[1] == [10., 50.]
    assert editor.workflow[0] == record and record["effect"]["source"]["id"] == owner.id
