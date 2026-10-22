"""Installed shared ImGui input; expectations are test-owned literals/formulas."""
import csv
import io
import json
import math
import os
from pathlib import Path
import numpy as np
import pytest

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('dtype',['u8','u16','f32'])
@pytest.mark.parametrize('policy',['whole','roi','empty'])
def test_histogram_controls_raw_results_log_and_csv(context,dtype,policy,tmp_path):
    case='histogram-'+dtype+'-'+policy
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_HISTOGRAM_CPP_ROOT'])/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
    assert report['contract']=='p10h.histogram.ui.001' and report['state']=='observed'
    assert report['context']==context and not report['acceptance_claim']
    assert report['opened']['result'] is None
    assert report['initial']['result']['position']==[0,0,0]
    values=np.arange(150,182,dtype=np.float64).reshape(4,8)
    if dtype=='u16':values*=256
    if dtype=='f32':values=values*.25-16
    values=values.ravel() if policy=='whole' else values[:,:4].ravel() if policy=='roi' else np.array([],dtype=np.float64)
    low=39936 if dtype=='u16' else 23 if dtype=='f32' else 156
    high=44544 if dtype=='u16' else 27.5 if dtype=='f32' else 174
    edges=np.linspace(low,high,7);included=values[(values>=low)&(values<=high)]
    counts=np.zeros(6,dtype=np.int64)
    for value in included:counts[min(int((value-low)/(high-low)*6),5)]+=1
    result=report['calculated']['result']
    assert result['position']==[1,1,1] and result['roi']==(policy!='whole')
    assert result['counts']==list(counts) and result['edges']==list(edges)
    assert result['finite']==result['visited']==len(values) and result['nan']==0
    assert result['included']==len(included)
    assert result['underflow']==int(sum(values<low)) and result['overflow']==int(sum(values>high))
    if len(values):
        assert result['mean']==pytest.approx(float(values.mean()))
        assert result['stddev']==pytest.approx(float(values.std()))
        assert result['sum']==float(values.sum()) and result['minimum']==float(values.min()) and result['maximum']==float(values.max())
    else:assert result['mean'] is result['stddev'] is result['minimum'] is result['maximum'] is None and result['sum']==0
    assert report['logged']['log'] and report['logged']['result']==result
    assert report['logged']['heights']==pytest.approx([math.log1p(n) for n in counts],rel=1e-6)
    assert report['invalid']['result']==result and report['invalid']['draft_changed']
    assert not report['invalid']['control_states']['calculate']['enabled'] and report['invalid']['control_states']['export']['enabled']
    rows=list(csv.DictReader(io.StringIO(report['csv_text'])))
    assert len(rows)==6 and [int(r['count']) for r in rows]==list(counts)
    assert [float(r['lower']) for r in rows]==list(edges[:-1]) and [float(r['upper']) for r in rows]==list(edges[1:])
    assert Path(report['csv']).read_text(encoding='utf-8')==report['csv_text']
    assert not report['stale']['control_states']['calculate']['enabled'] and report['stale']['result']==result
    assert not report['closed_source']['has_source'] and report['closed_source']['result']==result
    assert not report['closed']['open'] and report['closed']['result'] is None
    before,after=report['before'],report['after']
    assert after['pixels']==before['pixels'] and before['workflow']==[] and len(after['workflow'])==2 and after['workflow'][-1]==report['calculated']['command']
    assert after['revisions']['pixels']==before['revisions']['pixels'] and after['revisions']['selection']==before['revisions']['selection']
    assert after['undo']==1 and after['position']==[1,1,1] # one deliberate external metadata edit
    assert len(report['frames'])<300 and all(f['panel']['action_error']=='' for f in report['frames'])
