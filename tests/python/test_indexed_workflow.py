"""Indexed captures bind palette intent to each evolving workflow input."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij

CAP=512*1024*1024

def fixture(other=False):
    colors=[[0,0,255],[255,255,0]] if other else [[255,0,0],[0,255,0]]
    pixels=np.array(colors,np.uint8).reshape(1,1,1,1,2,3)
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS').observe_reads(reads))
    return owner,pixels,reads

def indexed(other=False):
    owner,pixels,reads=fixture(other)
    result=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(owner.snapshot(),max_colors=2)).preview
    return ij.Document(result),pixels

def target():
    return ij.TargetRequest(ij.TargetScope.RANGES,t=ij.AxisRange(0,1),c=ij.AxisRange(0,1),z=ij.AxisRange(0,1),roi=ij.RoiPolicy.WHOLE_PLANE)

def step(op,record,parameters=None,**kwargs):
    return ij.NativeWorkflowOperation(op,parameters or {},target(),original=record,**kwargs)

@pytest.mark.parametrize('palette',[ij.IndexedRgbPaletteSource.Display,ij.IndexedRgbPaletteSource.Stored])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_typed_expansion_replay_rebind_history(palette,destination):
    owner,pixels=indexed();other,other_pixels=indexed(True)
    command=ij.resolve_indexed_rgb_command(owner.snapshot(),palette_source=palette,destination=destination)
    record=command.record();ij.validate_indexed_rgb_command_record(record)
    loaded=ij.indexed_rgb_command_from_record(owner.snapshot(),json.loads(json.dumps(record)))
    prepared=ij.prepare_indexed_rgb(loaded)
    assert prepared.begin_edit().dependencies==ij.Change.ALL
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'),pixels)
    rebound=ij.rebind_indexed_rgb_command(other.snapshot(),record,palette_source=palette)
    np.testing.assert_array_equal(ij.prepare_indexed_rgb(rebound).preview.dataset.numpy('TCZYXS'),other_pixels)
    assert record==command.record() and owner.undo_steps==0
    if destination==ij.OutputDestination.IN_PLACE:
        owner.commit(prepared.begin_edit());assert owner.undo_steps==1
        owner.undo();assert owner.snapshot().dataset.shape[-1]==1
        owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels)
    else:
        app=ij.Application();app.adopt(owner);assert app.publish(prepared).document.id!=owner.id

@pytest.mark.parametrize('version',[1,2,3])
@pytest.mark.parametrize('authored',[False,True])
def test_pipeline_resolves_each_input_palette(version,authored):
    first,pixels,reads=fixture();second,other,_=fixture(True)
    capture=ij.resolve_indexed_dataset_command(first.snapshot(),max_colors=2).record()
    old_index=ij.prepare_indexed_dataset(ij.resolve_indexed_dataset_command(first.snapshot(),max_colors=2)).preview
    expansion=ij.resolve_indexed_rgb_command(old_index).record()
    quantize=step('indexed_dataset',capture,{'max_colors':2,'max_working_bytes':CAP} if authored else {})
    expand=step('indexed_rgb',expansion,{'palette_source':'stored','max_working_bytes':CAP} if authored else {})
    record=ij.NativeWorkflowProgram([quantize,expand]).record();record['version']=version
    if version>=2:
        for s in record['steps']:s['filter_options']=None
    if version==3:
        for s in record['steps']:s['enabled']=True
    if version<3:
        for s in record['steps']:s.pop('enabled',None)
    if version<2:
        for s in record['steps']:s.pop('filter_options',None)
    program=ij.NativeWorkflowProgram.from_record(record)
    for owner,expected in [(first,pixels),(second,other)]:
        prepared=ij.prepare_native_workflow(owner.snapshot(),program.steps)
        np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'),expected)
        assert prepared.dependencies==ij.Change.ALL and prepared.completed_steps==2
        owner.commit(prepared.begin_edit());assert owner.undo_steps==1
        owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),expected)
    assert program.record()['steps'][0]['original']==capture
    assert program.record()['steps'][1]['original']==expansion

def test_legacy_rgb_capture_stays_literal_new_expansion_is_dynamic():
    a,_=indexed();b,_=indexed(True)
    old=ij.resolve_rgb_dataset_command(a.snapshot()).record()
    new=ij.resolve_indexed_rgb_command(a.snapshot()).record()
    frozen=ij.prepare_native_workflow(b.snapshot(),[step('rgb_dataset',old)]).preview
    dynamic=ij.prepare_native_workflow(b.snapshot(),[step('indexed_rgb',new)]).preview
    indices=b.snapshot().dataset.numpy('TCZYX')
    np.testing.assert_array_equal(frozen.dataset.numpy('TCZYXS'),np.array(ij.indexed_palette(a.snapshot())['palette'],np.uint8)[indices])
    np.testing.assert_array_equal(dynamic.dataset.numpy('TCZYXS'),np.array(ij.indexed_palette(b.snapshot())['palette'],np.uint8)[indices])
    assert not np.array_equal(frozen.dataset.numpy('TCZYXS'),dynamic.dataset.numpy('TCZYXS'))

@pytest.mark.parametrize('mutation',['extra','version','policy','source','palette','cap','layout','metadata','nested','pose'])
def test_bad_expansion_records(mutation):
    owner,_=indexed();record=ij.resolve_indexed_rgb_command(owner.snapshot()).record()
    if mutation=='extra':record['extra']=1
    elif mutation=='version':record['version']=True
    elif mutation=='policy':record['policy']='future'
    elif mutation=='source':record['source']['shape'][-1]=3
    elif mutation=='palette':record['request']['palette_source']='other'
    elif mutation=='cap':record['request']['max_working_bytes']=1
    elif mutation=='layout':record['layout']['output_bytes']+=1
    elif mutation=='metadata':record['metadata_bytes']=-1
    elif mutation=='nested':record['rendering']['view_position'][0]=99
    else:record['view_position'][2]=99
    with pytest.raises(ValueError):ij.validate_indexed_rgb_command_record(record)

@pytest.mark.parametrize('op',['indexed_dataset','indexed_rgb'])
@pytest.mark.parametrize('bad',['partial','extra','bool','float','negative','range','no_capture','roi','target','disabled'])
def test_invalid_workflow_refuses_before_prefix_reads(op,bad):
    owner,_,reads=fixture();original=ij.resolve_indexed_dataset_command(owner.snapshot()).record()
    if op=='indexed_rgb':
        idx,_=indexed();original=ij.resolve_indexed_rgb_command(idx.snapshot()).record()
    params={'max_colors':2,'max_working_bytes':CAP} if op=='indexed_dataset' else {'palette_source':'stored','max_working_bytes':CAP}
    request=target();kwargs={}
    if bad=='partial':params.pop('max_working_bytes')
    elif bad=='extra':params['extra']=1
    elif bad=='bool':params['max_working_bytes']=True
    elif bad=='float':params['max_working_bytes']=float(CAP)
    elif bad=='negative':params['max_working_bytes']=-1
    elif bad=='range':params['max_colors' if op=='indexed_dataset' else 'palette_source']=257 if op=='indexed_dataset' else 'other'
    elif bad=='no_capture':original=None
    elif bad=='roi':request.roi=ij.RoiPolicy.ACTIVE_SELECTION
    elif bad=='target':request.c=ij.AxisRange(0,2)
    else:kwargs['enabled']=False;params['extra']=1
    first=ij.NativeWorkflowOperation('add',{'value':1},target())
    with pytest.raises((ValueError,TypeError,OverflowError)):
        last=ij.NativeWorkflowOperation(op,params,request,original=original,**kwargs)
        ij.prepare_native_workflow(owner.snapshot(),[first,last])
    assert reads.counts().plane_reads==0 and owner.undo_steps==0

def test_stored_palette_ignores_display_lut():
    owner,pixels=indexed();view=owner.snapshot().display
    view.properties={'lut':'Green','display_range':[0,1]};owner.set_display(view)
    stored=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(owner.snapshot(),palette_source=ij.IndexedRgbPaletteSource.Stored)).preview
    display=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(owner.snapshot())).preview
    np.testing.assert_array_equal(stored.dataset.numpy('TCZYXS'),pixels)
    assert not np.array_equal(display.dataset.numpy('TCZYXS'),pixels)

def test_cancel_cap_and_stale_display():
    owner,_=indexed();command=ij.resolve_indexed_rgb_command(owner.snapshot(),destination=ij.OutputDestination.IN_PLACE)
    with pytest.raises(ValueError):ij.prepare_indexed_rgb(command,execution_max_working_bytes=command.layout.working_bytes-1)
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_indexed_rgb(command,cancellation=token)
    prepared=ij.prepare_indexed_rgb(command);view=owner.snapshot().display;view.zoom=3;owner.set_display(view)
    with pytest.raises(ij.RevisionConflict):owner.commit(prepared.begin_edit())
    with pytest.raises(ij.RevisionConflict):ij.indexed_rgb_command_from_record(owner.snapshot(),command.record())

@pytest.mark.parametrize('all_documents',[False,True])
def test_session_batch_uses_each_input_palette(tmp_path,all_documents):
    a,pixels,_=fixture();b,other,_=fixture(True)
    capture=ij.resolve_indexed_dataset_command(a.snapshot(),max_colors=2).record()
    idx,_=indexed();expansion=ij.resolve_indexed_rgb_command(idx.snapshot()).record()
    steps=[step('indexed_dataset',capture),step('indexed_rgb',expansion)]
    session=ij.SessionCapture([a.snapshot(),b.snapshot()]);session.results=[{'kept':42}]
    session.workflow=[{'historical':'unexecuted'}];session.extensions={'future':[None,True]}
    path=tmp_path/'input.ijx';ij.save_session(str(path),session);original=path.read_bytes()
    out=tmp_path/'out';out.mkdir()
    prepared=ij.prepare_native_workflow_batch([ij.NativeWorkflowBatchInput(str(path),'indexed')],steps,str(out),ij.NativeWorkflowBatchOptions(all_documents=all_documents))
    assert not (out/'indexed.ijx').exists();prepared.publish()
    loaded=ij.load_session(str(out/'indexed.ijx'))
    for owner,expected in zip(loaded.documents,[pixels,other]):np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),expected)
    assert loaded.results==session.results and loaded.workflow==session.workflow and loaded.extensions==session.extensions
    assert path.read_bytes()==original and a.undo_steps==b.undo_steps==0

def test_unknown_palette_refused_stored_mode_ignores_future_channel_display():
    owner,pixels=indexed();view=owner.snapshot().display
    view.properties={'channel_display':{'schema':'imagejxx.channel-display','version':99}}
    owner.set_display(view)
    result=ij.prepare_indexed_rgb(ij.resolve_indexed_rgb_command(owner.snapshot(),palette_source=ij.IndexedRgbPaletteSource.Stored)).preview
    np.testing.assert_array_equal(result.dataset.numpy('TCZYXS'),pixels)
    with pytest.raises(ValueError):ij.resolve_indexed_rgb_command(owner.snapshot())
    record=ij.indexed_palette(owner.snapshot());record['version']=99
    tx=owner.begin_edit();tx.set_extension('imagejxx.indexed_color',record);owner.commit(tx)
    with pytest.raises(ValueError):ij.resolve_indexed_rgb_command(owner.snapshot(),palette_source=ij.IndexedRgbPaletteSource.Stored)

def test_indexed_parameter_inputs_in_both_hosts(tmp_path):
    from pathlib import Path
    import os
    from imagejxx import _desktop
    cpp=json.loads(Path(os.environ['IMAGEJXX_RGB_CPP_REPORT']).read_bytes())
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._rgb_dataset_ui_case(str(tmp_path/'capture001')))
    for report in [cpp,python]:
        data=report['indexed_workflow'];assert data['history_before_apply']==0 and data['undo_steps']==1
        assert data['shape']==data['redo_shape']==[1,1,1,1,2,3] and data['undo_shape']==[1,1,1,1,2,3]
        assert data['pixels']==[255,0,0,0,255,0] and data['review']['ready']
        steps=data['workflow'][0]['steps'];assert steps[0]['original']==data['quantize_original'] and steps[1]['original']==data['expand_original']
        assert steps[0]['parameters']['max_colors']=={'kind':'int64','value':2}
        assert steps[1]['parameters']['palette_source']=={'kind':'text','value':'stored'}
        assert data['palette']['color_count']==2 and any(any(row) for row in data['palette']['palette'])
