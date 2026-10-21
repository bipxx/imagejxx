"""Scientific shadows use native review, captured targets and pinned ImageJ pixels."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from test_scientific_process_recording import state
from test_shadow_ui import DIRECTIONS, REFERENCE


def fixture(profile, direction):
    return next(c for c in REFERENCE['cases'] if c['type'] == profile and c['direction'] == direction
                and c['width'] == 7 and c['height'] == 5 and c['pattern'] == 1 and not c['rectangle'])


def attach(editor, profile, direction='north'):
    row = fixture(profile, direction)
    dtype = {'byte': np.uint8, 'short': np.uint16, 'float': np.float32, 'rgb': np.uint8}[profile]
    shape = (5, 7) + ((3,) if profile == 'rgb' else ())
    plane = np.array(row['source'], dtype=dtype).reshape(shape)
    pixels = np.broadcast_to(plane, (2, 2, 3, *shape)).copy()
    observer = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYXS' if profile == 'rgb' else 'TCZYX').observe_reads(observer))
    live = editor.add_native_document(owner); live.set_view(t=1, c=1, crosshair=[3, 2, 1])
    return owner, observer, pixels, np.array(row['output'], dtype=dtype).reshape(shape)


def mouse(control):
    assert control['enabled'] and control['visible']
    return [dict(type='position', space='screen', point=control['point'], frames=2),
            dict(type='button', name='left', down=True, frames=1),
            dict(type='button', name='left', down=False, frames=2)]


def click(editor, panel, name):
    return editor.render_headless(frames=3, events=mouse(panel['controls'][name]))['native_workflow']


def choose(editor, name, index):
    panel = state(editor)
    opened = click(editor, panel, name)
    # Each render_headless call owns its ImGui context. Reopen the measured
    # combo and choose its measured option within the same input sequence.
    events = mouse(panel['controls'][name]) + mouse(opened['controls'][name+'.'+str(index)])
    return editor.render_headless(frames=3, events=events)['native_workflow']


def number(editor, name, value):
    events = mouse(state(editor)['controls'][name]) + [
        dict(type='keys', keys=[dict(name='Ctrl', down=True), dict(name='A', down=True)], frames=1),
        dict(type='keys', keys=[dict(name='A', down=False), dict(name='Ctrl', down=False)], frames=1),
        dict(type='text', value=str(value), frames=1),
        dict(type='keys', keys=[dict(name='Enter', down=True)], frames=1),
        dict(type='keys', keys=[dict(name='Enter', down=False)], frames=1),
    ]
    editor.render_headless(frames=3, events=events)


@pytest.mark.parametrize('profile', ['byte', 'short', 'float', 'rgb'])
@pytest.mark.parametrize('direction', DIRECTIONS)
@pytest.mark.parametrize('scope', [0, 1, 2], ids=['current', 'all-z', 'ranges'])
def test_scientific_shadow_targets_preview_and_atomic_history(tmp_path, profile, direction, scope):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.recording = True
    owner, reads, pixels, filtered = attach(editor, profile, direction)
    original = owner.snapshot(); count = reads.counts().plane_reads
    editor.action('show_operation', operation='shadow_imagej', parameters={'direction': direction})
    assert reads.counts().plane_reads == count and owner.snapshot().shares_data(original)
    choose(editor, 'scope', scope); choose(editor, 'region', 0)
    expected = pixels.copy()
    if scope == 0:
        expected[1, 1, 1] = filtered
        ranges = [(1, 2, 1), (1, 2, 1), (1, 2, 1)]
    elif scope == 1:
        expected[1, 1, :] = filtered
        ranges = [(1, 2, 1), (1, 2, 1), (0, 3, 1)]
    else:
        ranges = [(0, 1, 1), (1, 2, 1), (0, 3, 2)]
        for axis, (start, stop, step) in enumerate(ranges):
            for field, value in [('start', start), ('stop', stop), ('stride', step)]:
                number(editor, field+'.'+str(axis), value)
        expected[0, 1, ::2] = filtered
    click(editor, state(editor), 'confirm')
    click(editor, state(editor), 'prepare_preview'); editor.wait(); panel = state(editor)
    assert panel['ready'] and panel['preview_only'] and not panel['error']
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0 and editor.workflow == []
    click(editor, panel, 'prepare'); editor.wait(); panel = state(editor)
    assert panel['ready'] and not panel['error'] and owner.snapshot().shares_data(original)
    click(editor, panel, 'apply'); editor.wait(); assert not state(editor)['error']
    axes = 'TCZYXS' if profile == 'rgb' else 'TCZYX'
    assert owner.undo_steps == 1
    assert owner.snapshot().dataset.numpy(axes).tobytes() == expected.tobytes()
    row = ij.NativeWorkflowProgram.from_json(json.dumps(editor.workflow[-1])).record()['steps'][0]
    assert row['operation'] == 'shadow_imagej' and row['parameters'] == {'direction': {'kind': 'text', 'value': direction}}
    assert row['target']['scope'] == 2 and row['target']['roi'] == 0
    for name, (start, stop, step) in zip(('t', 'c', 'z'), ranges):
        assert row['target'][name] == dict(start=start, stop=stop, step=step)
    editor.action('cancel_native_workflow'); editor.action('undo')
    assert owner.snapshot().dataset.numpy(axes).tobytes() == pixels.tobytes()
    editor.action('redo'); assert owner.snapshot().dataset.numpy(axes).tobytes() == expected.tobytes()


@pytest.mark.parametrize('dtype,samples,policy', [
    (np.float64, 1, ij.FloatingSamplePolicy.FINITE_ONLY),
    (np.uint8, 4, ij.FloatingSamplePolicy.FINITE_ONLY),
    (np.uint16, 3, ij.FloatingSamplePolicy.FINITE_ONLY),
    (np.float32, 3, ij.FloatingSamplePolicy.FINITE_ONLY),
    (np.float32, 1, ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32),
    (np.float32, 1, ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32),
])
def test_scientific_shadow_source_guard_is_pixel_free(tmp_path, dtype, samples, policy):
    editor = Editor(preferences=tmp_path/'prefs.json')
    pixels = np.zeros((2, 2, 2, 5, 7) + (() if samples == 1 else (samples,)), dtype)
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYX' if samples == 1 else 'TCZYXS', sample_policy=policy).observe_reads(reads))
    editor.add_native_document(owner); before = owner.snapshot(); count = reads.counts().plane_reads
    with pytest.raises(ValueError): editor.action('show_operation', operation='shadow_imagej')
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0 and reads.counts().plane_reads == count


def check_scientific_shadow(report, direction):
    assert report['contract'] == 'p09k.scientific-shadows.ui.001' and report['state'] == 'observed'
    assert report['case_id'] == direction and not report['acceptance_claim'] and not report['final_error']
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    names = [c['name'] for c in report['controls']]
    for name in ['menu.command.shadow_imagej', 'review.edit_parameters', 'editor.direction',
                 'editor.direction.choice.'+str(DIRECTIONS.index(direction)), 'editor.keep_parameters',
                 'review.prepare_preview', 'review.view_preview', 'viewport.close', 'review.scope.1',
                 'review.prepare', 'review.apply', 'menu.Edit.Undo', 'menu.Edit.Redo']:
        assert name in names
    row = fixture('byte', direction); source = np.array(row['source'], np.uint8).reshape(5, 7)
    filtered = np.array(row['output'], np.uint8).reshape(5, 7)
    original = np.broadcast_to(source, (2, 2, 3, 5, 7)).copy()
    states = report['states']; before = states['before']
    assert states['preview'] == states['canceled'] == states['prepared'] == before
    np.testing.assert_array_equal(np.array(before['pixels']).reshape(original.shape), original)
    expected = original.copy(); expected[1, 1, :] = filtered
    for label in ['applied', 'redone']:
        assert states[label]['undo'] == 1
        np.testing.assert_array_equal(np.array(states[label]['pixels']).reshape(expected.shape), expected)
    assert states['undone']['undo'] == 0
    np.testing.assert_array_equal(np.array(states['undone']['pixels']).reshape(original.shape), original)
    for item in states.values():
        for field in ['selection', 'previous', 'roi', 'attributes', 't', 'c', 'crosshair']:
            assert item[field] == before[field]
    record = states['applied']['workflow']; assert len(record) == 2 and record[0] == before['workflow'][0]
    step = ij.NativeWorkflowProgram.from_json(json.dumps(record[1])).record()['steps'][0]
    assert step['operation'] == 'shadow_imagej' and step['parameters']['direction']['value'] == direction
    assert step['target']['t'] == step['target']['c'] == dict(start=1, stop=2, step=1)
    assert step['target']['z'] == dict(start=0, stop=3, step=1)
    preview = original.copy(); preview[1, 1, 1] = filtered
    for plane in report['preview_viewport']['planes']:
        axis, coordinate = plane['plane'], plane['coordinate']
        volume = preview[1, 1]
        wanted = volume[coordinate] if axis == 'XY' else volume[:, coordinate, :] if axis == 'XZ' else volume[:, :, coordinate]
        assert plane['readback']['available']
        np.testing.assert_array_equal(plane['readback']['source']['pixels'], wanted.ravel())


@pytest.mark.parametrize('direction', DIRECTIONS)
@pytest.mark.parametrize('context', ['imgui_cpp', 'imgui_python'])
def test_actual_scientific_shadow_review_both_hosts(tmp_path, direction, context):
    if context == 'imgui_cpp':
        report = json.loads((Path(os.environ['IMAGEJXX_SCIENTIFIC_SHADOW_CPP_BASE'])/direction/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._scientific_shadow_ui_case(direction, str(tmp_path/'capture')))
    assert report['context'] == context
    check_scientific_shadow(report, direction)


@pytest.mark.parametrize('preparation', ['prepare_preview', 'prepare'])
def test_scientific_shadow_nonfinite_output_is_visible_and_atomic(tmp_path, preparation):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.recording = True
    plane = np.array([[1e38, 1e38, 1e38], [0, 0, 0], [-1e38, -1e38, -1e38]], np.float32)
    pixels = np.broadcast_to(plane, (2, 2, 2, 3, 3)).copy()
    owner = ij.Document(ij.Dataset(pixels, axes='TCZYX')); editor.add_native_document(owner)
    before = owner.snapshot()
    editor.action('show_operation', operation='shadow_imagej')
    click(editor, state(editor), 'confirm'); click(editor, state(editor), preparation)
    editor.wait(); panel = state(editor)
    assert 'output is not finite' in panel['error'] and not panel['ready'] and not panel['working']
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0 and editor.workflow == []
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes() == pixels.tobytes()
    click(editor, panel, 'cancel'); assert not state(editor)['visible']
