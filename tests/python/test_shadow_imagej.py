"""Pinned ImageJ output, not another imagejxx operation, is the numeric oracle."""
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

FIXTURE = json.loads((Path(__file__).parents[1]/'fixtures/shadows-imagej154r.json').read_bytes())
CASES = FIXTURE['cases']


@pytest.mark.parametrize('case', CASES, ids=lambda c: '-'.join(map(str,
    [c['type'], c['direction'], c['width'], c['height'], c['pattern'], c['rectangle']])))
def test_shadow_matches_pinned_imagej(case):
    dtype = {'byte': np.uint8, 'short': np.uint16, 'float': np.float32, 'rgb': np.uint8}[case['type']]
    shape = (case['height'], case['width']) + ((3,) if case['type'] == 'rgb' else ())
    source = np.array(case['source'], dtype=dtype).reshape(shape)
    original = source.copy()
    extent = tuple(case['extent']) if case['rectangle'] else None
    if case['reference_error']:
        assert case['type'] == 'rgb' and case['width'] == 1 and case['height'] >= 3
        with pytest.raises(ValueError, match='RGB shadows require width'):
            ij.shadow_imagej(source, case['direction'], extent=extent)
    else:
        expected = np.array(case['output'], dtype=dtype).reshape(shape)
        actual = ij.shadow_imagej(source, case['direction'], extent=extent)
        np.testing.assert_array_equal(actual.view(np.uint32) if dtype == np.float32 else actual,
                                      expected.view(np.uint32) if dtype == np.float32 else expected)
        if extent is None:
            np.testing.assert_array_equal(ij.execute_operation('shadow_imagej', source,
                {'direction': case['direction']}), expected)
    np.testing.assert_array_equal(source, original)


@pytest.mark.parametrize('dtype,samples', [(np.float64, 1), (np.uint8, 4), (np.uint16, 3), (np.float32, 3)])
def test_shadow_rejects_non_imagej_layouts(dtype, samples):
    shape = (5, 7) if samples == 1 else (5, 7, samples)
    image = np.ones(shape, dtype=dtype)
    with pytest.raises(ValueError, match='shadows support'):
        ij.shadow_imagej(image)
    with pytest.raises(ValueError, match='shadows support'):
        ij.execute_operation('shadow_imagej', image)


@pytest.mark.parametrize('extent', [(-1,0,1,1), (0,-1,1,1), (0,0,-1,1), (0,0,1,-1),
                                    (0,0,8,1), (0,0,1,6), (2**31-1,0,2**31-1,1)])
def test_shadow_extent_is_bounded(extent):
    with pytest.raises(ValueError, match='extent must lie inside'):
        ij.shadow_imagej(np.ones((5,7), np.uint8), extent=extent)


@pytest.mark.parametrize('extent', [(True,0,1,1), (0,0,1.5,1), (0,0,1), (0,0,2**31,1)])
def test_shadow_extent_coordinates_are_exact_integers(extent):
    with pytest.raises((ValueError, TypeError, OverflowError)):
        ij.shadow_imagej(np.ones((5,7), np.uint8), extent=extent)


def test_shadow_empty_extent_and_invalid_direction():
    image = np.arange(35, dtype=np.uint16).reshape(5,7)
    np.testing.assert_array_equal(ij.shadow_imagej(image, extent=(7,5,0,0)), image)
    with pytest.raises(ValueError, match='direction'):
        ij.shadow_imagej(image, 'demo')
    with pytest.raises(ValueError):
        ij.validate_operation_record('shadow_imagej', {'direction': 'invalid'})


def test_shadow_scientific_targets_preview_history_and_cancel():
    case = next(c for c in CASES if c['type'] == 'byte' and c['width'] == 7
                and c['height'] == 5 and c['pattern'] == 1 and not c['rectangle']
                and c['direction'] == 'east')
    plane = np.array(case['source'], np.uint8).reshape(5,7)
    expected_plane = np.array(case['output'], np.uint8).reshape(5,7)
    pixels = np.broadcast_to(plane, (2,2,2,5,7)).copy()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYX'))
    app = ij.Application(); app.adopt(owner)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(0,0,0), ij.PlanePosition(1,1,1)], roi=ij.RoiPolicy.WHOLE_PLANE)
    target = app.resolve_target(request, operation='shadow_imagej')
    original = owner.snapshot()
    token = ij.CancellationToken(); token.cancel()
    with pytest.raises(ij.OperationCancelled):
        app.prepare('shadow_imagej', target, {'direction': 'east'}, cancellation=token)
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0
    prepared = app.prepare('shadow_imagej', target, {'direction': 'east'})
    expected = pixels.copy(); expected[0,0,0] = expected[1,1,1] = expected_plane
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'), expected)
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0
    app.publish(prepared)
    assert owner.undo_steps == 1
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    owner.undo(); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    owner.redo(); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)


def test_shadow_finite_output_failure_is_atomic():
    pixels = np.array([[1e38,1e38,1e38], [0,0,0], [-1e38,-1e38,-1e38]], np.float32)
    original = pixels.copy()
    with pytest.raises(ValueError, match='output is not finite'):
        ij.shadow_imagej(pixels, 'north')
    np.testing.assert_array_equal(pixels, original)


def test_registered_shadow_limits_computation_to_selected_bounds():
    pixels = np.zeros((7,9), np.float32)
    pixels[0,:3] = 1e38; pixels[2,:3] = -1e38
    owner = ij.Document(ij.Dataset(pixels, axes='YX'))
    owner.select(ij.Selection('rectangle', [(6,4),(8,6)]))
    app = ij.Application(); app.adopt(owner)
    before = owner.snapshot(); history = owner.undo_steps
    prepared = app.prepare('shadow_imagej', app.resolve_target(), {'direction': 'north'})
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('YX'), pixels)
    app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'), pixels)
    assert owner.undo_steps == history and owner.snapshot().shares_data(before)


def test_shadow_one_column_rgb_safe_rows_and_empty_extents():
    image = np.arange(21, dtype=np.uint8).reshape(7,1,3)
    for direction in ['north','northeast','east','southeast','south','southwest','west','northwest']:
        for row in [0,2,3,6]:
            np.testing.assert_array_equal(ij.shadow_imagej(image, direction, extent=(0,row,1,1)), image)
        for extent in [(1,0,0,7), (0,7,1,0)]:
            np.testing.assert_array_equal(ij.shadow_imagej(image, direction, extent=extent), image)
