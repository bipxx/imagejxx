"""Mixed launch captures Python inputs and pauses ordered C++ append at TIFFs."""
from pathlib import Path
import gc
import shutil
import time
import weakref

import imagejxx as ij
from imagejxx.ui import launch
from imagejxx.ui.app import Editor
from imagejxx.ui.model import Document, Selection
from imagejxx.ui.persistence import serialize_document
import numpy as np
import pytest
import tifffile

FIXTURE=Path(__file__).parents[1]/'fixtures/scientific-tiff-u16/little.tif'


@pytest.fixture(autouse=True)
def no_blocking_loop(monkeypatch):
    monkeypatch.setattr(Editor,'run',lambda self,**kwargs: None)


def settle(editor):
    until=time.monotonic()+10
    while editor.job_info is not None:
        assert time.monotonic()<until;editor.wait()
    return editor.render_headless(1)['scientific_io']


def choose(editor,mode='scientific'):
    settle(editor)
    editor.action('scientific_io_draft',**({'confirm_axes':True} if mode=='scientific' else {'accept_flat_pixels_only':True}))
    editor.action('scientific_io_apply' if mode=='scientific' else 'scientific_io_flat_open')
    editor.wait()


def values(editor):
    return [editor.live_document(info['id']).native_snapshot().dataset for info in editor.document_infos]


@pytest.mark.parametrize('order',['before','after','between'])
@pytest.mark.parametrize('mode',['scientific','flat'])
def test_launch_appends_in_order_and_needs_fresh_consent(tmp_path,order,mode):
    a=np.full((3,5),11,np.uint8);b=Document(np.full((3,5),23,np.uint16),'queued document')
    requested={'before':[a,FIXTURE,b],'after':[FIXTURE,a,b],'between':[a,FIXTURE,b,FIXTURE,a]}[order]
    editor=launch(requested,preferences=tmp_path/'prefs.json')
    expected_prefix=0 if order=='after' else 1
    assert len(editor.document_infos)==expected_prefix and editor.launch_state['status']=='reviewing'
    assert editor.launch_state['total_items']==len(requested)
    choose(editor,mode)
    if order=='between':
        assert len(editor.document_infos)==3 and editor.launch_state['completed_items']==3
        state=settle(editor)
        assert state['fixed_path'] and not state['confirm_axes'] and not state['accept_flat_pixels_only']
        choose(editor,mode)
    assert editor.launch_state==dict(status='completed',total_items=len(requested),completed_items=len(requested),remaining_items=0,reviewing=False,error='')
    assert len(editor.document_infos)==len(requested) and editor.active_index==len(requested)-1
    for item,dataset in zip(requested,values(editor)):
        if isinstance(item,Path):assert dataset.shape==((2,3,2,3,5,1) if mode=='scientific' else (1,1,12,3,5,1))
        else:np.testing.assert_array_equal(dataset.numpy('YX'),a if item is a else b.data)
    assert editor.workflow==[]


def test_pending_images_are_owned_without_python_queue_callbacks(tmp_path):
    pixels=np.full((3,5),19,np.uint8);reference=weakref.ref(pixels)
    editor=launch([FIXTURE,pixels],preferences=tmp_path/'prefs.json')
    pixels[:]=99;del pixels;gc.collect()
    assert reference() is None and not editor.document_infos
    choose(editor)
    np.testing.assert_array_equal(values(editor)[1].numpy('YX'),19)


def test_pending_document_metadata_selection_and_calibration_are_detached(tmp_path):
    source=Document(np.full((3,5),17,np.uint16),'captured title')
    source.calibration.pixel_width=.5;source.calibration.unit='um'
    source.selection=Selection('rectangle',[(0,0),(2,2)])
    editor=launch([FIXTURE,source],preferences=tmp_path/'prefs.json')
    source.title='later title';source.calibration.pixel_width=3;source.selection=None
    choose(editor)
    opened=editor.document_snapshot(editor.document_infos[1]['id'])
    assert opened.title=='captured title' and opened.calibration.pixel_width==.5 and opened.calibration.unit=='um'
    assert opened.selection.to_dict()==Selection('rectangle',[(0,0),(2,2)]).to_dict()
    np.testing.assert_array_equal(opened.data,17)


@pytest.mark.parametrize('when',['first_review','second_review','ready_job'])
def test_cancel_keeps_accepted_prefix_and_discards_remaining_items(tmp_path,when):
    a=np.ones((3,5),np.uint8)
    editor=launch([a,FIXTURE,a,FIXTURE,a],preferences=tmp_path/'prefs.json')
    if when=='second_review':choose(editor)
    if when=='ready_job':
        settle(editor);editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply')
        until=time.monotonic()+10
        while editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING):
            assert time.monotonic()<until;time.sleep(.002)
        editor.cancel()
    else:editor.action('cancel_scientific_io')
    editor.wait();editor.poll()
    expected=3 if when=='second_review' else 1
    assert len(editor.document_infos)==expected and editor.launch_state['status']=='canceled'
    assert editor.launch_state['completed_items']==expected and editor.launch_state['remaining_items']==0
    assert editor.live_document().native_snapshot().dataset.shape==((1,1,1,3,5,1))


@pytest.mark.parametrize('dtype,samples',[('uint8',1),('uint8',3),('float32',1)])
def test_generic_tiff_does_not_fall_back_or_append_later_images(tmp_path,dtype,samples):
    path=tmp_path/'generic.tif';pixels=np.arange(15*samples,dtype=dtype).reshape(3,5,samples)
    if samples==1:pixels=pixels[...,0]
    tifffile.imwrite(path,pixels,metadata=None,photometric='rgb' if samples==3 else 'minisblack')
    editor=launch([np.ones((3,5),np.uint8),path,np.zeros((3,5),np.uint8)],preferences=tmp_path/'prefs.json')
    state=settle(editor)
    assert state['error'] and len(editor.document_infos)==1 and editor.launch_state['status']=='reviewing'
    choose(editor,'flat')
    assert len(editor.document_infos)==3 and editor.launch_state['status']=='completed'
    np.testing.assert_array_equal(values(editor)[1].numpy('YXS' if samples>1 else 'YX'),pixels)


@pytest.mark.parametrize('after_review',[False,True])
def test_file_failure_stops_append_without_losing_prefix(tmp_path,after_review):
    requested=[np.ones((3,5),np.uint8)]
    if after_review:requested.append(FIXTURE)
    requested.extend([tmp_path/'missing.png',np.zeros((3,5),np.uint8)])
    editor=launch(requested,preferences=tmp_path/'prefs.json')
    if after_review:
        with pytest.raises(RuntimeError,match='Could not open'):choose(editor)
    else:assert editor.error
    assert editor.launch_state['status']=='failed' and editor.launch_state['error']
    assert len(editor.document_infos)==(2 if after_review else 1) and editor.launch_state['remaining_items']==0


def test_queued_relative_paths_are_resolved_at_launch_admission(tmp_path,monkeypatch):
    shutil.copyfile(FIXTURE,tmp_path/'input.tif');other=tmp_path/'elsewhere';other.mkdir();monkeypatch.chdir(tmp_path)
    editor=launch([np.zeros((3,5),np.uint8),'input.tif',np.ones((3,5),np.uint8)],preferences=tmp_path/'prefs.json')
    monkeypatch.chdir(other)
    assert Path(settle(editor)['path'])==tmp_path/'input.tif'
    choose(editor)
    assert editor.launch_state['status']=='completed' and len(editor.document_infos)==3


@pytest.mark.parametrize('session_first',[False,True])
def test_session_item_keeps_existing_append_and_inert_table_behavior(tmp_path,session_first):
    session=tmp_path/'input.ijx';source=Editor(preferences=tmp_path/'source.json')
    source.add_document(np.full((3,5),31,np.uint8));source.results=[{'Value':7}];source.workflow=[{'operation':'historical_inert'}]
    source.action('save_workspace',path=str(session))
    items=[session,FIXTURE,np.zeros((3,5),np.uint8)] if session_first else [np.zeros((3,5),np.uint8),FIXTURE,session]
    editor=launch(items,preferences=tmp_path/'prefs.json');choose(editor)
    assert editor.launch_state['status']=='completed' and len(editor.document_infos)==3
    assert editor.results==source.results and editor.workflow==source.workflow
    np.testing.assert_array_equal(values(editor)[0 if session_first else 2].numpy('YX'),31)


@pytest.mark.parametrize('bad',[{'path':''},{'path':'nul\x00.png'},{'path':True},{'path':'a','unknown':1},{'document':{}},{'unknown':0}],ids=['empty','nul','bool','extra','bad_document','unknown'])
def test_native_queue_prevalidates_all_items_before_publishing_prefix(tmp_path,bad):
    editor=Editor(preferences=tmp_path/'prefs.json')
    item={'document':serialize_document(Document(np.zeros((3,5),np.uint8)))}
    with pytest.raises((ValueError,RuntimeError,TypeError)):editor.action('queue_launch',items=[item,bad])
    assert not editor.document_infos and editor.launch_state is None


@pytest.mark.parametrize('state',['frame','document','already_started'])
def test_mixed_queue_requires_one_fresh_launch(tmp_path,state):
    editor=Editor(preferences=tmp_path/'prefs.json')
    pixels=np.zeros((3,5),np.uint8)
    if state=='frame':editor.render_headless(1)
    elif state=='document':editor.add_document(pixels)
    else:editor.append_launch_items([pixels])
    before=[d['id'] for d in editor.document_infos]
    with pytest.raises((ValueError,RuntimeError)):editor.append_launch_items([pixels])
    assert [d['id'] for d in editor.document_infos]==before


@pytest.mark.parametrize('native',[False,True])
@pytest.mark.parametrize('count',[0,65])
def test_mixed_queue_input_count_is_checked_before_append(tmp_path,native,count):
    editor=Editor(preferences=tmp_path/'prefs.json')
    with pytest.raises((ValueError,RuntimeError)):
        if native:editor.action('queue_launch',items=[{'path':str(FIXTURE)}]*count)
        else:editor.append_launch_items([np.zeros((1,1),np.uint8)]*count)
    assert not editor.document_infos and editor.launch_state is None


def test_reviewed_launch_path_cannot_be_replaced_and_external_open_is_blocked(tmp_path):
    editor=launch([np.zeros((3,5),np.uint8),FIXTURE],preferences=tmp_path/'prefs.json');state=settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',path=str(tmp_path/'another.tif'))
    with pytest.raises((ValueError,RuntimeError)):editor.open_file(FIXTURE)
    assert state['fixed_path'] and len(editor.document_infos)==1
    editor.action('cancel_launch');editor.wait()
    assert editor.launch_state['status']=='canceled' and len(editor.document_infos)==1


def test_memory_change_cancels_current_and_future_inputs(tmp_path):
    editor=launch([np.zeros((3,5),np.uint8),FIXTURE,np.ones((3,5),np.uint8)],preferences=tmp_path/'prefs.json');settle(editor)
    editor.update_settings(processing_memory_mb=128,persist=False);editor.poll()
    assert editor.launch_state['status']=='canceled' and len(editor.document_infos)==1


def test_no_tiff_mixed_launch_completes_synchronously(tmp_path):
    inputs=[np.zeros((3,5),np.uint8),Document(np.ones((3,5),np.uint8),'second')]
    editor=launch(inputs,preferences=tmp_path/'prefs.json')
    assert editor.launch_state['status']=='completed' and len(editor.document_infos)==2
    reference=Editor(preferences=tmp_path/'reference.json')
    for item in inputs:reference.add_document(item)
    def ownership_state(host):
        return [(host.live_document(info['id']).native_snapshot().dirty,host.live_document(info['id']).native_owner.undo_steps) for info in host.document_infos]
    assert ownership_state(editor)==ownership_state(reference)


def test_repeated_path_is_two_distinct_reviewed_append_items(tmp_path):
    editor=launch([np.zeros((3,5),np.uint8),FIXTURE,FIXTURE],preferences=tmp_path/'prefs.json')
    choose(editor);state=settle(editor)
    assert len(editor.document_infos)==2 and not state['confirm_axes']
    choose(editor)
    assert len(editor.document_infos)==3 and len({info['id'] for info in editor.document_infos})==3


def test_full_admitted_item_count_completes(tmp_path):
    editor=launch([np.zeros((1,1),np.uint8)]*64,preferences=tmp_path/'prefs.json')
    assert editor.launch_state['status']=='completed' and len(editor.document_infos)==64


def test_python_payload_error_does_not_start_native_append(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json')
    with pytest.raises((ValueError,TypeError,RuntimeError)):
        editor.append_launch_items([np.zeros((3,5),np.uint8),np.zeros((3,5,2),np.uint8)])
    assert editor.launch_state is None and not editor.document_infos
