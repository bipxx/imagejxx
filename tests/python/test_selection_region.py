"""Complete ImageJ geometry observations; projection never feeds the next edit."""
import base64
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_selection_enlargement import FIXTURE as ORIGINAL, geometry

FIXTURE = json.loads((Path(__file__).resolve().parents[1]/'fixtures/selection-region-imagej154r.json').read_bytes())
assert FIXTURE['imagej_version'] == '1.54r' and len(FIXTURE['cases']) == 144
def snapshot(value):
    return dict(kind=value.kind, points=value.points, width=value.width, attributes=value.attributes)


SOURCES = {r['kind']: r for r in ORIGINAL['cases'] if r['distance'] == 0}


def sample(region, rectangle):
    x, y, width, height = rectangle
    origin = [region.origin[0]-x, region.origin[1]-y]
    shifted = ij.SelectionRegion(region.local_selection, region.shape, origin)
    return shifted.project((height, width)).mask((height, width)) != 0


@pytest.mark.parametrize('row', FIXTURE['cases'], ids=lambda r: f"kind{r['kind']}-sequence{r['sequence']}-step{r['step']}")
def test_complete_off_grid_repeated_geometry_matches_imagej(row):
    original = geometry(SOURCES[row['kind']])
    region = ij.SelectionRegion(original, (40, 48))
    for stage in FIXTURE['cases']:
        if stage['kind'] == row['kind'] and stage['sequence'] == row['sequence'] and stage['step'] <= row['step']:
            region = region.enlarged(stage['distance'])
    x, y, width, height = row['sample']
    expected = np.unpackbits(np.frombuffer(base64.b64decode(row['mask']), np.uint8))[:width*height].reshape(height, width)
    actual = sample(region, row['sample'])
    np.testing.assert_array_equal(actual, expected)
    assert np.count_nonzero(actual) == row['occupied']
    left, top, span_x, span_y = row['float_bounds']
    np.testing.assert_array_equal(region.bounds, [left, top, left+span_x, top+span_y])
    assert ij.SelectionRegion(original, (40, 48)).local_selection == original


def corner():
    pixels = np.zeros((9, 9), np.uint8)
    pixels[0, 0] = 255
    return ij.SelectionRegion(ij.Selection.from_mask(pixels), (9, 9))


def test_explicit_projection_is_lossy_but_kept_region_restores_corner():
    region = corner()
    grown = region.enlarged(2)
    assert np.count_nonzero(grown.local_selection.mask(grown.shape)) == 21
    clipped = grown.project((9, 9))
    assert np.count_nonzero(clipped.mask((9, 9))) == 8
    restored = grown.enlarged(-2).project((9, 9))
    np.testing.assert_array_equal(restored.mask((9, 9)), region.project((9, 9)).mask((9, 9)))
    clipped_shrink = ij.SelectionEditor(clipped, (9, 9)).enlarged(-2)
    assert clipped_shrink != restored


@pytest.mark.parametrize('origin', [(0, 0), (-100, 200), (11, -17), (2**31-1, 2**31-1)])
def test_region_origins_and_disjoint_projection_do_not_clamp_geometry(origin):
    source = corner()
    region = ij.SelectionRegion(source.local_selection, source.shape, origin)
    assert region.bounds == [origin[0], origin[1], origin[0]+1, origin[1]+1]
    np.testing.assert_array_equal(sample(region, (*origin, 9, 9)), source.project((9, 9)).mask((9, 9)) != 0)
    if origin != (0, 0):
        assert np.count_nonzero(region.project((9, 9)).mask((9, 9))) == 0


@pytest.mark.parametrize('kind', ['rectangle', 'ellipse', 'polygon', 'freehand', 'rounded_rectangle'])
def test_region_freezes_source_geometry_and_attributes(kind):
    points = [(2, 2), (7, 7)] if kind in ('rectangle', 'ellipse', 'rounded_rectangle') else [(2, 2), (7, 2), (5, 7)]
    attributes = {'future': {'values': [True, 7, None]}}
    if kind == 'rounded_rectangle': attributes['corner_diameter'] = 3
    source = ij.Selection(kind, points, width=3, attributes=attributes)
    before = snapshot(source)
    region = ij.SelectionRegion(source, (9, 9))
    source.points[0][0] = 100
    attributes['future']['values'][1] = 99
    detached = region.local_selection
    detached.attributes['future']['values'][1] = 101
    assert snapshot(region.local_selection) == before
    result = region.enlarged(2)
    assert result.local_selection.width == 3
    assert result.local_selection.attributes['future'] == before['attributes']['future']


@pytest.mark.parametrize('operation', ['enlarge', 'project'])
def test_coarse_progress_and_frozen_limits(operation):
    region = corner()
    limits = ij.SelectionEditLimits()
    events = []
    def progress(done, total):
        events.append((done, total))
        limits.max_working_bytes = 1
    result = region.enlarged(2, limits, progress=progress) if operation == 'enlarge' else region.project((19, 19), limits, progress=progress)
    assert result is not None and events == [(0, 1), (1, 1)]


@pytest.mark.parametrize('operation', ['enlarge', 'project'])
@pytest.mark.parametrize('failure', ['before', 'progress', 'finish', 'checkpoint', 'limit'])
def test_failures_leave_immutable_source_and_no_result(operation, failure):
    region = corner()
    before = snapshot(region.local_selection)
    token = ij.CancellationToken()
    events = []
    limits = ij.SelectionEditLimits()
    if failure == 'before': token.cancel()
    if failure == 'limit':
        limits.max_working_bytes = (region.estimate_enlargement(2).working_bytes if operation == 'enlarge' else region.estimate_projection((19, 19)))-1
    def progress(done, total):
        events.append((done, total))
        if failure == 'progress': token.cancel()
        if failure == 'finish' and done == total: raise RuntimeError('deliberate finish failure')
    def checkpoint():
        if failure == 'checkpoint': raise RuntimeError('deliberate checkpoint failure')
    with pytest.raises((ValueError, RuntimeError, OverflowError)):
        if operation == 'enlarge': region.enlarged(2, limits, cancellation=token, progress=progress, checkpoint=checkpoint)
        else: region.project((19, 19), limits, cancellation=token, progress=progress, checkpoint=checkpoint)
    assert snapshot(region.local_selection) == before
    if failure in ('before', 'limit'): assert events == []


def test_global_origin_overflow_is_refused_before_callbacks():
    source = corner()
    region = ij.SelectionRegion(source.local_selection, source.shape, (-2**31, 0))
    events = []
    with pytest.raises(ValueError): region.enlarged(2, progress=lambda *x: events.append(x))
    assert events == [] and region.origin == [-2**31, 0]


@pytest.mark.parametrize('operation', ['enlarge', 'project'])
def test_exact_estimated_working_limit_is_admitted(operation):
    region = corner()
    limits = ij.SelectionEditLimits()
    limits.max_working_bytes = region.estimate_enlargement(2).working_bytes if operation == 'enlarge' else region.estimate_projection((19, 19))
    result = region.enlarged(2, limits) if operation == 'enlarge' else region.project((19, 19), limits)
    assert result is not None


def test_empty_region_and_zero_offset_keep_original_geometry():
    source = ij.Selection.from_mask(np.zeros((9, 9), np.uint8))
    region = ij.SelectionRegion(source, (9, 9), (-10, 20))
    assert region.bounds is None
    for distance in [0., 2., -2., 256.]:
        result = region.enlarged(distance)
        assert result.local_selection == source and result.origin == [-10, 20]
    original = corner()
    assert original.enlarged(0).local_selection == original.local_selection


@pytest.mark.parametrize('kind', ['point', 'line', 'polyline', 'angle'])
def test_region_rejects_non_area_geometry(kind):
    points = [(1, 1)] if kind == 'point' else [(1, 1), (2, 2), (3, 1)]
    with pytest.raises(ValueError): ij.SelectionRegion(ij.Selection(kind, points), (9, 9))
