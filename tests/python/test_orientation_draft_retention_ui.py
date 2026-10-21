"""Scientific partial range text survives plane changes until explicit correction."""
import hashlib
import json
import os
from pathlib import Path
import pytest


@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_orientation_partial_draft_survives_plane_switch(context, tmp_path):
    if context == 'imgui_cpp':
        index_path = Path(os.environ['IMAGEJXX_ORIENTATION_DRAFT_CPP_REPORT'])
        index = json.loads(index_path.read_bytes())
        assert index['case_count'] == 1 and index['cases'][0]['case_id'] == 'O06'
        entry = index['cases'][0]; path = index_path.parent / entry['relative_path']
        raw = path.read_bytes()
        assert len(raw) == entry['byte_count'] and hashlib.sha256(raw).hexdigest() == entry['sha256']
        report = json.loads(raw)
    else:
        from imagejxx import _desktop
        report = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._scientific_orientation_ui_case('O06', str(tmp_path/'capture')))
    assert report['contract_id'] == 'p06i.scientific-orientation-ui.001'
    assert report['case_id'] == 'O06' and report['context'] == context
    assert report['observation_state'] == 'observed' and not report['acceptance_claim']
    assert not report['window_error'] and report['frame_count'] == len(report['frames']) <= 180
    assert all(f['requested_events'] == f['report']['input_frame']['raw_inputs'] for f in report['frames'])
    states = report['states']; source_id = report['source_document_id']
    source = lambda s: next(d for d in s['documents'] if d['document_id'] == source_id)
    original = source(states['navigated'])
    for label, plane, field in [('invalid.first.xy', 'XY', 'first'),
                                 ('invalid.last.yz', 'YZ', 'last'), ('invalid.step.xz', 'XZ', 'step')]:
        state = states[label]; dialog = state['dialog']
        assert dialog['values']['plane'] == plane and dialog['text'][field] == '-'
        assert not dialog['can_apply'] and not dialog['controls']['apply']['enabled'] and dialog['error']
        assert source(state) == original and not state['window_error']
    for label in ['invalid.first', 'invalid.last', 'invalid.step.zero']:
        before, after = states[label+'.before_disabled'], states[label+'.after_disabled']
        assert {k:v for k,v in before.items() if k not in ['dialog','frame_ordinal']} == {k:v for k,v in after.items() if k not in ['dialog','frame_ordinal']}
        assert after['dialog']['text'] == before['dialog']['text']
        assert not after['dialog']['controls']['apply']['enabled'] and after['dialog']['error']
    assert len(report['outputs']) == 1
    output = report['outputs'][0]
    assert output['request'] == dict(plane='XZ',first=1,last=6,step=2,t=1,c=2,
        reverse_order=True,flip_vertical=True,transpose=True,max_working_bytes=512*1024*1024)
    assert output['output']['shape'] == [1,1,3,9,5,1]
    assert source(states['final']) == original and all(v == 0 for v in report['final_usage'].values())
