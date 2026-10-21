"""Pointer budgets are fresh on mouse-down and release, with guarded cancellation."""
import json
import os
from pathlib import Path
import numpy as np
import pytest


def check(report, case):
    assert report['contract'] == 'p08z.saved-area-pointer-budget.ui.002'
    assert report['case_id'] == case and report['state'] == 'observed'
    assert not report['acceptance_claim'] and report['final_error'] == ''
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['id'] and c['control']['enabled'] for c in report['controls'])
    states = report['states']; before = states['before']; ready = states['ready']
    assert states['entry_refused'] == before
    assert before['position'] == [0, 0, 1] and ready['position'] == [0, 0, 0]
    for label in ['begin_refused', 'release_refused', 'pointer_preview', 'idle_pointer_preview',
                  'idle_release_refused', 'idle_recovered', 'canceled']:
        assert states[label] == ready
    for label in ['entry_refused', 'begin_refused', 'release_refused', 'idle_release_refused']:
        refusal = report[label]
        assert refusal['modal']['open'] and 'resource limit' in refusal['error']
        assert not refusal['draft']['active'] and not refusal['properties']['open']
        assert refusal['draft']['retained_count'] == refusal['draft']['retained_working_bytes'] == 0
    assert report['inflight_api_budget_change']['memory_mb'] == 1
    assert report['idle_inflight_api_budget_change']['memory_mb'] == 1
    assert not report['idle_recovered_properties']['open']
    idle = report['idle_recovered_draft']
    assert idle['active'] and not idle['pointer'] and idle['retained_count'] == 1
    assert idle['retained_working_bytes'] > 1048576
    assert idle['original'] == before['roi']
    gestures = report['gestures']
    assert [g['label'] for g in gestures] == ['low_begin', 'low_release', 'idle_low_release',
                                            'idle_recovered', 'escape_cancel', 'recovered_shift_move']
    assert not gestures[0]['down_draft']['pointer']
    for g in gestures[1:]:
        assert g['down_draft']['pointer']
        assert g['down_draft']['retained_working_bytes'] > 1048576
        if g['motion_delivered']:
            assert g['preview']['pointer']
    for g in gestures[2:4]:
        assert not g['motion_delivered'] and 'preview' not in g
        assert g['from_image'] == g['to_image'] == [16, 12]
        assert g['from_screen'] == g['to_screen']
        assert g['down_draft']['candidate'] is None
        down = g['down_frame']
        assert report['frames'][down-1]['events'] == [{'type': 'button', 'name': 'left', 'down': True}]
        assert report['frames'][down]['events'] == [{'type': 'button', 'name': 'left', 'down': False}]
    assert gestures[4]['shift'] and gestures[5]['shift']
    for state in states.values():
        for key in ['selection', 'previous', 'attributes', 'samples']:
            assert state[key] == before[key]
    applied = states['applied']
    assert applied['undo'] == states['redone']['undo'] == 1
    assert states['undone']['undo'] == 0 and states['undone']['roi'] == before['roi']
    assert applied['roi'] == states['redone']['roi']
    original = dict(before['roi']); changed = dict(applied['roi'])
    old_points = original.pop('points'); new_points = changed.pop('points')
    assert changed == original
    np.testing.assert_allclose(np.array(new_points) - np.array(old_points),
                               np.tile([2, 0], (len(old_points), 1)), atol=1e-5, rtol=0)
    assert len(applied['workflow']) == 1
    record = applied['workflow'][0]
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


@pytest.mark.parametrize('case', ['finite-axis', 'finite-rotated', 'ieee-axis', 'ieee-rotated'])
def test_saved_area_pointer_budget_both_hosts(case, tmp_path):
    from imagejxx import _desktop
    report = json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._saved_roi_pointer_budget_ui_case(
        case, str(tmp_path/'capture001')))
    check(report, case)
    check(json.loads((Path(os.environ['IMAGEJXX_SAVED_POINTER_CPP_BASE'])/case/'case.json').read_bytes()), case)
