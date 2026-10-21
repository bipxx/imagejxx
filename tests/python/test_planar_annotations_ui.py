"""Independent assertions over real shared-ImGui planar annotation inputs."""
import json
import os
from pathlib import Path
import pytest

KEY="imagejxx.planar_annotations"

@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_planar_annotations(context,tmp_path):
    if context=="imgui_cpp":
        report=json.loads(Path(os.environ["IMAGEJXX_PLANAR_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._planar_annotation_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="p06p.planar-annotation.ui.001" and report["context"]==context
    assert not report["error"] and not report["acceptance_claim"]
    assert 0<len(report["frames"])<250
    for frame in report["frames"]:
        assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    states=report["states"]
    expected_pixels=[[10000*t+1000*c+100*z+10*y+x for y in range(9) for x in range(13)]
                     for t in range(2) for c in range(2) for z in range(6)]
    for name in ("before","created","moved","cancelled","styled","undone","redone","reopened"):
        state=states[name]
        assert state["shape"]==[2,2,6,9,13,1] and state["pixels"]==expected_pixels
        assert state["selection"] is None and (state["t"],state["c"],state["crosshair"])==(1,1,[4,3,2])
    for name,points,history in (("created",[[1,1],[4,4]],1),("moved",[[2,1],[5,4]],2),
                               ("cancelled",[[2,1],[5,4]],2),("styled",[[2,1],[5,4]],3)):
        state=states[name];rows=state["metadata"][KEY]["rows"]
        assert len(rows)==1 and state["undo"]==history
        row=rows[0]
        assert (row["plane"],row["coordinate"],row["t"],row["c"])==("XZ",3,1,1)
        assert row["selection"]["kind"]=="rectangle" and row["selection"]["points"]==points
    for name in ("styled","redone","reopened"):
        row=states[name]["metadata"][KEY]["rows"][0]
        assert row["name"]=="XZ area"
        assert row["selection"]["attributes"]["imagejxx.roi_style"]["fill_color"]==[64,210,240,128]
    assert states["undone"]["metadata"][KEY]["rows"][0]["name"]=="Annotation 1"
    baseline=[p["builds"] for p in report["before_render"]]
    for name in ("created_render","moved_render","cancel_render","styled_render"):
        assert [p["builds"] for p in report[name]]==baseline
        layers=report[name][0]["planar_annotations"]["layers"]
        assert len(layers)==1 and layers[0]["plane"]=="XZ" and layers[0]["coordinate"]==3
    assert report["cancel_render"][0]["planar_annotations"]["last_cancel"]=="escape"
    rotation=[h for h in report["moved_render"][0]["planar_annotations"]["handles"] if h["kind"]==3]
    assert len(rotation)==1
    plane=report["moved_render"][1]
    assert rotation[0]["point"][1]==pytest.approx(plane["origin"][1]+plane["scale"][1]-32)
    assert report["idle_before"][0]["planar_annotations"]["visual_builds"]==report["idle_after"][0]["planar_annotations"]["visual_builds"]
    assert {"annotation.rectangle","annotation.edit","annotation.ellipse","annotation.properties","undo","redo"}<={r["name"] for r in report["controls"]}
