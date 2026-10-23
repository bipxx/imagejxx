"""Saved-row regional transport and publication, with explicit canvas-independent frames."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij


def fixture(kind='u16', target_region=True):
    dtype = np.uint8 if kind in ('u8','rgb') else np.float32 if kind in ('quiet','ieee') else np.uint16
    pixels = np.arange(162, dtype=dtype).reshape(2,9,9,1)
    policy = ij.FloatingSamplePolicy.FINITE_ONLY
    if kind == 'rgb': pixels = np.repeat(pixels,3,axis=-1)
    if kind in ('quiet','ieee'):
        words = pixels.view(np.uint32)
        words.flat[0] = 0x7fc12345;words.flat[1] = 0xffc54321
        policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
        if kind == 'ieee':
            words.flat[2] = 0x7f800000;words.flat[3] = 0x80000000
            policy = ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    reads = ij.DatasetReadObserver()
    document = ij.Document(ij.Dataset(pixels,axes='ZYXS',sample_policy=policy).observe_reads(reads))
    seed = np.zeros((9,9),np.uint8);seed[0,0] = 255
    region = ij.SelectionRegion(ij.Selection.from_mask(seed),(9,9)).enlarged(2).as_selection()
    region = region.with_attributes({'origin':'opaque','attributes':{'flags':[True,2**64-1]}})
    target = region if target_region else ij.Selection('rectangle',[(1,1),(3,3)],attributes=region.attributes)
    edit = document.begin_edit();edit.set_rois([ij.ROI('target',target,attributes={'retained':[None,True]}),
        ij.ROI('unrelated',region,t=0,c=0,z=1)]);document.commit(edit)
    document.select(region);document.select(ij.Selection('point',[(4,4)]))
    app = ij.Application();app.adopt(document)
    return app,document,reads,pixels.copy(),target,region


def moved(region,delta=(-5,3)):
    return ij.SelectionEditor(region,(9,9)).moved(delta)


@pytest.mark.parametrize('kind',['u8','u16','rgb','quiet','ieee'])
def test_v3_capture_replay_prepare_publish_undo_and_v4_reopen(kind):
    app,doc,reads,pixels,region,_ = fixture(kind)
    replacement = moved(region);before = doc.snapshot();history = doc.undo_steps
    req = ij.SavedRoiGeometryRequest(0,replacement)
    cost = ij.estimate_saved_roi_geometry(before,req)
    command = ij.resolve_saved_roi_geometry(before,req)
    wire = json.loads(json.dumps(command.record(),allow_nan=False))
    assert wire['version'] == 3 and command.estimate.working_bytes == cost.working_bytes
    assert wire['original']['roi']['selection'] == region.to_dict()
    assert wire['replacement'] == replacement.to_dict()
    assert wire['replacement']['origin'] == [-8,0]
    ij.validate_saved_roi_geometry_record(wire)
    replay = ij.saved_roi_geometry_from_record(before,wire)
    prepared = app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),replay)
    assert doc.snapshot().shares_data(before) and prepared.record() == wire
    result = app.publish_saved_roi_geometry(prepared)
    after = doc.snapshot()
    assert result.changed and doc.undo_steps == history+1
    assert after.rois[0].selection == replacement and after.rois[0].attributes == before.rois[0].attributes
    assert after.rois[1].selection == before.rois[1].selection
    assert after.selection == before.selection and after.previous_selection == before.previous_selection
    assert after.dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    session = ij.encode_session(ij.SessionCapture([after]));assert session[:10] == b'IMAGEJXX4\n'
    restored = ij.decode_session(session).documents[0].snapshot()
    assert restored.rois[0].selection == replacement and restored.previous_selection == region
    assert restored.dataset.numpy('ZYXS').tobytes() == pixels.tobytes()
    doc.undo();assert doc.snapshot().shares_data(before)
    doc.redo();assert doc.snapshot().rois[0].selection == replacement


def test_unserialized_regional_carriers_do_not_upgrade_legacy_record():
    app,doc,reads,_,target,_ = fixture(target_region=False)
    command = ij.resolve_saved_roi_geometry(doc.snapshot(),ij.SavedRoiGeometryRequest(0,moved(target)))
    assert command.record()['version'] == 1
    ij.validate_saved_roi_geometry_record(command.record())
    prepared = app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command)
    app.publish_saved_roi_geometry(prepared)
    assert doc.snapshot().rois[1].selection.is_region and doc.snapshot().previous_selection.is_region
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('regional_original',[False,True])
def test_geometry_kind_transitions_preserve_full_frame_and_require_v3(regional_original):
    _,doc,reads,_,target,region = fixture(target_region=regional_original)
    replacement = ij.Selection('rectangle',[(-8,-6),(-2,-1)],attributes=region.attributes) if regional_original else moved(region)
    command = ij.resolve_saved_roi_geometry(doc.snapshot(),ij.SavedRoiGeometryRequest(0,replacement))
    record = command.record();assert record['version'] == 3
    assert ij.saved_roi_geometry_from_record(doc.snapshot(),record).replacement == replacement
    for version in [1,2]:
        bad = copy.deepcopy(record);bad['version'] = version
        with pytest.raises(ValueError): ij.validate_saved_roi_geometry_record(bad)
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('carrier',['original','replacement'])
@pytest.mark.parametrize('field,value',[('origin',[False,-3]),('origin',[1.,-3]),('origin',[2**31,-3]),
    ('origin',[-2**31-1,-3]),('shape',[0,7]),('shape',[8,8]),('points',[[0,0]]),('extra',1),('mask','/w==')])
def test_regional_wire_is_closed_before_replay_rebind_or_callbacks(carrier,field,value):
    app,doc,reads,_,region,_ = fixture()
    record = ij.resolve_saved_roi_geometry(doc.snapshot(),ij.SavedRoiGeometryRequest(0,moved(region))).record()
    at = record['original']['roi']['selection'] if carrier == 'original' else record['replacement']
    at[field] = value
    called = [];before = doc.snapshot()
    calls = [lambda:ij.validate_saved_roi_geometry_record(record),
        lambda:ij.saved_roi_geometry_from_record(before,record,checkpoint=lambda:called.append(True)),
        lambda:ij.rebind_saved_roi_geometry(before,record,ij.SavedRoiGeometryRequest(0,region),checkpoint=lambda:called.append(True))]
    for call in calls:
        with pytest.raises((ValueError,TypeError)): call()
    assert called == [] and reads.counts().plane_reads == 0 and doc.snapshot().shares_data(before)


@pytest.mark.parametrize('depth,accepted',[(122,True),(123,True),(124,False)])
@pytest.mark.parametrize('strings',[False,True])
def test_complete_record_depth_is_admitted_before_callbacks(depth,accepted,strings):
    app,doc,reads,_,region,_ = fixture()
    nested = ['leaf'] if strings else 0
    for _ in range(depth-int(strings)): nested = [nested]
    region = region.with_attributes({'deep':nested})
    edit = doc.begin_edit();edit.set_rois([ij.ROI('deep',region)]);doc.commit(edit)
    called = [];req = ij.SavedRoiGeometryRequest(0,moved(region))
    if accepted:
        record = ij.resolve_saved_roi_geometry(doc.snapshot(),req,checkpoint=lambda:called.append(True)).record()
        ij.validate_saved_roi_geometry_record(json.loads(json.dumps(record)))
        assert called
    else:
        with pytest.raises(ValueError): ij.resolve_saved_roi_geometry(doc.snapshot(),req,checkpoint=lambda:called.append(True))
        assert called == []
    assert reads.counts().plane_reads == 0


@pytest.mark.parametrize('limit',['max_xy_pixels','max_record_bytes','max_working_bytes','metadata_depth'])
def test_caps_refuse_before_io_callbacks_and_publication(limit):
    app,doc,reads,_,region,_ = fixture();before = doc.snapshot();called = []
    limits = ij.SavedRoiGeometryLimits(**{limit:1})
    with pytest.raises(ValueError):
        ij.resolve_saved_roi_geometry(before,ij.SavedRoiGeometryRequest(0,moved(region)),limits,checkpoint=lambda:called.append(True))
    assert reads.counts().plane_reads == 0 and called == [] and doc.snapshot().shares_data(before)


def test_intrinsic_region_extent_is_admitted_independently_of_document_canvas():
    _,doc,reads,_,_,_ = fixture()
    region = ij.SelectionRegion(ij.Selection.from_mask(np.ones((12,12),np.uint8)),(12,12),(-10,-10)).as_selection()
    edit = doc.begin_edit();edit.set_rois([ij.ROI('large',region)]);doc.commit(edit)
    req = ij.SavedRoiGeometryRequest(0,moved(region))
    called = []
    with pytest.raises(ValueError): ij.resolve_saved_roi_geometry(doc.snapshot(),req,
        ij.SavedRoiGeometryLimits(max_xy_pixels=81),checkpoint=lambda:called.append(True))
    assert called == [] and reads.counts().plane_reads == 0
    assert ij.resolve_saved_roi_geometry(doc.snapshot(),req).replacement.mask_origin == [-15,-7]


def test_origin_only_source_change_invalidates_strict_record_and_explicit_rebind_is_fresh():
    _,doc,reads,_,region,_ = fixture()
    request = ij.SavedRoiGeometryRequest(0,moved(region))
    record = ij.resolve_saved_roi_geometry(doc.snapshot(),request).record()
    edit = doc.begin_edit();edit.set_rois([ij.ROI('target',moved(region,(1,0)),attributes={'retained':[None,True]}),doc.snapshot().rois[1]]);doc.commit(edit)
    with pytest.raises(ij.RevisionConflict): ij.saved_roi_geometry_from_record(doc.snapshot(),record)
    command = ij.rebind_saved_roi_geometry(doc.snapshot(),record,request)
    assert command.original_roi.selection.mask_origin == [-2,-3] and command.replacement == request.replacement
    assert reads.counts().plane_reads == 0


def test_exact_regional_identity_does_not_add_history():
    app,doc,reads,_,region,_ = fixture();before = doc.snapshot();history = doc.undo_steps
    command = ij.resolve_saved_roi_geometry(before,ij.SavedRoiGeometryRequest(0,region))
    prepared = app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command)
    assert not app.publish_saved_roi_geometry(prepared).changed
    assert doc.snapshot().shares_data(before) and doc.undo_steps == history and reads.counts().plane_reads == 0


def test_actual_imgui_saved_row_numeric_move_preserves_global_selection_and_record(tmp_path):
    from imagejxx.ui.app import Editor
    from test_imgui_roi_style import click
    from test_selection_rotation import typing
    _,doc,reads,_,region,_ = fixture();before = doc.snapshot();history = doc.undo_steps
    editor = Editor(preferences=tmp_path/'preferences.json');editor._editor.add_virtual_document(doc)
    editor.recording = True;editor.action('edit_saved_roi_geometry',index=0)
    frame = editor.render_headless(frames=3)
    (tmp_path/'regional-saved-row-draft.json').write_text(json.dumps(frame))
    controls = frame['selection_properties']['control_states']
    assert not controls['Apply']['disabled']
    # Saved drafts intentionally cancel when a renderer is destroyed. Keep the
    # begin/draft/input in one renderer lifetime; reuse only measured layout.
    frame = editor.render_headless(events=[
        {'type':'action','name':'edit_saved_roi_geometry','parameters':{'index':0}},
        {'type':'wait','frames':3},
        *typing(frame['selection_properties']['controls']['Move X'],'-5'),
        *typing(frame['selection_properties']['controls']['Move Y'],'3'),
        *click(frame['selection_properties']['apply_point'])])
    (tmp_path/'regional-saved-row-applied.json').write_text(json.dumps(frame))
    assert doc.snapshot().rois[0].selection == moved(region) and doc.undo_steps == history+1
    assert doc.snapshot().selection == before.selection and doc.snapshot().previous_selection == before.previous_selection
    assert editor.workflow[-1]['version'] == 3 and editor.workflow[-1]['replacement']['origin'] == [-8,0]
    doc.undo();assert doc.snapshot().shares_data(before)
