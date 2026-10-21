"""Actual selector/preview/Apply input; expected pixels use pinned Java output."""
import json
import os
from pathlib import Path
import pytest
from test_auto_threshold_imagej import DATA,METHODS

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('method',METHODS)
def test_actual_method_form_preview_apply_and_history(method,context,tmp_path):
    if context=='imgui_cpp':
        root=Path(os.environ['IMAGEJXX_THRESHOLD_CPP_ROOT'])
        report=json.loads((root/('threshold-'+method)/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case('threshold-'+method,str(tmp_path/'capture')))
        assert report==json.loads((tmp_path/'capture/case.json').read_bytes())
    assert report['contract']=='p09z.auto-threshold.ui.001'
    assert report['case_id']==method and report['context']==context
    assert report['state']=='observed' and report['acceptance_claim'] is False
    assert not report['final_error'] and 0<len(report['frames'])<=150
    source=list(range(256));threshold=next(r for r in DATA['cases'] if r['id']=='uniform-256-0')['thresholds'][method]
    expected=[255 if x>threshold else 0 for x in source]
    states=report['states'];before=states['before']
    assert before['pixels']==source and before['undo']==before['redo']==0
    for label in ['preview','canceled','undone']:assert states[label]['pixels']==source
    for label in ['applied','redone']:assert states[label]['pixels']==expected and states[label]['undo']==1
    assert states['preview']==before and states['canceled']==before
    for after in states.values():
        for key in ['selection','previous','roi','attributes']:assert after[key]==before[key]
    assert report['selected_parameters']['method']==method
    steps=states['applied']['workflow'];assert len(steps)==1
    assert steps[0]['operation']=='auto_mask' and steps[0]['parameters']['method']==method
    preview=report['preview_image'];assert preview['pixels']==expected
    controls={x['name'] for x in report['controls']}
    assert {'menu.command.auto_mask','dialog.method','dialog.method.choice.'+method,'dialog.preview','dialog.cancel','dialog.apply','menu.Edit.Undo','menu.Edit.Redo'}<=controls
    for frame in report['frames']:
        assert frame['events']==frame['rendered']['input_frame']['raw_inputs']
        assert not frame['rendered']['window_error']
