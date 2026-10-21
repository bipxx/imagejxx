"""Whole-dataset conversion publishes numeric presentation with pixel history."""
import copy

import imagejxx as ij
import numpy as np
import pytest


def fixture(dtype=np.uint16, samples=1):
    pixels = np.arange(48 * samples, dtype=dtype).reshape(
        (2, 2, 2, 2, 3) + (() if samples == 1 else (samples,)))
    calibration = ij.SpatialCalibration(spacing=[2, 3, 5], origin=[10, 20, 30], unit='um')
    reads = ij.DatasetReadObserver()
    dataset = ij.Dataset(pixels, axes='TCZYX' if samples == 1 else 'TCZYXS',
                         calibration=calibration).observe_reads(reads)
    owner = ij.Document(dataset, 'Conversion state')
    tx = owner.begin_edit()
    tx.set_property('future_metadata', {'preserved': [True, 7]})
    tx.select(ij.Selection('rectangle', [(0, 0), (1, 1)]))
    owner.commit(tx)
    display = owner.snapshot().display
    display.t, display.c, display.crosshair = 1, 1, [1, 1, 1]
    props = dict(display_range=[10., 30.], future_display={'preserved': [True, 9]})
    if samples == 1:
        props['manual_threshold'] = dict(version=1, lower=10., upper=30., t=1, c=1, z=1)
        props['channel_display'] = ij.default_channel_display(dataset, 1, 1)
    display.properties = props
    owner.set_display(display)
    owner = ij.Document(owner.snapshot()); owner.mark_saved()
    app = ij.Application(); app.adopt(owner)
    return owner, app, pixels, reads


def prepare(owner, **params):
    target = ij.resolve_legacy_target(owner.snapshot(), False, 0, 'convert_image')
    return ij.prepare_operation('convert_image', target, dict(dtype='uint8', scale=False, **params))


@pytest.mark.parametrize('direct', [False, True])
@pytest.mark.parametrize('source,samples,dtype', [(np.uint16, 1, 'uint8'), (np.float32, 1, 'uint16'),
                                               (np.uint16, 1, 'float64'), (np.uint8, 1, 'float32'),
                                               (np.uint8, 3, 'float32'), (np.uint8, 4, 'float64')])
def test_conversion_state_and_history_are_one_transaction(direct, source, samples, dtype):
    owner, app, pixels, _ = fixture(source, samples)
    before = owner.snapshot(); original = copy.deepcopy(before.display.properties)
    target = ij.resolve_legacy_target(before, False, 0, 'convert_image')
    prepared = ij.prepare_operation('convert_image', target, dict(dtype=dtype, scale=False))
    expected_properties = dict(future_display=original['future_display'])
    assert prepared.preview.display.properties == expected_properties
    assert owner.snapshot().display.properties == original and owner.undo_steps == 0
    if direct:
        owner.commit(prepared.begin_edit())
    else:
        app.publish(prepared)
    after = owner.snapshot()
    expected = pixels.astype(dtype) if samples == 1 else pixels[..., :3].mean(axis=-1).astype(dtype)
    def assert_result(actual):
        if samples > 1 and dtype == 'float64':
            # Independent mean oracle: the native sum of weighted terms may
            # differ from NumPy's sum-then-divide by one representable step.
            np.testing.assert_allclose(actual, expected, rtol=np.finfo(np.float64).eps, atol=0)
        else:
            np.testing.assert_array_equal(actual, expected)
    assert_result(after.dataset.numpy('TCZYX'))
    assert after.display.properties == expected_properties and owner.undo_steps == 1
    assert (after.display.t, after.display.c, after.display.crosshair) == (1, 1, [1, 1, 1])
    assert after.selection.points == before.selection.points
    assert after.metadata == before.metadata
    assert after.dataset.calibration.spacing == before.dataset.calibration.spacing
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX' if samples == 1 else 'TCZYXS'), pixels)
    assert owner.snapshot().display.properties == original
    owner.redo()
    assert_result(owner.snapshot().dataset.numpy('TCZYX'))
    assert owner.snapshot().display.properties == expected_properties


@pytest.mark.parametrize('direct', [False, True])
@pytest.mark.parametrize('change', ['navigation', 'numeric_range', 'extension'])
def test_conversion_refuses_stale_display_without_partial_publication(direct, change):
    owner, app, pixels, _ = fixture()
    prepared = prepare(owner)
    display = owner.snapshot().display
    if change == 'navigation': display.t = 0
    else:
        props = display.properties
        props['display_range' if change == 'numeric_range' else 'future_display'] = [1., 2.]
        display.properties = props
    owner.set_display(display)
    before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError)):
        if direct: owner.commit(prepared.begin_edit())
        else: app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.snapshot().display.properties == before.display.properties and owner.undo_steps == 0


def test_opaque_channel_display_refuses_before_pixel_reads():
    owner, _, _, reads = fixture()
    display = owner.snapshot().display; props = display.properties
    props['channel_display'] = dict(version=999, future=True); display.properties = props
    owner.set_display(display)
    count = reads.counts().plane_reads
    with pytest.raises(ValueError): prepare(owner)
    assert reads.counts().plane_reads == count and owner.undo_steps == 0


def test_same_type_noop_keeps_numeric_presentation_and_history():
    owner, app, pixels, _ = fixture(np.uint8)
    before = owner.snapshot().display.properties
    prepared = prepare(owner)
    app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.snapshot().display.properties == before and owner.undo_steps == 0


@pytest.mark.parametrize('samples', [1, 3])
def test_new_document_pairs_conversion_history_and_preserves_source(samples):
    owner, app, pixels, _ = fixture(np.uint8 if samples > 1 else np.uint16, samples)
    before = owner.snapshot()
    request = ij.TargetRequest()
    request.scope = ij.TargetScope.RANGES
    request.t = ij.AxisRange(0, 2, 1); request.c = ij.AxisRange(0, 2, 1); request.z = ij.AxisRange(0, 2, 1)
    request.roi = ij.RoiPolicy.WHOLE_PLANE
    request.destination = ij.OutputDestination.NEW_DOCUMENT
    prepared = ij.prepare_operation('convert_image', ij.resolve_target(before, request),
                                    dict(dtype='float32', scale=False))
    published = app.publish(prepared)
    created = published.document
    assert published.created and created.id != owner.id and owner.undo_steps == 0
    assert owner.snapshot().display.properties == before.display.properties
    expected = dict(future_display=before.display.properties['future_display'])
    assert created.snapshot().display.properties == expected
    assert created.undo_steps == 1
    created.undo()
    np.testing.assert_array_equal(created.snapshot().dataset.numpy('TCZYX' if samples == 1 else 'TCZYXS'), pixels)
    assert created.snapshot().display.properties == before.display.properties
    created.redo()
    assert created.snapshot().display.properties == expected
