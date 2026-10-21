"""Recorded analysis intent: independent pooled-bin expectations and refusals."""
import json
import numpy as np
import pytest
import imagejxx as ij


def source(dtype=np.uint16):
    values=np.arange(48,dtype=dtype).reshape(2,2,3,2,2)
    observer=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(values,axes='TCZYX').observe_reads(observer))
    edit=owner.begin_edit()
    edit.select(ij.Selection('rectangle',[(0,0),(1,2)]))
    edit.set_rois([ij.ROI('left',ij.Selection('rectangle',[(0,0),(1,2)]),z=1)])
    owner.commit(edit)
    return owner,observer,values


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('roi',[ij.RoiPolicy.WHOLE_PLANE,ij.RoiPolicy.ACTIVE_SELECTION,ij.RoiPolicy.POSITIONED_ROIS])
@pytest.mark.parametrize('scope',[ij.TargetScope.CURRENT_PLANE,ij.TargetScope.ALL_Z,ij.TargetScope.RANGES,ij.TargetScope.POSITIONS])
@pytest.mark.parametrize('custom',[False,True])
def test_record_roundtrip(dtype,roi,scope,custom):
    owner,reads,values=source(dtype)
    kwargs={'roi':roi}
    positions=[(0,0,0)]
    if scope==ij.TargetScope.ALL_Z: positions=[(0,0,z) for z in range(3)]
    if scope==ij.TargetScope.RANGES:
        kwargs.update(t=ij.AxisRange(0,2),c=ij.AxisRange(1,2),z=ij.AxisRange(0,3,2))
        positions=[(t,1,z) for t in range(2) for z in (0,2)]
    if scope==ij.TargetScope.POSITIONS:
        positions=[(1,1,2),(0,0,1)]
        kwargs['positions']=[ij.PlanePosition(*p) for p in positions]
    request=ij.TargetRequest(scope,**kwargs)
    options=ij.TargetHistogramOptions()
    if custom:
        options.binning=ij.HistogramBinning.UniformRange;options.bins=6;options.range=(0.,48.)
    before=reads.counts().plane_reads
    command=ij.resolve_histogram_command(owner.snapshot(),options,request,True)
    record=json.loads(json.dumps(command.record()))
    ij.validate_histogram_command_record(record)
    replay=ij.histogram_command_from_record(owner.snapshot(),record)
    assert reads.counts().plane_reads==before and replay.log_counts
    result=ij.execute_histogram_command(replay)
    members=[]
    for t,c,z in positions:
        if roi==ij.RoiPolicy.POSITIONED_ROIS and z!=1:continue
        plane=values[t,c,z]
        members.extend((plane if roi==ij.RoiPolicy.WHOLE_PLANE else plane[:,:1]).ravel().tolist())
    assert result.finite_count==len(members) and result.sum==sum(members)
    if members:
        assert result.mean==pytest.approx(np.mean(members))
        assert result.stddev==pytest.approx(np.std(members))
    else:assert result.mean is None
    if custom:
        expected=np.histogram(members,bins=np.linspace(0,48,7))[0]
        assert result.histogram['counts'].tolist()==expected.tolist()
    assert owner.undo_steps==1
    # Public getters are copies and cannot alter recorded command intent.
    replay.options.bins=1
    detached=replay.record();detached['options']['bins']=17
    assert replay.record()==record


@pytest.mark.parametrize('mutate',[
    lambda r:r.update(schema_version=True),lambda r:r.update(schema_version=2),
    lambda r:r.update(log_counts=1),lambda r:r.update(junk=1),
    lambda r:r.update(pixel_type='rgb24'),lambda r:r.update(sample_policy='other'),
    lambda r:r['options'].update(bins=True),lambda r:r['options'].update(bins=256.),
    lambda r:r['options'].update(bins=1),lambda r:r['options'].update(range=[0.,1.]),
    lambda r:r['options'].update(binning='other'),lambda r:r['target'].update(document_id=True),
    lambda r:r['target'].update(positions=[[0,0,0]]),
])
def test_malformed_original_refused_before_rebind(mutate):
    owner,reads,_=source();record=ij.resolve_histogram_command(owner.snapshot()).record()
    mutate(record);before=reads.counts().plane_reads
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.validate_histogram_command_record(record)
    with pytest.raises((ValueError,TypeError,OverflowError)):
        ij.rebind_histogram_command(owner.snapshot(),record,ij.TargetRequest())
    assert reads.counts().plane_reads==before


def test_identity_navigation_selection_and_explicit_rebind():
    owner,reads,_=source();command=ij.resolve_histogram_command(owner.snapshot());record=command.record()
    display=owner.snapshot().display;display.crosshair=(0,0,2);owner.set_display(display)
    assert ij.histogram_command_from_record(owner.snapshot(),record).target.position(0).z==0
    other,_,_=source()
    with pytest.raises(ValueError):ij.histogram_command_from_record(other.snapshot(),record)
    owner.select(ij.Selection('rectangle',[(1,0),(2,2)]))
    with pytest.raises(ij.RevisionConflict):ij.histogram_command_from_record(owner.snapshot(),record)
    rebound=ij.rebind_histogram_command(owner.snapshot(),record,ij.TargetRequest())
    assert rebound.target.position(0).z==2
    assert ij.execute_histogram_command(rebound).mean==10.
    assert ij.execute_histogram_command(command).mean==1.


def test_limits_pre_cancel_and_callback_freeze():
    owner,reads,_=source();command=ij.resolve_histogram_command(owner.snapshot())
    cap=ij.HistogramCommandLimits();cap.max_record_bytes=1
    before=reads.counts().plane_reads
    for action in [lambda:ij.resolve_histogram_command(owner.snapshot(),limits=cap),
                   lambda:ij.estimate_histogram_command(command,cap),
                   lambda:ij.execute_histogram_command(command,cap)]:
        with pytest.raises(ValueError):action()
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.execute_histogram_command(command,cancellation=token)
    assert reads.counts().plane_reads==before
    cap=ij.HistogramCommandLimits();events=[]
    def progress(n,total):
        events.append((n,total));cap.processing.max_pixels=0
    assert ij.execute_histogram_command(command,cap,progress=progress).mean==1.
    assert events[0]==(0,1) and events[-1]==(1,1)
    with pytest.raises(ValueError):ij.execute_histogram_command(command,cap)


@pytest.mark.parametrize('field,value',[
    ('bins',0),('bins',65537),('bins',True),('bins',4.),
    ('range',[0.,0.]),('range',[1.,0.]),('range',[True,48.]),
    ('range',[0.,float('inf')]),('range',[0.,float('nan')]),
    ('range',[0.,2**53+1]),('range',[0.]),('range',None),
])
def test_uniform_record_bounds_are_strict(field,value):
    owner,reads,_=source();options=ij.TargetHistogramOptions()
    options.binning=ij.HistogramBinning.UniformRange;options.bins=4;options.range=(0.,48.)
    record=ij.resolve_histogram_command(owner.snapshot(),options).record()
    record['options'][field]=value;before=reads.counts().plane_reads
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.validate_histogram_command_record(record)
    assert reads.counts().plane_reads==before


@pytest.mark.parametrize('moment',['start','finish','exception'])
def test_execution_callback_failure_never_publishes(moment):
    owner,reads,_=source();command=ij.resolve_histogram_command(owner.snapshot())
    before=owner.snapshot();n=reads.counts().plane_reads;token=ij.CancellationToken()
    def progress(done,total):
        if moment=='exception':raise LookupError('analysis callback')
        if done==(0 if moment=='start' else 1):token.cancel()
    with pytest.raises(LookupError if moment=='exception' else ij.OperationCancelled):
        ij.execute_histogram_command(command,cancellation=token,progress=progress)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==1
    assert reads.counts().plane_reads-n==(2 if moment=='finish' else 0)


def test_quiet_nan_storage_claim_roundtrip_preserves_payload_bits():
    bits=np.array([[0x7fc12345,0x80000000,0x3f800000,0xffc54321]],dtype=np.uint32)
    data=ij.Dataset(bits.view(np.float32),axes='YX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    owner=ij.Document(data);command=ij.resolve_histogram_command(owner.snapshot())
    replay=ij.histogram_command_from_record(owner.snapshot(),command.record())
    result=ij.execute_histogram_command(replay)
    assert result.finite_count==2 and result.nan_count==2 and result.mean==.5
    assert data.numpy('YX').view(np.uint32).tobytes()==bits.tobytes()
    bad=command.record();bad['sample_policy']='finite_only'
    with pytest.raises(ValueError):ij.histogram_command_from_record(owner.snapshot(),bad)


def test_whole_plane_replay_ignores_selection_and_log_does_not_change_counts():
    owner,_,_=source();request=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    command=ij.resolve_histogram_command(owner.snapshot(),request=request)
    owner.select(ij.Selection('rectangle',[(1,0),(2,2)]))
    replay=ij.histogram_command_from_record(owner.snapshot(),command.record())
    logarithmic=ij.resolve_histogram_command(owner.snapshot(),request=request,log_counts=True)
    a=ij.execute_histogram_command(replay);b=ij.execute_histogram_command(logarithmic)
    assert a.histogram['counts'].tolist()==b.histogram['counts'].tolist() and a.mean==1.5
    for value in [1,None,'true',np.bool_(True)]:
        with pytest.raises(TypeError):ij.resolve_histogram_command(owner.snapshot(),log_counts=value)


def test_record_budget_and_recursive_python_records():
    owner,reads,_=source();record=ij.resolve_histogram_command(owner.snapshot()).record()
    before=reads.counts().plane_reads
    with pytest.raises(ValueError):ij.validate_histogram_command_record(record,1)
    with pytest.raises(TypeError):ij.validate_histogram_command_record(record,True)
    record['extra']='x'*1500000
    with pytest.raises(ValueError):ij.validate_histogram_command_record(record)
    record['extra']=record
    with pytest.raises(ValueError):ij.validate_histogram_command_record(record)
    assert reads.counts().plane_reads==before


def test_estimated_combined_working_limit_boundary():
    owner,reads,_=source();command=ij.resolve_histogram_command(owner.snapshot())
    estimate=ij.estimate_histogram_command(command);limits=ij.HistogramCommandLimits()
    limits.processing.max_working_bytes=estimate.working_bytes-1
    with pytest.raises(ValueError):ij.estimate_histogram_command(command,limits)
    limits.processing.max_working_bytes=estimate.working_bytes
    assert ij.estimate_histogram_command(command,limits).working_bytes==estimate.working_bytes
    assert ij.execute_histogram_command(command,limits).mean==1.
