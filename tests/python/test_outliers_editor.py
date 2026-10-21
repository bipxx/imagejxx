"""ImageJ Remove Outliers: captured LUT intent, transactions and shared UI."""
import copy
import json
import os
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest

FIXTURES=Path(__file__).parents[1]/'fixtures'
PIXELS=json.loads((FIXTURES/'outliers-imagej154r.json').read_bytes())['cases']
LUTS=json.loads((FIXTURES/'outliers-lut-imagej154r.json').read_bytes())['cases']

def decode(row,phase):
    return np.stack([np.frombuffer(bytes.fromhex(x),dtype=row['dtype']).reshape(20,25) for x in row[phase+'_hex']])

@pytest.mark.parametrize('row',PIXELS,ids=lambda r:f"{r['dtype']}-s{r['signal']}-r{r['radius']}-{r['which']}-i{int(r['inverted_lut'])}-m{r['mode']}-a{r['selection']}")
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
    command=ij.resolve_remove_outliers_command(before,request,ij.RemoveOutliersCommandOptions(row['radius'],row['threshold'],row['which']))
    assert reads.counts().plane_reads==0 and command.operation=='remove_outliers_imagej'
    assert command.inverted_lut==row['inverted_lut']
    assert command.parameters()=={'radius':row['radius'],'threshold':row['threshold'],'which':row['which'],'inverted_lut':row['inverted_lut']}
    assert ij.rank_command_from_record(before,command.record()).record()==command.record()
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    expected=decode(row,'output')
    assert prepared.preview.dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert prepared.preview.display.properties==before.display.properties
    owner.commit(prepared.begin_edit())
    assert owner.undo_steps==int(source.tobytes()!=expected.tobytes())
    if owner.undo_steps:
        owner.undo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==source.tobytes()
        owner.redo();assert owner.snapshot().dataset.numpy('ZYX').tobytes()==expected.tobytes()
    assert owner.snapshot().display.properties==before.display.properties

LUT_CASES=[(r,v) for r in LUTS for v in ([1,2] if r['mode'] else [1])]
@pytest.mark.parametrize('row,version',LUT_CASES,ids=[f"{r['dtype']}-p{r['table']}-m{r['mode']}-c{r['channel']}-h{r['threshold']}-v{v}" for r,v in LUT_CASES])
def test_actual_processor_lut_policy(row,version):
    source=np.asarray(row['before'],dtype=row['dtype']).reshape(3,3);scientific=bool(row['mode']);c=row['channel'] if scientific else 0
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.stack([source,source]) if scientific else source,axes='CYX' if scientific else 'YX').observe_reads(reads))
    view=owner.snapshot().display;view.c=c;view.properties={'lut':'Inverted'}
    if scientific:
        state={'schema':'imagejxx.channel-display','version':version,'mode':{1:'composite',2:'color',3:'grayscale'}[row['mode']],
               'channels':[{'visible':True,'range':[0.,255.],'lut':{'table':[[i if k==0 else 255-i]*3 for i in range(256)]}} for k in range(2)]}
        if version==2:state['projection']='max'
        view=ij.with_channel_display(view,owner.snapshot().dataset,state)
    else:view.properties={'lut':'Inverted','custom_lut':row['palette']}
    if row['threshold']:
        props=view.properties;props['manual_threshold']={'version':1,'lower':4.,'upper':8.,'t':0,'c':c,'z':0};view.properties=props
    owner.set_display(view);before=owner.snapshot()
    assert ij.remove_outliers_lut_inverted(before)==row['inverted'] and reads.counts().plane_reads==0
    command=ij.resolve_remove_outliers_command(before,options=ij.RemoveOutliersCommandOptions(1,50,'bright'))
    assert command.inverted_lut==row['inverted']
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters())
    assert prepared.preview.dataset.numpy('CYX')[c].tobytes()==np.asarray(row['after'],dtype=row['dtype']).tobytes()
    assert prepared.preview.display.properties==before.display.properties

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_frozen_polarity_workflow_session_and_explicit_rebind(tmp_path,dtype):
    source=np.zeros((2,2,3,5,5),dtype);source[...,2,2]=80
    owner=ij.Document(ij.Dataset(source,axes='TCZYX'));view=owner.snapshot().display;view.properties={'lut':'Inverted'};owner.set_display(view)
    request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(),c=ij.AxisRange(),z=ij.AxisRange())
    command=ij.resolve_remove_outliers_command(owner.snapshot(),request,ij.RemoveOutliersCommandOptions(1,50,'dark'))
    step=ij.NativeWorkflowOperation(command.operation,{},request,original=command.record())
    program=ij.NativeWorkflowProgram([step]);loaded=ij.NativeWorkflowProgram.from_json(program.json())
    view.properties={'lut':'Grays'};owner.set_display(view)
    rebound=ij.rebind_rank_command(owner.snapshot(),command.record(),request)
    assert rebound.inverted_lut is True
    result=ij.prepare_native_workflow(owner.snapshot(),loaded.steps);owner.commit(result.begin_edit())
    assert not owner.snapshot().dataset.numpy('TCZYX').any()
    owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==source.tobytes();owner.redo()
    path=tmp_path/'outliers.ijx';ij.save_session(str(path),ij.SessionCapture([owner.snapshot()]))
    assert not ij.load_session(str(path)).documents[0].snapshot().dataset.numpy('TCZYX').any()
    assert owner.snapshot().display.properties==view.properties

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_python_editor_new_document_and_noop(tmp_path,dtype):
    from imagejxx.ui.app import Editor
    source=np.zeros((5,5),dtype);source[2,2]=80
    owner=ij.Document(ij.Dataset(source,axes='YX'));editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    editor.apply_remove_outliers(options=ij.RemoveOutliersCommandOptions(1,80),wait=True)
    assert owner.undo_steps==0
    editor.apply_remove_outliers(options=ij.RemoveOutliersCommandOptions(1,50),wait=True)
    assert owner.undo_steps==1 and not owner.snapshot().dataset.numpy('YX').any()
    owner.undo();before=owner.snapshot()
    command=ij.resolve_remove_outliers_command(before,ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT),ij.RemoveOutliersCommandOptions(1,50))
    app=ij.Application();app.adopt(owner);proposal=app.prepare(command.operation,command.target,command.parameters());proposal.stage_new_document();created=app.publish(proposal)
    assert created.created and not created.document.snapshot().dataset.numpy('YX').any()
    assert owner.snapshot().dataset.numpy('YX').tobytes()==source.tobytes()

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_empty_mask_precancel_and_bad_metadata_are_read_free(dtype):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((5,5),dtype),axes='YX').observe_reads(reads))
    edit=owner.begin_edit();edit.select(ij.Selection.from_mask(np.zeros((5,5),np.uint8)));owner.commit(edit);owner=ij.Document(owner.snapshot())
    command=ij.resolve_remove_outliers_command(owner.snapshot());prepared=ij.prepare_operation(command.operation,command.target,command.parameters());owner.commit(prepared.begin_edit())
    assert reads.counts().plane_reads==0 and owner.undo_steps==0
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_operation(command.operation,command.target,command.parameters(),cancellation=token)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0
    view=owner.snapshot().display;view.properties={'lut':'Unknown'};owner.set_display(view)
    with pytest.raises(ValueError):ij.resolve_remove_outliers_command(owner.snapshot())
    assert reads.counts().plane_reads==0

@pytest.mark.parametrize('field,value',[('radius',101),('radius',True),('threshold',-1),('threshold',True),('threshold',float('inf')),('which','Bright'),('inverted_lut',1),('inverted_lut','false'),('unknown',0)])
def test_strict_record_fields(field,value):
    owner=ij.Document(ij.Dataset(np.zeros((3,3),np.uint8),axes='YX'))
    record=ij.resolve_remove_outliers_command(owner.snapshot()).record();record['options'][field]=value
    with pytest.raises((ValueError,TypeError)):ij.validate_rank_command_record(record)
    old=ij.resolve_rank_command(owner.snapshot()).record();old['options']['threshold']=50
    with pytest.raises(ValueError):ij.validate_rank_command_record(old)

@pytest.mark.parametrize('args',[(True,50,'bright'),(101,50,'bright'),(1,True,'bright'),(1,'50','bright'),(1,-1,'bright'),(1,50,b'bright'),(1,50,'Bright')])
def test_constructor_admission(args):
    with pytest.raises((ValueError,TypeError)):ij.RemoveOutliersCommandOptions(*args)

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_legacy_workflow_adapter_preserves_frozen_direction(dtype):
    from imagejxx.ui import workflows
    from imagejxx.ui.model import Document
    source=np.zeros((5,5),dtype);source[2,2]=80
    owner=ij.Document(ij.Dataset(source,axes='YX'));view=owner.snapshot().display;view.properties={'lut':'Inverted'};owner.set_display(view)
    command=ij.resolve_remove_outliers_command(owner.snapshot(),options=ij.RemoveOutliersCommandOptions(1,50,'dark'))
    steps=[command.record()];saved=copy.deepcopy(steps);detached=Document(source)
    result=workflows.run(detached,steps,rank_rebind=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    assert not result.any() and result.dtype==source.dtype and detached.data.tobytes()==source.tobytes() and steps==saved
    assert workflows.loads(workflows.dumps(steps))==steps

@pytest.mark.parametrize('case_id',['R32','R33','R34','R35','R36','R37'])
def test_actual_outliers_menu_both_hosts(tmp_path,case_id):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rank_ui_case(case_id,'imgui_python',str(tmp_path/'capture001')))
    cpp=json.loads((Path(os.environ['IMAGEJXX_OUTLIERS_CPP_DIR'])/case_id/'case.json').read_bytes())
    for report in [python,cpp]:
        assert report['observation_state']=='observed'
        assert all(f['requested_events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
        before=report['fixture_sources']['source'];states=report['states'];configured=states['outliers.configured']['documents'][0]
        assert all(f['rendered']['rank_panel']['remembered_radii']['median']==2 for f in report['frames'])
        preview=next(f['rendered']['ordinary_pixels']['readback']['source'] for f in report['frames'] if f['label']=='outliers.preview')
        if case_id!='R35':assert preview['pixels']==[60. if case_id in ['R34','R37'] else 30.]*25
        if case_id in ['R35','R36']:
            label='outliers.cancelled' if case_id=='R36' else 'outliers.applied';after=states[label]['documents'][0]
            assert after['undo_steps']==0 and not states[label]['workflow']
            assert [x['declared_hex'] for x in after['planes']]==[x['declared_hex'] for x in before['planes']]
            continue
        documents=states['outliers.applied']['documents']
        if case_id=='R37':
            assert len(documents)==2
            original=next(d for d in documents if d['document_id']==before['document_id'])
            assert original['undo_steps']==0 and [x['declared_hex'] for x in original['planes']]==[x['declared_hex'] for x in before['planes']]
            after=next(d for d in documents if d['document_id']!=before['document_id'])
        else:after=documents[0]
        selected={0,1,2} if case_id=='R33' else {10 if case_id in ['R34','R37'] else 1};dtype=np.dtype(before['dtype'])
        for i,p in enumerate(after['planes']):
            if i in selected:
                t,c,z=p['position'];assert bytes.fromhex(p['declared_hex'])==np.full((5,5),10*t+20*c+30*z,dtype).tobytes()
            else:assert p['declared_hex']==before['planes'][i]['declared_hex']
        assert after['display']['properties']==before['display']['properties'] and after['undo_steps']==1
        record=states['outliers.applied']['workflow'][-1]
        assert record['operation']=='remove_outliers_imagej' and record['options']=={'radius':1.,'threshold':50.,'which':'bright' if case_id=='R32' else 'dark','inverted_lut':case_id!='R32'}
        if case_id=='R37':assert record['target']['destination']=='new_document'
        undone=next(d for d in states['outliers.undo']['documents'] if d['document_id']==after['document_id'])
        redone=next(d for d in states['outliers.redo']['documents'] if d['document_id']==after['document_id'])
        assert [x['declared_hex'] for x in undone['planes']]==[x['declared_hex'] for x in before['planes']]
        assert [x['declared_hex'] for x in redone['planes']]==[x['declared_hex'] for x in after['planes']]
