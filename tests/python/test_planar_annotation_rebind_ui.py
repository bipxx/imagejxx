"""Actual shared ImGui rebind controls, distinct source/destination and literal oracles."""
import json
import os
from pathlib import Path
import pytest

KEY="imagejxx.planar_annotations"
@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_annotation_rebind_form(context,tmp_path):
    if context=="imgui_cpp":
        report=json.loads(Path(os.environ["IMAGEJXX_PLANAR_REBIND_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._planar_annotation_rebind_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="p06p.planar-rebind.ui.001" and report["context"]==context
    assert not report["error"] and not report["acceptance_claim"]
    assert 0<len(report["frames"])<512
    for frame in report["frames"]:assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    states=report["states"]
    assert states["canceled"]==states["before"]==states["invalid_template"]
    assert report["invalid_template"]["visible"] and report["invalid_template"]["error"]
    stored=states["stored"];rows=stored["metadata"][KEY]["rows"]
    assert len(rows)==2 and stored["undo"]==1 and len(stored["workflow"])==2
    assert rows[0]==states["before"]["metadata"][KEY]["rows"][0]
    replacement=rows[1]
    assert (replacement["name"],replacement["plane"],replacement["coordinate"],replacement["t"],replacement["c"])==("Rebound XZ","XZ",0,None,None)
    assert replacement["selection"]["points"]==[[2,2],[6,4]]
    assert replacement["selection"]["width"]==2
    assert replacement["selection"]["attributes"]["opaque"]=="kept" and replacement["attributes"]=={"tag":"preserved"}
    assert not report["stored_form"]["visible"] and report["stored_form"]["cancel_reason"]=="accepted"
    assert states["missing_destination"]==stored
    assert report["missing_destination"]["visible"] and report["missing_destination"]["error"]
    updated=states["updated"];rows=updated["metadata"][KEY]["rows"]
    assert len(rows)==2 and rows[0]["name"]=="Replaced" and rows[1]==replacement
    assert rows[0]["selection"]["kind"]=="polygon" and rows[0]["selection"]["points"]==[[2,2],[4,1],[2,4]]
    assert (rows[0]["plane"],rows[0]["coordinate"],rows[0]["t"],rows[0]["c"])==("YZ",4,1,1)
    assert rows[0]["attributes"]=={}
    assert updated["undo"]==2 and len(updated["workflow"])==3
    deleted=states["deleted"]
    assert len(deleted["metadata"][KEY]["rows"])==1 and deleted["metadata"][KEY]["rows"][0]==rows[0]
    assert deleted["undo"]==3 and len(deleted["workflow"])==4
    assert [step["operation"] for step in deleted["workflow"]]==["store","store","update","delete"]
    assert deleted["workflow"][0]==report["original"]
    assert deleted["workflow"][1]["source"]["document_id"]!=report["original"]["source"]["document_id"]
    assert deleted["workflow"][1]["source"]["shape"]==[2,2,6,9,13,1]
    assert report["original"]["source"]["shape"]==[2,2,9,15,17,1]
    assert states["before_concurrent"]==deleted
    assert states["concurrent"]==states["stale_canceled"]==states["forged_refused"]
    assert states["concurrent"]["undo"]==4 and states["concurrent"]["workflow"]==deleted["workflow"]
    assert not report["stale_form"]["visible"] and report["stale_form"]["cancel_reason"]=="source_changed"
    assert report["forged_refused"]
    for state in states.values():
        assert state["pixels"]==states["before"]["pixels"] and state["selection"] is None and state["crosshair"]==[4,3,2]
    assert states["reopened"]["metadata"]==deleted["metadata"] and states["reopened"]["workflow"]==deleted["workflow"]
    import imagejxx as ij
    for step in deleted["workflow"]:ij.validate_planar_annotation_command_record(step)

    baseline=[p["builds"] for p in report["before_render"]]
    for name in ("stored_render","updated_render","deleted_render"):
        assert [p["builds"] for p in report[name]]==baseline


def test_rebind_publication_resource_refusal_is_atomic(tmp_path):
    import imagejxx as ij
    from imagejxx import _desktop
    import numpy as np
    editor=_desktop.Editor(str(tmp_path/"preferences.json"))
    editor.update_settings(json.dumps({"processing_memory_mb":1}),False)
    owner=ij.Document(ij.Dataset(np.zeros((103,103),dtype=np.uint8),axes="YX"))
    row=ij.PlanarAnnotation("one",ij.Selection("rectangle",[(1,1),(3,3)]),"xy",0)
    original=ij.resolve_planar_annotation_command(owner.snapshot(),ij.PlanarAnnotationRequest("store",annotation=row))
    owner.commit(ij.prepare_planar_annotation(original));editor.add_native_document(owner);editor.recording=True
    before=owner.snapshot();history=owner.undo_steps;workflow=json.loads(editor.workflow)
    # One cached plane estimate fits 1MiB; two exceed it. Refuse before committing
    # or appending a record, through the same publisher used by the GUI form.
    request={"operation":"store","index":None,"annotation":original.record()["annotation"]}
    with pytest.raises(ValueError,match="visual estimate"):
        editor.action("replay_planar_annotation",json.dumps({"record":original.record(),"rebind_request":request}))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history
    assert json.loads(editor.workflow)==workflow and len(ij.planar_annotations(owner.snapshot()))==1
