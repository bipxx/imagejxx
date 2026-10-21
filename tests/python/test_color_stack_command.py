"""Component dataset layout, metadata, guard and pinned legacy-order checks."""
import copy
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

A=ij.ColorStackAction
L=ij.ColorStackLayoutPolicy
D=ij.OutputDestination

def fixture(c=2,t=2,z=3):
    pixels=np.arange(t*c*z*2*3*3,dtype=np.uint8).reshape(t,c,z,2,3,3)
    cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[10,20,30],direction=[1,.25,0,0,0,-1,0,1,0],unit='um',frame_interval=2,time_origin=7)
    observer=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes='TCZYXS',calibration=cal).observe_reads(observer),'component source')
    sel=ij.Selection('rectangle',[(0,0),(1,1)])
    tx=owner.begin_edit();tx.select(sel);tx.set_previous_selection(sel)
    tx.set_rois([ij.ROI('one',sel,t=t-1,c=c-1,z=z-1),ij.ROI('wild',sel),ij.ROI('partial',sel,z=z-1)])
    tx.set_overlays([dict(t=t-1,c=c-1,z=z-1,text='one'),dict(opaque=[True,None])])
    tx.set_property('plane_labels',[f'{i}/{j}/{k}' for i in range(t) for j in range(c) for k in range(z)])
    tx.set_extension('channel_names',[f'channel{i}' for i in range(c)])
    tx.set_property('slice_labels',[f'slice{i}' for i in range(z)])
    tx.set_extension('is_stack',True);tx.set_extension('opaque',{'tables':[{'t':1,'historical':True}]});owner.commit(tx)
    for plane in ['xy','xz','yz']:
        owner.commit(ij.prepare_store_planar_annotation(owner.snapshot(),ij.PlanarAnnotation(plane,sel,plane,0,t=t-1,c=c-1)))
    owner.commit(ij.prepare_store_planar_display_annotation(owner.snapshot(),ij.PlanarDisplayAnnotation('layer',ij.TextAnnotationPayload(text='kept',bounds=[0,0,1,1]),'xz',0,t=t-1,c=c-1)))
    owner=ij.Document(owner.snapshot());view=owner.snapshot().display;view.t=t-1;view.c=c-1;view.crosshair=[1,1,z-1];view.orthogonal=True
    view.properties={'display_range':[1,9],'future':{'kept':True}}
    owner.set_display(view);app=ij.Application();app.adopt(owner)
    return owner,app,pixels,observer

def scalar_cal_equal(a,b):
    for attr in ['spacing','origin','direction','unit','frame_interval','time_origin','time_unit']:
        assert getattr(a,attr)==getattr(b,attr)

@pytest.mark.parametrize('space',['rgb','hsb8','hsb32'])
@pytest.mark.parametrize('destination',[D.IN_PLACE,D.NEW_DOCUMENT])
def test_split_merge_preserves_scientific_axes_and_metadata(space,destination):
    owner,app,pixels,reads=fixture();before=owner.snapshot();count=reads.counts().plane_reads
    command=ij.resolve_color_stack_command(before,space=space,destination=destination)
    assert reads.counts().plane_reads==count
    assert list(command.layout.shape)==[2,6,3,2,3,1]
    assert command.layout.pixel_type==('float32' if space=='hsb32' else 'uint8')
    record=command.record();ij.validate_color_stack_command_record(record)
    assert ij.color_stack_command_from_record(before,record).record()==record
    progress=[]
    ready=ij.prepare_color_stack(command,progress=lambda done,total:progress.append((done,total)))
    assert progress==[(i,12) for i in range(1,13)]
    assert reads.counts().plane_reads-count==12
    expected=np.empty((2,6,3,2,3),np.float32 if space=='hsb32' else np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(3):expected[t,c*3:c*3+3,z]=ij.split_color_components(pixels[t,c,z],space)
    np.testing.assert_array_equal(ready.preview.dataset.numpy('TCZYX'),expected)
    assert owner.undo_steps==0
    after=app.publish(ready).document.snapshot()
    scalar_cal_equal(after.dataset.calibration,before.dataset.calibration)
    assert after.selection.points==before.selection.points
    assert [(r.name,r.c) for r in after.rois]==[('one',3),('one',4),('one',5),('wild',None),('partial',None)]
    assert len(ij.planar_annotations(after))==9 and len(ij.planar_display_annotations(after))==3
    assert after.display.c==3 and after.display.t==1 and after.display.crosshair==[1,1,2] and after.display.orthogonal
    assert after.display.properties=={'future':{'kept':True}}
    np.testing.assert_array_equal(after.dataset.plane('xz',1,t=1,c=4),expected[1,4,:,1,:])
    np.testing.assert_array_equal(after.dataset.plane('yz',2,t=1,c=4),expected[1,4,:,:,2])
    assert after.metadata['properties']['plane_labels'][9]=='0/1/0 / Red' if space=='rgb' else after.metadata['properties']['plane_labels'][9].endswith('Hue')
    assert after.metadata['extensions']['opaque']==before.metadata['extensions']['opaque']
    assert after.metadata['extensions']['imagejxx.color_stack_source']['original_display']['properties']==before.display.properties
    original=after.metadata['extensions']['imagejxx.color_stack_source']['original_native_metadata']
    assert original['metadata']['properties']==before.metadata['properties']
    assert (original['rois'][0]['t'],original['rois'][0]['c'],original['rois'][0]['z'])==(1,1,2)
    progress.clear()
    merged=ij.prepare_color_stack(ij.resolve_color_stack_command(after,action=A.MERGE,space=space),progress=lambda done,total:progress.append((done,total)))
    assert progress==[(i,12) for i in range(1,13)]
    expected_back=np.empty_like(pixels)
    for t in range(2):
        for c in range(2):
            for z in range(3):expected_back[t,c,z]=ij.merge_color_components(expected[t,c*3:c*3+3,z],space)
    np.testing.assert_array_equal(merged.preview.dataset.numpy('TCZYXS'),expected_back)
    scalar_cal_equal(merged.preview.dataset.calibration,before.dataset.calibration)
    if destination==D.IN_PLACE:
        assert owner.undo_steps==1;owner.undo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYXS'),pixels)
        assert owner.snapshot().display.properties==before.display.properties;owner.redo();np.testing.assert_array_equal(owner.snapshot().dataset.numpy('TCZYX'),expected)
    else:assert owner.undo_steps==0 and len(app.document_ids)==2

@pytest.mark.parametrize('space',['hsb8','hsb32'])
def test_legacy_hsb_requires_loss_consent_before_pixels(space):
    owner,app,pixels,reads=fixture();source=owner.snapshot();count=reads.counts().plane_reads
    options=dict(space=space,layout=L.IMAGEJ_LEGACY)
    with pytest.raises(ValueError,match='calibration reset'):ij.resolve_color_stack_command(source,**options)
    with pytest.raises(ValueError,match='non-XY'):ij.resolve_color_stack_command(source,**options,allow_calibration_reset=True)
    assert reads.counts().plane_reads==count
    command=ij.resolve_color_stack_command(source,**options,allow_calibration_reset=True,allow_nonxy_annotation_drop=True)
    result=ij.prepare_color_stack(command).preview
    assert list(result.dataset.shape)==[1,3,12,2,3,1]
    expected=np.empty((1,3,12,2,3),np.float32 if space=='hsb32' else np.uint8)
    for t in range(2):
        for z in range(3):
            for c in range(2):expected[0,:,(t*3+z)*2+c]=ij.split_color_components(pixels[t,c,z],space)
    np.testing.assert_array_equal(result.dataset.numpy('TCZYX'),expected)
    assert command.layout.calibration_reset
    scalar_cal_equal(result.dataset.calibration,ij.SpatialCalibration())
    assert [(a.plane,a.coordinate,a.c) for a in ij.planar_annotations(result)]==[('xy',7,None)]
    assert not ij.planar_display_annotations(result)
    assert [(r.name,r.z) for r in result.rois]==[('one',11),('wild',None),('partial',4),('partial',5),('partial',10),('partial',11)]
    assert result.display.t==result.display.c==0 and result.display.crosshair[2]==11
    original=result.metadata['extensions']['imagejxx.color_stack_source']['original_scalar_metadata']
    assert original and owner.undo_steps==0

@pytest.mark.parametrize('c,t,z',[(1,2,3),(2,2,3),(3,1,2)])
def test_rgb_flatten_is_lazy_c_fast_and_preserves_c1_zt(c,t,z):
    owner,_,pixels,reads=fixture(c,t,z);source=owner.snapshot();count=reads.counts().plane_reads
    options=dict(action=A.FLATTEN_RGB,layout=L.IMAGEJ_LEGACY,allow_calibration_reset=c>1,allow_nonxy_annotation_drop=c>1)
    command=ij.resolve_color_stack_command(source,**options);result=ij.prepare_color_stack(command).preview
    assert reads.counts().plane_reads==count
    expected=pixels if c==1 else pixels.transpose(0,2,1,3,4,5).reshape(1,1,t*c*z,2,3,3)
    np.testing.assert_array_equal(result.dataset.numpy('TCZYXS'),expected)
    scalar_cal_equal(result.dataset.calibration,source.dataset.calibration if c==1 else ij.SpatialCalibration())

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/color-components-imagej154r.json').read_text())
@pytest.mark.parametrize('row',REFERENCE['layout_cases'][18:],ids=lambda r:f"{r['operation']}-{r['source_czt']}")
def test_pinned_imagej_split_layout_and_pixels(row):
    c,z,t=row['source_czt'];source=np.array(row['source'],np.uint8).reshape(t,z,c,1,2,3).transpose(0,2,1,3,4,5)
    owner=ij.Document(ij.Dataset(source,axes='TCZYXS'))
    space={'rgb_components':'rgb','hsb8_components':'hsb8','hsb32_components':'hsb32'}[row['operation']]
    command=ij.resolve_color_stack_command(owner.snapshot(),space=space,layout=L.IMAGEJ_LEGACY,allow_calibration_reset=True)
    result=ij.prepare_color_stack(command).preview.dataset
    got=result.numpy('TCZYX').transpose(0,2,1,3,4).reshape(-1,2)
    np.testing.assert_array_equal(got,np.array(row['output'],dtype=got.dtype))
    assert [result.shape[1],result.shape[2],result.shape[0]]==row['output_czt']

@pytest.mark.parametrize('space,dtype',[('rgb','uint8'),('hsb8','uint8'),('hsb32','float32')])
def test_legacy_merge_z_stack(space,dtype):
    source=np.zeros((3,1,4),dtype=dtype)
    source[0]=np.array([0,1,2,3]) if space!='hsb32' else np.array([0,.2,.4,.6])
    source[1:]=255 if space!='hsb32' else 1
    owner=ij.Document(ij.Dataset(source,axes='ZYX'))
    with pytest.raises(ValueError):ij.resolve_color_stack_command(owner.snapshot(),action=A.MERGE,space=space,layout=L.IMAGEJ_LEGACY)
    command=ij.resolve_color_stack_command(owner.snapshot(),action=A.MERGE,space=space,layout=L.IMAGEJ_LEGACY,allow_calibration_reset=True)
    got=ij.prepare_color_stack(command).preview.dataset.numpy('YXS')
    np.testing.assert_array_equal(got,ij.merge_color_components(source,space))

@pytest.mark.parametrize('change',['pixels','metadata','selection','display'])
@pytest.mark.parametrize('destination',[D.IN_PLACE,D.NEW_DOCUMENT])
def test_all_revision_guards(change,destination):
    owner,app,pixels,_=fixture();command=ij.resolve_color_stack_command(owner.snapshot(),destination=destination);ready=ij.prepare_color_stack(command)
    if change=='display':view=owner.snapshot().display;view.c=0;owner.set_display(view)
    elif change=='selection':owner.select(None)
    else:
        tx=owner.begin_edit()
        if change=='metadata':tx.set_property('new',True)
        else:tx.replace_plane(np.zeros((2,3,3),np.uint8),t=0,c=0,z=0)
        owner.commit(tx)
    state=owner.snapshot();steps=owner.undo_steps
    with pytest.raises(RuntimeError):app.publish(ready)
    assert owner.snapshot().shares_data(state) and owner.undo_steps==steps and len(app.document_ids)==1

@pytest.mark.parametrize('mutation',range(11))
def test_record_closed_and_bound(mutation):
    owner,_,_,reads=fixture();source=owner.snapshot();record=ij.resolve_color_stack_command(source).record();bad=copy.deepcopy(record)
    if mutation==0:bad['extra']=1
    elif mutation==1:bad['version']=True
    elif mutation==2:bad['source']['shape'][1]=0
    elif mutation==3:bad['request']['action']='other'
    elif mutation==4:bad['request']['space']='hsv'
    elif mutation==5:bad['request']['layout']='flat'
    elif mutation==6:bad['request']['allow_calibration_reset']=1
    elif mutation==7:bad['view_position'][0]=True
    elif mutation==8:bad['layout']['shape'][1]=7
    elif mutation==9:bad['metadata_working_bytes']=-1
    else:bad['policy']='discard'
    count=reads.counts().plane_reads
    for work in [lambda:ij.validate_color_stack_command_record(bad),lambda:ij.color_stack_command_from_record(source,bad),lambda:ij.rebind_color_stack_command(source,bad)]:
        with pytest.raises((ValueError,TypeError,OverflowError)):work()
    assert reads.counts().plane_reads==count

def test_cap_cancel_rebind_and_callback_snapshot():
    owner,app,_,reads=fixture();source=owner.snapshot();command=ij.resolve_color_stack_command(source);count=reads.counts().plane_reads
    with pytest.raises(ValueError):ij.resolve_color_stack_command(source,max_working_bytes=1)
    visits=[]
    with pytest.raises(ValueError):ij.prepare_color_stack(command,execution_max_working_bytes=command.layout.working_bytes-1,checkpoint=lambda:visits.append(1))
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.prepare_color_stack(command,cancellation=token)
    assert not visits and reads.counts().plane_reads==count
    token=ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):ij.prepare_color_stack(command,cancellation=token,progress=lambda done,total:token.cancel())
    assert owner.undo_steps==0
    other=ij.Document(source)
    with pytest.raises(RuntimeError):ij.color_stack_command_from_record(other.snapshot(),command.record())
    assert ij.rebind_color_stack_command(other.snapshot(),command.record()).record()['source']['document_id']==other.id

def test_legacy_c1_t1_spanning_nonxy_is_retained_once():
    owner,_,pixels,reads=fixture(c=1,t=1,z=3)
    ready=ij.prepare_color_stack(ij.resolve_color_stack_command(owner.snapshot(),space='hsb32',layout=L.IMAGEJ_LEGACY))
    result=ready.preview
    assert len(ij.planar_annotations(result))==3 and len(ij.planar_display_annotations(result))==1
    assert [(r.name,r.z,r.c) for r in result.rois]==[('one',2,None),('wild',None,None),('partial',2,None)]
    assert reads.counts().plane_reads==3
    scalar_cal_equal(result.dataset.calibration,owner.snapshot().dataset.calibration)

@pytest.mark.parametrize('channels',[2,6])
def test_rgb_merge_channel_groups_and_inversion(channels):
    source=np.arange(channels*2*2*3,dtype=np.uint8).reshape(1,channels,2,2,3)
    owner=ij.Document(ij.Dataset(source,axes='TCZYX'))
    view=owner.snapshot().display;view.properties={'manual_threshold':dict(version=1,lower=10,upper=200,t=0,c=1,z=1)};owner.set_display(view)
    command=ij.resolve_color_stack_command(owner.snapshot(),action=A.MERGE,invert_rgb=True)
    ready=ij.prepare_color_stack(command)
    assert 'manual_threshold' not in ready.preview.display.properties
    got=ready.preview.dataset.numpy('TCZYXS')
    group=2 if channels==2 else 3
    expected=np.empty((1,channels//group,2,2,3,3),np.uint8)
    for c in range(channels//group):
        for z in range(2):expected[0,c,z]=ij.merge_color_components(source[0,c*group:c*group+group,z],invert_rgb=True)
    np.testing.assert_array_equal(got,expected)

def test_annotation_growth_refuses_before_pixels():
    owner,_,_,reads=fixture();selection=owner.snapshot().selection
    tx=owner.begin_edit();tx.set_rois([ij.ROI(str(i),selection,c=1) for i in range(1400)]);owner.commit(tx)
    count=reads.counts().plane_reads
    with pytest.raises(ValueError,match='row limit'):ij.resolve_color_stack_command(owner.snapshot())
    assert reads.counts().plane_reads==count

@pytest.mark.parametrize('cap',[True,np.bool_(False),1.5])
def test_strict_cap_types(cap):
    owner,_,_,_=fixture()
    with pytest.raises(TypeError):ij.resolve_color_stack_command(owner.snapshot(),max_working_bytes=cap)

@pytest.mark.parametrize('space',['rgb','hsb8','hsb32'])
def test_component_dataset_session_roundtrip(space):
    owner,_,_,_=fixture();result=ij.prepare_color_stack(ij.resolve_color_stack_command(owner.snapshot(),space=space)).preview
    decoded=ij.decode_session(ij.encode_session(ij.SessionCapture([result]),ij.SessionLimits()),ij.SessionLimits())
    restored=decoded.documents[0].snapshot()
    np.testing.assert_array_equal(restored.dataset.numpy('TCZYX'),result.dataset.numpy('TCZYX'))
    scalar_cal_equal(restored.dataset.calibration,result.dataset.calibration)
    assert restored.metadata['extensions']['imagejxx.color_stack_source']==result.metadata['extensions']['imagejxx.color_stack_source']
    assert [(a.plane,a.c) for a in ij.planar_annotations(restored)]==[(a.plane,a.c) for a in ij.planar_annotations(result)]

def test_ready_cancel_and_callback_owner_changes_are_guarded():
    owner,app,_,_=fixture();source=owner.snapshot();command=ij.resolve_color_stack_command(source)
    token=ij.CancellationToken();ready=ij.prepare_color_stack(command,cancellation=token);token.cancel()
    with pytest.raises(ij.OperationCancelled):app.publish(ready)
    visits=[]
    def checkpoint():
        if not visits:owner.select(None)
        visits.append(1)
    ready=ij.prepare_color_stack(command,checkpoint=checkpoint)
    assert visits and ready.preview.selection.points==source.selection.points
    with pytest.raises(RuntimeError):app.publish(ready)
    assert len(app.document_ids)==1
