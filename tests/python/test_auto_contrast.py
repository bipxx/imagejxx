"""Installed native raw Auto commands and typed Application jobs; no GUI credit.

Expected ranges, cycles and counts are independent literals. Python performs no
product processing. Frontend argument/getter copies are outside core estimates.
"""
import ctypes
import gc
import json
import re
import sys
import threading
import weakref

import imagejxx as ij
from imagejxx import _native
import numpy as np
import pytest

ERRORS = (ValueError, TypeError, OverflowError, IndexError)
EXPECTED_COLLECTION_COUNT = 207
VALUES = np.repeat([1, 2, 3, 4], [1, 10, 11, 78])
CYCLE_VALUES = np.repeat([10, 20, 30, 40, 50, 60], [1, 3, 200, 200, 200, 3492])
VERSIONS = ((1, "sum"), (2, "sum"), (2, "max"), (2, "min"))
MODES = ("composite", "color", "grayscale")
BITS = ((0, 7), (8, 255), (10, 1023), (12, 4095), (14, 16383), (15, 32767), (16, 65535))
TRANSITIONS = ((0, 5000), (5000, 2500), (2500, 1250), (1250, 625), (625, 312),
               (312, 156), (156, 78), (78, 39), (39, 19), (19, 9), (9, 5000))
FIELDS = (("request", "previous_divisor"), ("request", "unsigned16_bits"),
          ("options", "unsigned16_bits"), ("limits", "max_pixels"),
          ("limits", "max_working_bytes"), ("limits", "max_record_bytes"),
          ("limits", "max_selection_vertices"))
LITERAL_ROWS = (
    (np.uint8, VALUES, 0, [1., 2.], "HistogramBounds", [1, 2], [1., 2.], 5000),
    (np.uint8, [7]*100, 0, [0., 255.], "ResetFallback", None, None, 0),
    (np.uint8, [10]*8+[200]*92, 0, [10., 200.], "AreaExtrema", [10, 10], [10., 10.], 5000),
    (np.uint16, [7]*100, 0, [7., 7.], "ResetFallback", None, None, 0),
    (np.uint16, [7]*100, 12, [0., 4095.], "ResetFallback", None, None, 0),
    (np.uint16, [1000+257*i for i in range(16)], 0, [1000., 4840.9375], "HistogramBounds", [0, 255], [1000., 4840.9375], 5000),
    (np.float32, [-2.5+.5*i for i in range(16)], 0, [-2.5, 4.970703125], "HistogramBounds", [0, 255], [-2.5, 4.970703125], 5000),
    (np.float32, [7]*100, 0, [7., 7.], "ResetFallback", None, None, 0),
)


def _emit_auto_observation(case_id, observations, artifacts=None):
    row = {"schema_version": 1, "context": "python_api", "case_id": case_id,
           "observations": observations, "artifacts": artifacts or {}}
    encoded = json.dumps(row, allow_nan=False, separators=(",", ":"))
    line = "AUTO_OBSERVATION_JSON " + encoded
    assert len(line.encode("utf-8"))+1 <= 64*1024
    print(line, flush=True)


def _auto_setting_observations(label, setting, current, command):
    return {label+".setting": setting, label+".current": current,
            label+".effective": command.request.unsigned16_bits,
            label+".range_low": command.resolved_range[0],
            label+".range_high": command.resolved_range[1],
            label+".cycle_next": command.cycle.next}


def _state(channels=1, mode="composite", version=1, projection="sum"):
    value = {"schema": "imagejxx.channel-display", "version": version, "mode": mode,
        "channels": [{"visible": c % 2 == 0, "range": [10.*c, 99.+10*c],
                      "lut": {"name": ("Red", "Green", "Blue")[c % 3]}} for c in range(channels)]}
    if version == 2: value["projection"] = projection
    return value


def _owner(values=VALUES, dtype=np.uint8, channels=1, mode="composite", version=1,
           projection="sum", tcz=False, quiet=False):
    values = np.asarray(values, dtype=dtype)
    shape = (2 if tcz else 1, channels, 2 if tcz else 1, 1, len(values), 1)
    pixels = np.broadcast_to(values[None, None, None, None, :, None], shape).copy()
    calibration = ij.SpatialCalibration(spacing=(.5, 1.25, 2.5), origin=(11, -7, 19),
        unit="um", frame_interval=.75, time_origin=2.25, time_unit="s")
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if quiet else ij.FloatingSamplePolicy.FINITE_ONLY
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS", calibration=calibration, sample_policy=policy), "Auto literal")
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = int(tcz), min(1, channels-1), [0, 0, int(tcz)]
    view.zoom, view.pan = 1.25, [[1., 2.], [3., 4.], [5., 6.]]
    view.properties = {"channel_display": _state(channels, mode, version, projection), "retained": {"exact": 2**64-1}}
    owner.set_display(view)
    return pixels, owner


def _request(previous=0, bits=0, all_channels=False, whole=False):
    value = ij.AutoContrastRequest()
    value.previous_divisor, value.unsigned16_bits = previous, bits
    value.scope = ij.AutoContrastScope.CompositeAll if all_channels else ij.AutoContrastScope.ActiveChannel
    value.roi = ij.AutoContrastRoiPolicy.WholePlane if whole else ij.AutoContrastRoiPolicy.ActiveSelection
    return value


def _options(bits=0, all_channels=False, whole=False):
    value = ij.AutoContrastOptions()
    value.unsigned16_bits = bits
    value.scope = ij.AutoContrastScope.CompositeAll if all_channels else ij.AutoContrastScope.ActiveChannel
    value.roi = ij.AutoContrastRoiPolicy.WholePlane if whole else ij.AutoContrastRoiPolicy.ActiveSelection
    return value


def _resolve(owner, request=None, **kwargs):
    return ij.resolve_auto_contrast_command(owner.snapshot(), request or _request(), **kwargs)


def _stable(owner):
    snap, history = owner.snapshot(), owner.history()
    cal = snap.dataset.calibration
    return (snap.data_identity, snap.dirty, snap.metadata, snap.selection, snap.previous_selection,
        snap.overlays, [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snap.rois],
        snap.dataset.numpy("TCZYXS").tobytes(), (snap.revisions.pixels, snap.revisions.metadata, snap.revisions.selection),
        (history.undo_steps, history.redo_steps), tuple(getattr(cal, k) for k in
            ("spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit")))


def _full(owner):
    view = owner.snapshot().display
    return _stable(owner), (view.t, view.c, view.crosshair, view.zoom, view.pan, view.properties, view.revision)


def _assert(command, resolved, outcome="HistogramBounds", next_divisor=5000, indices=None, candidate=None):
    assert list(command.resolved_range) == resolved and command.outcome == getattr(ij.AutoContrastOutcome, outcome)
    assert command.cycle.next == next_divisor and command.indices == indices and command.candidate_range == candidate
    assert command.effect.action == "set" and command.record()["effect"] == command.effect.record()
    assert command.record()["decision"]["resolved_range"] == resolved
    for c in command.channels: assert command.effect.state["channels"][c]["range"] == command.applied_range
    ij.validate_auto_contrast_command_record(command.record())


def _app(owner):
    app = ij.Application()
    assert app.adopt(owner) == owner.id
    return app


def _ready(app, options=None, **kwargs):
    job = app.submit_auto_contrast(options or _options(), **kwargs)
    assert job.wait(5000) and job.ready and job.info.state == ij.JobState.READY
    return job


def test_native_exports_defaults_and_closed_result_constructors():
    names = ("AutoContrastScope", "AutoContrastRoiPolicy", "AutoContrastOutcome", "AutoContrastRequest",
        "AutoContrastOptions", "AutoContrastLimits", "AutoContrastCycle", "AutoContrastHistogramSummary",
        "AutoContrastEstimate", "ResolvedAutoContrastCommand", "AutoContrastJob", "estimate_auto_contrast_command",
        "resolve_auto_contrast_command", "validate_auto_contrast_command_record", "auto_contrast_command_from_record",
        "rebind_auto_contrast_command")
    for name in names: assert getattr(ij, name) is getattr(_native, name)
    request, options, limits = ij.AutoContrastRequest(), ij.AutoContrastOptions(), ij.AutoContrastLimits()
    assert request.previous_divisor == request.unsigned16_bits == options.unsigned16_bits == 0
    assert request.scope == options.scope == ij.AutoContrastScope.ActiveChannel
    assert request.roi == options.roi == ij.AutoContrastRoiPolicy.ActiveSelection
    assert (limits.max_pixels, limits.max_working_bytes, limits.max_record_bytes, limits.max_selection_vertices) == (16777216, 134217728, 16777216, 65536)
    for name in ("ResolvedAutoContrastCommand", "AutoContrastJob", "AutoContrastCycle"):
        with pytest.raises(TypeError): getattr(ij, name)()


@pytest.mark.parametrize("dtype,values,bits,resolved,outcome,indices,candidate,next_divisor", LITERAL_ROWS)
def test_literal_histogram_equal_and_fallback(dtype, values, bits, resolved, outcome, indices, candidate, next_divisor):
    pixels, owner = _owner(values, dtype)
    before = _full(owner)
    command = _resolve(owner, _request(bits=bits))
    _assert(command, resolved, outcome, next_divisor, indices, candidate)
    assert command.requires_captured_state and command.source.id == owner.id
    assert command.histogram.finite_count == command.histogram.visited_count == len(values)
    assert command.histogram.nan_count == 0 and command.estimate.histogram.pixel_count == len(values)
    assert owner.snapshot().dataset.numpy("TCZYXS").tobytes() == pixels.tobytes() and _full(owner) == before


@pytest.mark.parametrize("previous,used,low", ((0, 5000, 10), (5000, 2500, 20), (2500, 1250, 30)))
def test_strict_count_threshold_cycle(previous, used, low):
    _, owner = _owner(CYCLE_VALUES)
    command = _resolve(owner, _request(previous))
    _assert(command, [float(low), 50.], indices=[low, 50], candidate=[float(low), 50.], next_divisor=used)
    assert command.cycle.previous == previous and command.cycle.used == used


@pytest.mark.parametrize("previous,used", TRANSITIONS)
def test_all_legal_divisors(previous, used):
    _, owner = _owner()
    command = _resolve(owner, _request(previous))
    assert (command.cycle.previous, command.cycle.used, command.cycle.next) == (previous, used, 0 if used == 9 else used)
    if used == 9: assert command.outcome == ij.AutoContrastOutcome.ResetFallback


@pytest.mark.parametrize("bits,maximum", BITS)
@pytest.mark.parametrize("mode", MODES)
def test_all_configured_unsigned16_choices_and_all_c_fallback(bits, maximum, mode):
    _, owner = _owner([7]*100, np.uint16, channels=3, mode=mode, tcz=True)
    command = _resolve(owner, _request(bits=bits))
    _assert(command, [0. if bits else 7., float(maximum)], "ResetFallback", 0)
    assert command.channels == ([0, 1, 2] if bits else [1])
    assert command.effect.state["mode"] == mode and command.histogram.origin == 7 and command.histogram.bin_size == 1/256


@pytest.mark.parametrize("version,projection", VERSIONS)
@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("all_channels", (False, True))
def test_getter_scope_preserves_versions_projections_and_other_channels(version, projection, mode, all_channels):
    _, owner = _owner([1000+257*i for i in range(16)], np.uint16, channels=3, mode=mode,
        version=version, projection=projection, tcz=True)
    old = owner.snapshot().display.properties["channel_display"]
    command = _resolve(owner, _request(all_channels=all_channels))
    _assert(command, [1000., 4840.9375], indices=[0, 255], candidate=[1000., 4840.9375])
    propagated = all_channels and mode == "composite"
    assert command.channels == ([0, 1, 2] if propagated else [1])
    assert command.applied_range == [1000., 4841. if propagated else 4840.9375]
    state = command.effect.state
    assert state["version"] == command.effect.version == version and state["mode"] == mode
    assert state.get("projection", "sum") == projection
    for c, channel in enumerate(state["channels"]):
        assert channel["visible"] == old["channels"][c]["visible"] and channel["lut"] == old["channels"][c]["lut"]
        if c not in command.channels: assert channel == old["channels"][c]
    assert (command.position.t, command.position.c, command.position.z) == (1, 1, 1)


@pytest.mark.parametrize("profile", ("singleton", "color9", "float_getter"))
def test_singleton_large_c_and_float_getter(profile):
    if profile == "float_getter":
        _, owner = _owner([16777216+2*i for i in range(16)], np.float32, channels=3)
        command = _resolve(owner, _request(all_channels=True))
        assert command.resolved_range == [16777216., 16777245.8828125] and command.applied_range == [16777216., 16777246.]
    else:
        _, owner = _owner([1000+257*i for i in range(16)], np.uint16,
            channels=9 if profile == "color9" else 1, mode="color" if profile == "color9" else "composite")
        command = _resolve(owner, _request(all_channels=True))
        assert command.channels == ([1] if profile == "color9" else [0])
        assert command.applied_range == [1000., 4840.9375 if profile == "color9" else 4841.]


@pytest.mark.parametrize("kind", ("mask", "rectangle", "line", "polyline", "angle", "point", "multipoint", "whole"))
def test_roi_adaptation_and_same_plane_fallback(kind):
    _, owner = _owner([1, 7, 7, 7, 7, 7, 7, 99], np.uint16)
    if kind in ("mask", "whole"):
        selection = ij.Selection.from_mask(np.asarray([[0, 1, 1, 1, 1, 1, 1, 0]], np.uint8))
    elif kind == "rectangle": selection = ij.Selection(kind, [(1, 0), (7, 1)])
    else: selection = ij.Selection(kind, [(0, 0), (1, 0), (2, 0)])
    selection = selection.with_attributes({"kept": {"exact": 2**64-1}})
    owner.select(selection)
    command = _resolve(owner, _request(whole=kind == "whole"))
    _assert(command, [1., 99.], "ResetFallback", 0)
    area = kind in ("mask", "rectangle")
    assert command.histogram.visited_count == (6 if area else 8)
    assert (command.histogram.minimum, command.histogram.maximum) == ((7., 7.) if area else (1., 99.))
    assert (command.histogram.plane_minimum, command.histogram.plane_maximum) == (1., 99.)
    assert command.selection_provenance == (None if kind == "whole" else selection)
    assert command.selection_dependency_revision == owner.snapshot().revisions.selection


@pytest.mark.parametrize("kind", ("empty_mask", "all_nan"))
def test_no_finite_area_refuses_without_fallback(kind):
    if kind == "empty_mask":
        _, owner = _owner([1, 7, 9, 99], np.uint16)
        owner.select(ij.Selection.from_mask(np.zeros((1, 4), np.uint8)))
    else:
        values = np.asarray([0x7FC12345, 0xFFC54321], np.uint32).view(np.float32)
        _, owner = _owner(values, np.float32, quiet=True)
    before = _full(owner)
    with pytest.raises(ValueError): _resolve(owner, _request(bits=12))
    assert _full(owner) == before


def test_quiet_nan_payloads_finite_counts_and_raw_metadata_domain():
    values = np.asarray([-2.5+.5*i for i in range(16)], np.float32)
    values = np.concatenate((values.view(np.uint32), np.asarray([0x7FC12345, 0xFFC54321], np.uint32))).view(np.float32)
    pixels, owner = _owner(values, np.float32, quiet=True)
    edit = owner.begin_edit(); edit.set_property("density", {"signed16": True, "formula": "opaque"}); owner.commit(edit)
    command = _resolve(owner)
    _assert(command, [-2.5, 4.970703125], indices=[0, 255], candidate=[-2.5, 4.970703125])
    assert (command.histogram.visited_count, command.histogram.finite_count, command.histogram.nan_count) == (18, 16, 2)
    assert owner.snapshot().dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()


def test_detached_immutable_getters_and_record():
    _, owner = _owner()
    owner.select(ij.Selection("rectangle", [(0, 0), (100, 1)], attributes={"nested": [1, 2]}))
    command = _resolve(owner)
    record = command.record(); record["decision"]["resolved_range"][0] = -99
    record["effect"]["state"]["channels"].clear()
    request = command.request; request.previous_divisor = 5000
    command.channels.clear(); command.resolved_range.clear(); command.indices.clear()
    attributes = command.selection_provenance.attributes; attributes["nested"].clear()
    state = command.effect.state; state["channels"].clear()
    assert command.request.previous_divisor == 0 and command.channels == [0] and command.resolved_range == [1., 2.]
    assert command.indices == [1, 2] and command.selection_provenance.attributes == {"nested": [1, 2]}
    assert command.effect.state["channels"] and command.record()["decision"]["resolved_range"] == [1., 2.]
    for target, field in ((command.cycle, "next"), (command.histogram, "finite_count"), (command.estimate, "working_bytes")):
        with pytest.raises(AttributeError): setattr(target, field, 0)


@pytest.mark.parametrize("target,field", FIELDS)
@pytest.mark.parametrize("value", (True, False, np.bool_(True), 1., 1.5, 1 << 100))
def test_integer_setters_reject_bool_lossy_or_overflow(target, field, value):
    obj = {"request": ij.AutoContrastRequest, "options": ij.AutoContrastOptions, "limits": ij.AutoContrastLimits}[target]()
    before = getattr(obj, field)
    with pytest.raises(ERRORS): setattr(obj, field, value)
    assert getattr(obj, field) == before


@pytest.mark.parametrize("target,field", FIELDS)
def test_integer_setters_accept_numpy_index_values(target, field):
    obj = {"request": ij.AutoContrastRequest, "options": ij.AutoContrastOptions, "limits": ij.AutoContrastLimits}[target]()
    setattr(obj, field, np.int64(17)); assert getattr(obj, field) == 17


@pytest.mark.parametrize("previous,bits", ((-1, 0), (1, 0), (10, 0), (4999, 0), (5001, 0),
    (0, -1), (0, 1), (0, 9), (0, 11), (0, 13), (0, 17)))
def test_invalid_requests_refuse_before_callbacks(previous, bits):
    _, owner = _owner(); calls = []
    with pytest.raises(ValueError): _resolve(owner, _request(previous, bits), checkpoint=lambda: calls.append(True))
    assert calls == []


@pytest.mark.parametrize("mutation", ("missing", "extra", "version_bool", "version_float", "bits_bool", "bits_invalid",
    "source_id_float", "source_revision_bool", "hist_count_bool", "hist_count_overflow", "hist_domain", "cycle",
    "impossible_cycle", "candidate", "applied", "channels_empty", "channels_duplicate", "effect_action", "roi_policy", "deep"))
def test_complete_record_refusals_precede_replay_and_rebind(mutation):
    _, owner = _owner(); command = _resolve(owner); record = command.record()
    if mutation == "missing": del record["schema"]
    elif mutation == "extra": record["extra"] = True
    elif mutation == "version_bool": record["version"] = True
    elif mutation == "version_float": record["version"] = 1.
    elif mutation == "bits_bool": record["unsigned16_bits"] = True
    elif mutation == "bits_invalid": record["unsigned16_bits"] = 13
    elif mutation == "source_id_float": record["effect"]["source"]["id"] = 1.
    elif mutation == "source_revision_bool": record["effect"]["source"]["pixels_revision"] = True
    elif mutation == "hist_count_bool": record["histogram"]["finite_count"] = True
    elif mutation == "hist_count_overflow": record["histogram"]["plane_nan_count"] = 2**64-1
    elif mutation == "hist_domain": record["histogram"]["domain"] = "calibrated"
    elif mutation == "cycle": record["cycle"]["used"] = 2500
    elif mutation == "impossible_cycle": record["cycle"] = {"previous": 19, "used": 9, "next": 9}
    elif mutation == "candidate": record["decision"]["candidate_range"] = [1., 3.]
    elif mutation == "applied": record["decision"]["applied_range"] = [1., 3.]
    elif mutation == "channels_empty": record["channels"] = []
    elif mutation == "channels_duplicate": record["channels"] = [0, 0]
    elif mutation == "effect_action": record["effect"]["action"] = "disable"
    elif mutation == "roi_policy": record["roi"]["policy"] = "unknown"
    else:
        tree = True
        for _ in range(130): tree = [tree]
        record["deep"] = tree
    _, foreign = _owner([7]*100, np.uint16)
    before, calls = _full(foreign), []
    with pytest.raises(ValueError) as error: ij.validate_auto_contrast_command_record(record)
    pattern = re.escape(str(error.value))
    with pytest.raises(ValueError, match=pattern): ij.auto_contrast_command_from_record(foreign.snapshot(), record)
    with pytest.raises(ValueError, match=pattern):
        ij.rebind_auto_contrast_command(foreign.snapshot(), record, object(), checkpoint=lambda: calls.append(True))
    app = _app(foreign)
    with pytest.raises(ValueError, match=pattern): app.submit_rebound_auto_contrast(record, object())
    assert _full(foreign) == before and calls == [] and app.auto_contrast_divisor() == 0


@pytest.mark.parametrize("change", ("pixels", "metadata", "selection", "display"))
def test_fresh_all_four_revisions_and_replay_historical_dependencies(change):
    _, owner = _owner(channels=3, tcz=True, version=2, projection="min")
    command = _resolve(owner); record = command.record(); historical = owner.snapshot().revisions
    if change == "pixels":
        edit = owner.begin_edit(); edit.replace_plane(np.full((1, 100), 9, np.uint8), t=1, c=1, z=1); owner.commit(edit)
    elif change == "metadata":
        edit = owner.begin_edit(); edit.set_title("changed"); owner.commit(edit)
    elif change == "selection": owner.select(ij.Selection("point", [(0, 0)]))
    else:
        view = owner.snapshot().display; view.t, view.c, view.crosshair, view.zoom = 0, 2, [0, 0, 0], 3.; owner.set_display(view)
    current, before = owner.snapshot(), _full(owner)
    with pytest.raises(ij.RevisionConflict): ij.command_display_state(command, current)
    if change in ("pixels", "metadata"):
        with pytest.raises(ij.RevisionConflict): ij.auto_contrast_command_from_record(current, record)
    else:
        replay = ij.auto_contrast_command_from_record(current, record)
        view = ij.command_display_state(replay, current)
        assert not replay.requires_captured_state and replay.record() == record
        assert replay.selection_dependency_revision == historical.selection and replay.display_dependency_revision == historical.display
        assert (replay.position.t, replay.position.c, replay.position.z) == (1, 1, 1)
        assert (view.t, view.c, view.crosshair, view.zoom) == (current.display.t, current.display.c, current.display.crosshair, current.display.zoom)
    assert _full(owner) == before


def test_full_rebind_uses_new_settings_cycle_state_and_owner():
    _, old = _owner(); record = _resolve(old).record()
    _, owner = _owner([7]*100, np.uint16, channels=3, mode="color", version=2, tcz=True)
    command = ij.rebind_auto_contrast_command(owner.snapshot(), record, _request(5000, 10, True, True))
    _assert(command, [0., 1023.], "ResetFallback", 0)
    assert command.source.id == owner.id and command.request.previous_divisor == 5000 and command.cycle.used == 2500
    assert command.channels == [0, 1, 2] and command.effect.version == 2 and command.effect.state["mode"] == "color"
    assert (command.position.t, command.position.c, command.position.z) == (1, 1, 1)
    assert record["effect"]["source"]["id"] == old.id
    with pytest.raises(ValueError): ij.auto_contrast_command_from_record(owner.snapshot(), record)
    with pytest.raises(ValueError): ij.rebind_auto_contrast_command(owner.snapshot(), record, _request(previous=-1))


def test_valid_original_then_wrong_typed_replacement_refuses():
    _, owner = _owner(); record = _resolve(owner).record(); app = _app(owner)
    with pytest.raises(TypeError): ij.rebind_auto_contrast_command(owner.snapshot(), record, object())
    with pytest.raises(TypeError): app.submit_rebound_auto_contrast(record, object())
    assert app.auto_contrast_divisor() == 0 and not app.job_scheduler.jobs


def test_existing_unsigned16_preference_reopen_capture_and_explicit_override(tmp_path, request):
    case_id = request.node.name
    observations = {}
    preferences = ij.PreferencesDocument()
    _, owner = _owner([7]*100, np.uint16)
    default_setting = preferences.settings["conversion_unsigned16_range"]
    default = _resolve(owner, _request(bits=preferences.settings.conversion_options().unsigned16_bits))
    assert default.request.unsigned16_bits == 0 and default.resolved_range == [7., 7.]
    assert default.outcome == ij.AutoContrastOutcome.ResetFallback and default.cycle.next == 0
    observations.update(_auto_setting_observations("default", default_setting,
        preferences.settings["conversion_unsigned16_range"], default))
    preferences.settings["conversion_unsigned16_range"] = "12"
    changed_setting = preferences.settings["conversion_unsigned16_range"]
    changed = _resolve(owner, _request(bits=preferences.settings.conversion_options().unsigned16_bits))
    assert changed.request.unsigned16_bits == 12 and changed.resolved_range == [0., 4095.]
    assert preferences.settings.conversion_options().unsigned16_bits == 12
    observations.update(_auto_setting_observations("changed", changed_setting,
        preferences.settings["conversion_unsigned16_range"], changed))
    path = tmp_path / "preferences.json"
    assert preferences.save(str(path))
    saved = path.read_bytes()
    reopened = ij.PreferencesDocument.load(str(path), False)
    assert saved == preferences.json().encode("utf-8") == reopened.json().encode("utf-8")
    request = _request(bits=reopened.settings.conversion_options().unsigned16_bits)
    persisted_setting = reopened.settings["conversion_unsigned16_range"]
    persisted = _resolve(owner, request)
    assert persisted.request.unsigned16_bits == 12 and persisted.resolved_range == [0., 4095.]
    assert reopened.settings.conversion_options().unsigned16_bits == 12
    observations.update(_auto_setting_observations("persisted", persisted_setting,
        reopened.settings["conversion_unsigned16_range"], persisted))
    observations["persisted.reopened_setting"] = reopened.settings["conversion_unsigned16_range"]
    entered = False
    def checkpoint():
        nonlocal entered
        if not entered:
            entered = True
            preferences.settings["conversion_unsigned16_range"] = "auto"
            request.unsigned16_bits = 0
    captured = _resolve(owner, request, checkpoint=checkpoint)
    assert entered and captured.request.unsigned16_bits == 12 and captured.resolved_range == [0., 4095.]
    assert preferences.settings.conversion_options().unsigned16_bits == 0
    observations.update(_auto_setting_observations("frozen", persisted_setting,
        preferences.settings["conversion_unsigned16_range"], captured))
    app = _app(owner)
    explicit_options = _options(bits=10)
    explicit = _ready(app, explicit_options)
    assert (overridden := app.publish_auto_contrast(explicit)).resolved_range == [0., 1023.]
    observations.update(_auto_setting_observations("override", persisted_setting,
        preferences.settings["conversion_unsigned16_range"], overridden))
    observations["override.explicit"] = explicit_options.unsigned16_bits
    _emit_auto_observation(case_id, observations, {"persisted_file": str(path)})


@pytest.mark.parametrize("cap", ("max_pixels", "max_working_bytes", "max_record_bytes", "max_selection_vertices"))
def test_positive_limits_admission_and_no_callbacks(cap):
    _, owner = _owner(); limits = ij.AutoContrastLimits(); setattr(limits, cap, 0); calls = []
    with pytest.raises(ValueError): _resolve(owner, limits=limits, checkpoint=lambda: calls.append(True))
    assert calls == []


def test_exact_estimate_boundary_and_large_property_capture():
    _, owner = _owner(); estimate = ij.estimate_auto_contrast_command(owner.snapshot())
    assert estimate.histogram.pixel_count == 100 and estimate.histogram.plane_bytes == 800 and estimate.histogram.histogram_bytes == 4104
    limits = ij.AutoContrastLimits(); limits.max_working_bytes, limits.max_record_bytes = estimate.working_bytes, estimate.record_bytes
    assert _resolve(owner, limits=limits).estimate.working_bytes <= estimate.working_bytes
    limits.max_working_bytes -= 1; calls = []
    with pytest.raises(ValueError): _resolve(owner, limits=limits, checkpoint=lambda: calls.append(True))
    view = owner.snapshot().display; properties = view.properties; properties["large"] = "x"*65536; view.properties = properties; owner.set_display(view)
    assert ij.estimate_auto_contrast_command(owner.snapshot()).display_bytes >= estimate.display_bytes+65536
    assert calls == []


@pytest.mark.parametrize("hook", ("pre", "checkpoint", "progress"))
def test_cancellation_and_precancel_callback_boundary(hook):
    _, owner = _owner(); before = _full(owner); token = ij.CancellationToken(); calls = []
    def cancel(*args): calls.append(args); token.cancel()
    kwargs = {"cancellation": token}
    if hook == "pre": token.cancel(); kwargs.update(checkpoint=cancel, progress=cancel)
    else: kwargs[hook] = cancel
    with pytest.raises(ij.OperationCancelled): _resolve(owner, **kwargs)
    assert (not calls if hook == "pre" else bool(calls)) and _full(owner) == before


@pytest.mark.parametrize("hook", ("checkpoint", "progress"))
def test_python_callback_exception_lifetime_and_gil(hook):
    _, owner = _owner(); before = _full(owner)
    class Failure(RuntimeError): pass
    def fail(*args):
        assert ctypes.pythonapi.PyGILState_Check() == 1
        raise Failure("owned Auto callback")
    with pytest.raises(Failure): _resolve(owner, **{hook: fail})
    assert _full(owner) == before
    with pytest.raises(ValueError): _resolve(owner, **{hook: 3})


def test_every_checkpoint_reenters_and_inputs_are_captured():
    _, owner = _owner([7]*100, np.uint16); _, foreign = _owner()
    request, limits = _request(bits=12), ij.AutoContrastLimits(); source = owner.snapshot(); original_id = owner.id
    calls, depth, entered = [], 0, False
    def checkpoint():
        nonlocal source, depth, entered
        assert ctypes.pythonapi.PyGILState_Check() == 1 and depth == 0
        depth += 1
        try:
            if not entered:
                entered = True; source = foreign.snapshot(); request.unsigned16_bits = 0; request.previous_divisor = 5000
                limits.max_working_bytes = 1
            child = ij.Dataset(np.asarray([[0, 1, 2, 3]], np.uint8), axes="YX")
            assert ij.accumulate_dataset_histogram(child).included_count == 4
            calls.append(True)
            assert len(calls) <= 512
        finally: depth -= 1
    command = ij.resolve_auto_contrast_command(source, request, limits, checkpoint=checkpoint)
    _assert(command, [0., 4095.], "ResetFallback", 0)
    assert len(calls) > 1 and depth == 0 and command.source.id == original_id and command.request.unsigned16_bits == 12


def test_callback_owners_are_released_and_native_work_releases_gil():
    _, owner = _owner()
    class Callback:
        def __call__(self, *args): assert ctypes.pythonapi.PyGILState_Check() == 1
    callback = Callback(); reference = weakref.ref(callback)
    _resolve(owner, checkpoint=callback, progress=callback)
    del callback; gc.collect(); assert reference() is None
    large = ij.Document(ij.Dataset(np.zeros((1024, 1024), np.uint8), axes="YX"))
    view = large.snapshot().display; view.properties = {"channel_display": _state()}; large.set_display(view)
    ready, go, ran = threading.Event(), threading.Event(), threading.Event()
    def worker():
        ready.set()
        if go.wait(5): ran.set()
    thread = threading.Thread(target=worker, daemon=True); thread.start(); assert ready.wait(2)
    previous = sys.getswitchinterval()
    try:
        sys.setswitchinterval(10); go.set()
        command = _resolve(large); observed = ran.is_set()
    finally:
        sys.setswitchinterval(previous); go.set(); thread.join(timeout=2)
    assert not thread.is_alive() and observed and command.histogram.finite_count == 1024*1024


def test_application_cycle_publication_six_methods_and_preservation(request):
    observations = {}
    _, owner = _owner(CYCLE_VALUES); app = _app(owner); stable = _stable(owner)
    for previous, used, low in ((0, 5000, 10), (5000, 2500, 20), (2500, 1250, 30)):
        old_revision = owner.snapshot().revisions.display
        job = _ready(app)
        assert app.auto_contrast_divisor() == previous and job.info.document_id == owner.id and job.info.resource_bytes > 0
        assert job.resolved.request.previous_divisor == previous
        command = app.publish_auto_contrast(job)
        assert command.resolved_range == [float(low), 50.] and app.auto_contrast_divisor() == used
        assert job.info.state == ij.JobState.COMPLETED and not job.cancel()
        assert owner.snapshot().revisions.display == old_revision+1 and _stable(owner) == stable
        observations[("cycle.first.range_low", "cycle.second.range_low", "cycle.third.range_low")[len(observations)]] = command.resolved_range[0]
    before = _full(owner); record = command.record()
    replay = app.replay_auto_contrast(record)
    assert replay.record() == record and app.auto_contrast_divisor() == 1250 and _stable(owner) == stable
    assert owner.snapshot().revisions.display == before[1][-1]+1
    app.reset_auto_contrast_cycle(); assert app.auto_contrast_divisor() == 0
    _, destination = _owner([7]*100, np.uint16); other = _app(destination)
    rebound = other.submit_rebound_auto_contrast(record, _request(bits=10))
    assert rebound.wait_for(5000)
    published = other.publish_auto_contrast(rebound)
    assert published.resolved_range == [0., 1023.] and other.auto_contrast_divisor() == 0
    _emit_auto_observation(request.node.name, observations)


@pytest.mark.parametrize("mode", ("color", "grayscale"))
def test_application_color_gray_repeat_uses_native_logical_state(mode):
    _, owner = _owner([1000+257*i for i in range(16)], np.uint16, channels=3, mode=mode)
    app = _app(owner)
    for previous, next_divisor in ((0, 5000), (5000, 2500)):
        result = app.publish_auto_contrast(_ready(app, _options(all_channels=True)))
        assert result.request.previous_divisor == previous and app.auto_contrast_divisor() == next_divisor
        assert result.effect.state["mode"] == mode and result.channels == [1]
        assert result.resolved_range == result.applied_range == [1000., 4840.9375]
        assert owner.snapshot().display.c == 1


@pytest.mark.parametrize("change", ("selection", "display", "navigation", "metadata", "away_back", "reset"))
def test_application_cycle_key_preserves_only_declared_changes(change):
    _, owner = _owner(channels=3, tcz=True); app = _app(owner)
    app.publish_auto_contrast(_ready(app)); assert app.auto_contrast_divisor() == 5000
    if change == "selection": owner.select(ij.Selection("point", [(0, 0)]))
    elif change == "display":
        view = owner.snapshot().display; view.zoom += 1; owner.set_display(view)
    elif change == "navigation":
        view = owner.snapshot().display; view.t = 0; owner.set_display(view)
    elif change == "metadata":
        edit = owner.begin_edit(); edit.set_title("changed"); owner.commit(edit)
    elif change == "away_back":
        _, other = _owner(); app.adopt(other); app.activate(owner.id)
    else: app.reset_auto_contrast_cycle()
    assert app.auto_contrast_divisor() == (5000 if change in ("selection", "display") else 0)


@pytest.mark.parametrize("change", ("pixels", "metadata", "selection", "display"))
def test_ready_job_all_four_stale_guards_are_atomic(change):
    _, owner = _owner(); app = _app(owner); job = _ready(app)
    if change == "pixels":
        edit = owner.begin_edit(); edit.replace_plane(np.full((1, 100), 7, np.uint8)); owner.commit(edit)
    elif change == "metadata":
        edit = owner.begin_edit(); edit.set_title("changed"); owner.commit(edit)
    elif change == "selection": owner.select(ij.Selection("point", [(0, 0)]))
    else:
        view = owner.snapshot().display; view.zoom += 1; owner.set_display(view)
    before = _full(owner)
    with pytest.raises(ij.RevisionConflict): app.publish_auto_contrast(job)
    assert _full(owner) == before and app.auto_contrast_divisor() == 0 and job.info.state == ij.JobState.FAILED


def test_ready_cancel_alias_accounting_and_foreign_application():
    _, owner = _owner(); app = _app(owner); job = _ready(app); alias = job.resolved
    other = ij.Application()
    with pytest.raises(ij.DocumentUnavailable): other.publish_auto_contrast(job)
    assert job.info.state == ij.JobState.READY
    before = _full(owner)
    assert job.cancel() and job.wait(5000) and job.info.state == ij.JobState.CANCELED
    with pytest.raises(ij.OperationCancelled): _ = job.resolved
    assert app.auto_contrast_divisor() == 0 and _full(owner) == before and app.job_scheduler.usage.retained_bytes > 0
    assert alias.resolved_range == [1., 2.]
    del alias; gc.collect()
    assert app.job_scheduler.usage.reserved_bytes == 0


@pytest.mark.parametrize("action", ("same_active", "away_back", "reset", "close"))
def test_activation_epoch_and_explicit_reset_admission(action):
    _, owner = _owner(); app = _app(owner); job = _ready(app)
    if action == "same_active": app.activate(owner.id)
    elif action == "away_back":
        _, other = _owner(); app.adopt(other); app.activate(owner.id)
    elif action == "reset": app.reset_auto_contrast_cycle()
    else: app.close(owner.id, discard=True)
    before = _full(owner)
    if action == "same_active":
        app.publish_auto_contrast(job); assert app.auto_contrast_divisor() == 5000
    else:
        if action == "close":
            with pytest.raises(ij.OperationCancelled): app.publish_auto_contrast(job)
            assert job.info.state == ij.JobState.CANCELED
        else:
            with pytest.raises(ij.RevisionConflict): app.publish_auto_contrast(job)
        assert _full(owner) == before


def test_application_options_capture_rebind_previous_guard_and_scheduler_cap():
    _, owner = _owner([7]*100, np.uint16); app = _app(owner)
    options, limits = _options(bits=12), ij.AutoContrastLimits()
    job = app.submit_auto_contrast(options, limits)
    options.unsigned16_bits, limits.max_working_bytes = 10, 1
    assert job.wait(5000) and job.resolved.request.unsigned16_bits == 12
    command = app.publish_auto_contrast(job); assert command.resolved_range == [0., 4095.]
    with pytest.raises(ij.RevisionConflict): app.submit_rebound_auto_contrast(command.record(), _request(previous=5000))
    _, limited_owner = _owner(); limited = _app(limited_owner)
    limited.job_scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=1))
    before = _full(limited_owner)
    with pytest.raises(ValueError): limited.submit_auto_contrast()
    assert _full(limited_owner) == before and limited.auto_contrast_divisor() == 0 and limited.job_scheduler.usage.reserved_bytes == 0


@pytest.mark.parametrize("value", (True, np.bool_(True), 1., 1.5, 1 << 100))
def test_job_wait_and_replay_id_integral_bridges(value):
    _, owner = _owner(); app = _app(owner); job = _ready(app); record = job.resolved.record()
    with pytest.raises(ERRORS): job.wait(value)
    with pytest.raises(ERRORS): job.wait_for(value)
    with pytest.raises(ERRORS): app.replay_auto_contrast(record, document_id=value)
    job.cancel()


def test_native_session_retains_record_new_owner_requires_full_rebind():
    _, owner = _owner(); command = _resolve(owner)
    capture = ij.SessionCapture([owner.snapshot()], workflow=[command.record()])
    reopened = ij.decode_session(ij.encode_session(capture))
    destination = reopened.documents[0]
    assert destination.id != owner.id and reopened.workflow == [command.record()]
    with pytest.raises(ValueError): ij.auto_contrast_command_from_record(destination.snapshot(), reopened.workflow[0])
    rebound = ij.rebind_auto_contrast_command(destination.snapshot(), reopened.workflow[0], _request())
    assert rebound.source.id == destination.id and rebound.cycle.previous == 0 and rebound.resolved_range == [1., 2.]
