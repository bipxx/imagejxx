"""ImageJ Top Hat apply command; physical preview/UI remains separate."""
import copy
import json
import os
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest

FIXTURES=Path(__file__).parents[1]/'fixtures'
PIXELS=json.loads((FIXTURES/'tophat-imagej154r.json').read_bytes())['cases']

def decode(row,phase):
    return np.stack([np.frombuffer(bytes.fromhex(x),dtype=row['dtype']).reshape(20,25) for x in row[phase+'_hex']])

@pytest.mark.parametrize('row',PIXELS,ids=lambda r:f"{r['dtype']}-s{r['signal']}-r{r['radius']}-b{int(r['light_background'])}-d{int(r['dont_subtract'])}-i{int(r['inverted_lut'])}-m{r['mode']}-a{r['selection']}")
def test_runner_command_and_history(row):
    source=decode(row,'input');reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(source,axes='ZYX').observe_reads(reads))
    if row['selection']:
        edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[[2,3],[21,16]]) if row['selection']==1 else ij.Selection('polygon',[[2,3],[20,2],[18,17],[5,15]]));owner.commit(edit)
    view=owner.snapshot().display;view.crosshair=[12,10,1]
    view.properties={'lut':'Inverted' if row['inverted_lut'] else 'Grays','display_range':[7.,127.],
                     'manual_threshold':{'version':1,'lower':4.,'upper':8.,'t':0,'c':0,'z':1}}
    owner.set_display(view);owner=ij.Document(owner.snapshot());before=owner.snapshot()
    request=ij.TargetRequest(ij.TargetScope.ALL_Z if row['mode'] else ij.TargetScope.CURRENT_PLANE)
    command=ij.resolve_top_hat_command(before,request,ij.TopHatCommandOptions(row['radius'],row['light_background'],row['dont_subtract']))
    assert reads.counts().plane_reads==0 and command.operation=='top_hat_imagej'
    assert command.inverted_lut==row['inverted_lut']
    assert command.parameters()=={'radius':row['radius'],'light_background':row['light_background'],'dont_subtract':row['dont_subtract'],'inverted_lut':row['inverted_lut']}
    assert ij.rank_command_from_record(before,command.record()).record()==command.record()
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    expected=decode(row,'output')
    assert prepared.preview.dataset.numpy('ZYX').tobytes()==expected.tobytes()
    properties=before.display.properties
    properties['display_range']=row['final_range']
    if row['final_threshold'] is None:properties.pop('manual_threshold')
    assert prepared.preview.display.properties==properties
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps==int(source.tobytes()!=expected.tobytes() or properties!=before.display.properties)
    if owner.undo_steps:
        owner.undo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==source.tobytes();assert owner.snapshot().display.properties==before.display.properties
        owner.redo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert owner.snapshot().display.properties==properties


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_new_document_and_display_only_noop(dtype):
    source=np.zeros((5,5),dtype)
    owner=ij.Document(ij.Dataset(source,axes='YX'));view=owner.snapshot().display
    view.properties={'display_range':[7.,127.],'manual_threshold':{'version':1,'lower':4.,'upper':8.,'t':0,'c':0,'z':0}}
    owner.set_display(view);before=owner.snapshot()
    command=ij.resolve_top_hat_command(before,options=ij.TopHatCommandOptions(1))
    proposal=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert proposal.preview.dataset.numpy('YX').tobytes()==source.tobytes()
    assert 'manual_threshold' not in proposal.preview.display.properties
    owner.commit(proposal.begin_edit());assert owner.undo_steps==1
    owner.undo();assert owner.snapshot().display.properties==before.display.properties
    command=ij.resolve_top_hat_command(owner.snapshot(),ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT))
    app=ij.Application();app.adopt(owner);proposal=app.prepare(command.operation,command.target,command.parameters())
    proposal.stage_new_document();created=app.publish(proposal)
    assert created.created and created.document.id!=owner.id
    assert owner.snapshot().display.properties==before.display.properties
    assert 'manual_threshold' not in created.document.snapshot().display.properties
    output_properties=created.document.snapshot().display.properties
    created.document.undo();assert created.document.snapshot().display.properties==before.display.properties
    created.document.redo();assert created.document.snapshot().display.properties==output_properties

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_frozen_rebind_and_strict_record(dtype):
    source=np.arange(25,dtype=dtype).reshape(5,5);owner=ij.Document(ij.Dataset(source,axes='YX'))
    view=owner.snapshot().display;view.properties={'lut':'Inverted'};owner.set_display(view)
    command=ij.resolve_top_hat_command(owner.snapshot(),options=ij.TopHatCommandOptions(1,False,True))
    assert command.top_hat_options.radius==1 and command.outlier_options is None
    assert command.inverted_lut is True
    other=ij.Document(ij.Dataset(source,axes='YX'))
    rebound=ij.rebind_rank_command(other.snapshot(),command.record(),ij.TargetRequest())
    assert rebound.inverted_lut and rebound.parameters()==command.parameters()
    assert not ij.resolve_top_hat_command(other.snapshot()).inverted_lut
    for field,value in [('radius',True),('radius',1001),('light_background',1),('dont_subtract',0),('inverted_lut','false'),('method','minimum')]:
        record=copy.deepcopy(command.record());record['options'][field]=value
        with pytest.raises((ValueError,TypeError)):ij.rank_command_from_record(owner.snapshot(),record)
        with pytest.raises((ValueError,TypeError)):ij.rebind_rank_command(other.snapshot(),record,ij.TargetRequest())
    with pytest.raises(ValueError):ij.rank_command_from_record(other.snapshot(),command.record())

@pytest.mark.parametrize('kwargs,error',[
 ({'radius':True},TypeError),({'radius':1001},ValueError),({'radius':-1},ValueError),
 ({'light_background':1},TypeError),({'dont_subtract':'false'},TypeError)])
def test_options_admission(kwargs,error):
    with pytest.raises(error):ij.TopHatCommandOptions(**kwargs)

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_empty_selection_and_off_active_threshold(dtype):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((2,5,5),dtype),axes='ZYX').observe_reads(reads))
    edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[[8,8],[9,9]]));owner.commit(edit)
    before=owner.snapshot();command=ij.resolve_top_hat_command(before)
    assert reads.counts().plane_reads==0
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert reads.counts().plane_reads==0 and prepared.preview.display.properties==before.display.properties
    owner=ij.Document(ij.Dataset(np.zeros((2,5,5),dtype),axes='ZYX'));view=owner.snapshot().display
    view.properties={'manual_threshold':{'version':1,'lower':4.,'upper':8.,'t':0,'c':0,'z':1}}
    owner.set_display(view);command=ij.resolve_top_hat_command(owner.snapshot())
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert prepared.preview.display.properties['manual_threshold']==view.properties['manual_threshold']

CHANNELS=json.loads((FIXTURES/'tophat-channel-imagej154r.json').read_bytes())['cases']
@pytest.mark.parametrize('row',CHANNELS,ids=lambda r:f"{r['dtype']}-m{r['mode']}-s{r['signal']}-a{r['all']}-h{r['hosted']}-d{int(r['dont_subtract'])}")
def test_actual_channel_command(row):
    source=np.stack([np.frombuffer(bytes.fromhex(x),dtype=row['dtype']).reshape(5,5) for x in row['input_hex']]).reshape(2,2,5,5).transpose(1,0,2,3)
    owner=ij.Document(ij.Dataset(source,axes='CZYX'));view=owner.snapshot().display;view.c=1;view.crosshair=[2,2,1]
    state={'schema':'imagejxx.channel-display','version':1,'mode':{1:'composite',2:'color',3:'grayscale'}[row['mode']],
           'channels':[{'visible':True,'range':v,'lut':{'name':'Red' if c==0 else 'Green'}} for c,v in enumerate(row['initial_lut_ranges'])]}
    view=ij.with_channel_display(view,owner.snapshot().dataset,state)
    props=view.properties;props['manual_threshold']={'version':1,'lower':4.,'upper':8.,'t':0,'c':1,'z':1};view.properties=props
    owner.set_display(view);before=owner.snapshot()
    request=ij.TargetRequest(ij.TargetScope.RANGES,c=ij.AxisRange(),z=ij.AxisRange()) if row['all'] else ij.TargetRequest()
    command=ij.resolve_top_hat_command(before,request,ij.TopHatCommandOptions(1,False,row['dont_subtract']))
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    expected=np.stack([np.frombuffer(bytes.fromhex(x),dtype=row['dtype']).reshape(5,5) for x in row['output_hex']]).reshape(2,2,5,5).transpose(1,0,2,3)
    assert prepared.preview.dataset.numpy('CZYX').tobytes()==expected.tobytes()
    state['channels'][1]['range']=row['final_processor_range']
    expected_view=ij.with_channel_display(before.display,prepared.preview.dataset,state)
    props=expected_view.properties
    if row['final_threshold'] is None:props.pop('manual_threshold')
    assert prepared.preview.display.properties==props
    owner.commit(prepared.begin_edit())
    if owner.undo_steps:
        owner.undo();assert owner.snapshot().display.properties==before.display.properties
        owner.redo();assert owner.snapshot().display.properties==props

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_cancellation_and_scientific_ranges(dtype):
    source=np.arange(2*2*3*5*5,dtype=np.uint16).reshape(2,2,3,5,5)%127;source=source.astype(dtype)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(source,axes='TCZYX').observe_reads(reads))
    request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(1,2),c=ij.AxisRange(0,1),z=ij.AxisRange(1,3))
    command=ij.resolve_top_hat_command(owner.snapshot(),request,ij.TopHatCommandOptions(1,True,True))
    assert reads.counts().plane_reads==0
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_operation(command.operation,command.target,command.parameters(),cancellation=token)
    assert reads.counts().plane_reads==0
    result=ij.prepare_operation(command.operation,command.target,command.parameters())
    expected=source.copy()
    for z in [1,2]:expected[1,0,z]=ij.top_hat_imagej(source[1,0,z],1,light_background=True,dont_subtract=True)
    assert result.preview.dataset.numpy('TCZYX').tobytes()==expected.tobytes()

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_native_workflow_frozen_record_and_history(dtype,tmp_path):
    source=np.arange(50,dtype=dtype).reshape(2,5,5)%17
    owner=ij.Document(ij.Dataset(source,axes='ZYX'));view=owner.snapshot().display;view.properties={'lut':'Inverted','display_range':[0.,127.]};owner.set_display(view)
    request=ij.TargetRequest(ij.TargetScope.ALL_Z)
    command=ij.resolve_top_hat_command(owner.snapshot(),request,ij.TopHatCommandOptions(1,False,True))
    program=ij.NativeWorkflowProgram([ij.NativeWorkflowOperation(command.operation,{},request,original=command.record())])
    loaded=ij.NativeWorkflowProgram.from_json(program.json())
    view.properties={'lut':'Grays','display_range':[0.,127.]};owner.set_display(view)
    result=ij.prepare_native_workflow(owner.snapshot(),loaded.steps)
    expected=np.stack([ij.top_hat_imagej(p,1,dont_subtract=True,inverted_lut=True) for p in source])
    assert result.preview.dataset.numpy('ZYX').tobytes()==expected.tobytes()
    owner.commit(result.begin_edit());owner.undo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==source.tobytes()
    owner.redo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()
    path=tmp_path/'tophat.ijx';ij.save_session(str(path),ij.SessionCapture([owner.snapshot()]))
    assert ij.load_session(str(path)).documents[0].snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()

def test_off_active_byte_does_not_clear_active_threshold():
    source=np.arange(50,dtype=np.uint8).reshape(2,5,5)
    owner=ij.Document(ij.Dataset(source,axes='ZYX'));view=owner.snapshot().display
    view.properties={'display_range':[7.,127.],'manual_threshold':{'version':1,'lower':4.,'upper':8.,'t':0,'c':0,'z':0}}
    owner.set_display(view)
    request=ij.TargetRequest(ij.TargetScope.RANGES,z=ij.AxisRange(1,2))
    command=ij.resolve_top_hat_command(owner.snapshot(),request)
    result=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert result.preview.display.properties==view.properties
