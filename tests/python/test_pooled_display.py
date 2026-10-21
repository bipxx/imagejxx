"""Pooled display bounds, versioned scope and frozen multi-plane Apply."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_auto_threshold_options import DATA
from test_auto_threshold_imagej import METHODS


def target_for(pixels,axes="ZYX",request=None):
    owner=ij.Document(ij.Dataset(pixels,axes=axes));app=ij.Application();app.adopt(owner)
    target=app.resolve_target(request or ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE))
    return owner,app,target

@pytest.mark.parametrize("method",METHODS)
@pytest.mark.parametrize("bright",[True,False])
@pytest.mark.parametrize("black,white",[(False,False),(True,False),(False,True),(True,True)])
@pytest.mark.parametrize("dtype",[np.uint8,np.uint16,np.float32,np.float64])
def test_pooled_range_matches_pinned_global_histogram(method,bright,black,white,dtype):
    raw=np.arange(256).reshape(4,8,8)
    if dtype==np.uint16:pixels=(raw*257).astype(dtype);low,step=-.5,256.
    elif np.issubdtype(dtype,np.floating):pixels=((raw-128)*.25).astype(dtype);low,step=-32.,63.75/256
    else:pixels=raw.astype(dtype);low,step=-.5,1.
    owner,app,target=target_for(pixels);before=owner.snapshot()
    row=next(r for r in DATA["cases"] if r["input_id"]=="uniform-256-0" and r["ignore_black"]==black and r["ignore_white"]==white)
    level=low+(row["thresholds"][method]+.5)*step
    result=ij.pooled_automatic_threshold_display(target,method,bright=bright,ignore_black=black,ignore_white=white)
    assert result.threshold==level and result.stack_histogram and result.selected_samples==256
    expected=(pixels>level)==bright
    assert result.matched_count==int(expected.sum())
    for plane,mask in zip(pixels,expected):np.testing.assert_array_equal(ij.manual_threshold_mask(plane,result.range),mask.astype(np.uint8)*255)
    assert owner.snapshot().shares_data(before) and not owner.snapshot().dirty and owner.undo_steps==0
    assert owner.snapshot().display.properties==before.display.properties

@pytest.mark.parametrize("method",ij.THRESHOLD_METHODS)
def test_all_methods_binary_membership_compatibility(method):
    pixels=np.zeros((3,4,8),np.uint8);pixels[:,:,4:]=200
    owner,app,target=target_for(pixels)
    result=ij.pooled_automatic_threshold_display(target,method)
    expected=ij.auto_mask(pixels.reshape(12,8),method)
    np.testing.assert_array_equal(ij.manual_threshold_mask(pixels.reshape(12,8),result.range),expected)

@pytest.mark.parametrize("bright",[True,False])
def test_empty_float64_extreme_interval_and_progress(bright):
    owner,app,target=target_for(np.full((2,2,3),np.finfo(np.float64).max))
    progress=[];r=ij.pooled_automatic_threshold_display(target,"mean",bright=bright,progress=lambda d,t:progress.append((d,t)))
    assert progress==[(0,1),(1,1)] and r.matched_count==(0 if bright else 12)
    for plane in owner.snapshot().dataset.numpy("ZYX"):
        np.testing.assert_array_equal(ij.manual_threshold_mask(plane,r.range),np.full((2,3),0 if bright else 255,np.uint8))


def test_roi_estimation_range_covers_outside_members_and_arbitrary_positions():
    pixels=np.arange(2*2*3*4*5,dtype=np.float64).reshape(2,2,3,4,5)
    owner,app,_=target_for(pixels,"TCZYX");owner.select(ij.Selection("rectangle",[(1,1),(3,3)]))
    request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,0,0),ij.PlanePosition(0,1,2)],roi=ij.RoiPolicy.ACTIVE_SELECTION)
    target=app.resolve_target(request);r=ij.pooled_automatic_threshold_display(target,"mean")
    values=np.concatenate([pixels[1,0,0,1:3,1:3].ravel(),pixels[0,1,2,1:3,1:3].ravel()]);level=float(values.mean())
    assert r.threshold==level and r.selected_samples==8 and r.matched_count==np.count_nonzero(values>level)
    for plane in [pixels[1,0,0],pixels[0,1,2]]:np.testing.assert_array_equal(ij.manual_threshold_mask(plane,r.range),np.where(plane>level,255,0).astype(np.uint8))


def test_preflight_reads_cancellation_and_completion_refusal():
    reads=ij.DatasetReadObserver();data=ij.Dataset(np.arange(256,dtype=np.uint8).reshape(4,8,8),axes="ZYX").observe_reads(reads)
    owner=ij.Document(data);app=ij.Application();app.adopt(owner);target=app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z))
    calls=[]
    for kwargs in [dict(max_pixels=255),dict(max_working_bytes=1),dict(bins=1),dict(method="bad")]:
        with pytest.raises(ValueError):ij.pooled_automatic_threshold_display(target,checkpoint=lambda:calls.append(1),**kwargs)
    assert reads.counts().plane_reads==0 and calls==[]
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.pooled_automatic_threshold_display(target,cancellation=token)
    assert reads.counts().plane_reads==0
    token=ij.CancellationToken()
    def progress(done,total):
        assert ij.pooled_automatic_threshold_display(target,"mean").threshold==127.5
        if done==total:token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.pooled_automatic_threshold_display(target,cancellation=token,progress=progress)
    assert owner.undo_steps==0 and not owner.snapshot().dirty

@pytest.mark.parametrize("patch",[{"all_z":1},{"all_z":"true"},{"version":3},{"extra":False}])
def test_display_v2_remains_strict(patch):
    dataset=ij.Dataset(np.zeros((3,4,5),np.uint8),axes="ZYX");view=ij.DisplayState()
    record=dict(version=2,lower=0.,upper=1.,t=0,c=0,z=1,all_z=True);record.update(patch)
    view.properties={"manual_threshold":record}
    with pytest.raises(ValueError):ij.manual_threshold_state(view,dataset)


def test_all_z_state_session_and_subset_preserve_scope(tmp_path):
    from imagejxx import _desktop
    pixels=np.arange(5*4*5,dtype=np.uint16).reshape(5,4,5);owner,app,_=target_for(pixels)
    view=ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(30,70),ij.PlanePosition(0,0,2),all_z=True)
    owner.set_display(view);assert ij.manual_threshold_state(view,owner.snapshot().dataset).all_z
    editor=_desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    path=tmp_path/"pooled.ijx";editor.action("save_document",json.dumps(dict(path=str(path))))
    reopened=_desktop.Editor(str(tmp_path/"other.json"));reopened.open_file(str(path));restored=reopened.native_snapshot()
    assert ij.manual_threshold_state(restored.display,restored.dataset).all_z
    np.testing.assert_array_equal(restored.dataset.numpy("ZYX"),pixels)
    # The old anchor is removed, but the threshold covers all surviving Z.
    prepared=ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,2,ij.OutputDestination.IN_PLACE)))
    app.publish(prepared);state=ij.manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset)
    assert state.all_z and 0<=state.position.z<2 and (state.range.lower,state.range.upper)==(30,70)

@pytest.mark.parametrize("scope",[ij.TargetScope.ALL_Z,ij.TargetScope.RANGES,ij.TargetScope.POSITIONS])
def test_multiplane_manual_apply_record_preview_rebind_and_undo(scope):
    pixels=np.arange(2*2*3*4*5,dtype=np.uint16).reshape(2,2,3,4,5);owner,app,_=target_for(pixels,"TCZYX")
    view=owner.snapshot().display;view.t,view.c=1,0;view.crosshair=[0,0,2];owner.set_display(view)
    request=ij.TargetRequest(scope,roi=ij.RoiPolicy.WHOLE_PLANE)
    if scope==ij.TargetScope.RANGES:request.t=ij.AxisRange(1,2);request.c=ij.AxisRange(0,1);request.z=ij.AxisRange(0,3)
    if scope==ij.TargetScope.POSITIONS:request.positions=[ij.PlanePosition(1,0,z) for z in range(3)]
    source=owner.snapshot();command=ij.resolve_manual_threshold_command(source,ij.ManualThresholdRange(140,160),request)
    record=command.record();assert record["schema_version"]==2 and record["profile"]=="manual_raw_inclusive_v2"
    ij.validate_manual_threshold_command_record(record);ij.manual_threshold_command_from_record(source,record)
    forged=copy.deepcopy(record);forged["schema_version"]=1;forged["profile"]="manual_raw_inclusive_v1"
    with pytest.raises(ValueError):ij.validate_manual_threshold_command_record(forged)
    view=source.display;view.t,view.c=0,1;owner.set_display(view)
    prepared=app.prepare("threshold",command.target,command.parameters());expected=pixels.copy();expected[1,0]=np.where((pixels[1,0]>=140)&(pixels[1,0]<=160),255,0)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("TCZYX"),expected);app.publish(prepared)
    assert owner.undo_steps==1;np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),expected)
    with pytest.raises((ValueError,ij.RevisionConflict)):ij.manual_threshold_command_from_record(owner.snapshot(),record)
    app.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),pixels)
    destination=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,0,z) for z in range(3)],roi=ij.RoiPolicy.WHOLE_PLANE)
    rebound=ij.rebind_manual_threshold_command(owner.snapshot(),record,destination)
    app.publish(app.prepare("threshold",rebound.target,rebound.parameters()));np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),expected)


def test_scientific_desktop_pooled_capture_survives_navigation_and_session(tmp_path):
    from imagejxx import _desktop
    pixels=np.arange(2*2*3*4*5,dtype=np.uint16).reshape(2,2,3,4,5)
    owner=ij.Document(ij.Dataset(pixels,axes="TCZYX"));view=owner.snapshot().display
    view.t,view.c=1,1;view.crosshair=[0,0,2];owner.set_display(view)
    editor=_desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.action("show_manual_threshold","{}")
    editor.action("auto_manual_threshold",json.dumps(dict(method="mean",stack_histogram=True)))
    level=float(pixels[1,1].mean());state=ij.manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset)
    assert state.all_z and state.range.lower==int(np.floor(level))+1
    assert editor.settings() and json.loads(editor.settings())["threshold_stack_histogram"]
    path=tmp_path/"scientific-pooled.ijx";editor.action("save_document",json.dumps(dict(path=str(path))))
    other=_desktop.Editor(str(tmp_path/"other.json"));other.open_file(str(path))
    restored=other.native_snapshot();assert ij.manual_threshold_state(restored.display,restored.dataset).all_z
    assert (restored.display.t,restored.display.c)==(1,1)
    view=owner.snapshot().display;view.t,view.c=0,0;view.crosshair=[0,0,0];owner.set_display(view)
    editor.recording=True;editor.action("apply_manual_threshold",json.dumps(dict(lower=state.range.lower,upper=state.range.upper)));editor.wait()
    expected=pixels.copy();expected[1,1]=np.where(pixels[1,1]>level,255,0)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),expected)
    record=json.loads(editor.workflow)[0];assert record["schema_version"]==2 and record["target"]["scope"]=="ranges"
    assert owner.undo_steps==1 and "manual_threshold" not in owner.snapshot().display.properties
    editor.action("undo","{}");np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),pixels)
