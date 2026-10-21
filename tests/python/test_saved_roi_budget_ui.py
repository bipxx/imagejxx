"""Actual saved-area UI budget admission, live lowering and recovery."""
import json
import os
from pathlib import Path
import numpy as np
import pytest


def check(report, case):
    assert report['contract'] == 'p08y.saved-area-budget.ui.001'
    assert report['case_id'] == case and report['state'] == 'observed'
    assert not report['acceptance_claim'] and report['final_error'] == ''
    assert 1048576 < report['estimate_bytes'] < report['high_mb'] * 1048576
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['id'] and c['control']['enabled'] for c in report['controls'])
    states = report['states']
    before = states['before']
    assert states['entry_refused'] == before
    assert before['position'] == [0, 0, 1]
    assert states['preview']['position'] == [0, 0, 0]
    for key in ['entry_refused', 'apply_refused']:
        refusal = report[key]
        assert refusal['modal']['open'] and refusal['error']
        assert not refusal['draft']['active'] and not refusal['properties']['open']
        assert states[key]['undo'] == 0 and states[key]['workflow'] == []
    assert states['apply_refused'] == states['preview']
    for s in states.values():
        for key in ['selection', 'previous', 'attributes', 'samples']:
            assert s[key] == before[key]
    assert states['applied']['undo'] == states['redone']['undo'] == 1
    assert states['undone']['undo'] == 0
    assert states['undone']['roi'] == before['roi']
    changed = dict(before['roi'])
    changed['corner_diameter'] = 3
    assert states['applied']['roi'] == states['redone']['roi'] == changed
    assert len(states['applied']['workflow']) == 1
    record = states['applied']['workflow'][0]
    assert record['version'] == 2
    assert record['source']['sample_policy'] == ('preserve_ieee_float32' if case.startswith('ieee') else 'finite_only')
    patterns = [0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc12345, 0xffc54321, 1, 0x80000001]
    for z, words in enumerate(before['samples']):
        expected = np.array([(i + 7*z) % 251 for i in range(32*24)], dtype=np.float32).view(np.uint32)
        if case.startswith('ieee'):
            for i in range(len(expected)):
                if i % 9 < 8:
                    expected[i] = patterns[i % 9]
        np.testing.assert_array_equal(words, expected)
    assert sum(c['name'] == 'preferences.memory' for c in report['controls']) == 3


@pytest.mark.parametrize('case', ['finite-axis', 'finite-rotated', 'ieee-axis', 'ieee-rotated'])
def test_saved_area_budget_actual_controls_both_hosts(case, tmp_path):
    from imagejxx import _desktop
    report = json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._saved_roi_budget_ui_case(
        case, str(tmp_path/'capture001')))
    check(report, case)
    check(json.loads((Path(os.environ['IMAGEJXX_SAVED_BUDGET_CPP_BASE'])/case/'case.json').read_bytes()), case)
