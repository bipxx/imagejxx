"""Rounded membership through Boolean, session, history and Python adapters."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
SHAPE = (33, 41)
REFERENCE = json.loads((Path(__file__).parents[1]/'fixtures/rounded-path-imagej154r.json').read_bytes())
def box():
    return ij.Selection('rounded_rectangle', [(6,5),(24,19)], attributes={'corner_diameter':8,'label':'kept'})
def reference_selection(row):
    cx,cy,ux,uy,vx,vy=row['frame']
    return ij.Selection('rotated_rounded_rectangle', [(cx,cy),(cx+ux,cy+uy),(cx+vx,cy+vy)], attributes={'corner_diameter':row['diameter']})

@pytest.mark.parametrize('operation', ['union', 'intersection', 'subtraction', 'xor'])
def test_boolean_composition_uses_rounded_membership(operation):
    from imagejxx import _native
    left = reference_selection(REFERENCE['cases'][4])
    right = ij.Selection('rectangle', [(12, 7), (30, 24)])
    a, b = left.mask(SHAPE) != 0, right.mask(SHAPE) != 0
    expected = {'union': a | b, 'intersection': a & b, 'subtraction': a & ~b, 'xor': a ^ b}[operation]
    result = _native.combine_selections(41, 33, [left, right], operation=operation)
    assert result.kind == 'mask'
    np.testing.assert_array_equal(result.mask(SHAPE) != 0, expected)

def test_native_session_and_data_history_preserve_editable_geometry():
    pixels = np.arange(33*41, dtype=np.uint16).reshape(SHAPE)
    owner = ij.Document(ij.Dataset(pixels, axes='YX'))
    original = box()
    moved = ij.SelectionEditor(original, SHAPE).rotated(23)
    edit = owner.begin_edit(); edit.select(original)
    edit.set_rois([ij.ROI('rounded', moved, attributes={'retained': True})])
    owner.commit(edit)
    edit = owner.begin_edit(); edit.select(moved); edit.set_property('edit', 1); owner.commit(edit)
    owner.undo(); assert owner.snapshot().selection == original
    owner.redo(); assert owner.snapshot().selection == moved
    result = ij.decode_session(ij.encode_session(ij.SessionCapture([owner]))).documents[0].snapshot()
    assert result.selection == moved
    assert result.previous_selection == original
    assert result.rois[0].selection == moved
    assert result.rois[0].attributes == {'retained': True}
    np.testing.assert_array_equal(result.dataset.numpy('YX'), pixels)
    np.testing.assert_array_equal(result.selection.mask(SHAPE), moved.mask(SHAPE))

def test_python_model_adapter_preserves_native_rounded_geometry():
    from imagejxx.ui.model import Selection
    value = box()
    model = Selection(value.kind, value.points, value.width, attributes=value.attributes)
    restored = Selection.from_dict(model.to_dict())
    assert restored.native() == value
    np.testing.assert_array_equal(restored.mask(SHAPE), value.mask(SHAPE))
