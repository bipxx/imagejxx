"""Shared ImGui scientific Float32 file review, processing and export."""
from pathlib import Path
import copy,json,time
import imagejxx as ij
from imagejxx.ui.app import Editor
import numpy as np
import pytest
FIXTURES=Path(__file__).parents[1]/'fixtures/scientific-tiff-float32'
ROWS={r['file']:r for r in json.loads((FIXTURES/'SOURCE.json').read_bytes())['files']}

def settle(editor):
    end=time.monotonic()+15
    while editor.job_info is not None and editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING,ij.JobState.READY):
        assert time.monotonic()<end
        editor.wait()
    return editor.render_headless(1)['scientific_io']

@pytest.mark.parametrize('group,policy',[('finite','finite_only'),('quiet','preserve_quiet_nan_float32'),('ieee','preserve_ieee_float32')])
@pytest.mark.parametrize('order',['le','be'])
def test_editor_float32_file_processing_export_and_review(tmp_path,group,policy,order):
    filename=f'{group}-{order}-r2.tif';row=ROWS[filename]
    expected=bytes.fromhex(row['words_hex'])
    editor=Editor(preferences=tmp_path/'preferences.json');editor.recording=True
    editor.open_file(FIXTURES/filename)
    state=settle(editor)
    assert state['inspection']['dtype']=='float32' and state['sample_policy']=='finite_only'
    assert not editor.document_infos
    editor.action('scientific_io_draft',sample_policy=policy)
    editor.action('scientific_io_draft',confirm_axes=True)
    editor.action('scientific_io_apply');state=settle(editor)
    assert not state['open'] and not state['error']
    live=editor.live_document();owner=live.native_owner
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==expected
    assert not owner.snapshot().dirty and owner.undo_steps==0
    opening=editor.workflow[-1]
    assert opening['version']==2 and opening['sample_policy']==policy
    assert opening['reviewed_series']['dtype']=='float32'
    editor.apply_remove_nans(options=ij.RemoveNaNsCommandOptions(1),wait=True)
    filtered=owner.snapshot().dataset.numpy('TCZYX').tobytes()
    if owner.undo_steps:
        owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==expected
        owner.redo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==filtered
    path=tmp_path/'export.tif'
    editor.action('show_scientific_export',path=str(path))
    editor.action('scientific_io_inspect');state=settle(editor)
    assert state['estimate']['samples']==420 and not state['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_apply');state=settle(editor)
    assert not state['open'] and not state['error'] and path.exists()
    exported=editor.workflow[-1];assert exported['version']==2 and exported['operation']=='export' and exported['sample_policy']==policy
    editor.action('review_scientific_io',record=copy.deepcopy(opening))
    state=settle(editor)
    assert state['open'] and state['sample_policy']==policy and not state['confirm_axes']
    editor.action('cancel_scientific_io')
    second=Editor(preferences=tmp_path/'second-prefs.json');second.open_file(path);settle(second)
    second.action('scientific_io_draft',sample_policy=policy)
    second.action('scientific_io_draft',confirm_axes=True)
    second.action('scientific_io_apply');state=settle(second)
    assert not state['error']
    assert second.live_document().native_snapshot().dataset.numpy('TCZYX').tobytes()==filtered
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==filtered


def test_finite_default_refuses_nonfinite_file_without_publication(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_file(FIXTURES/'ieee-le-r2.tif');settle(editor)
    editor.action('scientific_io_draft',confirm_axes=True)
    editor.action('scientific_io_apply');state=settle(editor)
    assert state['open'] and state['error'] and not editor.document_infos
    editor.action('scientific_io_draft',sample_policy='preserve_ieee_float32')
    state=settle(editor);assert not state['confirm_axes']
    editor.action('scientific_io_draft',confirm_axes=True)
    editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and len(editor.document_infos)==1

@pytest.mark.parametrize('mutation',[{'version':1},{'sample_policy':'unknown'},{'sample_policy':True},{'version':3}])
def test_float32_record_rejects_inconsistent_policy_and_versions(tmp_path,mutation):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True
    editor.open_file(FIXTURES/'finite-le-r2.tif');settle(editor)
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');settle(editor)
    record=copy.deepcopy(editor.workflow[-1]);record.update(mutation)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1
