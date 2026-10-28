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
