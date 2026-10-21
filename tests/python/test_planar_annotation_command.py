"""Closed annotation equations, source binding, explicit rebind and no reads."""
import copy
import json
import imagejxx as ij
import numpy as np
import pytest

def fixture():
    observer=ij.DatasetReadObserver()
    values=np.arange(2*2*6*9*13,dtype=np.uint16).reshape(2,2,6,9,13)
    cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30])
    owner=ij.Document(ij.Dataset(values,axes="TCZYX",calibration=cal).observe_reads(observer))
    return owner,observer

def row(plane="xz",coordinate=0,name="shape"):
    return ij.PlanarAnnotation(name,ij.Selection("polygon",[(1,1),(4,1),(4,3)]),plane,coordinate,1,1)

@pytest.mark.parametrize("plane",["xy","xz","yz"])
@pytest.mark.parametrize("operation",["store","update","delete"])
def test_equations_and_replay(plane,operation):
    owner,observer=fixture()
    if operation!="store":owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(),row(plane)))
    request=ij.PlanarAnnotationRequest(operation,index=None if operation=="store" else 0,
        annotation=None if operation=="delete" else row(plane,name="new"))
    source=owner.snapshot();command=ij.resolve_planar_annotation_command(source,request)
    record=json.loads(json.dumps(command.record()))
    ij.validate_planar_annotation_command_record(record)
    replay=ij.planar_annotation_command_from_record(source,record)
    assert observer.counts().plane_reads==observer.counts().region_reads==0
    view=source.display;view.t=0;view.c=0;owner.set_display(view)
    owner.commit(ij.prepare_planar_annotation(replay))
    rows=ij.planar_annotations(owner.snapshot())
    assert len(rows)==(0 if operation=="delete" else 1)
    if rows:assert (rows[0].name,rows[0].plane,rows[0].t,rows[0].c)==("new",plane,1,1)
    assert owner.snapshot().dataset.shares_pixels(source.dataset) and owner.snapshot().selection is None
    with pytest.raises(ij.RevisionConflict):ij.planar_annotation_command_from_record(owner.snapshot(),record)

@pytest.mark.parametrize("path,value",[
    (("version",),True),(("version",),2),(("operation",),"replace"),(("index",),True),
    (("destination",),"new_document"),(("annotation",),None),(("after","rows"),[]),
    (("annotation","rows",0,"coordinate"),True),(("annotation","extent",2),5),
    (("after","rows",0,"name"),"forged"),(("source","shape",2),True),
])
def test_original_equation_before_rebind(path,value):
    owner,observer=fixture();command=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest("store",annotation=row()))
    original=command.record();bad=copy.deepcopy(original);target=bad
    for key in path[:-1]:target=target[key]
    target[path[-1]]=value
    with pytest.raises((ValueError,IndexError,OverflowError)):ij.validate_planar_annotation_command_record(bad)
    with pytest.raises((ValueError,IndexError,OverflowError)):
        ij.rebind_planar_annotation_command(owner.snapshot(),bad,ij.PlanarAnnotationRequest("store",annotation=row("yz")))
    assert observer.counts().plane_reads==observer.counts().region_reads==0
    assert owner.undo_steps==0

def test_explicit_rebind_and_stale_publication():
    owner,observer=fixture();other,_=fixture()
    original=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest("store",annotation=row()))
    with pytest.raises(ValueError):ij.planar_annotation_command_from_record(other.snapshot(),original.record())
    rebound=ij.rebind_planar_annotation_command(other.snapshot(),original.record(),ij.PlanarAnnotationRequest("store",annotation=row("yz",2,"explicit")))
    assert rebound.record()["before"]["rows"]==[]
    assert rebound.record()["after"]["rows"][0]["plane"]=="YZ"
    stale=ij.prepare_planar_annotation(original)
    edit=owner.begin_edit();edit.set_title("concurrent");owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):owner.commit(stale)
    assert not ij.planar_annotations(owner.snapshot()) and observer.counts().plane_reads==0
    other.commit(ij.prepare_planar_annotation(rebound))
    assert ij.planar_annotations(other.snapshot())[0].name=="explicit"

def test_noop_and_session_record():
    owner,_=fixture();value=row()
    owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(),value))
    command=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest("update",0,value))
    before=owner.snapshot();history=owner.undo_steps
    owner.commit(ij.prepare_planar_annotation(command))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history
    session=ij.decode_session(ij.encode_session(ij.SessionCapture([owner],workflow=[command.record()])))
    ij.validate_planar_annotation_command_record(session.workflow[0])
    assert session.workflow[0]==command.record()

@pytest.mark.parametrize("operation,index,annotation",[("store",0,True),("update",None,True),("update",0,False),("delete",0,True)])
def test_incomplete_requests_refuse(operation,index,annotation):
    owner,_=fixture()
    request=ij.PlanarAnnotationRequest(operation,index,row() if annotation else None)
    with pytest.raises((ValueError,IndexError)):ij.resolve_planar_annotation_command(owner.snapshot(),request)
