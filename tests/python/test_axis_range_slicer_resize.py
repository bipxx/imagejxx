"""Public native Slicer bindings: literals, detached values and atomic ownership.

Full ImageJ sample equivalence is checked separately against the qualified
240-case reference. These small expectations are independently handwritten.
"""

import itertools

import numpy as np
import pytest


@pytest.fixture(autouse=True)
def native_api():
    global ij
    import imagejxx as ij


def raw(value):
    array = np.asarray(value)
    return np.ascontiguousarray(array.astype(array.dtype.newbyteorder("<"))).tobytes()


def source(dtype=np.uint16, ratio=2., shape=(1, 1, 3, 2, 2, 1), calibration=None):
    t, c, z, y, x, _ = np.indices(shape, dtype=np.int64)
    pixels = (1000 * t + 100 * c + 10 * z + 2 * y + x).astype(dtype)
    cal = calibration or ij.SpatialCalibration(spacing=(1., 2.25, ratio))
    return pixels, ij.Dataset(pixels, axes="TCZYXS", calibration=cal)


def request(plane="xz", first=1, last=1, step=1, *, flags=(False, False, False),
            t=0, c=0, max_working_bytes=512 * 1024 * 1024):
    return ij.AxisRangeSlicerResizeRequest(ij.AxisRangeOrientationRequest(
        ij.AxisRangeResliceRequest(plane, first, last, step, t=t, c=c,
                                  max_working_bytes=max_working_bytes),
        reverse_order=flags[0], flip_vertical=flags[1], transpose=flags[2]))


def controller(dataset):
    app = ij.Application()
    owner, other = ij.Document(dataset, "Source"), ij.Document(dataset, "Other")
    app.adopt(owner)
    app.adopt(other)
    app.activate(owner.id)
    return app, owner, other


def state(owner):
    snap = owner.snapshot()
    return (raw(snap.dataset.numpy("TCZYXS")), snap.metadata, snap.record_layout,
            snap.data_identity, snap.dirty, owner.undo_steps, owner.redo_steps,
            (snap.revisions.pixels, snap.revisions.metadata,
             snap.revisions.selection, snap.revisions.display))


def invoke(wrapper, app, owner, req, **controls):
    if wrapper == "reslice":
        return ij.reslice_slicer_axis_range(owner.snapshot().dataset, req, **controls)
    if wrapper == "snapshot":
        return ij.prepare_axis_range_slicer_resize(owner.snapshot(), req, **controls)
    return app.prepare_axis_range_slicer_resize(req, owner.id, **controls)


def test_detached_request_estimate_result_and_unchanged_old_defaults():
    req = ij.AxisRangeSlicerResizeRequest()
    assert req.orientation.range.plane == "xz" and req.orientation.range.step == 1
    assert ij.AxisRangeSpacingRequest().physical_vertical_spacing == 1.
    assert ij.AxisRangeSpacingRequest().sampling == ij.AxisRangeSampling.Nearest
    nested = req.orientation
    nested.reverse_order = True
    assert req.orientation.reverse_order is False
    req.orientation = nested
    assert req.orientation.reverse_order is True
    _, dataset = source()
    req = request()
    estimate = ij.estimate_axis_range_slicer_resize(dataset, req)
    result = ij.reslice_slicer_axis_range(dataset, req)
    assert (estimate.width, estimate.height, estimate.plane_count,
            estimate.source_vertical_count, estimate.vertical_count) == (2, 6, 1, 3, 6)
    assert estimate.resize_branch == result.resize_branch == ij.AxisRangeSlicerResizeBranch.Bilinear
    assert estimate.processor_vertical_origin == -.25 and estimate.processor_vertical_step == .5
    assert estimate.z_spacing_ratio == 2. and tuple(estimate.scalar_spacing) == (1., 1., 2.25)
    assert result.working_bytes == estimate.working_bytes
    metrics = result.scalar_spacing
    metrics[0] = 99.
    assert tuple(result.scalar_spacing) == (1., 1., 2.25)
    returned = result.request.orientation
    returned.transpose = True
    assert result.request.orientation.transpose is False
    geometry = result.first_plane_geometry
    geometry["origin"][0] = 999.
    assert result.first_plane_geometry["origin"][0] == 0.
    for value, names in ((estimate, ("width", "height", "vertical_count", "scalar_spacing", "resize_branch")),
                         (result, ("dataset", "request", "working_bytes", "scalar_spacing", "resize_branch"))):
        for name in names:
            with pytest.raises(AttributeError):
                setattr(value, name, 0)


@pytest.mark.parametrize("dtype", (np.uint8, np.uint16, np.float32))
def test_handwritten_quarter_center_last_column_clamp_and_unsigned_ties(dtype):
    pixels, dataset = source(dtype)
    out = ij.reslice_slicer_axis_range(dataset, request())
    if dtype == np.float32:
        expected = [[2., 2.999], [4.5, 5.499], [9.5, 10.499],
                    [14.5, 15.499], [19.5, 20.499], [21.99, 22.989]]
    else:
        expected = [[2, 3], [5, 5], [10, 10], [15, 15], [20, 20], [22, 23]]
    assert raw(out.dataset.numpy("ZYXS")) == raw(np.array(expected, dtype=dtype).reshape(1, 6, 2, 1))
    assert raw(dataset.numpy("TCZYXS")) == raw(pixels)


@pytest.mark.parametrize("ratio,branch", ((1., "NoResize"), (1.125, "EqualDimensionCrop")))
@pytest.mark.parametrize("flags", tuple(itertools.product((False, True), repeat=3)))
def test_copy_branches_preserve_signed_zero_subnormal_and_reverse_selected_endpoint(ratio, branch, flags):
    bits = np.resize(np.array([0x80000000, 0, 1, 0x80000001, 0x3f800000, 0xbf800000], dtype=np.uint32),
                     (1, 1, 7, 5, 2, 1))
    dataset = ij.Dataset(bits.view(np.float32), axes="TCZYXS",
                         calibration=ij.SpatialCalibration(spacing=(1., 2.25, ratio)))
    req = request("xz", 1, 4, 2, flags=flags)
    out = ij.reslice_slicer_axis_range(dataset, req)
    expected = bits[0, 0, :, [3, 1] if flags[0] else [1, 3], :, :]
    # NumPy's separated advanced axis is emitted first: K,Z,X,S.
    if flags[1]:
        expected = expected[:, ::-1]
    if flags[2]:
        expected = expected.transpose(0, 2, 1, 3)
    assert raw(out.dataset.numpy("ZYXS").view(np.uint32)) == raw(expected)
    estimate = ij.estimate_axis_range_slicer_resize(dataset, req)
    assert out.resize_branch == getattr(ij.AxisRangeSlicerResizeBranch, branch)
    assert estimate.last_selected == 3 and estimate.effective_first == (3 if flags[0] else 1)
    assert out.processor_vertical_origin == 0. and out.processor_vertical_step == 1.


@pytest.mark.parametrize("plane", ("xz", "yz"))
def test_scientific_time_channel_signed_shear_and_separate_scalar_metric(plane):
    calibration = ij.SpatialCalibration(spacing=(.5, 1.25, 1.), origin=(11., -7., 19.),
        direction=(-1., .25, 0., 0., 1., -.6, .25, 0., .8), unit="um",
        frame_interval=.75, time_origin=2.25, time_unit="s")
    _, dataset = source(np.float32, shape=(2, 3, 3, 3, 4, 1), calibration=calibration)
    out = ij.reslice_slicer_axis_range(dataset, request(plane, 0, 2, 2, flags=(True, True, True), t=1, c=2))
    cal = out.dataset.calibration
    a, fixed = (0, 1) if plane == "xz" else (1, 0)
    direction = np.array(calibration.direction).reshape(3, 3)
    assert cal.spacing == [.5, calibration.spacing[a], 2 * calibration.spacing[fixed]]
    assert tuple(out.scalar_spacing) == (.5, calibration.spacing[a], calibration.spacing[fixed])
    np.testing.assert_array_equal(np.array(cal.direction).reshape(3, 3),
                                  direction[:, (2, a, fixed)] * [-1., 1., -1.])
    assert (cal.time_origin, cal.frame_interval, cal.unit, cal.time_unit) == (3., .75, "um", "s")
    for k in range(2):
        for v in range((4, 3)[a]):
            for u in range(6):
                q = np.zeros(3)
                q[a], q[fixed], q[2] = v, 2 - 2 * k, 2.25 - .5 * u
                world = np.array(calibration.origin) + direction @ (q * calibration.spacing)
                np.testing.assert_allclose(cal.voxel_to_world((u, v, k)), world, rtol=0, atol=1e-12)


@pytest.mark.parametrize("wrapper", ("reslice", "snapshot", "application"))
@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_capture_request_source_and_active_owner_before_callbacks(wrapper, callback):
    _, dataset = source()
    app, owner, other = controller(dataset)
    before, other_before, calls = state(owner), state(other), []
    req = request()
    def mutate(*args):
        calls.append(args)
        req.orientation = ij.AxisRangeOrientationRequest(ij.AxisRangeResliceRequest("xy", -1, -1, 0))
        app.activate(other.id)
    result = (app.prepare_axis_range_slicer_resize(req, **{callback: mutate}) if wrapper == "application"
              else invoke(wrapper, app, owner, req, **{callback: mutate}))
    assert calls and app.active_document_id == other.id
    if wrapper == "reslice":
        assert result.request.orientation.range.plane == "xz"
    else:
        assert result.target.document_id == owner.id and result.parameters["plane"] == "XZ"
        candidate = result.prepared_new_document()
        assert not app.contains(candidate.id)
        assert app.publish(result).document is candidate
    assert state(owner) == before and state(other) == other_before


@pytest.mark.parametrize("patch,error", (({"plane": "xy"}, ValueError), ({"first": -1}, IndexError),
    ({"last": 2}, IndexError), ({"step": 0}, ValueError), ({"t": 1}, IndexError),
    ({"c": 1}, IndexError), ({"max_working_bytes": 1}, ValueError)))
def test_metadata_refusals_precede_callbacks_and_publication(patch, error):
    _, dataset = source()
    app, owner, other = controller(dataset)
    before, other_before, ids, calls = state(owner), state(other), app.document_ids, []
    with pytest.raises(error):
        ij.estimate_axis_range_slicer_resize(dataset, request(**patch))
    with pytest.raises(error):
        app.prepare_axis_range_slicer_resize(request(**patch), owner.id, progress=lambda *a: calls.append(a))
    assert calls == [] and app.document_ids == ids and app.active_document_id == owner.id
    assert state(owner) == before and state(other) == other_before


@pytest.mark.parametrize("wrapper", ("reslice", "snapshot", "application"))
@pytest.mark.parametrize("phase", ("before", "positive_work", "exception"))
def test_cancellation_and_callback_exception_are_atomic(wrapper, phase):
    _, dataset = source()
    app, owner, other = controller(dataset)
    before, other_before, ids = state(owner), state(other), app.document_ids
    token, events = ij.CancellationToken(), []
    if phase == "before":
        token.cancel()
    def progress(done, total):
        events.append((done, total))
        if done:
            if phase == "exception":
                raise RuntimeError("slicer callback failure")
            token.cancel()
    error = RuntimeError if phase == "exception" else ij.OperationCancelled
    with pytest.raises(error):
        invoke(wrapper, app, owner, request(), cancellation=token, progress=progress)
    assert not any(done == total for done, total in events)
    assert app.document_ids == ids and app.active_document_id == owner.id
    assert state(owner) == before and state(other) == other_before


def test_exact_cap_and_one_less_admission():
    _, dataset = source()
    estimate = ij.estimate_axis_range_slicer_resize(dataset, request())
    exact = request(max_working_bytes=estimate.working_bytes)
    assert ij.reslice_slicer_axis_range(dataset, exact).working_bytes == estimate.working_bytes
    with pytest.raises(ValueError):
        ij.estimate_axis_range_slicer_resize(dataset, request(max_working_bytes=estimate.working_bytes - 1))


@pytest.mark.parametrize("mutation", ("pixels", "calibration", "closed", "candidate_pixels"))
def test_stale_closed_or_tampered_candidate_never_adopts(mutation):
    _, dataset = source()
    app, owner, other = controller(dataset)
    value = app.prepare_axis_range_slicer_resize(request(), owner.id)
    candidate = value.prepared_new_document()
    app.activate(other.id)
    if mutation == "closed":
        app.close(owner.id, discard=True)
    elif mutation == "calibration":
        edit = owner.begin_edit()
        edit.set_calibration(ij.SpatialCalibration(spacing=(1., 2.25, 3.)))
        owner.commit(edit)
    else:
        target = candidate if mutation == "candidate_pixels" else owner
        frame = target.snapshot().dataset.plane("xy", 0).copy()
        frame[0, 0] = 7
        edit = target.begin_edit()
        edit.replace_plane(frame, z=0)
        target.commit(edit)
    before, other_before, ids = state(owner), state(other), app.document_ids
    with pytest.raises(ij.DocumentUnavailable if mutation == "closed" else ij.RevisionConflict):
        app.publish(value)
    assert app.document_ids == ids and app.active_document_id == other.id
    assert not app.contains(candidate.id) and value.active
    assert state(owner) == before and state(other) == other_before


def test_fresh_annotation_free_owner_provenance_and_single_publication():
    _, dataset = source()
    app, owner, other = controller(dataset)
    edit = owner.begin_edit()
    edit.set_property("source_only", [1, 2])
    edit.set_extension("source_extension", {"kept": True})
    edit.set_record_layout({"source_layout": [1]})
    edit.set_rois([ij.ROI("stored", ij.Selection("rectangle", ((0., 0.), (1., 1.))), z=1)])
    edit.set_overlays([{"kind": "text", "text": "source-only"}])
    owner.commit(edit)
    value = app.prepare_axis_range_slicer_resize(request(), owner.id)
    candidate = value.prepared_new_document()
    snap = candidate.snapshot()
    assert value.operation == "axis_range_slicer_resize" and not app.contains(candidate.id)
    assert snap.data_identity == candidate.undo_steps == candidate.redo_steps == 0 and snap.dirty is False
    assert snap.selection is snap.previous_selection is snap.spectrum is None
    assert snap.rois == snap.overlays == [] and snap.record_layout == {} and snap.metadata["properties"] == {}
    assert set(snap.metadata["extensions"]) == {"axis_range_slicer_resize"}
    provenance = snap.metadata["extensions"]["axis_range_slicer_resize"]
    assert len(provenance) == 29 and len(value.parameters) == 35
    assert provenance["source_document_id"] == owner.id and provenance["plane"] == "XZ"
    assert provenance["scalar_spacing"] == [1., 1., 2.25]
    before, other_before = state(owner), state(other)
    published = app.publish(value)
    assert published.document is candidate and app.active_document_id == candidate.id and not value.active
    with pytest.raises(ValueError):
        app.publish(value)
    assert state(owner) == before and state(other) == other_before
