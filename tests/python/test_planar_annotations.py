"""Plane-explicit native annotation foundation; UI integration remains open."""
import copy
import itertools
import imagejxx as ij
import numpy as np
import pytest

KEY="imagejxx.planar_annotations"

def fixture():
    pixels=np.arange(2*3*5*3*7,dtype=np.uint16).reshape(2,3,5,3,7)
    cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30],
        direction=[1,.25,0,0,0,-1,0,1,0],time_origin=7,frame_interval=2)
    observer=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes="TCZYX",calibration=cal).observe_reads(observer))
    return owner,observer

def annotation(plane="xz",coordinate=0,t=1,c=2,selection=None):
    return ij.PlanarAnnotation("area",selection or ij.Selection("rectangle",[(1,1),(3,3)]),
        plane,coordinate,t,c,attributes={"future":{"kept":[None,True,7]}})

@pytest.mark.parametrize("plane",["xy","xz","yz"])
def test_plane_frame_and_persistence(plane):
    owner,observer=fixture();source=owner.snapshot();row=annotation(plane)
    tx=ij.prepare_store_planar_annotation(source,row);owner.commit(tx)
    assert observer.counts().plane_reads==observer.counts().region_reads==0
    assert owner.snapshot().dataset.shares_pixels(source.dataset)
    assert not owner.snapshot().rois and owner.snapshot().selection is None
    saved=ij.planar_annotations(owner.snapshot())[0]
    assert (saved.plane,saved.coordinate,saved.t,saved.c)==(plane,0,1,2)
    voxel={"xy":[1,2,0],"xz":[1,0,2],"yz":[0,1,2]}[plane]
    assert ij.planar_annotation_voxel(row,[1.5,2.5])==voxel
    # Handwritten affine matrix: X=10+2x+.75y,Y=20-5z,Z=30+3y.
    world=[10+2*voxel[0]+.75*voxel[1],20-5*voxel[2],30+3*voxel[1]]
    assert ij.planar_annotation_world(source.dataset.calibration,row,[1.5,2.5])==world
    assert ij.planar_annotation_boundary(source.dataset.calibration,row,world)==pytest.approx([1.5,2.5])
    assert ij.planar_annotation_visible(row,plane,0,1,2)
    assert not ij.planar_annotation_visible(row,plane,0,0,2)
    assert not ij.planar_annotation_visible(row,plane,1,1,2)
    stored=owner.snapshot().metadata["extensions"][KEY]
    decoded=ij.decode_session(ij.encode_session(ij.SessionCapture([owner.snapshot()])))
    restored=ij.planar_annotations(decoded.documents[0].snapshot())[0]
    assert restored.attributes==saved.attributes and restored.selection.points==saved.selection.points
    assert decoded.documents[0].snapshot().metadata["extensions"][KEY]==stored
    owner.undo();assert not ij.planar_annotations(owner.snapshot())
    owner.redo();assert len(ij.planar_annotations(owner.snapshot()))==1
    history=owner.undo_steps
    tx=ij.prepare_update_planar_annotation(owner.snapshot(),0,row);owner.commit(tx)
    assert owner.undo_steps==history
    tx=ij.prepare_delete_planar_annotation(owner.snapshot(),0);owner.commit(tx)
    assert KEY not in owner.snapshot().metadata["extensions"]

@pytest.mark.parametrize("plane",["xz","yz"])
def test_z_subset_masks_and_world_landmarks(plane):
    owner,observer=fixture();width=7 if plane=="xz" else 3
    mask=np.zeros((5,width),np.uint8);mask[1,0]=1;mask[3,-1]=1;mask[2,1]=1
    row=annotation(plane,selection=ij.Selection.from_mask(mask))
    tx=ij.prepare_store_planar_annotation(owner.snapshot(),row);owner.commit(tx)
    # Positioned XY row on a removed source Z plane is dropped.
    tx=ij.prepare_store_planar_annotation(owner.snapshot(),annotation("xy",0));owner.commit(tx)
    tx=ij.prepare_store_planar_annotation(owner.snapshot(),annotation("xy",3));owner.commit(tx)
    app=ij.Application();app.adopt(owner)
    result=app.publish(ij.prepare_z_subset(ij.resolve_z_subset_command(owner.snapshot(),ij.ZSubsetRequest(1,4,2))))
    rows=ij.planar_annotations(result.document.snapshot())
    assert len(rows)==2 and rows[1].coordinate==1
    np.testing.assert_array_equal(rows[0].selection.mask((2,width)),mask[[1,3]]*255)
    assert observer.counts().plane_reads==observer.counts().region_reads==0

@pytest.mark.parametrize("bad",[True,1.0,-1,2**63,None])
def test_strict_positions(bad):
    with pytest.raises((TypeError,ValueError,OverflowError)):
        annotation(coordinate=bad)

@pytest.mark.parametrize("path,value",[
    (("version",),True),(("version",),2),(("rows",0,"coordinate"),True),
    (("rows",0,"coordinate"),100),(("rows",0,"t"),2),(("rows",0,"c"),3),
    (("rows",0,"plane"),"oblique"),(("rows",0,"extra"),0),(("extent",2),4),
])
def test_reserved_schema_rejects_forgery_atomically(path,value):
    owner,_=fixture();tx=ij.prepare_store_planar_annotation(owner.snapshot(),annotation());owner.commit(tx)
    before=owner.snapshot();record=copy.deepcopy(before.metadata["extensions"][KEY]);target=record
    for key in path[:-1]:target=target[key]
    target[path[-1]]=value
    edit=owner.begin_edit();edit.set_extension(KEY,record)
    with pytest.raises((ValueError,RuntimeError,IndexError)):owner.commit(edit)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==1

def test_navigation_stale_and_extent_guards():
    owner,_=fixture();tx=ij.prepare_store_planar_annotation(owner.snapshot(),annotation())
    view=owner.snapshot().display;view.t=1;view.c=2;owner.set_display(view)
    owner.commit(tx)
    stale=ij.prepare_delete_planar_annotation(owner.snapshot(),0)
    edit=owner.begin_edit();edit.set_title("changed");owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):owner.commit(stale)
    before=owner.snapshot();edit=owner.begin_edit()
    edit.replace_dataset(ij.Dataset(np.zeros((2,3,4,3,7),np.uint16),axes="TCZYX"))
    with pytest.raises(ValueError):owner.commit(edit)
    assert owner.snapshot().shares_data(before)
    row=annotation();world=ij.planar_annotation_world(before.dataset.calibration,row,[1,1])
    world[2]+=3
    with pytest.raises(ValueError):ij.planar_annotation_boundary(before.dataset.calibration,row,world)
