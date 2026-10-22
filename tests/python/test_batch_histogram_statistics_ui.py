"""Batch summary review/export through real shared ImGui controls."""
import csv
import io
import json
import os
from pathlib import Path
import numpy as np
import pytest

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',['batch_statistics','batch_statistics_review','batch_statistics_conflict','batch_statistics_cancel','batch_statistics_setup'])
def test_unbinned_batch_review_and_export(context,mode,tmp_path):
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_HISTOGRAM_WORKFLOW_CPP_ROOT'])/mode/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._histogram_workflow_ui_case(mode,str(tmp_path/'capture')))
    assert report['contract']=='p10s.batch-histogram-statistics.ui.001' and report['state']=='observed' and report['context']==context
    assert not report['acceptance_claim']
    assert all(f['events']==f['input_frame']['raw_inputs'] for f in report['frames'])
    assert report['results']==[] and report['undo_steps']==0 and report['final_values']==[0,1,2,3]
    if mode=='batch_statistics_cancel':
        assert report['queued']['working'] and report['batch_files']=={}
        return
    rows=report['prepared']['batch_histograms'];assert len(rows)==4
    for r in rows:
        values=np.arange(12,dtype=float) if r['document_index']==0 else np.full(4,17.)
        if r['step_index']==2:values+=5
        assert r['input_index']==0 and r['plane_count']==(3 if r['document_index']==0 else 1)
        assert r['finite_count']==len(values) and r['included_count']==sum(values<=10)
        assert r['overflow_count']==sum(values>10) and r['nan_count']==r['underflow_count']==0
        assert r['mean']==pytest.approx(values.mean()) and r['stddev']==pytest.approx(values.std())
        assert r['minimum']==values.min() and r['maximum']==values.max() and r['sum']==values.sum()
    assert rows[0]['mean']==5.5 and rows[0]['bins']==1
    files=report['batch_files'];name='input-processed.histograms.csv'
    if mode=='batch_statistics_conflict':
        assert files[name]=='foreign winner' and 'input-processed.ijx' not in files
        assert report['applied']['error'] and report['applied']['items'][0]['state']=='Partial'
        assert len([f for f in report['applied']['files'] if f['published']])==4
    elif mode=='batch_statistics_review':assert name not in files and len(files)==5
    else:
        table=list(csv.DictReader(io.StringIO(files[name])));assert len(table)==4
        for a,b in zip(table,rows):
            assert int(a['finite_count'])==b['finite_count'] and float(a['mean'])==b['mean']
            assert float(a['stddev'])==pytest.approx(b['stddev']) and float(a['sum'])==b['sum']
        assert len(files)==6 and report['applied']['error']==''
    if mode=='batch_statistics_setup':
        assert report['setup_record']['version']==2 and report['setup_record']['histogram_statistics'] is True
        assert report['loaded_setup']['histogram_statistics'] and not report['loaded_setup']['file_consent']
        names={c['name'] for c in report['controls']}
        assert {'batch.setup_save','batch.setup_load','browser.submit.save_setup','browser.submit.load_setup'}<=names
