"""Independent expectations for real histogram workflow inputs in both hosts."""
import csv
import io
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

MODES=['run','captured','readonly','disabled','preview','conflict','stale','cancel','batch']

def csv_counts(text):
    rows=list(csv.DictReader(io.StringIO(text)))
    assert [float(r['lower']) for r in rows]==[0,5,10,15]
    assert [float(r['upper']) for r in rows]==[5,10,15,20]
    return [int(r['count']) for r in rows]


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',MODES)
def test_workflow_histogram_inputs(context,mode,tmp_path):
    if context=='imgui_cpp':
        root=Path(os.environ['IMAGEJXX_HISTOGRAM_WORKFLOW_CPP_ROOT'])/mode
        report=json.loads((root/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        root=tmp_path/'capture'
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._histogram_workflow_ui_case(mode,str(root)))
    assert report['contract']=='p10q.histogram-workflow.ui.001'
    assert report['state']=='observed' and report['context']==context and report['case_id']==mode
    assert not report['acceptance_claim']
    assert all(f['events']==f['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['enabled'] and c['control']['id'] for c in report['controls'])
    assert len(report['frames'])<2400
    assert report['opened']['visible']
    if mode=='cancel':
        assert report['queued']['working'] and not report['queued']['ready']
        assert report['results']==[] and report['undo_steps']==0 and report['csv']=={}
        assert 'cancel' in [c['name'] for c in report['controls']]
        return
    prepared=report['prepared']
    assert prepared['ready'] and prepared['error']==''
    if mode=='batch':
        files=report['batch_files']
        assert set(files)=={'input-processed.ijx',*[f'input-processed.d{d}.{name}.csv' for d in (1,2) for name in ('initial','after')]}
        assert csv_counts(files['input-processed.d1.initial.csv'])==[5,5,2,0]
        assert csv_counts(files['input-processed.d1.after.csv'])==[0,5,5,2]
        assert csv_counts(files['input-processed.d2.initial.csv'])==[0,0,0,4]
        assert csv_counts(files['input-processed.d2.after.csv'])==[0,0,0,0]
        capture=ij.load_session(str(root/'batch-output/input-processed.ijx'))
        assert capture.results==[{'kept':42}]
        np.testing.assert_array_equal(capture.documents[0].snapshot().dataset.numpy('ZYX'),np.arange(12).reshape(3,2,2)+5)
        np.testing.assert_array_equal(capture.documents[1].snapshot().dataset.numpy('YX'),np.full((2,2),22))
        assert report['undo_steps']==0 and report['results']==[]
        assert all(f['published'] for f in report['applied']['files'])
        return
    outputs=prepared['histograms']
    if mode=='disabled':
        assert len(outputs)==1 and outputs[0]['step_index']==2
    elif mode in ['captured','readonly']:
        assert len(outputs)==1 and outputs[0]['step_index']==0
    else:
        assert [o['step_index'] for o in outputs]==[0,2]
    for out in outputs:
        values=np.arange(12,dtype=float)
        if out['step_index']==2:values+=5
        elif mode=='run':values=values[:8]
        assert out['counts']==np.histogram(values,bins=[0,5,10,15,20])[0].tolist()
        assert out['edges']==[0,5,10,15,20]
        assert out['finite_count']==out['included_count']==values.size
        assert out['visited_count']==values.size and out['nan_count']==out['underflow_count']==out['overflow_count']==0
        assert out['minimum']==values.min() and out['maximum']==values.max()
        assert out['mean']==pytest.approx(values.mean()) and out['stddev']==pytest.approx(values.std())
        assert out['sum']==values.sum()
    if mode in ['preview','stale','conflict']:
        assert report['results']==[] and report['final_values']==[0,1,2,3]
        if mode=='preview':assert prepared['preview_only'] and report['undo_steps']==0 and report['csv']=={}
        if mode=='conflict':
            assert report['applied']['error'] and report['csv']['after.csv']=='foreign winner'
            assert csv_counts(report['csv']['before.csv'])==[5,5,2,0]
            assert [f['published'] for f in report['applied']['files']]==[True,False]
        if mode=='stale':assert report['csv']=={}
        return
    assert report['applied']['error']==''
    if mode=='readonly':assert 'histogram.0.bins' in {c['name'] for c in report['controls']}
    assert len(report['results'])==len(outputs)
    for row,out in zip(report['results'],outputs):
        assert row['Label']==out['name'] and row['WorkflowStep']==out['step_index']+1
        assert row['Count']==out['finite_count'] and row['Mean']==out['mean']
        assert row['StdDev']==out['stddev'] and row['Sum']==out['sum']
    assert report['undo_steps']==int(mode in ['run','disabled'])
    assert report['final_values']==([5,6,7,8] if mode in ['run','disabled'] else [0,1,2,3])
    if mode=='run':
        assert {'browse','browser.path','browser.submit'} <= {c['name'] for c in report['controls']}
        assert csv_counts(report['csv']['before.csv'])==[5,3,0,0]
        assert csv_counts(report['csv']['after.csv'])==[0,5,5,2]
