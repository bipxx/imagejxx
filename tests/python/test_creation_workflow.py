"""Typed V6 creation intent, stage ordering, source guards and shared review."""
import copy
import json
import os
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij


def base():
    bitmap=np.array([[1,0,1]],np.uint8)
    return ij.SelectionRegion(ij.Selection.from_mask(bitmap),(1,3),[-2,0]).as_selection()


def source(kind='u16'):
    dtype=np.uint8 if kind in ['u8','rgb'] else np.float32 if kind in ['float','quiet','ieee'] else np.uint16
    shape=(2,2,2,9,9,3 if kind=='rgb' else 1)
    pixels=np.full(shape,30,dtype)
    policy=ij.FloatingSamplePolicy.FINITE_ONLY
    if kind in ['quiet','ieee']:
        policy=(ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if kind=='quiet'
                else ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32)
        words=[0x7fc12345,0x80000000,1] if kind=='quiet' else [0x7fc12345,0x7f800000,0xff800000,0x80000000,1]
        pixels.view(np.uint32).flat[:len(words)]=words
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS',sample_policy=policy).observe_reads(reads))
    display=owner.snapshot().display;display.t=display.c=1;display.crosshair=[2,2,1];owner.set_display(display)
    owner.select(base())
    return owner,reads,pixels


def request(operation='ADD',points=None,kind='rectangle',geometry='AUTOMATIC',attributes=None):
    attributes=copy.deepcopy(attributes or {})
    if kind=='rounded_rectangle':attributes['corner_diameter']=2
    if points is None:
        points=[(-1,0),(2,1)] if kind in ['rectangle','ellipse','rounded_rectangle'] else [(-1,0),(2,0),(2,2),(-1,2)]
    return ij.SelectionCreationRequest(ij.Selection(kind,points,attributes=attributes),
        getattr(ij.SelectionCreationOperation,operation),getattr(ij.SelectionCreationGeometry,geometry))


def step(choices=None,original=None,enabled=True,target=None):
    return ij.NativeWorkflowOperation('selection_creation',{},ij.TargetRequest() if target is None else target,
        original=original,selection_creation=choices,enabled=enabled)


@pytest.mark.parametrize('kind',['rectangle','ellipse','rounded_rectangle','polygon','freehand'])
@pytest.mark.parametrize('operation',['REPLACE','ADD','SUBTRACT','INTERSECT','XOR'])
@pytest.mark.parametrize('geometry',['AUTOMATIC','FULL_REGION_V3'])
def test_typed_choices_round_trip_and_use_current_stage(kind,operation,geometry):
    owner,reads,_=source();before=owner.snapshot()
    choices=request(operation,kind=kind,geometry=geometry,attributes={'opaque':{'values':[True,None,2**64-1]}})
    program=ij.NativeWorkflowProgram([step(choices)])
    record=program.record();assert record['version']==6
    stored=record['steps'][0]['selection_creation']
    assert stored['operation']==operation.lower() and stored['candidate']['attributes']==choices.candidate.attributes
    restored=ij.NativeWorkflowProgram.from_json(program.json())
    assert restored.record()==record
    actual=ij.prepare_native_workflow(before,restored.steps)
    expected=ij.resolve_selection_creation_command(before,choices)
    assert actual.preview.selection==expected.result and actual.completed_steps==1
    assert actual.preview.previous_selection==before.selection
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads==reads.counts().region_reads==0


def test_ordered_creation_filter_and_histogram_are_one_atomic_proposal():
    pixels=np.arange(81,dtype=np.uint16).reshape(9,9)
    owner=ij.Document(ij.Dataset(pixels,axes='YX'));owner.select(base());before=owner.snapshot()
    steps=[step(request()),step(request('SUBTRACT',[(0,0),(1,1)])),
        ij.NativeWorkflowOperation('add',{'value':5},ij.TargetRequest()),
        ij.NativeWorkflowHistogram('area.csv',ij.TargetRequest(),authored_options=ij.NativeWorkflowHistogramOptions())]
    program=ij.NativeWorkflowProgram(steps);assert program.record()['version']==6
    assert program.record()['steps'][2]['selection_creation'] is None
    plan=ij.prepare_native_workflow(before,ij.NativeWorkflowProgram.from_json(program.json()).steps)
    assert plan.completed_steps==4 and owner.snapshot().shares_data(before)
    assert plan.preview.selection.to_dict()['mask']=='0A=='
    assert plan.preview.previous_selection.to_dict()['mask']=='8A=='
    expected=pixels.copy();expected[0,1]+=5
    np.testing.assert_array_equal(plan.preview.dataset.numpy('YX'),expected)
    assert len(plan.histograms)==1 and plan.histograms[0]['result'].visited_count==1 and plan.histograms[0]['result'].sum==6
    owner.commit(plan.begin_edit());assert owner.undo_steps==1
    owner.undo();assert owner.snapshot().shares_data(before)
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('YX'),expected)


@pytest.mark.parametrize('version',[1,2,3])
def test_original_equation_and_style_are_history_not_destination_binding(version):
    owner,reads,_=source()
    if version!=3:owner.select(ij.Selection('rectangle',[(0,0),(3,2)]))
    before=owner.snapshot();choices=request(kind='rounded_rectangle' if version==2 else 'rectangle')
    capture=ij.resolve_selection_creation_command(before,choices);original=capture.record()
    assert original['version']==version
    program=ij.NativeWorkflowProgram([step(original=original)])
    portable=ij.NativeWorkflowProgram.from_json(program.json());assert portable.record()['steps'][0]['original']==original
    assert ij.prepare_native_workflow(before,portable.steps).preview.selection==capture.result
    authored=request('REPLACE',[(2,3),(5,6)],attributes={'authored':{'label':'λ😀'}})
    edited=ij.NativeWorkflowProgram([step(authored,original)])
    plan=ij.prepare_native_workflow(before,edited.steps)
    assert plan.preview.selection==authored.candidate and edited.record()['steps'][0]['original']==original
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('kind',['u8','u16','rgb','float','quiet','ieee'])
def test_geometry_workflow_preserves_scientific_source_without_reads(kind):
    owner,reads,pixels=source(kind);before=owner.snapshot()
    plan=ij.prepare_native_workflow(before,[step(request()),step(request('SUBTRACT',[(0,0),(1,1)]))])
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    owner.commit(plan.begin_edit());after=owner.snapshot()
    assert after.selection.to_dict()['origin']==[-2,0] and after.selection.to_dict()['mask']=='0A=='
    assert after.display.t==after.display.c==after.display.crosshair[2]==1
    assert after.dataset.shares_pixels(before.dataset) and not after.dirty and owner.undo_steps==0
    assert after.dataset.numpy('TCZYXS').tobytes()==pixels.tobytes()


@pytest.mark.parametrize('field,value',[
    ('operation',True),('operation','unknown'),('geometry','full_region_v2'),('extra',0),
    ('candidate.extra',False),('candidate.attributes',None),('candidate.width',0),
    ('candidate.points',[[0,0]]),('candidate.kind','point')])
def test_closed_v6_choices_reject_malformed_transport(field,value):
    record=ij.NativeWorkflowProgram([step(request())]).record()
    parts=field.split('.');node=record['steps'][0]['selection_creation']
    for name in parts[:-1]:node=node[name]
    node[parts[-1]]=value
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.NativeWorkflowProgram.from_record(record)


@pytest.mark.parametrize('version',[1,2,3,4,5])
def test_old_program_grammars_do_not_admit_creation(version):
    record=ij.NativeWorkflowProgram([step(request())]).record();record['version']=version
    row=record['steps'][0];row.pop('selection_creation')
    if version<3:row.pop('enabled')
    if version<2:row.pop('filter_options')
    with pytest.raises(ValueError):ij.NativeWorkflowProgram.from_record(record)


@pytest.mark.parametrize('family',['histogram','operation'])
def test_v6_requires_retained_creation_intent(family):
    if family=='histogram':
        old=ij.NativeWorkflowHistogram('h.csv',ij.TargetRequest(),authored_options=ij.NativeWorkflowHistogramOptions())
    else:
        old=ij.NativeWorkflowOperation('add',{'value':1},ij.TargetRequest())
    record=ij.NativeWorkflowProgram([old]).record();record['version']=6
    if family=='operation':
        record['steps'][0].update(enabled=True,filter_options=None,selection_creation=None)
    with pytest.raises(ValueError,match='requires selection creation'):
        ij.NativeWorkflowProgram.from_record(record)


def test_invalid_late_intent_refuses_before_earlier_filter_reads():
    owner,reads,_=source();before=owner.snapshot()
    early=ij.NativeWorkflowOperation('add',{'value':5},ij.TargetRequest())
    capture=ij.resolve_selection_creation_command(before,request()).record();capture['result']['mask']='AA=='
    with pytest.raises(ValueError):ij.prepare_native_workflow(before,[early,step(original=capture)])
    target=ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    with pytest.raises(ValueError):ij.prepare_native_workflow(before,[early,step(request(),target=target)])
    with pytest.raises(ValueError):ij.NativeWorkflowProgram([
        ij.NativeWorkflowOperation('add',{'value':1},ij.TargetRequest(),selection_creation=request())])
    assert reads.counts().plane_reads==reads.counts().region_reads==0 and owner.snapshot().shares_data(before)


def test_disabled_intent_is_preserved_without_destination_compatibility():
    owner,reads,_=source();owner.select(ij.Selection('point',[(3,3)]));before=owner.snapshot()
    program=ij.NativeWorkflowProgram([step(request('SUBTRACT'),enabled=False)])
    record=program.record();assert record['version']==6 and not record['steps'][0]['enabled']
    plan=ij.prepare_native_workflow(before,ij.NativeWorkflowProgram.from_json(program.json()).steps)
    assert plan.completed_steps==0 and plan.preview.selection==before.selection
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('depth,accepted',[(122,True),(123,False)])
def test_v6_whole_envelope_depth(depth,accepted):
    nested='leaf'
    for _ in range(depth):nested=[nested]
    action=lambda:ij.NativeWorkflowProgram([step(request(attributes={'opaque':nested}))])
    if accepted:
        program=action();assert ij.NativeWorkflowProgram.from_json(program.json()).record()==program.record()
    else:
        with pytest.raises(ValueError):action()


def test_typed_requests_are_detached_and_cancellation_keeps_owner_atomic():
    choices=request();op=step(choices)
    choices.candidate=ij.Selection('rectangle',[(-1,0),(2,1)],7)
    detached=op.selection_creation;detached.candidate=ij.Selection('rectangle',[(-1,0),(2,1)],9)
    assert ij.NativeWorkflowProgram([op]).record()['steps'][0]['selection_creation']['candidate']['width']==1
    owner,reads,_=source();before=owner.snapshot();token=ij.CancellationToken()
    def progress(done,total):
        if done==1:token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.prepare_native_workflow(before,[op,step(request('SUBTRACT'))],cancellation=token,progress=progress)
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads==reads.counts().region_reads==0


def test_small_vector_program_does_not_reserve_binary_mask_codec_scratch():
    limits=ij.NativeWorkflowProgramLimits(max_steps=1,max_record_bytes=32768)
    program=ij.NativeWorkflowProgram([step(request())],limits)
    assert ij.NativeWorkflowProgram.from_json(program.json(),limits).record()==program.record()


def test_v6_sessions_and_batch_keep_program_intent(tmp_path):
    owner,_,_=source();program=ij.NativeWorkflowProgram([step(request())])
    path=tmp_path/'source.ijx';ij.save_session(str(path),ij.SessionCapture([owner],workflow=[program.record()]))
    reopened=ij.load_session(str(path));assert reopened.workflow==[program.record()]
    (tmp_path/'output').mkdir()
    plan=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(path),'result')],program.steps,str(tmp_path/'output'))
    plan.publish()
    result=ij.load_session(str(tmp_path/'output/result.ijx')).documents[0]
    assert result.snapshot().selection.to_dict()['mask']=='8A=='


@pytest.mark.parametrize('context',['imgui_cpp','imgui_python'])
def test_actual_workflow_candidate_edit_and_atomic_apply(context,tmp_path):
    if context=='imgui_cpp':
        row=json.loads(Path(os.environ['IMAGEJXX_CREATION_WORKFLOW_CPP_REPORT']).read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(preferences_path=str(tmp_path/'preferences.json'))
        row=editor._selection_creation_ui_case('Q04',context,str(tmp_path/'case'))
        if isinstance(row,str):row=json.loads(row)
    assert row['contract_id']=='p12r.creation-workflow-ui.001' and row['observation_state']=='observed'
    before=row['states']['initial']['documents'][0]
    prepared=row['states']['workflow.prepared']['documents'][0]
    after=row['states']['final']['documents'][0]
    assert prepared==before
    program=row['portable_program'];assert program['version']==6
    choice=program['steps'][0]['selection_creation']
    assert choice['candidate']['points']==[[18,6],[22,10]] and choice['candidate']['width']==3
    assert program['steps'][0]['original']==row['original_creation']
    assert row['original_creation']['candidate']['points']==[[18,6],[26,14]]
    assert after['selection']['kind']=='region' and after['selection']['origin']==[-2,0]
    assert after['previous_selection']==before['selection']
    for field in ['dirty','undo_steps','redo_steps','metadata','rois','overlays','planes','calibration']:
        assert after[field]==before[field]
    analysis=row['prepared_review']['histograms'][0]
    assert analysis['visited_count']==17 and analysis['sum']==791
    names=[v['selector'] for v in row['control_observations']]
    assert 'parameters.points' in names and 'parameters.keep_parameters' in names and 'program.apply' in names
