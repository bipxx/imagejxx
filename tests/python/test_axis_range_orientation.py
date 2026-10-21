"""Held C++ API assertions; NumPy only constructs literal inputs/expectations."""
import itertools
import numpy as np
import pytest

@pytest.fixture(autouse=True)
def native_api():
    # Collection is source-only; product imports happen only in a real gate.
    global ij
    import imagejxx as ij

AXES={'xy':(0,1,2),'xz':(0,2,1),'yz':(1,2,0)}
FLAGS=tuple(itertools.product((False,True),repeat=3))
PROFILES=((np.uint8,1),(np.uint16,1),(np.float32,1),(np.float64,1),(np.uint8,3),(np.uint8,4))
WRAPPERS=('reslice','snapshot_prepare','application_prepare')

def source(dtype=np.uint16,samples=1):
    t,c,z,y,x,s=np.indices((2,3,4,5,6,samples),dtype=np.int64)
    values=(103*t+37*c+23*z+5*y+3*x+s)%251
    if dtype==np.uint16:values+=40000
    elif dtype in (np.float32,np.float64):values=values-125.25
    pixels=values.astype(dtype)
    calibration=ij.SpatialCalibration(spacing=(.5,1.25,2.),origin=(11.,-7.,19.),
        direction=(-1.,.25,.5,0.,1.,.5,.5,0.,1.),unit='um',
        frame_interval=.75,time_origin=2.25,time_unit='s')
    return pixels,ij.Dataset(pixels,axes='TCZYXS',calibration=calibration)

def raw(value):
    a=np.asarray(value)
    return np.ascontiguousarray(a.astype(a.dtype.newbyteorder('<'))).tobytes()

def literal(pixels,plane,first,last,step,flags,t=1,c=2):
    reverse,flip,transpose=flags
    selected=list(range(first,last+1,step))
    if reverse:selected.reverse()
    volume=pixels[t,c]
    if plane=='xy':rows=[volume[k,:,:,:] for k in selected]
    elif plane=='xz':rows=[volume[:,k,:,:] for k in selected]
    else:rows=[volume[:,:,k,:] for k in selected]
    expected=np.stack(rows)
    if flip:expected=expected[:,::-1,:,:]
    if transpose:expected=expected.transpose(0,2,1,3)
    return np.ascontiguousarray(expected)

def request(plane='xz',first=1,last=4,step=2,flags=(True,True,True),t=1,c=2,**extra):
    return ij.AxisRangeOrientationRequest(ij.AxisRangeResliceRequest(plane,first,last,step,t=t,c=c,**extra),
        reverse_order=flags[0],flip_vertical=flags[1],transpose=flags[2])

def invoke(wrapper,app,owner,req,**controls):
    if wrapper=='reslice':return ij.reslice_oriented_axis_range(owner.snapshot().dataset,req,**controls)
    if wrapper=='snapshot_prepare':return ij.prepare_axis_range_orientation(owner.snapshot(),req,**controls)
    return app.prepare_axis_range_orientation(req,owner.id,**controls)

@pytest.mark.parametrize('plane',AXES)
@pytest.mark.parametrize('flags',FLAGS,ids=lambda v:''.join(str(int(x)) for x in v))
@pytest.mark.parametrize('dtype,samples',PROFILES)
def test_exact144_typed_T_C_mapping_and_signed_affine(plane,flags,dtype,samples):
    pixels,dataset=source(dtype,samples)
    a,b,f=AXES[plane];extent=(6,5,4)[f]
    req=request(plane,1,extent-1,2,flags)
    result=ij.reslice_oriented_axis_range(dataset,req)
    expected=literal(pixels,plane,1,extent-1,2,flags)
    assert isinstance(result,ij.AxisRangeOrientationResult)
    actual=result.dataset.numpy('ZYXS')
    assert actual.dtype==pixels.dtype and actual.shape==expected.shape and raw(actual)==raw(expected)
    assert raw(dataset.numpy('TCZYXS'))==raw(pixels)
    assert tuple(result.dataset.shape)==(1,1,*expected.shape)
    original,out=dataset.calibration,result.dataset.calibration
    reverse,flip,transpose=flags
    axes=(b,a,f) if transpose else (a,b,f)
    signs=(((-1 if flip else 1),1,(-1 if reverse else 1)) if transpose else
           (1,(-1 if flip else 1),(-1 if reverse else 1)))
    assert out.spacing==[original.spacing[axes[0]],original.spacing[axes[1]],original.spacing[f]*2]
    direction=np.asarray(original.direction).reshape(3,3)
    np.testing.assert_array_equal(np.asarray(out.direction).reshape(3,3),direction[:,axes]*signs)
    assert (out.time_origin,out.frame_interval,out.unit,out.time_unit)==(3.,.75,'um','s')
    selected=list(range(1,extent,2));selected=selected[::-1] if reverse else selected
    for k,v,u in itertools.product(range(expected.shape[0]),range(expected.shape[1]),range(expected.shape[2])):
        p,j=(v,u) if transpose else (u,v)
        q=[0,0,0];q[a]=p;q[b]=(6,5,4)[b]-1-j if flip else j;q[f]=selected[k]
        np.testing.assert_allclose(out.voxel_to_world((u,v,k)),original.voxel_to_world(q),rtol=0,atol=1e-12)
    assert result.first_plane_geometry['horizontal_axis']=='XYZ'[axes[0]]
    assert result.first_plane_geometry['vertical_axis']=='XYZ'[axes[1]]
    assert result.first_plane_geometry['origin']==out.origin
    assert result.working_bytes>=expected.size*8 and result.working_bytes<=req.range.max_working_bytes

def test_nested_request_and_result_values_are_owned_copies():
    r=ij.AxisRangeResliceRequest('xy',0,3,2,t=1,c=2)
    req=ij.AxisRangeOrientationRequest(r)
    r.first=-1
    draft=req.range;draft.step=0
    assert req.range.first==0 and req.range.step==2
    req.range=draft
    assert req.range.step==0
    req.range=ij.AxisRangeResliceRequest('xy',0,3,2,t=1,c=2)
    _,dataset=source()
    result=ij.reslice_oriented_axis_range(dataset,req)
    returned=result.request;returned.reverse_order=True
    nested=returned.range;nested.step=-1;returned.range=nested
    assert not result.request.reverse_order and result.request.range.step==2
    geometry=result.first_plane_geometry;geometry['origin'][0]=999
    assert result.first_plane_geometry['origin'][0]!=999
    with pytest.raises(AttributeError):result.working_bytes=0
    default=ij.AxisRangeOrientationRequest()
    assert (default.range.plane,default.range.first,default.range.last,default.range.step)==('xz',0,0,1)
    assert default.reverse_order is default.flip_vertical is default.transpose is False

@pytest.mark.parametrize('plane',AXES)
@pytest.mark.parametrize('flags',FLAGS)
def test_singleton_keeps_signed_stride_and_allfalse_equivalence(plane,flags):
    pixels,dataset=source(np.float64)
    a,b,f=AXES[plane];last=(6,5,4)[f]-1
    req=request(plane,last,last,7,flags)
    result=ij.reslice_oriented_axis_range(dataset,req)
    assert raw(result.dataset.numpy('ZYXS'))==raw(literal(pixels,plane,last,last,7,flags))
    assert result.dataset.calibration.spacing[2]==dataset.calibration.spacing[f]*7
    if flags==(False,False,False):
        old=ij.reslice_axis_range(dataset,req.range)
        assert raw(old.dataset.numpy('ZYXS'))==raw(result.dataset.numpy('ZYXS'))
        assert old.dataset.calibration.spacing==result.dataset.calibration.spacing
        assert old.dataset.calibration.direction==result.dataset.calibration.direction
        assert old.dataset.calibration.origin==result.dataset.calibration.origin

@pytest.mark.parametrize('wrapper',WRAPPERS)
@pytest.mark.parametrize('callback',('progress','checkpoint'))
def test_whole_nested_request_and_source_are_captured_before_callbacks(wrapper,callback):
    pixels,dataset=source()
    owner,app=ij.Document(dataset,'Source'),ij.Application();app.adopt(owner)
    before=owner.snapshot();req=request();calls=[]
    def mutate(*args):
        calls.append(args)
        req.range=ij.AxisRangeResliceRequest('yz',-1,-1,0,t=0,c=0,max_working_bytes=0)
        req.reverse_order=req.flip_vertical=req.transpose=False
        app.activate(owner.id)
    value=invoke(wrapper,app,owner,req,**{callback:mutate})
    assert calls and req.range.step==0
    if wrapper=='reslice':
        out=value.dataset
        assert value.request.range.plane=='xz' and value.request.range.step==2
        assert value.request.reverse_order and value.request.flip_vertical and value.request.transpose
    else:
        candidate=value.prepared_new_document()
        assert candidate.id!=owner.id and not app.contains(candidate.id)
        assert candidate.snapshot().metadata['title']=='Source XZ 2-5 step 2 [R1 F1 T1]'
        assert app.publish(value).document is candidate and not value.active
        out=candidate.snapshot().dataset
    assert raw(out.numpy('ZYXS'))==raw(literal(pixels,'xz',1,4,2,(True,True,True)))
    assert owner.snapshot().shares_data(before) and raw(owner.snapshot().dataset.numpy('TCZYXS'))==raw(pixels)

@pytest.mark.parametrize('wrapper',WRAPPERS)
@pytest.mark.parametrize('callback',('progress','checkpoint'))
def test_callback_type_and_exceptions_reject_atomically(wrapper,callback):
    _,dataset=source();owner,app=ij.Document(dataset),ij.Application();app.adopt(owner)
    before,ids,active=owner.snapshot(),app.document_ids,app.active_document_id
    with pytest.raises(ValueError,match='callbacks must be callable'):
        invoke(wrapper,app,owner,request(),**{callback:[]})
    def fail(*_):raise RuntimeError('orientation callback failure')
    with pytest.raises(RuntimeError,match='orientation callback failure'):
        invoke(wrapper,app,owner,request(),**{callback:fail})
    assert app.document_ids==ids and app.active_document_id==active and owner.snapshot().shares_data(before)

@pytest.mark.parametrize('fields,error',[
    ({'first':-1},IndexError),({'first':3,'last':1},IndexError),({'last':4},IndexError),
    ({'step':0},ValueError),({'step':-1},ValueError),({'t':-1},IndexError),({'t':2},IndexError),
    ({'c':-1},IndexError),({'c':3},IndexError),({'max_working_bytes':0},ValueError),({'max_working_bytes':1},ValueError)])
def test_validation_has_no_progress_or_publication(fields,error):
    pixels,dataset=source();values={'first':0,'last':3,'step':1,'t':1,'c':2};values.update(fields)
    req=ij.AxisRangeOrientationRequest(ij.AxisRangeResliceRequest('xy',**values),reverse_order=True,flip_vertical=True,transpose=True)
    calls=[]
    with pytest.raises(error):ij.reslice_oriented_axis_range(dataset,req,progress=lambda *a:calls.append(a))
    assert calls==[] and raw(dataset.numpy('TCZYXS'))==raw(pixels)

@pytest.mark.parametrize('plane',AXES)
@pytest.mark.parametrize('flags',FLAGS)
def test_quiet_nan_and_signed_zero_bits_are_permuted_without_arithmetic(plane,flags):
    bits=np.array([0,0x80000000,1,0x7FC01234,0xFFC34567,0x3F800000],dtype=np.uint32)
    source_bits=bits[np.arange(120)%6].reshape(4,5,6)
    policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    dataset=ij.Dataset(source_bits.view(np.float32),axes='ZYX',sample_policy=policy)
    extent=(6,5,4)[AXES[plane][2]]
    out=ij.reslice_oriented_axis_range(dataset,request(plane,1,extent-1,2,flags,t=0,c=0)).dataset
    expected=literal(source_bits[None,None,...,None],plane,1,extent-1,2,flags,t=0,c=0)
    assert out.sample_policy==policy and np.array_equal(out.numpy('ZYXS').view(np.uint32),expected)
    assert np.array_equal(dataset.numpy('ZYX').view(np.uint32),source_bits)

@pytest.mark.parametrize('wrapper',WRAPPERS)
@pytest.mark.parametrize('phase',('before','positive_work'))
def test_cancel_preserves_every_source_owner_and_active_membership(wrapper,phase):
    pixels,dataset=source();owner,app=ij.Document(dataset),ij.Application();app.adopt(owner)
    before,ids,active=owner.snapshot(),app.document_ids,app.active_document_id
    token,calls=ij.CancellationToken(),[]
    if phase=='before':token.cancel()
    def cancel(done,total):
        assert 0<=done<=total
        if calls:assert done>=calls[-1][0] and total==calls[-1][1]
        calls.append((done,total))
        if done>0:token.cancel()
    with pytest.raises(ij.OperationCancelled):invoke(wrapper,app,owner,request(),cancellation=token,progress=cancel)
    assert token.canceled and (not calls if phase=='before' else any(done>0 for done,_ in calls))
    assert app.document_ids==ids and app.active_document_id==active and owner.snapshot().shares_data(before)
    assert raw(owner.snapshot().dataset.numpy('TCZYXS'))==raw(pixels)

def test_exact_fresh_candidate_owner_history_and_source_metadata():
    pixels,dataset=source();owner,app=ij.Document(dataset,'Source'),ij.Application();app.adopt(owner)
    edit=owner.begin_edit();edit.set_property('source_only','retained');owner.commit(edit)
    before=owner.snapshot();history=(owner.undo_steps,owner.redo_steps,before.dirty,before.metadata)
    prepared=app.prepare_axis_range_orientation(request(),owner.id)
    candidate=prepared.prepared_new_document();snap=candidate.snapshot()
    assert candidate.id>0 and candidate.id!=owner.id and not app.contains(candidate.id)
    assert snap.data_identity==candidate.undo_steps==candidate.redo_steps==0 and snap.dirty is False
    assert snap.selection is snap.previous_selection is snap.spectrum is None and snap.rois==snap.overlays==[]
    assert snap.record_layout=={} and snap.metadata['properties']=={}
    assert set(snap.metadata['extensions'])=={'axis_range_orientation'}
    published=app.publish(prepared)
    assert published.created and published.document is candidate and published.document_id==candidate.id
    assert app.document_ids==[owner.id,candidate.id] and app.active_document_id==candidate.id and not prepared.active
    assert (owner.undo_steps,owner.redo_steps,owner.snapshot().dirty,owner.snapshot().metadata)==history
    assert owner.snapshot().shares_data(before) and raw(owner.snapshot().dataset.numpy('TCZYXS'))==raw(pixels)
    with pytest.raises(ValueError):app.publish(prepared)

@pytest.mark.parametrize('mutation',('pixels','calibration','closed','candidate_pixels','candidate_metadata','candidate_undo'))
def test_stale_closed_and_candidate_tampering_reject_without_adoption(mutation):
    pixels,dataset=source();owner,app=ij.Document(dataset,'Source'),ij.Application();app.adopt(owner)
    other=ij.Document(dataset,'Other');app.adopt(other);app.activate(other.id)
    prepared=app.prepare_axis_range_orientation(request(),owner.id)
    candidate=prepared.prepared_new_document()
    if mutation=='closed':app.close(owner.id,discard=True)
    elif mutation=='calibration':
        original=owner.snapshot().dataset.calibration
        cal=ij.SpatialCalibration(spacing=(.75,1.25,2.),origin=original.origin,
            direction=original.direction,unit=original.unit,frame_interval=original.frame_interval,
            time_origin=original.time_origin,time_unit=original.time_unit)
        edit=owner.begin_edit();edit.set_calibration(cal);owner.commit(edit)
    elif mutation in ('pixels','candidate_pixels'):
        target=owner if mutation=='pixels' else candidate
        plane=target.snapshot().dataset.plane('xy',0,t=0,c=0);plane=plane.copy();plane[0,0]=7
        edit=target.begin_edit();edit.replace_plane(plane,t=0,c=0,z=0);target.commit(edit)
    else:
        edit=candidate.begin_edit();edit.set_property('tamper','changed');candidate.commit(edit)
        if mutation=='candidate_undo':candidate.undo()
    ids,active=app.document_ids,app.active_document_id
    state=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError,ij.RevisionConflict,ij.DocumentUnavailable)):
        app.publish(prepared)
    assert app.document_ids==ids and app.active_document_id==active==other.id
    assert not app.contains(candidate.id) and owner.snapshot().shares_data(state)
    if mutation not in ('pixels','calibration'):
        assert raw(owner.snapshot().dataset.numpy('TCZYXS'))==raw(pixels)

def test_selection_and_display_only_changes_keep_captured_orientation_publishable():
    pixels,dataset=source();owner,app=ij.Document(dataset,'Source'),ij.Application();app.adopt(owner)
    prepared=app.prepare_axis_range_orientation(request(),owner.id)
    candidate=prepared.prepared_new_document()
    edit=owner.begin_edit();edit.select(ij.Selection('rectangle',((1.,1.),(3.,3.)),1.));owner.commit(edit)
    display=owner.snapshot().display;display.crosshair=[1,1,1];owner.set_display(display)
    current=owner.snapshot()
    assert app.publish(prepared).document is candidate
    assert owner.snapshot().shares_data(current)
    assert candidate.snapshot().selection is None and candidate.snapshot().previous_selection is None
    assert raw(candidate.snapshot().dataset.numpy('ZYXS'))==raw(literal(pixels,'xz',1,4,2,(True,True,True)))

def test_live_facade_dispatch_owns_endpoints_flags_and_source_id():
    from imagejxx.ui.app import LiveDocument
    sentinel=object()
    class Recorder:
        calls=None
        def action(self,name,**args):self.calls=(name,args)
        def live_document(self):return sentinel
    editor=Recorder();live=LiveDocument(editor,71)
    assert live.reslice_oriented_range('yz',1,7,3,reverse_order=True,flip_vertical=True,transpose=True) is sentinel
    assert editor.calls==('reslice_oriented_axis_range',{'plane':'yz','first':1,'last':7,'step':3,
        'reverse_order':True,'flip_vertical':True,'transpose':True,'document_id':71})


@pytest.mark.parametrize('samples',(2,5))
def test_unadmitted_S2_S5_dataset_rejection_preserves_source_and_application(samples):
    pixels,dataset=source(np.uint16)
    owner,app=ij.Document(dataset,'Source'),ij.Application();app.adopt(owner)
    other=ij.Document(dataset,'Other');app.adopt(other)
    edit=owner.begin_edit();edit.set_property('source_only','retained');owner.commit(edit)
    before,other_before=owner.snapshot(),other.snapshot()
    ids,active=app.document_ids,app.active_document_id
    history=(owner.undo_steps,owner.redo_steps,before.dirty,before.data_identity,before.metadata,before.record_layout)
    invalid=np.zeros((2,3,4,5,6,samples),dtype=np.uint16)
    with pytest.raises(ValueError,match='Sample axis S must contain one, three or four'):
        ij.Dataset(invalid,axes='TCZYXS',calibration=dataset.calibration)
    assert app.document_ids==ids and app.active_document_id==active==other.id
    after=owner.snapshot()
    assert after.shares_data(before) and other.snapshot().shares_data(other_before)
    assert (owner.undo_steps,owner.redo_steps,after.dirty,after.data_identity,after.metadata,after.record_layout)==history
    assert raw(after.dataset.numpy('TCZYXS'))==raw(pixels)
    assert raw(other.snapshot().dataset.numpy('TCZYXS'))==raw(pixels)


@pytest.mark.parametrize('channels',(2,5))
@pytest.mark.parametrize('plane',AXES)
@pytest.mark.parametrize('flags',FLAGS,ids=lambda v:''.join(str(int(x)) for x in v))
def test_C2_C5_selected_nonzero_T_C_orientation_preserves_every_owned_boundary(channels,plane,flags):
    t,c,z,y,x,s=np.indices((2,channels,4,5,6,1),dtype=np.int64)
    pixels=(12000*t+1000*c+100*z+10*y+x+s).astype(np.uint16)
    _,calibration_source=source(np.uint16)
    dataset=ij.Dataset(pixels,axes='TCZYXS',calibration=calibration_source.calibration)
    assert dataset.axis_sizes['T']==2 and dataset.axis_sizes['C']==channels and dataset.axis_sizes['S']==1
    selected_c=channels-1
    a,b,f=AXES[plane];extent=(6,5,4)[f]
    req=request(plane,1,extent-1,2,flags,t=1,c=selected_c)
    expected=literal(pixels,plane,1,extent-1,2,flags,t=1,c=selected_c)
    result=ij.reslice_oriented_axis_range(dataset,req)
    assert (result.request.range.t,result.request.range.c)==(1,selected_c)
    assert tuple(result.dataset.shape)==(1,1,*expected.shape)
    assert result.dataset.dtype==np.dtype(np.uint16) and raw(result.dataset.numpy('ZYXS'))==raw(expected)
    reverse,flip,transpose=flags
    axes=(b,a,f) if transpose else (a,b,f)
    signs=((-1 if flip else 1),1,(-1 if reverse else 1)) if transpose else (1,(-1 if flip else 1),(-1 if reverse else 1))
    original,out=dataset.calibration,result.dataset.calibration
    assert out.spacing==[original.spacing[axes[0]],original.spacing[axes[1]],original.spacing[f]*2]
    np.testing.assert_array_equal(np.asarray(out.direction).reshape(3,3),np.asarray(original.direction).reshape(3,3)[:,axes]*signs)
    assert (out.time_origin,out.frame_interval,out.unit,out.time_unit)==(original.time(1),.75,'um','s')
    selected=list(range(1,extent,2));selected=selected[::-1] if reverse else selected
    for k,v,u in itertools.product(range(expected.shape[0]),range(expected.shape[1]),range(expected.shape[2])):
        p,j=(v,u) if transpose else (u,v)
        q=[0,0,0];q[a]=p;q[b]=(6,5,4)[b]-1-j if flip else j;q[f]=selected[k]
        np.testing.assert_allclose(out.voxel_to_world((u,v,k)),original.voxel_to_world(q),rtol=0,atol=1e-12)
    assert result.first_plane_geometry['horizontal_axis']=='XYZ'[axes[0]]
    assert result.first_plane_geometry['vertical_axis']=='XYZ'[axes[1]]
    owner,app=ij.Document(dataset,'Scientific Source'),ij.Application();app.adopt(owner)
    before=owner.snapshot();ids,active=app.document_ids,app.active_document_id
    prepared=app.prepare_axis_range_orientation(req,owner.id)
    candidate=prepared.prepared_new_document();snap=candidate.snapshot()
    assert app.document_ids==ids and app.active_document_id==active and not app.contains(candidate.id)
    assert candidate.id>0 and candidate.id!=owner.id
    assert snap.data_identity==candidate.undo_steps==candidate.redo_steps==0 and snap.dirty is False
    assert snap.selection is snap.previous_selection is snap.spectrum is None and snap.rois==snap.overlays==[]
    assert snap.record_layout=={} and snap.metadata['properties']=={} and set(snap.metadata['extensions'])=={'axis_range_orientation'}
    provenance=snap.metadata['extensions']['axis_range_orientation']
    assert provenance['t']==1 and provenance['c']==selected_c
    assert provenance['reverse_order'] is reverse and provenance['flip_vertical'] is flip and provenance['transpose'] is transpose
    assert raw(snap.dataset.numpy('ZYXS'))==raw(expected)
    assert snap.dataset.calibration.time_origin==original.time(1)
    published=app.publish(prepared)
    assert published.created and published.document is candidate and published.document_id==candidate.id
    assert app.document_ids==ids+[candidate.id] and app.active_document_id==candidate.id and not prepared.active
    assert owner.snapshot().shares_data(before) and owner.snapshot().metadata==before.metadata
    assert raw(owner.snapshot().dataset.numpy('TCZYXS'))==raw(pixels)
    assert raw(dataset.numpy('TCZYXS'))==raw(pixels)
