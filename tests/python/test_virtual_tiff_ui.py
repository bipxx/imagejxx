import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop
from imagejxx.ui.app import Editor
from test_lzw_tiff_editor import settle
from test_virtual_tiff import write

@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
@pytest.mark.parametrize('mode',['open','replay','cancel','changed','startup'])
def test_virtual_import_controls(context,mode,tmp_path):
    if context=='imgui_cpp':r=json.loads((Path(os.environ['IMAGEJXX_VIRTUAL_TIFF_CPP_ROOT'])/mode/'case.json').read_bytes())
    else:r=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._virtual_tiff_ui_case(mode,str(tmp_path/'capture')))
    assert r['contract']=='p10u.virtual-tiff.ui.001' and r['state']=='observed' and r['context']==context and not r['acceptance_claim']
    assert all(f['events']==f['input_frame']['raw_inputs'] for f in r['frames'])
    assert not r['inspected']['virtual_loading'] and not r['inspected']['confirm_axes']
    assert r['selected']['virtual_loading'] and not r['selected']['confirm_axes']
    if mode in ('cancel','changed'):
        assert r['count']==0 and r['workflow']==[]
        if mode=='changed':assert 'changed' in r['import_error'].lower()
        return
    assert r['count']==(2 if mode in ('replay','startup') else 1)
    assert all(not d['eager'] and d['shape']==[2,2,3,4,5,1] and not d['info']['dirty'] for d in r['documents'])
    if mode=='startup':
        assert r['after_first_count']==0 and not r['second_review']['virtual_loading'] and not r['second_review']['confirm_axes']
    if mode=='replay':assert r['replayed']['virtual_loading'] and not r['replayed']['confirm_axes'] and r['replayed'].get('inspection') is None
    records=[v for v in r['workflow'] if v.get('schema')=='imagejxx.scientific-image-io']
    assert all(v['version']==16 and v['loading']=='virtual' and v['source_format_version']==(15 if mode=='startup' else 14) for v in records)
    values=1100+np.arange(3)[:,None,None]*10+np.arange(20).reshape(1,4,5)
    for view in r['xyz']:
        coordinate=view['coordinate'];plane=view['plane'];a=values[coordinate] if plane=='XY' else values[:,coordinate,:] if plane=='XZ' else values[:,:,coordinate]
        assert view['readback']['available'] and view['readback']['source']['pixels']==a.ravel().tolist()
    assert r['histogram']['counts']==[10,20,20,10] and r['histogram']['mean']==pytest.approx(values.mean())

def opened(tmp_path):
    path=tmp_path/'input.tif';write(path,np.arange(20,dtype=np.uint16).reshape(4,5))
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.open_file(path);assert not settle(editor)['error']
    editor.action('scientific_io_draft',virtual_loading=True);editor.action('scientific_io_draft',confirm_axes=True)
    editor.action('scientific_io_apply');assert not settle(editor)['error']
    assert not editor.live_document().native_snapshot().dataset.has_eager_planes
    return editor,path

def test_v16_replay_retains_intent_without_reading_historical_path_or_consent(tmp_path):
    editor,path=opened(tmp_path);record=copy.deepcopy(editor.workflow[-1]);assert record['version']==16 and record['source_format_version']==1
    path.unlink();editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['virtual_loading'] and not state['confirm_axes'] and state.get('inspection') is None and not state['working']

@pytest.mark.parametrize('patch',[{'version':16.0},{'source_format_version':16},{'source_format_version':True},{'source_format_version':2},{'loading':'eager'},{'operation':'export'},{'extra':1}])
def test_v16_closed_records_refuse_before_changing_owner(patch,tmp_path):
    editor,path=opened(tmp_path);record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError,KeyError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty

@pytest.mark.parametrize('field',['loading','source_format_version'])
def test_v16_requires_both_intent_fields(field,tmp_path):
    editor,path=opened(tmp_path);record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError,KeyError)):editor.action('review_scientific_io',record=record)


def test_python_constructor_keeps_virtual_startup_attachment_atomic(tmp_path):
    first,last=tmp_path/'first.tif',tmp_path/'last.tif'
    values=np.arange(20,dtype=np.uint16).reshape(4,5)
    write(first,values);write(last,values)
    editor=Editor(preferences=tmp_path/'prefs.json',images=[first,last])
    for index in range(2):
        state=settle(editor);assert not state['error'] and not state['virtual_loading']
        editor.action('scientific_io_draft',virtual_loading=True)
        editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply')
        assert not settle(editor)['error']
        assert len(editor.document_infos)==(0 if index==0 else 2)
    assert all(not editor.live_document(row['id']).native_snapshot().dataset.has_eager_planes for row in editor.document_infos)
