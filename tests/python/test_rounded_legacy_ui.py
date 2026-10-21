"""Physical legacy Add preview/apply budget, recovery and queued cancellation."""
import json
import math
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij


def check(r,case):
    assert r['contract']=='p08v.rounded-legacy.ui.001' and r['case_id']==case
    assert r['state']=='observed' and not r['acceptance_claim'] and r['final_error']==''
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
    assert all(c['control']['enabled'] and c['control']['id'] for c in r['controls'])
    for name in ['preview_refusal','apply_refusal']:
        assert 'memory budget' in r[name]['error'] and r[name]['jobs']==0
    assert not r['preview_refusal']['has_preview']
    s=r['states'];assert s['preview_refused']==s['apply_refused']==s['preview_ready']==s['before']
    assert r['accepted_preview']['has_preview'] and r['final_memory_mb']==2
    expected_bytes=64*64*8*17+512*1024+(64*64+7)//8
    assert 1024**2 < expected_bytes <= 2*1024**2
    for j in [r['accepted_preview']['job'],r['accepted_apply']]:
        assert j['bytes']==j['peak']==expected_bytes and j['acquired']
    source=((np.indices((64,64))[1]+3*np.indices((64,64))[0])%251).astype(np.uint16)
    selection=ij.Selection('rounded_rectangle',[(8.25,9.5),(55.75,54.25)],attributes={'corner_diameter':12,'label':'kept'})
    if case=='rotated':selection=ij.SelectionEditor(selection,(64,64)).rotated(23)
    expected=source.copy();expected[selection.mask((64,64))!=0]+=7
    np.testing.assert_array_equal(np.array(s['before']['pixels']).reshape(64,64),source)
    np.testing.assert_array_equal(np.array(s['applied']['pixels']).reshape(64,64),expected)
    assert s['undone']['pixels']==s['before']['pixels']
    assert s['redone']['pixels']==s['applied']['pixels']==s['canceled']['pixels']
    assert s['applied']['undo']==s['redone']['undo']==s['canceled']['undo']==1 and s['undone']['undo']==0
    assert len(s['applied']['workflow'])==1 and s['canceled']['workflow']==s['applied']['workflow']
    assert all(v['selection']==s['before']['selection'] and v['metadata']==s['before']['metadata'] for v in s.values())
    assert r['queued']['state']==int(ij.JobState.QUEUED) and r['queued']['bytes']==expected_bytes
    assert not r['queued']['acquired'] and r['queued']['peak']==0
    assert r['canceled_job']['state']==int(ij.JobState.CANCELED) and r['canceled_job']['canceled']
    assert not r['canceled_job']['acquired'] and r['canceled_job']['peak']==0
    assert 'jobs.cancel.queued_rounded' in [c['name'] for c in r['controls']]
    assert 'preferences.memory' in [c['name'] for c in r['controls']]


@pytest.mark.parametrize('case',['axis','rotated'])
def test_rounded_legacy_actual_controls_both_hosts(case,tmp_path):
    from imagejxx import _desktop
    r=json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._rounded_legacy_ui_case(case,str(tmp_path/'capture001')))
    check(r,case)
    check(json.loads((Path(os.environ['IMAGEJXX_ROUNDED_LEGACY_CPP_BASE'])/case/'case.json').read_bytes()),case)
