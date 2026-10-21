"""Actual T/C/Z editing and ROI estimation, checked against retained bins."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
from test_auto_threshold_imagej import DATA, METHODS

@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
@pytest.mark.parametrize("policy",["whole","active","positioned"])
@pytest.mark.parametrize("method",METHODS)
def test_scientific_target_actual_controls_and_exact_results(method,policy,context,tmp_path):
    case="target-threshold-"+method+"-"+policy
    if context=="imgui_cpp":r=json.loads((Path(os.environ["IMAGEJXX_SCIENTIFIC_THRESHOLD_TARGET_CPP_ROOT"])/case/"case.json").read_bytes())
    else:
        from imagejxx import _desktop
        r=json.loads(_desktop.Editor(str(tmp_path/"prefs.json"))._animation_ui_case(case,str(tmp_path/"capture")))
    assert r["contract"]=="p10f.scientific-threshold-target.ui.001" and r["state"]=="observed" and r["context"]==context and not r["acceptance_claim"]
    width=8 if policy=="whole" else 16;pixels=np.empty((2,2,4,8,width),np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(4):
                offset=8 if policy=="positioned" and z==3 else 0
                for y in range(8):
                    for x in range(width):pixels[t,c,z,y,x]=((2*t+(z-1)//2)*64+y*8+x-offset if offset<=x<offset+8 else 240) if c==1 and z in [1,3] else 17+23*t+19*c+3*z+(y*width+x)%71
    flattened=pixels.reshape(16,-1).tolist();assert r["before"]["pixels"]==flattened
    assert not r["invalid"]["target_valid"] and not r["invalid"]["control_states"]["auto"]["enabled"]
    assert r["invalid_state"]["pixels"]==flattened and r["invalid_state"]["revisions"]==r["before"]["revisions"]
    assert r["shown"]["position"]==[1,1,3] and r["target"]["target"]["ranges"]==[[0,2,1],[1,2,1],[1,4,2]]
    row=next(x for x in DATA["cases"] if x["id"]=="uniform-256-0");level=row["thresholds"][method]
    panel=r["automatic"];info=panel["automatic"];assert panel["scientific_target"] and panel["histogram_roi"] and panel["target_valid"]
    assert info["level"]==level and info["selected_samples"]==256 and info["matched_count"]==255-level
    assert panel["histogram"]["source"]=="auto_target" and panel["histogram"]["counts"]==[1]*256 and panel["histogram"]["edges"]==[-.5+i for i in range(257)]
    state=r["auto_state"];assert state["pixels"]==flattened and state["undo"]==0 and not state["dirty"] and state["revisions"]==r["before"]["revisions"]
    assert state["properties"]["manual_threshold"]["version"]==3 and state["properties"]["manual_threshold"]["ranges"]==[[0,2,1],[1,2,1],[1,4,2]]
    visible=pixels[1,1,3].ravel();expected_rgb=[v for value in visible for v in ((255,int(value)//2,int(value)//2) if value>level else (int(value),)*3)]
    assert r["overlay"]["available"] and r["overlay"]["source_tcz"]==[1,1,3] and r["overlay"]["rendered_rgb"]["pixels"]==expected_rgb
    expected=pixels.copy()
    for t in range(2):
        for z in [1,3]:
            offset=8 if policy=="positioned" and z==3 else 0
            expected[t,1,z,:,offset:offset+8]=np.where(pixels[t,1,z,:,offset:offset+8]>level,255,0)
    final=r["applied"];assert final["pixels"]==expected.reshape(16,-1).tolist() and final["undo"]==1 and final["position"]==[1,1,3]
    assert "manual_threshold" not in final["properties"] and len(final["workflow"])==1
    record=final["workflow"][0];assert record["schema_version"]==2 and record["target"]["ranges"]==[[0,2,1],[1,2,1],[1,4,2]]
    assert record["target"]["roi_policy"]=={"whole":"whole_plane","active":"active_selection","positioned":"positioned_rois"}[policy]
    controls={x["name"] for x in r["controls"]};assert {"target.t.all","target.c.range","target.z.range","target.z.step","panel.targets.histogram_roi","target.preview.show","panel.auto","panel.apply"}<=controls
    assert 0<len(r["frames"])<320
    for f in r["frames"]:assert f["events"]==f["input_frame"]["raw_inputs"]
