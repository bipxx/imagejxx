"""Native scalar Apply foundation; independent full-domain ImageJ output hashes."""
import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

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
    assert owner.snapshot().display.properties == {"display_range": [0., 4094.], "retained": {"exact": 42}}
    assert owner.undo_steps == steps + 1 and owner.snapshot().dirty
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), pixels)
    assert owner.snapshot().display.properties == before.display.properties
    owner.redo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.snapshot().display.properties["display_range"] == [0., 4094.]


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
