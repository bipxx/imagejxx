"""Evolving component/legacy layout workflow, immutable captures and review."""
import copy
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

A=ij.ColorStackAction
L=ij.ColorStackLayoutPolicy

def target(c=2,t=2,z=2):
    return ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,t),c=ij.AxisRange(0,c),z=ij.AxisRange(0,z),roi=ij.RoiPolicy.WHOLE_PLANE)

def fixture():
    pixels=np.arange(2*2*2*2*3*3,dtype=np.uint8).reshape(2,2,2,2,3,3)
    reads=ij.DatasetReadObserver();cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30],unit='um',time_origin=7,frame_interval=2)
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS',calibration=cal).observe_reads(reads))
    selection=ij.Selection('rectangle',[(0,0),(1,1)])
    tx=owner.begin_edit();tx.select(selection);tx.set_rois([ij.ROI('one',selection,t=1,c=1,z=1)]);owner.commit(tx)
    owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(),ij.PlanarAnnotation('XY',selection,'xy',1,t=1,c=1)))
    owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),ij.PlanarDisplayAnnotation('XZ',ij.TextAnnotationPayload(text='kept',bounds=[0,0,1,1]),'xz',0,t=1,c=1)))
    owner=ij.Document(owner.snapshot());view=owner.snapshot().display;view.t=view.c=1;view.crosshair=[1,1,1];view.orthogonal=True;view.properties={'future':{'kept':True}};owner.set_display(view)
    return owner,pixels,reads

def options(action='split',space='rgb',layout='preserve_axes',**changes):
    return dict(action=action,space=space,layout=layout,invert_rgb=False,allow_calibration_reset=False,allow_nonxy_annotation_drop=False,max_working_bytes=512*1024*1024)|changes

def capture(source,space='rgb',**kwargs):
    return ij.resolve_color_stack_command(source,space=space,**kwargs).record()

def operation(record,c=2,t=2,z=2,parameters=None,**kwargs):
    return ij.NativeWorkflowOperation('color_stack',parameters or {},target(c,t,z),original=record,**kwargs)

def split_expected(pixels,space):
    out=np.empty((2,6,2,2,3),np.float32 if space=='hsb32' else np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):out[t,c*3:c*3+3,z]=ij.split_color_components(pixels[t,c,z],space)
    return out

@pytest.mark.parametrize('space',['rgb','hsb8','hsb32'])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_mixed_split_filter_merge_one_guarded_transaction(space,destination):
    owner,pixels,reads=fixture();source=owner.snapshot();original=capture(source,space,destination=destination)
    # Authored merge choices reuse the old split capture without rewriting it.
    merge=operation(original,c=6,parameters=options(action='merge',space=space))
    delta=.1 if space=='hsb32' else 1
    steps=[ij.NativeWorkflowOperation('add',{'value':1},target()),operation(original),
           ij.NativeWorkflowOperation('add',{'value':delta},target(6)),merge]
    program=ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram(steps).json())
    assert reads.counts().plane_reads==0
    prepared=ij.prepare_native_workflow(source,program.steps)
    expected=np.empty_like(pixels);parts=split_expected(pixels+1,space)
    if space=='hsb32':parts=(parts.astype(np.float64)+.1).astype(np.float32)
    else:parts=np.clip(parts.astype(np.int16)+1,0,255).astype(np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):expected[t,c,z]=ij.merge_color_components(parts[t,3*c:3*c+3,z],space)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'),expected)
    assert prepared.completed_steps==4 and prepared.dependencies==ij.Change.ALL and owner.undo_steps==0
    assert prepared.preview.display.c==1 and prepared.preview.display.t==1
    assert prepared.preview.dataset.calibration.spacing==[2,3,5] and prepared.preview.dataset.calibration.time_origin==7
    assert len(ij.planar_annotations(prepared.preview))==3 and len(ij.planar_display_annotations(prepared.preview))==3
    assert program.record()['steps'][3]['original']==original
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels)
    assert owner.snapshot().display.properties==source.display.properties
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),expected)

@pytest.mark.parametrize('version',[1,2,3])
def test_versions_and_rebinding_another_owner(version):
    owner,pixels,reads=fixture();original=capture(owner.snapshot());step=operation(original,parameters=options(space='hsb32'))
    record=ij.NativeWorkflowProgram([step]).record();record['version']=version
    if version>=2:record['steps'][0]['filter_options']=None
    if version==3:record['steps'][0]['enabled']=True
    program=ij.NativeWorkflowProgram.from_record(record);other=ij.Document(owner.snapshot())
    prepared=ij.prepare_native_workflow(other.snapshot(),program.steps)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'),split_expected(pixels,'hsb32'))
    assert program.record()['steps'][0]['original']==original

@pytest.mark.parametrize('bad',['missing_capture','extra_capture','partial','roi','mode','destination','calibration','filter','partial_options','extra_options','flag_type','cap_type','negative_cap','space','invert'])
def test_invalid_intent_refuses_before_prefix_pixels(bad):
    owner,_,reads=fixture();original=capture(owner.snapshot());request=target();parameters={};kwargs={}
    if bad=='missing_capture':original=None
    elif bad=='extra_capture':original['extra']=1
    elif bad=='partial':request.c=ij.AxisRange(0,1)
    elif bad=='roi':request.roi=ij.RoiPolicy.ACTIVE_SELECTION
    elif bad=='mode':request.mode=ij.ExecutionMode.VOLUME
    elif bad=='destination':request.destination=ij.OutputDestination.NEW_DOCUMENT
    elif bad=='calibration':kwargs['use_destination_calibration']=True
    elif bad=='filter':kwargs['filter_options']=ij.GaussianBlurCommandOptions()
    else:
        parameters=options()
        if bad=='partial_options':parameters.pop('space')
        elif bad=='extra_options':parameters['extra']=1
        elif bad=='flag_type':parameters['allow_calibration_reset']=1
        elif bad=='cap_type':parameters['max_working_bytes']=1.5
        elif bad=='negative_cap':parameters['max_working_bytes']=-1
        elif bad=='space':parameters['space']='HSV'
        else:parameters['invert_rgb']=True
    with pytest.raises((ValueError,TypeError,IndexError,OverflowError)):
        step=ij.NativeWorkflowOperation('color_stack',parameters,request,original=original,**kwargs)
        ij.prepare_native_workflow(owner.snapshot(),[ij.NativeWorkflowOperation('add',{'value':1},target()),step])
    assert reads.counts().plane_reads==0 and owner.undo_steps==0

@pytest.mark.parametrize('bad',['old_extent','rank_float64','export','calibration_loss','nonxy_loss'])
def test_invalid_later_context_preflights_all_stages(bad):
    owner,_,reads=fixture();original=capture(owner.snapshot());steps=[ij.NativeWorkflowOperation('add',{'value':1},target())]
    if bad in ['calibration_loss','nonxy_loss']:
        p=options(space='hsb32',layout='imagej_legacy');p['allow_calibration_reset']=bad=='nonxy_loss'
        steps.append(operation(original,parameters=p))
    else:
        steps.append(operation(original,parameters=options(space='hsb32')))
        if bad=='old_extent':steps.append(operation(original,parameters=options(action='merge',space='hsb32')))
        elif bad=='rank_float64':
            steps.extend([ij.NativeWorkflowOperation('convert_image',{'dtype':'float64','scale':False},target(6)),ij.NativeWorkflowOperation('gaussian_blur_imagej',{'sigma':1.},target(6))])
        else:
            item=ij.ImageJAnnotationExportItem('bad.roi',None,ij.ImageJAnnotationKind.SELECTION,999,ij.ImageJRoi(ij.ROI('bad',ij.Selection('rectangle',[(0,0),(1,1)]))),fresh_source=True)
            steps.append(ij.NativeWorkflowExport('bad.zip',[item]))
    with pytest.raises((ValueError,IndexError)):ij.prepare_native_workflow(owner.snapshot(),steps)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0

@pytest.mark.parametrize('action',['hsb_legacy','flatten'])
def test_legacy_layout_and_paired_calibration_history(action):
    owner,pixels,reads=fixture();original=capture(owner.snapshot())
    p=options(space='hsb32',layout='imagej_legacy') if action=='hsb_legacy' else options(action='flatten_rgb',layout='imagej_legacy')
    p.update(allow_calibration_reset=True,allow_nonxy_annotation_drop=True)
    prepared=ij.prepare_native_workflow(owner.snapshot(),[operation(original,parameters=p)])
    expected_shape=(1,3,8,2,3,1) if action=='hsb_legacy' else (1,1,8,2,3,3)
    assert prepared.preview.dataset.shape==expected_shape and prepared.preview.dataset.calibration.unit=='px'
    assert not ij.planar_display_annotations(prepared.preview)
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    owner.undo();assert owner.snapshot().dataset.calibration.unit=='um' and len(ij.planar_display_annotations(owner.snapshot()))==1
    owner.redo();assert owner.snapshot().dataset.shape==expected_shape

def test_disabled_step_keeps_stage_and_validates_raw_choices():
    owner,pixels,reads=fixture();original=capture(owner.snapshot())
    program=ij.NativeWorkflowProgram([operation(original,enabled=False),ij.NativeWorkflowOperation('add',{'value':1},target())])
    result=ij.prepare_native_workflow(owner.snapshot(),program.steps)
    np.testing.assert_array_equal(result.preview.dataset.numpy('TCZYXS'),pixels+1)
    assert result.completed_steps==1
    p=options();p['invert_rgb']=True
    with pytest.raises(ValueError):ij.NativeWorkflowProgram([operation(original,parameters=p,enabled=False)])

@pytest.mark.parametrize('authored',[False,True])
def test_retained_and_authored_pose_rules(authored):
    owner,pixels,_=fixture();source=owner.snapshot();view=source.display;view.t=view.c=0;view.crosshair=[0,0,0]
    original=capture(source.with_display(view))
    step=operation(original,parameters=options() if authored else None)
    current=ij.TargetRequest(ij.TargetScope.CURRENT_PLANE,roi=ij.RoiPolicy.WHOLE_PLANE)
    result=ij.prepare_native_workflow(source,[step,ij.NativeWorkflowOperation('add',{'value':1},current)])
    expected=split_expected(pixels,'rgb');expected[1,3,1] += 1 if authored else 0
    if not authored:expected[0,0,0]+=1
    np.testing.assert_array_equal(result.preview.dataset.numpy('TCZYX'),expected)

@pytest.mark.parametrize('change',['pixels','metadata','selection','display'])
def test_all_revisions_guard_whole_workflow(change):
    owner,_,_=fixture();prepared=ij.prepare_native_workflow(owner.snapshot(),[operation(capture(owner.snapshot()))])
    if change=='display':view=owner.snapshot().display;view.c=0;owner.set_display(view)
    elif change=='selection':owner.select(None)
    else:
        tx=owner.begin_edit()
        if change=='metadata':tx.set_property('changed',True)
        else:tx.replace_plane(np.zeros((2,3,3),np.uint8),t=0,c=0,z=0)
        owner.commit(tx)
    before=owner.snapshot();steps=owner.undo_steps
    with pytest.raises(RuntimeError):owner.commit(prepared.begin_edit())
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps

def test_budget_cancel_and_captured_frame_validation():
    owner,_,reads=fixture();source=owner.snapshot();original=capture(source);step=operation(original)
    with pytest.raises(ValueError):ij.prepare_native_workflow(source,[step],ij.NativeWorkflowLimits(max_retained_bytes=65536))
    assert reads.counts().plane_reads==0
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_native_workflow(source,[step],cancellation=token)
    assert reads.counts().plane_reads==0
    token=ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):ij.prepare_native_workflow(source,[step],cancellation=token,progress=lambda done,total:token.cancel())
    assert owner.undo_steps==0

@pytest.mark.parametrize('merge_space',['hsb8','hsb32'])
@pytest.mark.parametrize('mask',['whole','selection','full_selection','partial_ranges','empty_rois'])
def test_threshold_dtype_and_mask_promotion_preflight(mask,merge_space):
    owner,pixels,reads=fixture()
    if mask in ['full_selection','empty_rois']:
        tx=owner.begin_edit()
        if mask=='full_selection':tx.select(ij.Selection('rectangle',[(0,0),(3,2)]))
        else:tx.set_rois([])
        owner.commit(tx);owner=ij.Document(owner.snapshot())
    original=capture(owner.snapshot());request=target(6)
    if mask in ['selection','full_selection']:request.roi=ij.RoiPolicy.ACTIVE_SELECTION
    elif mask=='empty_rois':request.roi=ij.RoiPolicy.POSITIONED_ROIS
    elif mask=='partial_ranges':request.c=ij.AxisRange(0,1)
    steps=[operation(original,parameters=options(space='hsb32')),
           ij.NativeWorkflowOperation('threshold',{'lower':.2,'upper':.8},request),
           operation(original,c=6,parameters=options(action='merge',space=merge_space))]
    byte_output=mask in ['whole','full_selection']
    if (merge_space=='hsb8')!=byte_output:
        with pytest.raises(ValueError):ij.prepare_native_workflow(owner.snapshot(),steps)
        assert reads.counts().plane_reads==0
    else:
        prepared=ij.prepare_native_workflow(owner.snapshot(),steps)
        assert prepared.preview.dataset.shape==(2,2,2,2,3,3) and prepared.completed_steps==3
        assert owner.undo_steps==0

@pytest.mark.parametrize('name,parameters',[
    ('auto_mask',{}),('local_threshold',{'radius':1}),('fill_holes',{}),('binary_outline',{}),
    ('skeletonize',{}),('remove_small_objects',{'min_size':1}),('clear_border',{}),('local_maxima',{'radius':1})])
def test_byte_producing_filters_feed_hsb8_merge(name,parameters):
    owner,_,_=fixture();original=capture(owner.snapshot())
    steps=[operation(original,parameters=options(space='hsb32')),
           ij.NativeWorkflowOperation(name,parameters,target(6)),
           operation(original,c=6,parameters=options(action='merge',space='hsb8'))]
    prepared=ij.prepare_native_workflow(owner.snapshot(),steps)
    assert prepared.preview.dataset.shape==(2,2,2,2,3,3) and prepared.completed_steps==3

@pytest.mark.parametrize('name,parameters',[
    ('variance_filter',{'radius':0}),('rank_filter',{'radius':0,'method':'variance'}),('find_edges',{}),
    ('laplacian',{}),('difference_of_gaussians',{'sigma_small':0.,'sigma_large':0.}),
    ('anisotropic_diffusion',{'iterations':0}),('distance_transform',{}),('label',{}),('binary_watershed',{'maxima_radius':1})])
def test_double_producing_filter_then_unsupported_gaussian_refuses_before_pixels(name,parameters):
    owner,_,reads=fixture();original=capture(owner.snapshot())
    steps=[operation(original,parameters=options(space='hsb32')),ij.NativeWorkflowOperation(name,parameters,target(6)),
           ij.NativeWorkflowOperation('gaussian_blur_imagej',{'sigma':1.},target(6))]
    with pytest.raises(ValueError):ij.prepare_native_workflow(owner.snapshot(),steps)
    assert reads.counts().plane_reads==0

def test_empty_positioned_masks_leave_promoting_filter_dtype_unchanged():
    owner,pixels,_=fixture();tx=owner.begin_edit();tx.set_rois([]);owner.commit(tx);owner=ij.Document(owner.snapshot())
    original=capture(owner.snapshot());request=target(6);request.roi=ij.RoiPolicy.POSITIONED_ROIS
    prepared=ij.prepare_native_workflow(owner.snapshot(),[operation(original,parameters=options(space='hsb32')),
        ij.NativeWorkflowOperation('find_edges',{},request)])
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYX'),split_expected(pixels,'hsb32'))

@pytest.mark.parametrize('name',['convert','calculate'])
@pytest.mark.parametrize('dtype,space',[('uint8','hsb8'),('float32','hsb32'),('float64',None)])
def test_explicit_registry_output_dtype_is_staged(name,dtype,space):
    owner,_,reads=fixture();original=capture(owner.snapshot())
    middle=ij.NativeWorkflowOperation('convert',{'dtype':dtype,'scale':False},target(6))
    final=operation(original,c=6,parameters=options(action='merge',space=space or 'hsb32'))
    record=ij.NativeWorkflowProgram([operation(original,parameters=options(space='hsb32')),middle,final]).record()
    if name=='calculate':
        record['steps'][1]['operation']='calculate'
        record['steps'][1]['parameters']={'dtype':{'kind':'text','value':dtype},'other':{'kind':'image','value':{'width':3,'height':2,'channels':1,'type':2,'values':[0.]*6}}}
    program=ij.NativeWorkflowProgram.from_record(record)
    if dtype=='float64':
        with pytest.raises(ValueError):ij.prepare_native_workflow(owner.snapshot(),program.steps)
        assert reads.counts().plane_reads==0
    else:assert ij.prepare_native_workflow(owner.snapshot(),program.steps).preview.dataset.shape==(2,2,2,2,3,3)

@pytest.mark.parametrize('all_documents',[False,True])
def test_session_batch_uses_color_stack_program_and_preserves_tables(tmp_path,all_documents):
    owner,pixels,_=fixture();other=ij.Document(owner.snapshot());source=ij.SessionCapture([owner.snapshot(),other.snapshot()])
    source.results=[{'historical':42}];source.workflow=[{'never':'execute'}];source.extensions={'opaque':[True,None]}
    path=tmp_path/'input.ijx';ij.save_session(str(path),source);original_bytes=path.read_bytes()
    output=tmp_path/'out';output.mkdir()
    step=operation(capture(owner.snapshot()),parameters=options(space='hsb32'))
    program=ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([step]).json())
    prepared=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(path),'color')],program.steps,str(output),ij.NativeWorkflowBatchOptions(all_documents=all_documents))
    assert not (output/'color.ijx').exists();prepared.publish()
    loaded=ij.load_session(str(output/'color.ijx'));first,second=[d.snapshot() for d in loaded.documents]
    np.testing.assert_array_equal(first.dataset.numpy('TCZYX'),split_expected(pixels,'hsb32'))
    if all_documents:np.testing.assert_array_equal(second.dataset.numpy('TCZYX'),split_expected(pixels,'hsb32'))
    else:np.testing.assert_array_equal(second.dataset.numpy('TCZYXS'),pixels)
    assert loaded.results==source.results and loaded.workflow==source.workflow and loaded.extensions==source.extensions
    assert path.read_bytes()==original_bytes and owner.undo_steps==0

def test_actual_color_stack_choice_inputs_in_both_hosts(tmp_path):
    from imagejxx import _desktop
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    pixels=np.empty((2,2,2,2,3,3),np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):
                for k in range(3):pixels[t,c,z,...,k]=10+50*t+20*c+z+np.arange(6).reshape(2,3)+20*k
    expected=split_expected(pixels,'hsb32').reshape(24,6)
    for report in [cpp,python]:
        data=report['color_stack_workflow']
        assert data['shape']==data['redo_shape']==[2,6,2,2,3,1] and data['undo_shape']==[2,2,2,2,3,3]
        np.testing.assert_array_equal(np.array(data['pixels'],np.float32),expected)
        assert data['history_before_apply']==0 and data['undo_steps']==1 and data['spacing']==[2,3,5]
        assert data['view']=={'t':1,'c':3,'crosshair':[1,1,1]}
        program=data['workflow'][0];step=program['steps'][0]
        assert step['original']==data['original'] and step['original']['request']['space']=='rgb'
        assert step['parameters']['space']=={'kind':'text','value':'hsb32'}
        assert data['editor_before']['color_stack'] and data['review']['ready']
        assert data['layers']==3
        legacy=report['legacy_color_workflow']
        assert legacy['shape']==legacy['redo_shape']==[1,3,8,2,3,1] and legacy['unit']=='px'
        assert legacy['history_before_apply']==0 and legacy['undo_steps']==1 and legacy['layers']==0
        assert legacy['undo_unit']=='um' and legacy['undo_layers']==1
        parameters=legacy['workflow'][0]['steps'][0]['parameters']
        assert parameters['allow_calibration_reset']=={'kind':'bool','value':True}
        assert parameters['allow_nonxy_annotation_drop']=={'kind':'bool','value':True}
        assert legacy['workflow'][0]['steps'][0]['original']==data['original']
        legacy_expected=split_expected(pixels,'hsb32').reshape(2,2,3,2,6).transpose(2,0,3,1,4).reshape(24,6)
        np.testing.assert_array_equal(np.array(legacy['pixels'],np.float32),legacy_expected)
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
