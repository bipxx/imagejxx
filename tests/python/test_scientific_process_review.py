"""Process menus seed a private scientific review without running the recorder."""
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor


@pytest.fixture
def editor(tmp_path):
    return Editor(preferences=tmp_path / 'preferences.json')


def attach(editor, dtype=np.uint16):
    pixels = np.arange(120, dtype=dtype).reshape(2, 3, 4, 5)
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes='CZYX').observe_reads(reads))
    live = editor.add_native_document(owner)
    return owner, live, reads


@pytest.mark.parametrize('operation,parameters,title', [
    ('add', {'value': 7}, 'Add'), ('invert', {}, 'Invert'),
    ('smooth', {'radius': 2, 'border': 'nearest'}, 'Smooth'),
    ('sharpen', {}, 'Sharpen'), ('divide', {'value': 2}, 'Divide'),
])
@pytest.mark.parametrize('dtype', [np.uint8, np.uint16, np.float32])
def test_seed_is_private_pixel_free_and_keeps_recorder(editor, operation, parameters, title, dtype):
    owner, live, reads = attach(editor, dtype)
    recorder = [{'operation': 'subtract', 'parameters': {'value': 3}, 'all_slices': False}]
    editor.workflow = recorder
    editor.recording = True
    before = live.info
    count = reads.counts().plane_reads
    editor.action('show_operation', operation=operation, parameters=parameters)
    assert reads.counts().plane_reads == count
    assert live.info == before and owner.undo_steps == 0 and editor.workflow == recorder
    panel = editor.render_headless(3)['native_workflow']
    assert panel['visible'] and panel['error'] == '' and not panel['working']
    assert [e['title'] for e in panel['entries']] == [title]
    assert not panel['entries'][0]['confirmed']
    assert not panel['ready']
    editor.action('cancel_native_workflow')
    assert owner.undo_steps == 0 and editor.workflow == recorder


@pytest.mark.parametrize('operation,parameters', [
    ('add', {'unknown': 2}), ('divide', {'value': 0}),
    ('smooth', {'radius': -1}), ('smooth', {'border': 'invalid'}),
])
def test_invalid_seed_preserves_existing_review(editor, operation, parameters):
    owner, _, reads = attach(editor)
    editor.action('show_operation', operation='add', parameters={'value': 8})
    count = reads.counts().plane_reads
    with pytest.raises((ValueError, RuntimeError)):
        editor.action('show_operation', operation=operation, parameters=parameters)
    assert reads.counts().plane_reads == count and owner.undo_steps == 0
    panel = editor.render_headless(3)['native_workflow']
    assert panel['visible'] and panel['entries'][0]['title'] == 'Add'


def test_recorder_mutation_invalidates_seed_without_publication(editor):
    owner, _, _ = attach(editor)
    editor.action('show_operation', operation='add')
    editor.workflow = [{'operation': 'invert', 'parameters': {}}]
    panel = editor.render_headless(3)['native_workflow']
    assert not panel['visible'] and panel['cancel_reason'] == 'workflow_context_changed'
    assert owner.undo_steps == 0


@pytest.mark.parametrize('operation', [
    'math', 'subtract', 'multiply', 'absolute', 'square', 'sqrt', 'log',
    'log1p', 'exp', 'gamma', 'clip', 'convolve', 'gaussian_blur', 'rank_filter',
    'median_filter', 'minimum_filter', 'maximum_filter', 'variance_filter',
    'unsharp_mask', 'remove_outliers',
])
def test_other_eligible_catalog_defaults_open_review(editor, operation):
    owner, _, reads = attach(editor)
    before = reads.counts().plane_reads
    editor.action('show_operation', operation=operation)
    assert reads.counts().plane_reads == before and owner.undo_steps == 0
    panel = editor.render_headless(3)['native_workflow']
    assert panel['visible'] and panel['error'] == ''
    assert len(panel['entries']) == 1 and not panel['entries'][0]['confirmed']
    editor.action('cancel_native_workflow')


def test_inactive_scientific_owner_and_geometry_remain_guarded(editor):
    owner, first, _ = attach(editor)
    attach(editor)
    with pytest.raises(ValueError):
        editor.action('show_operation', operation='add', document_id=first.id)
    with pytest.raises(ValueError):
        editor.action('show_operation', operation='resize')
    assert owner.undo_steps == 0


def test_actual_process_menus_preview_apply_history_and_cancel_in_both_hosts(tmp_path):
    cpp = json.loads(Path(os.environ['IMAGEJXX_SCIENTIFIC_PROCESS_CPP_REPORT']).read_bytes())
    native = _desktop.Editor(str(tmp_path / 'preferences.json'))
    python = json.loads(native._scientific_process_ui_case(str(tmp_path / 'capture001')))
    assert python == json.loads((tmp_path / 'capture001/case.json').read_bytes())
    expected = np.arange(15)[None, :] + 100 * np.arange(8)[:, None]
    applied = expected.copy(); applied[7] += 10  # DesktopUI default, captured T=C=Z=1.
    for report in (cpp, python):
        assert report['state'] == 'observed'
        np.testing.assert_array_equal(report['before'], expected)
        np.testing.assert_array_equal(report['after'], applied)
        np.testing.assert_array_equal(report['undone'], expected)
        np.testing.assert_array_equal(report['redone'], applied)
        assert report['undo_steps'] == 1
        assert report['recorder'] == [{'operation': 'subtract', 'parameters': {'value': 3}, 'all_slices': False}]
        assert report['filter']['entries'][0]['title'] == 'Smooth'
        assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        assert {'Add menu', 'Smooth menu', 'Prepare preview', 'Apply operation', 'Cancel filter review'} <= {c['name'] for c in report['controls']}
