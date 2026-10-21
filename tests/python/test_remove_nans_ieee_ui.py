"""IEEE Remove NaNs editor: actual ImGui inputs and independent byte checks."""
import json,os
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
CASES=['R52','R53','R54','R55','R56','R57','R58','R59']
POLICY=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32

def source_plane(position,case):
    t,c,z=position
    a=np.full((5,5),7+10*t+20*c+30*z,np.float32)
    a[1,1]=-0.;a.view(np.uint32)[1,2]=0x7f800002
    a.view(np.uint32)[2,2]=0xff812345;a.view(np.uint32)[0,4]=0x7f800001
    a.view(np.uint32)[0,0]=0x7f800000;a.view(np.uint32)[4,4]=0xff800000
    if case=='R58':a.view(np.uint32)[3,3]=0xff800003
    if case=='R57':a.view(np.uint32)[:]=0x7fc00000
    if case=='R59':a.view(np.uint32)[:]=0xff800001
    return a

def filtered_plane(position,case):
    a=source_plane(position,case)
    if case=='R57':return a
    if case=='R59':
        a.view(np.uint32)[:]=0x7fc00000
        return a
    t,c,z=position;base=7+10*t+20*c+30*z
    if case=='R58':
        # Actual ImageJ 1.54r full-bounds triangle mask captured independently
        # on constant 7: filtered samples are zero, masked-out samples remain 7.
        row=json.loads((Path(__file__).parents[1]/'fixtures/tophat-mask-imagej154r.json').read_bytes())['cases'][0]
        mask=np.frombuffer(bytes.fromhex(row['output_hex']),dtype='<f4').reshape(5,5)==0
        a[mask & np.isnan(a)]=base
    else:
        a[1,2]=base;a[2,2]=base
    return a

def storage_hex(array):
    # Reconstruct the storage codec with integer fields; never cast SNaNs.
    words=array.view(np.uint32);result=np.zeros(words.shape,np.uint64)
    finite=(words & 0x7f800000)!=0x7f800000
    result[finite]=array[finite].astype('<f8').view(np.uint64)
    raw=words[~finite].astype(np.uint64)
    result[~finite]=((raw & 0x80000000)<<32) | np.uint64(0x7ff0000000000000) | ((raw & 0x007fffff)<<29)
    return result.astype('<u8').tobytes().hex()


def plane_hex(document):return [p['declared_hex'] for p in document['planes']]

def check_report(report,case):
    assert report['case_id']==case
    assert report['observation_state']=='observed' and report['acceptance_claim'] is False
    assert all(f['requested_events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert report['final_usage']=={'running':0,'queued':0,'running_bytes':0,'retained_bytes':0,'reserved_bytes':0}
    labels={frame['label'] for frame in report['frames']}
    required={'menu.command.remove_nans_imagej.down','rank.preview.down','remove_nans.radius.text','rank.ok.down'}
    if case=='R52':required.add('rank.stack_no.down')
    if case=='R53':required.add('rank.stack_yes.down')
    if case=='R56':required.add('rank.stack_cancel.down')
    if case=='R55':required.add('rank.new_document.down')
    assert required <= labels
    before=report['fixture_sources']['source'];states=report['states']
    assert before['dirty'] is False
    assert before['sample_policy']=='preserve_ieee_float32' and before['dtype']=='float32'
    for p in before['planes']:
        expected=source_plane(p['position'],case)
        assert p['declared_hex']==expected.tobytes().hex()
        assert p['storage_f64_hex']==storage_hex(expected)
    for f in report['frames']:assert f['rendered']['rank_panel']['remembered_radii']['median']==2.
    panel=next(f['rendered']['rank_panel'] for f in report['frames'] if f['label']=='remove_nans.configured')
    assert panel['preset']=='remove_nans' and panel['radius']==1.
    assert not any(k in panel['controls'] for k in ['method','threshold','which','light_background','dont_subtract'])
    assert report['frames'][-1]['rendered']['rank_panel']['remembered_remove_nans']=={'radius':1.}
    preview=states['remove_nans.preview']['documents'][0]
    assert plane_hex(preview)==plane_hex(before) and preview['undo_steps']==0
    assert preview['display']['properties']==before['display']['properties']
    assert preview['dirty']==before['dirty'] and preview['redo_steps']==before['redo_steps']
    preview_frame=next(f for f in report['frames'] if f['label']=='remove_nans.preview')
    pixels=preview_frame['rendered']['ordinary_pixels'];readback=pixels['readback']['source']
    active=(1,1,1) if case in ['R54','R55'] else (0,0,0 if case in ['R57','R58','R59'] else 1)
    assert readback['encoding']=='IEEE754 Float32LE'
    assert bytes(readback['bytes'])==filtered_plane(active,case).tobytes()
    assert pixels['range']==[7.,127.] and pixels['pixels_source']=='preview'
    if case=='R56':
        after=states['remove_nans.cancelled']['documents'][0]
        assert plane_hex(after)==plane_hex(before) and after['undo_steps']==0 and after['redo_steps']==before['redo_steps']
        assert after['display']['properties']==before['display']['properties'] and not states['remove_nans.cancelled']['workflow']
        assert after['dirty']==before['dirty']
        return
    documents=states['remove_nans.applied']['documents']
    if case=='R55':
        assert len(documents)==2
        original=next(d for d in documents if d['document_id']==before['document_id'])
        assert plane_hex(original)==plane_hex(before) and original['undo_steps']==0 and original['display']['properties']==before['display']['properties']
        after=next(d for d in documents if d['document_id']!=before['document_id'])
    else:after=documents[0]
    selected={0,1,2} if case=='R53' else {10 if case in ['R54','R55'] else 0 if case in ['R57','R58','R59'] else 1}
    for i,p in enumerate(after['planes']):
        expected=filtered_plane(p['position'],case) if i in selected else source_plane(p['position'],case)
        assert p['declared_hex']==expected.tobytes().hex()
    assert after['display']['properties']==before['display']['properties']
    for key in ['shape','dtype','sample_policy','calibration','selection','previous_selection','rois','overlays']:
        assert after[key]==before[key]
    for key in ['properties','extensions','record_layout']:assert after['metadata'][key]==before['metadata'][key]
    if case!='R55':assert after['metadata']['title']==before['metadata']['title']
    if case=='R57':
        assert after['undo_steps']==0 and after['redo_steps']==before['redo_steps'] and not states['remove_nans.applied']['workflow']
        return
    assert after['undo_steps']==1 and after['dirty'] is True
    record=states['remove_nans.applied']['workflow'][-1]
    assert record['operation']=='remove_nans_imagej' and record['options']=={'radius':1.}
    if case=='R55':assert record['target']['destination']=='new_document'
    undone=next(d for d in states['remove_nans.undo']['documents'] if d['document_id']==after['document_id'])
    redone=next(d for d in states['remove_nans.redo']['documents'] if d['document_id']==after['document_id'])
    assert plane_hex(undone)==plane_hex(before) and plane_hex(redone)==plane_hex(after)
    if case!='R55':assert undone['dirty']==before['dirty']
    assert redone['dirty'] is True
    assert undone['display']['properties']==before['display']['properties'] and redone['display']['properties']==after['display']['properties']

@pytest.mark.parametrize('case',CASES)
def test_actual_ieee_remove_nans_both_hosts(tmp_path,case):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case,'imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads((Path(os.environ['IMAGEJXX_IEEE_REMOVE_NANS_CPP_DIR'])/case/'case.json').read_bytes())
    check_report(python,case);check_report(cpp,case)

