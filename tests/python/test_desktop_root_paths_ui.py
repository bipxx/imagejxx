"""Independent geometry and state oracles for actual root-path ImGui input."""
import json
import os
from pathlib import Path
import pytest

COMMANDS = [0, 1.25, 1.125, 2, 3.25, .25, 5.25, 1.125,
            3, 6.25, 2, 6.25, 4, 5.25, 4, 1, 1.25, 4, 4]


@pytest.mark.parametrize('mode', ['ordinary', 'linked', 'workflow'])
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_root_paths_actual_controls_and_native_session(mode, context, tmp_path):
    if context == 'imgui_cpp':
        reports = json.loads(os.environ['IMAGEJXX_ROOT_PATH_CPP_REPORTS'])
        row = json.loads(Path(reports[mode]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row = editor._root_path_ui_case(mode, str(tmp_path/'case'))
        if isinstance(row, str): row = json.loads(row)
    assert row['contract'] == 'p13e.root-path.ui.001'
    assert row['mode'] == mode and row['context'] == context
    assert row['state'] == 'observed' and row['error'] == ''
    states = row['states']; before = states['before']; applied = states['applied']
    def delivered(label):
        drag = next(value for value in row['drags'] if value['label'] == label)
        event = next(frame for frame in row['frames'] if frame['events'] and
                     frame['events'][0].get('type') == 'position' and frame['events'][0].get('point') == drag['to'])
        # Real pointer input is delivered in device pixels; numeric controls
        # below independently prove the exact binary64 editing contract.
        actual = event['rendered']['input_frame']['mouse_position']
        assert all(abs(a-b) < 1 for a, b in zip(actual, drag['to']))
        canvas = drag['canvas']
        return [(actual[i]-canvas['origin'][i])/canvas['scale'][i] for i in range(2)]
    assert before['spacing'] == ([.5, 1.25, 2] if mode == 'linked' else [1, 1, 1])
    assert before['pixels'] == [
        [100*z+10*y+x for y in range(10) for x in range(14)] for z in range(6)]
    if mode == 'workflow':
        assert states['workflow.prepared'] == before
        assert before['rois'][0]['selection']['kind'] == 'rectangle'
        assert applied['rois'][0]['selection']['path'] == COMMANDS
        assert applied['selection'] == before['selection'] and applied['previous'] == before['previous']
        assert applied['workflow'][0] == before['workflow'][0]
        assert applied['workflow'][0]['steps'][0]['operation'] == 'saved_roi_geometry'
        assert applied['workflow'][0]['steps'][0]['enabled'] is True
        assert states['undone']['rois'] == before['rois']
    else:
        assert before['selection']['path'] == COMMANDS and before['previous']['path'] == COMMANDS
        assert before['previous']['attributes']['previous'] is True
        base = before
        if mode == 'ordinary':
            assert states['active.preview'] == before
            assert states['active.after']['selection']['path'][4:6] == pytest.approx(delivered('active'), abs=1e-6)
            assert states['active.after']['previous'] == before['selection']
            assert states['active.cancel.preview'] == states['active.after']
            assert states['active.cancel.after'] == states['active.after']
            base = states['active.after']
        assert states['numeric.preview'] == base
        expected = list(COMMANDS); expected[4:6] = [3.7500000000000004, .8750000000000001]
        assert row['numeric_draft']['preview']['path'] == expected
        assert states['numeric.applied']['rois'][0]['selection']['path'] == expected
        assert states['numeric.applied']['selection'] == base['selection']
        assert states['numeric.applied']['previous'] == base['previous']
        assert states['saved.preview'] == states['numeric.applied']
        saved = states['saved.after']['rois'][0]['selection']['path']
        assert saved[4:6] == pytest.approx(delivered('saved'), abs=1e-6)
        assert saved[:4]+saved[6:] == COMMANDS[:4]+COMMANDS[6:]
        assert states['saved.cancel.preview'] == states['saved.after']
        assert states['saved.cancel.after'] == states['saved.after'] == applied
        assert states['undone']['rois'] == states['numeric.applied']['rois']
        assert applied['selection'] == base['selection'] and applied['previous'] == base['previous']
    assert applied['rois'][0]['selection']['attributes']['opaque'] == 'kept'
    assert applied['rois'][0]['attributes'] == before['rois'][0]['attributes'] == {'row': 'kept'}
    for name in ['applied', 'undone', 'redone', 'reopened']:
        assert states[name]['pixels'] == before['pixels']
        assert states[name]['spacing'] == before['spacing']
        assert states[name]['metadata'] == before['metadata']
        assert states[name]['selection'] == applied['selection']
        assert states[name]['previous'] == applied['previous']
    assert states['redone']['rois'] == states['reopened']['rois'] == applied['rois']
    assert states['reopened']['workflow'] == applied['workflow']
    assert any(layer['contour_edges'] > 0 for layer in row['applied_canvas']['style_layers'])
    idle = lambda value: value['orthogonal_planes'][0] if mode == 'linked' else value['ordinary_canvas']
    assert idle(row['idle_before'])['style_layers'] == idle(row['idle_after'])['style_layers']
    if mode == 'linked': assert idle(row['idle_before'])['annotation_builds'] == idle(row['idle_after'])['annotation_builds']
