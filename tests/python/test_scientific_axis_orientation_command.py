"""Ten owned V2 orientation adapter cases; no product-derived expectations.

Seven cases require only the native module. A07/A08/A10 additionally require the
installed shared ImGui binding and thin Python wrapper; none silently skips.
NumPy constructs owned fixtures and independently permutes exact sample bits.
This assertion source is not an expected-backed observation producer.
"""
import copy
import itertools
import json

import numpy as np
import pytest


CASE_IDS = (
    "A01.additive_sibling_nested_defensive_values_v1_separation",
    "A02.raw_dataset_descriptors_owned_literals",
    "A03.closed_v2_bool_integer_dictionary_grammar",
    "A04.strict_replay_original_first_full_rebind",
    "A05.execution_cap_controls_gil_capture",
    "A06.headless_owned_output_atomic_publication",
    "A07.scientific_current_tc_vs_headless_explicit",
    "A08.legacy_direct_defaults_recording_and_inactive_owner",
    "A09.rgb_and_quiet_bits_native_v3_save_open",
    "A10.wrapper_binding_same_record_no_python_geometry",
)
EXPECTED_COLLECTION_IDS = tuple("test_" + value.replace(".", "_", 1) for value in CASE_IDS)
EXPECTED_COLLECTION_COUNT = 10
CORE_COLLECTION_IDS = tuple(value for index, value in enumerate(EXPECTED_COLLECTION_IDS) if index not in (6, 7, 9))
DESKTOP_COLLECTION_IDS = tuple(EXPECTED_COLLECTION_IDS[index] for index in (6, 7, 9))
AXES = {"xy": (0, 1, 2), "xz": (0, 2, 1), "yz": (1, 2, 0)}
FLAGS = tuple(itertools.product((False, True), repeat=3))
CALIBRATION = {
    "spacing": [.4, 1.2, 3.], "origin": [11., -7., 19.],
    "direction": [1., .25, -.5, .5, -1., .125, 0., .75, 1.5],
    "unit": "um", "frame_interval": .75, "time_origin": 2.25, "time_unit": "min",
}
QUIET_BITS = (0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x7FC00001, 0xFFC12345)
CAP = 512 * 1024 * 1024


@pytest.fixture(autouse=True)
def native_api():
    global ij
    import imagejxx as ij


def _raw(value):
    value = np.asarray(value)
    return np.ascontiguousarray(value.astype(value.dtype.newbyteorder("<"))).tobytes()


def _fixture(dtype=np.uint16, samples=1, *, channels=3, times=2, quiet=False,
             calibration=None, observer=None):
    shape = (times, channels, 5, 7, 9, samples)
    if quiet:
        bits = np.asarray(QUIET_BITS, np.uint32)[np.arange(np.prod(shape)) % len(QUIET_BITS)].reshape(shape)
        pixels = bits.view(np.float32)
    else:
        t, c, z, y, x, s = np.indices(shape, dtype=np.int64)
        values = (97*t + 31*c + 11*z + 5*y + 3*x + s) % 251
        if np.dtype(dtype) == np.dtype(np.uint16):
            values += 40000
        elif np.dtype(dtype) in (np.dtype(np.float32), np.dtype(np.float64)):
            values = values - 125.125
        pixels = values.astype(dtype)
    keywords = {"axes": "TCZYXS", "calibration": ij.SpatialCalibration(**(calibration or CALIBRATION))}
    if quiet:
        keywords["sample_policy"] = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    dataset = ij.Dataset(pixels, **keywords)
    if observer is not None:
        dataset = dataset.observe_reads(observer)
    return pixels, dataset


def _owner(dtype=np.uint16, samples=1, *, channels=3, times=2, quiet=False,
           calibration=None, observer=None, spectrum=False):
    pixels, dataset = _fixture(dtype, samples, channels=channels, times=times,
                               quiet=quiet, calibration=calibration, observer=observer)
    owner = ij.Document(dataset, "Owned scientific orientation")
    edit = owner.begin_edit()
    edit.set_property("source_only", {"opaque": [None, True, 17]})
    edit.set_extension("future_annotation", {"version": 37, "opaque": [3, 1, 4]})
    edit.set_record_layout({"source_presence": True})
    edit.select(ij.Selection("rectangle", [(1, 1), (5, 4)]))
    edit.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)]))
    edit.set_rois([ij.ROI("positioned", ij.Selection("rectangle", [(1, 1), (3, 3)]), t=0, c=0, z=0),
                   ij.ROI("wildcard", ij.Selection("point", [(2, 2)]), z=1)])
    edit.set_overlays([{"name": "source overlay", "points": [[1, 1], [2, 2]]}])
    if spectrum:
        edit.set_spectrum(ij.FourierState(np.ones((7, 9), np.complex128), original_shape=(7, 9)))
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("history_probe", True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = times-1, channels-1, [4, 3, 2]
    view.zoom, view.linked_zoom, view.orthogonal = 1.25, False, True
    view.plane_zoom, view.pan = [1.25, 2., 3.], [[3., -2.], [1., 4.], [-5., 2.]]
    owner.set_display(view)
    return pixels, owner


def _request(plane="xz", *, first=1, last=None, step=2, t=1, c=2,
             flags=(True, True, True), cap=CAP):
    fixed = AXES[plane][2]
    if last is None:
        last = (9, 7, 5)[fixed] - 1
    return ij.AxisRangeOrientationRequest(
        ij.AxisRangeResliceRequest(plane, first, last, step, t=t, c=c, max_working_bytes=cap),
        reverse_order=flags[0], flip_vertical=flags[1], transpose=flags[2])


def _literal(pixels, request):
    source = request.range
    selected = list(range(source.first, source.last + 1, source.step))
    if request.reverse_order:
        selected.reverse()
    volume = pixels[source.t, source.c]
    if source.plane == "xy":
        planes = [volume[index, :, :, :] for index in selected]
    elif source.plane == "xz":
        planes = [volume[:, index, :, :] for index in selected]
    else:
        assert source.plane == "yz"
        planes = [volume[:, :, index, :] for index in selected]
    expected = np.stack(planes)
    if request.flip_vertical:
        expected = expected[:, ::-1, :, :]
    if request.transpose:
        expected = expected.transpose(0, 2, 1, 3)
    return np.ascontiguousarray(expected)


def _calibration_value(value):
    return {key: getattr(value, key) for key in CALIBRATION}


def _assert_layout(layout, request, shape, calibration=CALIBRATION):
    source = request.range
    horizontal, vertical, fixed = AXES[source.plane]
    extents = (shape[4], shape[3], shape[2])
    selected = list(range(source.first, source.last + 1, source.step))
    if request.reverse_order:
        selected.reverse()
    width, height = extents[horizontal], extents[vertical]
    if request.transpose:
        width, height = height, width
    assert list(layout.shape) == [1, 1, len(selected), height, width, shape[5]]
    assert tuple(layout.source_axes) == AXES[source.plane]  # Base permutation is deliberately not transposed.
    assert layout.plane_name == source.plane.upper()
    axes = (vertical, horizontal, fixed) if request.transpose else (horizontal, vertical, fixed)
    flip, reverse = -1 if request.flip_vertical else 1, -1 if request.reverse_order else 1
    signs = (flip, 1, reverse) if request.transpose else (1, flip, reverse)
    direction = np.asarray(calibration["direction"]).reshape(3, 3)
    spacing = calibration["spacing"]
    origin_index = [0, 0, 0]
    origin_index[vertical] = extents[vertical]-1 if request.flip_vertical else 0
    origin_index[fixed] = selected[0]
    expected_origin = [calibration["origin"][row] + sum(direction[row, axis] * spacing[axis] * origin_index[axis]
                       for axis in range(3)) for row in range(3)]
    actual = layout.calibration
    assert actual.spacing == [spacing[axes[0]], spacing[axes[1]], spacing[fixed] * source.step]
    np.testing.assert_allclose(actual.origin, expected_origin, rtol=0, atol=1e-12)
    np.testing.assert_array_equal(np.asarray(actual.direction).reshape(3, 3), direction[:, axes] * signs)
    assert (actual.unit, actual.time_unit, actual.frame_interval, actual.time_origin) == (
        calibration["unit"], calibration["time_unit"], calibration["frame_interval"],
        calibration["time_origin"] + calibration["frame_interval"] * source.t)
    geometry = layout.geometry
    assert geometry["horizontal_axis"] == "XYZ"[axes[0]] and geometry["vertical_axis"] == "XYZ"[axes[1]]
    assert geometry["unit"] == calibration["unit"]
    np.testing.assert_allclose(geometry["origin"], expected_origin, rtol=0, atol=1e-12)
    np.testing.assert_allclose(geometry["horizontal_step"], direction[:, axes[0]] * spacing[axes[0]] * signs[0], rtol=0, atol=1e-12)
    np.testing.assert_allclose(geometry["vertical_step"], direction[:, axes[1]] * spacing[axes[1]] * signs[1], rtol=0, atol=1e-12)
    for k, index in enumerate(selected):
        for u, v in ((0, 0), (width-1, height-1)):
            base_u, base_v = (v, u) if request.transpose else (u, v)
            voxel = [0, 0, 0]
            voxel[horizontal], voxel[vertical], voxel[fixed] = base_u, extents[vertical]-1-base_v if request.flip_vertical else base_v, index
            expected = [calibration["origin"][row] + sum(direction[row, axis] * spacing[axis] * voxel[axis]
                        for axis in range(3)) for row in range(3)]
            np.testing.assert_allclose(actual.voxel_to_world((u, v, k)), expected, rtol=0, atol=1e-12)
    assert layout.working_bytes >= np.prod(layout.shape) * 8 and layout.working_bytes <= source.max_working_bytes


def _state(owner):
    snap, history = owner.snapshot(), owner.history()
    view = snap.display
    return {"snapshot": snap, "id": snap.id, "identity": snap.data_identity, "dirty": snap.dirty,
            "pixels": _raw(snap.dataset.numpy("TCZYXS")), "policy": snap.dataset.sample_policy,
            "shape": tuple(snap.dataset.shape), "calibration": _calibration_value(snap.dataset.calibration),
            "metadata": snap.metadata, "layout": snap.record_layout, "selection": snap.selection,
            "previous": snap.previous_selection, "rois": [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snap.rois],
            "overlays": snap.overlays, "spectrum": None if snap.spectrum is None else _raw(snap.spectrum.numpy()),
            "revisions": (snap.revisions.pixels, snap.revisions.metadata, snap.revisions.selection, snap.revisions.display),
            "history": (history.undo_steps, history.redo_steps, history.step_limit, history.pixel_budget),
            "display": (view.t, view.c, view.crosshair, view.zoom, view.linked_zoom, view.plane_zoom, view.pan,
                        view.orthogonal, view.revision, view.properties)}


def _unchanged(owner, before):
    after = _state(owner)
    assert after.pop("snapshot").shares_data(before["snapshot"])
    assert after == {key: value for key, value in before.items() if key != "snapshot"}


def _counts(observer):
    value = observer.counts()
    return value.plane_reads, value.region_reads, value.overflowed


def _output(owner, pixels, source, request, layout):
    snapshot = owner.snapshot()
    expected, actual = _literal(pixels, request), snapshot.dataset.numpy("ZYXS")
    assert actual.dtype == pixels.dtype and _raw(actual) == _raw(expected)
    assert tuple(snapshot.dataset.shape) == (1, 1, *expected.shape)
    assert snapshot.dataset.sample_policy == source.dataset.sample_policy
    assert not snapshot.dataset.shares_pixels(source.dataset) and snapshot.id != source.id
    assert snapshot.dirty is False and owner.undo_steps == owner.redo_steps == 0
    assert snapshot.selection is snapshot.previous_selection is snapshot.spectrum is None
    assert snapshot.rois == snapshot.overlays == [] and snapshot.record_layout == {}
    assert snapshot.metadata["properties"] == {} and set(snapshot.metadata["extensions"]) == {"axis_range_orientation"}
    assert snapshot.display.t == snapshot.display.c == 0 and not snapshot.display.orthogonal
    nested = request.range
    assert snapshot.metadata["title"] == (f"{source.metadata['title']} {nested.plane.upper()} "
        f"{nested.first+1}-{nested.last+1} step {nested.step} "
        f"[R{int(request.reverse_order)} F{int(request.flip_vertical)} T{int(request.transpose)}]")
    assert _calibration_value(snapshot.dataset.calibration) == _calibration_value(layout.calibration)
    _assert_layout(layout, request, source.dataset.shape, _calibration_value(source.dataset.calibration))


def _tamper(record, keys, value):
    result = copy.deepcopy(record)
    target = result
    for key in keys[:-1]:
        target = target[key]
    target[keys[-1]] = value
    return result


def _editor(tmp_path):
    from imagejxx import _desktop  # Required installed module; never optional/skip.
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    assert _desktop is not None
    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def _activate(editor, owner):
    editor.active_index = next(index for index, row in enumerate(editor.document_infos) if row["id"] == owner.id)


def _direct(editor, owner, request):
    value = request.range
    return editor.action("reslice_oriented_axis_range", document_id=owner.id, plane=value.plane,
                         first=value.first, last=value.last, step=value.step,
                         reverse_order=request.reverse_order, flip_vertical=request.flip_vertical,
                         transpose=request.transpose)


def test_A01_additive_sibling_nested_defensive_values_v1_separation():
    pixels, owner = _owner()
    request = _request()
    command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
    assert isinstance(command, ij.ResolvedAxisOrientationCommand)
    original = command.record()
    assert set(original) == {"schema", "version", "operation", "source", "request", "derived", "destination"}
    assert (original["schema"], original["version"], original["operation"], original["destination"]) == (
        "imagejxx.axis-extraction-command", 2, "axis_range_orientation", "new_document")
    assert command.operation == "axis_range_orientation" and command.source.shares_data(owner.snapshot())
    nested = request.range
    nested.first, nested.t, nested.c, nested.max_working_bytes = 0, 0, 0, 0
    request.range = nested
    request.reverse_order = request.flip_vertical = request.transpose = False
    returned = command.request
    changed = returned.range
    changed.step = 0
    returned.range = changed
    returned.reverse_order = False
    assert command.request.range.step == 2 and command.request.reverse_order is True
    detached = command.record()
    detached["source"]["shape"][0] = 17
    detached["derived"]["calibration"]["origin"][0] = 999
    geometry = command.layout.geometry
    geometry["origin"][0] = -999
    origin = command.layout.calibration.origin
    origin[0] = -999
    assert command.record() == original
    for name in ("source", "request", "layout", "operation"):
        with pytest.raises(AttributeError):
            setattr(command, name, None)
    for old_request in (ij.AxisPlaneExportRequest("xy", 1, t=1, c=2), ij.AxisRangeResliceRequest("xz", 1, 6, 2, t=1, c=2)):
        old = ij.resolve_axis_extraction_command(owner.snapshot(), old_request)
        assert old.record()["version"] == 1
        ij.validate_axis_extraction_command_record(old.record())
        with pytest.raises(ValueError):
            ij.validate_axis_orientation_command_record(old.record())
        with pytest.raises(ValueError):
            ij.validate_axis_orientation_command_record(_tamper(old.record(), ("version",), 2))
    with pytest.raises(ValueError):
        ij.validate_axis_extraction_command_record(original)
    with pytest.raises(TypeError):
        ij.resolve_axis_extraction_command(owner.snapshot(), _request())
    with pytest.raises(TypeError):
        ij.resolve_axis_orientation_command(owner.snapshot(), ij.AxisRangeResliceRequest())
    _output(ij.prepare_axis_orientation(command).prepared_new_document(), pixels, owner.snapshot(), command.request, command.layout)


def test_A02_raw_dataset_descriptors_owned_literals():
    descriptor_count = 0
    for dtype, samples in itertools.product((np.uint8, np.uint16, np.float32, np.float64), (1, 3, 4)):
        observer = ij.DatasetReadObserver()
        _, dataset = _fixture(dtype, samples, observer=observer)
        for plane, flags in itertools.product(AXES, FLAGS):
            request = _request(plane, flags=flags)
            raw = ij.describe_axis_range_orientation(list(dataset.shape), np.dtype(dtype).name,
                                                     ij.SpatialCalibration(**CALIBRATION), request)
            delegated = ij.describe_axis_range_orientation(dataset, request)
            _assert_layout(raw, request, dataset.shape)
            assert list(raw.shape) == list(delegated.shape) and raw.geometry == delegated.geometry
            assert raw.working_bytes == delegated.working_bytes
            assert _calibration_value(raw.calibration) == _calibration_value(delegated.calibration)
            descriptor_count += 1
        assert _counts(observer) == (0, 0, False)
    assert descriptor_count == 288
    for samples in (2, 5):
        with pytest.raises(ValueError):
            ij.describe_axis_range_orientation([2, 3, 5, 7, 9, samples], "uint8",
                                                ij.SpatialCalibration(**CALIBRATION), _request())
        with pytest.raises(ValueError):
            _fixture(np.uint8, samples)
    for shape in ([2, 3, 5, True, 9, 1], [2, 3, 5, np.bool_(True), 9, 1],
                  [2, 3, 5, 7.0, 9, 1], [2, 3, 5, 7, 2**31, 1]):
        with pytest.raises((TypeError, ValueError, OverflowError)):
            ij.describe_axis_range_orientation(shape, "uint8", ij.SpatialCalibration(**CALIBRATION), _request())
    execution_count = 0
    for dtype, samples, channels, quiet in ((np.uint16, 1, 3, False), (np.float64, 4, 2, False), (np.float32, 1, 5, True)):
        pixels, owner = _owner(dtype, samples, channels=channels, quiet=quiet)
        before = _state(owner)
        for plane, flags in itertools.product(AXES, FLAGS):
            request = _request(plane, c=channels-1, flags=flags)
            command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
            _output(ij.prepare_axis_orientation(command).prepared_new_document(), pixels, owner.snapshot(), request, command.layout)
            execution_count += 1
        _unchanged(owner, before)
    assert execution_count == 72
    pixels, owner = _owner(np.float64)
    for plane in AXES:
        request = _request(plane, flags=(False, False, False))
        command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
        assert command.record()["version"] == 2 and command.operation == "axis_range_orientation"
        plain = ij.prepare_axis_range_reslice(owner.snapshot(), request.range).prepared_new_document()
        oriented = ij.prepare_axis_orientation(command).prepared_new_document()
        assert _raw(plain.snapshot().dataset.numpy("ZYXS")) == _raw(oriented.snapshot().dataset.numpy("ZYXS"))
        assert _calibration_value(plain.snapshot().dataset.calibration) == _calibration_value(oriented.snapshot().dataset.calibration)
        upper = (9, 7, 5)[AXES[plane][2]]-1
        for flags in ((False, False, False), (True, True, True)):
            request = _request(plane, first=upper, last=upper, step=7, flags=flags)
            command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
            _output(ij.prepare_axis_orientation(command).prepared_new_document(), pixels, owner.snapshot(), request, command.layout)
            assert command.layout.shape[2] == 1 and command.layout.calibration.spacing[2] == CALIBRATION["spacing"][AXES[plane][2]]*7


def test_A03_closed_v2_bool_integer_dictionary_grammar():
    observer = ij.DatasetReadObserver()
    _, owner = _owner(observer=observer)
    before = _state(owner)
    record = ij.resolve_axis_orientation_command(owner.snapshot(), _request()).record()
    reads = _counts(observer)
    assert set(record["request"]) == {"plane", "first", "last", "step", "t", "c", "max_working_bytes",
                                     "reverse_order", "flip_vertical", "transpose"}
    malformed = [
        (("extra",), 1), (("schema",), "other"), (("version",), 1), (("version",), True),
        (("operation",), "axis_range_reslice"), (("destination",), "in_place"),
        (("source", "document_id"), True), (("source", "document_id"), 0),
        (("source", "shape", 0), True), (("source", "shape", 4), 2**31),
        (("source", "pixel_type"), "uint32"), (("source", "sample_policy"), "unchecked"),
        (("source", "sample_policy"), "preserve_quiet_nan_float32"),
        (("source", "revisions", "pixels"), True), (("source", "revisions", "metadata"), -1),
        (("source", "calibration", "origin", 0), float("inf")),
        (("source", "calibration", "direction"), [0.]*9),
        (("source", "calibration", "spacing", 0), True),
        (("request", "plane"), "xz"), (("request", "first"), True), (("request", "last"), 7),
        (("request", "step"), 0), (("request", "step"), 1.5), (("request", "t"), 2),
        (("request", "c"), True), (("request", "max_working_bytes"), 0),
        (("request", "max_working_bytes"), True),
        (("request", "max_working_bytes"), 2**64), (("request", "range"), {}),
        (("derived", "shape", 4), 999), (("derived", "source_axes"), [0, 1, 2]),
        (("derived", "geometry", "origin", 0), 123), (("derived", "geometry", "vertical_step", 2), 123),
        (("derived", "working_bytes"), 1), (("derived", "calibration", "time_origin"), 999),
    ]
    for flag in ("reverse_order", "flip_vertical", "transpose"):
        for value in (0, 1, 1.0, "true", None, False):
            malformed.append((("request", flag), value))
    for keys, value in malformed:
        bad = _tamper(record, keys, value)
        for invoke in (lambda: ij.validate_axis_orientation_command_record(bad),
                       lambda: ij.axis_orientation_command_from_record(owner.snapshot(), bad),
                       lambda: ij.rebind_axis_orientation_command(owner.snapshot(), bad, _request("xy"))):
            with pytest.raises((ValueError, TypeError, OverflowError, IndexError)):
                invoke()
    for field in ("reverse_order", "flip_vertical", "transpose"):
        missing = copy.deepcopy(record)
        del missing["request"][field]
        with pytest.raises(ValueError):
            ij.validate_axis_orientation_command_record(missing)
    ij.validate_axis_orientation_command_record(json.loads(json.dumps(record)))
    assert _counts(observer) == reads  # Validation/replay/rebind failures perform no provider reads.
    _unchanged(owner, before)


def test_A04_strict_replay_original_first_full_rebind():
    pixels, owner = _owner()
    request = _request("yz")
    original = ij.resolve_axis_orientation_command(owner.snapshot(), request).record()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = 0, 0, [1, 1, 0], 2.5
    owner.set_display(view)
    owner.select(ij.Selection("point", [(3, 2)]))
    decoded = ij.axis_orientation_command_from_record(owner.snapshot(), copy.deepcopy(original))
    assert decoded.record() == original and (decoded.request.range.t, decoded.request.range.c) == (1, 2)
    _output(ij.prepare_axis_orientation(decoded).prepared_new_document(), pixels, owner.snapshot(), request, decoded.layout)
    _, foreign = _owner()
    with pytest.raises(ValueError):
        ij.axis_orientation_command_from_record(foreign.snapshot(), original)
    for mutation in ("metadata", "pixels"):
        fresh = ij.resolve_axis_orientation_command(owner.snapshot(), request).record()
        edit = owner.begin_edit()
        if mutation == "metadata":
            edit.set_property("stale", 1)
        else:
            edit.replace_plane(np.full((7, 9), 9, np.uint16), t=1, c=2, z=1)
        owner.commit(edit)
        with pytest.raises(ij.RevisionConflict):
            ij.axis_orientation_command_from_record(owner.snapshot(), fresh)
        owner.undo()
        with pytest.raises(ij.RevisionConflict):
            ij.axis_orientation_command_from_record(owner.snapshot(), fresh)
    changed_calibration = {**CALIBRATION, "origin": [-13., 5., 7.], "spacing": [2., 3., 4.], "time_origin": 31.}
    other_pixels, other = _owner(calibration=changed_calibration)
    replacement = _request("xz", t=0, c=1, flags=(False, True, False))
    rebound = ij.rebind_axis_orientation_command(other.snapshot(), original, replacement)
    assert rebound.record()["source"]["document_id"] == other.id
    assert (rebound.request.range.t, rebound.request.range.c) == (0, 1)
    _output(ij.prepare_axis_orientation(rebound).prepared_new_document(), other_pixels, other.snapshot(), replacement, rebound.layout)
    bad = _tamper(original, ("derived", "working_bytes"), 1)
    with pytest.raises(ValueError) as original_error:
        ij.validate_axis_orientation_command_record(bad)
    invalid_replacement = _request()
    nested = invalid_replacement.range
    nested.step = 0
    invalid_replacement.range = nested
    with pytest.raises(ValueError) as rebound_error:
        ij.rebind_axis_orientation_command(other.snapshot(), bad, invalid_replacement)
    assert str(rebound_error.value) == str(original_error.value)
    for invalid in ({}, True, ij.AxisRangeResliceRequest()):
        with pytest.raises(TypeError):
            ij.rebind_axis_orientation_command(other.snapshot(), original, invalid)
    assert original["source"]["document_id"] == owner.id and original["request"]["plane"] == "YZ"


def test_A05_execution_cap_controls_gil_capture():
    observer = ij.DatasetReadObserver()
    pixels, owner = _owner(observer=observer)
    request = _request()
    command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
    record, calls, reads = command.record(), [], _counts(observer)
    for cap in (0, command.layout.working_bytes-1):
        with pytest.raises(ValueError):
            ij.prepare_axis_orientation(command, execution_max_working_bytes=cap, progress=lambda *args: calls.append(args))
    for cap in (True, np.bool_(False), -1, 1.5, 2**64):
        with pytest.raises((TypeError, ValueError, OverflowError)):
            ij.prepare_axis_orientation(command, execution_max_working_bytes=cap)
    assert calls == [] and _counts(observer) == reads and command.record() == record
    before = _state(owner)
    for callback in ("progress", "checkpoint"):
        previous_calls = len(calls)
        def mutate(*args):
            calls.append(args)
            request.range = ij.AxisRangeResliceRequest("yz", -1, -1, 0, t=0, c=0, max_working_bytes=0)
            request.reverse_order = request.flip_vertical = request.transpose = False
            detached = command.request
            detached.transpose = False
        prepared = ij.prepare_axis_orientation(command, execution_max_working_bytes=command.layout.working_bytes,
                                               **{callback: mutate})
        assert len(calls) > previous_calls and command.record() == record
        _output(prepared.prepared_new_document(), pixels, owner.snapshot(), command.request, command.layout)
        with pytest.raises(ValueError, match="callbacks must be callable"):
            ij.prepare_axis_orientation(command, **{callback: []})
        def fail(*_):
            raise RuntimeError("owned orientation callback failure")
        with pytest.raises(RuntimeError, match="owned orientation callback failure"):
            ij.prepare_axis_orientation(command, **{callback: fail})
    for cancel_before in (True, False):
        token, progress = ij.CancellationToken(), []
        if cancel_before:
            token.cancel()
        def cancel(done, total):
            progress.append((done, total))
            if done > 0:
                token.cancel()
        with pytest.raises(ij.OperationCancelled):
            ij.prepare_axis_orientation(command, cancellation=token, progress=cancel)
        assert token.canceled and (not progress if cancel_before else any(done > 0 for done, _ in progress))
    assert command.record() == record
    _unchanged(owner, before)


def test_A06_headless_owned_output_atomic_publication():
    pixels, owner = _owner(spectrum=True)
    _, other = _owner()
    application = ij.Application()
    application.adopt(owner)
    application.adopt(other)
    application.activate(other.id)
    before = _state(owner)
    command = ij.resolve_axis_orientation_command(owner.snapshot(), _request())
    prepared = ij.prepare_axis_orientation(command)
    candidate = prepared.prepared_new_document()
    assert candidate.id not in application.document_ids
    _output(candidate, pixels, owner.snapshot(), command.request, command.layout)
    published = application.publish(prepared)
    assert published.created and published.document is candidate and published.document_id == candidate.id
    assert application.active_document_id == candidate.id and not prepared.active
    _unchanged(owner, before)
    with pytest.raises(ValueError):
        application.publish(prepared)
    for mutation in ("source_pixels", "source_metadata", "candidate", "closed", "foreign", "cancelled"):
        _, source = _owner()
        app = ij.Application()
        app.adopt(source)
        token = ij.CancellationToken()
        prepared = ij.prepare_axis_orientation(ij.resolve_axis_orientation_command(source.snapshot(), _request()), cancellation=token)
        output = prepared.prepared_new_document()
        publisher = app
        if mutation == "source_pixels":
            edit = source.begin_edit()
            edit.replace_plane(np.full((7, 9), 17, np.uint16), t=1, c=2, z=1)
            source.commit(edit)
        elif mutation in ("source_metadata", "candidate"):
            target = source if mutation == "source_metadata" else output
            edit = target.begin_edit()
            edit.set_property("tampered", True)
            target.commit(edit)
        elif mutation == "closed":
            app.close(source.id, discard=True)
        elif mutation == "foreign":
            publisher = ij.Application()
        else:
            token.cancel()
        membership, active, state = publisher.document_ids, publisher.active_document_id, _state(source)
        with pytest.raises((ValueError, RuntimeError, ij.RevisionConflict, ij.DocumentUnavailable, ij.OperationCancelled)):
            publisher.publish(prepared)
        assert publisher.document_ids == membership and publisher.active_document_id == active
        assert output.id not in publisher.document_ids
        _unchanged(source, state)


def test_A07_scientific_current_tc_vs_headless_explicit(tmp_path):
    editor = _editor(tmp_path)
    pixels, owner = _owner()
    editor.add_native_document(owner)
    explicit = _request("xy", t=0, c=0)
    headless = ij.resolve_axis_orientation_command(owner.snapshot(), explicit)
    _output(ij.prepare_axis_orientation(headless).prepared_new_document(), pixels, owner.snapshot(), explicit, headless.layout)
    captured = _request("xy")
    before = _state(owner)
    editor.recording = True
    _direct(editor, owner, captured)
    record, = editor.workflow
    assert (record["request"]["t"], record["request"]["c"]) == (1, 2)
    _output(editor.live_document().native_owner, pixels, before["snapshot"], captured,
            ij.resolve_axis_orientation_command(before["snapshot"], captured).layout)
    _unchanged(owner, before)
    editor.close_document(editor.live_document().id, discard=True)
    _activate(editor, owner)
    wrong_tc = _request("xz", t=0, c=0)
    membership, workflow = editor.document_infos, copy.deepcopy(editor.workflow)
    with pytest.raises(ValueError):
        editor.replay_axis_orientation(record, rebind_request=wrong_tc, document_id=owner.id)
    assert editor.document_infos == membership and editor.workflow == workflow
    _, second = _owner()
    editor.add_native_document(second)
    membership, workflow, active = editor.document_infos, copy.deepcopy(editor.workflow), editor.live_document().id
    with pytest.raises((ValueError, ij.DocumentUnavailable)):
        _direct(editor, owner, captured)
    assert editor.document_infos == membership and editor.workflow == workflow and editor.live_document().id == active
    _unchanged(owner, before)
    _activate(editor, owner)
    view = owner.snapshot().display
    view.t, view.c = 0, 0
    owner.set_display(view)
    navigated = _state(owner)
    result = editor.replay_axis_orientation(record, document_id=owner.id)
    assert result == editor.live_document().id
    _output(editor.live_document().native_owner, pixels, owner.snapshot(), captured,
            ij.resolve_axis_orientation_command(before["snapshot"], captured).layout)
    _unchanged(owner, navigated)


def test_A08_legacy_direct_defaults_recording_and_inactive_owner(tmp_path):
    editor = _editor(tmp_path)
    pixels, owner = _owner(np.uint8, channels=1, times=1)
    _, other = _owner(np.uint8, channels=1, times=1)
    # Explicit ROI T/C positions make even a singleton owner scientific. Keep
    # every ROI's remaining geometry/style/Z while creating ordinary fixtures.
    for fixture in (owner, other):
        original_rois = fixture.snapshot().rois
        edit = fixture.begin_edit()
        edit.set_rois([ij.ROI(roi.name, roi.selection, t=None, c=None,
                             z=roi.z, attributes=roi.attributes) for roi in original_rois])
        fixture.commit(edit)
        fixture.mark_saved()
        ordinary = fixture.snapshot()
        assert tuple(ordinary.dataset.shape[:2]) == (1, 1)
        assert all(roi.t is None and roi.c is None for roi in ordinary.rois)
        assert [(roi.name, roi.selection, roi.z, roi.attributes) for roi in ordinary.rois] == [
            (roi.name, roi.selection, roi.z, roi.attributes) for roi in original_rois]
    editor.add_native_document(owner)
    editor.add_native_document(other)
    assert editor.live_document().id == other.id
    before = _state(owner)
    default = ij.AxisRangeOrientationRequest()
    assert (default.range.plane, default.range.first, default.range.last, default.range.step) == ("xz", 0, 0, 1)
    assert default.reverse_order is default.flip_vertical is default.transpose is False
    editor.recording = True
    output = editor.live_document(owner.id).reslice_oriented_range("xz", 0, 6)
    request = _request("xz", first=0, last=6, step=1, t=0, c=0, flags=(False, False, False))
    record, = editor.workflow
    assert (record["version"], record["operation"]) == (2, "axis_range_orientation")
    assert all(record["request"][flag] is False for flag in ("reverse_order", "flip_vertical", "transpose"))
    _output(output.native_owner, pixels, owner.snapshot(), request,
            ij.resolve_axis_orientation_command(owner.snapshot(), request).layout)
    _unchanged(owner, before)
    editor.recording = False
    _direct(editor, owner, request)
    assert editor.workflow == [record]
    for old_request in (ij.AxisPlaneExportRequest("xy", 1), ij.AxisRangeResliceRequest("xz", 0, 6, 1)):
        old = ij.resolve_axis_extraction_command(owner.snapshot(), old_request)
        assert old.record()["version"] == 1
        ij.validate_axis_extraction_command_record(old.record())


def test_A09_rgb_and_quiet_bits_native_v3_save_open():
    limits = ij.SessionLimits()
    limits.max_documents = 16
    limits.max_encoded_bytes = limits.max_json_bytes = 32 * 1024 * 1024
    limits.max_working_bytes = 128 * 1024 * 1024
    for dtype, samples, channels, quiet in ((np.uint8, 3, 3, False), (np.float32, 1, 5, True)):
        pixels, owner = _owner(dtype, samples, channels=channels, quiet=quiet)
        request = _request("yz", c=channels-1, flags=(True, True, True))
        command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
        output = ij.prepare_axis_orientation(command).prepared_new_document()
        _output(output, pixels, owner.snapshot(), request, command.layout)
        legacy = {"operation": "add", "parameters": {"value": 2}}
        record = command.record()
        encoded = ij.encode_session(ij.SessionCapture([owner.snapshot(), output.snapshot()], workflow=[legacy, record]), limits)
        assert bytes(encoded).startswith(b"IMAGEJXX3\n")
        decoded = ij.decode_session(encoded, limits)
        reopened_source, reopened_output = decoded.documents
        assert decoded.workflow == [legacy, record]
        assert reopened_source.id != owner.id and reopened_output.id != output.id
        assert _raw(reopened_source.snapshot().dataset.numpy("TCZYXS")) == _raw(pixels)
        assert _raw(reopened_output.snapshot().dataset.numpy("ZYXS")) == _raw(_literal(pixels, request))
        assert reopened_source.snapshot().dataset.sample_policy == owner.snapshot().dataset.sample_policy
        assert _calibration_value(reopened_output.snapshot().dataset.calibration) == _calibration_value(command.layout.calibration)
        with pytest.raises(ValueError):
            ij.axis_orientation_command_from_record(reopened_source.snapshot(), record)
        rebound = ij.rebind_axis_orientation_command(reopened_source.snapshot(), record, request)
        _output(ij.prepare_axis_orientation(rebound).prepared_new_document(), pixels, reopened_source.snapshot(), request, rebound.layout)
        assert record["source"]["document_id"] == owner.id


def test_A10_wrapper_binding_same_record_no_python_geometry(tmp_path):
    editor = _editor(tmp_path)
    pixels, owner = _owner()
    editor.add_native_document(owner)
    request = _request("xz", flags=(False, True, True))
    command = ij.resolve_axis_orientation_command(owner.snapshot(), request)
    original = command.record()
    before = _state(owner)
    editor.recording = True
    first_id = editor.replay_axis_orientation(copy.deepcopy(original), document_id=owner.id)
    assert first_id == editor.live_document().id
    first = editor.live_document().native_owner
    _output(first, pixels, owner.snapshot(), request, command.layout)
    editor.close_document(first_id, discard=True)
    _activate(editor, owner)
    second_id = editor._editor.replay_axis_orientation(copy.deepcopy(original), document_id=owner.id)
    assert second_id == editor.live_document().id and second_id != first_id
    _output(editor.live_document().native_owner, pixels, owner.snapshot(), request, command.layout)
    assert editor.workflow == [original, original] and editor.results == []
    _unchanged(owner, before)
    membership, workflow = editor.document_infos, copy.deepcopy(editor.workflow)
    malformed = _tamper(original, ("request", "transpose"), 1)
    for invoke in (lambda: editor.replay_axis_orientation(malformed, rebind_request=request, document_id=owner.id),
                   lambda: editor._editor.replay_axis_orientation(malformed, rebind_request=request, document_id=owner.id)):
        with pytest.raises(ValueError):
            invoke()
    assert editor.document_infos == membership and editor.workflow == workflow
    assert original == command.record()
