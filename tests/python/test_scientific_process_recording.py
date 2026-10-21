"""Scientific menu Apply appends captured native intent with atomic publication."""
import copy
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor


@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp = json.loads(Path(os.environ['IMAGEJXX_SCIENTIFIC_PROCESS_CPP_REPORT']).read_bytes())
    root = tmp_path_factory.mktemp('recorded-process')
    window = _desktop.Editor(str(root / 'preferences.json'))
    python = json.loads(window._scientific_process_ui_case(str(root / 'capture001')))
    assert python == json.loads((root / 'capture001/case.json').read_bytes())
    return cpp, python


def original():
    return np.arange(15)[None, :] + 100 * np.arange(8)[:, None]


def test_record_appends_parameters_and_explicit_captured_tcz(reports):
    for report in reports:
        recorded = report['recorded']
        assert len(recorded['workflow']) == 2
        assert recorded['workflow'][0] == report['recorder'][0]
        program = ij.NativeWorkflowProgram.from_json(json.dumps(recorded['workflow'][1]))
        operation = program.record()['steps'][0]
        assert operation['operation'] == 'add'
        assert operation['parameters'] == {'value': {'kind': 'real', 'value': 10.0}}
        assert operation['target']['scope'] == 2  # Portable Ranges schema value.
        for name in ('t', 'c', 'z'):
            assert operation['target'][name] == {'start': 1, 'stop': 2, 'step': 1}
        assert recorded['reopened'] == recorded['workflow']


def test_capture_and_pixels_publish_together_with_one_undo(reports):
    once = original(); once[7] += 10
    twice = once.copy(); twice[7] += 10
    for report in reports:
        recorded = report['recorded']
        np.testing.assert_array_equal(recorded['pixels'], twice)
        np.testing.assert_array_equal(recorded['undone'], once)
        np.testing.assert_array_equal(recorded['redone'], twice)
        assert recorded['undo_steps'] == 2


def test_mixed_recorder_replay_keeps_order_positions_and_does_not_record_itself(reports):
    twice = original(); twice[7] += 20
    replayed = twice.copy(); replayed[0] = np.maximum(replayed[0] - 3, 0); replayed[7] += 10
    for report in reports:
        replay = report['replay']
        assert [e['title'] for e in replay['entries']] == ['Subtract', 'Add']
        np.testing.assert_array_equal(replay['before'], twice)
        np.testing.assert_array_equal(replay['after'], replayed)
        assert replay['workflow'] == report['recorded']['workflow'] and replay['undo_steps'] == 3


@pytest.fixture
def editing(tmp_path):
    editor = Editor(preferences=tmp_path / 'preferences.json')
    owner = ij.Document(ij.Dataset(np.arange(120, dtype=np.uint16).reshape(2, 3, 4, 5), axes='CZYX'))
    editor.add_native_document(owner)
    editor.recording = True
    return editor, owner


def state(editor):
    return editor.render_headless(frames=3)['native_workflow']


def click(editor, panel, name):
    control = panel['controls'][name]
    assert control['enabled'] and control['visible']
    events = [dict(type='position', space='screen', point=control['point'], frames=2),
              dict(type='button', name='left', down=True),
              dict(type='button', name='left', down=False, frames=2)]
    return editor.render_headless(frames=3, events=events)['native_workflow']


def confirmed(editor):
    editor.action('show_operation', operation='add', parameters={'value': 7})
    return click(editor, state(editor), 'confirm')


@pytest.mark.parametrize('stage', ['preview', 'prepared'])
def test_cancel_never_appends_pending_capture(editing, stage):
    editor, owner = editing
    before = copy.deepcopy(editor.workflow)
    panel = confirmed(editor)
    click(editor, panel, 'prepare_preview' if stage == 'preview' else 'prepare')
    editor.wait(); panel = state(editor)
    assert panel['ready'] and owner.undo_steps == 0 and editor.workflow == before
    click(editor, panel, 'cancel')
    assert not state(editor)['visible'] and owner.undo_steps == 0 and editor.workflow == before


def test_recording_toggle_invalidates_prepared_capture(editing):
    editor, owner = editing
    click(editor, confirmed(editor), 'prepare')
    editor.wait(); assert state(editor)['ready']
    editor.recording = False
    panel = state(editor)
    assert not panel['visible'] and panel['cancel_reason'] == 'workflow_context_changed'
    assert owner.undo_steps == 0 and editor.workflow == []


@pytest.mark.parametrize('prior', ['rows', 'flattened', 'invalid'])
def test_append_admission_refuses_before_processing_and_keeps_history(editing, prior):
    editor, owner = editing
    row = {'operation': 'subtract', 'parameters': {'value': 3}}
    if prior == 'rows':
        recorder = [copy.deepcopy(row) for _ in range(64)]
    elif prior == 'flattened':
        operation = ij.NativeWorkflowOperation('subtract', {'value': 3}, ij.TargetRequest())
        recorder = [json.loads(ij.NativeWorkflowProgram([operation] * 64).json())]
    else:
        recorder = [{'operation': 'unknown-prior-operation', 'parameters': {}}]
    editor.workflow = recorder
    panel = click(editor, confirmed(editor), 'prepare')
    assert panel['error'] and not panel['ready'] and not panel['working']
    assert editor.job_info is None and owner.undo_steps == 0 and editor.workflow == recorder
