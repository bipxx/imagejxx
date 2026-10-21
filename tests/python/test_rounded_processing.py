"""Rounded execution membership and resource admission through public native APIs."""
import numpy as np
import pytest
import imagejxx as ij

SHAPE=(13,17)

def selection(case):
    diameter=0 if case=='zero' else 2**31-1 if case=='oversized' else 6
    left=4.99999999 if case=='precision' else -20 if case=='empty' else 2.25
    right=-2 if case=='empty' else 13.75
    value=ij.Selection('rounded_rectangle',[(left,1.5),(right,10.25)],attributes={'corner_diameter':diameter,'retained':'native'})
    return ij.SelectionEditor(value,SHAPE).rotated(23) if case=='rotated' else value

OPERATIONS=[('add',{'value':7}),('gaussian_blur_imagej',{'sigma':[1.,1.]})]
OPERATIONS += [('rank_filter_imagej',{'method':method,'radius':1.}) for method in ['minimum','maximum','mean','median','variance']]
OPERATIONS += [('remove_outliers_imagej',{}),('top_hat_imagej',{})]


def source(dtype,geometry,policy):
    pixels=((np.arange(2*2*3*13*17).reshape(2,2,3,13,17)*13)%251).astype(dtype)
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYX'))
    edit=owner.begin_edit()
    if policy=='active':edit.select(geometry)
    else:edit.set_rois([ij.ROI('rounded',geometry,t=1,c=1,z=2),ij.ROI('unmatched',selection('rotated'),t=0,c=1,z=1)])
    owner.commit(edit)
    request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,1,2),ij.PlanePosition(0,0,0)],roi=ij.RoiPolicy.ACTIVE_SELECTION if policy=='active' else ij.RoiPolicy.POSITIONED_ROIS)
    return owner,pixels,request


@pytest.mark.parametrize('case',['axis','rotated','zero','oversized','precision','empty'])
@pytest.mark.parametrize('policy',['active','positioned'])
@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('operation,parameters',OPERATIONS,ids=['add','gaussian','min','max','mean','median','variance','outliers','top-hat'])
def test_filters_match_explicit_mask_and_preserve_outside(case,policy,dtype,operation,parameters):
    geometry=selection(case);mask=geometry.mask(SHAPE)
    owner,pixels,request=source(dtype,geometry,policy)
    reference,_,_=source(dtype,ij.Selection.from_mask(mask),policy)
    # The reference changes only this native geometry carrier, not its samples,
    # target positions, operation or ImageJ numerical profile.
    before=owner.snapshot();target=ij.resolve_target(before,request,operation)
    expected=ij.prepare_operation(operation,ij.resolve_target(reference.snapshot(),request,operation),parameters).preview.dataset.numpy('TCZYX')
    app=ij.Application();app.adopt(owner)
    job=app.submit(operation,target,parameters)
    assert job.wait(5000) and job.info.state==ij.JobState.READY
    output=job.prepared.preview.dataset.numpy('TCZYX')
    np.testing.assert_array_equal(output,expected)
    for t,c,z in [(1,1,2),(0,0,0)]:
        membership=(mask!=0) if policy=='active' or t==1 else np.zeros(SHAPE,bool)
        np.testing.assert_array_equal(output[t,c,z][~membership],pixels[t,c,z][~membership])
    np.testing.assert_array_equal(output[0,1],pixels[0,1])
    assert owner.snapshot().shares_data(before)
    assert job.prepared.preview.selection==before.selection
    assert [(r.name,r.selection,r.t,r.c,r.z,r.attributes) for r in job.prepared.preview.rois]==[(r.name,r.selection,r.t,r.c,r.z,r.attributes) for r in before.rois]
    published=app.publish(job)
    np.testing.assert_array_equal(published.document.snapshot().dataset.numpy('TCZYX'),expected)


@pytest.mark.parametrize('operation,parameters',OPERATIONS,ids=['add','gaussian','min','max','mean','median','variance','outliers','top-hat'])
def test_budget_refuses_rounded_raster_before_source_reads(operation,parameters):
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(np.arange(13*17,dtype=np.uint16).reshape(SHAPE),axes='YX').observe_reads(reads))
    owner.select(selection('axis'));before=owner.snapshot()
    app=ij.Application();app.adopt(owner)
    app.job_scheduler.set_limits(ij.JobLimits(workers=1,memory_budget=256*1024))
    with pytest.raises(ValueError,match='memory budget'):
        app.submit(operation,app.resolve_target(),parameters,job_options=ij.JobOptions(resource_bytes=1))
    assert reads.counts().plane_reads==0 and reads.counts().region_reads==0
    assert not app.job_scheduler.jobs and app.job_scheduler.usage.reserved_bytes==0
    assert owner.snapshot().shares_data(before)


def test_whole_plane_ignores_unused_rounded_geometry():
    owner=ij.Document(ij.Dataset(np.zeros(SHAPE,np.uint16),axes='YX'));owner.select(selection('axis'))
    app=ij.Application();app.adopt(owner)
    app.job_scheduler.set_limits(ij.JobLimits(workers=1,memory_budget=256*1024))
    job=app.submit('add',app.resolve_target(ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)),{'value':1})
    assert job.wait(5000) and job.info.state==ij.JobState.READY
    assert job.info.resource_bytes<256*1024
    np.testing.assert_array_equal(job.prepared.preview.dataset.numpy('YX'),np.ones(SHAPE,np.uint16))


@pytest.mark.parametrize('case',['axis','rotated','zero','oversized'])
def test_remove_nans_preserves_rounded_mask_holes_and_ieee_bits(case):
    geometry=selection(case);mask=geometry.mask(SHAPE)
    pixels=np.full(SHAPE,7,np.float32);pixels.view(np.uint32)[6,7]=0xffc12345;pixels.view(np.uint32)[0,0]=0x80000000;pixels.view(np.uint32)[0,1]=0x7fc12345
    def execute(g):
        owner=ij.Document(ij.Dataset(pixels,axes='YX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32));owner.select(g)
        return ij.prepare_operation('remove_nans_imagej',ij.resolve_target(owner.snapshot()),{'radius':1.}).preview.dataset.numpy('YX')
    actual=execute(geometry);expected=execute(ij.Selection.from_mask(mask))
    assert actual.tobytes()==expected.tobytes()
    np.testing.assert_array_equal(actual.view(np.uint32)[mask==0],pixels.view(np.uint32)[mask==0])
