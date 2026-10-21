"""Rounded saved-area resource, callback and provider boundaries through public APIs."""
from copy import deepcopy
import pytest
import imagejxx as ij
from test_saved_roi_geometry import _provider_module,_fixture


def rounded(rotated,diameter):
    value=ij.Selection('rounded_rectangle',[(2,2),(10,8)],1.5,attributes={'corner_diameter':diameter,'future':{'unsigned':2**64-1,'label':'kept'}})
    return ij.SelectionEditor(value,(10,12)).rotated(23) if rotated else value


def setup(rotated,diameter,dtype='uint16',samples=1):
    dataset,provider=_provider_module().make_source(dtype,samples)
    app,owner,_=_fixture(target=rounded(rotated,diameter),dataset=dataset)
    original=owner.snapshot().rois[0].selection
    changed=ij.SelectionEditor(original,(10,12)).moved((.5,-.5),keep_inside=False)
    attributes=changed.attributes;attributes['corner_diameter']=3;changed=changed.with_attributes(attributes)
    return app,owner,provider,ij.SavedRoiGeometryRequest(0,changed)


def unread(provider):
    assert provider.counts=={'plane':0,'strip':0,'window':0}


@pytest.mark.parametrize('rotated',[False,True],ids=['axis','rotated'])
@pytest.mark.parametrize('diameter',[0,6,2**31-1])
@pytest.mark.parametrize('dtype',['uint8','uint16','float32','float64'])
@pytest.mark.parametrize('samples',[1,3,4])
def test_rounded_layouts_resolve_prepare_publish_undo_without_samples(rotated,diameter,dtype,samples):
    app,owner,provider,request=setup(rotated,diameter,dtype,samples);before=owner.snapshot()
    estimate=ij.estimate_saved_roi_geometry(before,request)
    exact=ij.SavedRoiGeometryLimits(max_xy_pixels=120,max_working_bytes=estimate.working_bytes,max_record_bytes=estimate.record_wire_bytes)
    command=ij.resolve_saved_roi_geometry(before,request,exact)
    assert command.record()['version']==2 and command.estimate.working_bytes==estimate.working_bytes
    ij.validate_saved_roi_geometry_record(command.record())
    replay=ij.saved_roi_geometry_from_record(before,command.record())
    rebound=ij.rebind_saved_roi_geometry(before,command.record(),request)
    assert replay.is_replay and not rebound.is_replay and replay.replacement==rebound.replacement==request.replacement
    capture=app.capture_saved_roi_geometry();probe=app.prepare_saved_roi_geometry(capture,command);peak=probe.working_bytes
    assert probe.active and probe.retained_working_bytes==peak
    del probe
    calls=[]
    with pytest.raises(ValueError):
        app.prepare_saved_roi_geometry(capture,command,limits=ij.SavedRoiGeometryLimits(max_working_bytes=peak-1),checkpoint=lambda:calls.append(1))
    assert calls==[] and owner.snapshot().shares_data(before)
    prepared=app.prepare_saved_roi_geometry(capture,command,limits=ij.SavedRoiGeometryLimits(max_working_bytes=peak))
    assert prepared.retained_working_bytes==peak
    publication=app.publish_saved_roi_geometry(prepared)
    assert publication.changed and not prepared.active and prepared.retained_working_bytes==0
    after=owner.snapshot()
    assert after.rois[0].selection==request.replacement and after.rois[0].attributes==before.rois[0].attributes
    assert [(r.name,r.selection,r.t,r.c,r.z,r.attributes) for r in after.rois[1:]]==[(r.name,r.selection,r.t,r.c,r.z,r.attributes) for r in before.rois[1:]]
    assert after.selection==before.selection and after.previous_selection==before.previous_selection
    assert after.dataset.shares_pixels(before.dataset) and owner.undo_steps==1 and owner.redo_steps==0
    owner.undo();assert owner.snapshot().rois[0].selection==before.rois[0].selection
    owner.redo();assert owner.snapshot().rois[0].selection==request.replacement
    unread(provider)


@pytest.mark.parametrize('rotated',[False,True],ids=['axis','rotated'])
@pytest.mark.parametrize('diameter',[0,6,2**31-1])
@pytest.mark.parametrize('boundary',['pixels','vertices','working','record','depth'])
def test_rounded_preflight_refuses_before_callbacks(rotated,diameter,boundary):
    app,owner,provider,request=setup(rotated,diameter);before=owner.snapshot()
    estimate=ij.estimate_saved_roi_geometry(before,request)
    kwargs={'pixels':{'max_xy_pixels':119},'vertices':{'max_vertices':1},'working':{'max_working_bytes':estimate.working_bytes-1},'record':{'max_record_bytes':estimate.record_wire_bytes-1},'depth':{'metadata_depth':1}}[boundary]
    calls=[]
    with pytest.raises(ValueError):
        ij.resolve_saved_roi_geometry(before,request,ij.SavedRoiGeometryLimits(**kwargs),checkpoint=lambda:calls.append(1),progress=lambda*a:calls.append(1),stage_progress=lambda*a:calls.append(1))
    assert calls==[] and owner.snapshot().shares_data(before) and owner.undo_steps==0
    unread(provider)


@pytest.mark.parametrize('rotated',[False,True],ids=['axis','rotated'])
@pytest.mark.parametrize('diameter',[0,6,2**31-1])
def test_rounded_owned_callbacks_and_precancel(rotated,diameter):
    app,owner,provider,request=setup(rotated,diameter);before=owner.snapshot();intended=request.replacement
    calls=[];token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.resolve_saved_roi_geometry(before,request,cancellation=token,checkpoint=lambda:calls.append(1))
    assert calls==[]
    limits=ij.SavedRoiGeometryLimits()
    def mutate():
        calls.append(app.snapshot().id);request.row_index=99;request.replacement=ij.Selection('line',[(0,0),(1,1)]);limits.max_working_bytes=1
    command=ij.resolve_saved_roi_geometry(before,request,limits,checkpoint=mutate)
    assert calls and command.row_index==0 and command.replacement==intended and command.limits.max_working_bytes==268435456
    token=ij.CancellationToken();stages=[]
    def cancel(name,done,total):
        stages.append((name,done,total))
        if done:token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.resolve_saved_roi_geometry(before,ij.SavedRoiGeometryRequest(0,intended),cancellation=token,stage_progress=cancel)
    assert any(done for _,done,_ in stages) and owner.snapshot().shares_data(before)
    with pytest.raises(ij.OperationCancelled):
        app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command,cancellation=token)
    # Already canceled controls refuse preparation before producing a holder.
    unread(provider)


@pytest.mark.parametrize('rotated',[False,True],ids=['axis','rotated'])
@pytest.mark.parametrize('diameter',[0,6,2**31-1])
@pytest.mark.parametrize('damage',['legacy_version','unknown_field','wrong_shape','fractional_corner'])
def test_rounded_original_record_refused_before_rebind_inputs_or_callbacks(rotated,diameter,damage):
    app,owner,provider,request=setup(rotated,diameter);before=owner.snapshot()
    record=deepcopy(ij.resolve_saved_roi_geometry(before,request).record())
    if damage=='legacy_version':record['version']=1
    elif damage=='unknown_field':record['extra']=True
    elif damage=='wrong_shape':record['source']['shape'][4]=0
    else:record['replacement']['attributes']['corner_diameter']=2.5
    calls=[]
    with pytest.raises(ValueError):
        ij.rebind_saved_roi_geometry(None,record,None,checkpoint=lambda:calls.append(1))
    assert calls==[] and owner.snapshot().shares_data(before)
    unread(provider)
