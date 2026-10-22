import json
import os
from pathlib import Path
import pytest

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',['cancel','draft','source','close','replay','targets'])
def test_histogram_async_controls_and_publication(context,mode,tmp_path):
    case='histogram-async-'+mode
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_HISTOGRAM_ASYNC_CPP_ROOT'])/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
    assert report['contract']=='p10o.histogram-async.ui.001' and report['state']=='observed' and report['context']==context and not report['acceptance_claim']
    assert report['baseline']['result']['counts']==[12,4,0,0]
    assert report['queued_busy'] and report['queued']['active'] and report['queued']['job']['state']=='queued'
    panel=report['queued_panel'];assert panel['control_states']['cancel_job']['enabled'] and not panel['control_states']['calculate']['enabled']
    assert not report['processing_after']['active'] and report['reserved_after']==0
    if mode!='targets':assert report['defaults_after']==report['defaults_before']
    if mode=='targets':
        assert report['processing_after']['job']['state']=='completed' and not report['after']['targets_open']
        assert report['after']['result']['counts']==[8,8,0,0,0,0] and len(report['workflow_after'])==2
        assert report['defaults_after']['values']['histogram_bins']==6
    elif mode=='replay':
        assert report['processing_after']['job']['state']=='completed' and report['after']['frozen_target']
        assert report['after']['result']['counts']==[12,4,0,0] and len(report['workflow_after'])==2 and report['source']['position']==2
    else:
        assert report['processing_after']['job']['state']=='canceled' and report['workflow_after']==report['workflow_before']
        if mode=='close':assert not report['after']['open'] and report['after']['result'] is None
        else:assert report['after']['result']==report['baseline']['result']
    if mode=='draft':assert report['after']['bins']=='0'
    assert report['source']['undo']==int(mode=='source')
    names={r['name'] for r in report['controls']}
    if mode=='cancel':assert 'panel.cancel_job' in names
    assert len(report['frames'])<400 and all(f['panel']['action_error']=='' for f in report['frames'])
