"""Prospective public binding tests; no ImageJ48 reference or UI runtime claim.

Expected pixels use independent literal NumPy indexing only in tests. Processing,
geometry and publication are exercised through the C++ API after root adoption.
"""

import imagejxx as ij
import numpy as np
import pytest


WRAPPERS = ("reslice", "snapshot_prepare", "application_prepare")
AXES = {"xy": (0, 1, 2), "xz": (0, 2, 1), "yz": (1, 2, 0)}


def source(dtype=np.float64, samples=1):
    t, c, z, y, x, s = np.indices((2, 3, 4, 5, 6, samples), dtype=np.int64)
    values = (103 * t + 37 * c + 23 * z + 5 * y + 3 * x + s) % 251
    if dtype == np.uint16:
        values += 40000
    elif dtype in (np.float32, np.float64):
        values = values - 125.25
    pixels = values.astype(dtype)
    calibration = ij.SpatialCalibration(
        spacing=(.5, 1.25, 2), origin=(11, -7, 19),
        direction=(-1, .25, .5, 0, 1, .5, .5, 0, 1),
        unit="um", frame_interval=.75, time_origin=2.25, time_unit="s",
    )
    return pixels, ij.Dataset(pixels, axes="TCZYXS", calibration=calibration)


def literal_range(pixels, plane, coordinates, t=1, c=2):
    volume = pixels[t, c]
    if plane == "xy":
        return np.stack([volume[k, :, :, :] for k in coordinates])
    if plane == "xz":
        return np.stack([volume[:, k, :, :] for k in coordinates])
    assert plane == "yz"
    return np.stack([volume[:, :, k, :] for k in coordinates])


def raw(array):
    value = np.asarray(array)
    return np.ascontiguousarray(value.astype(value.dtype.newbyteorder("<"))).tobytes()


def invoke(wrapper, app, owner, request, **controls):
    if wrapper == "reslice":
        return ij.reslice_axis_range(owner.snapshot().dataset, request, **controls)
    if wrapper == "snapshot_prepare":
        return ij.prepare_axis_range_reslice(owner.snapshot(), request, **controls)
    assert wrapper == "application_prepare"
    return app.prepare_axis_range_reslice(request, owner.id, **controls)


@pytest.mark.parametrize("plane", ("xy", "xz", "yz"))
@pytest.mark.parametrize("dtype", (np.uint8, np.uint16, np.float32, np.float64))
@pytest.mark.parametrize("samples", (1, 3, 4))
def test_typed_selected_range_and_full_geometry(plane, dtype, samples):
    pixels, dataset = source(dtype, samples)
    n = {"xy": 4, "xz": 5, "yz": 6}[plane]
    request = ij.AxisRangeResliceRequest(plane, 1, n - 1, 2, t=1, c=2)
    result = ij.reslice_axis_range(dataset, request)
    coordinates = tuple(range(1, n, 2))
    expected = literal_range(pixels, plane, coordinates)
    assert isinstance(result, ij.AxisRangeResliceResult)
    actual = result.dataset.numpy("ZYXS")
    assert actual.dtype == pixels.dtype and actual.shape == expected.shape
    assert raw(actual) == raw(expected)
    assert raw(dataset.numpy("TCZYXS")) == raw(pixels)
    assert tuple(result.dataset.shape) == (1, 1, *expected.shape)
    original, calibration = dataset.calibration, result.dataset.calibration
    mapping = AXES[plane]
    assert calibration.spacing == [original.spacing[mapping[0]], original.spacing[mapping[1]],
                                   original.spacing[mapping[2]] * 2]
    direction = np.asarray(original.direction).reshape(3, 3)
    assert np.array_equal(np.asarray(calibration.direction).reshape(3, 3), direction[:, mapping])
    assert (calibration.time_origin, calibration.frame_interval, calibration.unit, calibration.time_unit) == (3, .75, "um", "s")
    first_voxel = np.zeros(3)
    first_voxel[mapping[2]] = 1
    assert calibration.origin == original.voxel_to_world(first_voxel.tolist())
    for k, fixed in enumerate(coordinates):
        point = np.zeros(3)
        point[mapping[0]], point[mapping[1]], point[mapping[2]] = 2, 1, fixed
        assert calibration.voxel_to_world((2, 1, k)) == original.voxel_to_world(point.tolist())
    assert result.first_plane_geometry["origin"] == calibration.origin


def test_request_defaults_and_result_values_are_detached():
    request = ij.AxisRangeResliceRequest()
    assert (request.plane, request.first, request.last, request.step, request.t, request.c,
            request.max_working_bytes) == ("xz", 0, 0, 1, 0, 0, 512 * 1024 * 1024)
    _, dataset = source()
    result = ij.reslice_axis_range(dataset)
    result.request.step = 0
    geometry = result.first_plane_geometry
    geometry["origin"][0] = 999
    assert result.request.step == 1
    assert result.first_plane_geometry["origin"][0] == 11
    with pytest.raises(AttributeError):
        result.working_bytes = 0
    with pytest.raises(ValueError, match="Plane must be"):
        ij.AxisRangeResliceRequest("oblique")


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_request_copied_before_reentrant_python_callbacks(wrapper, callback):
    pixels, dataset = source(np.uint16)
    owner, app = ij.Document(dataset, "Source"), ij.Application()
    app.adopt(owner)
    before = owner.snapshot()
    request = ij.AxisRangeResliceRequest("xy", 0, 3, 2, t=1, c=2)
    calls = []

    def mutate(*args):
        calls.append(args)
        request.plane, request.first, request.last, request.step = "yz", -1, -1, 0
        request.t, request.c, request.max_working_bytes = 0, 0, 0

    value = invoke(wrapper, app, owner, request, **{callback: mutate})
    assert calls and request.step == 0
    if wrapper == "reslice":
        output = value.dataset
        assert (value.request.plane, value.request.first, value.request.last, value.request.step,
                value.request.t, value.request.c) == ("xy", 0, 3, 2, 1, 2)
    else:
        candidate = value.prepared_new_document()
        assert value.parameters["first"] == 0 and value.parameters["last"] == 3
        assert value.parameters["step"] == value.parameters["count"] == 2
        assert candidate.snapshot().metadata["title"] == "Source XY 1-4 step 2"
        assert not app.contains(candidate.id)
        assert app.publish(value).document is candidate and not value.active
        output = candidate.snapshot().dataset
    assert raw(output.numpy("ZYXS")) == raw(literal_range(pixels, "xy", (0, 2)))
    assert owner.snapshot().shares_data(before)
    assert raw(owner.snapshot().dataset.numpy("TCZYXS")) == raw(pixels)


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("callback", ("progress", "checkpoint"))
def test_control_noncallable_and_python_exception_do_not_publish(wrapper, callback):
    _, dataset = source()
    owner, app = ij.Document(dataset), ij.Application()
    app.adopt(owner)
    before, ids = owner.snapshot(), app.document_ids
    request = ij.AxisRangeResliceRequest("xy", 0, 3, 2)
    with pytest.raises(ValueError, match="callbacks must be callable"):
        invoke(wrapper, app, owner, request, **{callback: []})

    def fail(*_):
        raise RuntimeError("independent callback failure")

    with pytest.raises(RuntimeError, match="independent callback failure"):
        invoke(wrapper, app, owner, request, **{callback: fail})
    assert app.document_ids == ids and owner.snapshot().shares_data(before)


@pytest.mark.parametrize("fields,error", [
    ({"first": -1}, IndexError), ({"first": 3, "last": 1}, IndexError),
    ({"last": 4}, IndexError), ({"step": 0}, ValueError), ({"step": -1}, ValueError),
    ({"t": -1}, IndexError), ({"t": 2}, IndexError), ({"c": -1}, IndexError),
    ({"c": 3}, IndexError), ({"max_working_bytes": 0}, ValueError),
    ({"max_working_bytes": 1}, ValueError),
])
def test_native_range_validation_precedes_progress(fields, error):
    pixels, dataset = source()
    values = {"first": 0, "last": 3, "step": 1, "t": 1, "c": 2}
    values.update(fields)
    request = ij.AxisRangeResliceRequest("xy", **values)
    calls = []
    with pytest.raises(error):
        ij.reslice_axis_range(dataset, request, progress=lambda *args: calls.append(args))
    assert calls == [] and raw(dataset.numpy("TCZYXS")) == raw(pixels)


@pytest.mark.parametrize("plane", ("xy", "xz", "yz"))
def test_quiet_nan_and_signed_zero_policy_passes_through_bindings(plane):
    bits = np.array([0, 0x80000000, 1, 0x7FC01234, 0xFFC34567, 0x3F800000], dtype=np.uint32)
    source_bits = bits[np.arange(120) % 6].reshape(4, 5, 6)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    dataset = ij.Dataset(source_bits.view(np.float32), axes="ZYX", sample_policy=policy)
    n = {"xy": 4, "xz": 5, "yz": 6}[plane]
    result = ij.reslice_axis_range(dataset, ij.AxisRangeResliceRequest(plane, 1, n - 1, 2))
    independent = literal_range(source_bits[None, None, ..., None], plane, range(1, n, 2), t=0, c=0)
    assert result.dataset.sample_policy == policy
    assert np.array_equal(result.dataset.numpy("ZYXS").view(np.uint32), independent)
    assert np.array_equal(dataset.numpy("ZYX").view(np.uint32), source_bits)


@pytest.mark.parametrize("wrapper", WRAPPERS)
@pytest.mark.parametrize("phase", ("before", "positive_work"))
def test_cancellation_preserves_source_and_membership(wrapper, phase):
    pixels, dataset = source()
    owner, app = ij.Document(dataset), ij.Application()
    app.adopt(owner)
    before, ids, active = owner.snapshot(), app.document_ids, app.active_document_id
    token, calls = ij.CancellationToken(), []
    if phase == "before":
        token.cancel()

    def cancel(done, total):
        calls.append((done, total))
        assert total == 8
        if done > 0:
            token.cancel()

    with pytest.raises(ij.OperationCancelled):
        invoke(wrapper, app, owner, ij.AxisRangeResliceRequest("xz", 1, 3, 2),
               cancellation=token, progress=cancel)
    assert token.canceled and (not calls if phase == "before" else any(done > 0 for done, _ in calls))
    assert app.document_ids == ids and app.active_document_id == active
    assert owner.snapshot().shares_data(before) and raw(owner.snapshot().dataset.numpy("TCZYXS")) == raw(pixels)


def test_live_document_dispatch_is_zero_based_and_owner_bound():
    from imagejxx.ui.app import LiveDocument

    sentinel = object()

    class Recorder:
        calls = None
        reads = 0
        fail = False

        def action(self, action, **parameters):
            self.calls = action, parameters
            if self.fail:
                raise RuntimeError("native action rejected")

        def live_document(self):
            self.reads += 1
            return sentinel

    editor = Recorder()
    live = LiveDocument(editor, 71)
    assert live.reslice_range("yz", 1, 4, 3) is sentinel
    assert editor.calls == ("reslice_axis_range", {"plane": "yz", "first": 1, "last": 4, "step": 3, "document_id": 71})
    editor.fail = True
    with pytest.raises(RuntimeError, match="native action rejected"):
        live.reslice_range("xy", -1, 4, 0)
    assert editor.calls[1]["first"] == -1 and editor.calls[1]["step"] == 0
    assert editor.reads == 1
