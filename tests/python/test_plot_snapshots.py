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
    assert first["source_title"]==second["source_title"]==report["live"]["source_title"]=="Snapshot source µ #1"
    baseline=report["memory_before"]["retained_bytes"]
    assert report["memory_first"]["retained_bytes"]==baseline+first["window_bytes"]
    assert report["memory_second"]["retained_bytes"]==baseline+first["window_bytes"]+second["window_bytes"]
    assert report["source_unchanged_after_keep"] and report["source_document_released"]
    for key in ("after_source_edit","after_source_close"):
        assert report[key]["live"] is None
        for name in ("first","second"):
            plot=report[key][name]
            assert plot["value_unit"]=="HU snapshot" and plot["annotation"]["formula"]=="y = x - 32768"
            assert plot["source_title"]=="Snapshot source µ #1"
            np.testing.assert_array_equal(plot["curve_y"],257*np.arange(256)-32768)
    first_style=report["styled"]["first"];styled=report["styled"]["second"]
    assert first_style["model_overlay"] is None and not first_style["style"]["annotation_visible"]
    assert styled["style"]==dict(line_width=3,point_radius=5,font_scale=1.2,annotation_x=.75,annotation_y=.25,annotation_visible=True,curve_color="#00ff00",point_color="#ff5a5a")
    overlay=styled["model_overlay"];assert overlay["text"]=="y = x - 32768\na=-32768; b=1"
    canvas=np.array(styled["canvas_size"]);box=np.minimum(np.array(overlay["text_size"])+2*overlay["padding"],canvas)
    np.testing.assert_allclose(overlay["size"],box,atol=1e-5)
    np.testing.assert_allclose(overlay["origin"],np.array(styled["canvas_origin"])+np.array([.75,.25])*(canvas-box),atol=1e-4)
    assert report["independent_views"]["first"]["view_limits"]==report["live"]["limits"]
    assert report["independent_views"]["second"]["view_limits"]==[100,200,-50,50]
    assert report["memory_source_closed"]["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]+second["window_bytes"]
    path=Path(report["csv_path"]);assert "µ" in path.name and path.read_text()==report["csv_text"]
    rows=list(csv.DictReader(io.StringIO(report["csv_text"])))
    assert len(rows)==256 and all(row["series"]=="table" for row in rows)
    np.testing.assert_array_equal([float(row["y"]) for row in rows],257*np.arange(256)-32768)
    assert len(report["after_second_close"])==1 and report["final_info"]==[]
    assert report["memory_one_left"]["retained_bytes"]==first["payload_bytes"]+first["window_bytes"]
    exported=report["png_export"];pixels=read_plot_png(report["png_path"])
    assert pixels.shape==(640,1024,3) and exported["encoded_bytes"]==Path(report["png_path"]).stat().st_size
    assert exported["source_id"]==first["source_id"] and exported["view_limits"]==[100,200,-50,50]
    assert exported["nice_ticks"] and exported["minor_ticks"]
    assert [t["value"] for t in exported["x_ticks"]]==[100,125,150,175,200]
    assert exported["x_minor_ticks"] and exported["y_minor_ticks"]
    assert report["independent_views"]["second"]["nice_ticks"] and not report["independent_views"]["first"]["nice_ticks"]
    box=exported["model_overlay"];assert box["text"]==overlay["text"]
    origin=np.array(exported["canvas_origin"]);size=np.array(exported["canvas_size"])
    expected=np.minimum(np.array(box["text_size"])+2*box["padding"],size)
    np.testing.assert_allclose(box["origin"],origin+np.array([.75,.25])*(size-expected),atol=1e-4)
    x,y=np.ceil(box["origin"]).astype(int);w,h=np.floor(box["size"]).astype(int)
    region=pixels[y+2:y+h-2,x+2:x+w-2]
    assert np.count_nonzero(np.all(region<80,axis=-1))>100
    assert np.count_nonzero(np.all(region>220,axis=-1))>10
    assert report["memory_final"]["retained_bytes"]==report["memory_final"]["working_bytes"]==0
    assert report["lifecycle"]["open"] is False


@pytest.mark.parametrize("route",["manual","table","fit"])
def test_source_caption_captured_by_every_publication_route(route,tmp_path):
    owner,editor=editor_with_plot(tmp_path)
    title="Source µ ###stable %s"
    edit=owner.begin_edit();edit.set_title(title);owner.commit(edit)
    if route=="manual":editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.signed16("HU").record,show_plot=True)
    elif route=="table":editor.action("show_intensity_calibration_plot")
    else:
        editor.action("fit_intensity_calibration",function="straight_line",measured=[0,1],known=[2,4],show_plot=True);editor.wait()
    assert editor.render_headless(4)["fit_plot"]["source_title"]==title
    editor.action("keep_plot_snapshot");info=editor.plot_snapshots_info[0]
    assert info["source_title"]==title
    edit=owner.begin_edit();edit.set_title("Renamed");owner.commit(edit)
    report=editor.render_headless(4)
    assert report["fit_plot"] is None and observed_snapshot(report,info["plot_id"])["source_title"]==title
    editor.close_document(discard=True)
    assert observed_snapshot(editor.render_headless(4),info["plot_id"])["source_title"]==title


def test_source_caption_budget_refusal_preserves_calibration_and_snapshot(tmp_path):
    owner,editor=editor_with_plot(tmp_path)
    editor.action("keep_plot_snapshot");info=editor.plot_snapshots_info
    edit=owner.begin_edit();edit.set_title("s"*(1024*1024));owner.commit(edit)
    editor.update_settings(render_memory_mb=1,persist=False)
    before=owner.snapshot();steps=owner.undo_steps;memory=editor.render_memory_info["retained_bytes"]
    with pytest.raises((ValueError,RuntimeError),match="memory"):
        editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.signed16("new unit").record,show_plot=True)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    assert editor.plot_snapshots_info==info and editor.render_memory_info["retained_bytes"]==memory


def test_plot_style_is_independent_of_axes_data_and_other_windows(tmp_path):
    owner,editor=editor_with_plot(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    editor.action("set_intensity_plot_style",annotation_visible=True,annotation_x=.75,annotation_y=.25,line_width=3,point_radius=5,curve_color="#00FF00",font_scale=1.2)
    live=editor.render_headless(4)["fit_plot"]
    assert live["style"]["curve_color"]=="#00ff00" and live["model_overlay"]["text"]=="y = x - 32768\na=-32768; b=1"
    editor.action("keep_plot_snapshot");first=editor.plot_snapshots_info[0]["plot_id"]
    editor.action("keep_plot_snapshot",plot_id=first);second=editor.plot_snapshots_info[1]["plot_id"]
    editor.action("set_intensity_plot_style",plot_id=second,annotation_x=0,annotation_y=1,curve_color="#abcdef")
    editor.action("set_intensity_plot_limits",plot_id=first,limits=[1,100,1,100],log_x=True,log_y=True)
    result=editor.render_headless(4)
    assert observed_snapshot(result,first)["style"]==live["style"]
    assert observed_snapshot(result,second)["style"]["curve_color"]=="#abcdef"
    editor.action("close_plot_snapshot",plot_id=second);editor.action("close_plot_snapshot",plot_id=first)
    assert editor.render_headless(4)["fit_plot"]["style"]==live["style"]
    editor.action("keep_plot_snapshot");first=editor.plot_snapshots_info[0]["plot_id"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    editor.close_document(discard=True)
    editor.action("set_intensity_plot_style",plot_id=first,annotation_visible=False)
    assert observed_snapshot(editor.render_headless(4),first)["model_overlay"] is None


@pytest.mark.parametrize("fields",[
    {"line_width":0},{"line_width":True},{"point_radius":13},{"font_scale":2.01},
    {"annotation_x":-.01},{"annotation_y":1.01},{"annotation_visible":1},
    {"curve_color":"red"},{"point_color":"#12345678"},{"curve_color":123},{"unknown":True},
])
def test_invalid_plot_style_is_atomic(fields,tmp_path):
    _,editor=editor_with_plot(tmp_path);before=editor.render_headless(4)["fit_plot"]["style"]
    with pytest.raises((ValueError,RuntimeError)):editor.action("set_intensity_plot_style",**fields)
    assert editor.render_headless(4)["fit_plot"]["style"]==before


def test_canvas_model_reports_undefined_fit_quality(tmp_path):
    owner,editor=editor_with_plot(tmp_path)
    editor.action("fit_intensity_calibration",function="straight_line",measured=[0,1],known=[2,2],show_plot=True);editor.wait()
    before=owner.snapshot();steps=owner.undo_steps
    editor.action("set_intensity_plot_style",annotation_visible=True)
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["annotation"]["r_squared_undefined"] and plot["model_overlay"]["text"].endswith("RMSE: 0\nR^2: undefined")
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps


def read_plot_png(path):
    """Independent standard-library PNG/CRC/filter reader; no product decoder."""
    import struct
    import zlib
    encoded=Path(path).read_bytes();assert encoded[:8]==b"\x89PNG\r\n\x1a\n"
    offset=8;compressed=bytearray();width=height=None
    while offset<len(encoded):
        size=struct.unpack_from(">I",encoded,offset)[0];kind=encoded[offset+4:offset+8]
        data=encoded[offset+8:offset+8+size];crc=struct.unpack_from(">I",encoded,offset+8+size)[0]
        assert zlib.crc32(kind+data)&0xffffffff==crc
        if kind==b"IHDR":
            width,height,depth,color,compression,filtering,interlace=struct.unpack(">IIBBBBB",data)
            assert depth==8 and color==2 and compression==filtering==interlace==0
        elif kind==b"IDAT":compressed.extend(data)
        offset+=12+size
        if kind==b"IEND":break
    assert offset==len(encoded) and width and height
    raw=zlib.decompress(compressed);stride=width*3;assert len(raw)==height*(stride+1)
    prior=bytearray(stride);rows=[]
    for y in range(height):
        start=y*(stride+1);mode=raw[start];row=bytearray(raw[start+1:start+1+stride])
        assert mode in range(5)
        for x in range(stride):
            a=row[x-3] if x>=3 else 0;b=prior[x];c=prior[x-3] if x>=3 else 0
            if mode==1:predict=a
            elif mode==2:predict=b
            elif mode==3:predict=(a+b)//2
            elif mode==4:
                estimate=a+b-c;distances=[abs(estimate-a),abs(estimate-b),abs(estimate-c)]
                predict=(a,b,c)[distances.index(min(distances))]
            else:predict=0
            row[x]=(row[x]+predict)&255
        rows.append(bytes(row));prior=row
    return np.frombuffer(b"".join(rows),np.uint8).reshape(height,width,3)


@pytest.mark.parametrize("reverse",[False,True])
def test_png_export_preserves_view_color_and_source_independent_payload(reverse,tmp_path):
    owner,editor=editor_with_plot(tmp_path)
    editor.action("set_intensity_plot_style",line_width=4,curve_color="#00ff00")
    editor.action("set_intensity_plot_limits",limits=[65535,0,-32768,32767] if reverse else [0,65535,-32768,32767],grid=False,ticks=False)
    editor.action("keep_plot_snapshot");plot_id=editor.plot_snapshots_info[0]["plot_id"]
    before=owner.snapshot();steps=owner.undo_steps;memory=editor.render_memory_info["retained_bytes"]
    first=tmp_path/"first-µ.png";second=tmp_path/"second-µ.png"
    editor.action("export_intensity_plot_image",plot_id=plot_id,path=str(first),width=512,height=320)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    assert editor.render_memory_info["retained_bytes"]==memory and editor.render_memory_info["working_bytes"]==0
    editor.close_document(discard=True)
    editor.action("export_intensity_plot_image",plot_id=plot_id,path=str(second),width=512,height=320)
    assert first.read_bytes()==second.read_bytes()
    pixels=read_plot_png(first);assert pixels.shape==(320,512,3)
    # Canvas=(80,70)+(402,195); y=x-32768, independently map x at one quarter of visible width.
    px=80+.25*402;py=70+(.25 if reverse else .75)*195
    patch=pixels[int(py)-1:int(py)+2,int(px)-1:int(px)+2]
    assert np.any(np.all(patch==[0,255,0],axis=-1))
    assert np.count_nonzero(np.all(pixels==[0,255,0],axis=-1))>500


@pytest.mark.parametrize("fields",[{"width":0},{"width":True},{"height":192.0},{"width":4096,"height":4096},{"unknown":1}])
def test_invalid_png_export_preserves_existing_file_and_memory(fields,tmp_path):
    _,editor=editor_with_plot(tmp_path);path=tmp_path/"keep.png";path.write_bytes(b"sentinel")
    memory=editor.render_memory_info["retained_bytes"]
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("export_intensity_plot_image",path=str(path),**fields)
    assert path.read_bytes()==b"sentinel" and editor.render_memory_info["retained_bytes"]==memory
    assert editor.render_memory_info["working_bytes"]==0


def test_png_export_memory_refusal_precedes_output(tmp_path):
    _,editor=editor_with_plot(tmp_path);editor.update_settings(render_memory_mb=1,persist=False)
    path=tmp_path/"keep.png";path.write_bytes(b"sentinel")
    with pytest.raises((ValueError,RuntimeError),match="memory"):
        editor.action("export_intensity_plot_image",path=str(path),width=256,height=192)
    assert path.read_bytes()==b"sentinel" and editor.render_memory_info["working_bytes"]==0


@pytest.mark.parametrize("reverse",[False,True])
def test_automatic_ticks_round_linear_values_and_preserve_snapshot_state(reverse,tmp_path):
    owner,editor=editor_with_plot(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    default=editor.render_headless(4)["fit_plot"]
    assert not default["nice_ticks"] and not default["minor_ticks"] and not default["x_minor_ticks"]
    editor.action("set_intensity_plot_limits",limits=[97,3,0,100] if reverse else [3,97,0,100],nice_ticks=True,minor_ticks=True)
    plot=editor.render_headless(4)["fit_plot"]
    assert [t["value"] for t in plot["x_ticks"]]==([75,50,25] if reverse else [25,50,75])
    assert [t["value"] for t in plot["y_ticks"]]==[0,25,50,75,100]
    assert plot["x_minor_ticks"] and plot["y_minor_ticks"]
    for axis in ("x","y"):
        labels=[t["label_bounds"] for t in plot[axis+"_ticks"] if t["label_visible"]]
        for a,b in zip(labels,labels[1:]):
            assert b[0]>=a[2]+3.9 if axis=="x" else b[3]<=a[1]-3.9
    editor.action("keep_plot_snapshot");plot_id=editor.plot_snapshots_info[0]["plot_id"]
    editor.close_document(discard=True)
    snapshot=observed_snapshot(editor.render_headless(4),plot_id)
    assert snapshot["nice_ticks"] and snapshot["minor_ticks"]
    editor.action("set_intensity_plot_limits",plot_id=plot_id,limits=[3,97,0,100],ticks=False)
    snapshot=observed_snapshot(editor.render_headless(4),plot_id)
    assert snapshot["nice_ticks"] and snapshot["minor_ticks"] and not snapshot["x_ticks"] and not snapshot["x_minor_ticks"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps


def test_automatic_log_ticks_and_extreme_ranges_are_bounded(tmp_path):
    _,editor=editor_with_plot(tmp_path)
    editor.action("set_intensity_plot_limits",limits=[1,10000,10000,1],nice_ticks=True,minor_ticks=True,log_x=True,log_y=True)
    plot=editor.render_headless(4)["fit_plot"]
    assert [t["value"] for t in plot["x_ticks"]]==[1,10,100,1000,10000]
    assert [t["value"] for t in plot["y_ticks"]]==[10000,1000,100,10,1]
    assert 0<len(plot["x_minor_ticks"])<=32 and 0<len(plot["y_minor_ticks"])<=32
    editor.action("set_intensity_plot_limits",limits=[1e-300,1e300,1e300,1e-300])
    plot=editor.render_headless(4)["fit_plot"]
    assert len(plot["x_ticks"])<=12 and len(plot["y_ticks"])<=12 and not plot["x_minor_ticks"] and not plot["y_minor_ticks"]
    assert plot["finite_geometry"]


@pytest.mark.parametrize("field",["nice_ticks","minor_ticks"])
def test_tick_flags_require_booleans(field,tmp_path):
    _,editor=editor_with_plot(tmp_path);before=editor.render_headless(4)["fit_plot"]
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("set_intensity_plot_limits",limits=[0,100,0,100],**{field:1})
    after=editor.render_headless(4)["fit_plot"]
    assert after["view_limits"]==before["view_limits"] and after[field]==before[field]
