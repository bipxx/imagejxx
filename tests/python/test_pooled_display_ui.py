"""Actual pooled-scope input with independent global histogram/tint/mask oracles."""
import json
import os
from pathlib import Path
import pytest
from test_auto_threshold_options import DATA
from test_auto_threshold_imagej import METHODS

@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
@pytest.mark.parametrize("mode",["pool","pooldark","poolboth"])
@pytest.mark.parametrize("method",METHODS)
def test_pooled_display_selection_and_all_z_apply(method,mode,context,tmp_path):
    case="auto-display-"+method+"-"+mode
    if context=="imgui_cpp":r=json.loads((Path(os.environ["IMAGEJXX_POOLED_DISPLAY_CPP_ROOT"])/case/"case.json").read_bytes())
    else:
        from imagejxx import _desktop
        r=json.loads(_desktop.Editor(str(tmp_path/"prefs.json"))._animation_ui_case(case,str(tmp_path/"capture")))
    assert r["contract"]=="p10e.pooled-display.ui.001" and r["state"]=="observed" and not r["acceptance_claim"]
    both=mode=="poolboth";bright=mode!="pooldark"
    row=next(x for x in DATA["cases"] if x["input_id"]=="uniform-256-0" and x["ignore_black"]==both and x["ignore_white"]==both);level=row["thresholds"][method]
    pixels=[[2*i+z for i in range(128)] for z in range(2)];membership=[[(v>level)==bright for v in plane] for plane in pixels]
    info=r["automatic"]["automatic"];assert info["level"]==level and info["stack_histogram"] and info["selected_samples"]==256 and info["matched_count"]==sum(map(sum,membership))
    auto=r["states"]["automatic"];assert auto["pixels"]==pixels and auto["undo"]==0 and not auto["dirty"] and auto["threshold"]["all_z"]
    assert auto["properties"]["manual_threshold"]["version"]==2
    for z,key in [(0,"overlay"),(1,"overlay_z1")]:
        rgb=[c for v,inside in zip(pixels[z],membership[z]) for c in ((255,v//2,v//2) if inside else (v,v,v))]
        assert r[key]["available"] and r[key]["source_tcz"]==[0,0,z] and r[key]["rendered_rgb"]["pixels"]==rgb
    for phase in ["reset","cancelled"]:
        assert r["states"][phase]["threshold"] is None and r["states"][phase]["pixels"]==pixels and r["states"][phase]["undo"]==0
    mask=membership[0];assert r["states"]["selection"]["selection_mask"]==[255 if s else 0 for s in mask]
    # Outside the active 2D selection stays exact on every Z.
    expected=[[((255 if (v>level)==bright else 0) if inside else v) for v,inside in zip(plane,mask)] for plane in pixels]
    final=r["states"]["applied"];assert final["pixels"]==expected and final["threshold"] is None and final["undo"]==1
    assert len(final["workflow"])==1 and final["workflow"][0]["schema_version"]==2 and final["workflow"][0]["target"]["scope"]=="ranges"
    assert r["settings_after_cancel"]["values"]["threshold_stack_histogram"]
    assert any(x["name"]=="panel.auto.stack_histogram" for x in r["controls"])
    for f in r["frames"]:assert f["events"]==f["rendered"]["input_frame"]["raw_inputs"]
