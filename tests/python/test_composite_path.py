"""Continuous path geometry, using independent analytic and ImageJ fixtures."""
import json
import math
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij


def rect(x, y, right, bottom):
    return [0, x, y, 1, right, y, 1, right, bottom, 1, x, bottom, 4]


@pytest.mark.parametrize('commands,expected', [
    ([0, 0, 0, 2, 2, 4, 4, 0, 4], [0, 0, 4, 2]),
    ([0, 0, 0, 3, 0, 4, 4, 4, 4, 0, 4], [0, 0, 4, 3]),
    ([0, 0, 0, 3, 0, 3, 3, 3, 9, 0], [0, 0, 9, 2.25]),
    ([0, 0, 0, 3, 1, 1, 2, 2, 3, 3], [0, 0, 3, 3]),
    ([0, 0, 0, 3, 0, 3, 3, -3, 3, 0], [0, -math.sqrt(3)/2, 3, math.sqrt(3)/2]),
    ([0, 0, 0, 3, 1, 1, 2, 0, 3, 1], [0, 0, 3, 1]),
    ([0, 0, 0, 3, 1, 1, 2, 1, 3, 2], [0, 0, 3, 2]),
    ([0, 0, 0, 2, 1, 1, 2, 2], [0, 0, 2, 2]),
    ([0, -3, 5, 4, 1, -1, 7], [-3, 5, -1, 7]),
    ([0, 9, -2, 0, 1, 3, 4], [1, -2, 9, 3]),
])
def test_analytic_extrema(commands, expected):
    original = list(commands)
    path = ij.ImageJCompositePath(commands)
    assert path.bounds == pytest.approx(expected)
    detached = path.commands
    detached[1] = 123
    commands[1] = 456
    assert path.bounds == pytest.approx(expected)
    assert path.commands == original
    with pytest.raises(AttributeError):
        path.commands = []


@pytest.mark.parametrize('matrix', [
    [1, 0, 3, 0, 1, -2], [-1, 0, 20, 0, 1, 0],
    [0, -1, 16, 1, 0, 0], [2, .5, -3, .25, 1, 2],
])
def test_transformed_bezier_points(matrix):
    original = ij.ImageJCompositePath([0, 1, 2, 3, 3, -4, 8, 15, 11, 3, 4])
    transformed = original.transformed(matrix)
    assert transformed.segment_count == original.segment_count
    assert [transformed.commands[i] for i in [0, 3, 10]] == [0, 3, 4]
    a = np.array(matrix).reshape(2, 3)
    source = np.array(original.commands[1:3] + original.commands[4:10]).reshape(4, 2)
    target = np.array(transformed.commands[1:3] + transformed.commands[4:10]).reshape(4, 2)
    lo_x, lo_y, hi_x, hi_y = transformed.bounds
    for t in np.linspace(0, 1, 101):
        weights = np.array([(1-t)**3, 3*t*(1-t)**2, 3*t*t*(1-t), t**3])
        point = weights @ target
        expected = a[:, :2] @ (weights @ source) + a[:, 2]
        assert point == pytest.approx(expected)
        assert lo_x - 1e-12 <= point[0] <= hi_x + 1e-12
        assert lo_y - 1e-12 <= point[1] <= hi_y + 1e-12


@pytest.mark.parametrize('path', [[], [1, 1, 1], [4], [0, 1], [0, 1, 1, 5],
    [0, 1, 1, 2, 1, 2, 3], [0, 1, 1, 3, 1, 2, 3, 4, 5],
    [0, 1, 1, .5], [0, math.inf, 1], [0, math.nan, 1], [0, 1e9+1, 0]])
def test_bad_grammar(path):
    with pytest.raises(ValueError):
        ij.ImageJCompositePath(path)


@pytest.mark.parametrize('value', [True, '2', None, object()])
def test_non_numeric_coordinates(value):
    with pytest.raises(TypeError):
        ij.ImageJCompositePath([0, value, 0])


@pytest.mark.parametrize('matrix', [[1, 0], [1, 0, 0, 0, 1, 0, 0],
    [1, 2, 0, 2, 4, 0], [1, 0, math.nan, 0, 1, 0],
    [1, 0, 1e9+1, 0, 1, 0], [1, 0, True, 0, 1, 0], '100010'])
def test_invalid_transform(matrix):
    with pytest.raises((ValueError, TypeError)):
        ij.ImageJCompositePath(rect(1, 1, 8, 8)).transformed(matrix)


def test_exact_admission_and_identity():
    commands = rect(-4, -2, 8, 8)
    limits = ij.ImageJRoiLimits(max_bytes=len(commands)*8, max_vertices=5)
    path = ij.ImageJCompositePath(commands, limits)
    assert path.transformed([1, 0, 0, 0, 1, 0], limits).commands == commands
    for low in [ij.ImageJRoiLimits(max_bytes=len(commands)*8-1),
                ij.ImageJRoiLimits(max_vertices=4), ij.ImageJRoiLimits(max_vertices=0)]:
        with pytest.raises(ValueError):
            ij.ImageJCompositePath(commands, low)
        with pytest.raises(ValueError):
            path.transformed([1, 0, 0, 0, 1, 0], low)
    zero = ij.ImageJCompositePath([0, -0., 0, 4]).transformed([1, 0, 0, 0, 1, 0])
    assert math.copysign(1, zero.commands[1]) == -1


@pytest.mark.parametrize('dx,dy', [(0, 0), (3, 2), (-4, -2), (40, -30)])
def test_holes_islands_and_offcanvas_recovery(dx, dy):
    commands = rect(1, 1, 8, 8)+rect(3, 3, 6, 6)+rect(9, 1, 11, 3)
    path = ij.ImageJCompositePath(commands).transformed([1, 0, dx, 0, 1, dy])
    mask = path.mask(16, 14).mask((14, 16))
    for y in range(14):
        for x in range(16):
            u, v = x-dx, y-dy
            expected = ((1 <= u < 8 and 1 <= v < 8 and not (3 <= u < 6 and 3 <= v < 6))
                        or (9 <= u < 11 and 1 <= v < 3))
            assert bool(mask[y, x]) == expected
    assert path.transformed([1, 0, -dx, 0, 1, -dy]).commands == commands


REFERENCE = json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-composite-imagej154r.json').read_text(encoding='utf-8'))


@pytest.mark.parametrize('case', REFERENCE['cases'], ids=lambda c: str(c['index']))
def test_actual_imagej_paths_and_mask_roundtrip(case):
    row = ij.decode_imagej_roi(bytes.fromhex(case['hex']), mask_width=12, mask_height=10)
    path = ij.ImageJCompositePath(row.composite_path)
    assert ''.join('1' if v else '0' for v in path.mask(12, 10).mask((10, 12)).flat) == case['mask']
    moved = ij.transform_imagej_composite_roi(row, [1, 0, 2, 0, 1, 3], 18, 16)
    restored = ij.transform_imagej_composite_roi(moved, [1, 0, -2, 0, 1, -3], 12, 10)
    assert restored.composite_path == row.composite_path == case['path']
    assert restored.roi.selection.mask((10, 12)).tobytes() == row.roi.selection.mask((10, 12)).tobytes()
    assert restored.properties == row.properties
    assert restored.roi.selection.attributes == row.roi.selection.attributes
    assert (restored.roi.t, restored.roi.c, restored.roi.z) == (0, 1, 2)
    wire = ij.encode_imagej_roi(moved)
    decoded = ij.decode_imagej_roi(wire, mask_width=18, mask_height=16)
    assert decoded.composite_path == moved.composite_path
    assert decoded.roi.selection.mask((16, 18)).tobytes() == moved.roi.selection.mask((16, 18)).tobytes()


def test_mismatched_source_and_output_admission():
    path = ij.ImageJCompositePath(rect(1, 1, 8, 8))
    row = ij.ImageJRoi(ij.ROI('shape', path.mask(12, 10)))
    row.composite_path = rect(1, 1, 2, 2)
    with pytest.raises(ValueError, match='differs'):
        ij.transform_imagej_composite_roi(row, [1, 0, 0, 0, 1, 0], 12, 10)
    row.composite_path = path.commands
    for w, h in [(0, 1), (1, -1), (10000, 10000)]:
        with pytest.raises(ValueError):
            ij.transform_imagej_composite_roi(row, [1, 0, 0, 0, 1, 0], w, h)
    assert row.composite_path == path.commands
