"""Automatic predicates must map exactly to inclusive manual display bounds."""
import numpy as np
import pytest
import imagejxx as ij
from test_auto_threshold_imagej import METHODS
from test_auto_threshold_options import DATA

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('bright',[True,False])
@pytest.mark.parametrize('black,white',[(False,False),(True,False),(False,True),(True,True)])
@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32,np.float64])
def test_inclusive_range_matches_exact_oracle_predicate(method,bright,black,white,dtype):
    base=np.arange(256).reshape(16,16)
    if dtype==np.uint16:source=(base*257).astype(dtype);low,step=-.5,256.
    elif np.issubdtype(dtype,np.floating):source=((base-128)*.25).astype(dtype);low,step=-32.,63.75/256
    else:source=base.astype(dtype);low,step=-.5,1.
    data=ij.Dataset(source,axes='YX');original=source.tobytes()
    row=next(r for r in DATA['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==black and r['ignore_white']==white)
    level=low+(row['thresholds'][method]+.5)*step
    result=ij.automatic_threshold_display(data,method,bright=bright,ignore_black=black,ignore_white=white)
    assert result.threshold==level
    expected=((source>level)==bright).astype(np.uint8)*255
    np.testing.assert_array_equal(ij.manual_threshold_mask(source,result.range),expected)
    assert result.matched_count==np.count_nonzero(expected) and result.working_bytes>0
    assert source.tobytes()==original

@pytest.mark.parametrize('bright',[True,False])
def test_float64_maximum_can_encode_an_exact_empty_interval(bright):
    source=np.full((2,2),np.finfo(np.float64).max)
    result=ij.automatic_threshold_display(ij.Dataset(source,axes='YX'),'mean',bright=bright)
    assert result.threshold==np.finfo(np.float64).max
    assert result.matched_count==(0 if bright else 4)
    assert np.isfinite(result.range.lower) and np.isfinite(result.range.upper)
    np.testing.assert_array_equal(ij.manual_threshold_mask(source,result.range),np.full((2,2),0 if bright else 255,np.uint8))

def test_scientific_position_and_source_ownership():
    source=np.arange(2*2*3*4*5,dtype=np.float32).reshape(2,2,3,4,5)
    data=ij.Dataset(source,axes='TCZYX');plane=source[1,0,2].copy()
    def mutate():source[:]=999
    result=ij.automatic_threshold_display(data,'mean',position=ij.PlanePosition(1,0,2),checkpoint=mutate)
    assert result.threshold==plane.mean() and result.matched_count==10
    np.testing.assert_array_equal(data.plane('xy',2,t=1,c=0),plane)

def test_preflight_cancellation_progress_and_callback_reentry():
    source=ij.Dataset(np.arange(256,dtype=np.uint8).reshape(16,16),axes='YX');calls=[]
    for args in [dict(max_pixels=1),dict(max_working_bytes=1),dict(bins=1),dict(bins=65537),dict(method='bad')]:
        with pytest.raises(ValueError):ij.automatic_threshold_display(source,checkpoint=lambda:calls.append(1),**args)
    assert calls==[]
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.automatic_threshold_display(source,cancellation=token,checkpoint=lambda:calls.append(1))
    assert calls==[]
    events=[]
    def progress(done,total):
        events.append((done,total));assert ij.automatic_threshold_display(source,'mean').threshold==127.5
    ij.automatic_threshold_display(source,progress=progress)
    assert events==[(0,1),(1,1)]
    token=ij.CancellationToken()
    def cancel(done,total):
        if done==total:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.automatic_threshold_display(source,cancellation=token,progress=cancel)
    def fail():raise RuntimeError('auto display checkpoint')
    with pytest.raises(RuntimeError,match='auto display checkpoint'):ij.automatic_threshold_display(source,checkpoint=fail)

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
def test_all_methods_have_compatible_binary_and_display_membership(method):
    source=np.zeros((4,8),np.uint8);source[:,4:]=200
    result=ij.automatic_threshold_display(ij.Dataset(source,axes='YX'),method)
    np.testing.assert_array_equal(ij.manual_threshold_mask(source,result.range),ij.auto_mask(source,method))

def test_python_ui_automation_changes_only_display(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    editor=Editor(preferences=Preferences(tmp_path/'prefs.json'))
    source=np.arange(256,dtype=np.uint8).reshape(16,16);editor.add_document(Document(source))
    editor.action('show_manual_threshold');editor.action('auto_manual_threshold',method='huang')
    np.testing.assert_array_equal(editor.document.frame,source)
    assert not editor.document.dirty
