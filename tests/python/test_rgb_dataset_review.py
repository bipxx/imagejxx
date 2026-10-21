"""Shared ImGui RGB review uses isolated native preparation and explicit adoption."""
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor


def setup(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True
    pixels=np.zeros((2,2,2,2,3),np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):pixels[t,c,z]=10+100*t+20*c+z+np.arange(6).reshape(2,3)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes='TCZYX').observe_reads(reads))
    display=owner.snapshot().display;display.t=display.c=1;display.crosshair=[1,1,1]
    display.properties={'channel_display':{'schema':'imagejxx.channel-display','version':1,'mode':'composite','channels':[
        {'visible':True,'range':[0,255],'lut':{'name':'Red'}},{'visible':True,'range':[0,255],'lut':{'name':'Green'}}]}}
    owner.set_display(display);editor.add_native_document(owner)
    return editor,owner,pixels,reads


def state(editor):return editor.render_headless(frames=3)['rgb_dataset']


def click(editor,panel,name):
    c=panel['controls'][name];assert c['enabled'] and c['visible'] and c['id']
    events=[dict(type='position',space='screen',point=c['point'],frames=2),
            dict(type='button',name='left',down=True),dict(type='button',name='left',down=False,frames=2)]
    return editor.render_headless(frames=3,events=events)['rgb_dataset']


@pytest.mark.parametrize('mode',['per_plane','composite'])
@pytest.mark.parametrize('replace',[False,True])
def test_preview_apply_records_exact_native_intent_and_history(tmp_path,mode,replace):
    editor,owner,pixels,reads=setup(tmp_path);before=owner.snapshot();count=reads.counts().plane_reads
    editor.action('show_rgb_dataset');assert reads.counts().plane_reads==count
    editor.action('rgb_dataset_draft',mode=mode,replace=replace)
    click(editor,state(editor),'preview');editor.wait();panel=state(editor)
    assert panel['ready'] and not panel['error'] and owner.undo_steps==0 and not editor.workflow
    expected=ij.prepare_rgb_dataset(ij.rgb_dataset_command_from_record(before,panel['record'])).preview.dataset.numpy('TCZYXS')
    click(editor,panel,'apply');editor.wait();assert not state(editor)['open']
    result=editor.live_document().native_snapshot();np.testing.assert_array_equal(result.dataset.numpy('TCZYXS'),expected)
    assert len(editor.workflow)==1 and editor.workflow[0]==panel['record']
    ij.validate_rgb_dataset_command_record(editor.workflow[0])
    assert owner.undo_steps==int(replace)
    if replace:
        editor.action('undo');np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
        editor.action('redo');np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),expected)
    else:
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
        assert result.id!=owner.id


def test_ready_preview_cancel_does_not_publish_or_record(tmp_path):
    editor,owner,pixels,_=setup(tmp_path);editor.action('show_rgb_dataset')
    click(editor,state(editor),'preview');editor.wait();assert state(editor)['ready']
    click(editor,state(editor),'cancel');editor.poll()
    assert not state(editor)['open'] and owner.undo_steps==0 and not editor.workflow
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)


@pytest.mark.parametrize('kind',['display','pixels','selection','metadata'])
def test_stale_source_discards_ready_preview(tmp_path,kind):
    editor,owner,pixels,_=setup(tmp_path);editor.action('show_rgb_dataset');editor.action('rgb_dataset_preview');editor.wait()
    if kind=='display':
        view=owner.snapshot().display;view.t=0;owner.set_display(view)
    else:
        tx=owner.begin_edit()
        if kind=='pixels':tx.replace_plane(np.zeros((2,3),np.uint8))
        elif kind=='metadata':tx.set_title('changed')
        else:tx.select(ij.Selection('rectangle',[(0,0),(1,1)]))
        owner.commit(tx)
    editor.poll();panel=state(editor)
    assert not panel['open'] and panel['cancel_reason']=='source_or_settings_changed' and not editor.workflow
    with pytest.raises(ValueError):editor.action('rgb_dataset_apply')


def test_bad_patch_leaves_ready_preview_and_invalid_record_cannot_rebind(tmp_path):
    editor,owner,_,reads=setup(tmp_path);editor.action('show_rgb_dataset');editor.action('rgb_dataset_preview');editor.wait()
    original=state(editor);count=reads.counts().plane_reads
    with pytest.raises(ValueError):editor.action('rgb_dataset_draft',replace=1)
    assert state(editor)['record']==original['record'] and state(editor)['ready']
    editor.action('cancel_rgb_dataset');bad=copy.deepcopy(original['record']);bad['extra']=True
    count=reads.counts().plane_reads
    with pytest.raises(ValueError):editor.action('review_rgb_dataset',record=bad)
    assert reads.counts().plane_reads==count


def test_record_review_is_inert_and_preserves_frozen_rendering_on_new_source(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.action('show_rgb_dataset');editor.action('rgb_dataset_draft',mode='composite')
    original=state(editor)['record'];editor.action('cancel_rgb_dataset')
    other=ij.Document(owner.snapshot());view=other.snapshot().display;view.properties={};other.set_display(view);editor.add_native_document(other)
    editor.action('review_rgb_dataset',record=json.loads(json.dumps(original)))
    panel=state(editor);assert panel['open'] and not panel['ready'] and not editor.workflow and owner.undo_steps==0
    assert panel['record']['request']==original['request'] and panel['record']['source']['document_id']==other.id
    assert original['source']['document_id']==owner.id and panel['restored_record']
    editor.action('rgb_dataset_draft',replace=True)
    assert state(editor)['record']['request']['channels']==original['request']['channels']
    click(editor,state(editor),'recapture')
    assert state(editor)['record']['request']['channels'] is None and not state(editor)['restored_record']


def test_legacy_palette_is_baked_in_per_plane_mode(tmp_path):
    editor,owner,pixels,_=setup(tmp_path);view=owner.snapshot().display
    view.properties={'lut':'Inverted','display_range':[0.,255.]};owner.set_display(view)
    editor.action('show_rgb_dataset');editor.action('rgb_dataset_apply');editor.wait()
    expected=np.repeat((255-pixels)[...,None],3,axis=-1)
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('TCZYXS'),expected)


def test_actual_menu_and_buttons_in_both_hosts(tmp_path):
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    for report in [cpp,python]:
        assert report['shape']==report['redo_shape']==[2,1,2,2,3,3] and report['undo_shape']==[2,2,2,2,3,1]
        expected=[]
        for t in range(2):
            for z in range(2):
                rgb=np.zeros((2,3,3),np.uint8)
                rgb[...,0]=10+100*t+z+np.arange(6).reshape(2,3)
                rgb[...,1]=30+100*t+z+np.arange(6).reshape(2,3)
                expected.append(rgb.reshape(-1))
        np.testing.assert_array_equal(report['pixels'],expected)
        assert report['history_before_apply']==0 and not report['workflow_before_apply']
        assert report['undo_steps']==1 and report['display']=={} and not report['error']
        assert report['preview']['ready'] and len(report['workflow'])==1
        legacy=report['deferred_legacy'];assert legacy['shape']==[1,1,1,1,3,3] and legacy['source_undo_steps']==0
        np.testing.assert_array_equal(legacy['pixels'],[0,0,0,128,0,0,255,0,0])
        assert legacy['record']['request']['channels'] is None and legacy['record']['request']['lut'] is not None
        assert report['workflow'][0]==report['preview']['record']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])


@pytest.mark.parametrize('explicit_range',[False,True])
def test_legacy_composite_palette_is_preserved_with_deferred_or_explicit_range(tmp_path,explicit_range):
    editor,owner,pixels,_=setup(tmp_path);view=owner.snapshot().display
    properties={'lut':'Red'}
    if explicit_range:properties['display_range']=[0.,255.]
    view.properties=properties
    owner.set_display(view);editor.action('show_rgb_dataset')
    editor.action('rgb_dataset_draft',mode='composite');panel=state(editor)
    assert (panel['record']['request']['lut'] is None)==explicit_range
    editor.action('rgb_dataset_apply');editor.wait()
    expected=np.zeros((2,1,2,2,3,3),np.uint8)
    # Byte defaults use the full0..255 range, matching the explicit range.
    expected[:,0,...,0]=np.minimum(pixels[:,0].astype(int)+pixels[:,1].astype(int),255)
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('TCZYXS'),expected)
