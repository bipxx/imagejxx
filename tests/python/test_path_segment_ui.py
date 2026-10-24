"""Independent command/state oracles for actual shared ImGui authoring input."""
import json
import os
from pathlib import Path
import pytest

ORIGINAL = [0,1.25,1.125,2,3.25,.25,5.25,1.125,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4]
DIVIDED = [0,1.25,1.125,2,1.75,.90625,2.25,.796875,2,3.75,.46875,5.25,1.125,
           3,6.25,2,6.25,4,5.25,4,1,1.25,4,4]
FINAL = [0,1.25,1.125,2,3.7500000000000004,.8750000000000001,2.25,.796875,
         2,1.75,.90625,2.25,.796875,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4]


@pytest.mark.parametrize('mode', ['active','saved','xy','xz','yz'])
@pytest.mark.parametrize('context', ['imgui_cpp','imgui_python'])
def test_actual_segment_controls_preview_apply_cancel_history_session(mode, context, tmp_path):
    if context == 'imgui_cpp':
        row = json.loads(Path(json.loads(os.environ['IMAGEJXX_PATH_SEGMENT_CPP_REPORTS'])[mode]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row = json.loads(editor._path_segment_ui_case(mode, str(tmp_path/'capture')))
    assert row['contract'] == 'p13g.path-segments.ui.001' and row['context'] == context and row['mode'] == mode
    assert row['state'] == 'observed' and not row['error']
    states = row['states']; before = states['before']; applied = states['applied']
    planar = mode in ['xy','xz','yz']
    index = ['xy','xz','yz'].index(mode) if planar else 0
    def selection(state):
        return state['rows'][index]['selection'] if planar else state['rois'][0]['selection'] if mode == 'saved' else state['selection']
    assert selection(before)['path'] == ORIGINAL
    assert states['cancel_preview'] == states['canceled'] == states['preview'] == before
    assert states['invalid_preview'] == before
    assert row['invalid_draft']['segments']['error'] and not row['invalid_apply']['enabled']
    for label in ['cancel_draft','split_draft']:
        draft = row[label]['path_draft'] if planar else row[label]['preview']
        assert draft['path'] == DIVIDED
    final = row['final_draft']['path_draft'] if planar else row['final_draft']['preview']
    assert final['path'] == FINAL
    assert not row['final_draft']['segments']['error']
    assert selection(applied)['path'] == FINAL
    assert selection(applied)['attributes'] == selection(before)['attributes']
    assert applied['undo'] == before['undo'] + 1
    assert selection(states['undone']) == selection(before)
    for label in ['applied','undone','redone','reopened']:
        current = states[label]
        assert current['pixels'] == before['pixels'] == [[100*z+10*y+x for y in range(10) for x in range(14)] for z in range(6)]
        assert current['spacing'] == before['spacing'] == [.5,1.25,2]
        if mode != 'active': assert current['selection'] == before['selection'] and current['previous'] == before['previous']
        if mode != 'saved': assert current['rois'] == before['rois']
        for n in range(3):
            if not planar or n != index: assert current['rows'][n] == before['rows'][n]
    for label in ['redone','reopened']: assert selection(states[label]) == selection(applied)
    names = [c['name'] for c in row['controls']]
    assert all(name in names for name in ['Stage split','Stage remove','Stage insert','Stage replace','Path X1','Path Y1'])
    assert all(c['control']['enabled'] for c in row['controls'])
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in row['frames'])
    if planar:
        assert any(layer['preview'] and layer['geometry']['path'] == FINAL
                   for f in row['frames'] for p in f['rendered']['orthogonal_planes']
                   for layer in p['planar_annotations']['layers'])
