"""Owned literal V3 command expectations; no reference-parity claim."""
import copy
import itertools
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


def fixture():
    t,c,z,y,x=np.indices((2,2,3,3,4))
    pixels=(1000*t+100*c+10*z+2*y+x).astype(np.uint16)
    cal=ij.SpatialCalibration(spacing=[2,3,4],origin=[10,20,30],
        direction=[1,0,0,0,0,-1,0,1,0],time_origin=7,frame_interval=2)
    observer=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes="TCZYX",calibration=cal).observe_reads(observer))
    app=ij.Application();app.adopt(owner)
    return owner,app,observer


def request(plane="xz",flags=(False,False,False),linear=True):
    r=ij.AxisRangeResliceRequest(plane,1,2,1,t=1,c=1)
    o=ij.AxisRangeOrientationRequest(r,reverse_order=flags[0],flip_vertical=flags[1],transpose=flags[2])
    return ij.AxisRangeSpacingRequest(o,physical_vertical_spacing=2,
        sampling=ij.AxisRangeSampling.Linear if linear else ij.AxisRangeSampling.Nearest)


@pytest.mark.parametrize("plane,flags,linear",itertools.product(("xz","yz"),itertools.product((False,True),repeat=3),(False,True)))
def test_literal_mapping(plane,flags,linear):
    owner,app,observer=fixture();source=owner.snapshot()
    command=ij.resolve_axis_spacing_command(source,request(plane,flags,linear))
    record=json.loads(json.dumps(command.record()))
    ij.validate_axis_spacing_command_record(record)
    replay=ij.axis_spacing_command_from_record(source,record)
    assert observer.counts().plane_reads==0 and observer.counts().region_reads==0
    expected=[]
    for fixed in ((2,1) if flags[0] else (1,2)):
        rows=[]
        for v in range(5):
            z=2-v/2 if flags[1] else v/2
            if not linear:z=int(np.floor(z+.5))
            rows.append([1100+10*z+(2*fixed+u if plane=="xz" else 2*u+fixed)
                         for u in range(4 if plane=="xz" else 3)])
        image=np.asarray(rows,dtype=np.uint16)
        expected.append(image.T if flags[2] else image)
    published=app.publish(ij.prepare_axis_spacing(replay))
    output=published.document.snapshot()
    np.testing.assert_array_equal(output.dataset.numpy("ZYX"),np.asarray(expected))
    cal=output.dataset.calibration
    assert cal.time_origin==9
    assert cal.origin==([10,12 if flags[1] else 20,30+3*(2 if flags[0] else 1)] if plane=="xz" else
                        [10+2*(2 if flags[0] else 1),12 if flags[1] else 20,30])
    assert not output.rois and not output.overlays and output.spectrum is None
    assert owner.snapshot().shares_data(source) and owner.undo_steps==0
    assert published.created and published.document.undo_steps==0
    # Returned nested values are copies, not mutable command state.
    detached=command.request;detached.orientation.range.t=0
    assert command.record()==record


@pytest.mark.parametrize("path,value",[
    (("version",),True),(("request","t"),True),(("request","step"),0),
    (("request","physical_vertical_spacing"),True),(("request","sampling"),"cubic"),
    (("request","max_working_bytes"),1),(("grid","voxel_step"),.75),
    (("grid","output_axes"),["Y","Z","X"]),(("destination",),"in_place"),
    (("derived","working_bytes"),0),(("request","c"),2**63),
])
def test_closed_record_original_first(path,value):
    owner,_,observer=fixture();record=ij.resolve_axis_spacing_command(owner.snapshot(),request()).record()
    altered=copy.deepcopy(record);target=altered
    for key in path[:-1]:target=target[key]
    target[path[-1]]=value
    with pytest.raises((ValueError,OverflowError)):
        ij.validate_axis_spacing_command_record(altered)
    with pytest.raises((ValueError,OverflowError)):
        ij.rebind_axis_spacing_command(owner.snapshot(),altered,request())
    assert observer.counts().plane_reads==0


def test_replay_navigation_stale_cap_cancel():
    owner,app,_=fixture();source=owner.snapshot()
    command=ij.resolve_axis_spacing_command(source,request())
    view=source.display;view.t=0;view.c=0;owner.set_display(view)
    assert ij.axis_spacing_command_from_record(owner.snapshot(),command.record()).request.orientation.range.t==1
    events=[]
    with pytest.raises(ValueError):
        ij.prepare_axis_spacing(command,execution_max_working_bytes=1,progress=lambda *v:events.append(v))
    assert events==[]
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_axis_spacing(command,cancellation=token)
    prepared=ij.prepare_axis_spacing(command)
    edit=owner.begin_edit();edit.set_property("changed",True);owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):ij.axis_spacing_command_from_record(owner.snapshot(),command.record())
    with pytest.raises(ij.RevisionConflict):app.publish(prepared)
    assert len(app.document_ids)==1
    ij.rebind_axis_spacing_command(owner.snapshot(),command.record(),request())


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_scientific_spacing_ui(context,tmp_path):
    if context=="imgui_cpp":report=json.loads(Path(os.environ["IMAGEJXX_SPACING_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._axis_spacing_ui_case(str(tmp_path/"capture")))
    assert report["context"]==context and report["contract"]=="p06n.axis-spacing.ui.001"
    assert not report["error"] and not report["acceptance_claim"]
    assert 0<len(report["frames"])<150
    for frame in report["frames"]:assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    assert (report["configured"]["captured_t"],report["configured"]["captured_c"])==(1,1)
    expected=[[11000+50*v+10*fixed+x for v in range(8,-1,-1) for x in range(7)] for fixed in (3,1)]
    # Transpose the literal 9-row by 7-column source-oriented images.
    expected=[np.asarray(row).reshape(9,7).T.ravel().tolist() for row in expected]
    for name in ("applied","cancelled","reopened"):
        state=report["states"][name]
        assert state["shape"]==[1,1,2,7,9,1] and state["pixels"]==expected
        assert state["spacing"]==[2.5,2,6] and state["origin"]==[10,29,50]
        assert state["undo"]==0
    record=report["states"]["applied"]["workflow"][0]
    assert record["version"]==3 and (record["request"]["t"],record["request"]["c"])==(1,1)
    assert {"sampling.Linear","reverse_order","flip_vertical","transpose","workflow.rebind","cancel"}<={r["name"] for r in report["controls"]}
