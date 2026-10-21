"""Real checkbox input; numerical expectations come from the Java oracle."""
import json
import os
from pathlib import Path
import pytest
from test_auto_threshold_imagej import METHODS
from test_auto_threshold_options import DATA

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('option',['black','white','both'])
@pytest.mark.parametrize('method',METHODS)
def test_endpoint_options_preview_cancel_apply_history(method,option,context,tmp_path):
    case=method+'-ignore-'+option
    if context=='imgui_cpp':
        report=json.loads((Path(os.environ['IMAGEJXX_THRESHOLD_OPTIONS_CPP_ROOT'])/('threshold-'+case)/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case('threshold-'+case,str(tmp_path/'capture')))
    assert report['contract']=='p10a.auto-threshold-options.ui.001'
    assert report['case_id']==case and report['method']==method and report['context']==context
    assert report['state']=='observed' and report['acceptance_claim'] is False
    black,white=option in ['black','both'],option in ['white','both']
    assert report['ignore_black']==black and report['ignore_white']==white
    row=next(r for r in DATA['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==black and r['ignore_white']==white)
    threshold=row['thresholds'][method];expected=[255 if x>threshold else 0 for x in range(256)]
    states=report['states'];before=states['before']
    assert before['pixels']==list(range(256)) and before['undo']==before['redo']==0
    assert states['preview']==states['canceled']==before
    assert report['preview_image']['pixels']==expected
    for label in ['applied','redone']:
        assert states[label]['pixels']==expected and states[label]['undo']==1
    assert states['undone']['pixels']==before['pixels'] and states['undone']['redo']==1
    for state in states.values():
        for key in ['selection','previous','roi','attributes']:assert state[key]==before[key]
    for params in [report['selected_parameters'],states['applied']['workflow'][0]['parameters']]:
        assert params['method']==method and params['ignore_black']==black and params['ignore_white']==white
    assert len(states['applied']['workflow'])==1 and states['applied']['workflow'][0]['operation']=='auto_mask'
    controls={c['name'] for c in report['controls']}
    for flag,on in [('ignore_black',black),('ignore_white',white)]:assert ('dialog.'+flag in controls)==on
    assert {'dialog.preview','dialog.cancel','dialog.apply','menu.Edit.Undo','menu.Edit.Redo'}<=controls
    assert not report['final_error'] and 0<len(report['frames'])<=150
    for frame in report['frames']:
        assert frame['events']==frame['rendered']['input_frame']['raw_inputs']
        assert not frame['rendered']['window_error']
