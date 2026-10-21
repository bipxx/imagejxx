"""Required installed-native Dataset/ROI histogram checks, without UI credit.

Literal small histograms are independent of the native mapper. Native estimates
exclude frontend Python argument/value-conversion captures and subsequent
getter/representation copies, allocator bookkeeping, and opaque callable-target
internals; they are not process-memory measurements. Core preflight precedes
pixel/provider callbacks and reads.
"""

import ctypes
import gc
import sys
import threading
import weakref

import imagejxx as ij
from imagejxx import _native
import numpy as np
import pytest


ERRORS = (ValueError, TypeError, OverflowError, IndexError)
# Source-frozen membership; root compares actual installed collection to this.
EXPECTED_COLLECTION_COUNT = 94
ROWS = (
    (np.uint8, [0, 1, 1, 127, 255], {0: 1, 1: 2, 127: 1, 255: 1}, 0., 256., 1.),
    (np.uint16, [1000, 1001, 1255], {0: 1, 1: 1, 255: 1}, 1000., 1256., 1.),
    (np.float32, [-2, 0, 2], {0: 1, 128: 1, 255: 1}, -2., 2., 1./64),
    (np.float32, [7, 7, 7], {0: 3}, 7., 7., 0.),
)


def _source(values=(0, 1, 2, 3), dtype=np.uint8):
    return ij.Dataset(np.asarray([values], dtype=dtype), axes="YX")


def _uniform(low=0., high=2., bins=2):
    request = ij.DatasetHistogramRequest()
    request.binning = ij.HistogramBinning.UniformRange
    request.bins = bins
    request.range = (low, high)
    return request


def _arrays(result):
    value = result.histogram
    assert set(value) == {"counts", "edges"}
    counts, edges = value["counts"], value["edges"]
    assert isinstance(counts, np.ndarray) and counts.dtype == np.dtype(np.uint64)
    assert isinstance(edges, np.ndarray) and edges.dtype == np.dtype(np.float64)
    assert counts.shape == (result.request.bins,)
    assert edges.shape == (result.request.bins + 1,)
    assert np.isfinite(edges).all() and np.all(edges[:-1] <= edges[1:])
    assert result.visited_count == result.finite_count + result.nan_count
    assert result.finite_count == result.included_count + result.underflow_count + result.overflow_count
    assert result.included_count == sum(int(n) for n in counts)
    return counts, edges


def _position(position):
    return position.t, position.c, position.z


def test_public_exports_are_actual_native_types_and_defaults():
    for name in ("HistogramBinning", "DatasetHistogramRequest", "DatasetHistogramLimits",
                 "DatasetHistogramEstimate", "DatasetHistogramResult",
                 "estimate_dataset_histogram", "accumulate_dataset_histogram"):
        assert getattr(ij, name) is getattr(_native, name)
    request, limits = ij.DatasetHistogramRequest(), ij.DatasetHistogramLimits()
    assert _position(request.position) == (0, 0, 0)
    assert request.area_roi is None and request.range is None and request.bins == 256
    assert request.binning == ij.HistogramBinning.ProcessorRaw
    assert limits.max_pixels == 16 * 1024 * 1024
    assert limits.max_working_bytes == 128 * 1024 * 1024


def test_optional_typed_bridges_copy_and_clear_without_loss():
    request = ij.DatasetHistogramRequest()
    position = ij.PlanePosition(1, 2, 3)
    request.position = position
    assert _position(request.position) == (1, 2, 3) and request.position is not position
    selection = ij.Selection("rectangle", [(0, 0), (2, 1)], attributes={"keep": [1, 2]})
    request.area_roi = selection
    assert request.area_roi == selection and request.area_roi is not selection
    request.range = np.asarray([np.float32(.5), np.float64(1.25)])
    assert list(request.range) == [.5, 1.25]
    request.area_roi, request.range = None, None
    assert request.area_roi is request.range is None
    limits = ij.DatasetHistogramLimits()
    limits.max_pixels, limits.max_working_bytes = np.uint64(7), np.int64(8192)
    copied = ij.DatasetHistogramLimits(limits)
    limits.max_pixels = 1
    assert copied.max_pixels == 7 and copied.max_working_bytes == 8192
    for field in ("max_pixels", "max_working_bytes"):
        with pytest.raises(ERRORS): setattr(limits, field, -1)


@pytest.mark.parametrize("dtype,values,sparse,low,high,width", ROWS)
def test_processor_raw_literal_rows(dtype, values, sparse, low, high, width):
    source = _source(values, dtype)
    before = source.numpy("YX").tobytes()
    result = ij.accumulate_dataset_histogram(source)
    counts, edges = _arrays(result)
    expected = np.zeros(256, dtype=np.uint64)
    for index, count in sparse.items(): expected[index] = count
    np.testing.assert_array_equal(counts, expected)
    np.testing.assert_array_equal(edges, low + np.arange(257) * width)
    assert edges[0] == low and edges[-1] == high
    assert result.histogram_origin == low and result.bin_size == width
    assert (result.minimum, result.maximum) == (min(values), max(values))
    assert (result.plane_minimum, result.plane_maximum) == (min(values), max(values))
    assert result.visited_count == result.included_count == result.plane_finite_count == len(values)
    assert result.nan_count == result.plane_nan_count == 0 and not result.empty_finite
    assert source.numpy("YX").tobytes() == before


@pytest.mark.parametrize("dtype", (np.uint8, np.uint16, np.float32))
def test_uniform_range_literal_inclusive_upper_and_outliers(dtype):
    values = [0, 1, 2, 3, 4, 5]
    result = ij.accumulate_dataset_histogram(_source(values, dtype), _uniform(1, 4, 3))
    counts, edges = _arrays(result)
    np.testing.assert_array_equal(counts, [1, 1, 2])
    np.testing.assert_array_equal(edges, [1., 2., 3., 4.])
    assert result.minimum == result.plane_minimum == 0
    assert result.maximum == result.plane_maximum == 5
    assert (result.underflow_count, result.overflow_count, result.included_count) == (1, 1, 4)
    assert result.bin_size == 1 and not result.empty_finite


@pytest.mark.parametrize("bins", (1, 2, 256, 65536))
def test_uniform_legal_bin_counts(bins):
    result = ij.accumulate_dataset_histogram(_source([0, 1]), _uniform(0, 1, bins))
    counts, edges = _arrays(result)
    assert counts[0] == (2 if bins == 1 else 1) and counts[-1] == (2 if bins == 1 else 1)
    assert result.included_count == 2 and edges[0] == 0 and edges[-1] == 1


def test_mask_roi_at_nonzero_tcz_uses_same_full_plane_extrema():
    pixels = np.arange(2 * 2 * 3 * 2 * 4, dtype=np.uint16).reshape(2, 2, 3, 2, 4)
    pixels[1, 1, 2] = [[1000, 1001, 1002, 1003], [1252, 1253, 1254, 1255]]
    source = ij.Dataset(pixels, axes="TCZYX")
    request = ij.DatasetHistogramRequest()
    request.position = ij.PlanePosition(1, 1, 2)
    request.area_roi = ij.Selection.from_mask(np.asarray([[1, 0, 1, 0], [0, 1, 0, 1]], np.uint8))
    result = ij.accumulate_dataset_histogram(source, request)
    counts, edges = _arrays(result)
    expected = np.zeros(256, np.uint64)
    expected[[0, 2, 253, 255]] = 1
    np.testing.assert_array_equal(counts, expected)
    np.testing.assert_array_equal(edges, np.arange(1000., 1257.))
    assert _position(result.request.position) == (1, 1, 2)
    assert result.visited_count == result.finite_count == result.included_count == 4
    assert result.plane_finite_count == 8 and result.plane_nan_count == 0
    assert (result.minimum, result.maximum, result.plane_minimum, result.plane_maximum) == (1000, 1255, 1000, 1255)
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()


def test_area_roi_changes_area_extrema_but_not_full_plane_observation():
    source = _source([0, 10, 20, 30], np.uint16)
    request = ij.DatasetHistogramRequest()
    request.area_roi = ij.Selection.from_mask(np.asarray([[0, 1, 1, 0]], np.uint8))
    result = ij.accumulate_dataset_histogram(source, request)
    counts, edges = _arrays(result)
    expected = np.zeros(256, np.uint64)
    expected[[0, 232]] = 1  # floor((20-10) * (256/11))
    np.testing.assert_array_equal(counts, expected)
    assert result.minimum == 10 and result.maximum == 20
    assert result.plane_minimum == 0 and result.plane_maximum == 30
    assert result.visited_count == 2 and result.plane_finite_count == 4
    assert edges[0] == 10 and edges[-1] == 21 and result.bin_size == 11 / 256


@pytest.mark.parametrize("empty_kind", ("mask", "all_nan", "mask_with_nan_outside"))
def test_empty_finite_has_owned_finite_edges_and_nullable_extrema(empty_kind):
    request = ij.DatasetHistogramRequest()
    if empty_kind == "mask":
        source = _source([2, 3], np.uint16)
        request.area_roi = ij.Selection.from_mask(np.zeros((1, 2), np.uint8))
        expected_plane = (2., 3., 2, 0)
    else:
        bits = np.asarray([[0x7FC12345, 0xFFC54321]], np.uint32)
        source = ij.Dataset(bits.view(np.float32), axes="YX",
            sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
        if empty_kind == "mask_with_nan_outside":
            request.area_roi = ij.Selection.from_mask(np.zeros((1, 2), np.uint8))
        expected_plane = (None, None, 0, 2)
    result = ij.accumulate_dataset_histogram(source, request)
    counts, edges = _arrays(result)
    assert not counts.any() and result.empty_finite and result.minimum is result.maximum is None
    assert result.histogram_origin == 0 and result.bin_size == 1 / 256
    np.testing.assert_array_equal(edges, np.arange(257) / 256)
    assert (result.plane_minimum, result.plane_maximum,
            result.plane_finite_count, result.plane_nan_count) == expected_plane
    assert result.visited_count == result.nan_count == (2 if empty_kind == "all_nan" else 0)


def test_quiet_nan_payloads_signed_zero_and_roi_counts_preserve_source_bits():
    bits = np.asarray([[0x7FC12345, 0x80000000, 0x3F800000, 0xFFC54321]], np.uint32)
    source = ij.Dataset(bits.view(np.float32), axes="YX",
        sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    request = _uniform(0, 1, 2)
    request.area_roi = ij.Selection.from_mask(np.asarray([[1, 1, 1, 0]], np.uint8))
    result = ij.accumulate_dataset_histogram(source, request)
    counts, edges = _arrays(result)
    np.testing.assert_array_equal(counts, [1, 1])
    np.testing.assert_array_equal(edges, [0., .5, 1.])
    assert (result.visited_count, result.finite_count, result.nan_count) == (3, 2, 1)
    assert (result.plane_finite_count, result.plane_nan_count) == (2, 2)
    assert result.minimum == 0 and np.signbit(result.minimum) and result.maximum == 1
    assert source.numpy("YX").view(np.uint32).tobytes() == bits.tobytes()


@pytest.mark.parametrize("kind", ("huge", "tiny"))
def test_uniform_unrepresentable_width_is_nullable_with_finite_edges(kind):
    if kind == "huge":
        limit = sys.float_info.max
        request, source, expected, expected_edges = _uniform(-limit, limit, 1), _source([0, 1]), [2], [-limit, limit]
    else:
        smallest = float.fromhex("0x0.0000000000001p-1022")
        request, source, expected, expected_edges = _uniform(0, smallest, 2), _source([0, 1]), [1, 0], [0., 0., smallest]
    result = ij.accumulate_dataset_histogram(source, request)
    counts, edges = _arrays(result)
    assert result.bin_size is None
    np.testing.assert_array_equal(counts, expected)
    np.testing.assert_array_equal(edges, expected_edges)
    assert result.overflow_count == (0 if kind == "huge" else 1)


def test_request_and_returned_arrays_and_provenance_are_detached():
    request = _uniform(0, 4, 2)
    request.area_roi = ij.Selection("rectangle", [(0, 0), (4, 1)], attributes={"nested": {"keep": [1, 2]}})
    copy_request = ij.DatasetHistogramRequest(request)
    request.bins, request.range, request.area_roi = 1, [0, 1], None
    assert copy_request.bins == 2 and list(copy_request.range) == [0., 4.]
    result = ij.accumulate_dataset_histogram(_source(), copy_request)
    first = result.histogram
    first["counts"][:] = 99
    first["edges"][:] = -99
    first["extra"] = True
    returned = result.request
    returned.bins, returned.range, returned.area_roi = 1, None, None
    attributes = result.request.area_roi.attributes
    attributes["nested"]["keep"].clear()
    bounds = result.request.area_roi.points
    bounds.clear()
    separate_range = result.request.range
    separate_range[0] = -100
    counts, edges = _arrays(result)
    np.testing.assert_array_equal(counts, [2, 2])
    np.testing.assert_array_equal(edges, [0., 2., 4.])
    assert result.request.bins == 2 and list(result.request.range) == [0., 4.]
    assert result.request.area_roi.attributes == {"nested": {"keep": [1, 2]}}
    assert result.request.area_roi.points == [[0., 0.], [4., 1.]]
    with pytest.raises(AttributeError): result.estimate.pixel_count = 1
    with pytest.raises(AttributeError): result.minimum = -1


@pytest.mark.parametrize("field", ("bins", "max_pixels", "max_working_bytes"))
@pytest.mark.parametrize("value", (True, False, np.bool_(True), 1., 1.5, 1 << 100))
def test_integral_setters_reject_booleans_floats_and_overflow(field, value):
    target = ij.DatasetHistogramRequest() if field == "bins" else ij.DatasetHistogramLimits()
    before = getattr(target, field)
    with pytest.raises(ERRORS): setattr(target, field, value)
    assert getattr(target, field) == before


@pytest.mark.parametrize("field", ("bins", "max_pixels", "max_working_bytes"))
def test_integral_setters_accept_numpy_integers(field):
    target = ij.DatasetHistogramRequest() if field == "bins" else ij.DatasetHistogramLimits()
    setattr(target, field, np.int64(17))
    assert getattr(target, field) == 17


@pytest.mark.parametrize("value", ([True, 1], [0, np.bool_(True)], [0, float("inf")],
                                  [float("nan"), 1], [0], [0, 1, 2], "01", [0, 2**53 + 1]))
def test_range_bridge_rejects_bad_endpoints_without_mutation(value):
    request = _uniform()
    with pytest.raises(ERRORS): request.range = value
    assert list(request.range) == [0., 2.]


@pytest.mark.parametrize("value", ([0, 1 + 2j], [np.complex64(1 + 2j), 2], [0, np.complex128(1 + 2j)]))
def test_range_bridge_rejects_complex_endpoints_without_processing(value):
    request = _uniform()
    with pytest.raises(TypeError): request.range = value
    assert list(request.range) == [0., 2.]


@pytest.mark.parametrize("field,value", (("position", (0, 0, 0)), ("area_roi", {}),
                                         ("binning", "ProcessorRaw")))
def test_typed_request_fields_refuse_wrong_bridges(field, value):
    with pytest.raises(ERRORS): setattr(ij.DatasetHistogramRequest(), field, value)


@pytest.mark.parametrize("defect", ("raw_bins", "raw_range", "uniform_missing", "bins_zero", "bins_large", "equal", "reverse", "t", "c", "z"))
def test_native_request_refusals_have_no_callbacks(defect):
    request, calls = ij.DatasetHistogramRequest(), []
    if defect == "raw_bins": request.bins = 128
    elif defect == "raw_range": request.range = [0, 1]
    elif defect == "uniform_missing": request.binning = ij.HistogramBinning.UniformRange
    elif defect in ("bins_zero", "bins_large", "equal", "reverse"):
        request = _uniform()
        if defect == "bins_zero": request.bins = 0
        elif defect == "bins_large": request.bins = 65537
        elif defect == "equal": request.range = [1, 1]
        else: request.range = [2, 1]
    else: request.position = ij.PlanePosition(**{defect: 1})
    with pytest.raises(ERRORS): ij.estimate_dataset_histogram(_source(), request)
    with pytest.raises(ERRORS):
        ij.accumulate_dataset_histogram(_source(), request,
            checkpoint=lambda: calls.append("checkpoint"), progress=lambda *p: calls.append(p))
    assert calls == []


@pytest.mark.parametrize("kind", ("line", "polyline", "angle", "point", "multipoint", "unknown"))
def test_service_rejects_nonarea_without_consumer_fallback(kind):
    request, calls = ij.DatasetHistogramRequest(), []
    request.area_roi = ij.Selection(kind, [(0, 0), (1, 0), (1, 1)])
    with pytest.raises(ValueError):
        ij.accumulate_dataset_histogram(_source(), request, checkpoint=lambda: calls.append(True))
    assert not calls


@pytest.mark.parametrize("profile", ("float64", "rgb", "rgba"))
def test_unsupported_storage_is_not_silently_coerced(profile):
    source = (_source(dtype=np.float64) if profile == "float64" else
              ij.Dataset(np.zeros((2, 3, 3 if profile == "rgb" else 4), np.uint8), axes="YXS"))
    calls = []
    with pytest.raises(ValueError):
        ij.accumulate_dataset_histogram(source, checkpoint=lambda: calls.append(True))
    assert not calls


def test_metadata_estimate_exact_arrays_mask_and_admission_boundary():
    source, request = _source(), ij.DatasetHistogramRequest()
    plain = ij.estimate_dataset_histogram(source, request)
    assert plain.pixel_count == 4 and plain.plane_bytes == 32
    assert plain.mask_bytes == plain.selection_bytes == 0
    assert plain.histogram_bytes == 256 * 8 + 257 * 8
    assert plain.working_bytes >= plain.plane_bytes + plain.histogram_bytes
    request.area_roi = ij.Selection.from_mask(np.ones((1, 4), np.uint8)).with_attributes({"retained": "a" * 4096})
    masked = ij.estimate_dataset_histogram(source, request)
    assert masked.mask_bytes == 32 and masked.selection_bytes >= 4096
    assert masked.working_bytes >= plain.working_bytes + masked.mask_bytes + masked.selection_bytes
    limits = ij.DatasetHistogramLimits()
    limits.max_pixels, limits.max_working_bytes = 4, masked.working_bytes
    actual = ij.accumulate_dataset_histogram(source, request, limits)
    assert actual.estimate.working_bytes == masked.working_bytes
    limits.max_working_bytes -= 1
    calls = []
    with pytest.raises(ValueError):
        ij.accumulate_dataset_histogram(source, request, limits, checkpoint=lambda: calls.append(True))
    assert calls == []


@pytest.mark.parametrize("field,value", (("max_pixels", 0), ("max_pixels", 3), ("max_working_bytes", 0), ("max_working_bytes", 1)))
def test_limit_refusals_precede_callbacks(field, value):
    limits, calls = ij.DatasetHistogramLimits(), []
    setattr(limits, field, value)
    with pytest.raises(ValueError):
        ij.accumulate_dataset_histogram(_source(), limits=limits,
            checkpoint=lambda: calls.append(True), progress=lambda *p: calls.append(p))
    assert calls == []


def test_precancellation_has_no_callbacks():
    token, calls = ij.CancellationToken(), []
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.accumulate_dataset_histogram(_source(), cancellation=token,
            checkpoint=lambda: calls.append(True), progress=lambda *p: calls.append(p))
    assert token.canceled and calls == []


@pytest.mark.parametrize("stage", (1, 3, 5))
def test_midoperation_checkpoint_cancellation_preserves_source(stage):
    source, token, calls = _source([0, 1, 2], np.float32), ij.CancellationToken(), []
    before = source.numpy("YX").tobytes()
    def checkpoint():
        calls.append(True)
        if len(calls) == stage: token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.accumulate_dataset_histogram(source, cancellation=token, checkpoint=checkpoint)
    assert len(calls) == stage and source.numpy("YX").tobytes() == before


@pytest.mark.parametrize("hook", ("checkpoint", "progress_start", "progress_finish"))
def test_python_callback_exception_aborts_detached_work(hook):
    source, seen = _source(), []
    class CallbackFailure(RuntimeError): pass
    def checkpoint(): raise CallbackFailure("histogram checkpoint")
    def progress(done, total):
        seen.append((done, total))
        if done == (0 if hook == "progress_start" else 1):
            raise CallbackFailure("histogram progress")
    kwargs = {"checkpoint": checkpoint} if hook == "checkpoint" else {"progress": progress}
    before = source.numpy("YX").tobytes()
    with pytest.raises(CallbackFailure): ij.accumulate_dataset_histogram(source, **kwargs)
    assert seen == ([] if hook == "checkpoint" else [(0, 1)] if hook == "progress_start" else [(0, 1), (1, 1)])
    assert source.numpy("YX").tobytes() == before


@pytest.mark.parametrize("hook", ("progress", "checkpoint"))
def test_callbacks_must_be_callable(hook):
    with pytest.raises(ValueError): ij.accumulate_dataset_histogram(_source(), **{hook: 7})


def test_callbacks_have_gil_reenter_and_keep_captured_inputs_alive():
    source, request, limits = _source(), _uniform(0, 4, 2), ij.DatasetHistogramLimits()
    request.area_roi = ij.Selection.from_mask(np.asarray([[1, 0, 1, 0]], np.uint8))
    owners, calls, progress, nested = {"source": source, "request": request, "limits": limits}, [], [], []
    def checkpoint():
        assert ctypes.pythonapi.PyGILState_Check() == 1
        if not calls:
            request.position, request.bins, request.range, request.area_roi = ij.PlanePosition(99, 0, 0), 1, [90, 100], None
            limits.max_pixels = limits.max_working_bytes = 1
            owners.clear()
            gc.collect()
            nested.append(ij.estimate_dataset_histogram(source).pixel_count)
            nested.append(int(ij.accumulate_dataset_histogram(_source([9])).histogram["counts"][9]))
        calls.append(True)
    def report(done, total):
        assert ctypes.pythonapi.PyGILState_Check() == 1
        progress.append((done, total))
    actual = ij.accumulate_dataset_histogram(source, request, limits,
        checkpoint=checkpoint, progress=report)
    counts, edges = _arrays(actual)
    np.testing.assert_array_equal(counts, [1, 1])
    np.testing.assert_array_equal(edges, [0., 2., 4.])
    assert calls and nested == [4, 1] and progress == [(0, 1), (1, 1)]
    assert _position(actual.request.position) == (0, 0, 0) and actual.request.area_roi is not None
    assert source.numpy("YX").tolist() == [[0, 1, 2, 3]] and not owners


def test_retained_python_callbacks_are_released_after_native_return():
    class Callback:
        def __init__(self): self.calls = 0
        def __call__(self, *args): self.calls += 1
    callback = Callback()
    reference = weakref.ref(callback)
    ij.accumulate_dataset_histogram(_source(), checkpoint=callback, progress=callback)
    assert callback.calls >= 4
    del callback
    gc.collect()
    assert reference() is None


def test_native_scan_releases_gil_without_python_callbacks():
    source = ij.Dataset(np.zeros((1024, 1024), np.uint8), axes="YX")
    ready, go, ran = threading.Event(), threading.Event(), threading.Event()
    def worker():
        ready.set()
        if go.wait(5): ran.set()
    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    assert ready.wait(2)
    previous = sys.getswitchinterval()
    try:
        # Suppress an ordinary bytecode timeslice between go.set() and the
        # native call. The already waiting worker needs the native GIL release.
        sys.setswitchinterval(10)
        go.set()
        result = ij.accumulate_dataset_histogram(source)
        observed = ran.is_set()
    finally:
        sys.setswitchinterval(previous)
        go.set()
        thread.join(timeout=2)
    assert not thread.is_alive() and observed
    assert result.included_count == 1024 * 1024


def test_read_only_analysis_preserves_document_calibration_history_and_display():
    calibration = ij.SpatialCalibration(spacing=(.5, 1.25, 2.5), origin=(11, -7, 19),
        unit="um", frame_interval=.75, time_origin=2.25, time_unit="s")
    source = ij.Dataset(np.asarray([[0, 1, 2, 3]], np.uint16), axes="YX", calibration=calibration)
    owner = ij.Document(source, "Histogram immutable source")
    edit = owner.begin_edit()
    edit.set_property("retained", {"exact": 2**64 - 1})
    edit.select(ij.Selection("rectangle", [(0, 0), (4, 1)]))
    edit.set_previous_selection(ij.Selection("point", [(1, 0)]))
    edit.set_overlays([{"name": "kept", "points": [[1, 0]]}])
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("redo", True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.properties = {"display_range": [10., 20.], "unrelated": {"keep": True}}
    owner.set_display(view)
    def facts():
        snap, history = owner.snapshot(), owner.history()
        cal = snap.dataset.calibration
        return (snap.data_identity, snap.dirty, snap.metadata, snap.selection, snap.previous_selection,
            snap.overlays, snap.display.properties, snap.dataset.numpy("YX").tobytes(),
            tuple(getattr(snap.revisions, key) for key in ("pixels", "metadata", "selection", "display")),
            (history.undo_steps, history.redo_steps),
            tuple(getattr(cal, key) for key in ("spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit")))
    before = facts()
    request = ij.DatasetHistogramRequest()
    request.area_roi = owner.snapshot().selection
    actual = ij.accumulate_dataset_histogram(owner.snapshot().dataset, request)
    assert actual.included_count == 4 and actual.minimum == 0 and actual.maximum == 3
    assert facts() == before
