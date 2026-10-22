"""Native scalar Apply foundation; independent full-domain ImageJ output hashes."""
import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

RGB_FIXTURE = json.loads((Path(__file__).parents[1] / "fixtures/rgb-contrast-apply-imagej154r.json").read_text())


def rgb_options(minimum=10.9, maximum=40.1, channels=7):
    result = ij.RgbContrastApplyOptions()
    result.minimum, result.maximum, result.channels = minimum, maximum, channels
    return result


def rgb_input():
    x = np.arange(256, dtype=np.uint32)
    pixels = np.empty((2, 256, 3), np.uint8)
    for y in range(2):
        pixels[y, :, 0], pixels[y, :, 1] = x, 255 - x
        pixels[y, :, 2] = (73 * x + 19 * y) & 255
    return pixels


@pytest.mark.parametrize("row", RGB_FIXTURE["rows"], ids=lambda r: f'rgb-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}')
def test_actual_imagej_rgb_preview_commit_and_next_baseline(row):
    source = rgb_input()
    original = source.copy()
    low, high = RGB_FIXTURE["ranges"][row["range_index"]]
    opts = rgb_options(low, high, row["channels"])
    owner = ij.Document(ij.Dataset(source, axes="YXS"))
    if row["roi"]:
        mask = np.zeros((2, 256), np.uint8)
        mask[:, 17:236] = 1
        if row["roi"] == 2:
            y, x = np.indices(mask.shape)
            mask[(x + y) % 3 == 0] = 0
        owner.select(ij.Selection.from_mask(mask))
    view = owner.snapshot().display
    view.properties = {"display_range": [low, high], "lut": "Grays"}
    owner.set_display(view)
    before = owner.snapshot()
    target = ij.resolve_target(before)
    estimate = ij.estimate_rgb_contrast_apply(target, opts)
    assert estimate.pixels == 512 and estimate.samples == 1536
    prepared = ij.prepare_rgb_contrast_apply(target, opts)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    owner.commit(prepared)
    applied = owner.snapshot().dataset.numpy("YXS")
    assert hashlib.sha256(applied.tobytes()).hexdigest() == row["sha256"]
    assert owner.snapshot().display.properties == {"display_range": [0., 255.], "lut": "Grays"}
    assert owner.undo_steps == 1
    if not row["roi"]:
        np.testing.assert_array_equal(applied, ij.apply_rgb_contrast_imagej(source, opts))
    second = ij.apply_rgb_contrast_imagej(applied, opts)
    assert hashlib.sha256(second.tobytes()).hexdigest() == row["next_sha256"]
    np.testing.assert_array_equal(source, original)
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"), original)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"), applied)


@pytest.mark.parametrize("scope", ["current", "all_z", "ranges", "positions"])
@pytest.mark.parametrize("roi", ["whole", "active", "positioned"])
@pytest.mark.parametrize("channels", [1, 4, 7])
def test_rgb_transaction_targets_scientific_axes_separately_from_components(scope, roi, channels):
    from test_native_workflow import contrast_target
    pixels = np.tile(np.array([[[10, 20, 30], [20, 30, 40], [30, 40, 50],
                               [40, 50, 60], [50, 60, 70], [60, 70, 80]]], np.uint8), (2, 2, 2, 2, 1, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    owner.select(ij.Selection.from_mask(np.array([[0, 0, 1, 1, 0, 0]] * 2, np.uint8)))
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("rgb", ij.Selection("rectangle", [(1, 0), (5, 2)]), t=1, c=1, z=0)])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [0, 0, 1]
    view.properties = {"display_range": [10., 40.], "lut": "retained"}
    owner.set_display(view)
    before, undo = owner.snapshot(), owner.undo_steps
    request, positions = contrast_target(scope, roi)
    prepared = ij.prepare_rgb_contrast_apply(ij.resolve_target(before, request), rgb_options(10, 40, channels))
    expected = pixels.copy()
    changed = False
    for position in positions:
        if roi == "positioned" and position != (1, 1, 0): continue
        changed = True
        columns = slice(None) if roi == "whole" else slice(2, 4) if roi == "active" else slice(1, 5)
        for c, bit in enumerate([4, 2, 1]):
            if channels & bit:
                expected[position][:, columns, c] = np.clip(np.trunc((pixels[position][:, columns, c].astype(float) - 10) * 256 / 30), 0, 255)
    assert owner.snapshot().shares_data(before)
    owner.commit(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"), expected)
    assert owner.undo_steps == undo + changed
    assert owner.snapshot().display.properties["lut"] == "retained"
    assert owner.snapshot().display.properties["display_range"] == ([0., 255.] if changed else [10., 40.])


@pytest.mark.parametrize("bad", [0, 8, -1, 2**40, True, np.bool_(True), 1.5, "red"])
def test_rgb_component_mask_validation_is_atomic(bad):
    source = rgb_input()
    opts = rgb_options()
    with pytest.raises((ValueError, TypeError, OverflowError)):
        opts.channels = bad
        ij.apply_rgb_contrast_imagej(source, opts)
    np.testing.assert_array_equal(source, rgb_input())


@pytest.mark.parametrize("low,high", [(float("nan"), 1), (0, float("inf")), (2, 1)])
def test_rgb_invalid_endpoints_refuse_without_mutation(low, high):
    source = rgb_input()
    with pytest.raises(ValueError): ij.apply_rgb_contrast_imagej(source, rgb_options(low, high))
    np.testing.assert_array_equal(source, rgb_input())


@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint8, 4), (np.uint16, 3), (np.float32, 3)])
def test_rgb_profile_refuses_unqualified_sample_formats(dtype, samples):
    source = np.zeros((2, 4, samples), dtype)
    with pytest.raises((ValueError, TypeError)): ij.apply_rgb_contrast_imagej(source)


@pytest.mark.parametrize("failure", ["pixels", "bytes", "volume", "destination", "opaque"])
def test_rgb_admission_precedes_provider_reads(failure):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS").observe_reads(reads))
    request, limits = ij.TargetRequest(), ij.ContrastApplyLimits()
    if failure == "pixels": limits.max_pixels = 1
    elif failure == "bytes": limits.max_working_bytes = 1
    elif failure == "volume": request = ij.TargetRequest(mode=ij.ExecutionMode.VOLUME)
    elif failure == "destination": request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    else:
        view = owner.snapshot().display
        view.properties = {"channel_display": {"future": 1}}
        owner.set_display(view)
    before = owner.snapshot()
    target = ij.resolve_target(before, request)
    for call in [ij.estimate_rgb_contrast_apply, ij.prepare_rgb_contrast_apply]:
        with pytest.raises(ValueError): call(target, rgb_options(), limits)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_rgb_empty_mask_preserves_display_history_and_performs_no_reads():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS").observe_reads(reads))
    owner.select(ij.Selection.from_mask(np.zeros((2, 256), np.uint8)))
    view = owner.snapshot().display
    view.properties = {"display_range": [10., 40.]}
    owner.set_display(view)
    before = owner.snapshot()
    owner.commit(ij.prepare_rgb_contrast_apply(ij.resolve_target(before), rgb_options()))
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    assert owner.snapshot().display.properties == before.display.properties


@pytest.mark.parametrize("failure", ["cancel", "callback", "stale"])
def test_rgb_failure_and_stale_display_reject_atomic_publication(failure):
    owner = ij.Document(ij.Dataset(rgb_input(), axes="YXS"))
    before = owner.snapshot()
    token = ij.CancellationToken()
    def progress(done, total):
        if failure == "cancel": token.cancel()
        elif failure == "callback": raise RuntimeError("controlled RGB callback")
    if failure == "stale":
        edit = ij.prepare_rgb_contrast_apply(ij.resolve_target(before), rgb_options())
        view = before.display
        view.zoom = 2.
        owner.set_display(view)
        with pytest.raises(ij.RevisionConflict): owner.commit(edit)
    else:
        with pytest.raises((RuntimeError, ij.OperationCancelled)):
            ij.prepare_rgb_contrast_apply(ij.resolve_target(before), rgb_options(), cancellation=token, progress=progress)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_rgb_reentrant_controls_cannot_change_captured_mapping():
    source = rgb_input()
    opts = rgb_options()
    expected = ij.apply_rgb_contrast_imagej(source, opts)
    calls = []
    def checkpoint():
        calls.append(True)
        opts.minimum, opts.maximum, opts.channels = 0., 1., 1
    actual = ij.apply_rgb_contrast_imagej(source, opts, checkpoint=checkpoint)
    np.testing.assert_array_equal(actual, expected)
    assert calls

FIXTURE = json.loads((Path(__file__).parents[1] / "fixtures/contrast-apply-imagej154r.json").read_text())


def options(minimum=10.9, maximum=40.9, bits=0):
    value = ij.ContrastApplyOptions()
    value.minimum, value.maximum, value.unsigned16_bits = minimum, maximum, bits
    return value


@pytest.mark.parametrize("case", FIXTURE["rows"], ids=lambda c: f'u{c["depth"]}-b{c["bits"]}-r{c["range_index"]}')
def test_full_domain_actual_imagej_apply(case):
    dtype = np.uint8 if case["depth"] == 8 else np.uint16
    source = np.arange(2**case["depth"], dtype=dtype).reshape(1, -1)
    original = source.copy()
    low, high = FIXTURE["ranges"][case["range_index"]]
    actual = ij.apply_contrast_imagej(source, options(low, high, case["bits"]))
    assert actual.dtype == dtype and actual.shape == source.shape
    assert hashlib.sha256(actual.astype(np.dtype(dtype).newbyteorder('<')).tobytes()).hexdigest() == case["sha256"]
    np.testing.assert_array_equal(source, original)


def test_masked_multiplane_apply_pairs_pixels_and_display_history():
    pixels = np.tile(np.array([10, 20, 30, 40], np.uint16), (2, 1, 1))
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX"))
    owner.select(ij.Selection.from_mask(np.array([[0, 1, 1, 0]], np.uint8)))
    app = ij.Application()
    app.adopt(owner)
    view = owner.snapshot().display
    view.properties = {"display_range": [10., 40.], "retained": {"exact": 42}}
    owner.set_display(view)
    before = owner.snapshot()
    steps = owner.undo_steps
    target = app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z))
    transaction = ij.prepare_contrast_apply(target, options(10, 40, 12))
    assert owner.snapshot().shares_data(before)
    app.commit(transaction)
    expected = np.tile(np.array([10, 1365, 2730, 40], np.uint16), (2, 1, 1))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.snapshot().display.properties == {"display_range": [0., 4095.], "retained": {"exact": 42}}
    assert owner.undo_steps == steps + 1 and owner.snapshot().dirty
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), pixels)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.snapshot().display.properties["display_range"] == [0., 4095.]


def test_admission_and_cancellation_preserve_source():
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.arange(48, dtype=np.uint8).reshape(6, 8), axes="YX").observe_reads(observer))
    app = ij.Application()
    app.adopt(owner)
    target = app.resolve_target()
    before = owner.snapshot()
    limits = ij.ContrastApplyLimits()
    limits.max_working_bytes = 1
    with pytest.raises(ValueError):
        ij.prepare_contrast_apply(target, options(), limits)
    assert observer.counts().plane_reads == 0
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_contrast_apply(target, options(), cancellation=token,
                                  progress=lambda done, total: token.cancel())
    assert owner.snapshot().shares_data(before) and not owner.snapshot().dirty
    transaction = ij.prepare_contrast_apply(target, options())
    view = owner.snapshot().display
    view.zoom = 2.
    owner.set_display(view)
    stable = owner.snapshot()
    with pytest.raises(ij.RevisionConflict):
        app.commit(transaction)
    assert stable.shares_data(owner.snapshot())
    assert owner.snapshot().display.revision == stable.display.revision


@pytest.mark.parametrize("dtype,samples", [(np.float32, 1), (np.float64, 1), (np.uint8, 3), (np.uint8, 4)])
def test_unsupported_formats_refuse_without_input_changes(dtype, samples):
    source = np.ones((2, 3, samples), dtype=dtype)
    with pytest.raises(ValueError):
        ij.apply_contrast_imagej(source, options())
    assert np.all(source == 1)


@pytest.mark.parametrize("value", [True, np.bool_(False), 1.5, -1, 2**100])
@pytest.mark.parametrize("field", ["max_pixels", "max_working_bytes"])
def test_limits_reject_invalid_python_integers(field, value):
    limits = ij.ContrastApplyLimits()
    before = getattr(limits, field)
    with pytest.raises((TypeError, ValueError, OverflowError)):
        setattr(limits, field, value)
    assert getattr(limits, field) == before
