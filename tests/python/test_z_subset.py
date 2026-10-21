"""Literal regular-Z mapping expectations, independent of the remapper."""
import copy
import json

import imagejxx as ij
import numpy as np
import pytest


def fixture(opaque_view=True):
    t,c,z,y,x=np.indices((2,2,5,5,7))
    pixels=(10000*t+1000*c+100*z+10*y+x).astype(np.uint16)
    calibration=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30],
        direction=[1,0,0,0,0,-1,0,1,0],unit="um",frame_interval=2,time_origin=7)
    observer=ij.DatasetReadObserver()
    dataset=ij.Dataset(pixels,axes="TCZYX",calibration=calibration).observe_reads(observer)
    owner=ij.Document(dataset,"Z mapping")
    mask=np.array([[0,0,0,0,0,0,0],[0,1,1,1,1,1,0],[0,1,0,0,0,1,0],
                   [0,1,1,1,1,1,0],[0,0,0,0,0,0,0]],np.uint8)*255
    selection=ij.Selection.from_mask(mask)
    edit=owner.begin_edit()
    edit.select(selection)
    edit.set_rois([ij.ROI("zero",selection,t=0,c=0,z=0),ij.ROI("first",selection,t=0,c=0,z=1),
                   ij.ROI("removed",selection,z=2),ij.ROI("last",selection,t=1,c=1,z=3),
                   ij.ROI("wild",selection,attributes={"opaque":{"z":4}})])
    edit.set_overlays([{"future":{"kept":True}}, {"text":"mapped","t":0,"c":0,"z":1,"slice_index":1},
                       {"text":"drop","z":4},{"text":"wild","z":None}])
    edit.set_property("plane_labels",[f"p{i}" for i in range(20)])
    edit.set_extension("slice_labels",["a","b","c","d","e"])
    edit.set_extension("opaque",{"kept":[True,None,4]})
    owner.commit(edit)
    view=owner.snapshot().display
    view.t,view.c,view.crosshair=1,1,[3,2,4]
    view.properties={"manual_threshold":{"version":1,"lower":1,"upper":7,"t":1,"c":1,"z":3},
                     "future_display":{"kept":True}}
    if not opaque_view:view.properties={"manual_threshold":view.properties["manual_threshold"]}
    owner.set_display(view);owner.mark_saved()
    app=ij.Application();app.adopt(owner)
    return owner,app,pixels,mask,observer


def assert_result(snapshot,pixels,mask,opaque_view=True):
    np.testing.assert_array_equal(snapshot.dataset.numpy("TCZYX"),pixels[:,:,[1,3]])
    cal=snapshot.dataset.calibration
    assert cal.spacing==[2,3,10] and cal.origin==[10,15,30]
    assert cal.direction==[1,0,0,0,0,-1,0,1,0] and cal.time_origin==7 and cal.frame_interval==2
    assert [(r.name,r.t,r.c,r.z) for r in snapshot.rois]==[
        ("first",0,0,0),("last",1,1,1),("wild",None,None,None)]
    np.testing.assert_array_equal(snapshot.selection.mask((5,7)),mask)
    for roi in snapshot.rois:np.testing.assert_array_equal(roi.selection.mask((5,7)),mask)
    assert snapshot.rois[-1].attributes=={"opaque":{"z":4}}
    assert snapshot.overlays==[{"future":{"kept":True}},
        {"text":"mapped","t":0,"c":0,"z":0,"slice_index":0},{"text":"wild","z":None}]
    assert snapshot.metadata["properties"]["plane_labels"]==["p1","p3","p6","p8","p11","p13","p16","p18"]
    assert snapshot.metadata["extensions"]["slice_labels"]==["b","d"]
    assert snapshot.metadata["extensions"]["opaque"]=={"kept":[True,None,4]}
    assert (snapshot.display.t,snapshot.display.c,snapshot.display.crosshair[2])==(1,1,1)
    assert snapshot.display.properties["manual_threshold"]["z"]==1
    if opaque_view:assert snapshot.display.properties["future_display"]=={"kept":True}


@pytest.mark.parametrize("destination",[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_complete_mapping_and_publication(destination):
    owner,app,pixels,mask,observer=fixture()
    before=owner.snapshot()
    command=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(1,4,2,destination))
    counts=observer.counts()
    prepared=ij.prepare_z_subset(command)
    assert observer.counts().plane_reads==counts.plane_reads and observer.counts().region_reads==counts.region_reads
    assert_result(prepared.preview,pixels,mask)
    published=app.publish(prepared)
    assert published.created==(destination==ij.OutputDestination.NEW_DOCUMENT)
    assert_result(published.document.snapshot(),pixels,mask)
    if published.created:
        assert owner.snapshot().shares_data(before) and owner.undo_steps==1
        assert published.document.undo_steps==0
    else:
        assert owner.undo_steps==2
        revision=owner.snapshot().display.revision
        owner.undo()
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"),pixels)
        assert owner.snapshot().display.crosshair[2]==4
        assert owner.snapshot().display.properties["manual_threshold"]["z"]==3
        assert not owner.snapshot().dirty and owner.snapshot().display.revision>revision
        owner.redo();assert_result(owner.snapshot(),pixels,mask)


def test_noop_removed_threshold_and_nearest_tie():
    owner,app,pixels,mask,_=fixture()
    source=owner.snapshot();history=owner.undo_steps
    command=ij.resolve_z_subset_command(source,ij.ZSubsetRequest(0,4,1,ij.OutputDestination.IN_PLACE))
    app.publish(ij.prepare_z_subset(command))
    assert owner.undo_steps==history and owner.snapshot().shares_data(source)
    command=ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(0,4,2,ij.OutputDestination.IN_PLACE))
    app.publish(ij.prepare_z_subset(command))
    assert "manual_threshold" not in owner.snapshot().display.properties
    assert [(r.name,r.z) for r in owner.snapshot().rois]==[("zero",0),("removed",1),("wild",None)]
    mapping=ij.RegularAxisMapping(7,1,6,2)
    assert [mapping.source_index(i) for i in range(mapping.count)]==[1,3,5]
    assert [mapping.nearest_output(i) for i in range(7)]==[0,0,0,1,1,2,2]
    assert [mapping.output_index(i) for i in range(7)]==[None,0,None,1,None,2,None]


def test_strict_replay_rebind_and_stale_display():
    owner,app,_,_,_=fixture()
    command=ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,2))
    record=command.record();ij.validate_z_subset_command_record(record)
    ij.z_subset_command_from_record(owner.snapshot(),record)
    prepared=ij.prepare_z_subset(command)
    view=owner.snapshot().display;view.crosshair=[0,0,0];owner.set_display(view)
    with pytest.raises(ij.RevisionConflict):app.publish(prepared)
    with pytest.raises(ij.RevisionConflict):ij.z_subset_command_from_record(owner.snapshot(),record)
    other,_,_,_,_=fixture()
    with pytest.raises(ValueError):ij.z_subset_command_from_record(other.snapshot(),record)
    rebound=ij.rebind_z_subset_command(other.snapshot(),record,ij.ZSubsetRequest(0,4,2))
    assert rebound.request.first==0
    invalid=copy.deepcopy(record);invalid["request"]["step"]=True
    with pytest.raises(ValueError):ij.rebind_z_subset_command(other.snapshot(),invalid,ij.ZSubsetRequest())


@pytest.mark.parametrize("change",["selection","metadata","pixels"])
def test_stale_publication(change):
    owner,app,_,_,_=fixture()
    prepared=ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,2)))
    edit=owner.begin_edit()
    if change=="selection":edit.select(None)
    elif change=="metadata":edit.set_title("changed")
    else:edit.replace_plane(np.zeros((5,7),np.uint16))
    owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):app.publish(prepared)
    assert len(app.document_ids)==1


@pytest.mark.parametrize("bounds",[(True,4,1),(-1,4,1),(0,5,1),(3,2,1),(0,4,0),(0,4,1.5),(2**80,4,1)])
def test_invalid_requests(bounds):
    owner,_,_,_,_=fixture()
    with pytest.raises((ValueError,TypeError,OverflowError)):
        ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(*bounds)))


def test_cancellation_and_annotation_refusal_are_atomic():
    owner,app,_,_,_=fixture();before=owner.snapshot()
    command=ij.resolve_z_subset_command(before,ij.ZSubsetRequest(1,4,2))
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_z_subset(command,cancellation=token)
    edit=owner.begin_edit();edit.set_overlays([{"z":1,"slice_index":3}]);owner.commit(edit)
    command=ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,2))
    with pytest.raises(ValueError,match="Conflicting"):ij.prepare_z_subset(command)
    assert owner.snapshot().dataset.shape==(2,2,5,5,7,1) and len(app.document_ids)==1


def test_sample_layout_and_nan_bits_are_preserved():
    rgb=np.arange(2*2*3*2*3*3).astype(np.uint8).reshape(2,2,3,2,3,3)
    bits=np.array([0x7fc01234,0x80000000,0x3f800000,0x40000000,0x7fc05678,0x40400000],np.uint32).reshape(3,1,2)
    for data,axes,policy in [(rgb,"TCZYXS",ij.FloatingSamplePolicy.FINITE_ONLY),
            (bits.view(np.float32),"ZYX",ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)]:
        owner=ij.Document(ij.Dataset(data,axes=axes,sample_policy=policy));app=ij.Application();app.adopt(owner)
        command=ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(0,2,2))
        output=app.publish(ij.prepare_z_subset(command)).document.snapshot().dataset
        assert output.sample_policy==policy
        actual=output.numpy(axes)
        expected=rgb[:,:,[0,2]] if axes=="TCZYXS" else bits[[0,2]]
        np.testing.assert_array_equal(actual if axes=="TCZYXS" else actual.view(np.uint32),expected)


@pytest.mark.parametrize("replace",[False,True])
def test_desktop_action_record_and_save(tmp_path,replace):
    from imagejxx import _desktop
    owner,_,pixels,mask,_=fixture(opaque_view=False)
    editor=_desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner);editor.recording=True
    editor.action("substack",json.dumps(dict(first=2,last=5,step=2,replace=replace)))
    snapshot=editor.native_snapshot();assert_result(snapshot,pixels,mask,opaque_view=False)
    assert len(json.loads(editor.document_infos()))==(1 if replace else 2)
    record=json.loads(editor.workflow)[0]
    assert record["request"]["first"]==1 and record["request"]["last"]==4
    path=tmp_path/"subset.ijx";editor.action("save_document",json.dumps({"path":str(path)}))
    reopened=_desktop.Editor(str(tmp_path/"again.json"));reopened.open_file(str(path))
    assert_result(reopened.native_snapshot(),pixels,mask,opaque_view=False)
