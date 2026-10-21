"""Saved-area metadata editing preserves admitted IEEE Float32 source bytes."""
from copy import deepcopy
import numpy as np
import pytest
import imagejxx as ij

POLICIES={'finite_only':ij.FloatingSamplePolicy.FINITE_ONLY,'preserve_quiet_nan_float32':ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,'preserve_ieee_float32':ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32}
KINDS=['rectangle','ellipse','rotated_rectangle','rotated_ellipse','rounded_rectangle','rotated_rounded_rectangle','polygon','freehand','mask']


def geometry(kind):
    attributes={'opaque':{'unsigned':2**64-1,'label':'kept'}}
    if kind=='mask':
        mask=np.zeros((10,12),np.uint8);mask[2:8,2:10]=1
        return ij.Selection.from_mask(mask).with_attributes(attributes)
    if kind in ['polygon','freehand']:return ij.Selection(kind,[(2,2),(9,2),(8,7),(3,8)],attributes=attributes)
    base=kind.removeprefix('rotated_')
    if base=='rounded_rectangle':attributes['corner_diameter']=6
    value=ij.Selection(base,[(2,2),(9,7)],attributes=attributes)
    return ij.SelectionEditor(value,(10,12)).rotated(23) if kind.startswith('rotated_') else value


def setup(kind,policy='preserve_ieee_float32',axes='YX'):
    words=[0,0x80000000,0x3f800000,0xbf800000,1,0x80000001,0x7f7fffff,0xff7fffff]
    if policy!='finite_only':words += [0x7fc12345,0xffc54321]
    if policy=='preserve_ieee_float32':words += [0x7f800000,0xff800000]
    shape={'YX':(10,12),'ZYX':(3,10,12),'TCZYX':(2,2,3,10,12)}[axes]
    bits=np.resize(np.array(words,np.uint32),shape)
    reads=ij.DatasetReadObserver();dataset=ij.Dataset(bits.view(np.float32),axes=axes,sample_policy=POLICIES[policy]).observe_reads(reads)
    seed=ij.Document(dataset);edit=seed.begin_edit()
    edit.select(ij.Selection('point',[(0,0)]));edit.set_previous_selection(ij.Selection('line',[(0,0),(1,1)]))
    edit.set_rois([ij.ROI('area',geometry(kind),attributes={'row':'kept'}),ij.ROI('other',ij.Selection('rectangle',[(0,0),(1,1)]))])
    edit.set_extension('future',{'kept':True});seed.commit(edit)
    owner=ij.Document(seed.snapshot());app=ij.Application();app.adopt(owner)
    original=owner.snapshot().rois[0].selection
    changed=ij.SelectionEditor(original,(10,12)).moved((1,-1),keep_inside=False)
    return app,owner,reads,bits,ij.SavedRoiGeometryRequest(0,changed)


def counts(reads):
    c=reads.counts();return c.plane_reads,c.region_reads


@pytest.mark.parametrize('kind',KINDS)
@pytest.mark.parametrize('policy',list(POLICIES))
@pytest.mark.parametrize('axes',['YX','ZYX','TCZYX'])
def test_policy_records_payload_history_and_session(kind,policy,axes):
    app,owner,reads,bits,request=setup(kind,policy,axes);before=owner.snapshot()
    np.testing.assert_array_equal(before.dataset.numpy(axes).view(np.uint32),bits);warm=counts(reads)
    command=ij.resolve_saved_roi_geometry(before,request);record=command.record()
    assert record['source']['sample_policy']==policy and record['version']==(2 if 'rounded' in kind else 1)
    ij.validate_saved_roi_geometry_record(record)
    assert ij.saved_roi_geometry_from_record(before,record).replacement==request.replacement
    prepared=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command)
    assert app.publish_saved_roi_geometry(prepared).changed and not prepared.active and prepared.retained_working_bytes==0
    assert counts(reads)==warm and owner.snapshot().dataset.shares_pixels(before.dataset)
    assert owner.snapshot().dataset.sample_policy==POLICIES[policy]
    assert owner.snapshot().selection==before.selection and owner.snapshot().previous_selection==before.previous_selection
    assert owner.snapshot().rois[0].selection==request.replacement and owner.snapshot().rois[0].attributes==before.rois[0].attributes
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy(axes).view(np.uint32),bits)
    warm=counts(reads);owner.undo();assert owner.snapshot().rois[0].selection==before.rois[0].selection
    owner.redo();assert owner.snapshot().rois[0].selection==request.replacement
    assert counts(reads)==warm and owner.undo_steps==1 and owner.redo_steps==0
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([owner],workflow=[record])))
    reopened=restored.documents[0];assert reopened.snapshot().dataset.sample_policy==POLICIES[policy]
    np.testing.assert_array_equal(reopened.snapshot().dataset.numpy(axes).view(np.uint32),bits)
    assert restored.workflow==[record];ij.validate_saved_roi_geometry_record(restored.workflow[0])
    with pytest.raises(ij.RevisionConflict):ij.saved_roi_geometry_from_record(reopened.snapshot(),record)
    new=ij.rebind_saved_roi_geometry(reopened.snapshot(),record,ij.SavedRoiGeometryRequest(0,before.rois[0].selection))
    assert new.record()['source']['sample_policy']==policy
    second=ij.Application();second.adopt(reopened)
    proposal=second.prepare_saved_roi_geometry(second.capture_saved_roi_geometry(),new);second.publish_saved_roi_geometry(proposal)
    assert reopened.snapshot().rois[0].selection==before.rois[0].selection
    np.testing.assert_array_equal(reopened.snapshot().dataset.numpy(axes).view(np.uint32),bits)


@pytest.mark.parametrize('kind',KINDS)
@pytest.mark.parametrize('boundary',['pixels','vertices','working','record','depth'])
def test_ieee_preflight_and_callback_boundaries_are_read_free(kind,boundary):
    app,owner,reads,bits,request=setup(kind);before=owner.snapshot();estimate=ij.estimate_saved_roi_geometry(before,request)
    kwargs={'pixels':{'max_xy_pixels':119},'vertices':{'max_vertices':1},'working':{'max_working_bytes':estimate.working_bytes-1},'record':{'max_record_bytes':estimate.record_wire_bytes-1},'depth':{'metadata_depth':1}}[boundary]
    # Explicit masks have no vertices: use a zero record cap discriminator for
    # that case, keeping the API's positive-limit rule intact.
    if kind=='mask' and boundary=='vertices':kwargs={'max_record_bytes':1}
    calls=[];warm=counts(reads)
    with pytest.raises(ValueError):
        ij.resolve_saved_roi_geometry(before,request,ij.SavedRoiGeometryLimits(**kwargs),checkpoint=lambda:calls.append(1),stage_progress=lambda*a:calls.append(1))
    assert not calls and counts(reads)==warm and owner.snapshot().shares_data(before)
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.resolve_saved_roi_geometry(before,request,cancellation=token,checkpoint=lambda:calls.append(1))
    assert not calls and counts(reads)==warm


@pytest.mark.parametrize('kind',KINDS)
@pytest.mark.parametrize('damage',['type','components','unknown_policy','policy_type'])
def test_ieee_invalid_original_precedes_rebind_inputs(kind,damage):
    app,owner,reads,bits,request=setup(kind);before=owner.snapshot();record=deepcopy(ij.resolve_saved_roi_geometry(before,request).record())
    if damage=='type':record['source']['type']='uint16'
    elif damage=='components':record['source']['shape'][5]=3
    elif damage=='unknown_policy':record['source']['sample_policy']='unknown_ieee_policy'
    else:record['source']['sample_policy']=True
    warm=counts(reads);calls=[]
    with pytest.raises(ValueError):ij.rebind_saved_roi_geometry(None,record,None,checkpoint=lambda:calls.append(1))
    assert not calls and counts(reads)==warm and owner.snapshot().shares_data(before)


@pytest.mark.parametrize('kind',KINDS)
def test_ieee_strict_replay_never_downgrades_source_policy(kind):
    app,owner,reads,bits,request=setup(kind);before=owner.snapshot();record=ij.resolve_saved_roi_geometry(before,request).record()
    record['source']['sample_policy']='finite_only';warm=counts(reads)
    with pytest.raises(ij.RevisionConflict):ij.saved_roi_geometry_from_record(before,record)
    assert counts(reads)==warm and owner.snapshot().shares_data(before)


@pytest.mark.parametrize('kind',KINDS)
def test_finite_original_explicitly_rebinds_to_ieee_destination(kind):
    old,owner,reads,bits,request=setup(kind,'finite_only')
    record=ij.resolve_saved_roi_geometry(owner.snapshot(),request).record()
    app,destination,observed,expected,new_request=setup(kind)
    warm=counts(observed);command=ij.rebind_saved_roi_geometry(destination.snapshot(),record,new_request)
    assert command.record()['source']['sample_policy']=='preserve_ieee_float32' and record['source']['sample_policy']=='finite_only'
    prepared=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command);app.publish_saved_roi_geometry(prepared)
    assert counts(observed)==warm
    np.testing.assert_array_equal(destination.snapshot().dataset.numpy('YX').view(np.uint32),expected)
