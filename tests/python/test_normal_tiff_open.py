"""Interactive TIFF opening must preserve review and explicit flat-pixel consent."""
from pathlib import Path
import time

import imagejxx as ij
from imagejxx.ui.app import Editor
import numpy as np
import pytest
import tifffile

FIXTURE = Path(__file__).parents[1] / 'fixtures/scientific-tiff-u16/little.tif'


def state(editor):
    return editor.render_headless(1)['scientific_io']


def settle(editor):
    deadline = time.monotonic() + 10
    while editor.job_info is not None and editor.job_info.state in (
            ij.JobState.QUEUED, ij.JobState.RUNNING, ij.JobState.READY):
        assert time.monotonic() < deadline
        editor.wait()
    return state(editor)


def scientific(editor):
    settle(editor)
    editor.action('scientific_io_draft', confirm_axes=True)
    editor.action('scientific_io_apply')
    editor.wait()


def flat(editor):
    settle(editor)
    editor.action('scientific_io_draft', accept_flat_pixels_only=True)
    editor.action('scientific_io_flat_open')


def test_normal_open_preserves_axes_calibration_and_clean_owner(tmp_path):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.recording = True
    editor.open_file(FIXTURE)
    review = settle(editor)
    assert review['open'] and not review['confirm_axes'] and not editor.document_infos
    with pytest.raises((ValueError, RuntimeError)):
        editor.action('scientific_io_apply')
    scientific(editor)
    live = editor.live_document(); snapshot = live.native_snapshot()
    assert snapshot.dataset.shape == (2,3,2,3,5,1)
    assert tuple(snapshot.dataset.calibration.spacing) == (.5,1.25,2.75)
    assert not snapshot.dirty and live.native_owner.undo_steps == 0
    assert len(editor.workflow) == 1 and editor.workflow[0]['operation'] == 'open'


@pytest.mark.parametrize('dtype,samples', [('uint8',1),('uint8',3),('float32',1)])
def test_generic_tiff_never_automatically_falls_back(tmp_path,dtype,samples):
    pixels = np.arange(15*samples,dtype=dtype).reshape(3,5,samples)
    if samples == 1: pixels = pixels[...,0]
    path = tmp_path/'generic.TIFF'
    tifffile.imwrite(path,pixels,metadata=None,photometric='rgb' if samples==3 else 'minisblack')
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.open_file(path)
    review = settle(editor)
    assert review['open'] and review['error'] and not editor.document_infos
    with pytest.raises((ValueError,RuntimeError)):
        editor.action('scientific_io_flat_open')
    flat(editor)
    snap = editor.live_document().native_snapshot()
    np.testing.assert_array_equal(snap.dataset.numpy('YXS') if samples>1 else snap.dataset.numpy('YX'),pixels)
    assert tuple(snap.dataset.calibration.spacing) == (1,1,1)
    assert not snap.dirty and editor.workflow == []


@pytest.mark.parametrize('change',['path','limits','inspect'])
def test_flat_consent_invalidated_on_review_change(tmp_path,change):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.open_file(FIXTURE); settle(editor)
    editor.action('scientific_io_draft',accept_flat_pixels_only=True)
    if change == 'path': editor.action('scientific_io_draft',path=str(tmp_path/'other.tif'))
    elif change == 'limits': editor.action('scientific_io_draft',limits={'max_ifds':100})
    else: editor.action('scientific_io_inspect'); settle(editor)
    assert not state(editor)['accept_flat_pixels_only']
    with pytest.raises((ValueError,RuntimeError)):
        editor.action('scientific_io_flat_open')
    assert not editor.document_infos


def test_mixed_queue_keeps_order_and_fresh_tiff_consent(tmp_path):
    png = tmp_path/'middle.png'; ij.imwrite(png,np.full((3,5),7,dtype=np.uint8))
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor.open_file(FIXTURE); editor.open_file(png); editor.open_file(FIXTURE)
    assert len(state(editor)['queued_paths']) == 2
    scientific(editor)
    # First publication can dispatch PNG immediately; the next poll starts TIFF review.
    editor.poll(); review = settle(editor)
    assert len(editor.document_infos) == 2 and review['open']
    assert not review['confirm_axes'] and not review['accept_flat_pixels_only']
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('YX'),7)
    flat(editor)
    assert len(editor.document_infos) == 3
    assert editor.live_document().native_snapshot().dataset.shape == (1,1,12,3,5,1)


def test_queue_paths_are_frozen_against_cwd_changes(tmp_path,monkeypatch):
    import shutil
    shutil.copyfile(FIXTURE,tmp_path/'relative.tif')
    elsewhere = tmp_path/'elsewhere'; elsewhere.mkdir()
    monkeypatch.chdir(tmp_path)
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.open_file('relative.tif'); editor.open_file('relative.tif')
    monkeypatch.chdir(elsewhere)
    scientific(editor); review = settle(editor)
    assert Path(review['path']) == tmp_path/'relative.tif'
    scientific(editor)
    assert len(editor.document_infos) == 2


def test_cancel_clears_waiting_files_without_publication(tmp_path):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.open_file(FIXTURE); editor.open_file(FIXTURE)
    editor.action('cancel_scientific_io'); editor.wait(); editor.poll()
    review = state(editor)
    assert not editor.document_infos and not review['open'] and review['queued_paths'] == []


def test_failed_flat_decode_preserves_owner_and_review(tmp_path):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.open_legacy_file(FIXTURE)
    before = editor.live_document().native_snapshot(); ids = [d['id'] for d in editor.document_infos]
    broken = tmp_path/'broken.tif'; broken.write_bytes(b'not a TIFF')
    editor.open_file(broken); settle(editor)
    editor.action('scientific_io_draft',accept_flat_pixels_only=True)
    with pytest.raises((ValueError,RuntimeError)):
        editor.action('scientific_io_flat_open')
    assert state(editor)['open'] and [d['id'] for d in editor.document_infos] == ids
    assert editor.live_document().native_snapshot().shares_data(before)


@pytest.mark.parametrize('bad',['bad\x00.tif','x'*131065],ids=['nul','oversize'])
def test_invalid_open_path_leaves_review_intact(tmp_path,bad):
    editor = Editor(preferences=tmp_path/'prefs.json'); editor.open_file(FIXTURE); settle(editor)
    before = state(editor)
    with pytest.raises((ValueError,RuntimeError)):
        editor.open_file(bad)
    after = state(editor)
    assert after['path'] == before['path'] and after['generation'] == before['generation']
    assert after['queued_paths'] == []


def test_mixed_launch_pauses_at_tiff_review(tmp_path,monkeypatch):
    from imagejxx.ui import launch
    monkeypatch.setattr(Editor,'run',lambda self,**kwargs: None)
    pixels = np.full((3,5),11,dtype=np.uint8)
    editor = launch([FIXTURE,pixels],preferences=tmp_path/'prefs.json')
    assert not editor.document_infos and state(editor)['open']
    scientific(editor)
    first = editor.live_document(editor.document_infos[0]['id']).native_snapshot()
    assert first.dataset.shape == (2,3,2,3,5,1) and len(editor.document_infos) == 2
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('YX'),pixels)
