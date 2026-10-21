"""Real click-based tools, explicit scope, native recording and replay."""
import json
import os
from pathlib import Path
import pytest

KEY="imagejxx.planar_annotations"

@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_polygon_multipoint_scope_recording(context,tmp_path):
    if context=="imgui_cpp":
        report=json.loads(Path(os.environ["IMAGEJXX_PLANAR_TOOLS_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._planar_annotation_tools_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="p06p.planar-tools.ui.001" and report["context"]==context
    assert not report["error"] and not report["acceptance_claim"]
    assert 0<len(report["frames"])<512
    for frame in report["frames"]:assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    assert report["removed_draft"]["active"] and report["removed_draft"]["draft_vertices"]==2
    states=report["states"]
    polygon=states["polygon"]["metadata"][KEY]["rows"][0]
    assert (polygon["plane"],polygon["coordinate"],polygon["t"],polygon["c"])==("YZ",4,1,1)
    assert polygon["selection"]["kind"]=="polygon" and polygon["selection"]["points"]==[[1,1],[3,1],[2,4]]
    assert states["polygon"]["undo"]==1 and len(states["polygon"]["workflow"])==1
    assert states["invalid_scope"]["metadata"]==states["polygon"]["metadata"]
    assert states["invalid_scope"]["undo"]==1 and len(states["invalid_scope"]["workflow"])==1
    assert report["scope_error"]["error"]
    for name,count in (("scoped",2),("noop",3),("replayed",4)):
        state=states[name];row=state["metadata"][KEY]["rows"][0]
        assert (row["coordinate"],row["t"],row["c"])==(0,None,None)
        assert state["undo"]==2 and len(state["workflow"])==count
    assert states["positioned"]["crosshair"]==[0,3,2]
    final=states["multipoint"];rows=final["metadata"][KEY]["rows"]
    assert len(rows)==2 and final["undo"]==3 and len(final["workflow"])==5
    assert rows[1]["selection"]["kind"]=="multipoint" and rows[1]["selection"]["points"]==[[2,2],[5,2]]
    assert (rows[1]["plane"],rows[1]["coordinate"],rows[1]["t"],rows[1]["c"])==("XY",2,1,1)
    assert [step["operation"] for step in final["workflow"]]==["store","update","update","update","store"]
    import imagejxx as ij
    for step in final["workflow"]:
        ij.validate_planar_annotation_command_record(step)
        assert step["schema"]=="imagejxx.planar-annotation-command" and step["destination"]=="in_place"
    assert states["reopened"]["metadata"]==states["numeric_redone"]["metadata"]
    for state in states.values():
        assert state["pixels"]==states["before"]["pixels"] and state["selection"] is None
    assert {"annotation.polygon","annotation.remove_last","annotation.finish_polygon","annotation.invalid_scope",
            "annotation.all_t","annotation.all_c","annotation.apply_scope","workflow.planar.replay",
            "annotation.multipoint","annotation.finish_multipoint"}<={c["name"] for c in report["controls"]}

    def points(name,index=1):return states[name]["metadata"][KEY]["rows"][index]["selection"]["points"]
    assert points("vertex_moved")==[[3,2],[5,2]]
    assert states["vertex_moved"]["undo"]==4 and len(states["vertex_moved"]["workflow"])==6
    assert states["vertex_noop"]["revisions"]==states["vertex_moved"]["revisions"]
    assert states["vertex_noop"]["undo"]==4 and len(states["vertex_noop"]["workflow"])==7
    assert states["vertex_index_refused"]==states["vertex_noop"] and report["vertex_index_error"]["error"]
    assert points("vertex_inserted")==[[3,2],[5,2],[7,3]]
    assert points("vertex_removed")==[[3,2],[7,3]]
    assert points("vertex_minimum")==[[3,2]]
    assert states["vertex_minimum"]["undo"]==7 and len(states["vertex_minimum"]["workflow"])==10
    assert states["vertex_minimum_refused"]==states["vertex_minimum"] and report["vertex_minimum_error"]["error"]
    assert states["numeric_rectangle"]["undo"]==8 and len(states["numeric_rectangle"]["workflow"])==11
    assert points("numeric_rectangle",2)==[[1,1],[4,4]]
    assert states["box_refused"]==states["numeric_rectangle"] and report["box_error"]["error"]
    box=states["box_resized"]["metadata"][KEY]["rows"][2]
    assert (box["plane"],box["coordinate"],box["t"],box["c"])==("XZ",3,1,1)
    assert box["selection"]["kind"]=="rotated_rectangle"
    assert box["selection"]["points"]==[[6,3],[6,6],[5,3]]
    assert states["box_resized"]["undo"]==8 and len(states["box_resized"]["workflow"])==12
    assert states["box_noop"]["undo"]==8 and len(states["box_noop"]["workflow"])==13
    assert states["box_noop"]["revisions"]==states["box_resized"]["revisions"]
    assert states["number_refused"]==states["box_noop"] and report["number_error"]["error"]
    assert points("numeric_undone",2)==[[1,1],[4,4]]
    assert states["numeric_redone"]["metadata"]==states["box_resized"]["metadata"]
    assert states["reopened"]["workflow"]==states["numeric_redone"]["workflow"]
    for step in states["numeric_redone"]["workflow"]:ij.validate_planar_annotation_command_record(step)
