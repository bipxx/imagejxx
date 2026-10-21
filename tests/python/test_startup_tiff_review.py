"""Startup reviews TIFFs while retaining one all-or-nothing attachment."""
from pathlib import Path
import shutil
import time
import imagejxx as ij
from imagejxx.ui.app import Editor
import numpy as np
import pytest
import tifffile

FIXTURE = Path(__file__).parents[1]/'fixtures/scientific-tiff-u16/little.tif'


def review(editor):
    return editor.render_headless(1)['scientific_io']


def settle(editor):
    until = time.monotonic()+10
    while editor.job_info is not None:
        assert time.monotonic()<until
        editor.wait()
    return review(editor)


def choose(editor,mode='scientific'):
    settle(editor)
    if mode == 'scientific':
        editor.action('scientific_io_draft',confirm_axes=True)
        editor.action('scientific_io_apply')
    else:
        editor.action('scientific_io_draft',accept_flat_pixels_only=True)
        editor.action('scientific_io_flat_open')
    editor.wait()


def paths(tmp_path):
    a=tmp_path/'first.tif'; b=tmp_path/'last.tiff'
    shutil.copyfile(FIXTURE,a);shutil.copyfile(FIXTURE,b)
    return a,b


@pytest.mark.parametrize('first,last',[('scientific','scientific'),('flat','scientific'),('scientific','flat')])
def test_startup_waits_for_all_choices_before_attaching(tmp_path,first,last):
    a,b=paths(tmp_path);prefs=tmp_path/'prefs.json'
    editor=Editor(preferences=prefs,images=[a,b])
    assert editor.startup_state['status']=='reviewing' and not editor.document_infos
    assert not prefs.exists()
    choose(editor,first)
    state=settle(editor)
    assert not editor.document_infos and state['startup_batch']==dict(active=True,accepted=1,tiff_count=2)
    assert not state['confirm_axes'] and not state['accept_flat_pixels_only']
    choose(editor,last)
    assert editor.startup_state['status']=='loaded' and editor.startup_state['completed']
    assert len(editor.document_infos)==2 and editor.active_index==1 and editor.workflow==[]
    for info,mode in zip(editor.document_infos,[first,last]):
        live=editor.live_document(info['id']);snapshot=live.native_snapshot()
        assert snapshot.dataset.shape==((2,3,2,3,5,1) if mode=='scientific' else (1,1,12,3,5,1))
        assert tuple(snapshot.dataset.calibration.spacing)==((.5,1.25,2.75) if mode=='scientific' else (1,1,1))
        assert not snapshot.dirty and live.native_owner.undo_steps==0


@pytest.mark.parametrize('dtype,samples',[('uint8',1),('uint8',3),('float32',1)])
def test_generic_startup_tiff_requires_explicit_flat_choice(tmp_path,dtype,samples):
    path=tmp_path/'generic.tif';pixels=np.arange(15*samples,dtype=dtype).reshape(3,5,samples)
    if samples==1:pixels=pixels[...,0]
    tifffile.imwrite(path,pixels,metadata=None,photometric='rgb' if samples==3 else 'minisblack')
    editor=Editor(preferences=tmp_path/'prefs.json',images=[path])
    state=settle(editor)
    assert state['error'] and state['open'] and not editor.document_infos
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_flat_open')
    choose(editor,'flat')
    assert editor.startup_state['status']=='loaded'
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('YXS' if samples>1 else 'YX'),pixels)


@pytest.mark.parametrize('when',['inspection','second_review','ready_read'])
def test_cancel_discards_entire_detached_startup_batch(tmp_path,when):
    a,b=paths(tmp_path);editor=Editor(preferences=tmp_path/'prefs.json',images=[a,b])
    if when!='inspection':choose(editor)
    if when=='ready_read':
        settle(editor);editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply')
        until=time.monotonic()+10
        while editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING):
            assert time.monotonic()<until;time.sleep(.002)
        assert editor.job_info.state==ij.JobState.READY
    editor.action('cancel_scientific_io');editor.wait();editor.poll()
    assert not editor.document_infos and editor.results==[] and editor.workflow==[]
    assert editor.startup_state['status']=='canceled' and editor.startup_state['completed']


@pytest.mark.parametrize('first',['scientific','flat'])
def test_accepted_input_change_refuses_final_batch(tmp_path,first):
    a,b=paths(tmp_path);editor=Editor(preferences=tmp_path/'prefs.json',images=[a,b])
    choose(editor,first);settle(editor)
    with a.open('ab') as stream:stream.write(b'changed after acceptance')
    choose(editor)
    assert not editor.document_infos and editor.workflow==[]
    assert editor.startup_state['status']=='failed' and editor.startup_state['diagnostics']


def test_startup_review_cannot_change_planned_path_or_append_other_owners(tmp_path):
    a,b=paths(tmp_path);editor=Editor(preferences=tmp_path/'prefs.json',images=[a,b]);settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',path=str(b))
    with pytest.raises((ValueError,RuntimeError)):editor.open_file(b)
    with pytest.raises((ValueError,RuntimeError)):editor.open_legacy_file(b)
    with pytest.raises((ValueError,RuntimeError)):editor.add_document(np.zeros((3,5),np.uint8))
    state=review(editor)
    assert Path(state['path'])==a and state['fixed_path'] and not state['controls']['path']['enabled']
    assert not state['controls']['browse']['enabled'] and not editor.document_infos


def test_missing_other_input_cannot_publish_accepted_tiff(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json',images=[FIXTURE,tmp_path/'missing.png'])
    choose(editor)
    assert editor.startup_state['status']=='failed' and not editor.document_infos
    assert editor.results==[] and editor.workflow==[]
    editor.start_empty_startup();assert editor.startup_state['status']=='started_empty'


@pytest.mark.parametrize('order',['first','middle','last'])
def test_mixed_startup_session_keeps_tables_order_and_activation(tmp_path,order):
    a,b=paths(tmp_path);session=tmp_path/'middle.ijx'
    source=Editor(preferences=tmp_path/'source-prefs.json')
    source.add_native_document(ij.Document(ij.Dataset(np.full((3,5),17,np.uint8),axes='YX'),'session owner'))
    source.results=[{'Value':23}];source.workflow=[{'operation':'inert_historical_action'}]
    source.action('save_workspace',path=str(session))
    requested={'first':[session,a,b],'middle':[a,session,b],'last':[a,b,session]}[order]
    editor=Editor(preferences=tmp_path/'prefs.json',images=requested);choose(editor)
    assert not editor.document_infos and editor.workflow==[] and editor.results==[]
    choose(editor)
    assert editor.startup_state['status']=='loaded' and len(editor.document_infos)==3
    assert editor.workflow==source.workflow and editor.results==source.results
    expected=[(1,1,1,3,5,1) if path==session else (2,3,2,3,5,1) for path in requested]
    actual=[editor.live_document(info['id']).native_snapshot().dataset.shape for info in editor.document_infos]
    assert actual==expected and editor.active_index==2


def test_processing_limit_change_abandons_prepared_startup(tmp_path):
    a,b=paths(tmp_path);editor=Editor(preferences=tmp_path/'prefs.json',images=[a,b])
    choose(editor);settle(editor)
    editor.update_settings(processing_memory_mb=128,persist=False);editor.poll()
    assert not editor.document_infos and editor.startup_state['status']=='canceled'


def test_generic_job_cancel_abandons_ready_startup_without_publication(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json',images=[FIXTURE]);settle(editor)
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply')
    until=time.monotonic()+10
    while editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING):
        assert time.monotonic()<until;time.sleep(.002)
    editor.cancel();editor.wait()
    assert not editor.document_infos and editor.startup_state['status']=='canceled'


def test_memory_budget_change_after_flat_choice_prevents_final_attachment(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json',images=[FIXTURE]);settle(editor)
    editor.action('scientific_io_draft',accept_flat_pixels_only=True)
    editor.action('scientific_io_flat_open')
    editor.update_settings(processing_memory_mb=128,persist=False);editor.poll()
    assert not editor.document_infos and editor.startup_state['status']=='canceled'


def test_normal_ready_read_rechecks_input_before_publication(tmp_path):
    a,_=paths(tmp_path);editor=Editor(preferences=tmp_path/'prefs.json');editor.open_file(a);settle(editor)
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply')
    until=time.monotonic()+10
    while editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING):
        assert time.monotonic()<until;time.sleep(.002)
    assert editor.job_info.state==ij.JobState.READY
    with a.open('ab') as stream:stream.write(b'changed after worker ready')
    editor.wait()
    assert not editor.document_infos and review(editor)['error'] and editor.workflow==[]
