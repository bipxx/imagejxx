"""ImageJ observations are immutable expectations; Python does no product editing."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

FIXTURE = json.loads((Path(__file__).resolve().parents[1]/'fixtures/selection-enlargement-imagej154r.json').read_bytes())
assert FIXTURE['imagej_version'] == '1.54r' and len(FIXTURE['cases']) == 144


def geometry(row):
    k = row['kind']
    if k < 2:
        return ij.Selection('rectangle' if k == 0 else 'ellipse', [(4.75, 5.25), (24.25, 21.75)])
    if k == 2:
        return ij.Selection('rounded_rectangle', [(4, 5), (24, 22)], attributes={'corner_diameter': 8})
    if k == 3:
        return ij.Selection('polygon', [(3, 4), (27, 3), (35, 17), (20, 32), (7, 23)])
    if k == 4:
        return ij.Selection('freehand', [(-4, -2), (19, 1), (31, 20), (12, 35), (-3, 17)])
    return ij.Selection.from_mask(rows(row['before']))


def rows(value):
    return np.array([[255 if c == '1' else 0 for c in row] for row in value], np.uint8)


@pytest.mark.parametrize('row', FIXTURE['cases'], ids=lambda r: f"kind{r['kind']}-distance{r['distance']}")
def test_matches_actual_imagej154r_enlarger(row):
    original = geometry(row)
    np.testing.assert_array_equal(original.mask((40, 48)), rows(row['before']))
    editor = ij.SelectionEditor(original, (40, 48))
    result = editor.enlarged(row['distance'])
    np.testing.assert_array_equal(result.mask((40, 48)), rows(row['after']))
    assert editor.selection == original
    assert editor.estimate_enlargement(row['distance']).rounded_pixels == int(np.floor(row['distance']+.5))
    if row['kind'] < 3:
        # For analytic results, preserve complete off-image geometry as well as pixels.
        bounds = ij.SelectionEditor(result, (40, 48)).bounds
        expected = row['float_bounds']
        np.testing.assert_array_equal(bounds, [expected[0], expected[1], expected[0]+expected[2], expected[1]+expected[3]])


@pytest.mark.parametrize('kind', ['rotated_rectangle', 'rotated_ellipse', 'rotated_rounded_rectangle'])
@pytest.mark.parametrize('distance', [0., .5, 2., -2., -100.])
def test_native_rotated_area_extensions_are_immutable_and_keep_style(kind, distance):
    attributes = {'label': 'retained', 'future': {'values': [True, 7, None]}}
    if kind == 'rotated_rounded_rectangle':
        attributes['corner_diameter'] = 4
    area = ij.Selection(kind, [(18, 15), (25, 18), (15, 22)], width=3, attributes=attributes)
    editor = ij.SelectionEditor(area, (40, 48))
    result = editor.enlarged(distance)
    assert editor.selection == area and result.width == 3
    assert result.attributes['future'] == attributes['future'] and result.attributes['label'] == 'retained'
    assert result.mask((40, 48)).shape == (40, 48)
    if distance == 0 or distance == -100:
        assert result == area


@pytest.mark.parametrize('distance', [0., 2., -2., 256., -256.])
def test_explicit_empty_mask_stays_empty(distance):
    area = ij.Selection.from_mask(np.zeros((5, 7), np.uint8))
    assert ij.SelectionEditor(area, (5, 7)).enlarged(distance) == area


@pytest.mark.parametrize('distance', [1., -1., 13., 255.5, 256.])
def test_degenerate_polygon_never_creates_pixels(distance):
    area = ij.Selection('polygon', [(2, 2), (4, 2), (6, 2)])
    assert not np.any(area.mask((8, 8)))
    assert ij.SelectionEditor(area, (8, 8)).enlarged(distance) == area


@pytest.mark.parametrize('kind', ['line', 'polyline', 'point', 'multipoint', 'angle'])
def test_non_area_refusal(kind):
    area = ij.Selection(kind, [(1, 1)] if kind == 'point' else [(1, 1), (4, 3), (6, 1)])
    with pytest.raises(ValueError, match='area'):
        ij.SelectionEditor(area, (5, 7)).enlarged(2)


@pytest.mark.parametrize('distance', [float('nan'), float('inf'), -float('inf'), 1e20, -1e20])
def test_invalid_distance_refusal(distance):
    with pytest.raises(ValueError):
        ij.SelectionEditor(geometry(FIXTURE['cases'][0]), (40, 48)).enlarged(distance)


def test_limits_callbacks_cancellation_and_source_freezing():
    area = ij.Selection('polygon', [(3, 4), (27, 3), (35, 17), (20, 32), (7, 23)])
    editor = ij.SelectionEditor(area, (40, 48))
    estimate = editor.estimate_enlargement(2)
    assert estimate.padded_pixels > 0
    cap = ij.SelectionEditLimits()
    cap.max_working_bytes = estimate.working_bytes-1
    calls = []
    with pytest.raises((ValueError, RuntimeError), match='working limit'):
        editor.enlarged(2, cap, checkpoint=lambda: calls.append('forbidden'))
    assert calls == []
    cap.max_working_bytes = estimate.working_bytes
    result = editor.enlarged(2, cap, progress=lambda a, b: calls.append((a, b)))
    assert calls == [(0, 1), (1, 1)]
    assert result.kind == 'mask' and editor.selection == area
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(RuntimeError, match='cancel'):
        editor.enlarged(2, cancellation=token)
    calls.clear()
    def finish(a, b):
        calls.append((a, b))
        if a == 1:
            raise RuntimeError('finish refusal')
    with pytest.raises(RuntimeError, match='finish refusal'):
        editor.enlarged(2, progress=finish)
    assert calls == [(0, 1), (1, 1)] and editor.selection == area
    cap.max_working_bytes = estimate.working_bytes
    def mutate_limits(a, b):
        cap.max_working_bytes = 1
    assert editor.enlarged(2, cap, progress=mutate_limits).kind == 'mask'
    with pytest.raises((ValueError, RuntimeError)):
        editor.enlarged(2, cap)


def test_padded_pixel_cap_is_checked_before_callbacks():
    editor = ij.SelectionEditor(ij.Selection('polygon', [(1, 1), (5, 1), (3, 4)]), (5, 7))
    cap = ij.SelectionEditLimits()
    cap.max_xy_pixels = 100
    calls = []
    with pytest.raises((ValueError, RuntimeError), match='pixel limit'):
        editor.enlarged(13, cap, checkpoint=lambda: calls.append(True))
    assert calls == []
