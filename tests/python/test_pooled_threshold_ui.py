"""Pooled preview and Apply must use identical captured stack membership."""
import json
import os
from pathlib import Path
import pytest
from test_auto_threshold_imagej import METHODS
from test_auto_threshold_options import DATA

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('both',[False,True])
@pytest.mark.parametrize('method',METHODS)
def test_stack_checkbox_preview_cancel_apply_and_history(method,both,context,tmp_path):
    case=method+('-ignore-both' if both else '')+'-pool'
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_POOLED_THRESHOLD_CPP_ROOT'])/('threshold-'+case)/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case('threshold-'+case,str(tmp_path/'capture')))
    assert report['contract']=='p10b.pooled-threshold.ui.001' and report['context']==context
    assert report['case_id']==case and report['method']==method and report['pooled']
    assert report['state']=='observed' and not report['acceptance_claim'] and not report['final_error']
    row=next(r for r in DATA['cases'] if r['input_id']=='uniform-256-0' and r['ignore_black']==both and r['ignore_white']==both)
    threshold=row['thresholds'][method]
    source=[list(range(z*64,(z+1)*64)) for z in range(4)]
    expected=[[255 if value>threshold else 0 for value in plane] for plane in source]
    states=report['states'];before=states['before']
    assert before['volume']==source and before['position']==[0,0,1]
    assert states['preview']==states['canceled']==before
    assert report['preview_image']['pixels']==expected[1]
    configured=report['configured'];assert configured['parameters']['stack_histogram']
    assert configured['controls']['all_slices']['value'] and not configured['controls']['all_slices']['enabled']
    for label in ['applied','redone']:
        assert states[label]['volume']==expected and states[label]['undo']==1
    assert states['undone']['volume']==source
    for state in states.values():
        for key in ['selection','previous','roi','attributes','position']:assert state[key]==before[key]
    step=states['applied']['workflow'][0]
    assert step['all_slices'] and step['parameters']['stack_histogram'] and step['parameters']['method']==method
    assert step['parameters']['ignore_black']==both and step['parameters']['ignore_white']==both
    controls={c['name'] for c in report['controls']};assert 'dialog.stack_histogram' in controls
    assert 0<len(report['frames'])<=150
    for frame in report['frames']:assert frame['events']==frame['rendered']['input_frame']['raw_inputs']
