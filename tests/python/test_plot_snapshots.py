"""Retained native plot windows: independent lifetime, axes and managed memory."""
import csv
import io
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij


def editor_with_plot(tmp_path, unit="HU"):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=ij.Document(ij.Dataset(np.array([[0,32768,65535]],np.uint16),axes="YX"),"snapshot source")
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.signed16(unit).record,show_plot=True)
    return owner,editor


def observed_snapshot(report, plot_id):
    return next(row["plot"] for row in report["plot_snapshots"] if row["plot_id"]==plot_id)


def test_snapshots_share_payload_but_own_view_and_survive_source_closure(tmp_path):
    owner,editor=editor_with_plot(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    memory=editor.render_memory_info["retained_bytes"]
    editor.action("keep_plot_snapshot");first=editor.plot_snapshots_info[0]
    assert editor.render_memory_info["retained_bytes"]-memory==first["window_bytes"]
    editor.action("keep_plot_snapshot",plot_id=first["plot_id"]);second=editor.plot_snapshots_info[1]
    assert first["payload_id"]==second["payload_id"]
    assert editor.render_memory_info["retained_bytes"]-memory==first["window_bytes"]+second["window_bytes"]
    editor.action("set_intensity_plot_limits",plot_id=second["plot_id"],limits=[100,200,-50,50])
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    editor.close_document(discard=True)
    assert editor.render_memory_info["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]+second["window_bytes"]
    rendered=editor.render_headless(4)
    assert rendered["fit_plot"] is None
    a=observed_snapshot(rendered,first["plot_id"]);b=observed_snapshot(rendered,second["plot_id"])
    assert a["value_unit"]==b["value_unit"]=="HU"
    assert a["view_limits"]==a["limits"] and b["view_limits"]==[100,200,-50,50]
    editor.action("zoom_intensity_plot_rectangle",plot_id=first["plot_id"],rectangle=[.25,.25,.75,.75])
    assert observed_snapshot(editor.render_headless(4),first["plot_id"])["viewport"]==[.25,.75,.25,.75]
    editor.action("close_plot_snapshot",plot_id=second["plot_id"])
    assert editor.render_memory_info["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]
    editor.action("close_plot_snapshot",plot_id=first["plot_id"])
    assert editor.plot_snapshots_info==[] and editor.render_memory_info["retained_bytes"]==0


def test_replaced_live_plot_keeps_old_snapshot_and_releases_unique_buffers(tmp_path):
    owner,editor=editor_with_plot(tmp_path)
    editor.action("keep_plot_snapshot");first=editor.plot_snapshots_info[0]
    editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.signed16("new unit").record,show_plot=True)
    rendered=editor.render_headless(4)
    assert rendered["fit_plot"]["payload_id"]!=first["payload_id"]
    assert rendered["fit_plot"]["value_unit"]=="new unit"
    assert observed_snapshot(rendered,first["plot_id"])["value_unit"]=="HU"
    memory=editor.render_memory_info["retained_bytes"]
    editor.action("close_plot_snapshot",plot_id=first["plot_id"])
    assert memory-editor.render_memory_info["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]


def test_closed_snapshot_ids_never_retarget_reused_window_slots(tmp_path):
    _,editor=editor_with_plot(tmp_path)
    editor.action("keep_plot_snapshot");old=editor.plot_snapshots_info[0]["plot_id"]
    editor.render_headless(4);editor.action("close_plot_snapshot",plot_id=old)
    editor.action("keep_plot_snapshot");new=editor.plot_snapshots_info[0]["plot_id"]
    assert new>old
    with pytest.raises((ValueError,RuntimeError),match="closed"):
        editor.action("set_intensity_plot_limits",plot_id=old,limits=[0,1,0,1])
    assert observed_snapshot(editor.render_headless(4),new)["view_limits"]==[0,65535,-32768,32767]


def test_render_budget_refusal_precedes_calibration_commit(tmp_path):
    owner,editor=editor_with_plot(tmp_path,unit="u"*(512*1024))
    editor.update_settings(render_memory_mb=1,persist=False)
    editor.action("keep_plot_snapshot");before=owner.snapshot();steps=owner.undo_steps;info=editor.plot_snapshots_info
    memory=editor.render_memory_info["retained_bytes"]
    with pytest.raises((ValueError,RuntimeError),match="memory"):
        editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.signed16("v"*(512*1024)).record,show_plot=True)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    assert editor.plot_snapshots_info==info and editor.render_memory_info["retained_bytes"]==memory


@pytest.mark.parametrize("plot_id",[True,-1,1.0,"1",None])
def test_snapshot_ids_are_strict(plot_id,tmp_path):
    _,editor=editor_with_plot(tmp_path);editor.action("keep_plot_snapshot");before=editor.plot_snapshots_info
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("close_plot_snapshot",plot_id=plot_id)
    assert editor.plot_snapshots_info==before


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_floating_snapshot_lifecycle(context,tmp_path):
    if context=="imgui_cpp":report=json.loads(Path(os.environ["IMAGEJXX_PLOT_SNAPSHOTS_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._plot_snapshots_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="calibration-plot-snapshots-ui.001" and report["context"]==context
    assert report["state"]=="observed" and not report["error"] and not report["acceptance_claim"]
    assert len(report["frames"])<=192
    for frame in report["frames"]:assert frame["events"]==frame["input_frame"]["raw_inputs"]
    first=report["first_info"][0];second=report["second_info"][1]
    assert first["payload_id"]==second["payload_id"]==report["live"]["payload_id"]
    baseline=report["memory_before"]["retained_bytes"]
    assert report["memory_first"]["retained_bytes"]==baseline+first["window_bytes"]
    assert report["memory_second"]["retained_bytes"]==baseline+first["window_bytes"]+second["window_bytes"]
    assert report["source_unchanged_after_keep"] and report["source_document_released"]
    for key in ("after_source_edit","after_source_close"):
        assert report[key]["live"] is None
        for name in ("first","second"):
            plot=report[key][name]
            assert plot["value_unit"]=="HU snapshot" and plot["annotation"]["formula"]=="y = x - 32768"
            np.testing.assert_array_equal(plot["curve_y"],257*np.arange(256)-32768)
    assert report["independent_views"]["first"]["view_limits"]==report["live"]["limits"]
    assert report["independent_views"]["second"]["view_limits"]==[100,200,-50,50]
    assert report["memory_source_closed"]["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]+second["window_bytes"]
    path=Path(report["csv_path"]);assert "µ" in path.name and path.read_text()==report["csv_text"]
    rows=list(csv.DictReader(io.StringIO(report["csv_text"])))
    assert len(rows)==256 and all(row["series"]=="table" for row in rows)
    np.testing.assert_array_equal([float(row["y"]) for row in rows],257*np.arange(256)-32768)
    assert len(report["after_second_close"])==1 and report["final_info"]==[]
    assert report["memory_one_left"]["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]
    assert report["memory_final"]["retained_bytes"]==report["memory_final"]["working_bytes"]==0
    assert report["lifecycle"]["open"] is False
