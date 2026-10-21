"""Shared indexed menu review, isolated worker previews and guarded publication."""
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor
from test_color_stack_workflow import fixture

def setup(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True
    owner,pixels,reads=fixture();view=owner.snapshot().display;view.properties={};owner.set_display(view);editor.add_native_document(owner)
    return editor,owner,pixels,reads

def state(editor):return editor.render_headless(frames=3)['indexed_color']

@pytest.mark.parametrize('colors',[2,16,256])
@pytest.mark.parametrize('replace',[False,True])
def test_quantize_preview_publish_palette_and_history(tmp_path,colors,replace):
    editor,owner,pixels,reads=setup(tmp_path);source=owner.snapshot();count=reads.counts().plane_reads
    editor.action('show_indexed_dataset');editor.action('indexed_color_draft',max_colors=colors,replace=replace)
    assert reads.counts().plane_reads==count
    editor.action('indexed_color_preview');editor.wait();panel=state(editor)
    expected=ij.quantize_indexed_color(pixels.reshape(-1,2,3,3),max_colors=colors)
    assert panel['ready'] and not panel['error'] and owner.undo_steps==0 and not editor.workflow
    assert panel['palette']['color_count']==expected['color_count'] and panel['palette']['palette']==expected['palette'].tolist()
    editor.action('indexed_color_apply');editor.wait();assert not state(editor)['open']
    result=editor.live_document().native_snapshot()
    np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),expected['indices'].reshape(2,2,2,2,3))
    assert result.dataset.calibration.spacing==[2,3,5] and result.display.t==1 and result.display.c==1
    assert ij.planar_display_annotations(result) and editor.workflow==[panel['record']]
    assert owner.undo_steps==int(replace) and (result.id==owner.id)==replace
    if replace:
        editor.action('undo');np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels)
        assert owner.snapshot().display.properties==source.display.properties
        editor.action('redo');assert ij.indexed_palette(owner.snapshot())==panel['palette']

@pytest.mark.parametrize('palette',['display','stored'])
@pytest.mark.parametrize('replace',[False,True])
def test_expand_uses_current_palette_choice_and_records(tmp_path,palette,replace):
    editor,owner,_,_=setup(tmp_path)
    idx=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(owner.snapshot(),max_colors=16)).preview
    indexed=ij.Document(idx);view=indexed.snapshot().display;view.properties={'lut':'Red','display_range':[0,15]};indexed.set_display(view)
    editor.add_native_document(indexed);editor.action('show_indexed_rgb');editor.action('indexed_color_draft',palette_source=palette,replace=replace)
    expected=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(indexed.snapshot(),palette_source=ij.IndexedRgbPaletteSource.Stored if palette=='stored' else ij.IndexedRgbPaletteSource.Display)).preview
    editor.action('indexed_color_preview');editor.wait();panel=state(editor);assert panel['ready'] and panel['expand']
    editor.action('indexed_color_apply');editor.wait();result=editor.live_document().native_snapshot()
    np.testing.assert_array_equal(result.dataset.numpy('TCZYXS'),expected.dataset.numpy('TCZYXS'))
    assert editor.workflow==[panel['record']] and indexed.undo_steps==int(replace)

@pytest.mark.parametrize('kind',['display','pixels','selection','metadata','activation','settings'])
def test_stale_preview_discarded_without_recording(tmp_path,kind):
    editor,owner,_,_=setup(tmp_path)
    if kind=='activation':editor.add_native_document(ij.Document(owner.snapshot()));editor.active_index=0
    editor.action('show_indexed_dataset');editor.action('indexed_color_preview');editor.wait()
    if kind=='display':view=owner.snapshot().display;view.zoom=3;owner.set_display(view)
    elif kind=='activation':
        with pytest.raises(ValueError):editor.active_index=1
        assert state(editor)['ready'];editor.action('cancel_indexed_color');editor.active_index=1
    elif kind=='settings':editor.update_settings(processing_memory_mb=256)
    else:
        tx=owner.begin_edit()
        if kind=='pixels':tx.replace_plane(np.zeros((2,3,3),np.uint8))
        elif kind=='metadata':tx.set_title('changed')
        else:tx.select(ij.Selection('rectangle',[(0,0),(1,1)]))
        owner.commit(tx)
    editor.poll();assert not state(editor)['open'] and not editor.workflow
    with pytest.raises(ValueError):editor.action('indexed_color_apply')

@pytest.mark.parametrize('patch',[{'max_colors':True},{'max_colors':1},{'max_colors':257},{'max_colors':2.0},{'replace':1},{'extra':True},{'palette_source':'stored'}])
def test_bad_patch_retains_ready_capture(tmp_path,patch):
    editor,_,_,_=setup(tmp_path);editor.action('show_indexed_dataset');editor.action('indexed_color_preview');editor.wait();before=state(editor)
    with pytest.raises(ValueError):editor.action('indexed_color_draft',**patch)
    after=state(editor);assert after['ready'] and after['record']==before['record']

def test_ready_cancel_and_changed_valid_choices_are_inert(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.action('show_indexed_dataset');editor.action('indexed_color_preview');editor.wait()
    editor.action('indexed_color_draft',max_colors=7);assert not state(editor)['ready'] and not editor.workflow
    editor.action('indexed_color_preview');editor.wait();editor.action('cancel_indexed_color');editor.poll()
    assert not state(editor)['open'] and not editor.workflow and owner.undo_steps==0

@pytest.mark.parametrize('expand',[False,True])
def test_record_review_rebinds_without_processing(tmp_path,expand):
    editor,owner,_,reads=setup(tmp_path)
    if expand:
        owner=ij.Document(ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(owner.snapshot())).preview);editor.add_native_document(owner)
    editor.action('show_indexed_rgb' if expand else 'show_indexed_dataset');capture=state(editor)['record'];editor.action('cancel_indexed_color')
    other=ij.Document(owner.snapshot());editor.add_native_document(other);count=reads.counts().plane_reads
    editor.action('review_indexed_color',record=json.loads(json.dumps(capture)));assert reads.counts().plane_reads==count
    panel=state(editor)
    assert panel['restored_record'] and panel['open'] and not panel['ready'] and not editor.workflow
    assert panel['record']['source']['document_id']==other.id and panel['record']['request']==capture['request']
    bad=copy.deepcopy(capture);bad['extra']=True
    with pytest.raises(ValueError):editor.action('review_indexed_color',record=bad)
    assert state(editor)['record']==panel['record']

def test_unsupported_input_leaves_editable_review(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.action('show_indexed_rgb');panel=state(editor)
    assert panel['open'] and panel['error'] and not panel['controls']['apply']['enabled']
    with pytest.raises(ValueError):editor.action('indexed_color_apply')
    assert not editor.workflow and owner.undo_steps==0

def test_memory_setting_refuses_before_pixels(tmp_path):
    editor,owner,_,reads=setup(tmp_path);editor.update_settings(processing_memory_mb=1);count=reads.counts().plane_reads
    editor.action('show_indexed_dataset');assert reads.counts().plane_reads==count
    panel=state(editor)
    assert panel['open'] and panel['error'] and not panel['controls']['apply']['enabled']
    assert not editor.workflow and owner.undo_steps==0

def test_recording_disabled_does_not_append_capture(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.recording=False;editor.action('show_indexed_dataset')
    editor.action('indexed_color_draft',replace=True);editor.action('indexed_color_apply');editor.wait()
    assert owner.undo_steps==1 and not editor.workflow and owner.snapshot().dataset.shape[-1]==1

def test_actual_indexed_menus_preview_record_and_replay_in_both_hosts(tmp_path):
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    for report in [cpp,python]:
        d=report['standalone_indexed'];assert d['history_before_apply']==0 and d['undo_steps']==1
        assert d['shape']==[1,1,1,1,2,1] and d['undo_shape']==[1,1,1,1,2,3] and d['redo_shape']==d['shape']
        assert d['preview']['ready'] and d['preview']['palette']['color_count']==2
        assert d['preview']['preview_rgba']==[0xff0000ff,0xff00ff00]
        assert d['record']['request']['max_colors']==2 and d['record']['schema']=='imagejxx.indexed-dataset-command'
        assert d['replay']['restored_record'] and not d['replay']['ready'] and d['replay']['record']['source']['document_id']==d['replay_owner']
        e=report['standalone_indexed_rgb'];assert e['preview']['ready'] and e['record']['request']['palette_source']=='stored'
        assert e['pixels']==[255,0,0,0,255,0] and e['undo_steps']==2 and e['shape']==[1,1,1,1,2,3]
