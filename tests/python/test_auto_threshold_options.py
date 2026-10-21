"""Endpoint exclusion: independent pinned histograms and literal mask/transaction checks."""
import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_auto_threshold_imagej import METHODS

FIXTURE=Path(__file__).parents[1]/'fixtures/auto-threshold-options-imagej154r.json'
DATA=json.loads(FIXTURE.read_bytes())

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('row',DATA['cases'],ids=lambda r:r['id'])
def test_pinned_endpoint_histograms(row,method):
    h=[0]*row['bins']
    for k,v in row['nonzero'].items():h[int(k)]=v
    before=h.copy();options={k:row[k] for k in ['ignore_black','ignore_white']}
    if row['thresholds'] is None:
        with pytest.raises(ValueError,match='no samples'):ij.auto_threshold_histogram_imagej(h,method,**options)
    else:assert ij.auto_threshold_histogram_imagej(h,method,**options)==row['thresholds'][method]
    assert h==before

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
@pytest.mark.parametrize('dtype', [np.uint8,np.uint16,np.float32])
def test_all_methods_exclude_histogram_edges_but_mask_entire_image(method,dtype):
    source=np.array([[0,50,100,255]],dtype)
    bins=256
    if dtype==np.uint16:source=np.array([[0,5000,10000,65535]],dtype);bins=65536
    original=source.tobytes()
    threshold=ij.auto_threshold(source,method,bins=bins,ignore_black=True,ignore_white=True)
    assert source[0,1]<=threshold<source[0,2]
    for bright,expected in [(True,[[0,0,255,255]]),(False,[[255,255,0,0]])]:
        actual=ij.auto_mask(source,method,bins=bins,bright=bright,ignore_black=True,ignore_white=True)
        np.testing.assert_array_equal(actual,np.array(expected,np.uint8))
        np.testing.assert_array_equal(ij.execute_operation('auto_mask',source,dict(method=method,bins=bins,bright=bright,ignore_black=True,ignore_white=True)),actual)
    assert source.tobytes()==original

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
def test_empty_eligible_histogram_never_publishes(method):
    source=np.array([[0,255]],np.uint8)
    for fn in [ij.auto_threshold,ij.auto_mask]:
        with pytest.raises(ValueError,match='no samples'):fn(source,method,ignore_black=True,ignore_white=True)
    owner=ij.Document(ij.Dataset(source,axes='YX'));app=ij.Application();app.adopt(owner);before=owner.snapshot()
    job=app.submit('auto_mask',app.resolve_target(),dict(method=method,ignore_black=True,ignore_white=True))
    job.wait()
    assert job.info.state==ij.JobState.FAILED
    with pytest.raises((ValueError,RuntimeError)):app.publish(job)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
def test_exclusion_reserves_tables_before_job_admission(method):
    owner=ij.Document(ij.Dataset(np.array([[0,5,255]],np.uint8),axes='YX'))
    app=ij.Application();app.adopt(owner);before=owner.snapshot()
    app.job_scheduler.set_limits(ij.JobLimits(workers=1,memory_budget=1024*1024))
    with pytest.raises(ValueError,match='memory budget'):
        app.submit('auto_mask',app.resolve_target(),dict(method=method,bins=65536,ignore_black=True))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
def test_invalid_options_fail_before_publication(method):
    source=np.array([[0,100,255]],np.uint8)
    for n in [1,-1,65537]:
        with pytest.raises(ValueError):ij.auto_mask(source,method,bins=n,ignore_black=True)
    for key in ['ignore_black','ignore_white']:
        for value in [0,1,'true',None]:
            with pytest.raises(TypeError):ij.auto_mask(source,method,**{key:value})
            with pytest.raises((TypeError,ValueError)):ij.execute_operation('auto_mask',source,dict(method=method,**{key:value}))

def test_reference_identity_and_default_schema():
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest()=='03543d3a9f0eeb82a2653ed51c61510ae615d2f84ba2472a5fe33aea0a9d3bd3'
    assert len(DATA['cases'])==232
    catalog=next(c for c in ij.operation_catalog() if c['id']=='auto_mask')
    flags=[p for p in catalog['parameters'] if p['name'] in ['ignore_black','ignore_white']]
    assert len(flags)==2 and all(p['kind']=='bool' and p['default'] is False for p in flags)

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('black,white',[(True,False),(False,True),(True,True)])
def test_recorded_flags_survive_workflow_serialization_and_replay(method,black,white,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    from imagejxx.ui import workflows
    editor=Editor(preferences=Preferences(tmp_path/'prefs.json'))
    source=np.arange(256,dtype=np.uint8).reshape(16,16)
    editor.add_document(Document(source));editor.recording=True
    editor.apply_operation('auto_mask',dict(method=method,ignore_black=black,ignore_white=white),asynchronous=False)
    row=next(r for r in DATA['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==black and r['ignore_white']==white)
    expected=np.where(source>row['thresholds'][method],255,0).astype(np.uint8)
    np.testing.assert_array_equal(editor.document.frame,expected)
    assert workflows.loads(workflows.dumps(editor.workflow))==editor.workflow
    assert editor.workflow[0]['parameters']['ignore_black']==black and editor.workflow[0]['parameters']['ignore_white']==white
    editor.recording=False;editor.action('undo');np.testing.assert_array_equal(editor.document.frame,source)
    editor.action('run_workflow');editor.wait()
    np.testing.assert_array_equal(editor.document.frame,expected)
    assert len(editor.workflow)==1

@pytest.mark.parametrize('method',METHODS)
def test_exclusion_cancellation_preserves_counts(method):
    h=[1]*256;token=ij.CancellationToken()
    def progress(done,total):
        if done==total:token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.auto_threshold_histogram_imagej(h,method,ignore_black=True,ignore_white=True,cancellation=token,progress=progress)
    assert h==[1]*256
