"""Compound scalar RGB rendering and ImageJ C-fast stack layout."""
import copy
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor

A=ij.ColorStackAction
L=ij.ColorStackLayoutPolicy
REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/scalar-rgb-stack-imagej154r.json').read_bytes())


def palette(kind):
    values=np.arange(256,dtype=np.uint8)
    if kind==0:return np.stack([values]*3,axis=1)
    if kind==1:return np.stack([255-values]*3,axis=1)
    return np.stack([values,(values.astype(np.uint16)*17).astype(np.uint8),255-values],axis=1)


def fixture(row):
    c,z,t=row['source_czt'];dtype=np.dtype(row['type'])
    pixels=np.array(row['input'],dtype).reshape(t,z,c,2,3).transpose(0,2,1,3,4)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes='TCZYX').observe_reads(reads))
    view=owner.snapshot().display;view.t=t-1;view.c=c-1;view.crosshair=[1,1,z-1]
    view.properties={'custom_lut':palette(row['lut']).tolist()}
    if row['range_kind']!=3:view.properties=view.properties|{'display_range':row['opening_range']}
    owner.set_display(view)
    return owner,pixels,reads


def resolve(source,**kw):
    return ij.resolve_color_stack_command(source,action=A.FLATTEN_RGB,layout=L.IMAGEJ_LEGACY,
        allow_calibration_reset=True,allow_nonxy_annotation_drop=True,**kw)


@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"{r['type']}-{r['source_czt']}-range{r['range_kind']}-lut{r['lut']}")
def test_pinned_scalar_stack_range_palette_layout(row):
    owner,pixels,reads=fixture(row);source=owner.snapshot();progress=[]
    command=resolve(source);record=command.record();assert record['version']==2
    ij.validate_color_stack_command_record(record)
    rebound=ij.color_stack_command_from_record(source,json.loads(json.dumps(record)))
    assert rebound.record()==record and reads.counts().plane_reads==0
    prepared=ij.prepare_color_stack(rebound,progress=lambda a,b:progress.append((a,b)))
    c,z,t=row['output_czt'];assert prepared.preview.dataset.shape==(t,c,z,2,3,3)
    output=prepared.preview.dataset.numpy('TCZYXS').transpose(0,2,1,3,4,5).reshape(-1,18)
    np.testing.assert_array_equal(output,row['output'])
    assert reads.counts().plane_reads==len(row['input']) and owner.undo_steps==0
    assert progress and all(a/b<=c/d for (a,b),(c,d) in zip(progress,progress[1:]))


def example():
    return next(r for r in REFERENCE['cases'] if r['type']=='uint16' and r['source_czt']==[2,2,2] and r['range_kind']==1 and r['lut']==2)


@pytest.mark.parametrize('authored',[False,True])
@pytest.mark.parametrize('version',[1,2,3])
def test_workflow_freezes_rendering_through_prefix_and_roundtrip(authored,version):
    owner,pixels,reads=fixture(example());source=owner.snapshot();original=resolve(source).record()
    target=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(0,2),z=ij.AxisRange(0,2),roi=ij.RoiPolicy.WHOLE_PLANE)
    options=dict(action='flatten_rgb',space='rgb',layout='imagej_legacy',invert_rgb=False,allow_calibration_reset=True,allow_nonxy_annotation_drop=True,max_working_bytes=512*1024*1024) if authored else {}
    operation=ij.NativeWorkflowOperation('color_stack',options,target,original=original)
    program=ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('add',{'value':25},target),operation]).record();program['version']=version
    if version>1:
        for step in program['steps']:step['filter_options']=None
    if version==3:
        for step in program['steps']:step['enabled']=True
    loaded=ij.NativeWorkflowProgram.from_record(program);assert reads.counts().plane_reads==0
    prepared=ij.prepare_native_workflow(source,loaded.steps)
    expected=[]
    for t in range(2):
        for z in range(2):
            for c in range(2):expected.append(ij.convert_rgb(pixels[t,c,z]+25,display_range=example()['opening_range'],lut=palette(2)).reshape(-1))
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS').reshape(8,18),expected)
    assert loaded.record()['steps'][1]['original']==original and prepared.dependencies==ij.Change.ALL
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
    owner.redo();assert owner.snapshot().dataset.shape==(1,1,8,2,3,3)


@pytest.mark.parametrize('bad',['version','rendering_missing','rendering_extra','nested_source','nested_pose','nested_display','outer_layout','nested_layout','composite'])
def test_bad_compound_records_refuse_without_samples(bad):
    owner,_,reads=fixture(example());record=resolve(owner.snapshot()).record()
    if bad=='version':record['version']=1
    elif bad=='rendering_missing':del record['rendering']
    elif bad=='rendering_extra':record['rendering']['extra']=True
    elif bad=='nested_source':record['rendering']['source']['document_id']+=1
    elif bad=='nested_pose':record['rendering']['view_position'][0]=0
    elif bad=='nested_display':record['rendering']['display_revision']+=1
    elif bad=='outer_layout':record['layout']['working_bytes']+=1
    elif bad=='nested_layout':record['rendering']['layout']['working_bytes']+=1
    else:record['rendering']['request']['mode']='composite'
    with pytest.raises(ValueError):ij.validate_color_stack_command_record(record)
    assert reads.counts().plane_reads==0


@pytest.mark.parametrize('replace',[False,True])
def test_shared_review_scalar_flatten_records_replays_and_undoes(tmp_path,replace):
    owner,pixels,reads=fixture(example());editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner);editor.recording=True
    editor.action('show_color_stack');editor.action('color_stack_draft',action='flatten_rgb',layout='imagej_legacy',allow_calibration_reset=True,allow_nonxy_annotation_drop=True,replace=replace)
    assert reads.counts().plane_reads==0
    editor.action('color_stack_preview');editor.wait();panel=editor.render_headless(frames=3)['color_stack']
    assert panel['ready'] and not panel['error'] and panel['record']['version']==2
    editor.action('color_stack_apply');editor.wait();assert editor.workflow==[panel['record']]
    output=editor.live_document().native_snapshot().dataset.numpy('TCZYXS').reshape(8,18)
    np.testing.assert_array_equal(output,example()['output'])
    assert owner.undo_steps==int(replace)
    if replace:
        editor.action('undo');np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
    else:
        other=ij.Document(owner.snapshot());view=other.snapshot().display;view.properties={'lut':'Blue','display_range':[0,65535]};other.set_display(view);editor.add_native_document(other)
        editor.action('review_color_stack',record=panel['record']);restored=editor.render_headless(frames=3)['color_stack']
        assert restored['record']['rendering']['request']==panel['record']['rendering']['request']
        editor.action('color_stack_apply');editor.wait()
        np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('TCZYXS').reshape(8,18),example()['output'])


def test_cancellation_and_execution_cap_do_not_read_or_publish():
    owner,_,reads=fixture(example());command=resolve(owner.snapshot());token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_color_stack(command,cancellation=token)
    with pytest.raises(ValueError):ij.prepare_color_stack(command,execution_max_working_bytes=command.layout.working_bytes-1)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0


def test_explicit_rendering_capture_can_bind_to_current_source():
    owner,pixels,_=fixture(example());render=ij.resolve_rgb_dataset_command(owner.snapshot(),display_range=[0,65535],lut=palette(1)).record()
    command=resolve(owner.snapshot(),rendering_record=render)
    result=ij.prepare_color_stack(command).preview.dataset.numpy('TCZYXS').reshape(8,18)
    expected=[ij.convert_rgb(pixels[t,c,z],display_range=[0,65535],lut=palette(1)).reshape(-1) for t in range(2) for z in range(2) for c in range(2)]
    np.testing.assert_array_equal(result,expected)


@pytest.mark.parametrize('name',['Grays','Red','Inverted','Fire','Ice','Spectrum'])
def test_owned_legacy_palettes_match_the_existing_shared_rgb_review(tmp_path,name):
    owner,_,reads=fixture(example());view=owner.snapshot().display;view.properties={'lut':name,'display_range':[100,6000]};owner.set_display(view)
    editor=Editor(preferences=tmp_path/'prefs.json');editor.add_native_document(owner)
    editor.action('show_rgb_dataset');record=editor.render_headless(frames=3)['rgb_dataset']['record'];editor.action('cancel_rgb_dataset')
    count=reads.counts().plane_reads;compound=resolve(owner.snapshot()).record()
    assert reads.counts().plane_reads==count
    assert compound['rendering']['request']['lut']==record['request']['lut']


def test_float64_is_an_owned_scalar_extension_with_common_range():
    pixels=np.array([[[[-1.,0.,1.]],[[100.,200.,300.]]]],np.float64)
    owner=ij.Document(ij.Dataset(pixels,axes='TCYX'));view=owner.snapshot().display;view.c=1;owner.set_display(view)
    result=ij.prepare_color_stack(resolve(owner.snapshot())).preview.dataset.numpy('TCZYXS').reshape(2,1,3,3)
    expected=[ij.convert_rgb(pixels[0,c],display_range=[100,300]) for c in range(2)]
    np.testing.assert_array_equal(result,expected)


def test_scalar_planar_metadata_requires_consent_and_roundtrips_paired_history():
    owner,_,_=fixture(example());cal=ij.SpatialCalibration(spacing=[2,3,5],unit='um');tx=owner.begin_edit();tx.set_calibration(cal);tx.select(ij.Selection('rectangle',[(0,0),(1,1)]));owner.commit(tx)
    owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),ij.PlanarDisplayAnnotation('layer',ij.TextAnnotationPayload(text='original',bounds=[0,0,1,1]),'xz',0,t=1,c=1)))
    owner=ij.Document(owner.snapshot());source=owner.snapshot();reads=ij.DatasetReadObserver()
    with pytest.raises(ValueError):ij.resolve_color_stack_command(source,action=A.FLATTEN_RGB,layout=L.IMAGEJ_LEGACY,allow_calibration_reset=True)
    prepared=ij.prepare_color_stack(resolve(source,destination=ij.OutputDestination.IN_PLACE));assert not ij.planar_display_annotations(prepared.preview)
    owner.commit(prepared.begin_edit());owner.undo();assert owner.snapshot().dataset.calibration.unit=='um' and len(ij.planar_display_annotations(owner.snapshot()))==1
    owner.redo();assert owner.snapshot().dataset.calibration.unit=='px'


def test_scalar_flatten_session_batch_uses_v2_capture(tmp_path):
    owner,_,_=fixture(example());command=resolve(owner.snapshot());target=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(0,2),z=ij.AxisRange(0,2),roi=ij.RoiPolicy.WHOLE_PLANE)
    program=ij.NativeWorkflowProgram([ij.NativeWorkflowOperation('color_stack',{},target,original=command.record())])
    source=tmp_path/'input.ijx';ij.save_session(str(source),ij.SessionCapture([owner.snapshot()]));before=source.read_bytes();destination=tmp_path/'out';destination.mkdir()
    prepared=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(source),'scalar')],program.steps,str(destination));prepared.publish()
    result=ij.load_session(str(destination/'scalar.ijx')).documents[0].snapshot()
    np.testing.assert_array_equal(result.dataset.numpy('TCZYXS').reshape(8,18),example()['output'])
    assert source.read_bytes()==before


def test_actual_scalar_rgb_flow_in_both_hosts(tmp_path):
    import os
    from imagejxx import _desktop
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes());python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    expected=[ij.convert_rgb((100*(t*4+z*2+c)+1000*np.arange(6,dtype=np.uint16)).reshape(2,3),display_range=[100,6000],lut=ij.channel_display_lut('Red')).reshape(-1) for t in range(2) for z in range(2) for c in range(2)]
    for report in [cpp,python]:
        data=report['scalar_rgb_flatten'];assert data['shape']==data['redo_shape']==[1,1,8,2,3,3] and data['undo_shape']==[2,2,2,2,3,1]
        assert data['preview']['ready'] and data['history_before_apply']==0 and data['undo_steps']==1 and data['undo_unit']=='um'
        assert data['record']==data['preview']['record'] and data['record']['version']==2
        assert data['record']['rendering']['request']['display_range']==[100,6000]
        np.testing.assert_array_equal(data['pixels'],expected)
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])


def test_explicit_deferred_gray_capture_does_not_inherit_destination_display():
    owner,pixels,_=fixture(example());view=owner.snapshot().display;view.properties={};owner.set_display(view)
    rendering=ij.resolve_rgb_dataset_command(owner.snapshot()).record()
    assert rendering['request']['display_range'] is None and rendering['request']['lut'] is None
    view=owner.snapshot().display;view.properties={'lut':'Red','display_range':[100,6000]};owner.set_display(view)
    command=resolve(owner.snapshot(),rendering_record=rendering)
    assert command.record()['rendering']['request']['display_range'] is None
    expected=[ij.convert_rgb(pixels[t,c,z],display_range=[float(pixels[-1,-1,-1].min()),float(pixels[-1,-1,-1].max())]).reshape(-1) for t in range(2) for z in range(2) for c in range(2)]
    np.testing.assert_array_equal(ij.prepare_color_stack(command).preview.dataset.numpy('TCZYXS').reshape(8,18),expected)
