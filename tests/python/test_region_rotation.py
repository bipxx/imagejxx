"""Full regional rotation, actual upstream observations and actual shared UI."""
import base64
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_selection_region import sample

FIXTURE=json.loads((Path(__file__).parents[1]/'fixtures/selection-region-rotation-imagej154r.json').read_bytes())


def region(kind=0):
    y,x=np.indices((3,9))
    pixels=(~((y==1)&(x>=3)&(x<=5)) if kind==0 else (x+y)%3==0 if kind==1 else ((x==0)&(y==0))|((x==8)&(y==2)))
    value=ij.SelectionRegion(ij.Selection.from_mask(pixels.astype(np.uint8)),(3,9),[-2,2]).as_selection()
    return ij.SelectionEditor(value,(10,12)).properties(2.5,{'label':'retained','opaque':{'uint':2**64-1,'items':[True,'hello']}})


@pytest.mark.parametrize('row',FIXTURE['cases'],ids=lambda r:f"k{r['kind']}-a{r['angle']}-p{r['pivot']}")
def test_full_imagej_rotation_pixels(row):
    before=region(row['kind']);editor=ij.SelectionEditor(before,(10,12))
    estimate=ij.estimate_selection_region_rotation(before,(10,12),row['angle'],row['pivot'])
    result=editor.rotated(row['angle'],row['pivot'])
    actual=sample(ij.SelectionRegion(result,(10,12)),FIXTURE['sample'])
    expected=np.unpackbits(np.frombuffer(base64.b64decode(row['mask']),np.uint8)).reshape(64,64)!=0
    np.testing.assert_array_equal(actual,expected)
    assert result.is_region and result.width==before.width and result.attributes==before.attributes
    assert result.to_dict()['origin']==estimate.origin and result.to_dict()['shape']==estimate.shape
    assert result==ij.Selection.from_dict(json.loads(json.dumps(result.to_dict())))
    assert editor.selection==before


@pytest.mark.parametrize('kind',range(3))
def test_four_quarter_turns_preserve_full_cells_and_style(kind):
    before=region(kind);result=before
    for _ in range(4):result=ij.SelectionEditor(result,(10,12)).rotated(90)
    assert result==before


@pytest.mark.parametrize('angle',[0,360,-720])
def test_identity_retains_storage_even_when_frame_has_zero_padding(angle):
    mask=np.zeros((5,7),np.uint8);mask[2,3]=1
    before=ij.SelectionRegion(ij.Selection.from_mask(mask),(5,7),[-20,30]).as_selection()
    assert ij.SelectionEditor(before,(10,12)).rotated(angle)==before


@pytest.mark.parametrize('angle',[0,23,90,180])
def test_empty_region_rotation_is_exact_noop(angle):
    before=ij.SelectionRegion(ij.Selection.from_mask(np.zeros((3,5),np.uint8)),(3,5),[-20,30]).as_selection()
    assert ij.SelectionEditor(before,(10,12)).rotated(angle)==before


@pytest.mark.parametrize('field,value',[('max_xy_pixels',0),('max_xy_pixels',27),('max_working_bytes',1),('max_vertices',0)])
def test_full_output_frame_admitted_under_limits(field,value):
    limits=ij.SelectionEditLimits();setattr(limits,field,value)
    # The 27-cell source fits, while the rotated output frame does not.
    with pytest.raises((ValueError,RuntimeError,OverflowError)):
        ij.estimate_selection_region_rotation(region(),(1,1),45,[200,0],limits)


@pytest.mark.parametrize('angle,pivot',[(float('nan'),None),(float('inf'),None),(45,[1e308,0]),(90,[2**40,0])])
def test_invalid_or_unrepresentable_rotation_refused(angle,pivot):
    with pytest.raises((ValueError,RuntimeError,OverflowError)):
        ij.SelectionEditor(region(),(10,12)).rotated(angle,pivot)


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_regional_pointer_and_numeric_rotation(context,tmp_path):
    if context=='imgui_cpp':row=json.loads(Path(os.environ['IMAGEJXX_REGION_ROTATION_CPP_REPORT']).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row=editor._saved_roi_geometry_ui_case('PX11',str(tmp_path/'case'))
        if isinstance(row,str):row=json.loads(row)
    assert row['regional_rotation_contract']=='p12s.region-rotation-ui.001'
    assert row['observation_state']=='observed' and row['window_error']==''
    before=row['fixture_sources']['source'];states=row['states']
    # Read the producer's captured state below without invoking its geometry.
    after=states['final']['documents']['source']
    assert len(row['drag_observations'])==1
    assert row['drag_observations'][0]['modifier']=='Shift'
    selections=[after['rois'][i]['selection'] for i in [0,3]]
    assert selections[0]==selections[1]
    value=selections[0];assert value['kind']=='region' and value['origin']==[1,-1] and value['shape']==[9,3]
    actual=np.unpackbits(np.frombuffer(base64.b64decode(value['mask']),np.uint8))[:27].reshape(9,3)
    y,x=np.indices((3,9));expected=np.rot90(~((y==1)&(x>=3)&(x<=5)),-1)
    np.testing.assert_array_equal(actual,expected)
    for key in ['planes','calibration','selection','previous_selection','overlays']:
        assert after[key]==before[key]
    assert after['rois'][1:3]==before['rois'][1:3]
    assert after['undo_steps']==before['undo_steps']+2 and after['redo_steps']==0
