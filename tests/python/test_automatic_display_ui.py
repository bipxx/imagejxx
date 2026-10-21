"""Actual Auto/Reset/Cancel/selection/Apply input with literal tint expectations."""
import json
import os
from pathlib import Path
import pytest
from test_auto_threshold_imagej import METHODS
from test_auto_threshold_options import DATA

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',['bright','dark','both'])
@pytest.mark.parametrize('method',METHODS)
def test_automatic_display_controls_and_exact_tint(method,mode,context,tmp_path):
    case=method+'-'+mode
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_AUTOMATIC_DISPLAY_CPP_ROOT'])/('auto-display-'+case)/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case('auto-display-'+case,str(tmp_path/'capture')))
    assert report['contract']=='p10c.automatic-display.ui.001' and report['context']==context
    assert report['case_id']==case and report['state']=='observed' and not report['acceptance_claim']
    both=mode=='both';bright=mode!='dark'
    row=next(r for r in DATA['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==both and r['ignore_white']==both)
    level=row['thresholds'][method];selected=[(v>level)==bright for v in range(256)]
    before=report['states']['before'];assert before['pixels']==[list(range(256)),[200]*256]
    automatic=report['states']['automatic'];info=report['automatic']['automatic']
    assert info['method']==method and info['bright']==bright and info['level']==level
    assert info['ignore_black']==both and info['ignore_white']==both and info['matched_count']==sum(selected)
    threshold=automatic['threshold'];assert threshold['position']==[0,0,0]
    assert [threshold['lower']<=v<=threshold['upper'] for v in range(256)]==selected
    assert automatic['pixels']==before['pixels'] and automatic['undo']==0 and not automatic['dirty']
    for key in ['pixels_revision','metadata_revision','selection_revision']:assert automatic[key]==before[key]
    assert report['overlay']['available']
    expected=[c for v,inside in enumerate(selected) for c in ((255,v//2,v//2) if inside else (v,v,v))]
    assert report['overlay']['rendered_rgb']['pixels']==expected
    for label in ['reset','cancelled']:
        state=report['states'][label]
        assert state['pixels']==before['pixels'] and state['properties']==before['properties']
        assert state['undo']==0 and not state['dirty'] and state['threshold'] is None
    selection=report['states']['selection'];assert selection['selection_mask']==[255 if s else 0 for s in selected]
    assert selection['pixels']==before['pixels'] and selection['undo']==0
    applied=report['states']['applied'];assert applied['pixels']==[[255 if s else v for v,s in enumerate(selected)],[200]*256]
    assert applied['threshold'] is None and applied['undo']==1 and len(applied['workflow'])==1
    controls={x['name'] for x in report['controls']}
    assert {'menu.manual_threshold','panel.auto','panel.reset','panel.cancel','panel.to_selection','panel.apply'}<=controls
    assert 0<len(report['frames'])<=180
    for frame in report['frames']:assert frame['events']==frame['rendered']['input_frame']['raw_inputs']
