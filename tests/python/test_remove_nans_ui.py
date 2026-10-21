"""Shared Remove NaNs editor: actual ImGui inputs and independent byte checks."""
import json,os
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
CASES=['R45','R46','R47','R48','R49','R50','R51']
POLICY=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32

def source_plane(position,case):
    t,c,z=position
    a=np.full((5,5),7+10*t+20*c+30*z,np.float32)
    a[1,1]=-0.;a.view(np.uint32)[1,2]=0x7fc00002
    a.view(np.uint32)[2,2]=0xffc12345;a.view(np.uint32)[0,4]=0x7fc00001
    if case=='R51':a.view(np.uint32)[3,3]=0xffc00003
    if case=='R50':a.view(np.uint32)[:]=0x7fc00000
    return a

def filtered_plane(position,case):
    a=source_plane(position,case)
    if case=='R50':return a
    t,c,z=position;base=7+10*t+20*c+30*z
    if case=='R51':
        # Actual ImageJ 1.54r full-bounds triangle mask captured independently
        # on constant 7: filtered samples are zero, masked-out samples remain 7.
        row=json.loads((Path(__file__).parents[1]/'fixtures/tophat-mask-imagej154r.json').read_bytes())['cases'][0]
        mask=np.frombuffer(bytes.fromhex(row['output_hex']),dtype='<f4').reshape(5,5)==0
        a[mask & np.isnan(a)]=base
    else:
        a[1,2]=base;a[2,2]=base
    return a

def plane_hex(document):return [p['declared_hex'] for p in document['planes']]

def check_report(report,case):
    assert report['observation_state']=='observed' and report['acceptance_claim'] is False
    assert all(f['requested_events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert report['final_usage']=={'running':0,'queued':0,'running_bytes':0,'retained_bytes':0,'reserved_bytes':0}
    before=report['fixture_sources']['source'];states=report['states']
    assert before['sample_policy']=='preserve_quiet_nan_float32' and before['dtype']=='float32'
    for p in before['planes']:assert p['declared_hex']==source_plane(p['position'],case).tobytes().hex()
    for f in report['frames']:assert f['rendered']['rank_panel']['remembered_radii']['median']==2.
    panel=next(f['rendered']['rank_panel'] for f in report['frames'] if f['label']=='remove_nans.configured')
    assert panel['preset']=='remove_nans' and panel['radius']==1.
    assert not any(k in panel['controls'] for k in ['method','threshold','which','light_background','dont_subtract'])
    assert report['frames'][-1]['rendered']['rank_panel']['remembered_remove_nans']=={'radius':1.}
    preview=states['remove_nans.preview']['documents'][0]
    assert plane_hex(preview)==plane_hex(before) and preview['undo_steps']==0
    assert preview['display']['properties']==before['display']['properties']
    preview_frame=next(f for f in report['frames'] if f['label']=='remove_nans.preview')
    pixels=preview_frame['rendered']['ordinary_pixels'];readback=pixels['readback']['source']
    active=(1,1,1) if case in ['R47','R48'] else (0,0,0 if case in ['R50','R51'] else 1)
    assert readback['encoding']=='IEEE754 Float32LE'
    assert bytes(readback['bytes'])==filtered_plane(active,case).tobytes()
    assert pixels['range']==[7.,127.] and pixels['pixels_source']=='preview'
    if case=='R49':
        after=states['remove_nans.cancelled']['documents'][0]
        assert plane_hex(after)==plane_hex(before) and after['undo_steps']==0 and after['redo_steps']==before['redo_steps']
        assert after['display']['properties']==before['display']['properties'] and not states['remove_nans.cancelled']['workflow']
        return
    documents=states['remove_nans.applied']['documents']
    if case=='R48':
        assert len(documents)==2
        original=next(d for d in documents if d['document_id']==before['document_id'])
        assert plane_hex(original)==plane_hex(before) and original['undo_steps']==0 and original['display']['properties']==before['display']['properties']
        after=next(d for d in documents if d['document_id']!=before['document_id'])
    else:after=documents[0]
    selected={0,1,2} if case=='R46' else {10 if case in ['R47','R48'] else 0 if case in ['R50','R51'] else 1}
    for i,p in enumerate(after['planes']):
        expected=filtered_plane(p['position'],case) if i in selected else source_plane(p['position'],case)
        assert p['declared_hex']==expected.tobytes().hex()
    assert after['display']['properties']==before['display']['properties']
    for key in ['shape','dtype','sample_policy','calibration','selection','previous_selection','rois','overlays']:
        assert after[key]==before[key]
    for key in ['properties','extensions','record_layout']:assert after['metadata'][key]==before['metadata'][key]
    if case!='R48':assert after['metadata']['title']==before['metadata']['title']
    if case=='R50':
        assert after['undo_steps']==0 and after['redo_steps']==before['redo_steps'] and not states['remove_nans.applied']['workflow']
        return
    assert after['undo_steps']==1
    record=states['remove_nans.applied']['workflow'][-1]
    assert record['operation']=='remove_nans_imagej' and record['options']=={'radius':1.}
    if case=='R48':assert record['target']['destination']=='new_document'
    undone=next(d for d in states['remove_nans.undo']['documents'] if d['document_id']==after['document_id'])
    redone=next(d for d in states['remove_nans.redo']['documents'] if d['document_id']==after['document_id'])
    assert plane_hex(undone)==plane_hex(before) and plane_hex(redone)==plane_hex(after)
    assert undone['display']['properties']==before['display']['properties'] and redone['display']['properties']==after['display']['properties']

@pytest.mark.parametrize('case',CASES)
def test_actual_remove_nans_both_hosts(tmp_path,case):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case,'imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads((Path(os.environ['IMAGEJXX_REMOVE_NANS_CPP_DIR'])/case/'case.json').read_bytes())
    check_report(python,case);check_report(cpp,case)

@pytest.mark.parametrize('kind',['finite','quiet_nan','scientific'])
def test_python_editor_apply_and_new_output(tmp_path,kind):
    from imagejxx.ui.app import Editor
    a=np.full((5,5),7.,np.float32)
    if kind!='finite':a.view(np.uint32)[2,2]=0xffc12345
    if kind=='scientific':a=np.stack([a for _ in range(12)]).reshape(2,2,3,5,5)
    axes='TCZYX' if kind=='scientific' else 'YX'
    data=ij.Dataset(a,axes=axes,**({'sample_policy':POLICY} if kind!='finite' else {}))
    owner=ij.Document(data);editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    editor.apply_remove_nans(options=ij.RemoveNaNsCommandOptions(1),wait=True)
    expected=a.copy()
    if kind=='scientific':expected[0,0,0,2,2]=7.
    elif kind!='finite':expected[2,2]=7.
    assert owner.snapshot().dataset.numpy(axes).tobytes()==expected.tobytes()
    assert owner.undo_steps==int(kind!='finite')
    if kind!='finite':owner.undo();assert owner.snapshot().dataset.numpy(axes).tobytes()==a.tobytes()
    editor.apply_remove_nans(ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT),ij.RemoveNaNsCommandOptions(1),wait=True)
    assert owner.snapshot().dataset.numpy(axes).tobytes()==a.tobytes()
    assert editor._editor.native_snapshot().dataset.numpy(axes).tobytes()==expected.tobytes()

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float64])
def test_editor_rejects_other_pixel_types(tmp_path,dtype):
    from imagejxx.ui.app import Editor
    owner=ij.Document(ij.Dataset(np.zeros((5,5),dtype),axes='YX'));editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    with pytest.raises(ValueError):editor.apply_remove_nans(wait=True)
    with pytest.raises(ValueError):editor.action('show_operation',operation='remove_nans_imagej')
    assert owner.undo_steps==0
