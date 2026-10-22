"""Actual shared ImGui input; expectations are independent bin literals."""
import csv
import io
import json
import os
from pathlib import Path
import pytest

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('policy',['whole','positioned'])
def test_histogram_recorder_save_replay_and_rebind(context,policy,tmp_path):
    case='histogram-recorder-'+policy
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_HISTOGRAM_RECORDER_CPP_ROOT'])/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
    assert report['contract']=='p10m.histogram-recorder.ui.001' and report['state']=='observed'
    assert report['context']==context and not report['acceptance_claim']
    expected=[4]*6 if policy=='whole' else [0,0,2,2,0,0]
    assert report['calculated']['result']['counts']==expected
    assert report['replayed']['result']['counts']==expected and report['replayed']['frozen_target']
    assert report['recorded']==report['loaded']==[report['calculated']['command']]
    assert report['after_replay_workflow']==report['recorded']*2
    assert report['rebind_open']['rebind'] and report['rebind_open']['result'] is None
    assert report['rebind_open']['custom'] and report['rebind_open']['log']
    assert not report['rebind_open']['control_states']['custom']['enabled']
    result=report['rebound']['result'];assert result['counts']==[0]*6 and result['finite']==result['overflow']==6 and result['mean']==50.
    assert len(report['workflow'])==3 and report['workflow'][-1]['options']==report['recorded'][0]['options']
    assert report['workflow'][-1]['target']['document_id']!=report['recorded'][0]['target']['document_id']
    assert [int(x['count']) for x in csv.DictReader(io.StringIO(report['csv_text']))]==[0]*6
    assert report['before']==report['after'] and report['destination_before']==report['destination_after']
    names={c['name'] for c in report['controls']}
    assert {'workflow.record','workflow.histogram.0.replay','workflow.histogram.0.rebind','panel.calculate','panel.export'}<=names
    assert len(report['frames'])<500 and all(f['panel']['action_error']=='' for f in report['frames'])
