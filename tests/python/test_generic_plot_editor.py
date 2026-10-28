"""Shared plot UI with original histogram bins and legacy profile sampling."""
import csv
import io
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from test_plot_snapshots import read_plot_png,observed_snapshot


def editor_fixture(tmp_path,reads=None):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    calibration=ij.SpatialCalibration(spacing=(2,3,1),unit="um")
    dataset=ij.Dataset(np.arange(9,dtype=np.uint8).reshape(3,3)*10,axes="YX",calibration=calibration)
    if reads is not None:dataset=dataset.observe_reads(reads)
    owner=ij.Document(dataset,"Generic source")
    edit=owner.begin_edit();edit.select(ij.Selection("line",[(0,0),(2,0)]));owner.commit(edit)
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    return owner,editor


def test_legacy_profile_and_histogram_share_controls_without_changing_producers(tmp_path):
    owner,editor=editor_fixture(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    editor.action("profile");report=editor.render_headless(4);profile=report["plot"]
    assert report["fit_plot"] is None and report["plot_samples"]==3
    assert profile["kind"]=="profile" and profile["series_x"]==[0,2,4] and profile["series_y"]==[0,10,20]
    assert profile["x_label"]=="Distance (um)" and profile["y_label"]=="Pixel value (raw)" and profile["annotation"] is None
    assert all(key not in profile for key in ("measured","known","curve_x","curve_y","objective"))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    editor.action("keep_plot_snapshot");plot_id=editor.plot_snapshots_info[0]["plot_id"]
    editor.action("set_plot_limits",plot_id=plot_id,limits=[1,3,5,15],nice_ticks=True)
    editor.action("select_none");before=owner.snapshot();steps=owner.undo_steps
    editor.action("histogram",bins=4);histogram=editor.render_headless(4)["plot"]
    assert histogram["kind"]=="histogram" and histogram["counts"]==[7,2,0,0]
    assert "series_x" not in histogram and "series_y" not in histogram and "measured" not in histogram
    assert histogram["bin_edges"]==[-.5,63.5,127.5,191.5,255.5] and histogram["drawn_bars"]==4
    legacy_csv=tmp_path/"legacy.csv";editor.action("export_plot_csv",path=str(legacy_csv))
    rows=list(csv.DictReader(io.StringIO(legacy_csv.read_text())));assert list(rows[0])==["x","y"] and [int(row["y"]) for row in rows]==[7,2,0,0]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    editor.close_document(discard=True)
    frozen=observed_snapshot(editor.render_headless(4),plot_id)
    assert frozen["series_x"]==[0,2,4] and frozen["view_limits"]==[1,3,5,15]
    path=tmp_path/"profile.csv";editor.action("export_plot_csv",plot_id=plot_id,path=str(path))
    assert path.read_text()=="x,y\n0,0\n2,10\n4,20\n"


def test_scientific_result_plot_survives_source_and_panel_closure_with_exact_csv(tmp_path):
    owner,editor=editor_fixture(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    editor.action("show_histogram");editor.action("configure_histogram",custom=True,bins="4",lower="0",upper="80",roi=False)
    editor.action("calculate_histogram");editor.action("wait_histogram")
    original=tmp_path/"panel.csv";editor.action("export_histogram_csv",path=str(original))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    editor.close_document(discard=True);editor.action("show_histogram_plot");plot_id=editor.plot_snapshots_info[0]["plot_id"]
    editor.action("close_histogram");editor.action("set_plot_style",plot_id=plot_id,curve_color="#00ff00")
    path=tmp_path/"snapshot.csv";editor.action("export_plot_csv",plot_id=plot_id,path=str(path));assert path.read_bytes()==original.read_bytes()
    rows=list(csv.DictReader(io.StringIO(path.read_text())));assert [int(r["count"]) for r in rows]==[2,2,2,3]
    png=tmp_path/"histogram-µ.png";editor.action("export_plot_image",plot_id=plot_id,path=str(png),width=512,height=320)
    pixels=read_plot_png(png);assert pixels.shape==(320,512,3) and np.all(pixels[200,130]==[0,255,0])
    editor.action("close_plot_snapshot",plot_id=plot_id);assert editor.render_memory_info["retained_bytes"]==editor.render_memory_info["working_bytes"]==0


@pytest.mark.parametrize("route",["profile","histogram"])
def test_generic_plot_admission_refusal_preserves_previous_plot(route,tmp_path):
    owner,editor=editor_fixture(tmp_path);editor.action("profile");editor.action("keep_plot_snapshot")
    info=editor.plot_snapshots_info
    if route=="histogram":editor.action("select_none")
    editor.update_settings(render_memory_mb=1,persist=False)
    edit=owner.begin_edit();edit.set_title("s"*(1024*1024));owner.commit(edit)
    before=owner.snapshot();steps=owner.undo_steps;retained=editor.render_memory_info["retained_bytes"]
    with pytest.raises((ValueError,RuntimeError),match="memory"):editor.action(route,**({"bins":4} if route=="histogram" else {}))
    assert editor.plot_snapshots_info==info and owner.snapshot().shares_data(before) and owner.undo_steps==steps
    assert editor.render_memory_info["retained_bytes"]==retained and editor.render_memory_info["working_bytes"]==0


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_generic_plot_workflow(context,tmp_path):
    if context=="imgui_cpp":report=json.loads(Path(os.environ["IMAGEJXX_GENERIC_PLOT_CPP_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"));report=json.loads(editor._generic_plot_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="generic-plot-ui.001" and report["context"]==context
    assert report["state"]=="observed" and not report["error"] and len(report["frames"])<=160
    for frame in report["frames"]:assert frame["events"]==frame["input_frame"]["raw_inputs"]
    assert report["producer_source_unchanged"] and report["source_released"]
    assert report["profile"]["context_caption"]=="XY profile | T 1, C 1, Z 1 | active line selection"
    assert report["legacy_histogram"]["context_caption"]=="XY histogram | T 1, C 1, Z 1 | whole plane"
    assert report["scientific_plot"]["context_caption"]=="1 XY plane | T 1, C 1, Z 1 | whole plane"
    sample=report["histogram_hover"]["sample"]
    assert sample["kind"]=="histogram_bin" and sample["index"]==1 and sample["count"]==2 and sample["lower"]==20 and sample["upper"]==40 and not sample["upper_inclusive"]
    for key in ("profile_hover","profile_log_reversed_hover"):
        sample=report[key]["sample"];assert sample["kind"]=="profile_sample" and sample["index"]==1 and sample["x"]==2 and sample["y"]==10
    assert report["png_export"]["context_caption"]==report["profile"]["context_caption"]
    assert report["profile"]["series_x"]==[0,2,4] and report["profile"]["series_y"]==[0,10,20]
    assert report["legacy_histogram"]["counts"]==[7,2,0,0]
    assert report["scientific_plot"]["counts"]==report["scientific_result"]["counts"]==[2,2,2,3]
    for key in ("profile","legacy","scientific"):
        plot=report["after_source_close"][key];assert plot["annotation"] is None and plot["source_title"]=="Generic source"
    assert {field["name"] for field in report["style_schema"]}=={"line_width","curve_color"}
    rows=list(csv.DictReader(io.StringIO(report["csv_text"])));assert [int(r["count"]) for r in rows]==[2,2,2,3]
    assert Path(report["csv_path"]).read_text()==report["csv_text"]
    assert report["png_export"]["kind"]=="profile" and report["png_export"]["model_overlay"] is None
    pixels=read_plot_png(report["png_path"]);assert np.count_nonzero(np.all(pixels==[0,255,0],axis=-1))>500
    assert report["final_info"]==[] and report["memory_final"]["retained_bytes"]==report["memory_final"]["working_bytes"]==0


def test_polyline_profile_preserves_distances_and_shared_junction_sampling(tmp_path):
    owner,editor=editor_fixture(tmp_path)
    edit=owner.begin_edit();edit.select(ij.Selection("polyline",[(0,0),(2,0),(2,2)]));owner.commit(edit)
    before=owner.snapshot();steps=owner.undo_steps;editor.action("profile")
    plot=editor.render_headless(4)["plot"]
    assert plot["series_x"]==[0,2,4,7,10] and plot["series_y"]==[0,10,20,50,80]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps


def test_legacy_histogram_csv_retains_bins_above_scientific_export_limit(tmp_path):
    _,editor=editor_fixture(tmp_path);editor.action("select_none");editor.action("histogram",bins=65537)
    path=tmp_path/"many-bins.csv";editor.action("export_plot_csv",path=str(path))
    with path.open() as stream:
        reader=csv.DictReader(stream);assert reader.fieldnames==["x","y"]
        counts=[int(row["y"]) for row in reader]
    assert len(counts)==65537 and sum(counts)==9


def test_reopening_histogram_shares_one_payload_and_never_reads_source(tmp_path):
    reads=ij.DatasetReadObserver();owner,editor=editor_fixture(tmp_path,reads)
    editor.action("show_histogram");editor.action("configure_histogram",custom=True,bins="4",lower="0",upper="80",roi=False)
    editor.action("calculate_histogram");editor.action("wait_histogram")
    count=reads.counts().plane_reads
    editor.action("show_histogram_plot");first=editor.plot_snapshots_info[0];memory=editor.render_memory_info["retained_bytes"]
    editor.action("show_histogram_plot");second=editor.plot_snapshots_info[1]
    assert first["payload_id"]==second["payload_id"] and editor.render_memory_info["retained_bytes"]==memory+second["window_bytes"]
    editor.action("export_plot_csv",plot_id=second["plot_id"],path=str(tmp_path/"no-reads.csv"))
    editor.action("export_plot_image",plot_id=second["plot_id"],path=str(tmp_path/"no-reads.png"),width=256,height=192)
    assert reads.counts().plane_reads==count
    editor.close_document(discard=True);editor.action("close_histogram")
    editor.action("close_plot_snapshot",plot_id=first["plot_id"]);editor.action("close_plot_snapshot",plot_id=second["plot_id"])
    assert editor.render_memory_info["retained_bytes"]==0


def test_profile_sample_lookup_is_exact_source_independent_and_view_independent(tmp_path):
    reads=ij.DatasetReadObserver();owner,editor=editor_fixture(tmp_path,reads);editor.action("profile")
    before=owner.snapshot();steps=owner.undo_steps;count=reads.counts().plane_reads
    for x,index in [(-1,0),(0,0),(1,0),(1.1,1),(2,1),(3,1),(3.1,2),(8,2)]:
        sample=editor.plot_sample_at(x);assert sample["index"]==index and sample["x"]==[0,2,4][index] and sample["y"]==[0,10,20][index]
    editor.action("keep_plot_snapshot");plot_id=editor.plot_snapshots_info[0]["plot_id"]
    editor.action("set_plot_limits",plot_id=plot_id,limits=[4,1,20,1],log_x=True,log_y=True)
    assert editor.plot_sample_at(2,plot_id=plot_id)["index"]==1
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps and reads.counts().plane_reads==count
    edit=owner.begin_edit();edit.set_title("Changed after capture");owner.commit(edit)
    with pytest.raises((ValueError,RuntimeError)):editor.plot_sample_at(2)
    editor.close_document(discard=True);sample=editor.plot_sample_at(2,plot_id=plot_id)
    assert sample["context_caption"]=="XY profile | T 1, C 1, Z 1 | active line selection"
    editor.action("close_plot_snapshot",plot_id=plot_id)
    with pytest.raises((ValueError,RuntimeError),match="closed"):editor.plot_sample_at(2,plot_id=plot_id)


def test_histogram_lookup_uses_half_open_bins_and_closed_final_edge(tmp_path):
    _,editor=editor_fixture(tmp_path);editor.action("select_none");editor.action("histogram",bins=4)
    for x,index in [(-.5,0),(0,0),(63.499,0),(63.5,1),(127.5,2),(255.5,3)]:
        sample=editor.plot_sample_at(x);assert sample["index"]==index and sample["count"]==[7,2,0,0][index]
        assert sample["upper_inclusive"]==(index==3)
    assert editor.plot_sample_at(-1) is None and editor.plot_sample_at(256) is None


@pytest.mark.parametrize("field,value",[("x",True),("x","2"),("x",float("nan")),("plot_id",True),("plot_id",1.0),("plot_id",-1)])
def test_sample_lookup_parameters_are_strict_and_read_only(field,value,tmp_path):
    owner,editor=editor_fixture(tmp_path);editor.action("profile");before=owner.snapshot();steps=owner.undo_steps
    params=dict(x=2,plot_id=0);params[field]=value
    with pytest.raises((ValueError,RuntimeError,TypeError)):editor.plot_sample_at(**params)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps


@pytest.mark.parametrize("roi",["whole_plane","positioned_rois"])
def test_pooled_context_is_captured_before_navigation_and_source_closure(roi,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    reads=ij.DatasetReadObserver();dataset=ij.Dataset(np.arange(24,dtype=np.uint8).reshape(2,2,3,1,2),axes="TCZYX").observe_reads(reads)
    owner=ij.Document(dataset,"Pooled source");editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    editor.action("show_histogram");editor.action("configure_histogram_target",target=dict(scope="ranges",t=dict(start=0,stop=2),c=dict(start=1,stop=2),z=dict(start=0,stop=3,step=2),roi=roi))
    editor.action("calculate_histogram");editor.action("wait_histogram");count=reads.counts().plane_reads
    display=owner.snapshot().display;display.t=1;display.c=0;display.crosshair=[0,0,1];owner.set_display(display)
    edit=owner.begin_edit();edit.set_title("Retitled");owner.commit(edit)
    editor.action("show_histogram_plot");info=editor.plot_snapshots_info[0];plot_id=info["plot_id"]
    expected="4 XY planes | T 1..2 step 1, C 2..2 step 1, Z 1..3 step 2 | "+("whole plane" if roi=="whole_plane" else "positioned ROI unions")
    assert info["context_caption"]==expected and info["source_title"]=="Pooled source"
    editor.close_document(discard=True);editor.action("close_histogram")
    sample=editor.plot_sample_at(.25,plot_id=plot_id)
    assert sample["context_caption"]==expected and sample["count"]==0 and reads.counts().plane_reads==count
    assert (sample["lower"],sample["upper"])==((0,1) if roi=="whole_plane" else (.25,.25+1/256))


def test_calibration_plot_lookup_refuses_without_inventing_generic_samples(tmp_path):
    from test_plot_snapshots import editor_with_plot
    owner,editor=editor_with_plot(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    with pytest.raises((ValueError,RuntimeError),match="histogram or profile"):editor.plot_sample_at(0)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
