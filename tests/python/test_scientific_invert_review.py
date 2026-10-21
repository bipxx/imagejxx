"""Scientific UInt16 inversion captures preferences and native target intent."""
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop
from imagejxx.ui.app import Editor
from test_scientific_process_recording import state, click
from test_scientific_binary_review import attach, apply_binary, parameters

REFERENCE = json.loads((Path(__file__).parent.parent / 'fixtures/conversion-imagej154r.json').read_bytes())
CASES = [r for r in REFERENCE['cases'] if r['operation'] == 'invert_uint16'
         and r['source_display_range'] is None and r['pattern'] in (0, 2)
         and r['parameters']['unsigned16_bits'] in (0, 12)]
assert len(CASES) == 8


def configure(editor, full=True, bits='12'):
    editor.update_settings(dict(conversion_full_range_inversions=full,
                                conversion_unsigned16_range=bits), persist=False)


def open_invert(editor, **options):
    editor.action('show_operation', operation='invert_uint16', parameters=options)
    return click(editor, state(editor), 'confirm')


@pytest.mark.parametrize('case', CASES)
def test_captured_conversion_preferences_match_pinned_reference(tmp_path, case):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    image = np.array(case['source'], np.uint16).reshape(case['shape'])
    pixels = np.broadcast_to(image, (2, 2, 2, *image.shape)).copy()
    owner, live, reads = attach(editor, pixels)
    live.set_view(t=1, c=1, crosshair=[0, 0, 1])
    opts = case['parameters']
    configure(editor, opts['full_range_inversions'], str(opts['unsigned16_bits']) if opts['unsigned16_bits'] else 'auto')
    before = reads.counts().plane_reads
    editor.action('show_operation', operation='invert_uint16')
    assert reads.counts().plane_reads == before
    panel = click(editor, state(editor), 'confirm')
    configure(editor, not opts['full_range_inversions'], '10')
    panel = apply_binary(editor, panel)
    assert not panel['error'] and owner.undo_steps == 1
    expected = pixels.copy(); expected[1, 1, 1] = np.array(case['pixels'], np.uint16).reshape(image.shape)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    row = editor.workflow[-1]
    assert parameters(row) == opts
    record = ij.NativeWorkflowProgram.from_json(json.dumps(row)).record()
    target = record['steps'][0]['target']
    assert target['scope'] == 2
    for axis in ('t', 'c', 'z'):
        assert target[axis] == dict(start=1, stop=2, step=1)
    editor.action('cancel_native_workflow')
    editor.action('undo'); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    editor.action('redo'); np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)


def test_explicit_inversion_parameters_override_preferences(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.broadcast_to(np.arange(10, 26, dtype=np.uint16).reshape(2, 8), (2, 2, 2, 2, 8)).copy()
    owner, _, _ = attach(editor, pixels); configure(editor)
    opts = dict(full_range_inversions=False, unsigned16_bits=0)
    apply_binary(editor, open_invert(editor, **opts))
    expected = pixels.copy(); expected[0, 0, 0] = 35 - pixels[0, 0, 0]
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), expected)
    assert parameters(editor.workflow[-1]) == opts


@pytest.mark.parametrize('dtype,samples', [(np.uint8, 1), (np.float32, 1), (np.float64, 1), (np.uint8, 3), (np.uint8, 4)])
def test_source_guards_are_metadata_only(tmp_path, dtype, samples):
    editor = Editor(preferences=tmp_path / 'prefs.json')
    pixels = np.zeros((2, 2, 2, 5, 5) + (() if samples == 1 else (samples,)), dtype)
    owner, live, reads = attach(editor, pixels, 'TCZYX' if samples == 1 else 'TCZYXS')
    before, count = live.info, reads.counts().plane_reads
    with pytest.raises(ValueError):
        editor.action('show_operation', operation='invert_uint16')
    assert live.info == before and reads.counts().plane_reads == count and owner.undo_steps == 0


@pytest.mark.parametrize('options', [dict(unsigned16_bits=9), dict(full_range_inversions=0),
                                   dict(unsigned16_bits=[12]), dict(unknown=1), dict(unsigned16_bits=12.0)])
def test_invalid_options_refuse_before_staging(tmp_path, options):
    editor = Editor(preferences=tmp_path / 'prefs.json')
    owner, _, reads = attach(editor, np.zeros((2, 2, 2, 5, 5), np.uint16))
    before = reads.counts().plane_reads
    with pytest.raises(ValueError):
        editor.action('show_operation', operation='invert_uint16', parameters=options)
    assert reads.counts().plane_reads == before and owner.undo_steps == 0 and not editor.workflow


def test_preview_cancel_preserves_owner_and_recorder(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.arange(128, dtype=np.uint16).reshape(2, 2, 2, 2, 8)
    owner, _, _ = attach(editor, pixels); configure(editor)
    click(editor, open_invert(editor), 'prepare_preview'); editor.wait()
    panel = state(editor); assert panel['ready'] and panel['preview_only']
    click(editor, panel, 'cancel')
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert not editor.workflow and owner.undo_steps == 0


def test_constant_observed_inversion_records_without_pixel_history(tmp_path):
    editor = Editor(preferences=tmp_path / 'prefs.json'); editor.recording = True
    pixels = np.full((2, 2, 2, 2, 8), 7, np.uint16)
    owner, _, _ = attach(editor, pixels); configure(editor, False, 'auto')
    apply_binary(editor, open_invert(editor))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'), pixels)
    assert owner.undo_steps == 0 and len(editor.workflow) == 1


def test_actual_edit_menu_in_both_hosts(tmp_path):
    cpp = json.loads(Path(os.environ['IMAGEJXX_SCIENTIFIC_PROCESS_CPP_REPORT']).read_bytes())
    window = _desktop.Editor(str(tmp_path / 'prefs.json'))
    python = json.loads(window._scientific_process_ui_case(str(tmp_path / 'capture001')))
    for report in (cpp, python):
        row = report['invert16']
        before = np.array([[100 * plane + j for j in range(15)] for plane in range(8)])
        before[0] = np.maximum(np.arange(15) - 3, 0); before[7] += 30
        after = before.copy(); after[7] = 4095 - before[7]
        for field, expected in [('before', before), ('after', after), ('undone', before), ('redone', after)]:
            np.testing.assert_array_equal(row[field], expected)
        assert row['undo_steps'] == 4 and row['workflow'][:-1] == row['recorder_before']
        assert parameters(row['workflow'][-1]) == dict(full_range_inversions=True, unsigned16_bits=12)
