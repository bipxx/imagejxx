"""Continuous-session physical saved-corner controls and data history."""
from pathlib import Path
import json
import os
import pytest
import imagejxx as ij

def check(report):
    assert report['observation_state']=='observed' and not report['window_error']
    assert report['acceptance_claim'] is False
    for frame in report['frames']:
        assert frame['requested_events']==frame['rendered']['input_frame']['raw_inputs']
    states=report['states']
    before=states['rounded.saved.initial']['documents'][0]
    refused=states['rounded.saved.refused']['documents'][0]
    after=states['rounded.saved.published']['documents'][0]
    undo=states['rounded.saved.undo']['documents'][0]
    redo=states['rounded.saved.redo']['documents'][0]
    for current in [refused,after,undo,redo]:
        for key in ['planes','selection','previous_selection','metadata','calibration','overlays']:
            assert current[key]==before[key]
        assert current['revisions']['pixels']==before['revisions']['pixels']
    assert refused['rois']==before['rois'] and refused['undo_steps']==before['undo_steps']
    original=before['rois'][0]['selection'];replacement=after['rois'][0]['selection']
    assert replacement=={**original,'corner_diameter':3} and original['corner_diameter']==8
    assert after['rois']==[{**before['rois'][0],'selection':replacement}]
    assert after['revisions']['metadata']==before['revisions']['metadata']+1
    assert after['undo_steps']==before['undo_steps']+1 and after['redo_steps']==0
    assert undo['rois']==before['rois'] and redo['rois']==after['rois']
    invalid=next(f['rendered']['selection_properties'] for f in report['frames'] if f['label']=='rounded.saved.invalid')
    assert invalid['open'] and invalid['geometry_only'] and invalid['error'] and not invalid['preview']
    records=[r for r in report['rounded_saved_workflow'] if r.get('schema')=='imagejxx.saved-roi-geometry-command']
    assert len(records)==1 and records[0]['version']==2
    ij.validate_saved_roi_geometry_record(records[0])

@pytest.mark.parametrize('case_id',['R02','R03'])
@pytest.mark.parametrize('host',['python','cpp'])
def test_saved_corner_actual_input_and_history(case_id,host,tmp_path):
    if host=='python':
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._selection_creation_ui_case(case_id,'imgui_python',str(tmp_path/'capture001')))
    else:
        report=json.loads((Path(os.environ['IMAGEJXX_ROUNDED_SAVED_CPP_BASE'])/case_id/'case.json').read_bytes())
    check(report)
