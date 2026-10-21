"""Independent geometry checks for actual saved-area resize/rotate/move input."""
import json
import os
from pathlib import Path

import numpy as np
import pytest

from test_saved_roi_pointer_budget_ui import check as check_budget


def box(selection):
    points = np.asarray(selection['points'], dtype=float)
    if selection['kind'] == 'rotated_rounded_rectangle':
        return points[0], points[1] - points[0], points[2] - points[0]
    assert selection['kind'] == 'rounded_rectangle'
    return ((points[0] + points[1]) / 2,
            np.array([(points[1, 0] - points[0, 0]) / 2, 0]),
            np.array([0, (points[1, 1] - points[0, 1]) / 2]))


def close(actual, expected):
    np.testing.assert_allclose(actual, expected, atol=1e-4, rtol=0)


def check(report, case):
    check_budget(report, case, contract='p09f.saved-area-modifiers.ui.001')
    cases = report['modifier_cases']
    assert [item['label'] for item in cases] == [
        'ctrl_resize', 'shift_resize', 'shift_rotate', 'alt_move']
    baseline = report['states']['redone']
    for ordinal, item in enumerate(cases):
        label = item['label']; before = item['before']; after = item['applied']
        assert item['preview_state'] == before
        assert before['roi'] == baseline['roi']
        assert item['preview']['pointer'] and item['preview']['active']
        assert item['preview']['candidate'] == after['roi']
        assert before['undo'] == item['undone']['undo'] == item['restored']['undo'] == 1
        assert after['undo'] == item['redone']['undo'] == 2
        assert after['redo'] == item['redone']['redo'] == 0
        assert item['undone']['redo'] == item['restored']['redo'] == 1
        assert after['roi'] == item['redone']['roi']
        assert item['undone']['roi'] == item['restored']['roi'] == before['roi']
        assert len(before['workflow']) == ordinal + 1
        assert len(after['workflow']) == ordinal + 2
        assert after['workflow'][:-1] == before['workflow']
        record = after['workflow'][-1]
        assert record['version'] == 2
        assert record['source']['sample_policy'] == (
            'preserve_ieee_float32' if case.startswith('ieee') else 'finite_only')
        for state in [before, after, item['undone'], item['redone'], item['restored']]:
            for key in ['selection', 'previous', 'attributes', 'samples', 'position']:
                assert state[key] == baseline[key]
        old_attributes = {k: v for k, v in before['roi'].items() if k not in ['kind', 'points']}
        assert {k: v for k, v in after['roi'].items() if k not in ['kind', 'points']} == old_attributes
        down = item['down_input']; preview = item['preview_input']
        assert down['phase'] == 'began' and preview['phase'] == 'updated'
        assert down['handle_kind'] == (3 if label == 'shift_rotate' else 0 if label == 'alt_move' else 1)
        if label in ['ctrl_resize', 'shift_resize']:
            assert down['handle_index'] == 4
        for name in ['ctrl', 'shift', 'alt']:
            assert down[name] == preview[name] == (name == item['modifier'].lower())
        ordinal_down = item['down_frame']
        assert report['frames'][ordinal_down-1]['events'] == [
            {'type': 'button', 'name': 'left', 'down': True}]
        assert report['frames'][ordinal_down]['events'][0]['type'] == 'position'
        c, u, v = box(before['roi']); new_c, new_u, new_v = box(after['roi'])
        delivered = report['frames'][ordinal_down]['rendered']
        screen = np.array(delivered['input_frame']['mouse_position'])
        canvas = delivered['ordinary_canvas']
        cursor = (screen - np.array(canvas['origin'])) / np.array(canvas['scale'])
        close(preview['point'], cursor)
        assert np.all(np.abs(cursor - np.array(item['to_image'])) <= 1 / np.array(canvas['scale']) + 1e-8)
        assert delivered['input_frame']['modifiers'][item['modifier'].lower()]
        # Resize uses the absolute delivered cursor, not an ideal fractional
        # handle location. Moving instead uses the captured mouse-down point.
        delta = cursor - np.array(down['point'] if label == 'alt_move' else item['from_image'])
        if label != 'shift_rotate':
            assert not np.allclose(np.array(before['roi']['points']), np.array(after['roi']['points']))
        if label == 'ctrl_resize':
            close(new_c, c)
            close(new_u, u + np.dot(delta, u / np.linalg.norm(u)) * u / np.linalg.norm(u))
            close(new_v, v + np.dot(delta, v / np.linalg.norm(v)) * v / np.linalg.norm(v))
        elif label == 'shift_resize':
            close(new_c - new_u - new_v, c - u - v)
            close(np.linalg.norm(new_u) / np.linalg.norm(new_v), np.linalg.norm(u) / np.linalg.norm(v))
            close(new_u / np.linalg.norm(new_u), u / np.linalg.norm(u))
            close(new_v / np.linalg.norm(new_v), v / np.linalg.norm(v))
        elif label == 'shift_rotate':
            angle = np.deg2rad(15)
            rotation = np.array([[np.cos(angle), -np.sin(angle)], [np.sin(angle), np.cos(angle)]])
            close(new_c, c); close(new_u, rotation @ u); close(new_v, rotation @ v)
        else:
            close(new_c - c, delta); close(new_u, u); close(new_v, v)
            assert max((new_c + a * new_u + b * new_v)[0] for a in [-1, 1] for b in [-1, 1]) > 32
    assert report['modifier_final'] == cases[-1]['restored']


@pytest.mark.parametrize('case', ['finite-axis', 'finite-rotated', 'ieee-axis', 'ieee-rotated'])
def test_saved_area_modifiers_both_hosts(case, tmp_path):
    from imagejxx import _desktop
    variant = case + '-modifiers'
    report = json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._saved_roi_pointer_budget_ui_case(
        variant, str(tmp_path/'capture001')))
    check(report, variant)
    check(json.loads((Path(os.environ['IMAGEJXX_SAVED_MODIFIERS_CPP_BASE'])/variant/'case.json').read_bytes()), variant)
