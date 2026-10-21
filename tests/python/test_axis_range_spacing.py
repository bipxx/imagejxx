"""Public spacing binding contracts with independent small literal expectations.

Processing is native; these tests claim no ImageJ/Slicer equivalence. Pixel-read
counters are not public Python interfaces: invalid cases assert pre-progress and
atomic state, while native counted-provider tests qualify zero provider reads.
"""

import math

import numpy as np
import pytest


WRAPPERS = ("reslice", "snapshot_prepare", "application_prepare")
PROFILES = ((np.uint8, 1), (np.float32, 1), (np.uint8, 3), (np.uint8, 4))
# Source-only inventory; Root's future collection must independently match it.
EXPECTED_CASE_COUNT = 72


@pytest.fixture(autouse=True)
def native_api():
    global ij
    import imagejxx as ij


def raw(value):
    array = np.asarray(value)
    return np.ascontiguousarray(array.astype(array.dtype.newbyteorder("<"))).tobytes()


def source(dtype=np.uint16, samples=1, scientific=False, calibration=None):
    shape = (2, 3, 3, 3, 4, samples) if scientific else (1, 1, 3, 2, 2, samples)
    t, c, z, y, x, s = np.indices(shape, dtype=np.int64)
    values = 1000 * t + 100 * c + 10 * z + 2 * y + x + s
    pixels = values.astype(dtype)
    return pixels, ij.Dataset(pixels, axes="TCZYXS", calibration=calibration or ij.SpatialCalibration())


def request(plane="xz", first=0, last=1, step=1, *, h=.5, sampling="nearest",
            flags=(False, False, False), t=0, c=0, max_working_bytes=512 * 1024 * 1024):
    orientation = ij.AxisRangeOrientationRequest(
        ij.AxisRangeResliceRequest(plane, first, last, step, t=t, c=c,
                                  max_working_bytes=max_working_bytes),
        reverse_order=flags[0], flip_vertical=flags[1], transpose=flags[2],
    )
    mode = ij.AxisRangeSampling.Nearest if sampling == "nearest" else ij.AxisRangeSampling.Linear
    return ij.AxisRangeSpacingRequest(orientation, physical_vertical_spacing=h, sampling=mode)


def controller(dataset):
    owner, other, app = ij.Document(dataset, "Source"), ij.Document(dataset, "Other"), ij.Application()
    app.adopt(owner)
    app.adopt(other)
    app.activate(owner.id)
    return app, owner, other


def state(owner):
    snap = owner.snapshot()
    display = snap.display
    return (snap, owner.undo_steps, owner.redo_steps, snap.dirty, snap.data_identity,
            snap.metadata, snap.record_layout, snap.selection, snap.previous_selection,
            [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snap.rois],
            snap.overlays, tuple(display.crosshair), display.t, display.c, display.zoom,
            display.orthogonal, display.linked_zoom, tuple(display.plane_zoom),
            tuple(tuple(row) for row in display.pan), display.properties, display.revision,
            (snap.revisions.pixels, snap.revisions.metadata, snap.revisions.selection, snap.revisions.display),
            None if snap.spectrum is None else (tuple(snap.spectrum.shape), tuple(snap.spectrum.original_shape), raw(snap.spectrum.numpy())))


def unchanged(owner, before):
    after = state(owner)
    assert after[0].shares_data(before[0]) and after[1:] == before[1:]


def invoke(wrapper, app, owner, req, **controls):
    if wrapper == "reslice":
        return ij.reslice_spaced_axis_range(owner.snapshot().dataset, req, **controls)
    if wrapper == "snapshot_prepare":
        return ij.prepare_axis_range_spacing(owner.snapshot(), req, **controls)
    assert wrapper == "application_prepare"
    # Positional request-first order is deliberate.
    return app.prepare_axis_range_spacing(req, owner.id, **controls)


def test_public_defaults_nested_values_estimate_and_result_are_detached():
    default = ij.AxisRangeSpacingRequest()
    assert default.physical_vertical_spacing == 1 and default.sampling == ij.AxisRangeSampling.Nearest
    old = default.orientation
    assert (old.range.plane, old.range.first, old.range.last, old.range.step,
            old.range.t, old.range.c, old.range.max_working_bytes) == ("xz", 0, 0, 1, 0, 0, 512 * 1024 * 1024)
    assert old.reverse_order is old.flip_vertical is old.transpose is False
    assert (ij.AxisRangeResliceRequest().plane, ij.AxisPlaneExportRequest().plane) == ("xz", "xy")
    base = ij.AxisRangeResliceRequest("xz", 0, 1, 1)
    orientation = ij.AxisRangeOrientationRequest(base)
    req = ij.AxisRangeSpacingRequest(orientation, physical_vertical_spacing=.5)
    base.step = 0
    orientation.reverse_order = True
    detached = req.orientation
    nested = detached.range
    nested.step = 0
    detached.range = nested
    assert req.orientation.range.step == 1 and req.orientation.reverse_order is False
    req.orientation = detached
    assert req.orientation.range.step == 0
    req.orientation = ij.AxisRangeOrientationRequest(ij.AxisRangeResliceRequest("xz", 0, 1, 1))
    _, dataset = source()
    estimate = ij.estimate_axis_range_spacing(dataset, req)
    result = ij.reslice_spaced_axis_range(dataset, req)
    assert isinstance(estimate, ij.AxisRangeSpacingEstimate) and isinstance(result, ij.AxisRangeSpacingResult)
    assert (estimate.width, estimate.height, estimate.plane_count, estimate.samples,
            estimate.vertical_count, estimate.last_selected, estimate.effective_first,
            estimate.effective_last) == (2, 5, 2, 1, 5, 1, 0, 1)
    assert (estimate.source_vertical_spacing, estimate.voxel_step, tuple(estimate.output_axes)) == (1., .5, ("X", "Z", "Y"))
    axes = estimate.output_axes
    axes[0] = "bad"
    assert tuple(estimate.output_axes) == ("X", "Z", "Y")
    assert result.working_bytes == estimate.working_bytes and result.vertical_count == 5
    returned = result.request
    returned.physical_vertical_spacing = 9
    nested_orientation = returned.orientation
    nested_orientation.transpose = True
    returned.orientation = nested_orientation
    assert result.request.physical_vertical_spacing == .5 and result.request.orientation.transpose is False
    geometry = result.first_plane_geometry
    geometry["origin"][0] = 999
    assert result.first_plane_geometry["origin"] == [0., 0., 0.]
    for name in ("width", "height", "plane_count", "samples", "vertical_count", "last_selected",
                 "effective_first", "effective_last", "source_vertical_spacing", "voxel_step",
                 "output_axes", "working_bytes"):
        with pytest.raises(AttributeError):
            setattr(estimate, name, 0)
    for name in ("dataset", "request", "first_plane_geometry", "working_bytes",
                 "source_vertical_spacing", "voxel_step", "vertical_count"):
        with pytest.raises(AttributeError):
            setattr(result, name, 0)


@pytest.mark.parametrize("plane", ("xz", "yz"))
@pytest.mark.parametrize("sampling", ("nearest", "linear"))
@pytest.mark.parametrize("dtype,samples", PROFILES, ids=("u8", "f32", "rgb3", "rgba4"))
def test_literal_nearest_and_linear_samples_and_types(plane, sampling, dtype, samples):
    pixels, dataset = source(dtype, samples)
    result = ij.reslice_spaced_axis_range(dataset, request(plane, 1, 1, h=.5, sampling=sampling))
    levels = (0, 10, 10, 20, 20) if sampling == "nearest" else (0, 5, 10, 15, 20)
    horizontal = (2, 3) if plane == "xz" else (1, 3)
    expected = np.array([[[level + a + s for s in range(samples)] for a in horizontal]
                         for level in levels], dtype=dtype)[None, ...]
    actual = result.dataset.numpy("ZYXS")
    assert actual.shape == (1, 5, 2, samples) and actual.dtype == np.dtype(dtype)
    assert raw(actual) == raw(expected) and raw(dataset.numpy("TCZYXS")) == raw(pixels)
    assert tuple(result.dataset.shape) == (1, 1, 1, 5, 2, samples)
    assert (result.source_vertical_spacing, result.voxel_step, result.vertical_count) == (1., .5, 5)


@pytest.mark.parametrize("plane", ("xz", "yz"))
@pytest.mark.parametrize("sampling", ("nearest", "linear"))
def test_selected_T_C_signed_shear_world_coordinates_and_first_output_geometry(plane, sampling):
    calibration = ij.SpatialCalibration(spacing=(.5, 1.25, 2.), origin=(11., -7., 19.),
        direction=(-1., .25, 0., 0., 1., -.6, .25, 0., .8), unit="um",
        frame_interval=.75, time_origin=2.25, time_unit="s")
    pixels, dataset = source(np.float64, scientific=True, calibration=calibration)
    req = request(plane, 0, 2, 2, h=1., sampling=sampling, flags=(True, True, True), t=1, c=2)
    out = ij.reslice_spaced_axis_range(dataset, req)
    z_values = (2, 2, 1, 1, 0) if sampling == "nearest" else (2., 1.5, 1., .5, 0.)
    width = 4 if plane == "xz" else 3
    expected = np.array([[[1200 + 10 * z + (2 * fixed + a if plane == "xz" else 2 * a + fixed)
                          for z in z_values] for a in range(width)] for fixed in (2, 0)], dtype=np.float64)[..., None]
    assert raw(out.dataset.numpy("ZYXS")) == raw(expected)
    assert raw(dataset.numpy("TCZYXS")) == raw(pixels)
    cal = out.dataset.calibration
    horizontal_axis, fixed_axis = (0, 1) if plane == "xz" else (1, 0)
    direction = np.array([[-1., .25, 0.], [0., 1., -.6], [.25, 0., .8]])
    expected_direction = direction[:, (2, horizontal_axis, fixed_axis)] * np.array([-1., 1., -1.])
    assert cal.spacing == [1., (.5, 1.25)[horizontal_axis], (.5, 1.25)[fixed_axis] * 2]
    np.testing.assert_array_equal(np.array(cal.direction).reshape(3, 3), expected_direction)
    assert (cal.time_origin, cal.frame_interval, cal.unit, cal.time_unit) == (3., .75, "um", "s")
    assert out.first_plane_geometry["horizontal_axis"] == "Z"
    assert out.first_plane_geometry["vertical_axis"] == "XY"[horizontal_axis]
    assert out.first_plane_geometry["origin"] == cal.origin
    origin = np.array([11., -7., 19.])
    for k in (0, 1):
        for v in range(width):
            for u in range(5):
                q = np.zeros(3)
                q[horizontal_axis], q[fixed_axis], q[2] = v, 2 - 2 * k, 2 - .5 * u
                expected_world = origin + direction @ (q * np.array([.5, 1.25, 2.]))
                np.testing.assert_allclose(cal.voxel_to_world((u, v, k)), expected_world, rtol=0, atol=1e-12)
    np.testing.assert_allclose(out.first_plane_geometry["horizontal_step"], (0., .6, -.8), rtol=0, atol=1e-15)
    np.testing.assert_array_equal(out.first_plane_geometry["vertical_step"], direction[:, horizontal_axis] * (.5, 1.25)[horizontal_axis])


@pytest.mark.parametrize("profile", ("f64_fractional", "f64_signed_zero", "canonical_f32"))
@pytest.mark.parametrize("mode", ("nearest", "linear", "equal_linear"))
def test_public_typed_values_exact_fractional_signed_zero_endpoint_and_equal_copies(profile, mode):
    # Noncanonical typed working doubles cannot be constructed through these
    # public Python interfaces; their exact copy obligations remain native.
    if profile == "f64_fractional":
        dtype, levels = np.float64, (1.25, 3.25, 5.25)
        nearest, linear = (1.25, 3.25, 3.25, 5.25, 5.25), (1.25, 2.25, 3.25, 4.25, 5.25)
    elif profile == "f64_signed_zero":
        dtype, levels = np.float64, (-0., 0., -0.)
        nearest, linear = (-0., 0., 0., -0., -0.), (-0., -0., 0., 0., -0.)
    else:
        dtype, levels = np.float32, (1.125, 3.125, 5.125)
        nearest, linear = (1.125, 3.125, 3.125, 5.125, 5.125), (1.125, 2.125, 3.125, 4.125, 5.125)
    if mode == "equal_linear":
        levels = (levels[0],) * 3
        expected = (levels[0],) * 5
    else:
        expected = nearest if mode == "nearest" else linear
    pixels = np.array(levels, dtype=dtype).reshape(3, 1, 1)
    dataset = ij.Dataset(pixels, axes="ZYX")
    assert raw(dataset.numpy("ZYX")) == raw(pixels)
    out = ij.reslice_spaced_axis_range(dataset, request("xz", 0, 0, h=.5,
        sampling="nearest" if mode == "nearest" else "linear"))
    literal = np.array(expected, dtype=dtype).reshape(1, 5, 1, 1)
    assert out.dataset.dtype == np.dtype(dtype) and raw(out.dataset.numpy("ZYXS")) == raw(literal)
    assert raw(dataset.numpy("ZYX")) == raw(pixels)


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_complete_nested_request_and_active_owner_are_captured_before_callbacks(wrapper, callback):
    _, dataset = source(np.uint16)
    app, owner, other = controller(dataset)
    before, other_before, calls = state(owner), state(other), []
    req = request("xz", 1, 1)
    def mutate(*args):
        calls.append(args)
        req.orientation = ij.AxisRangeOrientationRequest(ij.AxisRangeResliceRequest("xy", -1, -1, 0))
        req.physical_vertical_spacing = float("nan")
        req.sampling = ij.AxisRangeSampling.Linear
        app.activate(other.id)
    result = (app.prepare_axis_range_spacing(req, **{callback: mutate})
              if wrapper == "application_prepare" else invoke(wrapper, app, owner, req, **{callback: mutate}))
    assert calls and math.isnan(req.physical_vertical_spacing)
    if wrapper == "reslice":
        output = result.dataset
        assert result.request.orientation.range.plane == "xz"
        assert result.request.physical_vertical_spacing == .5 and result.request.sampling == ij.AxisRangeSampling.Nearest
        assert app.active_document_id == other.id
    else:
        candidate = result.prepared_new_document()
        assert not app.contains(candidate.id) and app.active_document_id == other.id
        assert result.target.document_id == owner.id
        assert result.operation == "axis_range_spacing" and result.parameters["physical_vertical_spacing"] == .5
        assert result.parameters["plane"] == "XZ" and result.parameters["sampling"] == "nearest"
        assert app.publish(result).document is candidate
        output = candidate.snapshot().dataset
    expected = np.array([[2, 3], [12, 13], [12, 13], [22, 23], [22, 23]], dtype=np.uint16).reshape(1, 5, 2, 1)
    assert raw(output.numpy("ZYXS")) == raw(expected)
    unchanged(owner, before)
    unchanged(other, other_before)


@pytest.mark.parametrize("wrapper", ("snapshot_prepare", "application_prepare"))
def test_callback_source_edit_does_not_retarget_captured_pixels_and_rejects_publish(wrapper):
    _, dataset = source(np.uint16)
    app, owner, other = controller(dataset)
    calls = []
    def mutate(done, _total):
        calls.append(done)
        if len(calls) == 1:
            edit = owner.begin_edit()
            edit.replace_plane(np.full((2, 2), 99, dtype=np.uint16), z=0)
            owner.commit(edit)
            app.activate(other.id)
    value = invoke(wrapper, app, owner, request("xz", 1, 1), progress=mutate)
    candidate = value.prepared_new_document()
    expected = np.array([[2, 3], [12, 13], [12, 13], [22, 23], [22, 23]], dtype=np.uint16).reshape(1, 5, 2, 1)
    assert raw(candidate.snapshot().dataset.numpy("ZYXS")) == raw(expected)
    current, ids, active = state(owner), app.document_ids, app.active_document_id
    with pytest.raises(ij.RevisionConflict):
        app.publish(value)
    assert app.document_ids == ids and app.active_document_id == active == other.id
    assert not app.contains(candidate.id) and value.active
    unchanged(owner, current)


INVALID = (
    ({"h": 0.}, ValueError), ({"h": -1.}, ValueError), ({"h": float("nan")}, ValueError),
    ({"h": float("inf")}, ValueError), ({"h": 2.**-100}, ValueError), ({"plane": "xy"}, ValueError),
    ({"first": -1}, IndexError), ({"last": 3}, IndexError), ({"step": 0}, ValueError),
    ({"t": 2}, IndexError), ({"c": 3}, IndexError), ({"max_working_bytes": 1}, ValueError),
)


@pytest.mark.parametrize("patch,error", INVALID, ids=("zero_h", "negative_h", "nan_h", "infinite_h", "huge_grid",
    "xy_not_admitted", "negative_first", "last_outside", "zero_step", "time_outside", "channel_outside", "memory_refusal"))
def test_invalid_metadata_rejects_before_progress_and_preserves_state(patch, error):
    _, dataset = source(np.uint16, scientific=True)
    app, owner, other = controller(dataset)
    before, other_before, ids, active = state(owner), state(other), app.document_ids, app.active_document_id
    values = {"first": 0, "last": 2, "step": 2, "t": 1, "c": 2}
    values.update(patch)
    req = request(**values)
    calls = []
    with pytest.raises(error):
        ij.estimate_axis_range_spacing(dataset, req)
    with pytest.raises(error):
        ij.reslice_spaced_axis_range(dataset, req, progress=lambda *a: calls.append(a))
    with pytest.raises(error):
        app.prepare_axis_range_spacing(req, owner.id, progress=lambda *a: calls.append(a))
    assert calls == [] and app.document_ids == ids and app.active_document_id == active
    unchanged(owner, before)
    unchanged(other, other_before)


def quiet_dataset():
    bits = np.array([0, 0x80000000, 0x7FC01234, 0xFFC34567, 0x3F800000, 1], dtype=np.uint32).reshape(3, 1, 2)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    return bits, ij.Dataset(bits.view(np.float32), axes="ZYX", sample_policy=policy)


def test_quiet_nearest_preserves_payload_and_signed_zero_bits():
    bits, dataset = quiet_dataset()
    out = ij.reslice_spaced_axis_range(dataset, request("xz", 0, 0, h=.5))
    expected = bits[[0, 1, 1, 2, 2], 0, :][None, :, :, None]
    assert out.dataset.sample_policy == ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    assert raw(out.dataset.numpy("ZYXS").view(np.uint32)) == raw(expected)
    assert raw(dataset.numpy("ZYX").view(np.uint32)) == raw(bits)


@pytest.mark.parametrize("wrapper", WRAPPERS)
def test_quiet_linear_rejects_before_progress_and_publication(wrapper):
    bits, dataset = quiet_dataset()
    app, owner, other = controller(dataset)
    before, other_before, ids, active = state(owner), state(other), app.document_ids, app.active_document_id
    req = request("xz", 0, 0, sampling="linear")
    calls = []
    with pytest.raises(ValueError):
        ij.estimate_axis_range_spacing(dataset, req)
    with pytest.raises(ValueError):
        invoke(wrapper, app, owner, req, progress=lambda *a: calls.append(a))
    assert calls == [] and app.document_ids == ids and app.active_document_id == active
    unchanged(owner, before)
    unchanged(other, other_before)
    assert raw(dataset.numpy("ZYX").view(np.uint32)) == raw(bits)


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_callback_type_and_exception_are_atomic(wrapper, callback):
    _, dataset = source()
    app, owner, other = controller(dataset)
    before, other_before, ids, active = state(owner), state(other), app.document_ids, app.active_document_id
    with pytest.raises(ValueError, match="callbacks must be callable"):
        invoke(wrapper, app, owner, request(), **{callback: []})
    def fail(*_):
        raise RuntimeError("independent spacing callback failure")
    with pytest.raises(RuntimeError, match="independent spacing callback failure"):
        invoke(wrapper, app, owner, request(), **{callback: fail})
    assert app.document_ids == ids and app.active_document_id == active
    unchanged(owner, before)
    unchanged(other, other_before)


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("phase", ("before", "positive_work"))
def test_cancel_before_or_during_work_keeps_membership_history_and_source(wrapper, phase):
    _, dataset = source()
    app, owner, other = controller(dataset)
    before, other_before, ids, active = state(owner), state(other), app.document_ids, app.active_document_id
    token, events = ij.CancellationToken(), []
    if phase == "before":
        token.cancel()
    def cancel(done, total):
        if events:
            assert total == events[-1][1] and done >= events[-1][0]
        events.append((done, total))
        if done > 0:
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        invoke(wrapper, app, owner, request(), cancellation=token, progress=cancel)
    assert token.canceled and (events == [] if phase == "before" else any(done > 0 for done, _ in events))
    assert not any(done == total for done, total in events)
    assert app.document_ids == ids and app.active_document_id == active
    unchanged(owner, before)
    unchanged(other, other_before)


@pytest.mark.parametrize("mutation", ("pixels", "calibration", "closed", "candidate_pixels"))
def test_stale_closed_or_modified_candidate_rejects_without_adoption(mutation):
    _, dataset = source()
    app, owner, other = controller(dataset)
    value = app.prepare_axis_range_spacing(request(), owner.id)
    candidate = value.prepared_new_document()
    app.activate(other.id)
    if mutation == "closed":
        app.close(owner.id, discard=True)
    elif mutation == "calibration":
        edit = owner.begin_edit()
        edit.set_calibration(ij.SpatialCalibration(spacing=(1., 1., 2.)))
        owner.commit(edit)
    else:
        target = candidate if mutation == "candidate_pixels" else owner
        frame = target.snapshot().dataset.plane("xy", 0).copy()
        frame[0, 0] = 7
        edit = target.begin_edit()
        edit.replace_plane(frame, z=0)
        target.commit(edit)
    before, other_before, ids, active = state(owner), state(other), app.document_ids, app.active_document_id
    expected_error = ij.DocumentUnavailable if mutation == "closed" else ij.RevisionConflict
    with pytest.raises(expected_error):
        app.publish(value)
    assert app.document_ids == ids and app.active_document_id == active == other.id
    assert not app.contains(candidate.id) and value.active
    unchanged(owner, before)
    unchanged(other, other_before)


def test_exact_fresh_owner_annotations_captured_selection_display_and_single_publish():
    _, dataset = source()
    app, owner, other = controller(dataset)
    edit = owner.begin_edit()
    edit.set_property("source_only", [1, 2])
    edit.set_extension("source_extension", {"kept": True})
    edit.set_record_layout({"opaque_source": [1]})
    edit.set_rois([ij.ROI("stored", ij.Selection("rectangle", ((0., 0.), (1., 1.))), z=1)])
    edit.set_overlays([{"kind": "text", "text": "source-only"}])
    edit.set_spectrum(ij.FourierState(np.ones((4, 4), dtype=np.complex128), original_shape=(2, 2)))
    owner.commit(edit)
    value = app.prepare_axis_range_spacing(request(), owner.id)
    candidate = value.prepared_new_document()
    snap = candidate.snapshot()
    assert value.operation == "axis_range_spacing" and candidate.id != owner.id and not app.contains(candidate.id)
    assert snap.data_identity == candidate.undo_steps == candidate.redo_steps == 0 and snap.dirty is False
    assert snap.selection is snap.previous_selection is snap.spectrum is None
    assert snap.rois == snap.overlays == [] and snap.record_layout == {} and snap.metadata["properties"] == {}
    assert set(snap.metadata["extensions"]) == {"axis_range_spacing"}
    provenance = snap.metadata["extensions"]["axis_range_spacing"]
    assert (provenance["source_document_id"], provenance["sampling"], provenance["physical_vertical_spacing"],
            provenance["vertical_count"], provenance["t"], provenance["c"]) == (owner.id, "nearest", .5, 5, 0, 0)
    edit = owner.begin_edit()
    edit.select(ij.Selection("rectangle", ((0., 0.), (1., 1.))))
    owner.commit(edit)
    display = owner.snapshot().display
    display.crosshair = [1, 1, 2]
    owner.set_display(display)
    app.activate(other.id)
    before, other_before, ids = state(owner), state(other), app.document_ids
    published = app.publish(value)
    assert published.document is candidate and published.document_id == candidate.id and published.created
    assert app.document_ids == ids + [candidate.id] and app.active_document_id == candidate.id and not value.active
    unchanged(owner, before)
    unchanged(other, other_before)
    assert candidate.snapshot().selection is candidate.snapshot().previous_selection is None
    assert candidate.undo_steps == candidate.redo_steps == 0 and candidate.snapshot().dirty is False
    with pytest.raises(ValueError, match="already published"):
        app.publish(value)
    assert app.document_ids == ids + [candidate.id]


def test_live_document_facade_forwards_spacing_and_captured_owner_without_processing():
    from imagejxx.ui.app import LiveDocument
    sentinel = object()
    class Recorder:
        calls = None
        def action(self, name, **values):
            self.calls = name, values
        def live_document(self):
            return sentinel
    recorder = Recorder()
    assert LiveDocument(recorder, 71).reslice_spaced_range("yz", 1, 7, 3,
        physical_vertical_spacing=.25, sampling="linear", reverse_order=True,
        flip_vertical=True, transpose=True) is sentinel
    assert recorder.calls == ("reslice_spaced_axis_range", {"plane": "yz", "first": 1, "last": 7, "step": 3,
        "physical_vertical_spacing": .25, "sampling": "linear", "reverse_order": True,
        "flip_vertical": True, "transpose": True, "document_id": 71})
