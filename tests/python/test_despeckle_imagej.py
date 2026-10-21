"""Fixed Despeckle command, actual runner observations, shared editor jobs."""
import copy
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/despeckle-imagej154r.json').read_bytes())


def decode(row,phase):
    return np.stack([np.frombuffer(bytes.fromhex(v),dtype=row['dtype']).reshape(20,25) for v in row[phase+'_hex']])


@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"{r['dtype']}-s{r['signal']}-m{r['mode']}-a{r['selection']}")
def test_runner_fixed_kernel_command_record_and_history(row):
    source=decode(row,'input');expected=decode(row,'output')
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(source,axes='ZYX').observe_reads(reads))
    edit=owner.begin_edit()
    edit.set_extension('future',{'keep':True})
    if row['selection']==1:edit.select(ij.Selection('rectangle',[[2,3],[21,16]]))
    elif row['selection']==2:edit.select(ij.Selection('polygon',[[2,3],[20,2],[18,17],[5,15]]))
    owner.commit(edit)
    display=owner.snapshot().display
    display.crosshair=[12,10,1]
    display.properties={'display_range':row['initial_range'],'lut':'Inverted',
                        'manual_threshold':{'version':1,'lower':4.,'upper':8.,'t':0,'c':0,'z':1}}
    owner.set_display(display);owner=ij.Document(owner.snapshot());before=owner.snapshot()
    request=ij.TargetRequest(ij.TargetScope.ALL_Z if row['mode'] else ij.TargetScope.CURRENT_PLANE)
    command=ij.resolve_despeckle_command(before,request)
    assert command.parameters()=={'method':'median','radius':1.}
    assert ij.rank_command_from_record(before,command.record()).record()==command.record()
    rebound=ij.rebind_rank_command(before,command.record(),request)
    assert rebound.parameters()==command.parameters() and reads.counts().plane_reads==0
    prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters())
    assert prepared.preview.dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert prepared.preview.display.properties==before.display.properties
    assert row['final_range']==row['initial_range'] and row['final_threshold']==[4.,8.]
    assert prepared.preview.metadata['extensions']['future']=={'keep':True}
    if row['selection']!=2:
        extent=(2,3,19,13) if row['selection']==1 else None
        actual=ij.despeckle_imagej(source[1],extent=extent)
        assert actual.dtype==source.dtype and actual.tobytes()==expected[1].tobytes()
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps==int(source.tobytes()!=expected.tobytes())
    if owner.undo_steps:
        owner.undo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==source.tobytes()
        owner.redo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert owner.snapshot().display.properties==before.display.properties


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_fixed_nine_point_footprint_and_empty_extent(dtype):
    pixels=np.array([[0,100,0],[100,0,100],[0,100,0]],dtype)
    assert ij.despeckle_imagej(pixels)[1,1]==0
    assert ij.despeckle_imagej(pixels,extent=(0,0,0,0)).tobytes()==pixels.tobytes()
    with pytest.raises(TypeError):ij.despeckle_imagej(pixels,radius=2)
    with pytest.raises(TypeError):ij.despeckle_imagej(pixels,extent=(True,0,1,1))


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_pre_cancel_and_empty_selection_are_read_free(dtype):
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(np.zeros((4,4),dtype),axes='YX').observe_reads(reads))
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(np.zeros((4,4),np.uint8)));owner.commit(edit)
    owner=ij.Document(owner.snapshot());before=owner.snapshot()
    command=ij.resolve_despeckle_command(before)
    prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters())
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps==0 and reads.counts().plane_reads==0
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_operation('rank_filter_imagej',command.target,command.parameters(),cancellation=token)
    assert owner.undo_steps==0 and reads.counts().plane_reads==0


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_workflow_new_document_session_and_python_editor(tmp_path,dtype):
    from imagejxx.ui.app import Editor
    source=np.zeros((2,2,3,5,5),dtype);source[...,2,2]=80
    owner=ij.Document(ij.Dataset(source,axes='TCZYX'))
    request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(),c=ij.AxisRange(),z=ij.AxisRange())
    command=ij.resolve_despeckle_command(owner.snapshot(),request)
    program=ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('rank_filter_imagej',command.parameters(),request)])
    prepared=ij.prepare_native_workflow(owner.snapshot(),ij.NativeWorkflowProgram.from_json(program.json()).steps)
    owner.commit(prepared.begin_edit());assert not owner.snapshot().dataset.numpy('TCZYX').any()
    owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==source.tobytes()
    editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    editor.apply_despeckle(request,wait=True)
    assert not owner.snapshot().dataset.numpy('TCZYX').any() and owner.undo_steps==1
    path=tmp_path/'despeckle.ijx';ij.save_session(str(path),ij.SessionCapture([owner.snapshot()]))
    assert not ij.load_session(str(path)).documents[0].snapshot().dataset.numpy('TCZYX').any()
    owner.undo();before=owner.snapshot();app=ij.Application();app.adopt(owner)
    new=ij.resolve_despeckle_command(before,ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT))
    proposal=app.prepare('rank_filter_imagej',new.target,new.parameters());proposal.stage_new_document()
    result=app.publish(proposal)
    assert result.created and owner.snapshot().dataset.numpy('TCZYX').tobytes()==source.tobytes()
    assert not result.document.snapshot().dataset.numpy('TCZYX')[0,0,0].any()


@pytest.mark.parametrize('pixels',[np.zeros((3,3),np.float64),np.zeros((3,3,3),np.uint8),np.array([[np.nan]],np.float32),np.array([[np.inf]],np.float32)])
def test_unqualified_pixel_profiles_refuse(pixels):
    with pytest.raises(ValueError):ij.despeckle_imagej(pixels)


@pytest.mark.parametrize('case_id',['R27','R28','R29','R30','R31'])
def test_actual_despeckle_menu_both_imgui_hosts(tmp_path,case_id):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    own=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case_id,'imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads((Path(os.environ['IMAGEJXX_DESPECKLE_CPP_DIR'])/case_id/'case.json').read_bytes())
    for report in [own,cpp]:
        assert report['observation_state']=='observed'
        assert all(f['requested_events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        before=report['fixture_sources']['source'];states=report['states']
        assert all(f['rendered']['rank_panel']['remembered_radii']['median']==2 for f in report['frames'])
        assert all('radius' not in f['rendered']['rank_panel']['controls'] and 'method' not in f['rendered']['rank_panel']['controls'] for f in report['frames'])
        if case_id=='R31':
            after=states['despeckle.cancelled']['documents'][0]
            assert after['undo_steps']==0 and not states['despeckle.cancelled']['workflow']
            assert [p['declared_hex'] for p in after['planes']]==[p['declared_hex'] for p in before['planes']]
            continue
        after=states['despeckle.applied']['documents'][0]
        dtype=np.dtype(before['dtype']);selected={0,1,2} if case_id=='R28' else {10 if case_id=='R29' else 0 if case_id=='R30' else 1}
        for i,p in enumerate(after['planes']):
            if i in selected:
                t,c,z=p['position'];expected=np.full((5,5),10*t+20*c+30*z,dtype)
                assert bytes.fromhex(p['declared_hex'])==expected.tobytes()
            else:assert p['declared_hex']==before['planes'][i]['declared_hex']
        assert after['display']['properties']==before['display']['properties']
        assert after['undo_steps']==1 and states['despeckle.applied']['workflow'][-1]['options']=={'method':'median','radius':1.}
        undone=states['despeckle.undo']['documents'][0];redone=states['despeckle.redo']['documents'][0]
        assert [p['declared_hex'] for p in undone['planes']]==[p['declared_hex'] for p in before['planes']]
        assert [p['declared_hex'] for p in redone['planes']]==[p['declared_hex'] for p in after['planes']]
        if case_id=='R30':assert not any(f['rendered']['rank_panel']['open'] for f in report['frames'])
