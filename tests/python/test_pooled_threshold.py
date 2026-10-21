"""Frozen target pooling, with independent histogram and mask expectations."""
import numpy as np
import pytest
import imagejxx as ij
from test_auto_threshold_imagej import DATA as ORIGINAL,METHODS
from test_auto_threshold_options import DATA as OPTIONS

def controller(data,axes='ZYX',request=None):
    owner=ij.Document(ij.Dataset(data,axes=axes));app=ij.Application();app.adopt(owner)
    request=request or ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE)
    return app,owner,app.resolve_target(request)

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('black,white',[(False,False),(True,False),(False,True),(True,True)])
@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32,np.float64])
def test_shared_threshold_from_exact_global_histogram(method,black,white,dtype):
    base=np.arange(256).reshape(4,8,8)
    if dtype==np.uint16:data=(base*257).astype(dtype);low,step=-.5,256.
    elif np.issubdtype(dtype,np.floating):data=((base-128)*.25).astype(dtype);low,step=-32.,63.75/256
    else:data=base.astype(dtype);low,step=-.5,1.
    row=next(r for r in OPTIONS['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==black and r['ignore_white']==white)
    expected_level=low+(row['thresholds'][method]+.5)*step
    app,owner,target=controller(data);before=owner.snapshot()
    result=ij.pooled_auto_threshold(target,method,ignore_black=black,ignore_white=white)
    assert result.threshold==expected_level and result.selected_samples==256 and result.working_bytes>0
    job=app.submit('auto_mask',target,dict(method=method,ignore_black=black,ignore_white=white,stack_histogram=True))
    job.wait();assert job.info.state==ij.JobState.READY
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    expected=(data>expected_level).astype(np.uint8)*255
    np.testing.assert_array_equal(job.prepared.preview.dataset.numpy('ZYX'),expected)
    app.publish(job);np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'),expected)
    app.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'),data)
    app.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'),expected)

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
def test_image_method_compatibility_over_logical_concatenation(method):
    data=np.random.default_rng(2057).integers(1,255,(3,5,7),dtype=np.uint8)
    app,owner,target=controller(data)
    expected=ij.auto_threshold(data.reshape(1,-1),method)
    assert ij.pooled_auto_threshold(target,method).threshold==expected
    job=app.submit('auto_mask',target,dict(method=method,stack_histogram=True,bright=False));job.wait();app.publish(job)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'),np.where(data<=expected,255,0).astype(np.uint8))

def test_scientific_positions_and_active_roi_control_estimation_and_writes():
    data=np.arange(2*2*3*4*5,dtype=np.uint16).reshape(2,2,3,4,5)
    app,owner,_=controller(data,'TCZYX')
    edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[(1,1),(4,3)]));owner.commit(edit)
    request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,0,0),ij.PlanePosition(0,1,2)],roi=ij.RoiPolicy.ACTIVE_SELECTION)
    target=app.resolve_target(request);before=owner.snapshot()
    selected=np.concatenate([data[1,0,0,1:3,1:4].ravel(),data[0,1,2,1:3,1:4].ravel()])
    level=float(selected.mean());result=ij.pooled_auto_threshold(target,'mean')
    assert result.threshold==level and result.selected_samples==12
    job=app.submit('auto_mask',target,dict(method='mean',stack_histogram=True));job.wait();app.publish(job)
    expected=data.copy()
    for t,c,z in [(1,0,0),(0,1,2)]:expected[t,c,z,1:3,1:4]=np.where(data[t,c,z,1:3,1:4]>level,255,0)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),expected)
    assert owner.snapshot().selection.kind==before.selection.kind
    assert owner.snapshot().selection.points==before.selection.points

def test_captured_source_survives_navigation_and_rejects_stale_publication():
    data=np.arange(256,dtype=np.uint8).reshape(4,8,8);app,owner,target=controller(data)
    changed=[]
    def checkpoint():
        if not changed:
            changed.append(True);display=owner.snapshot().display;display.crosshair=[1,1,3];owner.set_display(display)
    assert ij.pooled_auto_threshold(target,'mean',checkpoint=checkpoint).threshold==127.5
    assert owner.snapshot().display.crosshair[2]==3
    job=app.submit('auto_mask',target,dict(method='mean',stack_histogram=True));job.wait()
    edit=owner.begin_edit();edit.set_title('changed');owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):app.publish(job)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('ZYX'),data)

def test_preflight_cancellation_and_callback_failure_are_detached():
    data=np.arange(256,dtype=np.uint8).reshape(4,8,8);app,owner,target=controller(data);before=owner.snapshot();calls=[]
    def checkpoint():calls.append(1)
    with pytest.raises(ValueError,match='working-byte'):ij.pooled_auto_threshold(target,max_working_bytes=1,checkpoint=checkpoint)
    assert calls==[]
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.pooled_auto_threshold(target,cancellation=token,checkpoint=checkpoint)
    assert calls==[]
    token=ij.CancellationToken()
    def cancel():
        calls.append(1)
        if len(calls)==5:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.pooled_auto_threshold(target,cancellation=token,checkpoint=cancel)
    def fail():raise RuntimeError('pooled checkpoint')
    with pytest.raises(RuntimeError,match='pooled checkpoint'):ij.pooled_auto_threshold(target,checkpoint=fail)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0

def test_empty_and_empty_after_exclusion_never_publish():
    app,owner,target=controller(np.zeros((3,2,2),np.uint8));before=owner.snapshot()
    with pytest.raises(ValueError,match='no samples'):ij.pooled_auto_threshold(target,'huang',ignore_black=True)
    job=app.submit('auto_mask',target,dict(method='huang',stack_histogram=True,ignore_black=True));job.wait()
    assert job.info.state==ij.JobState.FAILED
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(np.zeros((2,2),np.uint8)));owner.commit(edit)
    target=app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.ACTIVE_SELECTION))
    with pytest.raises(ValueError,match='no selected samples'):ij.pooled_auto_threshold(target)

def test_pooling_admission_and_invalid_requests():
    app,owner,target=controller(np.zeros((2,2,2),np.uint8));before=owner.snapshot()
    for bins in [1,-1,65537]:
        with pytest.raises(ValueError):ij.pooled_auto_threshold(target,bins=bins)
        with pytest.raises(ValueError):app.submit('auto_mask',target,dict(stack_histogram=True,bins=bins))
    app.job_scheduler.set_limits(ij.JobLimits(workers=1,memory_budget=1024*1024))
    with pytest.raises(ValueError,match='memory budget'):app.submit('auto_mask',target,dict(stack_histogram=True,bins=65536))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0

@pytest.mark.parametrize('method',ij.THRESHOLD_METHODS)
def test_standalone_progress_and_reentry_are_one_task(method):
    data=np.zeros(256,np.uint8);data[128:]=200
    app,owner,target=controller(data.reshape(4,8,8));events=[]
    def progress(done,total):
        events.append((done,total))
        assert ij.pooled_auto_threshold(target,'mean').threshold==100
    ij.pooled_auto_threshold(target,method,progress=progress)
    assert events==[(0,1),(1,1)]
    token=ij.CancellationToken()
    def cancel(done,total):
        if done==total:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.pooled_auto_threshold(target,method,cancellation=token,progress=cancel)

def test_managed_progress_counts_planes_separately_from_histogram_solver():
    app,owner,target=controller(np.arange(256,dtype=np.uint8).reshape(4,8,8));events=[]
    app.prepare('auto_mask',target,dict(method='huang',stack_histogram=True),progress=lambda done,total:events.append((done,total)))
    assert events==[(i,4) for i in range(5)]

@pytest.mark.parametrize('method',METHODS)
@pytest.mark.parametrize('both',[False,True])
def test_pooled_workflow_serialization_and_replay(method,both,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    from imagejxx.ui.persistence import Preferences
    from imagejxx.ui import workflows
    data=np.arange(256,dtype=np.uint8).reshape(4,8,8)
    editor=Editor(preferences=Preferences(tmp_path/'prefs.json'))
    editor.add_document(Document(data,is_stack=True));editor.recording=True
    editor.apply_operation('auto_mask',dict(method=method,stack_histogram=True,ignore_black=both,ignore_white=both),True,asynchronous=False)
    row=next(r for r in OPTIONS['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==both and r['ignore_white']==both)
    expected=np.where(data>row['thresholds'][method],255,0).astype(np.uint8)
    np.testing.assert_array_equal(editor.document.data,expected)
    assert len(editor.workflow)==1 and editor.workflow[0]['all_slices']
    assert editor.workflow[0]['parameters']['stack_histogram']
    assert workflows.loads(workflows.dumps(editor.workflow))==editor.workflow
    editor.recording=False;editor.action('undo')
    np.testing.assert_array_equal(editor.document.data,data)
    editor.action('run_workflow');editor.wait()
    np.testing.assert_array_equal(editor.document.data,expected)
