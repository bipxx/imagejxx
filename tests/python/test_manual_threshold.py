"""Manual threshold public API checks using literal raw intensity expectations."""

import copy

import imagejxx as ij
import numpy as np
import pytest


DTYPES = (np.uint8, np.uint16, np.float32, np.float64)


def assert_histogram(summary, sample_count):
    histogram = summary.histogram
    counts, edges = histogram["counts"], histogram["edges"]
    assert len(counts) == 256 and len(edges) == 257
    assert all(isinstance(value, int) and value >= 0 for value in counts)
    assert sum(counts) == summary.sample_count == sample_count
    assert np.isfinite(edges).all()
    assert all(left <= right for left, right in zip(edges, edges[1:]))


@pytest.mark.parametrize("dtype", DTYPES)
def test_inclusive_raw_interval_histogram_mask_and_detached_results(dtype):
    pixels = np.array([[0, 1, 2], [3, 4, 5]], dtype=dtype)
    original = pixels.copy()
    interval = ij.ManualThresholdRange(1, 4)
    summary = ij.analyze_manual_threshold(pixels, interval)
    assert_histogram(summary, 6)
    assert (summary.minimum, summary.maximum, summary.matched_count) == (0, 5, 4)
    mask = ij.manual_threshold_mask(pixels, interval)
    assert mask.dtype == np.uint8 and mask.shape == (2, 3)
    np.testing.assert_array_equal(mask, [[0, 255, 255], [255, 255, 0]])
    np.testing.assert_array_equal(pixels, original)
    histogram = summary.histogram
    histogram["counts"][0] = 99999
    histogram["edges"][0] = 99999
    assert summary.histogram["counts"][0] != 99999
    assert summary.histogram["edges"][0] != 99999
    mask[:] = 17
    np.testing.assert_array_equal(pixels, original)
    if dtype is np.uint8:
        assert summary.histogram["edges"][::256] == [0, 256]
        assert summary.histogram["counts"][:6] == [1] * 6
    elif dtype is np.uint16:
        assert summary.histogram["edges"][::256] == [0, 65536]
        assert summary.histogram["counts"][0] == 6
    else:
        assert summary.histogram["edges"][::256] == [0, 5]


def test_uint16_histogram_stays_bounded_and_uses_raw_bin_boundaries():
    pixels = np.array([[0, 255, 256], [32768, 65280, 65535]], np.uint16)
    summary = ij.analyze_manual_threshold(pixels, ij.ManualThresholdRange(255, 65280))
    assert_histogram(summary, 6)
    counts = summary.histogram["counts"]
    assert (counts[0], counts[1], counts[128], counts[255]) == (2, 1, 1, 2)
    assert summary.matched_count == 4
    np.testing.assert_array_equal(
        ij.manual_threshold_mask(pixels, ij.ManualThresholdRange(255, 65280)),
        [[0, 255, 255], [255, 255, 0]],
    )


@pytest.mark.parametrize("dtype", (np.float32, np.float64))
def test_negative_floating_data_uses_raw_extrema_and_inclusive_bounds(dtype):
    pixels = np.array([[-8, -4, -2], [-1, -0.5, 0]], dtype)
    interval = ij.ManualThresholdRange(-4, -0.5)
    summary = ij.analyze_manual_threshold(pixels, interval)
    assert_histogram(summary, 6)
    assert (summary.minimum, summary.maximum, summary.matched_count) == (-8, 0, 4)
    assert summary.histogram["edges"][::256] == [-8, 0]
    np.testing.assert_array_equal(ij.manual_threshold_mask(pixels, interval),
                                  [[0, 255, 255], [255, 255, 0]])


@pytest.mark.parametrize("value", (-np.finfo(np.float64).max, -3.5,
                                   0.0, np.finfo(np.float64).max))
def test_constant_float64_including_finite_extremes_has_explicit_single_bin(value):
    pixels = np.full((2, 3), value, np.float64)
    summary = ij.analyze_manual_threshold(pixels, ij.ManualThresholdRange(value, value))
    assert_histogram(summary, 6)
    assert summary.minimum == summary.maximum == value
    assert summary.matched_count == 6
    assert summary.histogram["counts"] == [6] + [0] * 255
    assert summary.histogram["edges"] == [value] * 257


def test_float64_cross_zero_extreme_range_keeps_finite_edges_and_all_samples():
    maximum = np.finfo(np.float64).max
    pixels = np.array([[-maximum, -maximum / 2, 0],
                       [maximum / 2, np.nextafter(maximum, 0), maximum]], np.float64)
    summary = ij.analyze_manual_threshold(
        pixels, ij.ManualThresholdRange(-maximum / 2, maximum / 2)
    )
    assert_histogram(summary, 6)
    assert (summary.minimum, summary.maximum, summary.matched_count) == (-maximum, maximum, 3)
    assert summary.histogram["counts"][0] == 1
    assert summary.histogram["counts"][-1] == 2


def test_subnormal_and_adjacent_extreme_float64_intervals_are_admitted():
    tiny = np.nextafter(np.float64(0), np.float64(1))
    maximum = np.finfo(np.float64).max
    for pixels in (np.array([[0, tiny, tiny * 2]], np.float64),
                   np.array([[np.nextafter(maximum, 0), maximum, maximum]], np.float64)):
        summary = ij.analyze_manual_threshold(
            pixels, ij.ManualThresholdRange(float(pixels.min()), float(pixels.max()))
        )
        assert_histogram(summary, 3)
        assert summary.matched_count == 3


def test_selection_preserves_holes_and_explicit_empty_membership():
    pixels = np.array([[5, 5, 5], [5, 0, 5], [5, 5, 5]], np.uint8)
    selection = ij.manual_threshold_selection(pixels, ij.ManualThresholdRange(5, 5))
    assert selection.kind == "mask"
    np.testing.assert_array_equal(selection.mask((3, 3)),
                                  [[255, 255, 255], [255, 0, 255], [255, 255, 255]])
    empty = ij.manual_threshold_selection(pixels, ij.ManualThresholdRange(6, 7))
    assert empty is not None and empty.kind == "mask"
    np.testing.assert_array_equal(empty.mask((3, 3)), np.zeros((3, 3), np.uint8))
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    before = owner.snapshot()
    owner.select(selection)
    after = owner.snapshot()
    assert after.revisions.selection == before.revisions.selection + 1
    assert after.revisions.pixels == before.revisions.pixels
    assert not after.dirty and owner.undo_steps == 0
    np.testing.assert_array_equal(after.dataset.numpy("YX"), pixels)


def test_metadata_estimate_counts_only_selected_scalar_plane():
    pixels = np.arange(2 * 3 * 4 * 2 * 3, dtype=np.uint16).reshape(2, 3, 4, 2, 3)
    dataset = ij.Dataset(pixels, axes="TCZYX")
    limits = ij.ManualThresholdLimits()
    limits.max_pixels = 6
    estimate = ij.estimate_manual_threshold(dataset, ij.PlanePosition(1, 2, 3), limits)
    assert (estimate.pixel_count, estimate.analysis_bytes,
            estimate.mask_bytes, estimate.selection_bytes) == (6, 4104, 48, 1)
    with pytest.raises(IndexError):
        ij.estimate_manual_threshold(dataset, ij.PlanePosition(2, 0, 0))
    with pytest.raises(IndexError):
        ij.estimate_manual_threshold(dataset, ij.PlanePosition(0, 0, 4))
    color = ij.Dataset(np.zeros((2, 3, 3), np.uint8), axes="YXS")
    with pytest.raises(ValueError):
        ij.estimate_manual_threshold(color)


def test_independent_operation_byte_bounds_and_exact_pixel_bound():
    pixels = np.arange(6, dtype=np.uint8).reshape(2, 3)
    interval = ij.ManualThresholdRange(0, 5)
    limits = ij.ManualThresholdLimits()
    limits.max_pixels = 5
    with pytest.raises(ValueError):
        ij.analyze_manual_threshold(pixels, interval, limits)
    limits.max_pixels = 6
    limits.max_working_bytes = 47
    with pytest.raises(ValueError):
        ij.manual_threshold_mask(pixels, interval, limits)
    limits.max_working_bytes = 48
    np.testing.assert_array_equal(ij.manual_threshold_mask(pixels, interval, limits), 255)
    limits.max_working_bytes = 1
    selection = ij.manual_threshold_selection(pixels, interval, limits)
    np.testing.assert_array_equal(selection.mask((2, 3)), 255)
    limits.max_working_bytes = 4103
    with pytest.raises(ValueError):
        ij.analyze_manual_threshold(pixels, interval, limits)
    limits.max_working_bytes = 4104
    assert_histogram(ij.analyze_manual_threshold(pixels, interval, limits), 6)
    limits.max_working_bytes = 0
    with pytest.raises(ValueError):
        ij.manual_threshold_selection(pixels, interval, limits)


@pytest.mark.parametrize("bounds", ((2, 1), (np.nan, 1), (0, np.nan),
                                    (-np.inf, 0), (0, np.inf)))
def test_invalid_or_nonfinite_range_is_rejected(bounds):
    with pytest.raises(ValueError):
        ij.ManualThresholdRange(*bounds)


def test_mutated_range_is_revalidated_before_processing():
    interval = ij.ManualThresholdRange(0, 1)
    interval.upper = -1
    with pytest.raises(ValueError):
        ij.analyze_manual_threshold(np.zeros((2, 3), np.uint8), interval)


@pytest.mark.parametrize("dtype", (np.float32, np.float64))
@pytest.mark.parametrize("value", (np.nan, np.inf, -np.inf))
def test_nonfinite_pixels_are_rejected_by_every_manual_operation(dtype, value):
    pixels = np.array([[0, value, 1]], dtype)
    interval = ij.ManualThresholdRange(0, 1)
    for operation in (ij.analyze_manual_threshold, ij.manual_threshold_mask,
                      ij.manual_threshold_selection):
        with pytest.raises(ValueError):
            operation(pixels, interval)


@pytest.mark.parametrize("pixels", (
    np.zeros((2, 3, 3), np.uint8), np.zeros((2, 3, 4), np.uint8),
    np.zeros((2, 3, 2), np.uint8), np.zeros((2, 3), np.int16),
    np.zeros((2, 3), bool), np.zeros((0, 3), np.uint8),
    np.zeros((1, 2, 3, 4), np.float32),
))
def test_color_invalid_extent_dimension_and_unsupported_dtype_rejected(pixels):
    with pytest.raises(ValueError):
        ij.analyze_manual_threshold(pixels, ij.ManualThresholdRange(0, 1))


def test_noncontiguous_readonly_and_endian_input_use_logical_source_coordinates():
    values = np.array([[0, 1000, 2000], [3000, 4000, 65535]], dtype=">u2")
    pixels = values[:, ::-1]
    pixels.flags.writeable = False
    expected = np.array([[255, 255, 0], [0, 255, 255]], np.uint8)
    np.testing.assert_array_equal(
        ij.manual_threshold_mask(pixels, ij.ManualThresholdRange(1000, 4000)), expected
    )


def test_typed_display_state_roundtrip_is_detached_and_display_only():
    dataset = ij.Dataset(np.zeros((2, 3, 4, 2, 3), np.uint16), axes="TCZYX")
    owner = ij.Document(dataset)
    original = owner.snapshot()
    view = original.display
    view.properties = {"lut": "Fire", "range": [0, 65535]}
    interval = ij.ManualThresholdRange(1200, 4500)
    enabled = ij.set_manual_threshold_state(view, dataset, interval, ij.PlanePosition(1, 2, 3))
    assert "manual_threshold" not in view.properties
    assert enabled.properties["manual_threshold"] == {
        "version": 1, "lower": 1200, "upper": 4500, "t": 1, "c": 2, "z": 3,
    }
    owner.set_display(enabled)
    current = owner.snapshot()
    state = ij.manual_threshold_state(current.display, dataset)
    assert (state.range.lower, state.range.upper) == (1200, 4500)
    assert (state.position.t, state.position.c, state.position.z) == (1, 2, 3)
    detached_range = state.range
    detached_range.lower = -999
    assert ij.manual_threshold_state(current.display, dataset).range.lower == 1200
    assert current.shares_data(original) and current.data_identity == original.data_identity
    assert not current.dirty and owner.undo_steps == owner.redo_steps == 0
    assert current.revisions.display == original.revisions.display + 1
    assert current.revisions.pixels == current.revisions.metadata == current.revisions.selection == 0
    disabled = ij.reset_manual_threshold_state(current.display)
    assert ij.manual_threshold_state(disabled, dataset) is None
    assert disabled.properties == {"lut": "Fire", "range": [0, 65535]}
    assert ij.manual_threshold_state(current.display, dataset) is not None


@pytest.mark.parametrize("patch", (
    {"version": 2}, {"version": True}, {"c": 0.0}, {"t": -1},
    {"lower": True}, {"lower": "0"}, {"upper": -1}, {"extra": 0},
))
def test_strict_display_property_schema_rejected_without_live_mutation(patch):
    dataset = ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX")
    owner = ij.Document(dataset)
    before = owner.snapshot()
    record = {"version": 1, "lower": 0, "upper": 1, "t": 0, "c": 0, "z": 0}
    record.update(patch)
    view = before.display
    view.properties = {"manual_threshold": record}
    with pytest.raises((ValueError, IndexError)):
        ij.manual_threshold_state(view, dataset)
    with pytest.raises((ValueError, IndexError)):
        owner.set_display(view)
    assert owner.snapshot().shares_data(before)
    assert owner.snapshot().display.properties == before.display.properties
    assert owner.snapshot().revisions.display == before.revisions.display


@pytest.mark.parametrize("property_value", (True, None, [], "threshold",
    {"version": 1, "lower": 0, "upper": 1, "t": 0, "c": 0}))
def test_missing_or_nonobject_display_property_is_not_silently_disabled(property_value):
    dataset = ij.Dataset(np.zeros((2, 3), np.uint8), axes="YX")
    view = ij.DisplayState()
    view.properties = {"manual_threshold": copy.deepcopy(property_value)}
    with pytest.raises(ValueError):
        ij.manual_threshold_state(view, dataset)


@pytest.mark.parametrize("name", ("analyze_manual_threshold", "manual_threshold_mask",
                                   "manual_threshold_selection"))
def test_precanceled_and_inside_kernel_cancellation_preserve_input(name):
    pixels = np.full((2, 4096), 17, np.uint8)
    interval = ij.ManualThresholdRange(17, 17)
    operation = getattr(ij, name)
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        operation(pixels, interval, cancellation=token)
    token = ij.CancellationToken()
    visits = []

    def checkpoint():
        visits.append(len(visits))
        if len(visits) == 5:
            token.cancel()

    with pytest.raises(ij.OperationCancelled):
        operation(pixels, interval, cancellation=token, checkpoint=checkpoint)
    assert len(visits) == 5
    np.testing.assert_array_equal(pixels, 17)


def test_callback_exception_propagates_and_source_array_is_frozen_before_callback():
    pixels = np.array([[0, 1, 2], [3, 4, 5]], np.uint8)
    called = []

    def mutate_input():
        if not called:
            called.append(True)
            pixels[:] = 255

    mask = ij.manual_threshold_mask(pixels, ij.ManualThresholdRange(1, 4),
                                    checkpoint=mutate_input)
    np.testing.assert_array_equal(mask, [[0, 255, 255], [255, 255, 0]])
    assert called == [True]

    def broken():
        raise RuntimeError("manual threshold callback failure")

    with pytest.raises(RuntimeError, match="manual threshold callback failure"):
        ij.analyze_manual_threshold(pixels, ij.ManualThresholdRange(0, 255),
                                     checkpoint=broken)


@pytest.mark.parametrize("dtype", DTYPES)
def test_application_apply_previews_then_publishes_and_undoes_full_plane(dtype):
    pixels = np.array([[0, 1, 2], [3, 4, 5]], dtype)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    before = owner.snapshot()
    prepared = app.prepare("threshold", app.resolve_target(operation="threshold"),
                           {"lower": 1, "upper": 4})
    expected = np.array([[0, 255, 255], [255, 255, 0]], np.uint8)
    preview = prepared.preview.dataset.numpy("YX")
    assert preview.dtype == np.uint8
    np.testing.assert_array_equal(preview, expected)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    app.publish(prepared)
    assert owner.undo_steps == 1 and owner.snapshot().dirty
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), expected)
    app.undo()
    restored = owner.snapshot().dataset.numpy("YX")
    assert restored.dtype == pixels.dtype
    np.testing.assert_array_equal(restored, pixels)
    assert not owner.snapshot().dirty


def test_apply_current_plane_roi_preserves_holes_untargeted_planes_and_source_type():
    pixels = np.array([[[700, 700, 700], [700, 700, 700], [700, 700, 700]],
                       [[5, 5, 5], [5, 900, 5], [5, 5, 5]]], np.uint16)
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX"))
    app = ij.Application()
    app.adopt(owner)
    roi = np.ones((3, 3), np.uint8)
    roi[1, 1] = 0
    owner.select(ij.Selection.from_mask(roi))
    view = owner.snapshot().display
    view.crosshair = [0, 0, 1]
    owner.set_display(view)
    target = app.resolve_target(operation="threshold")
    prepared = app.prepare("threshold", target, {"lower": 5, "upper": 5})
    expected = pixels.copy()
    expected[1][roi != 0] = 255
    assert prepared.preview.dataset.numpy("ZYX").dtype == np.uint16
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("ZYX"), expected)
    app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.undo_steps == 1
