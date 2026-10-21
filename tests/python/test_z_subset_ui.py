"""Independent exact assertions over both real shared-ImGui input producers."""
import json
import os
from pathlib import Path
import pytest


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_z_subset_ui(context,tmp_path):
    if context=="imgui_cpp":
        path=Path(os.environ["IMAGEJXX_Z_SUBSET_CPP_REPORT"])
        assert 0<path.stat().st_size<=16*1024*1024
        report=json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._z_subset_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="p06m.z-subset.ui.001" and report["context"]==context
    assert report["state"]=="observed" and not report["error"] and not report["acceptance_claim"]
    assert 0<len(report["frames"])<150
    for frame in report["frames"]:assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    assert {"menu.Image","menu.Image.Stacks","menu.z_subset","first","last","step","replace","apply","undo","redo"}<={r["name"] for r in report["controls"]}
    source=[[10000*t+1000*c+100*z+10*y+x for y in range(5) for x in range(7)]
            for t in range(2) for c in range(2) for z in range(5)]
    expected=[[10000*t+1000*c+100*z+10*y+x for y in range(5) for x in range(7)]
              for t in range(2) for c in range(2) for z in (1,3)]
    states=report["states"]
    assert states["before"]["pixels"]==source
    for name in ("applied","redone","reopened"):
        state=states[name]
        assert state["shape"]==[2,2,2,5,7,1] and state["pixels"]==expected
        assert state["spacing"]==[2,3,10] and state["origin"]==[10,20,35]
        assert (state["t"],state["c"],state["crosshair"][2])==(1,1,1)
        assert [(r["name"],r["z"]) for r in state["rois"]]==[("first",0),("last",1),("wild",None)]
        assert state["document_count"]==1
    assert states["undone"]["pixels"]==source and states["undone"]["crosshair"][2]==4
    assert states["undone"]["spacing"]==[2,3,5] and states["undone"]["origin"]==[10,20,30]
    record=states["applied"]["workflow"][0]
    assert record["schema"]=="imagejxx.z-subset-command"
    assert record["request"]["first"]==1 and record["request"]["last"]==4 and record["request"]["step"]==2
    rebound=states["rebound"]
    assert rebound["pixels"]==expected and rebound["shape"]==[2,2,2,5,7,1]
    assert rebound["document_count"]==2 and rebound["undo"]==0
    assert rebound["workflow"][1]["request"]=={
        "first":0,"last":1,"step":1,"destination":"new_document","max_planes":1048576}
    assert {"workflow.rebind","new_document","rebind.apply"}<={r["name"] for r in report["controls"]}
