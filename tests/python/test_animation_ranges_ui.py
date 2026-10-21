"""Actual range/options gestures with literal forward/reverse/ping-pong traces."""
import json
import os
from pathlib import Path
import pytest
from test_animation_ui import samples

EXPECTED = {
    'range': [3,5,1,3,5,1,3,5],
    'reverse': [5,3,1,5,3,1,5,3],
    'ping': [3,5,3,1,3,5,3,1],
    'range-offgrid': [1,3,5,1,3,5,1,3],
    'range-c': [2,0,2,0,2,0,2,0],
    'range-t': [0,1,0,1,0,1,0,1],
    'range-invalid': [3,5,1,3,5,1,3,5],
}


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('case',list(EXPECTED))
def test_actual_range_and_direction_controls(case, context, tmp_path):
    if context == 'imgui_cpp':
        configured=os.environ.get('IMAGEJXX_ANIMATION_CPP_ROOT')
        assert configured,'Required installed C++ playback captures are missing'
        report=json.loads((Path(configured)/case/'case.json').read_bytes())
    else:
        from imagejxx import _desktop
        report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._animation_ui_case(case,str(tmp_path/'capture')))
        assert report==json.loads((tmp_path/'capture/case.json').read_bytes())
    assert report['contract_id']=='p09s.animation-ui.001'
    assert (report['case_id'],report['context'])==(case,context)
    assert report['observation_state']=='observed' and report['acceptance_claim'] is False
    assert 0<report['elapsed_seconds']<=15 and report['frame_count']==len(report['frames'])<=180
    assert not report['lifecycle']['open']
    shape=[2,3,8,5,7,1]; assert report['shape']==shape
    before,after=report['before'],report['after']
    assert before['samples']==after['samples']==samples(case,shape)
    axis=1 if case=='range-c' else 0 if case=='range-t' else 2
    mode='reverse' if case in {'reverse','range-c'} else 'ping_pong' if case in {'ping','range-t'} else 'forward'
    first,last,step=(0,2,2) if axis==1 else (0,1,1) if axis==0 else (1,6,2)
    options=report['options_before_submit']
    assert options['open'] and not options['is_operation']
    label={'forward':'Forward','reverse':'Reverse','ping_pong':'Ping-pong'}[mode]
    assert {k:options['text'][k] for k in ('first','last','step')}==dict(first=str(first+1),last=str(last+1),step=str(step))
    assert options['values']['mode']==label
    controls=[x['name'] for x in report['controls']]
    assert {'menu.action.show_animation_options','dialog.first','dialog.last','dialog.step','dialog.apply'}<=set(controls)
    if mode!='forward':assert 'dialog.mode.choice.'+label in controls
    previous=before; ticks=0;trace=[]
    for frame in report['frames']:
        source=frame['source']; rendered=frame['rendered']; animation=rendered['animation']
        assert frame['events']==rendered['input_frame']['raw_inputs'] and not rendered['window_error']
        delta=animation['ticks']-ticks;assert delta in {0,1}
        position=previous['position'].copy()
        if delta:
            position[axis]=EXPECTED[case][len(trace)];trace.append(position[axis])
        assert source['position']==position and source['display_revision']==previous['display_revision']+delta
        assert rendered['view']['crosshair']==source['crosshair']
        assert source['shares_original'] and not source['dirty']
        assert source['undo']==source['redo']==source['data_identity']==0
        assert source['pixels_revision']==source['metadata_revision']==source['selection_revision']==0
        assert source['workflow']==source['results']==[]
        for key in ('selection','previous','roi','zoom','plane_zoom','linked_zoom','spacing','origin'):
            assert source[key]==before[key]
        if animation['playing']:
            assert (animation['first'],animation['last'],animation['step'],animation['mode'])==(first,last,step,mode)
            assert animation['count']==(2 if axis in {0,1} else 3)
            assert animation['last_selected']==(2 if axis==1 else 1 if axis==0 else 5)
            for p in rendered['orthogonal_planes']:assert p['current_tcz']==source['position']
        if frame['label']=='after_stop':assert not animation['playing'] and animation['source_id'] is None
        previous,ticks=source,animation['ticks']
    assert trace==EXPECTED[case] and after['position']==previous['position']
    if case=='range-invalid':
        assert {x['label'] for x in report['api']}=={'range_negative','range_outside','range_zero_step','range_singleton','range_mode'}
        for item in report['api']:
            assert item['accepted'] is False and item['error']
            assert item['before']==item['after']


@pytest.mark.parametrize('change',['axis','source','metadata'])
def test_options_refuse_changed_context_without_publishing(change,tmp_path):
    import numpy as np
    import imagejxx as ij
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor=Editor(preferences=Preferences(tmp_path/'prefs.json'))
    pixels=np.arange(2*3*8*5*7,dtype=np.uint16).reshape(2,3,8,5,7,1)
    source=ij.Document(ij.Dataset(pixels,axes='TCZYXS'),'Source')
    other=ij.Document(ij.Dataset(np.zeros((5,7),np.uint8),axes='YX'),'Other')
    editor.add_native_document(source);editor.add_native_document(other);editor.active_index=0
    editor.action('show_animation_options')
    opened=editor.render_headless()
    assert opened['dialog']['title']=='Animation options (Z)' and opened['dialog']['open']
    if change=='axis':editor.action('animation',axis='c')
    elif change=='source':editor.active_index=1
    else:
        edit=source.begin_edit();edit.set_title('Changed');source.commit(edit)
    before=source.snapshot();other_before=other.snapshot()
    point=opened['dialog']['controls']['apply']['point']
    report=editor.render_headless(events=[{'type':'position','space':'screen','point':point},
        {'type':'button','name':'left','down':True},{'type':'button','name':'left','down':False},
        {'type':'wait','frames':2}])
    assert report['window_error']=='Animation options source or axis changed'
    assert report['error_modal']['open'] and report['dialog']['open']
    assert not report['animation']['playing']
    assert report['animation']['axis']==('c' if change=='axis' else 'z')
    assert source.snapshot().shares_data(before) and other.snapshot().shares_data(other_before)
    assert source.undo_steps==(1 if change=='metadata' else 0) and other.undo_steps==0
    assert editor.workflow==[] and editor.results==[]
    (tmp_path/'guard.json').write_bytes((json.dumps(dict(
        contract_id='p09u.animation-options-guard.001',case_id=change,context='imgui_python',
        observation_state='observed',acceptance_claim=False,rendered=report),indent=2)+'\n').encode())
