"""Literal numerical and physical shared-ImGui projection workflow checks."""
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij


def document():
    a=np.empty((2,2,4,2,3),np.uint16)
    for t in range(2):
        for c in range(2):
            for z in range(4):
                a[t,c,z]=100*t+10*c+2*z+np.arange(3)[None,:]+np.arange(2)[:,None]
    return ij.Document(ij.Dataset(a,axes="TCZYX"),"projection"),a


def test_prepared_python_interface_and_closed_records():
    owner,original=document();before=owner.snapshot()
    request=ij.imagej_projection_defaults();request.update(first=1,last=3,method="maximum")
    record=ij.resolve_imagej_projection_record(before,request)
    assert record["profile"]=="imagej154r" and record["destination"]=="new_document"
    assert "working_bytes" not in record["derived"] and "reduction_scratch_bytes" not in record["derived"]
    ij.validate_imagej_projection_record(json.loads(json.dumps(record)))
    prepared=ij.prepare_imagej_projection(before,request)
    result=prepared.prepared_new_document().snapshot()
    expected=original[:,:,3:4,...]
    np.testing.assert_array_equal(result.dataset.numpy(axes="TCZYX"),expected)
    assert owner.snapshot().shares_data(before)
    replay=ij.replay_imagej_projection(before,record).prepared_new_document().snapshot()
    np.testing.assert_array_equal(replay.dataset.numpy(axes="TCZYX"),expected)
    for key,value in [("first",True),("last",2.0),("group_size",True),("all_channels",1),("max_working_bytes",0)]:
        bad=dict(request);bad[key]=value
        with pytest.raises((ValueError,TypeError,RuntimeError)):
            ij.resolve_imagej_projection_record(before,bad)
    for key,value in [("version",True),("profile","native_finite"),("derived",{})]:
        bad=copy.deepcopy(record);bad[key]=value
        with pytest.raises((ValueError,RuntimeError)):
            ij.validate_imagej_projection_record(bad)
        with pytest.raises((ValueError,RuntimeError)):
            ij.rebind_imagej_projection_record(before,bad,request)


@pytest.mark.parametrize("axis,first,last,shape,expected",[
    ("time",1,1,(1,2,4,2,3),100),
    ("channels",1,1,(2,1,4,2,3),10),
])
def test_explicit_scientific_axes(axis,first,last,shape,expected):
    owner,_=document();request=ij.imagej_projection_defaults()
    request.update(axis=axis,first=first,last=last,method="maximum")
    output=ij.prepare_imagej_projection(owner.snapshot(),request).prepared_new_document().snapshot().dataset.numpy(axes="TCZYX")
    assert output.shape==shape and output[0,0,0,0,0]==expected


def test_rgb_clipped_sum_retains_sample_axis():
    pixels=np.array([[[[200,100,20]]],[[[200,100,20]]]],np.uint8)
    owner=ij.Document(ij.Dataset(pixels,axes="ZYXS"),"RGB")
    request=ij.imagej_projection_defaults();request.update(last=1,method="sum",clip_when_summing=True)
    output=ij.prepare_imagej_projection(owner.snapshot(),request).prepared_new_document().snapshot().dataset.numpy()
    assert output.shape==(1,1,1,1,1,3)
    np.testing.assert_array_equal(output.ravel(),[255,200,40])


def editor_at(path):
    pytest.importorskip("imagejxx._desktop",exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    return Editor(preferences=Preferences(path))


def test_scientific_editor_worker_sample_sd_record_replay_and_budget(tmp_path):
    editor=editor_at(tmp_path/"prefs.json");owner,_=document();editor.add_native_document(owner)
    before=owner.snapshot();editor.recording=True
    editor.update_settings(processing_memory_mb=1,persist=False)
    with pytest.raises((ValueError,RuntimeError),match="(?i)memory|budget|limit"):
        editor.action("project_imagej",first=2,last=4)
    assert not editor.busy and len(editor.document_infos)==1
    editor.update_settings(processing_memory_mb=32,persist=False)
    for bounds in ({"first":True},{"last":2**31},{"last":2**64-1}):
        with pytest.raises((ValueError,RuntimeError)):
            editor.action("project_imagej",**bounds)
        assert not editor.busy and len(editor.document_infos)==1
    editor.action("project_imagej",first=2,last=4,method="stddev",all_time_frames=False,all_channels=False)
    editor.wait();assert len(editor.document_infos)==2
    result=editor.live_document().native_snapshot()
    output=result.dataset.numpy(axes="TCZYX")
    assert output.shape==(1,1,1,2,3) and output.dtype==np.float32
    np.testing.assert_array_equal(output,np.full(output.shape,2,np.float32))
    assert owner.snapshot().shares_data(before)
    record=editor.workflow[0];assert record["schema"]=="imagejxx.imagej-projection-command"
    assert record["request"]["first"]==1 and record["request"]["last"]==3
    editor.active_index=0;editor.action("replay_imagej_projection",record=record);editor.wait()
    assert len(editor.document_infos)==3 and len(editor.workflow)==2
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy(axes="TCZYX"),output)
    editor.active_index=0;edit=owner.begin_edit();edit.set_property("stale",True);owner.commit(edit)
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("replay_imagej_projection",record=record)
    assert len(editor.document_infos)==3 and not editor.busy


def test_scientific_editor_cancellation_does_not_adopt_output(tmp_path):
    editor=editor_at(tmp_path/"prefs.json");owner,_=document();editor.add_native_document(owner)
    before=owner.snapshot();editor.recording=True
    editor.action("project_imagej",first=1,last=4,method="median")
    editor.cancel();editor.wait()
    assert len(editor.document_infos)==1 and not editor.workflow and not editor.busy
    assert owner.snapshot().shares_data(before)


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_imagej_projection_dialog(context,tmp_path):
    if context=="imgui_cpp":
        path=Path(os.environ["IMAGEJXX_PROJECTION_CPP_REPORT"])
        assert 0<path.stat().st_size<16*1024*1024;report=json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._imagej_projection_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="g034.imagej-projection-ui.001" and report["context"]==context
    assert report["state"]=="observed" and not report["error"] and not report["acceptance_claim"]
    for frame in report["frames"]:
        assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    assert {"menu.Image","menu.Image.Stacks","show_imagej_projection","first","last",
        "all_time_frames","apply","show_imagej_grouped_projection","cancel"}<={r["name"] for r in report["controls"]}
    ready=report["ready"]
    assert ready["state"]==2 and ready["documents"]==1
    assert ready["resource_bytes"]>0 and ready["peak_reserved_bytes"]==ready["resource_bytes"]
    assert ready["retained_bytes"]==ready["resource_bytes"] and report["reserved_after_publish"]==0
    expected=[[100+10*c+6+x+y for y in range(3) for x in range(4)] for c in range(2)]
    for key in ("applied","reopened"):
        row=report[key]
        assert row["shape"]==[1,2,1,3,4,1] and row["pixels"]==expected
        assert row["title"]=="MAX_projection-ui" and not row["selection"] and not row["previous_selection"]
        assert row["rois"]==0 and row["undo"]==0 and "note" not in row["properties"]
        assert [r["name"] for r in row["overlays"]]==["global","inside","wild-c-out-z"]
        assert row["overlays"][1]["c"]==1 and "z" not in row["overlays"][1] and "t" not in row["overlays"][1]
        channels=row["presentation"]["channel_display"]["channels"]
        assert [r["lut"] for r in channels]==[{"name":"Red"},{"name":"Green"}]
        assert [r["range"] for r in channels]==[[11.,111.],[22.,222.]]
        assert [r["visible"] for r in channels]==[True,False]
        assert row["workflow"][0]["request"]["all_time_frames"] is False
    assert report["source_data_unchanged"] and report["source_revisions_unchanged"]
    assert report["applied"]["document_count"]==2 and report["after_cancel"]["document_count"]==2
    assert len(report["after_cancel"]["workflow"])==1
    assert "group_size" not in report["grouped_hyperstack_dialog"]["controls"]
    assert [r["index"] for r in report["applied_render"]["overlay_layers"]]==[0,2]
    assert report["lifecycle"]["open"] is False

    replayed=report["replayed"]
    assert replayed["shape"]==[1,2,1,3,4,1] and replayed["pixels"]==expected
    assert replayed["document_count"]==3 and len(replayed["workflow"])==2
    dialog=report["rebind_dialog"]
    assert dialog["values"]["first"]==2 and dialog["values"]["last"]==4
    assert dialog["values"]["method"]=="maximum" and dialog["values"]["axis"]=="automatic"
    assert dialog["values"]["all_time_frames"] is False and dialog["values"]["all_channels"] is True
    rebound=report["rebound"]
    assert rebound["shape"]==[1,1,1,3,4,1]
    assert rebound["pixels"]==[[116+x+y for y in range(3) for x in range(4)]]
    assert rebound["title"]=="MAX_MAX_projection-ui" and rebound["document_count"]==4
    assert len(rebound["workflow"])==3 and report["rebind_source_unchanged"]
    assert rebound["workflow"][0]==replayed["workflow"][0]
    assert rebound["workflow"][2]["source"]!=rebound["workflow"][0]["source"]
    invalid=report["invalid_group"]
    assert not invalid["busy"] and invalid["state"]["document_count"]==4
    assert len(invalid["state"]["workflow"])==3
    assert invalid["error"]["open"] and "positive group size" in invalid["error"]["message"]
    grouped=report["grouped"]
    assert grouped["shape"]==[1,1,1,3,4,1]
    assert grouped["pixels"]==[[111+x+y for y in range(3) for x in range(4)]]
    assert grouped["document_count"]==5 and len(grouped["workflow"])==4
    assert grouped["workflow"][3]["request"]["group_size"]==2

    for axis,shape,planes in (
        ("time",[1,2,4,3,4,1],[[100+10*c+2*z+x+y for y in range(3) for x in range(4)] for c in range(2) for z in range(4)]),
        ("channels",[2,1,4,3,4,1],[[100*t+10+2*z+x+y for y in range(3) for x in range(4)] for t in range(2) for z in range(4)]),
    ):
        dialog=report[axis+"_dialog"]
        assert dialog["values"]["axis"]==axis and dialog["text"]["last"]=="2"
        row=report[axis+"_output"]
        assert row["shape"]==shape and row["pixels"]==planes
        assert row["workflow"][-1]["request"]["axis"]==axis
        assert row["workflow"][-1]["request"]["last"]==1
        assert row["workflow"][-1]["source"]==report["applied"]["workflow"][0]["source"]
    assert report["axis_source_unchanged"]
    rgb=report["rgb_output"]
    assert rgb["shape"]==[1,1,1,1,1,3] and rgb["pixels"]==[[255.,200.,40.]]
    assert rgb["workflow"][-1]["request"]["method"]=="sum"
    assert rgb["workflow"][-1]["request"]["clip_when_summing"] is True
    assert report["rgb_dialog"]["values"]["clip_when_summing"] is True
    assert report["rgb_source_unchanged"]
    assert rgb["document_count"]==9 and len(rgb["workflow"])==7
    names={row["name"] for row in report["controls"]}
    assert {"axis.choice.time","axis.choice.channels","method.choice.sum","clip_when_summing","rgb_apply"}<=names
