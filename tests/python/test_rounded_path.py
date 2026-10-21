"""Continuous native rounded geometry, checked against pinned Java observations."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/rounded-path-imagej154r.json').read_bytes())

def frame(values):
    return ij.SelectionFrame(values[:2],values[2:4],values[4:6])

@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:r['id'])
def test_reference_path_and_shape_mask(row):
    result=ij.rounded_rectangle_path(frame(row['frame']),row['diameter'])
    np.testing.assert_allclose(result,row['path'],rtol=2e-12,atol=2e-12)
    actual=ij.imagej_shape_mask(result,41,33).mask((33,41))
    expected=np.array([255 if v=='1' else 0 for v in row['mask']],dtype=np.uint8).reshape(33,41)
    np.testing.assert_array_equal(actual,expected)

@pytest.mark.parametrize('value',[-1,1.5,True,False,None,'4',2**31,2**100])
def test_invalid_diameter_is_not_coerced(value):
    with pytest.raises((TypeError,ValueError,OverflowError)):
        ij.rounded_rectangle_path(frame([0,0,1,0,0,1]),value)

@pytest.mark.parametrize('values',[[0,0,0,0,0,1],[0,0,1,0,1,1],[float('nan'),0,1,0,0,1],
                                 [0,0,float('inf'),0,0,1],[1e9+100,0,1,0,0,1],
                                 [0,0,1e308,0,0,1e308]])
def test_invalid_frames_fail_before_rasterization(values):
    with pytest.raises(ValueError):ij.rounded_rectangle_path(frame(values),5)

def test_continuous_geometry_keeps_wire_limits_separate():
    value=frame([8,8,4,0,0,3])
    assert ij.rounded_rectangle_path(value,60536)==ij.rounded_rectangle_path(value,2**31-1)
    with pytest.raises(ValueError):ij.imagej_rounded_rectangle_mask([4,5,8,6,60536],20,20)
    assert ij.rounded_rectangle_path(value,0)!=ij.rounded_rectangle_path(value,2)
