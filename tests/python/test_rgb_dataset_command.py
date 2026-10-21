"""RGB dataset intent, C collapse, metadata, guarded history and replay."""
import copy
import numpy as np
import pytest
import imagejxx as ij


def fixture(stored=True):
    pixels=np.zeros((2,2,2,2,3),np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2): pixels[t,c,z]=10+100*t+20*c+z
    reads=ij.DatasetReadObserver()
    calibration=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30],unit='um',frame_interval=2)
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYX',calibration=calibration).observe_reads(reads),'RGB source')
    selection=ij.Selection('rectangle',[(0,0),(1,1)])
    tx=owner.begin_edit();tx.select(selection)
    tx.set_rois([ij.ROI('one',selection,t=1,c=1,z=1),ij.ROI('wild',selection)])
    tx.set_overlays([dict(c=1,t=1,z=1,text='positioned'),dict(c=None,opaque=[True,None])])
    tx.set_property('plane_labels',[f'{t}/{c}/{z}' for t in range(2) for c in range(2) for z in range(2)])
    tx.set_extension('channel_names',['red source','green source']);tx.set_extension('is_stack',True);tx.set_extension('labels',True)
    tx.set_extension('opaque',{'preserve':[True,None,2**64-1]});owner.commit(tx)
    for plane in ('xy','xz','yz'):
        owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(),ij.PlanarAnnotation(plane,selection,plane,0,t=1,c=1)))
    layer=ij.PlanarDisplayAnnotation('text',ij.TextAnnotationPayload(text='label',bounds=[0,0,1,1]),'xz',0,t=1,c=1)
    owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),layer))
    owner=ij.Document(owner.snapshot());view=owner.snapshot().display;view.t=view.c=1;view.crosshair=[1,1,1]
    state={'schema':'imagejxx.channel-display','version':1,'mode':'composite','channels':[
        {'visible':True,'range':[0,255],'lut':{'name':'Red'}}, {'visible':True,'range':[0,255],'lut':{'name':'Green'}}]}
    view.properties={'manual_threshold':dict(version=1,lower=10,upper=200,t=1,c=1,z=1),'future_display':{'kept':True}}
    if stored:view.properties={**view.properties,'channel_display':state}
    owner.set_display(view);owner.mark_saved();app=ij.Application();app.adopt(owner)
    return owner,app,pixels,reads,state


def expected_rgb(pixels,composite):
    result=np.zeros((2,1 if composite else 2,2,2,3,3),np.uint8)
    result[:,0,...,0]=pixels[:,0]
    result[:,0 if composite else 1,...,1]=pixels[:,1]
    return result


@pytest.mark.parametrize('mode',[ij.RgbDatasetMode.PER_PLANE,ij.RgbDatasetMode.COMPOSITE])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_complete_conversion_metadata_and_publication(mode,destination):
    owner,app,pixels,reads,_=fixture();before=owner.snapshot();composite=mode==ij.RgbDatasetMode.COMPOSITE
    count=reads.counts().plane_reads
    command=ij.resolve_rgb_dataset_command(before,mode=mode,destination=destination)
    assert reads.counts().plane_reads==count
    record=command.record();ij.validate_rgb_dataset_command_record(record)
    restored=ij.rgb_dataset_command_from_record(before,record);assert restored.record()==record
    prepared=ij.prepare_rgb_dataset(restored)
    expected=expected_rgb(pixels,composite)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'),expected)
    assert owner.undo_steps==0 and len(app.document_ids)==1
    assert prepared.preview.display.properties=={'future_display':{'kept':True}}
    assert prepared.begin_edit().preview().display.properties==prepared.preview.display.properties
    after=app.publish(prepared).document.snapshot()
    np.testing.assert_array_equal(after.dataset.numpy('TCZYXS'),expected)
    assert after.dataset.calibration.spacing==before.dataset.calibration.spacing
    assert after.dataset.calibration.origin==before.dataset.calibration.origin
    assert after.selection.points==before.selection.points
    assert [(r.name,r.c) for r in after.rois]==[('one',0 if composite else 1),('wild',None)]
    assert all(r.c==(0 if composite else 1) for r in ij.planar_annotations(after))
    assert ij.planar_display_annotations(after)[0].c==(0 if composite else 1)
    assert after.overlays[0]['c']==(0 if composite else 1) and after.overlays[1]['c'] is None
    assert after.metadata['extensions']['opaque']==before.metadata['extensions']['opaque']
    assert not after.metadata['extensions']['labels']
    if composite:
        assert after.metadata['properties']['plane_labels']==['0/0/0 | 0/1/0','0/0/1 | 0/1/1','1/0/0 | 1/1/0','1/0/1 | 1/1/1']
        assert after.metadata['extensions']['channel_names']==['RGB']
        old=after.metadata['extensions']['imagejxx.rgb_conversion']['labels']
        assert old['properties']['plane_labels']==before.metadata['properties']['plane_labels']
    if destination==ij.OutputDestination.IN_PLACE:
        assert owner.undo_steps==1;owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)
        assert owner.snapshot().display.properties==before.display.properties
        owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),expected)
    else:
        assert len(app.document_ids)==2 and owner.undo_steps==0
        np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),pixels)


@pytest.mark.parametrize('state_mode,projection,version', [('color',None,1),('grayscale',None,1),('composite','max',2),('composite','min',2)])
def test_composite_uses_captured_mode_and_projection(state_mode,projection,version):
    owner,app,pixels,_,state=fixture();state['mode']=state_mode;state['version']=version
    if projection:state['projection']=projection
    command=ij.resolve_rgb_dataset_command(owner.snapshot(),mode=ij.RgbDatasetMode.COMPOSITE,channels=state)
    state['channels'][1]['range']=[0,1]
    actual=ij.prepare_rgb_dataset(command).preview.dataset.numpy('TCZYXS')
    if state_mode=='grayscale':expected=np.repeat(pixels[:,1:2,...,None],3,axis=-1)
    elif state_mode=='color':
        expected=np.zeros((2,1,2,2,3,3),np.uint8);expected[:,0,...,1]=pixels[:,1]
    elif projection=='min':expected=np.zeros((2,1,2,2,3,3),np.uint8)
    else:expected=expected_rgb(pixels,True)
    np.testing.assert_array_equal(actual,expected)


def test_auto_composite_ranges_are_deferred_and_source_is_unchanged():
    owner,_,pixels,reads,_=fixture(stored=False);before=owner.snapshot();count=reads.counts().plane_reads
    command=ij.resolve_rgb_dataset_command(before,mode=ij.RgbDatasetMode.COMPOSITE)
    assert reads.counts().plane_reads==count and command.record()['request']['channels'] is None
    np.testing.assert_array_equal(ij.prepare_rgb_dataset(command).preview.dataset.numpy('TCZYXS'),expected_rgb(pixels,True))
    assert owner.snapshot().display.properties==before.display.properties and owner.undo_steps==0


@pytest.mark.parametrize('change',['pixels','metadata','selection','display'])
@pytest.mark.parametrize('destination',[ij.OutputDestination.IN_PLACE,ij.OutputDestination.NEW_DOCUMENT])
def test_all_revision_staleness_refuses_publication(change,destination):
    owner,app,_,_,_=fixture();command=ij.resolve_rgb_dataset_command(owner.snapshot(),destination=destination)
    prepared=ij.prepare_rgb_dataset(command)
    if change=='display':
        view=owner.snapshot().display;view.c=0;owner.set_display(view)
    elif change=='selection':owner.select(None)
    else:
        tx=owner.begin_edit()
        if change=='metadata':tx.set_property('changed',True)
        else:tx.replace_plane(np.zeros((2,3),np.uint8),t=0,c=0,z=0)
        owner.commit(tx)
    before=owner.snapshot();steps=owner.undo_steps
    with pytest.raises(RuntimeError):app.publish(prepared)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps and len(app.document_ids)==1


@pytest.mark.parametrize('mutation',range(10))
def test_closed_record_rejects_malformed_claims_before_rebinding(mutation):
    owner,_,_,reads,_=fixture();record=ij.resolve_rgb_dataset_command(owner.snapshot()).record();bad=copy.deepcopy(record)
    if mutation==0:bad['version']=True
    elif mutation==1:bad['extra']=1
    elif mutation==2:bad['source']['shape'][1]=0
    elif mutation==3:bad['layout']['shape'][1]=3
    elif mutation==4:bad['request']['mode']='flatten'
    elif mutation==5:bad['request']['lut']=[[0,0,0]]
    elif mutation==6:bad['source']['revisions']['pixels']=-1
    elif mutation==7:bad['display_revision']=1.0
    elif mutation==8:bad['metadata_bytes']=-1
    else:bad['policy']='discard'
    count=reads.counts().plane_reads
    for action in [lambda:ij.validate_rgb_dataset_command_record(bad),lambda:ij.rgb_dataset_command_from_record(owner.snapshot(),bad),lambda:ij.rebind_rgb_dataset_command(owner.snapshot(),bad)]:
        with pytest.raises((ValueError,TypeError,OverflowError)):action()
    assert reads.counts().plane_reads==count


def test_explicit_rebind_preserves_old_record_and_uses_new_owner():
    owner,_,_,_,_=fixture();record=ij.resolve_rgb_dataset_command(owner.snapshot()).record()
    other=ij.Document(owner.snapshot())
    with pytest.raises(RuntimeError):ij.rgb_dataset_command_from_record(other.snapshot(),record)
    rebound=ij.rebind_rgb_dataset_command(other.snapshot(),record,mode=ij.RgbDatasetMode.COMPOSITE)
    assert rebound.record()['source']['document_id']==other.id and record['source']['document_id']==owner.id


def test_admission_and_execution_caps_refuse_before_pixels():
    owner,_,_,reads,_=fixture();count=reads.counts().plane_reads
    with pytest.raises(ValueError):ij.resolve_rgb_dataset_command(owner.snapshot(),max_working_bytes=1)
    command=ij.resolve_rgb_dataset_command(owner.snapshot())
    with pytest.raises(ValueError):ij.prepare_rgb_dataset(command,execution_max_working_bytes=command.layout.working_bytes-1)
    assert reads.counts().plane_reads==count and owner.undo_steps==0


def test_cancellation_leaves_no_live_mutation_or_new_document():
    owner,app,pixels,_,_=fixture();token=ij.CancellationToken();before=owner.snapshot()
    def progress(done,total):token.cancel()
    with pytest.raises(RuntimeError):ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(before),cancellation=token,progress=progress)
    assert owner.snapshot().shares_data(before) and len(app.document_ids)==1 and owner.undo_steps==0


def test_ready_cancellation_refuses_later_adoption():
    owner,app,_,_,_=fixture();token=ij.CancellationToken()
    plan=ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(owner.snapshot()),cancellation=token)
    token.cancel()
    with pytest.raises(RuntimeError):app.publish(plan)
    assert len(app.document_ids)==1 and owner.undo_steps==0


@pytest.mark.parametrize('samples',[3,4])
def test_rgb_noop_and_rgba_sample_change_preserve_display_annotations(samples):
    source=np.arange(2*2*2*2*3*samples,dtype=np.uint8).reshape(2,2,2,2,3,samples)
    owner=ij.Document(ij.Dataset(source,axes='TCZYXS'))
    owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),ij.PlanarDisplayAnnotation('layer',ij.TextAnnotationPayload(text='kept',bounds=[0,0,1,1]),'xy',0)))
    owner=ij.Document(owner.snapshot());app=ij.Application();app.adopt(owner)
    app.publish(ij.prepare_rgb_dataset(ij.resolve_rgb_dataset_command(owner.snapshot(),destination=ij.OutputDestination.IN_PLACE)))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),source[...,:3])
    assert ij.planar_display_annotations(owner.snapshot())[0].name=='layer' and owner.undo_steps==(samples==4)


def test_custom_per_channel_palette_is_not_replaced_by_named_fallback():
    owner,_,_,_,state=fixture();table=np.zeros((256,3),np.uint8);table[:]=[7,31,127]
    state['channels'][0]['lut']={'table':table.tolist()}
    command=ij.resolve_rgb_dataset_command(owner.snapshot(),channels=state)
    actual=ij.prepare_rgb_dataset(command).preview.dataset.numpy('TCZYXS')
    np.testing.assert_array_equal(actual[:,0],np.broadcast_to([7,31,127],actual[:,0].shape))


def test_record_binds_captured_view_position_even_without_revision_change():
    owner,_,_,reads,_=fixture();source=owner.snapshot()
    view=source.display;view.c=0;alternate=source.with_display(view)
    command=ij.resolve_rgb_dataset_command(alternate,mode=ij.RgbDatasetMode.COMPOSITE)
    count=reads.counts().plane_reads
    assert command.record()['view_position']==[1,0,1]
    with pytest.raises((ValueError,RuntimeError)):
        ij.rgb_dataset_command_from_record(source,command.record())
    assert reads.counts().plane_reads==count
    assert ij.rgb_dataset_command_from_record(alternate,command.record()).record()==command.record()
