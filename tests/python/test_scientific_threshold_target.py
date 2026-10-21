"""Range-scoped state, retained histograms and scientific threshold actions."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij


def dataset():return ij.Dataset(np.arange(2*3*7*3*4,dtype=np.uint16).reshape(2,3,7,3,4),axes="TCZYX")
def axes():return [ij.AxisRange(0,2),ij.AxisRange(0,3,2),ij.AxisRange(1,7,2)]


def test_version_three_contains_only_selected_grid_and_is_detached():
    data=dataset();before=ij.DisplayState();view=ij.set_manual_threshold_state(before,data,ij.ManualThresholdRange(30,90),ij.PlanePosition(1,2,6),ranges=axes())
    assert "manual_threshold" not in before.properties
    assert view.properties["manual_threshold"]==dict(version=3,lower=30.,upper=90.,t=1,c=2,z=6,ranges=[[0,2,1],[0,3,2],[1,7,2]])
    state=ij.manual_threshold_state(view,data);assert not state.all_z and "manual_threshold_contains" in ij.__all__
    returned=state.ranges;returned[0].start=99
    for t in range(2):
        for c in range(3):
            for z in range(7):assert ij.manual_threshold_contains(state,ij.PlanePosition(t,c,z))==(c in [0,2] and z in [1,3,5])
    with pytest.raises(ValueError):ij.set_manual_threshold_state(before,data,ij.ManualThresholdRange(0,1),all_z=True,ranges=axes())

@pytest.mark.parametrize("ranges",[[],[[0,2,1]],[[0,2,1],[0,3,1],[0,7,1],[0,1,1]],[[0,2,0],[0,3,1],[0,7,1]],[[0,3,1],[0,3,1],[0,7,1]],[[-1,2,1],[0,3,1],[0,7,1]],[[0,None,1],[0,3,1],[0,7,1]],[[False,2,1],[0,3,1],[0,7,1]],[[0,2.,1],[0,3,1],[0,7,1]]])
def test_invalid_range_records_refuse_without_mutating_owner(ranges):
    data=dataset();owner=ij.Document(data);before=owner.snapshot();view=before.display
    view.properties={"manual_threshold":dict(version=3,lower=0.,upper=1.,t=0,c=0,z=0,ranges=ranges)}
    with pytest.raises((ValueError,IndexError)):owner.set_display(view)
    assert owner.snapshot().display.properties==before.display.properties and owner.snapshot().shares_data(before)

@pytest.mark.parametrize("first,last,step",[(0,6,2),(1,6,2),(1,6,3),(0,6,1),(3,3,1)])
def test_subset_intersects_selected_grid_without_reading_pixels(first,last,step):
    reads=ij.DatasetReadObserver();data=dataset().observe_reads(reads);owner=ij.Document(data);app=ij.Application();app.adopt(owner)
    view=ij.set_manual_threshold_state(owner.snapshot().display,data,ij.ManualThresholdRange(30,90),ij.PlanePosition(1,2,6),ranges=axes());owner.set_display(view)
    command=ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(first,last,step,ij.OutputDestination.IN_PLACE));prepared=ij.prepare_z_subset(command)
    assert reads.counts().plane_reads==0;app.publish(prepared)
    result=owner.snapshot();state=ij.manual_threshold_state(result.display,result.dataset);selected=[z for z in range(first,last+1,step) if z in [1,3,5]]
    if not selected:assert state is None
    else:
        assert state.ranges is not None
        for t in range(2):
            for c in range(3):
                for z,old_z in enumerate(range(first,last+1,step)):assert ij.manual_threshold_contains(state,ij.PlanePosition(t,c,z))==(c in [0,2] and old_z in [1,3,5])
    if (first,last,step)==(0,6,1):assert owner.undo_steps==0 and owner.snapshot().display.properties==view.properties
    else:
        assert owner.undo_steps==1;app.undo();assert owner.snapshot().display.properties==view.properties


def fixture(policy="active_selection"):
    width=16;pixels=np.empty((2,2,4,8,width),np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(4):
                offset=8 if policy=="positioned_rois" and z==3 else 0
                for y in range(8):
                    for x in range(width):pixels[t,c,z,y,x]=((2*t+(z-1)//2)*64+8*y+x-offset if offset<=x<offset+8 else 240) if c==1 and z in [1,3] else 17+(y*width+x)%71
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="TCZYX").observe_reads(reads));edit=owner.begin_edit();edit.select(ij.Selection("rectangle",[(0,0),(8,8)]))
    edit.set_rois([ij.ROI("target",ij.Selection("rectangle",[(8 if z==3 else 0,0),(16 if z==3 else 8,8)]),t=t,c=1,z=z) for t in range(2) for z in [1,3]]);owner.commit(edit);owner=ij.Document(owner.snapshot())
    request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(1,2),z=ij.AxisRange(1,4,2),roi={"active_selection":ij.RoiPolicy.ACTIVE_SELECTION,"positioned_rois":ij.RoiPolicy.POSITIONED_ROIS}[policy])
    return owner,pixels,reads,request

@pytest.mark.parametrize("policy",["active_selection","positioned_rois"])
@pytest.mark.parametrize("histogram_roi",[False,True])
def test_scientific_action_uses_same_frozen_ranges_and_roi_for_apply(policy,histogram_roi,tmp_path):
    from imagejxx import _desktop
    owner,pixels,reads,request=fixture(policy);editor=_desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner);editor.action("show_manual_threshold","{}");before=owner.snapshot();count=reads.counts().plane_reads
    target=dict(scope="ranges",t=dict(start=0,stop=2),c=dict(start=1,stop=2),z=dict(start=1,stop=4,step=2),roi=policy)
    editor.action("configure_threshold_target",json.dumps(dict(target=target,histogram_roi=histogram_roi)));assert reads.counts().plane_reads==count
    editor.action("auto_manual_threshold",'{"method":"mean"}');level=127.5 if histogram_roi else 183.75
    state=ij.manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset);assert state.ranges is not None and state.range.lower==int(np.floor(level))+1
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0 and not owner.snapshot().dirty
    saved=json.loads((tmp_path/"prefs.json").read_bytes());assert saved["values"]["threshold_histogram_roi"]==histogram_roi and saved["values"]["threshold_apply_roi"]==policy
    path=tmp_path/"ranged.ijx";editor.action("save_document",json.dumps(dict(path=str(path))));other=_desktop.Editor(str(tmp_path/"other.json"));other.open_file(str(path));reopened=other.native_snapshot();assert reopened.display.properties["manual_threshold"]==owner.snapshot().display.properties["manual_threshold"]
    # Navigation remains independent of the captured Apply request.
    view=owner.snapshot().display;view.t,view.c=1,0;view.crosshair=[0,0,2];owner.set_display(view);editor.recording=True
    editor.action("apply_manual_threshold",json.dumps(dict(lower=state.range.lower,upper=state.range.upper)));editor.wait();expected=pixels.copy()
    for t in range(2):
        for z in [1,3]:
            offset=8 if policy=="positioned_rois" and z==3 else 0
            expected[t,1,z,:,offset:offset+8]=np.where(pixels[t,1,z,:,offset:offset+8]>level,255,0)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),expected);assert owner.undo_steps==1
    record=json.loads(editor.workflow)[0];assert record["schema_version"]==2 and record["target"]["roi_policy"]==policy
    if policy=="positioned_rois":assert record["selection"] is None
    ij.validate_manual_threshold_command_record(record)
    editor.action("undo","{}");np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),pixels)
    rebound=ij.rebind_manual_threshold_command(owner.snapshot(),record,request);assert rebound.target.position_count==4

@pytest.mark.parametrize("policy",["active_selection","positioned_rois"])
@pytest.mark.parametrize("black,white",[(False,False),(True,True)])
def test_retained_histogram_is_raw_and_detached(policy,black,white):
    owner,pixels,reads,request=fixture(policy);target=ij.resolve_target(owner.snapshot(),request)
    result=ij.pooled_auto_threshold(target,"huang",ignore_black=black,ignore_white=white,include_histogram=True)
    assert result.histogram["counts"]==[1]*256 and result.histogram["edges"]==[-.5+i for i in range(257)]
    mutable=result.histogram;mutable["counts"][0]=999;assert result.histogram["counts"][0]==1
    display=ij.pooled_automatic_threshold_display(target,"huang",ignore_black=black,ignore_white=white);assert display.histogram==result.histogram
    assert ij.pooled_auto_threshold(target,"huang").histogram is None
    assert ij.pooled_auto_threshold(target,"mean",include_histogram=True).histogram["counts"]==[1]*256

@pytest.mark.parametrize("target",[dict(scope="ranges",z=dict(start=2,stop=2)),dict(scope="ranges",z=dict(step=0)),dict(scope="positions",positions=[[0,0,0]]),dict(mode="volume"),dict(destination="new_document"),dict(roi="invalid")])
def test_invalid_configure_refuses_without_reads_or_edits(target,tmp_path):
    from imagejxx import _desktop
    owner,pixels,reads,request=fixture();editor=_desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner);editor.action("show_manual_threshold","{}");before=owner.snapshot();count=reads.counts().plane_reads
    with pytest.raises((ValueError,IndexError)):editor.action("configure_threshold_target",json.dumps(dict(target=target)))
    assert reads.counts().plane_reads==count and owner.snapshot().display.properties==before.display.properties and owner.undo_steps==0


@pytest.mark.parametrize("dtype",[np.uint8,np.float64])
def test_requested_constant_histogram_is_complete(dtype):
    value=17 if dtype==np.uint8 else np.finfo(np.float64).max
    owner=ij.Document(ij.Dataset(np.full((2,3,4),value,dtype),axes="ZYX"))
    target=ij.resolve_target(owner.snapshot(),ij.TargetRequest(ij.TargetScope.ALL_Z))
    result=ij.pooled_auto_threshold(target,"mean",include_histogram=True)
    assert result.threshold==value and sum(result.histogram["counts"])==24
    assert np.isfinite(result.histogram["edges"]).all()
    assert max(result.histogram["counts"])==24


def test_requested_mean_histogram_handles_finite_cross_zero_extrema():
    value=np.finfo(np.float64).max
    owner=ij.Document(ij.Dataset(np.array([[[-value,value]]]),axes="ZYX"))
    target=ij.resolve_target(owner.snapshot())
    result=ij.pooled_auto_threshold(target,"mean",include_histogram=True)
    assert result.threshold==0 and result.histogram["counts"]==[1]+[0]*254+[1]
    assert np.isfinite(result.histogram["edges"]).all() and result.histogram["edges"][0]==-value and result.histogram["edges"][-1]==value
