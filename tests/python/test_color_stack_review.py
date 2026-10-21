"""Standalone shared ImGui color components: native intent, isolation and publication."""
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor
from test_color_stack_workflow import fixture, split_expected


def setup(tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True
    owner,pixels,reads=fixture()
    view=owner.snapshot().display;view.properties={};owner.set_display(view)
    editor.add_native_document(owner)
    return editor,owner,pixels,reads


def state(editor):return editor.render_headless(frames=3)['color_stack']


@pytest.mark.parametrize('space',['rgb','hsb8','hsb32'])
@pytest.mark.parametrize('replace',[False,True])
def test_preview_apply_records_native_intent_and_paired_history(tmp_path,space,replace):
    editor,owner,pixels,reads=setup(tmp_path);source=owner.snapshot();count=reads.counts().plane_reads
    editor.action('show_color_stack');assert reads.counts().plane_reads==count
    editor.action('color_stack_draft',space=space,replace=replace)
    editor.action('color_stack_preview');editor.wait();panel=state(editor)
    assert panel['ready'] and not panel['error'] and owner.undo_steps==0 and not editor.workflow
    expected=split_expected(pixels,space)
    editor.action('color_stack_apply');editor.wait();assert not state(editor)['open']
    result=editor.live_document().native_snapshot();np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),expected)
    assert editor.workflow==[panel['record']];ij.validate_color_stack_command_record(editor.workflow[0])
    assert owner.undo_steps==int(replace) and result.dataset.calibration.spacing==source.dataset.calibration.spacing
    assert len(ij.planar_display_annotations(result))==3
    if replace:
        editor.action('undo');np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels)
        assert owner.snapshot().display.properties==source.display.properties
        editor.action('redo');np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),expected)
    else:
        assert result.id!=owner.id
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels)


def test_ready_cancel_is_inert(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.action('show_color_stack');editor.action('color_stack_preview');editor.wait()
    assert state(editor)['ready'];editor.action('cancel_color_stack');editor.poll()
    assert not state(editor)['open'] and owner.undo_steps==0 and not editor.workflow


@pytest.mark.parametrize('kind',['display','pixels','selection','metadata'])
def test_stale_source_discards_preview(tmp_path,kind):
    editor,owner,_,_=setup(tmp_path);editor.action('show_color_stack');editor.action('color_stack_preview');editor.wait()
    if kind=='display':
        view=owner.snapshot().display;view.t=0;owner.set_display(view)
    else:
        tx=owner.begin_edit()
        if kind=='pixels':tx.replace_plane(np.zeros((2,3,3),np.uint8))
        elif kind=='metadata':tx.set_title('changed')
        else:tx.select(ij.Selection('rectangle',[(0,0),(1,1)]))
        owner.commit(tx)
    editor.poll();panel=state(editor)
    assert not panel['open'] and panel['cancel_reason']=='source_or_settings_changed' and not editor.workflow
    with pytest.raises(ValueError):editor.action('color_stack_apply')


def test_bad_patch_and_capture_do_not_replace_ready_intent(tmp_path):
    editor,_,_,reads=setup(tmp_path);editor.action('show_color_stack');editor.action('color_stack_preview');editor.wait()
    original=state(editor)
    with pytest.raises(ValueError):editor.action('color_stack_draft',replace=1)
    assert state(editor)['record']==original['record'] and state(editor)['ready']
    editor.action('cancel_color_stack');bad=copy.deepcopy(original['record']);bad['extra']=True
    count=reads.counts().plane_reads
    with pytest.raises(ValueError):editor.action('review_color_stack',record=bad)
    assert reads.counts().plane_reads==count


def test_review_restores_request_on_new_owner_without_execution(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.action('show_color_stack');editor.action('color_stack_draft',space='hsb32')
    original=state(editor)['record'];editor.action('cancel_color_stack')
    other=ij.Document(owner.snapshot());editor.add_native_document(other)
    editor.action('review_color_stack',record=json.loads(json.dumps(original)));panel=state(editor)
    assert panel['open'] and not panel['ready'] and not editor.workflow and panel['restored_record']
    assert panel['record']['request']==original['request'] and panel['record']['source']['document_id']==other.id
    assert original['source']['document_id']==owner.id


def test_invalid_legacy_choices_remain_editable_until_explicit_loss_consent(tmp_path):
    editor,owner,_,reads=setup(tmp_path);editor.action('show_color_stack');count=reads.counts().plane_reads
    editor.action('color_stack_draft',space='hsb32',layout='imagej_legacy')
    assert reads.counts().plane_reads==count
    panel=state(editor);assert panel['error'] and not panel['ready'] and 'record' not in panel
    assert not panel['controls']['apply']['enabled']
    with pytest.raises(ValueError):editor.action('color_stack_apply')
    count=reads.counts().plane_reads
    editor.action('color_stack_draft',allow_calibration_reset=True);assert reads.counts().plane_reads==count
    assert state(editor)['error']
    count=reads.counts().plane_reads
    editor.action('color_stack_draft',allow_nonxy_annotation_drop=True)
    assert reads.counts().plane_reads==count
    assert not state(editor)['error'] and owner.undo_steps==0


def test_actual_standalone_menu_choices_preview_apply_in_both_hosts(tmp_path):
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    pixels=np.empty((2,2,2,2,3,3),np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):
                for k in range(3):pixels[t,c,z,...,k]=10+50*t+20*c+z+np.arange(6).reshape(2,3)+20*k
    expected=split_expected(pixels,'hsb32').reshape(2,2,3,2,6).transpose(2,0,3,1,4).reshape(24,6)
    for report in [cpp,python]:
        data=report['standalone_color_stack']
        assert data['shape']==data['redo_shape']==[1,3,8,2,3,1] and data['unit']=='px'
        assert data['undo_shape']==[2,2,2,2,3,3] and data['undo_unit']=='um' and data['undo_layers']==1
        assert data['history_before_apply']==0 and data['undo_steps']==1 and data['layers']==0
        assert data['missing_consent']['error'] and not data['missing_consent']['controls']['apply']['enabled']
        assert data['preview']['ready'] and not data['preview']['error'] and data['preview']['preview_size']==[3,2]
        assert data['record']==data['preview']['record']
        request=data['record']['request'];assert request['space']=='hsb32' and request['layout']=='imagej_legacy'
        assert request['allow_calibration_reset'] and request['allow_nonxy_annotation_drop'] and request['destination']=='in_place'
        np.testing.assert_array_equal(np.array(data['pixels'],np.float32),expected)
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])


@pytest.mark.parametrize('space,invert',[('rgb',False),('rgb',True),('hsb8',False),('hsb32',False)])
def test_merge_default_for_scalar_source_and_apply_without_preview(tmp_path,space,invert):
    pixels=np.arange(18,dtype=np.uint8).reshape(2,3,3)*10
    components=ij.split_color_components(pixels,space)
    owner=ij.Document(ij.Dataset(components,axes='CYX'))
    editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner);editor.recording=True
    editor.action('show_color_stack');editor.action('color_stack_draft',space=space,invert_rgb=invert,replace=True)
    assert state(editor)['record']['request']['action']=='merge'
    editor.action('color_stack_apply');editor.wait()
    expected=ij.merge_color_components(components,space,invert_rgb=invert)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS')[0,0,0],expected)
    assert owner.undo_steps==1 and editor.workflow[0]['request']['invert_rgb']==invert


def test_existing_rgb_flatten_uses_explicit_legacy_loss_flags(tmp_path):
    editor,owner,_,_=setup(tmp_path);editor.action('show_color_stack')
    editor.action('color_stack_draft',action='flatten_rgb',layout='imagej_legacy',allow_calibration_reset=True,allow_nonxy_annotation_drop=True,replace=True)
    assert not state(editor)['error']
    editor.action('color_stack_apply');editor.wait()
    assert owner.snapshot().dataset.shape==(1,1,8,2,3,3) and owner.undo_steps==1
    assert editor.workflow[0]['request']['action']=='flatten_rgb'


@pytest.mark.parametrize('args,kwargs,expected',[
    (('color_stack_draft',),{'action':'split'},('color_stack_draft',{'action':'split'})),
    (('lut',),{'name':'Red'},('lut',{'name':'Red'})),
    ((),{'action':'lut','name':'Red'},('lut',{'name':'Red'})),
    ((),{'name':'undo'},('undo',{}))])
def test_python_action_dispatch_preserves_parameter_names(args,kwargs,expected):
    class Capture:
        def action(self,name,parameters):self.observed=(name,json.loads(parameters))
    editor=Editor.__new__(Editor);editor._editor=Capture();editor.action(*args,**kwargs)
    assert editor._editor.observed==expected
