"""Remove NaNs immutable intent, execution, history and workflow coverage."""
import copy,json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
POLICY=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
ROWS=json.loads((Path(__file__).parents[1]/'fixtures/remove-nans-imagej154r.json').read_bytes())['cases']
def dataset(a):return ij.Dataset(a,axes='ZYX' if a.ndim==3 else 'YX',sample_policy=POLICY)
def decode(row,key):return np.stack([np.frombuffer(bytes.fromhex(v),dtype='<f4').reshape(20,25) for v in row[key+'_hex']])
def props(z=0):return {'display_range':[7.,127.],'manual_threshold':{'version':1,'lower':4.,'upper':8.,'t':0,'c':0,'z':z}}
def example():
    a=np.full((5,5),7.,np.float32);a.view(np.uint32)[2,2]=0xffc12345;a[0,0]=-0.;return a
@pytest.mark.parametrize('legacy',[False,True],ids=['scientific','legacy'])
@pytest.mark.parametrize('row',ROWS,ids=lambda r:f"s{r['signal']}-r{r['radius']}-m{r['mode']}-a{r['selection']}")
def test_runner_apply_and_history(row,legacy):
    source=decode(row,'input')
    if row['signal']==5:
        with pytest.raises(ValueError):dataset(source)
        return
    reads=ij.DatasetReadObserver();owner=ij.Document(dataset(source).observe_reads(reads))
    if row['selection']:
        edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[[2,3],[21,16]]) if row['selection']==1 else ij.Selection('polygon',[[2,3],[20,2],[18,17],[5,15]]));owner.commit(edit)
    view=owner.snapshot().display;view.crosshair=[12,10,1];view.properties=props(1);owner.set_display(view);owner=ij.Document(owner.snapshot());before=owner.snapshot()
    request=ij.TargetRequest(ij.TargetScope.ALL_Z if row['mode'] else ij.TargetScope.CURRENT_PLANE)
    command=ij.resolve_remove_nans_command(before,request,ij.RemoveNaNsCommandOptions(row['radius']))
    assert reads.counts().plane_reads==0 and command.operation=='remove_nans_imagej'
    assert command.remove_nans_options.radius==row['radius'] and command.outlier_options is None and command.top_hat_options is None
    assert command.parameters()=={'radius':row['radius']} and command.record()['options']==command.parameters()
    assert ij.rank_command_from_record(before,command.record()).record()==command.record()
    assert command.region()['extent']==ij.remove_nans_command_region(command.target)['extent']
    with pytest.raises(ValueError):ij.rank_command_region(command.target)
    execution=ij.ExecutionOptions()
    if legacy:execution.dataset=ij.DatasetPolicy.LEGACY_DESKTOP;execution.geometry=ij.GeometryPolicy.LEGACY_DESKTOP
    result=ij.prepare_operation(command.operation,command.target,command.parameters(),options=execution)
    expected=decode(row,'output')
    assert result.preview.dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert result.preview.dataset.sample_policy==POLICY and before.dataset.numpy('ZYX').tobytes()==source.tobytes()
    assert result.preview.display.properties==props(1)
    owner.commit(result.begin_edit());changed=source.tobytes()!=expected.tobytes()
    assert owner.undo_steps==int(changed)
    if changed:
        owner.undo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==source.tobytes()
        assert owner.snapshot().display.properties==props(1)
        owner.redo()
    assert owner.snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert owner.snapshot().display.properties==props(1)
@pytest.mark.parametrize('radius,error',[(-1,ValueError),(100.1,ValueError),(np.nan,ValueError),(np.inf,ValueError),(True,TypeError),('2',TypeError)])
def test_options(radius,error):
    with pytest.raises(error):ij.RemoveNaNsCommandOptions(radius)
@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float64])
def test_source_admission(dtype):
    owner=ij.Document(ij.Dataset(np.zeros((5,5),dtype),axes='YX'))
    with pytest.raises(ValueError):ij.resolve_remove_nans_command(owner.snapshot())
@pytest.mark.parametrize('field,value',[('radius',True),('radius',101),('radius',-1),('method','median'),('inverted_lut',False),('threshold',0)])
def test_strict_record(field,value):
    owner=ij.Document(dataset(example()));other=ij.Document(dataset(example()))
    command=ij.resolve_remove_nans_command(owner.snapshot(),options=ij.RemoveNaNsCommandOptions(1))
    bad=copy.deepcopy(command.record());bad['options'][field]=value
    for action in [lambda:ij.validate_rank_command_record(bad),lambda:ij.rank_command_from_record(owner.snapshot(),bad),lambda:ij.rebind_rank_command(other.snapshot(),bad,ij.TargetRequest())]:
        with pytest.raises((ValueError,TypeError)):action()
    with pytest.raises(ValueError):ij.rank_command_from_record(other.snapshot(),command.record())
    rebound=ij.rebind_rank_command(other.snapshot(),command.record(),ij.TargetRequest())
    assert rebound.parameters()=={'radius':1.} and rebound.target.document_id==other.id
@pytest.mark.parametrize('legacy',[False,True])
def test_new_output_and_preview(legacy):
    a=example();owner=ij.Document(dataset(a));view=owner.snapshot().display;view.properties=props();owner.set_display(view);before=owner.snapshot()
    app=ij.Application();app.adopt(owner)
    request=ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    command=ij.resolve_remove_nans_command(before,request,ij.RemoveNaNsCommandOptions(1))
    execution=ij.ExecutionOptions()
    if legacy:execution.dataset=ij.DatasetPolicy.LEGACY_DESKTOP;execution.geometry=ij.GeometryPolicy.LEGACY_DESKTOP
    result=app.prepare(command.operation,command.target,command.parameters(),options=execution);result.stage_new_document();created=app.publish(result)
    assert created.created and created.document.id!=owner.id and owner.undo_steps==0
    assert owner.snapshot().dataset.numpy('YX').tobytes()==a.tobytes() and owner.snapshot().display.properties==props()
    assert created.document.snapshot().dataset.numpy('YX')[2,2]==7. and created.document.snapshot().display.properties==props()
    created.document.undo();assert created.document.snapshot().dataset.numpy('YX').tobytes()==a.tobytes()
    assert created.document.snapshot().display.properties==props()
    created.document.redo();assert created.document.snapshot().dataset.numpy('YX')[2,2]==7.
    command=ij.resolve_remove_nans_command(before)
    execution.phase=ij.ExecutionPhase.PREVIEW
    transient=app.prepare(command.operation,command.target,command.parameters(),options=execution)
    for action in [transient.begin_edit,transient.stage_new_document,lambda:app.publish(transient)]:
        with pytest.raises(ValueError):action()
    assert owner.snapshot().dataset.numpy('YX').tobytes()==a.tobytes()
@pytest.mark.parametrize('payload',[0x7fc00000,0xffc12345])
def test_registry_preserves_payload_and_zero(payload):
    a=np.zeros((5,5),np.float32);a.view(np.uint32)[:]=payload;a[0,0]=-0.
    assert ij.validate_operation('remove_nans_imagej',a,{'radius':0})=={'radius':0.}
    actual=ij.run_operation('remove_nans_imagej',a,{'radius':0}).pixels
    assert actual.tobytes()==ij.remove_nans_imagej(a,0).tobytes()
    with pytest.raises(ValueError):ij.run_operation('rank_filter_imagej',a)
@pytest.mark.parametrize('legacy',[False,True])
def test_empty_selection_no_read_and_no_history(legacy):
    reads=ij.DatasetReadObserver();owner=ij.Document(dataset(example()).observe_reads(reads))
    edit=owner.begin_edit();edit.select(ij.Selection('rectangle',[[8,8],[9,9]]));owner.commit(edit);owner=ij.Document(owner.snapshot())
    command=ij.resolve_remove_nans_command(owner.snapshot());execution=ij.ExecutionOptions()
    if legacy:execution.dataset=ij.DatasetPolicy.LEGACY_DESKTOP;execution.geometry=ij.GeometryPolicy.LEGACY_DESKTOP
    result=ij.prepare_operation(command.operation,command.target,command.parameters(),options=execution)
    assert reads.counts().plane_reads==0
    owner.commit(result.begin_edit());assert owner.undo_steps==0
@pytest.mark.parametrize('capture',[False,True],ids=['registry','captured'])
def test_workflow_session_and_atomic_history(tmp_path,capture):
    a=np.stack([example(),example()]);owner=ij.Document(dataset(a));request=ij.TargetRequest(ij.TargetScope.ALL_Z)
    command=ij.resolve_remove_nans_command(owner.snapshot(),request,ij.RemoveNaNsCommandOptions(1))
    step=ij.NativeWorkflowOperation(command.operation,{} if capture else command.parameters(),request,**({'original':command.record()} if capture else {}))
    program=ij.NativeWorkflowProgram([step]);loaded=ij.NativeWorkflowProgram.from_json(program.json())
    result=ij.prepare_native_workflow(owner.snapshot(),loaded.steps);expected=np.stack([ij.remove_nans_imagej(p,1) for p in a])
    assert result.preview.dataset.numpy('ZYX').tobytes()==expected.tobytes()
    owner.commit(result.begin_edit());owner.undo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==a.tobytes()
    owner.redo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()
    path=tmp_path/'remove-nans.ijx';ij.save_session(str(path),ij.SessionCapture([owner.snapshot()]))
    reopened=ij.load_session(str(path)).documents[0].snapshot()
    assert reopened.dataset.sample_policy==POLICY and reopened.dataset.numpy('ZYX').tobytes()==expected.tobytes()
def test_scientific_explicit_positions():
    a=np.stack([example() for _ in range(12)]).reshape(2,2,3,5,5)
    owner=ij.Document(ij.Dataset(a,axes='TCZYX',sample_policy=POLICY))
    request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,0,2),ij.PlanePosition(0,1,1)])
    command=ij.resolve_remove_nans_command(owner.snapshot(),request,ij.RemoveNaNsCommandOptions(1))
    result=ij.prepare_operation(command.operation,command.target,command.parameters());expected=a.copy()
    for t,c,z in [(1,0,2),(0,1,1)]:expected[t,c,z]=ij.remove_nans_imagej(a[t,c,z],1)
    assert result.preview.dataset.numpy('TCZYX').tobytes()==expected.tobytes()
    owner.commit(result.begin_edit());owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==a.tobytes()


def test_positioned_roi_holes_and_unmatched_planes_preserve_nan_bits():
    a=np.stack([example() for _ in range(12)]).reshape(2,2,3,5,5)
    a.view(np.uint32)[:,:,:,1,1]=0x7fc00001
    a.view(np.uint32)[:,:,:,0,4]=0xffc00002
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(a,axes='TCZYX',sample_policy=POLICY).observe_reads(reads))
    mask=np.zeros((5,5),np.uint8);mask[1:4,1:4]=1;mask[2,2]=0
    edit=owner.begin_edit();edit.set_rois([ij.ROI('one plane with hole',ij.Selection.from_mask(mask),t=1,c=0,z=2)]);owner.commit(edit)
    request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(1,0,2),ij.PlanePosition(0,1,1)],roi=ij.RoiPolicy.POSITIONED_ROIS)
    command=ij.resolve_remove_nans_command(owner.snapshot(),request,ij.RemoveNaNsCommandOptions(1))
    assert reads.counts().plane_reads==0
    result=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert reads.counts().plane_reads==1 and result.outputs[1].skipped
    expected=a.copy();expected[1,0,2,1,1]=7.
    assert result.preview.dataset.numpy('TCZYX').tobytes()==expected.tobytes()
    assert result.preview.dataset.numpy('TCZYX').view(np.uint32)[1,0,2,2,2]==0xffc12345


def test_stale_source_proposal_cannot_publish():
    owner=ij.Document(dataset(example()));app=ij.Application();app.adopt(owner)
    command=ij.resolve_remove_nans_command(owner.snapshot());result=app.prepare(command.operation,command.target,command.parameters())
    edit=owner.begin_edit();a=example();a[1,1]=42.;edit.replace_dataset(dataset(a));owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):app.publish(result)
    assert owner.snapshot().dataset.numpy('YX').tobytes()==a.tobytes()
