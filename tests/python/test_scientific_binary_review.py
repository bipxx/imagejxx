"""Scientific binary UI consumes captured settings and existing native kernels."""
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor
from test_scientific_process_recording import state, click, confirmed


REFERENCE = json.loads((Path(__file__).parent.parent / 'fixtures/binary-imagej154r.json').read_bytes())
CASES = [r for r in REFERENCE['cases'] if r['iterations'] == 2 and r['neighbors'] == 3
         and r['pad_edges'] and r['shape'] == [7, 9] and r['pattern'] == 0]
assert len(CASES) == 8


def attach(editor, pixels, axes='TCZYX'):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes=axes).observe_reads(reads))
    live = editor.add_native_document(owner)
    return owner, live, reads


def configured(editor, **changes):
    values = dict(binary_iterations=2, binary_neighbors=1,
                  binary_black_background=False, binary_pad_edges=True)
    values.update(changes)
    editor.update_settings(values, persist=False)


def open_binary(editor, operation='dilate', **parameters):
    editor.action('show_operation', operation='binary_' + operation, parameters=parameters)
    return click(editor, state(editor), 'confirm')


def apply_binary(editor, panel):
    click(editor, panel, 'prepare'); editor.wait()
    panel = state(editor)
    assert panel['ready'] and not panel['error']
    return click(editor, panel, 'apply')


def parameters(row):
    verified = ij.NativeWorkflowProgram.from_json(json.dumps(row)).record()
    return {k: v['value'] for k, v in verified['steps'][0]['parameters'].items()}


@pytest.mark.parametrize('case', CASES, ids=lambda r: r['operation'] + '-black' + str(int(r['black_background'])))
def test_binary_settings_reach_native_scientific_targets_and_pinned_pixels(tmp_path, case):
    editor = Editor(preferences=tmp_path / 'preferences.json'); editor.recording = True
    image = np.array(case['source'], np.uint8).reshape(case['shape'])
    pixels = np.broadcast_to(image, (2, 2, 2, *image.shape)).copy()
    owner, live, reads = attach(editor, pixels)
    live.set_view(t=1, c=1, crosshair=[0, 0, 1])
    options = {k: case[k] for k in ('iterations', 'neighbors', 'black_background', 'pad_edges')}
    configured(editor, **{'binary_' + k: v for k, v in options.items()})
    before = reads.counts().plane_reads
    editor.action('show_operation', operation='binary_' + case['operation'])
    assert reads.counts().plane_reads == before
    panel = click(editor, state(editor), 'confirm')
    # Later preferences must not change the captured dialog options.
    configured(editor, binary_iterations=1, binary_neighbors=8,
               binary_black_background=not options['black_background'], binary_pad_edges=False)
    panel = apply_binary(editor, panel)
    assert not panel['error'] and owner.undo_steps == 1
    expected = pixels.copy(); expected[1, 1, 1] = np.array(case['pixels'], np.uint8).reshape(image.shape)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    assert parameters(editor.workflow[-1]) == options
    editor.action('cancel_native_workflow')
    editor.action('undo'); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    editor.action('redo'); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)


def test_explicit_parameters_override_preferences(tmp_path):
    editor = Editor(preferences=tmp_path / 'preferences.json'); editor.recording = True
    pixels = np.zeros((2, 2, 2, 5, 5), np.uint8); pixels[..., 2, 2] = 255
    owner, _, _ = attach(editor, pixels); configured(editor)
    options = dict(iterations=1, neighbors=1, black_background=True, pad_edges=False)
    apply_binary(editor, open_binary(editor, **options))
    expected = pixels.copy(); expected[0, 0, 0, 1:4, 1:4] = 255
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    assert parameters(editor.workflow[-1]) == options


@pytest.mark.parametrize('dtype,samples', [(np.uint16, 1), (np.float32, 1), (np.float64, 1), (np.uint8, 3), (np.uint8, 4)])
def test_binary_source_guards_do_not_read_pixels_or_mutate(tmp_path, dtype, samples):
    editor = Editor(preferences=tmp_path / 'preferences.json')
    pixels = np.zeros((2, 2, 2, 5, 5) + (() if samples == 1 else (samples,)), dtype)
    owner, live, reads = attach(editor, pixels, 'TCZYX' if samples == 1 else 'TCZYXS')
    before, count = live.info, reads.counts().plane_reads
    with pytest.raises(ValueError):
        editor.action('show_operation', operation='binary_erode')
    assert live.info == before and owner.undo_steps == 0 and reads.counts().plane_reads == count


@pytest.mark.parametrize('parameters', [{'iterations': 0}, {'neighbors': 9}, {'black_background': 0}, {'iterations': [2, 3]}, {'unknown': 1}])
def test_invalid_binary_parameters_refuse_without_staging(tmp_path, parameters):
    editor = Editor(preferences=tmp_path / 'preferences.json')
    owner, _, reads = attach(editor, np.zeros((2, 2, 2, 5, 5), np.uint8))
    count = reads.counts().plane_reads
    with pytest.raises(ValueError):
        editor.action('show_operation', operation='binary_open', parameters=parameters)
    assert reads.counts().plane_reads == count and owner.undo_steps == 0 and not editor.workflow


def test_nonbinary_pixels_fail_preparation_atomically(tmp_path):
    editor = Editor(preferences=tmp_path / 'preferences.json'); editor.recording = True
    pixels = np.full((2, 2, 2, 5, 5), 7, np.uint8)
    owner, _, _ = attach(editor, pixels)
    click(editor, open_binary(editor), 'prepare'); editor.wait()
    panel = state(editor)
    assert '0/255' in panel['error'] and not panel['ready']
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.undo_steps == 0 and not editor.workflow


def test_binary_preview_cancel_does_not_record(tmp_path):
    editor = Editor(preferences=tmp_path / 'preferences.json'); editor.recording = True
    pixels = np.zeros((2, 2, 2, 5, 5), np.uint8); pixels[..., 2, 2] = 255
    owner, _, _ = attach(editor, pixels)
    click(editor, open_binary(editor), 'prepare_preview'); editor.wait()
    panel = state(editor); assert panel['ready'] and panel['preview_only']
    click(editor, panel, 'cancel')
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.undo_steps == 0 and not editor.workflow


def test_successful_noop_records_intent_without_pixel_history(tmp_path):
    editor = Editor(preferences=tmp_path / 'preferences.json'); editor.recording = True
    pixels = np.full((2, 2, 2, 5, 5), 255, np.uint8)
    owner, _, _ = attach(editor, pixels); configured(editor)
    panel = apply_binary(editor, open_binary(editor, 'erode'))
    assert not panel['error'] and owner.undo_steps == 0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert len(editor.workflow) == 1
    assert parameters(editor.workflow[0]) == dict(iterations=2, neighbors=1, black_background=False, pad_edges=True)


def test_actual_binary_menu_settings_capture_and_history_in_both_hosts(tmp_path):
    cpp = json.loads(Path(os.environ['IMAGEJXX_SCIENTIFIC_PROCESS_CPP_REPORT']).read_bytes())
    window = _desktop.Editor(str(tmp_path / 'preferences.json'))
    python = json.loads(window._scientific_process_ui_case(str(tmp_path / 'capture001')))
    for report in (cpp, python):
        binary = report['binary']
        before = np.full((8, 25), 255); before[:, 12] = 0
        after = before.copy(); after[7] = 0
        np.testing.assert_array_equal(binary['before'], before)
        np.testing.assert_array_equal(binary['after'], after)
        np.testing.assert_array_equal(binary['undone'], before)
        np.testing.assert_array_equal(binary['redone'], after)
        assert binary['undo_steps'] == 1 and binary['workflow'][:-1] == binary['recorder_before']
        assert parameters(binary['workflow'][-1]) == dict(iterations=2, neighbors=1, black_background=False, pad_edges=True)
        assert {'Scientific Dilate menu', 'Prepare binary preview', 'Apply binary edit'} <= {c['name'] for c in report['controls']}
