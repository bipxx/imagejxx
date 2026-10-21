"""Detached target histograms, independent sample/membership expectations."""
import csv
import io
import math
import numpy as np
import pytest
import imagejxx as ij

DTYPES=(np.uint8,np.uint16,np.float32)

def mapped(values,dtype):
    a=np.asarray(values,dtype=np.float64)
    return (a*257 if dtype==np.uint16 else a*.25-8 if dtype==np.float32 else a).astype(dtype)

def fixture(dtype=np.uint8,policy='whole'):
    values=np.empty((2,2,4,4,4),dtype)
    for t in range(2):
        for c in range(2):
            for z in range(4):values[t,c,z]=mapped(np.arange(16).reshape(4,4)+(2*t+(z-1)//2)*16 if c==1 and z in (1,3) else np.full((4,4),180+10*t+3*c+z),dtype)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(values,axes='TCZYX').observe_reads(reads));edit=owner.begin_edit()
    edit.select(ij.Selection('rectangle',[(0,0),(2,4)]))
    rois=[ij.ROI('overlap A',ij.Selection('rectangle',[(0,0),(3,4)]),c=1,z=1),ij.ROI('overlap B',ij.Selection('rectangle',[(1,0),(4,4)]),c=1,z=1),ij.ROI('partial',ij.Selection('rectangle',[(2,0),(4,4)]),t=0,c=1,z=3),ij.ROI('unused nonarea',ij.Selection('point',[(0,0)]),c=0)]
    edit.set_rois(rois);owner.commit(edit);owner=ij.Document(owner.snapshot())
    request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(1,2),z=ij.AxisRange(1,4,2),roi={'whole':ij.RoiPolicy.WHOLE_PLANE,'active':ij.RoiPolicy.ACTIVE_SELECTION,'positioned':ij.RoiPolicy.POSITIONED_ROIS}[policy])
    target=ij.resolve_target(owner.snapshot(),request)
    members=[]
    for t in range(2):
        for z in (1,3):
            plane=np.arange(16).reshape(4,4)+(2*t+(z-1)//2)*16
            if policy=='whole' or policy=='positioned' and z==1:members.extend(plane.ravel())
            elif policy=='active':members.extend(plane[:,:2].ravel())
            elif t==0:members.extend(plane[:,2:].ravel())
    return owner,reads,target,values,mapped(members,dtype)

def uniform(dtype=np.uint8):
    options=ij.TargetHistogramOptions();options.binning=ij.HistogramBinning.UniformRange;options.bins=8;options.range=tuple(float(x) for x in mapped([8,48],dtype));return options

@pytest.mark.parametrize('dtype',DTYPES)
@pytest.mark.parametrize('policy',['whole','active','positioned'])
@pytest.mark.parametrize('custom',[False,True])
def test_pooled_grid_roi_membership_statistics_and_read_budget(dtype,policy,custom):
    owner,reads,target,source,values=fixture(dtype,policy);before=owner.snapshot();n=reads.counts().plane_reads
    options=uniform(dtype) if custom else ij.TargetHistogramOptions();estimate=ij.estimate_target_histogram(target,options)
    assert estimate.plane_count==4 and estimate.plane_pixels==16 and estimate.total_pixels==64 and reads.counts().plane_reads==n
    result=ij.accumulate_target_histogram(target,options)
    assert reads.counts().plane_reads-n==(4 if custom or dtype==np.uint8 else 8)
    assert result.visited_count==result.finite_count==len(values) and result.nan_count==0
    assert result.mean==pytest.approx(float(values.astype(np.float64).mean())) and result.stddev==pytest.approx(float(values.astype(np.float64).std()))
    assert result.sum==float(values.astype(np.float64).sum()) and result.minimum==float(values.min()) and result.maximum==float(values.max())
    assert result.plane_finite_count==64 and result.plane_nan_count==0 and result.plane_minimum==float(mapped([0],dtype)[0]) and result.plane_maximum==float(mapped([63],dtype)[0])
    if custom:
        low,high=options.range;edges=np.linspace(low,high,9);counts=[0]*8
        for value in values:
            if low<=value<=high:counts[min(7,int((float(value)-low)/(high-low)*8))]+=1
        assert result.underflow_count==sum(values<low) and result.overflow_count==sum(values>high)
    else:
        bins=256;low=0. if dtype==np.uint8 else float(values.min());high=256. if dtype==np.uint8 else float(values.max())+(1 if dtype==np.uint16 else 0)
        edges=[low+i*((high-low)/256) for i in range(257)];counts=[0]*256
        for value in values:
            # UInt16 expected bins use exact integer-rational membership.
            index=int(value) if dtype==np.uint8 else (int(value)-int(low))*256//int(high-low) if dtype==np.uint16 else int((float(value)-low)*256/(high-low))
            counts[min(index,255)]+=1
    arrays=result.histogram;assert list(arrays['counts'])==counts and list(arrays['edges'])==list(edges)
    assert result.included_count==sum(counts) and result.included_count+result.underflow_count+result.overflow_count==len(values)
    assert result.target_record==target.record() and result.estimate.working_bytes==estimate.working_bytes
    rows=list(csv.DictReader(io.StringIO(ij.target_histogram_csv(result))));assert [int(r['count']) for r in rows]==counts
    arrays['counts'][0]=999;assert result.histogram['counts'][0]==counts[0]
    record=result.target_record;record['ranges'][0][0]=99;assert result.target_record==target.record()
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),source)

@pytest.mark.parametrize('dtype',DTYPES)
@pytest.mark.parametrize('value',[0,7])
def test_constants_and_empty_target_members_are_single_pass(dtype,value):
    pixels=np.full((3,2,4),mapped([value],dtype)[0],dtype);reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes='ZYX').observe_reads(reads))
    request=ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE);result=ij.accumulate_target_histogram(ij.resolve_target(owner.snapshot(),request))
    assert reads.counts().plane_reads==3 and result.finite_count==24 and result.stddev==0 and result.mean==float(pixels[0,0,0])
    assert sum(result.histogram['counts'])==24
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(np.zeros((2,4),np.uint8)));owner.commit(edit);request.roi=ij.RoiPolicy.ACTIVE_SELECTION
    n=reads.counts().plane_reads;result=ij.accumulate_target_histogram(ij.resolve_target(owner.snapshot(),request))
    assert reads.counts().plane_reads-n==3 and result.mean is result.stddev is result.minimum is result.maximum is None and result.sum==0
    assert result.finite_count==result.visited_count==0 and result.plane_finite_count==24
    np.testing.assert_array_equal(result.histogram['edges'],np.arange(257)/256)
    assert not result.histogram['counts'].any()

@pytest.mark.parametrize('dtype',DTYPES)
@pytest.mark.parametrize('custom',[False,True])
def test_one_plane_matches_existing_dataset_service(dtype,custom):
    owner,reads,target,source,values=fixture(dtype,'active');request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,1,3)],roi=ij.RoiPolicy.ACTIVE_SELECTION)
    target=ij.resolve_target(owner.snapshot(),request);options=uniform(dtype) if custom else ij.TargetHistogramOptions();result=ij.accumulate_target_histogram(target,options)
    single=ij.DatasetHistogramRequest();single.position=ij.PlanePosition(1,1,3);single.area_roi=owner.snapshot().selection;single.binning=options.binning;single.bins=options.bins;single.range=options.range
    expected=ij.accumulate_dataset_histogram(owner.snapshot().dataset,single)
    for field in ['histogram_origin','bin_size','minimum','maximum','mean','stddev','sum','visited_count','finite_count','included_count','nan_count','underflow_count','overflow_count','plane_minimum','plane_maximum','plane_finite_count','plane_nan_count','empty_finite']:assert getattr(result,field)==getattr(expected,field)
    for key in ['counts','edges']:np.testing.assert_array_equal(result.histogram[key],expected.histogram[key])
    assert ij.target_histogram_csv(result)==ij.dataset_histogram_csv(expected)

@pytest.mark.parametrize('kind',['pixels','bytes','bins','range','nonarea','nonarea_positioned','volume','destination'])
def test_invalid_or_underbudget_targets_refuse_before_callbacks_or_reads(kind):
    owner,reads,target,source,values=fixture();options=uniform();limits=ij.DatasetHistogramLimits()
    if kind=='pixels':limits.max_pixels=63
    if kind=='bytes':limits.max_working_bytes=ij.estimate_target_histogram(target,options).working_bytes-1
    if kind=='bins':options.bins=0
    if kind=='range':options.range=(1,1)
    if kind=='nonarea':
        edit=owner.begin_edit();edit.select(ij.Selection('line',[(0,0),(2,2)]));owner.commit(edit);target=ij.resolve_target(owner.snapshot())
    if kind=='nonarea_positioned':
        edit=owner.begin_edit();edit.set_rois([ij.ROI('matching point',ij.Selection('point',[(0,0)]),c=1,z=1)]);owner.commit(edit);req=ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.POSITIONED_ROIS);view=owner.snapshot().display;view.c=1;owner.set_display(view);target=ij.resolve_target(owner.snapshot(),req)
    if kind in ('volume','destination'):
        req=ij.TargetRequest(ij.TargetScope.CURRENT_PLANE,mode=ij.ExecutionMode.VOLUME if kind=='volume' else ij.ExecutionMode.PLANES,destination=ij.OutputDestination.NEW_DOCUMENT if kind=='destination' else ij.OutputDestination.IN_PLACE);target=ij.resolve_target(owner.snapshot(),req)
    events=[];n=reads.counts().plane_reads
    with pytest.raises((ValueError,OverflowError)):ij.accumulate_target_histogram(target,options,limits,progress=lambda *v:events.append(v),checkpoint=lambda:events.append('checkpoint'))
    assert reads.counts().plane_reads==n and events==[]

@pytest.mark.parametrize('when',['before','start','finish','checkpoint'])
def test_cancellation_and_callback_failure_are_detached(when):
    owner,reads,target,source,values=fixture();before=owner.snapshot();token=ij.CancellationToken();events=[]
    if when=='before':token.cancel()
    def progress(n,total):
        events.append((n,total))
        if (when=='start' and n==0) or (when=='finish' and n==1):token.cancel()
    def checkpoint():
        if when=='checkpoint':raise RuntimeError('declared callback failure')
    n=reads.counts().plane_reads
    with pytest.raises(RuntimeError):ij.accumulate_target_histogram(target,uniform(),cancellation=token,progress=progress,checkpoint=checkpoint)
    assert reads.counts().plane_reads-n==(4 if when=='finish' else 0) and owner.snapshot().shares_data(before) and owner.undo_steps==0
    if when=='before':assert events==[]

def test_callback_argument_mutation_does_not_retarget_or_change_options():
    owner,reads,target,source,values=fixture();before=owner.snapshot();options=uniform();limits=ij.DatasetHistogramLimits();events=[]
    def progress(n,total):
        events.append((n,total))
        if n==0:
            options.bins=0;options.range=(1,1);limits.max_pixels=1
            display=owner.snapshot().display;display.c=0;owner.set_display(display)
            edit=owner.begin_edit();edit.select(None);edit.set_property('pooled_callback',True);owner.commit(edit)
    result=ij.accumulate_target_histogram(target,options,limits,progress=progress)
    assert events==[(0,1),(1,1)] and result.options.bins==8 and result.options.range==[8,48]
    assert result.finite_count==64 and result.target_record==target.record() and owner.undo_steps==1
    assert result.target_record['source_revisions']!=[owner.snapshot().revisions.pixels,owner.snapshot().revisions.metadata,owner.snapshot().revisions.selection]

@pytest.mark.parametrize('policy',['whole','active','positioned'])
def test_quiet_nan_counts_and_bits_across_target_planes(policy):
    bits=np.array([[[0x7fc12345,0,0x3f800000,0xffc12345]],[[0x40000000,0x40400000,0x40800000,0x40a00000]]],np.uint32)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(bits.view(np.float32),axes='ZYX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32).observe_reads(reads));edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[(0,0),(2,1)]));edit.set_rois([ij.ROI('first',ij.Selection('rectangle',[(0,0),(2,1)]),z=0)]);owner.commit(edit)
    req=ij.TargetRequest(ij.TargetScope.ALL_Z,roi={'whole':ij.RoiPolicy.WHOLE_PLANE,'active':ij.RoiPolicy.ACTIVE_SELECTION,'positioned':ij.RoiPolicy.POSITIONED_ROIS}[policy]);result=ij.accumulate_target_histogram(ij.resolve_target(owner.snapshot(),req))
    values=[0,1,2,3,4,5] if policy=='whole' else [0,2,3] if policy=='active' else [0]
    assert result.finite_count==len(values) and result.nan_count==(2 if policy=='whole' else 1)
    assert result.mean==pytest.approx(np.mean(values)) and result.stddev==pytest.approx(np.std(values)) and result.sum==sum(values)
    assert result.plane_finite_count==6 and result.plane_nan_count==2
    assert reads.counts().plane_reads==(2 if policy=='positioned' else 4)
    assert owner.snapshot().dataset.numpy('ZYX').view(np.uint32).tobytes()==bits.tobytes()

@pytest.mark.parametrize('field,value',[('bins',True),('bins',1.5),('range',(False,1)),('range',(1+2j,3)),('range',(float('nan'),1))])
def test_options_strict_numeric_bridges(field,value):
    options=ij.TargetHistogramOptions()
    with pytest.raises((ValueError,TypeError)):setattr(options,field,value)
@pytest.mark.parametrize('custom',[False,True])
def test_all_nan_target_has_finite_empty_edges_and_no_statistics(custom):
    bits=np.full((2,2,3),0x7fc12345,np.uint32);reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(bits.view(np.float32),axes='ZYX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32).observe_reads(reads))
    target=ij.resolve_target(owner.snapshot(),ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE));options=uniform() if custom else ij.TargetHistogramOptions()
    result=ij.accumulate_target_histogram(target,options)
    assert reads.counts().plane_reads==2 and result.visited_count==result.nan_count==result.plane_nan_count==12
    assert result.finite_count==result.plane_finite_count==0 and result.mean is result.stddev is result.minimum is result.maximum is None
    assert result.sum==0 and not result.histogram['counts'].any() and np.isfinite(result.histogram['edges']).all()
    if not custom:np.testing.assert_array_equal(result.histogram['edges'],np.arange(257)/256)
    assert owner.snapshot().dataset.numpy('ZYX').view(np.uint32).tobytes()==bits.tobytes()
