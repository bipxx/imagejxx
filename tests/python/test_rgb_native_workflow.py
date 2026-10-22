"""Mixed RGB programs validate the evolving stage before acquiring any samples."""
import copy
import io
import json
import zipfile
import numpy as np
import pytest
import imagejxx as ij


def target(channels=2):
    return ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,2),c=ij.AxisRange(0,channels),
        z=ij.AxisRange(0,2),roi=ij.RoiPolicy.WHOLE_PLANE)


def fixture():
    pixels=np.zeros((2,2,2,2,3),np.uint16)
    for t in range(2):
        for c in range(2):
            for z in range(2):pixels[t,c,z]=10+100*t+20*c+z+np.arange(6).reshape(2,3)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes='TCZYX',
        calibration=ij.SpatialCalibration(spacing=[2,3,5],unit='um')).observe_reads(reads))
    view=owner.snapshot().display;view.t=view.c=1;view.crosshair=[1,1,1]
    view.properties={'channel_display':{'schema':'imagejxx.channel-display','version':1,'mode':'composite','channels':[
        {'visible':True,'range':[0,255],'lut':{'name':'Red'}},{'visible':True,'range':[0,255],'lut':{'name':'Green'}}]},'future_display':{'keep':True}}
    owner.set_display(view)
    tx=owner.begin_edit();tx.select(ij.Selection('rectangle',[(0,0),(1,1)]));owner.commit(tx)
    layer=ij.PlanarDisplayAnnotation('text',ij.TextAnnotationPayload([0,0,1,1],'kept'),'xy',1,t=1,c=1)
    owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),layer));owner=ij.Document(owner.snapshot())
    return owner,pixels,reads


def capture(source,mode='composite',destination=ij.OutputDestination.NEW_DOCUMENT):
    record=ij.resolve_rgb_dataset_command(source,mode=ij.RgbDatasetMode.COMPOSITE if mode=='composite' else ij.RgbDatasetMode.PER_PLANE,destination=destination).record()
    return record


def rgb(source,mode='composite',destination=ij.OutputDestination.NEW_DOCUMENT,enabled=True):
    return ij.NativeWorkflowOperation('rgb_dataset',{},target(),original=capture(source,mode,destination),enabled=enabled)


def expected_rgb(pixels,mode,prefix=0,after=0):
    out=np.full((2,1 if mode=='composite' else 2,2,2,3,3),after,np.uint8)
    out[:,0,...,0]=pixels[:,0]+prefix+after
    out[:,0 if mode=='composite' else 1,...,1]=pixels[:,1]+prefix+after
    return out


@pytest.mark.parametrize('mode',['per_plane','composite'])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_mixed_program_has_one_paired_publication_and_exact_intermediate_axes(mode,destination):
    owner,pixels,reads=fixture();source=owner.snapshot()
    steps=[ij.NativeWorkflowOperation('add',{'value':1},target()),rgb(source,mode,destination),
           ij.NativeWorkflowOperation('add',{'value':2},target(1 if mode=='composite' else 2))]
    program=ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram(steps).json())
    assert reads.counts().plane_reads==0
    prepared=ij.prepare_native_workflow(source,program.steps)
    assert prepared.completed_steps==3 and prepared.dependencies==ij.Change.ALL and owner.undo_steps==0
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'),expected_rgb(pixels,mode,1,2))
    assert ij.planar_display_annotations(prepared.preview)[0].c==(0 if mode=='composite' else 1)
    owner.commit(prepared.begin_edit());assert owner.undo_steps==1
    assert owner.snapshot().display.properties=={'future_display':{'keep':True}}
    assert owner.snapshot().dataset.calibration.spacing==[2,3,5]
    owner.undo();assert owner.snapshot().display.properties==source.display.properties
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
    owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),expected_rgb(pixels,mode,1,2))


@pytest.mark.parametrize('mode',['per_plane','composite'])
@pytest.mark.parametrize('dtype',['uint8','uint16','float32'])
def test_rgb_grayscale_then_filter_remaps_sample_bound_display_annotations(mode,dtype):
    owner,pixels,_=fixture();channels=1 if mode=='composite' else 2
    steps=[rgb(owner.snapshot(),mode),ij.NativeWorkflowOperation('convert_image',dict(dtype=dtype,scale=False,
        weighted_rgb=True,red_weight=1.,green_weight=0.,blue_weight=0.),target(channels)),
        ij.NativeWorkflowOperation('add',{'value':1},target(channels))]
    plan=ij.prepare_native_workflow(owner.snapshot(),steps)
    expected=expected_rgb(pixels,mode)[...,0].astype(dtype)+1
    np.testing.assert_array_equal(plan.preview.dataset.numpy('TCZYX'),expected)
    assert len(ij.planar_display_annotations(plan.preview))==1
    owner.commit(plan.begin_edit());owner.undo();assert len(ij.planar_display_annotations(owner.snapshot()))==1


@pytest.mark.parametrize('bad',['old_channels','rank_on_rgb','gaussian_after_float64','bad_export'])
def test_invalid_later_stage_is_refused_before_prefix_pixels(bad):
    owner,_,reads=fixture();source=owner.snapshot();steps=[ij.NativeWorkflowOperation('add',{'value':1},target()),rgb(source)]
    if bad=='old_channels':steps.append(ij.NativeWorkflowOperation('add',{'value':1},target(2)))
    elif bad=='rank_on_rgb':steps.append(ij.NativeWorkflowOperation('rank_filter_imagej',dict(method='median',radius=1),target(1)))
    elif bad=='gaussian_after_float64':
        steps.extend([ij.NativeWorkflowOperation('convert_image',dict(dtype='float64',scale=False),target(1)),
                      ij.NativeWorkflowOperation('gaussian_blur_imagej',dict(sigma=1.),target(1))])
    else:
        item=ij.ImageJAnnotationExportItem('bad.roi',None,ij.ImageJAnnotationKind.SELECTION,999,
            ij.ImageJRoi(ij.ROI('bad',ij.Selection('rectangle',[(0,0),(1,1)]))),fresh_source=True)
        steps.append(ij.NativeWorkflowExport('bad.zip',[item]))
    with pytest.raises((ValueError,IndexError)):ij.prepare_native_workflow(source,steps)
    assert reads.counts().plane_reads==0 and source.shares_data(owner.snapshot()) and owner.undo_steps==0


@pytest.mark.parametrize('version',[1,2,3])
def test_portable_rgb_program_versions_rebind_capture_to_another_owner(version):
    owner,pixels,reads=fixture();original=rgb(owner.snapshot());record=ij.NativeWorkflowProgram([original]).record();record['version']=version
    for step in record['steps']:
        if version>=2:step['filter_options']=None
        if version==3:step['enabled']=True
    program=ij.NativeWorkflowProgram.from_json(json.dumps(record));other=ij.Document(owner.snapshot())
    plan=ij.prepare_native_workflow(other.snapshot(),program.steps)
    np.testing.assert_array_equal(plan.preview.dataset.numpy('TCZYXS'),expected_rgb(pixels,'composite'))
    assert program.record()['steps'][0]['original']==record['steps'][0]['original'] and other.undo_steps==0


@pytest.mark.parametrize('bad',['missing_capture','capture_field','parameters','partial','selection','calibration'])
def test_rgb_intent_is_closed_and_original_is_validated_before_any_kernel(bad):
    owner,_,reads=fixture();source=owner.snapshot();record=capture(source)
    parameters={};request=target();consent=False
    if bad=='missing_capture':record=None
    elif bad=='capture_field':record=copy.deepcopy(record);record['extra']=True
    elif bad=='parameters':parameters={'mode':'composite'}
    elif bad=='partial':request=target(1)
    elif bad=='selection':request.roi=ij.RoiPolicy.ACTIVE_SELECTION
    else:consent=True
    with pytest.raises((ValueError,IndexError)):
        step=ij.NativeWorkflowOperation('rgb_dataset',parameters,request,original=record,use_destination_calibration=consent)
        ij.prepare_native_workflow(source,[ij.NativeWorkflowOperation('add',{'value':1},target()),step])
    assert reads.counts().plane_reads==0 and owner.undo_steps==0


def test_disabled_rgb_validates_intent_but_does_not_change_stage():
    owner,pixels,reads=fixture();step=rgb(owner.snapshot(),enabled=False)
    program=ij.NativeWorkflowProgram([step,ij.NativeWorkflowOperation('add',{'value':1},target())])
    plan=ij.prepare_native_workflow(owner.snapshot(),program.steps)
    assert plan.completed_steps==1
    np.testing.assert_array_equal(plan.preview.dataset.numpy('TCZYX'),pixels+1)


def test_stale_display_refuses_whole_workflow_publication():
    owner,_,_=fixture();plan=ij.prepare_native_workflow(owner.snapshot(),[rgb(owner.snapshot())])
    view=owner.snapshot().display;view.t=0;owner.set_display(view)
    with pytest.raises(RuntimeError):owner.commit(plan.begin_edit())
    assert owner.undo_steps==0 and owner.snapshot().dataset.shape[-1]==1


def test_budget_and_cancellation_do_not_publish_partial_rgb():
    owner,pixels,reads=fixture();source=owner.snapshot();steps=[rgb(source),ij.NativeWorkflowOperation('add',{'value':1},target(1))]
    with pytest.raises(ValueError):ij.prepare_native_workflow(source,steps,ij.NativeWorkflowLimits(max_retained_bytes=65536))
    assert reads.counts().plane_reads==0
    token=ij.CancellationToken()
    def progress(done,total):token.cancel()
    with pytest.raises(RuntimeError):ij.prepare_native_workflow(source,steps,cancellation=token,progress=progress)
    assert owner.undo_steps==0 and source.shares_data(owner.snapshot())


def test_import_rgb_export_uses_remapped_channel_geometry_and_stage_source():
    owner,pixels,reads=fixture();source=owner.snapshot();shape=[2,2,2,2,3,1]
    selection=ij.Selection('rectangle',[(.25,.25),(1.75,1.75)])
    original=ij.ImageJRoi(ij.ROI('square',selection,t=1,c=1,z=1))
    mapping=ij.ImageJRoiPlaneMapping();mapping.extent=shape;mapping.plane='xy';mapping.coordinate=1
    imported=ij.ImageJRoiImportItem('square.roi',ij.encode_imagej_roi(original),mapping)
    projection=ij.ImageJRoi(ij.ROI('square',selection,t=1,c=0,z=1))
    item=ij.ImageJAnnotationExportItem('square.roi',0,ij.ImageJAnnotationKind.SELECTION,0,projection)
    plan=ij.prepare_native_workflow(source,[ij.NativeWorkflowImport([imported]),rgb(source),ij.NativeWorkflowExport('out.zip',[item])])
    assert len(plan.outputs)==1 and ij.planar_annotations(plan.preview)[0].c==0
    output=plan.outputs[0];assert output['source'].dataset.shape[1]==1 and output['source'].dataset.shape[-1]==3
    with zipfile.ZipFile(io.BytesIO(output['data'])) as z:
        mapped=ij.decode_imagej_roi_native_envelope(z.read('square.roi'))
        assert mapped.target_extent==[2,1,2,2,3,3] and mapped.annotation.c==0
    assert owner.undo_steps==0 and source.shares_data(owner.snapshot())


def test_actual_mixed_rgb_review_inputs_in_both_hosts(tmp_path):
    from imagejxx import _desktop
    import os
    from pathlib import Path
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    for report in [cpp,python]:
        mixed=report['mixed_workflow'];assert mixed['shape']==mixed['redo_shape']==[2,1,2,2,3,3] and mixed['undo_shape']==[2,2,2,2,3,1]
        expected=[]
        for t in range(2):
            for z in range(2):
                image=np.full((2,3,3),2,np.uint8);image[...,0]=13+100*t+z+np.arange(6).reshape(2,3);image[...,1]=33+100*t+z+np.arange(6).reshape(2,3);expected.append(image.reshape(-1))
        np.testing.assert_array_equal(mixed['pixels'],expected)
        assert mixed['history_before_apply']==0 and mixed['undo_steps']==1
        assert mixed['workflow']==[mixed['program']] and mixed['review']['ready']
        assert [s['operation'] for s in mixed['program']['steps']]==['add','rgb_dataset','add']
        assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])


def test_recorded_color_channel_is_independent_of_current_view():
    owner,pixels,reads=fixture();view=owner.snapshot().display
    state=view.properties['channel_display'];state['mode']='color';properties=view.properties;properties['channel_display']=state;view.properties=properties;owner.set_display(view)
    step=rgb(owner.snapshot());view=owner.snapshot().display;view.c=0;view.t=0;view.crosshair=[0,0,0];owner.set_display(view)
    plan=ij.prepare_native_workflow(owner.snapshot(),[step])
    expected=np.zeros((2,1,2,2,3,3),np.uint8);expected[:,0,...,1]=pixels[:,1]
    np.testing.assert_array_equal(plan.preview.dataset.numpy('TCZYXS'),expected)
    assert plan.preview.display.t==1 and plan.preview.display.crosshair[2]==1


def test_recorded_deferred_range_uses_captured_tz_after_prefix_processing():
    owner,pixels,reads=fixture();view=owner.snapshot().display;view.properties={};owner.set_display(view)
    table=np.zeros((256,3),np.uint8);table[:,0]=np.arange(256,dtype=np.uint8)
    record=ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,lut=table).record()
    step=ij.NativeWorkflowOperation('rgb_dataset',{},target(),original=record)
    view=owner.snapshot().display;view.t=0;view.c=0;view.crosshair=[0,0,0];owner.set_display(view)
    plan=ij.prepare_native_workflow(owner.snapshot(),[ij.NativeWorkflowOperation('add',{'value':1},target()),step])
    expected=np.zeros((2,1,2,2,3,3),np.uint8);expected[1,...,0]=255
    np.testing.assert_array_equal(plan.preview.dataset.numpy('TCZYXS'),expected)


def test_rgb_noop_restores_captured_stage_position_for_later_current_target():
    pixels=np.full((2,2,2,2,3,3),10,np.uint8);owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS'))
    view=owner.snapshot().display;view.t=view.c=1;view.crosshair=[1,1,1];owner.set_display(view)
    step=rgb(owner.snapshot(),'per_plane')
    view=owner.snapshot().display;view.t=view.c=0;view.crosshair=[0,0,0];owner.set_display(view)
    current=ij.TargetRequest(ij.TargetScope.CURRENT_PLANE,roi=ij.RoiPolicy.WHOLE_PLANE)
    plan=ij.prepare_native_workflow(owner.snapshot(),[step,ij.NativeWorkflowOperation('add',{'value':1},current)])
    expected=pixels.copy();expected[1,1,1]+=1
    np.testing.assert_array_equal(plan.preview.dataset.numpy('TCZYXS'),expected)
    assert owner.snapshot().display.t==0 and owner.undo_steps==0


import hashlib
from test_contrast_apply import STACK_FIXTURE, rgb_stack_owner, rgb_stack_steps, rgb_options


@pytest.mark.parametrize("row",STACK_FIXTURE["rows"],ids=lambda r:f'portable-stack-r{r["range_index"]}-c{r["channels"]}-roi{r["roi"]}-a{r["anchor"]}-p{int(r["prefix"])}-w{int(r["whole_plane"])}')
def test_rgb_stack_workflow_matches_actual_imagej_with_explicit_anchor_and_preserved_navigation(row):
    reads=ij.DatasetReadObserver();owner=rgb_stack_owner(row,reads);command=ij.resolve_rgb_contrast_stack_command(owner.snapshot(),rgb_stack_steps(row))
    step=ij.make_rgb_contrast_stack_workflow_step(command);program=ij.NativeWorkflowProgram([step]);saved=program.record()
    assert saved["steps"][0]["target"]["positions"]==[STACK_FIXTURE["anchors"][row["anchor"]]] and saved["steps"][0]["original"]==command.record()
    program=ij.NativeWorkflowProgram.from_json(program.json());assert ij.NativeWorkflowProgram.from_record(saved).record()==program.record()==saved and reads.counts().plane_reads==0
    t,c,z=STACK_FIXTURE["anchors"][row["anchor"]];view=owner.snapshot().display;view.t,view.c=(t+1)%3,(c+1)%3;view.crosshair=[12,3,(z+1)%3];view.zoom=1.5;owner.set_display(view)
    before=owner.snapshot();prepared=ij.prepare_native_workflow(before,program.steps)
    expected_reads=0 if row["roi"]==3 and not row["whole_plane"] else 1 if row["roi"]==3 else 27
    assert reads.counts().plane_reads==expected_reads and owner.undo_steps==0 and owner.snapshot().shares_data(before) and prepared.completed_steps==1 and prepared.dependencies==ij.Change.ALL
    preview=prepared.preview;pixels=preview.dataset.numpy("TCZYXS");assert hashlib.sha256(pixels.tobytes()).hexdigest()==row["stack_sha256"] and preview.selection==before.selection
    assert (preview.display.t,preview.display.c,preview.display.crosshair,preview.display.zoom)==(view.t,view.c,view.crosshair,view.zoom)
    owner.commit(prepared.begin_edit());assert owner.undo_steps==int(expected_reads!=0)
    if expected_reads:
        assert owner.snapshot().display.properties=={"display_range":[0.,255.]};owner.undo();assert owner.snapshot().shares_data(before) and owner.snapshot().display.properties==before.display.properties
        owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYXS"),pixels)
    else:assert owner.snapshot().shares_data(before) and owner.snapshot().display.properties==before.display.properties
    assert program.record()==saved


def rgb_stack_workflow_small():
    pixels=np.tile(np.array([[[20,30,40]]],np.uint8),(2,1,1,1));reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="ZYXS").observe_reads(reads))
    step=ij.RgbContrastStackStep();step.options=rgb_options(10.9,40.1,4)
    command=ij.resolve_rgb_contrast_stack_command(owner.snapshot(),[step]);return owner,reads,ij.make_rgb_contrast_stack_workflow_step(command)


@pytest.mark.parametrize("bad",["missing","parameters","calibration","filter","current","all_z","ranges","empty_positions","many_positions","whole_roi","positioned_roi","volume","new_document","unused_range","negative"])
def test_rgb_stack_workflow_rejects_ambiguous_intent_before_reads(bad):
    owner,reads,step=rgb_stack_workflow_small();record=ij.NativeWorkflowProgram([step]).record();row=record["steps"][0];target=row["target"]
    if bad=="missing":row["original"]=None
    elif bad=="parameters":row["parameters"]={"minimum":{"kind":"real","value":10.}}
    elif bad=="calibration":row["use_destination_calibration"]=True
    elif bad=="filter":
        request=ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(0,0,0)])
        invalid=ij.NativeWorkflowOperation("rgb_contrast_stack",{},request,original=row["original"],filter_options=ij.GaussianBlurCommandOptions())
        with pytest.raises(ValueError):ij.NativeWorkflowProgram([invalid])
        assert reads.counts().plane_reads==0;return
    elif bad in ["current","all_z","ranges"]:target["scope"]={"current":0,"all_z":1,"ranges":2}[bad];target["positions"]=[]
    elif bad=="empty_positions":target["positions"]=[]
    elif bad=="many_positions":target["positions"]=[[0,0,0],[0,0,1]]
    elif bad=="whole_roi":target["roi"]=0
    elif bad=="positioned_roi":target["roi"]=2
    elif bad=="volume":target["mode"]=1
    elif bad=="new_document":target["destination"]=1
    elif bad=="unused_range":target["t"]=[0,1,1]
    else:target["positions"]=[[-1,0,0]]
    with pytest.raises((ValueError,TypeError,IndexError)):ij.NativeWorkflowProgram.from_record(record)
    assert reads.counts().plane_reads==0 and owner.undo_steps==0


@pytest.mark.parametrize("bad",["anchor","scalar_after_conversion","late_original","record_limit","working_limit"])
def test_rgb_stack_workflow_late_failure_preflights_without_prefix_reads(bad):
    owner,reads,step=rgb_stack_workflow_small();record=ij.NativeWorkflowProgram([step]).record();row=record["steps"][0]
    prefix=ij.NativeWorkflowOperation("add",{"value":1},ij.TargetRequest(ij.TargetScope.ALL_Z,roi=ij.RoiPolicy.WHOLE_PLANE));limits=ij.NativeWorkflowLimits()
    if bad=="anchor":row["target"]["positions"]=[[0,0,2]];step=ij.NativeWorkflowProgram.from_record(record).steps[0]
    elif bad=="scalar_after_conversion":
        request=ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,1),c=ij.AxisRange(0,1),z=ij.AxisRange(0,2),roi=ij.RoiPolicy.WHOLE_PLANE)
        prefix=ij.NativeWorkflowOperation("convert_image",{"dtype":"uint8","scale":False,"weighted_rgb":False},request)
    elif bad=="late_original":
        capture=copy.deepcopy(row["original"]);capture["stages"][0]["whole_plane"]=1
        step=ij.NativeWorkflowOperation("rgb_contrast_stack",{},ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(0,0,0)]),original=capture)
    elif bad=="record_limit":
        with pytest.raises(ValueError):ij.NativeWorkflowProgram([step],ij.NativeWorkflowProgramLimits(max_record_bytes=512))
        assert reads.counts().plane_reads==0;return
    else:limits=ij.NativeWorkflowLimits(max_retained_bytes=1)
    before=owner.snapshot()
    expected=IndexError if bad=="anchor" else (ValueError,TypeError)
    with pytest.raises(expected):ij.prepare_native_workflow(before,[prefix,step],limits)
    assert reads.counts().plane_reads==0 and owner.snapshot().shares_data(before) and owner.undo_steps==0


@pytest.mark.parametrize("change",["pixels","selection","display","metadata"])
def test_rgb_stack_workflow_pending_publication_guards_all_revisions(change):
    owner,_,step=rgb_stack_workflow_small();source=owner.snapshot();prepared=ij.prepare_native_workflow(source,[step])
    if change=="pixels":owner.commit(prepared.begin_edit())
    elif change=="selection":owner.select(ij.Selection("rectangle",[(0,0),(1,1)]))
    elif change=="display":
        view=source.display;view.zoom=2;owner.set_display(view)
    else:
        edit=owner.begin_edit();edit.set_title("new title");owner.commit(edit)
    stable=owner.snapshot();history=owner.undo_steps
    with pytest.raises(ij.RevisionConflict):owner.commit(prepared.begin_edit())
    assert owner.snapshot().shares_data(stable) and owner.undo_steps==history


@pytest.mark.parametrize("failure",["pre_cancel","late_cancel","callback"])
def test_rgb_stack_workflow_cancellation_and_failure_never_publish(failure):
    owner,reads,step=rgb_stack_workflow_small();before=owner.snapshot();token=ij.CancellationToken()
    if failure=="pre_cancel":token.cancel()
    def progress(done,total):
        if failure=="callback":raise RuntimeError("workflow callback failure")
        if failure=="late_cancel":token.cancel()
    with pytest.raises((ij.OperationCancelled,RuntimeError)):ij.prepare_native_workflow(before,[step],cancellation=token,progress=progress)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    assert reads.counts().plane_reads==(0 if failure=="pre_cancel" else 2)


def test_rgb_stack_workflow_disabled_anchor_retains_valid_intent_without_resolving_destination():
    owner,reads,step=rgb_stack_workflow_small();record=ij.NativeWorkflowProgram([step]).record();original=record["steps"][0]["original"]
    disabled=ij.NativeWorkflowOperation("rgb_contrast_stack",{},ij.TargetRequest(ij.TargetScope.POSITIONS,positions=[ij.PlanePosition(999,0,0)]),original=original,enabled=False)
    program=ij.NativeWorkflowProgram.from_json(ij.NativeWorkflowProgram([disabled]).json());assert program.record()["version"]==3
    before=owner.snapshot();prepared=ij.prepare_native_workflow(before,program.steps);owner.commit(prepared.begin_edit())
    assert prepared.completed_steps==0 and reads.counts().plane_reads==0 and owner.undo_steps==0 and owner.snapshot().shares_data(before)


@pytest.mark.parametrize("all_documents",[False,True])
def test_rgb_stack_workflow_batch_preserves_session_tables_navigation_and_other_documents(tmp_path,all_documents):
    first,_,_=rgb_stack_workflow_small();second=ij.Document(ij.Dataset(np.tile(np.array([[[30,40,50]]],np.uint8),(2,1,1,1)),axes="ZYXS"))
    source=first.snapshot();view=source.display;view.crosshair=[0,0,1]
    step1=ij.RgbContrastStackStep();step1.options=rgb_options(10,40,7);step2=ij.RgbContrastStackStep();step2.options=rgb_options(10.9,40.1,4)
    command=ij.resolve_rgb_contrast_stack_command(source.with_display(view),[step1,step2]);step=ij.make_rgb_contrast_stack_workflow_step(command)
    capture=ij.SessionCapture([first.snapshot(),second.snapshot()]);capture.results=[{"kept":True}];capture.workflow=[{"schema":"kept"}]
    path=tmp_path/"input.ijx";ij.save_session(str(path),capture);before=path.read_bytes();out=tmp_path/"out";out.mkdir()
    batch=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(path),"result")],[step],str(out),ij.NativeWorkflowBatchOptions(all_documents=all_documents));batch.publish()
    result=ij.load_session(str(out/"result.ijx"));assert result.results==capture.results and result.workflow==capture.workflow and path.read_bytes()==before
    np.testing.assert_array_equal(result.documents[0].snapshot().dataset.numpy("ZYXS"),np.array([[[[85,30,40]]],[[[255,170,255]]]],np.uint8))
    expected=np.array([[[[170,40,50]]],[[[255,255,255]]]],np.uint8) if all_documents else np.tile(np.array([[[30,40,50]]],np.uint8),(2,1,1,1))
    np.testing.assert_array_equal(result.documents[1].snapshot().dataset.numpy("ZYXS"),expected)
    assert result.documents[0].snapshot().display.crosshair==[0,0,0] and first.undo_steps==second.undo_steps==0
