"""Editable rounded boxes; exact raster references and retained parametric edits."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1]/'fixtures/rounded-path-imagej154r.json').read_bytes())
SHAPE = (33, 41)

def box(diameter=8, points=((6, 5), (24, 19))):
    return ij.Selection('rounded_rectangle', points, attributes={
        'corner_diameter': diameter, 'label': 'kept', 'future': {'values': [True, 17]}})

def reference_selection(row):
    cx, cy, ux, uy, vx, vy = row['frame']
    return ij.Selection('rotated_rounded_rectangle', [(cx, cy), (cx+ux, cy+uy), (cx+vx, cy+vy)],
                        attributes={'corner_diameter': row['diameter']})

@pytest.mark.parametrize('row', REFERENCE['cases'], ids=lambda row: row['id'])
def test_editable_mask_matches_independent_java_reference(row):
    selection = reference_selection(row)
    assert selection.is_rounded_box and selection.corner_diameter == row['diameter']
    np.testing.assert_allclose(selection.rounded_path(), row['path'], rtol=2e-12, atol=2e-12)
    expected = np.array([255 if v == '1' else 0 for v in row['mask']], dtype=np.uint8).reshape(SHAPE)
    np.testing.assert_array_equal(selection.mask(SHAPE), expected)

@pytest.mark.parametrize('diameter', [None, True, False, -1, 1.0, '4', 2**31, 2**64-1])
def test_invalid_diameter_is_rejected_by_all_geometry_entries(diameter):
    selection = box(diameter)
    for operation in [lambda: selection.corner_diameter, selection.rounded_path,
                      lambda: selection.mask(SHAPE), lambda: ij.SelectionEditor(selection, SHAPE)]:
        with pytest.raises((ValueError, TypeError, OverflowError)): operation()

def test_missing_diameter_is_not_inferred_from_legacy_attributes():
    selection = ij.Selection('rounded_rectangle', [(6, 5), (24, 19)])
    with pytest.raises(ValueError): selection.mask(SHAPE)
    legacy = ij.Selection('rectangle', [(6, 5), (24, 19)], attributes={'corner_diameter': 8})
    assert not legacy.is_rounded_box
    np.testing.assert_array_equal(legacy.mask(SHAPE), ij.Selection('rectangle', legacy.points).mask(SHAPE))
    with pytest.raises(ValueError): legacy.rounded_path()

def test_corner_hit_testing_and_parametric_edit_preservation():
    selection = box()
    editor = ij.SelectionEditor(selection, SHAPE)
    assert len(editor.handles()) == 8
    assert editor.hit_test((15, 12), (.01, .01)).kind == ij.SelectionHandleKind.MOVE
    assert editor.hit_test((6.2, 5.2), (.01, .01)) is None
    assert editor.hit_test((6.2, 12), (.01, .01)).kind == ij.SelectionHandleKind.MOVE
    moved = editor.moved((2, 1))
    np.testing.assert_array_equal(moved.points, [[8, 6], [26, 20]])
    rotated = editor.rotated(31)
    assert rotated.kind == 'rotated_rounded_rectangle'
    rotated_editor = ij.SelectionEditor(rotated, SHAPE)
    assert len(rotated_editor.handles()) == 8
    corner = rotated.box_frame.image_point((-.98, -.98))
    assert rotated_editor.hit_test(corner, (.001, .001)) is None
    resized = rotated_editor.resized(4, rotated.box_frame.image_point((1.4, 1.3)))
    restored_axis = rotated_editor.box_geometry((15, 12), (18, 14), 0)
    for result in [moved, rotated, resized, restored_axis]:
        assert result.is_rounded_box and result.corner_diameter == 8
        assert result.attributes == selection.attributes
        assert result.mask(SHAPE).any()
    assert restored_axis.kind == 'rounded_rectangle'
    changed = editor.properties(2, {'corner_diameter': 4})
    assert changed.corner_diameter == 4 and changed.width == 2
    assert selection.corner_diameter == 8

def test_processing_quantization_is_separate_from_continuous_and_wire_geometry():
    selection = box(5, ((-1.7, 2.3), (14.6, 13.8)))
    frame = ij.SelectionFrame((7.5, 8), (8.5, 0), (0, 6))
    np.testing.assert_array_equal(selection.rounded_path(processing=True), ij.rounded_rectangle_path(frame, 5))
    assert selection.rounded_path() != selection.rounded_path(processing=True)
    np.testing.assert_array_equal(selection.mask(SHAPE), ij.imagej_shape_mask(selection.rounded_path(processing=True), 41, 33).mask(SHAPE))

def test_histogram_counts_and_preflight_include_curve_scratch():
    selection = box()
    source = ij.Dataset(np.arange(33*41, dtype=np.uint16).reshape(SHAPE), axes='YX')
    request = ij.DatasetHistogramRequest(); request.area_roi = selection
    estimate = ij.estimate_dataset_histogram(source, request)
    assert estimate.working_bytes >= 512*1024 + 2*33*41*8 + (33*41+7)//8
    limits = ij.DatasetHistogramLimits(); limits.max_working_bytes = estimate.working_bytes
    result = ij.accumulate_dataset_histogram(source, request, limits)
    assert result.included_count == np.count_nonzero(selection.mask(SHAPE))
    limits.max_working_bytes -= 1
    calls = []
    with pytest.raises(ValueError):
        ij.accumulate_dataset_histogram(source, request, limits, checkpoint=lambda: calls.append(True))
    assert calls == []
