"""Direct Shadows entries: immediate image edits, stack scope and scientific review."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_shadow_ui import DIRECTIONS, REFERENCE

CASES = [mode+'.'+direction for mode in ['ordinary', 'stack', 'scientific'] for direction in DIRECTIONS]
CASES += ['failure.float32']
CASES += ['guard.'+name for name in ['ordinary-f64', 'scientific-f64', 'scientific-rgba', 'scientific-ieee']]


def check(report, case):
    assert report['contract'] == 'p09l.shadow-presets.ui.001' and report['case_id'] == case
    assert report['state'] == 'observed' and not report['acceptance_claim']
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    states = report['states']; before = states['before']; mode, value = case.split('.')
    assert before['undo'] == 0 and len(before['workflow']) == 1
    if mode == 'failure':
        assert 'output is not finite' in report['final_error']
        assert states['after'] == before
        assert 'menu.shadow.north' in [c['name'] for c in report['controls']]
        return
    assert not report['final_error']
    if mode == 'guard':
        assert states['after'] == before
        controls = report['disabled_controls']
        assert set(controls) == {'menu.shadow.'+d for d in DIRECTIONS} | {'menu.command.shadow_imagej'}
        assert all(not c['enabled'] and c['id'] for c in controls.values())
        return
    names = [c['name'] for c in report['controls']]
    assert 'menu.shadow.'+value in names and 'menu.Edit.Undo' in names and 'menu.Edit.Redo' in names
    assert all(c['control']['enabled'] and c['control']['id'] for c in report['controls'])
    row = next(r for r in REFERENCE['cases'] if r['type'] == 'byte' and r['direction'] == value
               and r['width'] == 7 and r['height'] == 5 and r['pattern'] == 1 and not r['rectangle'])
    source = np.array(row['source'], np.uint8).reshape(5, 7)
    filtered = np.array(row['output'], np.uint8).reshape(5, 7)
    shape = tuple(report['shape'][:-1]); assert report['shape'][-1] == 1
    original = np.broadcast_to(source, shape).copy(); expected = original.copy()
    np.testing.assert_array_equal(np.array(before['pixels']).reshape(shape), original)
    if mode == 'scientific':
        assert states['seeded'] == before and 'review.confirm' in names and 'review.apply' in names
        expected[1, 1, 1] = filtered
    elif mode == 'stack':
        assert states['seeded'] == states['preview'] == before
        assert report['seeded_dialog']['open'] and report['seeded_dialog']['parameters']['direction'] == value
        assert report['seeded_dialog']['controls']['all_slices']['value'] is False
        assert report['stack_choice']['controls']['all_slices']['value'] is True
        assert 'dialog.all_slices' in names and 'dialog.apply' in names and 'dialog.preview' in names
        np.testing.assert_array_equal(report['preview_image']['pixels'], filtered.ravel())
        expected[:] = filtered
    else:
        assert not any(n.startswith(('dialog.', 'review.')) for n in names)
        expected[0, 0, 0] = filtered
    for name in ['applied', 'redone']:
        assert states[name]['undo'] == 1
        np.testing.assert_array_equal(np.array(states[name]['pixels']).reshape(shape), expected)
    assert states['undone']['undo'] == 0
    np.testing.assert_array_equal(np.array(states['undone']['pixels']).reshape(shape), original)
    for item in states.values():
        for field in ['selection', 'previous', 'roi', 'attributes', 't', 'c', 'crosshair']:
            assert item[field] == before[field]
    workflow = states['applied']['workflow']; assert len(workflow) == 2 and workflow[0] == before['workflow'][0]
    record = workflow[-1]
    if mode == 'scientific':
        operation = ij.NativeWorkflowProgram.from_json(json.dumps(record)).record()['steps'][0]
        assert operation['operation'] == 'shadow_imagej' and operation['parameters']['direction']['value'] == value
        for axis in ['t', 'c', 'z']: assert operation['target'][axis] == dict(start=1, stop=2, step=1)
    else:
        assert record['operation'] == 'shadow_imagej' and record['parameters']['direction'] == value
        assert record['all_slices'] == (mode == 'stack')


@pytest.mark.parametrize('case', CASES)
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_actual_shadow_presets_and_disabled_source_entries(tmp_path, case, context):
    if context == 'imgui_cpp':
        report = json.loads((Path(os.environ['IMAGEJXX_SHADOW_PRESET_CPP_BASE'])/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._shadow_preset_ui_case(case, str(tmp_path/'capture')))
    assert report['context'] == context
    check(report, case)
