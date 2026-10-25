"""Animated Shadows Demo: all real-time canvas frames and source restoration."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
from test_shadow_ui import DIRECTIONS, REFERENCE

CASES = ['byte', 'short', 'float', 'rgb', 'escape', 'stop', 'source', 'budget',
         'overflow', 'guard-stack', 'guard-type', 'guard-policy']


def check(report, case):
    assert report['contract'] == 'p09m.shadow-demo.ui.001' and report['case_id'] == case
    assert report['state'] == 'observed' and not report['acceptance_claim']
    assert not report['final_error']
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['enabled'] and c['control']['id'] for c in report['controls'])
    before, after, final = report['before'], report['after'], report['final_demo']
    assert before['selection'] is None
    assert before['previous'] == {'kind': 'line', 'points': [[0, 0], [6, 4]], 'width': 1}
    assert before['undo'] == before['redo'] == 0 and len(before['workflow']) == 1
    for field in ['pixels', 'roi', 'attributes', 'undo', 'redo', 'workflow']:
        assert after[field] == before[field]
    if case == 'source':
        assert after['selection'] != before['selection'] and not after['shares_original']
        assert after['selection'] == {'kind': 'point', 'points': [[1, 1]], 'width': 1}
        assert after['previous'] is None
    else:
        assert after == before and after['shares_original']
    assert not final['active'] and not final['retained_preview']
    assert final['job_id'] == final['resource_bytes'] == 0
    assert report['final_canvas']['pixels_source'] == 'live'
    restored = report['final_canvas']['readback']['source']
    if case == 'guard-policy':
        assert restored['encoding'] == 'IEEE754 Float32LE'
        np.testing.assert_array_equal(np.frombuffer(bytes(restored['bytes']), '<f4').view('<u4'),
                                      np.array(before['pixels'][0], '<f4').view('<u4'))
    else:
        np.testing.assert_array_equal(restored['pixels'], before['pixels'][0])
    effects = report['effects']
    if case.startswith('guard-'):
        assert not report['guard']['enabled'] and report['guard']['id']
        assert not effects and final['reason'] == 'idle'
        return
    assert 'menu.shadow.demo' in [c['name'] for c in report['controls']]
    if case == 'overflow':
        assert not effects and final['shown'] == 0 and final['reason'] == 'error'
        assert 'output is not finite' in final['error']
        return
    reason = {'escape': 'escape', 'stop': 'stopped', 'source': 'source_changed', 'budget': 'stopped'}.get(case, 'completed')
    count = 1 if case == 'budget' else 10 if case in ['escape', 'stop', 'source'] else 160
    assert final['reason'] == reason and final['shown'] == len(effects) == count
    assert final['total'] == 160 and final['cycles'] == 20 and final['interval_ms'] == 50
    assert not final['error']
    if case == 'budget':
        assert 'below current reservations' in report['budget_rejection']
        assert report['retained_memory_mb'] == 64
    profile = case if case in ['short', 'float', 'rgb'] else 'byte'
    dtype = {'byte': np.uint8, 'short': np.uint16, 'float': np.float32, 'rgb': np.uint8}[profile]
    for number, effect in enumerate(effects, 1):
        direction = DIRECTIONS[(number-1) % 8]
        assert effect['number'] == number and effect['direction'] == direction and effect['source_unchanged']
        canvas = effect['canvas']
        assert canvas['pixels_source'] == 'preview' and canvas['preview_generation'] == number
        assert canvas['readback']['available']
        if case != 'budget':
            row = next(r for r in REFERENCE['cases'] if r['type'] == profile and r['direction'] == direction
                       and r['width'] == 7 and r['height'] == 5 and r['pattern'] == 1 and not r['rectangle'])
            actual = np.array(canvas['readback']['source']['pixels'], dtype)
            expected = np.array(row['output'], dtype)
            np.testing.assert_array_equal(actual.view(np.uint32) if dtype == np.float32 else actual,
                                          expected.view(np.uint32) if dtype == np.float32 else expected)
        if number > 1:
            # Timestamps include renderer work; allow 2 ms of observation jitter.
            assert effect['elapsed_ms'] - effects[number-2]['elapsed_ms'] >= 48
    if count == 160:
        assert effects[-1]['elapsed_ms'] - effects[0]['elapsed_ms'] >= 159 * 48
    if case == 'stop':
        assert 'shadow_demo.stop' in [c['name'] for c in report['controls']]
    if case == 'escape':
        assert any(e.get('type') == 'key' and e.get('name') == 'Escape' and e.get('down')
                   for f in report['frames'] for e in f['events'])


@pytest.mark.parametrize('case', CASES)
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_shadow_demo_canvas_playback_and_restoration(tmp_path, case, context):
    if context == 'imgui_cpp':
        report = json.loads((Path(os.environ['IMAGEJXX_SHADOW_DEMO_CPP_BASE'])/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._shadow_demo_ui_case(case, str(tmp_path/'capture')))
    assert report['context'] == context
    check(report, case)
