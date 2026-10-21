"""Nonbyte editor publication: runner observations and explicit owned policies."""
import copy,json,os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/variance-display-imagej154r.json').read_bytes())

def decode(row,phase):
    return np.stack([np.frombuffer(bytes.fromhex(v),dtype=row['dtype']).reshape(20,25) for v in row[phase+'_hex']])[None,None]

def fixture(row):
    pixels=decode(row,'input');reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYX').observe_reads(reads))
    edit=owner.begin_edit();edit.set_extension('future',{'keep':True})
    if row['selection']==1:edit.select(ij.Selection('rectangle',[[2,3],[21,16]]))
    elif row['selection']==2:edit.select(ij.Selection('polygon',[[2,3],[20,2],[18,17],[5,15]]))
    owner.commit(edit)
    display=owner.snapshot().display;display.crosshair=[12,10,1];display.zoom=2.5
    properties={'lut':'Inverted','unrelated':{'keep':True}}
    if row['custom']:properties['display_range']=row['initial_range']
    display.properties=properties
    owner.set_display(display);owner=ij.Document(owner.snapshot())
    request=ij.TargetRequest(ij.TargetScope.ALL_Z if row['mode'] else ij.TargetScope.CURRENT_PLANE)
    return owner,reads,request,pixels

@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"{r['dtype']}-s{r['signal']}-m{r['mode']}-a{r['selection']}-c{r['custom']}-r{r['radius']}")
def test_runner_pixels_final_range_and_paired_history(row):
    owner,reads,request,pixels=fixture(row);before=owner.snapshot()
    command=ij.resolve_rank_command(before,request,ij.RankFilterCommandOptions('variance',row['radius']))
    record=command.record();saved=copy.deepcopy(record)
    assert ij.rank_command_from_record(before,record).record()==record and reads.counts().plane_reads==0
    prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters())
    out=prepared.preview;expected=decode(row,'output')
    assert out.dataset.numpy('TCZYX').tobytes()==expected.tobytes()
    assert out.display.properties['display_range']==row['final_range']
    assert out.display.zoom==2.5 and out.display.crosshair==before.display.crosshair
    assert out.display.properties['lut']=='Inverted' and out.display.properties['unrelated']=={'keep':True}
    assert out.metadata['extensions']['future']=={'keep':True} and prepared.begin_edit().dependencies==ij.Change.ALL
    assert owner.undo_steps==0 and owner.snapshot().display.properties==before.display.properties
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==pixels.tobytes()
    assert owner.snapshot().display.properties==before.display.properties
    owner.redo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==expected.tobytes()
    assert owner.snapshot().display.properties['display_range']==row['final_range'] and record==saved

INITIAL=json.loads((Path(__file__).parents[1]/'fixtures/variance-initial-range-imagej154r.json').read_bytes())

@pytest.mark.parametrize('row',INITIAL['cases'],ids=lambda r:f"{r['dtype']}-s{r['signal']}-c{r['choice']}")
def test_initial_range_processor_rounding_and_zero_reset(row):
    dtype=np.dtype(row['dtype']);pixels=np.full((4,8),7,dtype)
    if row['signal']:
        if dtype==np.dtype('uint16'):pixels.reshape(-1)[1::2]=65535;pixels.reshape(-1)[::2]=0
        else:pixels[:]=1000000
    owner=ij.Document(ij.Dataset(pixels,axes='YX'));display=owner.snapshot().display
    if row['choice']!=3:display.properties={'display_range':row['requested']}
    owner.set_display(display);before=owner.snapshot()
    prepared=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(before),{'method':'variance','radius':0})
    assert prepared.preview.display.properties['display_range']==row['final']
    owner.commit(prepared.begin_edit());owner.undo()
    assert owner.snapshot().display.properties==before.display.properties
    assert owner.snapshot().dataset.numpy('YX').tobytes()==pixels.tobytes()

@pytest.mark.parametrize('dtype,bounds',[(np.uint16,[1.e30,2.e30]),(np.float32,[1.e39,2.e39])])
def test_unqualified_processor_display_bounds_refuse_before_reads(dtype,bounds):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((3,3),dtype),axes='YX').observe_reads(reads))
    display=owner.snapshot().display;display.properties={'display_range':bounds};owner.set_display(display)
    with pytest.raises(ValueError):ij.resolve_rank_command(owner.snapshot(),options=ij.RankFilterCommandOptions('variance',0))
    with pytest.raises(ValueError):ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':0})
    assert reads.counts().plane_reads==0

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_legacy_variance_empty_mask_keeps_owner(tmp_path,dtype):
    from imagejxx.ui.app import Editor
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((4,4),dtype),axes='YX').observe_reads(reads))
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(np.zeros((4,4),np.uint8)));owner.commit(edit)
    owner=ij.Document(owner.snapshot());before=owner.snapshot()
    editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    editor.apply_operation('rank_filter_imagej',{'method':'variance','radius':0},asynchronous=False)
    assert owner.undo_steps==0 and owner.snapshot().display.properties==before.display.properties
    assert reads.counts().plane_reads==0

EQUAL=json.loads((Path(__file__).parents[1]/'fixtures/equal-range-imagej154r.json').read_bytes())

@pytest.mark.parametrize('row',EQUAL['cases'],ids=lambda r:f"{r['dtype']}-{r['requested']}")
def test_detached_display_uses_cpp_equal_range_mapping(row):
    from imagejxx.ui.rendering import display_pixels
    source=np.asarray(row['input'],dtype=row['dtype'])[None,:];before=source.tobytes()
    expected=np.repeat(np.asarray(row['display'],np.uint8)[None,:,None],3,axis=2)
    actual=display_pixels(source,display_range=(row['requested'],row['requested']))
    np.testing.assert_array_equal(actual,expected)
    np.testing.assert_array_equal(display_pixels(source,display_range=(row['requested'],row['requested']),lut='Inverted'),255-expected)
    assert source.tobytes()==before

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_equal_range_native_and_detached_session_roundtrip(tmp_path,dtype):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import save_session,load_session
    from imagejxx.ui.rendering import render_document
    owner=ij.Document(ij.Dataset(np.full((4,4),7,dtype),axes='YX'))
    prepared=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':0})
    owner.commit(prepared.begin_edit());path=tmp_path/'native.ijx'
    ij.save_session(str(path),ij.SessionCapture([owner.snapshot()]))
    decoded=ij.load_session(str(path));assert decoded.documents[0].snapshot().display.properties['display_range']==[7.,7.]
    editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(decoded.documents[0])
    detached=editor.documents[0];assert detached.display_range==(7.,7.)
    assert not np.asarray(render_document(detached,overlays=False)).any()
    legacy=tmp_path/'detached.ijxs';save_session(legacy,[detached])
    restored,_,_=load_session(legacy);assert restored[0].display_range==(7.,7.)
    assert not np.asarray(render_document(restored[0],overlays=False)).any()

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_null_range_uses_implicit_source_range(dtype):
    owner=ij.Document(ij.Dataset(np.full((3,3),7,dtype),axes='YX'))
    display=owner.snapshot().display;display.properties={'display_range':None};owner.set_display(display)
    prepared=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':0})
    owner.commit(prepared.begin_edit());assert owner.snapshot().display.properties['display_range']==[7.,7.]
    owner.undo();assert owner.snapshot().display.properties=={'display_range':None}

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_display_only_variance_has_paired_history_and_repeat_is_noop(dtype):
    owner=ij.Document(ij.Dataset(np.zeros((3,3),dtype),axes='YX'));source=owner.snapshot()
    prepared=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(source),{'method':'variance','radius':0})
    assert prepared.preview.dataset.shares_pixels(source.dataset)
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    assert owner.snapshot().display.properties['display_range']==[0.,0.]
    owner.undo();assert owner.snapshot().display.properties==source.display.properties
    owner.redo()
    again=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':0})
    owner.commit(again.begin_edit());assert owner.undo_steps==1

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_empty_write_is_read_free_and_keeps_display(dtype):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((4,4),dtype),axes='YX').observe_reads(reads))
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(np.zeros((4,4),np.uint8)));owner.commit(edit)
    owner=ij.Document(owner.snapshot());before=owner.snapshot()
    prepared=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(before),{'method':'variance','radius':1})
    assert reads.counts().plane_reads==0 and prepared.outputs[0].skipped
    owner.commit(prepared.begin_edit());assert owner.undo_steps==0 and owner.snapshot().display.properties==before.display.properties

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_off_current_target_uses_untouched_active_plane_for_final_range(dtype):
    pixels=np.stack([np.arange(500,dtype=dtype).reshape(20,25),np.full((20,25),7,dtype)])
    owner=ij.Document(ij.Dataset(pixels,axes='ZYX'));display=owner.snapshot().display
    display.crosshair=[0,0,1];display.properties={'display_range':[2.,100.]};owner.set_display(display)
    request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(0,0,0)],roi=ij.RoiPolicy.WHOLE_PLANE)
    command=ij.resolve_rank_command(owner.snapshot(),request,ij.RankFilterCommandOptions('variance',1))
    prepared=ij.prepare_operation('rank_filter_imagej',command.target,command.parameters())
    assert prepared.preview.display.properties['display_range']==[2.,100.]
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('ZYX')[1],pixels[1])

@pytest.mark.parametrize('state',['unknown_channel','bad_range'])
def test_unqualified_display_state_refuses_before_samples(state):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((3,3),np.uint16),axes='YX').observe_reads(reads))
    display=owner.snapshot().display
    if state=='bad_range':display.properties={'display_range':[2.,1.]}
    elif state=='unknown_channel':display.properties={'channel_display':{'version':999}}
    owner.set_display(display)
    with pytest.raises(ValueError):ij.resolve_rank_command(owner.snapshot(),options=ij.RankFilterCommandOptions('variance',1))
    with pytest.raises(ValueError):ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':1})
    assert reads.counts().plane_reads==0 and owner.undo_steps==0

def test_overflow_and_postfilter_cancel_do_not_publish():
    source=np.array([[np.finfo(np.float32).max,-np.finfo(np.float32).max]],np.float32)
    owner=ij.Document(ij.Dataset(source,axes='YX'));before=owner.snapshot()
    with pytest.raises(ValueError,match='finite Float32 output'):
        ij.prepare_operation('rank_filter_imagej',ij.resolve_target(before),{'method':'variance','radius':0})
    assert owner.undo_steps==0 and owner.snapshot().display.properties==before.display.properties
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'),source)
    safe=ij.Document(ij.Dataset(np.arange(100,dtype=np.uint16).reshape(10,10),axes='YX'))
    token=ij.CancellationToken()
    def progress(done,total):
        if done==total and done>0:token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_operation('rank_filter_imagej',ij.resolve_target(safe.snapshot()),{'method':'variance','radius':0},cancellation=token,progress=progress)
    assert safe.undo_steps==0 and safe.snapshot().display.properties=={}

def test_variance_histogram_keeps_stack_plane_progress():
    pixels=np.ones((3,4,4),np.uint16);owner=ij.Document(ij.Dataset(pixels,axes='ZYX'))
    command=ij.resolve_rank_command(owner.snapshot(),ij.TargetRequest(ij.TargetScope.ALL_Z),ij.RankFilterCommandOptions('variance',0))
    progress=[]
    ij.prepare_operation('rank_filter_imagej',command.target,command.parameters(),progress=lambda done,total:progress.append((done,total)))
    assert progress==[(0,3),(1,3),(2,3),(3,3)]

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_nonbyte_variance_guards_concurrent_display(dtype):
    owner=ij.Document(ij.Dataset(np.arange(25,dtype=dtype).reshape(5,5),axes='YX'))
    prepared=ij.prepare_operation('rank_filter_imagej',ij.resolve_target(owner.snapshot()),{'method':'variance','radius':1})
    display=owner.snapshot().display;display.zoom=3;owner.set_display(display)
    with pytest.raises(ij.RevisionConflict):owner.commit(prepared.begin_edit())
    assert owner.undo_steps==0 and owner.snapshot().display.zoom==3

@pytest.mark.parametrize('dtype',[np.uint16,np.float32])
def test_workflow_and_new_document_keep_range_pair(dtype):
    pixels=np.arange(25,dtype=dtype).reshape(5,5);owner=ij.Document(ij.Dataset(pixels,axes='YX'))
    step=ij.NativeWorkflowOperation('rank_filter_imagej',{'method':'variance','radius':1},ij.TargetRequest())
    program=ij.NativeWorkflowProgram([step]);loaded=ij.NativeWorkflowProgram.from_json(program.json())
    prepared=ij.prepare_native_workflow(owner.snapshot(),loaded.steps)
    owner.commit(prepared.begin_edit());final=owner.snapshot()
    assert 'display_range' in final.display.properties
    owner.undo();assert owner.snapshot().display.properties=={}
    owner.redo();assert owner.snapshot().display.properties==final.display.properties
    app=ij.Application();app.adopt(owner);before=owner.snapshot()
    request=ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    command=ij.resolve_rank_command(before,request,ij.RankFilterCommandOptions('variance',1))
    proposal=app.prepare('rank_filter_imagej',command.target,command.parameters());proposal.stage_new_document()
    result=app.publish(proposal);assert result.created and owner.snapshot().display.properties==before.display.properties
    assert result.document.snapshot().display.properties==proposal.preview.display.properties

@pytest.mark.parametrize('case_id',['R15','R16','R17','R18'])
def test_actual_nonbyte_variance_both_imgui_hosts(tmp_path,case_id):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    own=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case_id,'imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads((Path(os.environ['IMAGEJXX_NONBYTE_CPP_DIR'])/case_id/'case.json').read_bytes())
    for report in [own,cpp]:
        assert report['observation_state']=='observed'
        assert all(frame['requested_events']==frame['rendered']['input_frame']['raw_inputs'] for frame in report['frames'])
        before=report['fixture_sources']['source'];applied=report['states']['variance.applied']['documents'][0]
        undone=report['states']['variance.undo']['documents'][0];redone=report['states']['variance.redo']['documents'][0]
        dtype=np.dtype('uint16' if case_id in ['R15','R17'] else 'float32')
        expected=np.zeros((5,5),dtype) if case_id in ['R17','R18'] else np.array([[30,30,30,30,30],[30,0,1024,0,30],[30,1024,1024,1024,30],[30,0,1024,0,30],[30,30,30,30,30]],dtype)
        actual=np.frombuffer(bytes.fromhex(applied['planes'][1]['declared_hex']),dtype).reshape(5,5)
        assert actual.tobytes()==expected.tobytes()
        preview=next(f['rendered']['ordinary_pixels']['readback']['source'] for f in report['frames'] if f['label']=='variance.preview')
        assert np.asarray(preview['pixels'],dtype=dtype).reshape(5,5).tobytes()==expected.tobytes()
        expected_range=[7.,7.] if case_id in ['R17','R18'] else [0.,1024. if case_id=='R15' else 1020.]
        assert applied['display']['properties']['display_range']==expected_range
        assert undone['display']['properties']==before['display']['properties']
        assert redone['display']['properties']==applied['display']['properties']
        assert [p['declared_hex'] for p in undone['planes']]==[p['declared_hex'] for p in before['planes']]
        assert [p['declared_hex'] for p in redone['planes']]==[p['declared_hex'] for p in applied['planes']]
        assert applied['undo_steps']==1 and report['states']['variance.applied']['workflow'][-1]['options']['method']=='variance'
