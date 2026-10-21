"""Actual Process/Shadows menu, presets and Preview/Cancel/Apply in both hosts."""
import json
import os
from pathlib import Path

import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parents[1]/'fixtures/shadows-imagej154r.json').read_bytes())
DIRECTIONS = ['north','northeast','east','southeast','south','southwest','west','northwest']


def check(report, direction):
    assert report['contract'] == 'p09g.shadows.ui.001'
    assert report['state'] == 'observed' and report['case_id'] == direction
    assert not report['acceptance_claim'] and report['final_error'] == ''
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert all(c['control']['enabled'] and c['control']['id'] for c in report['controls'])
    names = [c['name'] for c in report['controls']]
    for name in ['menu.Process','menu.Process.Shadows','menu.command.shadow_imagej',
                 'dialog.direction','dialog.direction.choice.' + direction,
                 'dialog.preview','dialog.cancel','dialog.apply','menu.Edit.Undo','menu.Edit.Redo']:
        assert name in names
    assert report['selected_parameters']['direction'] == direction
    reference = next(c for c in REFERENCE['cases'] if c['type'] == 'byte' and c['width'] == 7
        and c['height'] == 5 and c['pattern'] == 1 and not c['rectangle'] and c['direction'] == direction)
    states = report['states']; before = states['before']
    np.testing.assert_array_equal(before['pixels'], reference['source'])
    assert states['preview'] == states['canceled'] == before
    preview = report['preview_image']
    assert (preview['width'],preview['height'],preview['channels']) == (7,5,1)
    np.testing.assert_array_equal(preview['pixels'], reference['output'])
    assert before['undo'] == states['undone']['undo'] == 0
    assert states['applied']['undo'] == states['redone']['undo'] == 1
    np.testing.assert_array_equal(states['applied']['pixels'], reference['output'])
    np.testing.assert_array_equal(states['redone']['pixels'], reference['output'])
    np.testing.assert_array_equal(states['undone']['pixels'], reference['source'])
    for state in states.values():
        for field in ['selection','previous','roi','attributes']:
            assert state[field] == before[field]
    record = states['applied']['workflow']
    assert len(record) == 1 and record[0]['operation'] == 'shadow_imagej'
    assert record[0]['parameters']['direction'] == direction


@pytest.mark.parametrize('direction', DIRECTIONS)
def test_shadow_actual_controls_both_hosts(direction, tmp_path):
    from imagejxx import _desktop
    report = json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._shadow_ui_case(
        direction, str(tmp_path/'capture001')))
    check(report, direction)
    check(json.loads((Path(os.environ['IMAGEJXX_SHADOW_CPP_BASE'])/direction/'case.json').read_bytes()), direction)
