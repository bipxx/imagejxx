"""Real author/reset/invalid controls, kept programs and reopened execution."""
import json
import os
from pathlib import Path
import numpy as np
import pytest

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',['choices','raw_choices','restore','invalid_choices','authored'])
def test_numeric_choices_keep_history_and_reopen(context,mode,tmp_path):
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_HISTOGRAM_WORKFLOW_CPP_ROOT'])/mode/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._histogram_workflow_ui_case(mode,str(tmp_path/'capture')))
    assert report['contract']=='p10r.histogram-workflow-options.ui.001'
    assert report['context']==context and report['state']=='observed' and not report['acceptance_claim']
    assert report['defaults_before']==report['defaults_after']
    assert all(f['events']==f['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['enabled'] and c['control']['id'] for c in report['controls'])
    assert report['admission_reads']['before_plane']==report['admission_reads']['after_plane']
    assert report['admission_reads']['before_region']==report['admission_reads']['after_region']
    if mode=='invalid_choices':
        assert report['rejected']['error'] and not report['rejected']['ready']
        assert report['results']==[] and report['undo_steps']==0 and report['csv']=={}
        assert report['workflow']==report['original_workflow'] and report['final_values']==[0,1,2,3]
        return
    wire=report['retained_program'][0]
    original=report['original_workflow'][0]
    assert wire['version']==(4 if mode=='restore' else 5)
    assert [s['original'] for s in wire['steps']]==[s['original'] for s in original['steps']]
    assert report['workflow']==report['retained_program']
    assert report['prepared']['error']=='' and report['applied']['error']==''
    first,last=report['prepared']['histograms']
    if mode=='restore':
        assert 'options' not in wire['steps'][0]
        edges=np.linspace(0,20,5)
    elif mode=='raw_choices':
        assert wire['steps'][0]['options']==dict(binning='processor_raw',bins=256,range=None,log_counts=False)
        edges=np.linspace(0,12,257)
    elif mode=='authored':
        assert wire['steps'][0]['original'] is None
        assert wire['steps'][0]['options']==dict(binning='uniform_range',bins=3,range=[0.,12.],log_counts=False)
        edges=np.linspace(0,12,4)
    else:
        assert wire['steps'][0]['options']==dict(binning='uniform_range',bins=5,range=[0.,10.],log_counts=False)
        edges=np.linspace(0,10,6)
    assert first['counts']==np.histogram(np.arange(12),bins=edges)[0].tolist()
    np.testing.assert_allclose(first['edges'],edges,rtol=0,atol=0)
    assert first['finite_count']==12 and first['sum']==66 and first['mean']==5.5
    assert first['overflow_count']==int(mode=='choices')
    assert last['counts']==[0,5,5,2] and last['sum']==126
    assert report['undo_steps']==1 and report['final_values']==[5,6,7,8]
    assert [r['Sum'] for r in report['results']]==[66,126]
    names={c['name'] for c in report['controls']}
    assert {'prepare_program','keep_program','workflow.run.retained','apply'} <= names
    if mode!='authored':assert 'histogram.author' in names
