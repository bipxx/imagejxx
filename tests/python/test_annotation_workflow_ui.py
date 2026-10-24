"""Independent expectations for the shared ImGui annotation workflow recipe."""
import json
import os
from pathlib import Path
import pytest


@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_curve_workflow_choices_publication_and_session(context, tmp_path):
    if context == 'imgui_cpp':
        row = json.loads(Path(os.environ['IMAGEJXX_ANNOTATION_WORKFLOW_CPP_REPORT']).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row = editor._annotation_workflow_ui_case(str(tmp_path/'case'))
        if isinstance(row, str): row = json.loads(row)
    assert row['contract'] == 'p13d.annotation-workflow.ui.001'
    assert row['context'] == context and row['state'] == 'observed' and row['error'] == ''
    states = row['states']; before = states['before']; applied = states['applied']
    assert states['choices_kept'] == before
    assert states['saved_choices_kept'] == before
    assert states['prepared']['rows'] == before['rows']
    assert states['prepared']['pixels'] == before['pixels']
    program = row['program']; assert program['version'] == 7
    step = program['steps'][0]
    assert step['operation'] == 'planar_annotation' and step['original'] == row['original']
    assert step['annotation_intent']['extent'] == [1, 1, 6, 10, 14, 1]
    assert applied['rows'][0] == before['rows'][0] and applied['rows'][2] == before['rows'][2]
    edited = applied['rows'][1]
    assert edited['name'] == 'workflow curve' and edited['plane'] == 1 and edited['coordinate'] == 0
    expected = [0, 1.25, 1.125, 2, 3.7500000000000004, .8750000000000001, 5.25, 1.125,
                3, 6.25, 2, 6.25, 4, 5.25, 4, 1, 1.25, 4, 4]
    # Native path transport retains exact commands rather than an approximation.
    geometry = edited['selection']
    assert geometry['path'] == expected
    assert edited['attributes'] == {'row': 'kept'}
    assert geometry['attributes']['opaque'] == 'kept'
    saved = program['steps'][1]
    assert saved['operation'] == 'saved_roi_geometry' and saved['enabled'] is False
    intent = saved['annotation_intent']
    assert intent['kind'] == 'saved_roi' and intent['request']['index'] == 0
    saved_expected = list(expected)
    saved_expected[4:6] = [3.8750000000000004, .6250000000000001]
    assert intent['request']['geometry']['rows'][0]['selection']['path'] == saved_expected
    assert applied['saved_rows'] == before['saved_rows']
    assert applied['pixels'] == before['pixels'] and applied['undo'] == before['undo']+1
    assert states['undone']['rows'] == before['rows']
    for name in ['redone', 'reopened']:
        assert states[name]['rows'] == applied['rows'] and states[name]['pixels'] == before['pixels']
    assert states['reopened']['workflow'] == [program]
    names = [v['name'] for v in row['controls']]
    for name in ['parameters.vertex_u', 'parameters.vertex_v', 'parameters.apply', 'parameters.keep_parameters',
                 'parameters.confirm_extent', 'program.keep_program', 'program.apply']:
        assert name in names
