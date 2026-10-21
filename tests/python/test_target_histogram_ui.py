"""Actual ImGui sampled-grid and positioned-ROI histogram interactions."""
import csv
import io
import json
import math
import os
from pathlib import Path
import numpy as np
import pytest


def members(dtype,policy):
    values=[]
    for t in range(2):
        for z in (1,3):
            a=np.arange(16).reshape(4,4)+(2*t+(z-1)//2)*16
            if policy=='whole' or policy=='positioned' and z==1:values.extend(a.ravel())
            elif policy=='active':values.extend(a[:,:2].ravel())
            elif t==0:values.extend(a[:,2:].ravel())
    values=np.asarray(values,dtype=np.float64)
    return values*257 if dtype=='u16' else values*.25-8 if dtype=='f32' else values


def expected(values,dtype,custom):
    if custom:
        low=2056 if dtype=='u16' else -6 if dtype=='f32' else 8
        high=12336 if dtype=='u16' else 4 if dtype=='f32' else 48
        edges=np.linspace(low,high,9);counts=[0]*8
        for value in values:
            if low<=value<=high:counts[min(7,int((value-low)/(high-low)*8))]+=1
    else:
        low=0 if dtype=='u8' else float(values.min());high=256 if dtype=='u8' else float(values.max())+(1 if dtype=='u16' else 0)
        edges=[low+i*((high-low)/256) for i in range(257)];counts=[0]*256
        for value in values:
            i=int(value) if dtype=='u8' else (int(value)-int(low))*256//int(high-low) if dtype=='u16' else int((value-low)*256/(high-low))
            counts[min(i,255)]+=1
    return counts,list(edges)


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('dtype',['u8','u16','f32'])
@pytest.mark.parametrize('policy',['whole','active','positioned'])
def test_scientific_histogram_controls_and_frozen_results(context,dtype,policy,tmp_path):
    case='target-histogram-'+dtype+'-'+policy
    if context=='imgui_cpp':report=json.loads((Path(os.environ['IMAGEJXX_TARGET_HISTOGRAM_CPP_ROOT'])/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
    assert report['contract']=='p10j.target-histogram.ui.001' and report['state']=='observed' and report['context']==context and not report['acceptance_claim']
    initial=report['initial']['result'];assert initial['plane_count']==1 and not initial['scientific_target'] and initial['position']==[0,0,0]
    invalid=report['invalid_initial'];assert invalid['result']==initial and invalid['scientific_target'] and not invalid['target_valid'] and not invalid['control_states']['calculate']['enabled']
    assert report['target']['target']['ranges']==[[0,2,1],[1,2,1],[1,4,2]]
    values=members(dtype,policy)
    for name,custom in [('raw',False),('calculated',True)]:
        panel=report[name];result=panel['result'];counts,edges=expected(values,dtype,custom)
        assert panel['scientific_target'] and result['scientific_target'] and result['plane_count']==4 and result['position']==[0,1,1]
        assert result['target']['ranges']==[[0,2,1],[1,2,1],[1,4,2]] and result['target']['scope']=='ranges'
        assert result['roi_policy']==result['target']['roi_policy']=={'whole':'whole_plane','active':'active_selection','positioned':'positioned_rois'}[policy]
        assert result['roi']==(policy!='whole') and result['counts']==counts and result['edges']==edges
        assert result['visited']==result['finite']==len(values) and result['nan']==0
        assert result['mean']==pytest.approx(float(values.mean())) and result['stddev']==pytest.approx(float(values.std())) and result['sum']==float(values.sum())
        assert result['minimum']==float(values.min()) and result['maximum']==float(values.max())
        assert result['included']==sum(counts) and result['underflow']==int(sum(values<edges[0])) and result['overflow']==int(sum(values>edges[-1]))
        assert result['estimate']['plane_count']==4 and result['estimate']['total_pixels']==64 and result['estimate']['working_bytes']>0
        assert not panel['control_states']['t']['enabled'] and not panel['control_states']['roi']['enabled']
        heights=[math.log1p(n) for n in counts] if custom else counts
        assert panel['heights']==pytest.approx(heights,rel=1e-6)
    result=report['calculated']['result'];invalid=report['invalid']
    assert invalid['result']==result and invalid['draft_changed'] and not invalid['target_valid'] and not invalid['control_states']['calculate']['enabled'] and invalid['control_states']['export']['enabled']
    rows=list(csv.DictReader(io.StringIO(report['csv_text'])));assert len(rows)==8
    assert [int(r['count']) for r in rows]==result['counts'] and [float(r['lower']) for r in rows]==result['edges'][:-1] and [float(r['upper']) for r in rows]==result['edges'][1:]
    assert Path(report['csv']).read_text(encoding='utf-8')==report['csv_text']
    assert not report['mode_off']['scientific_target'] and report['mode_off']['result']==result and report['mode_off']['control_states']['t']['enabled'] and report['mode_off']['draft_changed']
    assert report['closed_source']['result']==result and not report['closed_source']['has_source'] and not report['closed_source']['targets_open']
    assert report['closed']['result'] is None and not report['closed']['open']
    before,after=report['before'],report['after'];assert before['pixels']==after['pixels'] and before['revisions']==after['revisions'] and before['dirty']==after['dirty']==False
    assert before['undo']==after['undo']==0 and before['workflow']==after['workflow']==[] and after['position']==[1,0,2]
    assert len(report['frames'])<512 and all(f['panel']['action_error']=='' for f in report['frames'])
