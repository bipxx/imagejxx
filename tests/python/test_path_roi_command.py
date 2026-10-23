"""Strict V4 saved-row commands preserve complete continuous paths."""
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_path_selection import COMMANDS, selection
from test_region_storage import owner


def fixture(kind='u16',target_kind='path'):
    doc,reads,pixels=owner(kind)
    path=selection();attributes=path.attributes
    if target_kind=='path':target=path
    elif target_kind=='region':
        target=ij.SelectionRegion(ij.Selection.from_mask(np.ones((3,4),np.uint8)),(3,4),[-3,-2]).as_selection().with_attributes(attributes)
    else:target=ij.Selection(target_kind,[(1,1),(4,4)],attributes=attributes)
    edit=doc.begin_edit();edit.select(path);edit.set_previous_selection(path)
    edit.set_rois([ij.ROI('target',target,t=0,c=0,z=0,attributes={'row':[True,2**64-1]}),ij.ROI('other',path,z=1)])
    doc.commit(edit);app=ij.Application();app.adopt(doc)
    return app,doc,reads,pixels,target


def move(value,delta=(.25,-.75)):
    return ij.SelectionEditor(value,(9,9)).moved(delta)


def roi_fields(row):
    return row.name,row.selection,row.t,row.c,row.z,row.attributes


def record(doc,replacement,limits=None):
    request=ij.SavedRoiGeometryRequest(0,replacement)
    return ij.resolve_saved_roi_geometry(doc.snapshot(),request,*([limits] if limits else [])).record()


@pytest.mark.parametrize('kind',['u8','u16','float','quiet','ieee','rgb'])
@pytest.mark.parametrize('transform',['move','rotate','vertex'])
def test_capture_replay_prepare_publish_history_and_v5_session(kind,transform):
    app,doc,reads,pixels,target=fixture(kind);before=doc.snapshot();history=doc.undo_steps
    editor=ij.SelectionEditor(target,(9,9))
    replacement=move(target) if transform=='move' else editor.rotated(90,[0,0]) if transform=='rotate' else editor.moved_vertex(1,[9.25,-3.75])
    request=ij.SavedRoiGeometryRequest(0,replacement)
    estimate=ij.estimate_saved_roi_geometry(before,request)
    command=ij.resolve_saved_roi_geometry(before,request)
    wire=json.loads(json.dumps(command.record(),allow_nan=False))
    assert wire['version']==4 and wire['original']['roi']['selection']['path']==COMMANDS
    assert wire['replacement']['path']==replacement.path.commands
    assert wire['replacement']['points']==[] and command.estimate.working_bytes==estimate.working_bytes
    ij.validate_saved_roi_geometry_record(wire)
    replay=ij.saved_roi_geometry_from_record(before,wire)
    prepared=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),replay)
    assert doc.snapshot().shares_data(before) and prepared.record()==wire
    assert app.publish_saved_roi_geometry(prepared).changed
    after=doc.snapshot();assert doc.undo_steps==history+1
    assert after.rois[0].selection==replacement and after.rois[0].attributes==before.rois[0].attributes
    assert roi_fields(after.rois[1])==roi_fields(before.rois[1]) and after.selection==before.selection and after.previous_selection==before.previous_selection
    assert after.dataset.shares_pixels(before.dataset) and reads.counts().plane_reads==reads.counts().region_reads==0
    blob=ij.encode_session(ij.SessionCapture([after]));assert blob[:10]==b'IMAGEJXX5\n'
    restored=ij.decode_session(blob).documents[0].snapshot()
    assert restored.rois[0].selection==replacement and restored.rois[1].selection.path.commands==COMMANDS
    assert restored.dataset.numpy('TCZYXS').tobytes()==pixels.tobytes()
    doc.undo();assert doc.snapshot().shares_data(before)
    doc.redo();assert doc.snapshot().rois[0].selection==replacement
    with pytest.raises(ij.RevisionConflict):ij.saved_roi_geometry_from_record(doc.snapshot(),wire)


@pytest.mark.parametrize('legacy',['rectangle','ellipse','region'])
@pytest.mark.parametrize('path_original',[False,True])
def test_path_kind_transitions_choose_v4_and_keep_complete_original(legacy,path_original):
    app,doc,reads,_,target=fixture(target_kind='path' if path_original else legacy)
    if path_original:
        replacement=(ij.SelectionRegion(ij.Selection.from_mask(np.ones((3,4),np.uint8)),(3,4),[-8,-6]).as_selection().with_attributes(target.attributes)
                     if legacy=='region' else ij.Selection(legacy,[(-8,-6),(-2,-1)],attributes=target.attributes))
    else:replacement=selection().with_attributes(target.attributes)
    wire=record(doc,replacement);assert wire['version']==4
    rebound=ij.saved_roi_geometry_from_record(doc.snapshot(),wire)
    assert rebound.original_roi.selection==target and rebound.replacement==replacement
    assert reads.counts().plane_reads==0


@pytest.mark.parametrize('version',[1,2,3])
def test_older_versions_remain_closed(version):
    _,doc,_,_,target=fixture();wire=record(doc,move(target));wire['version']=version
    with pytest.raises(ValueError,match='version 4'):ij.validate_saved_roi_geometry_record(wire)


def test_v4_requires_serialized_path_and_does_not_follow_unrelated_carriers():
    _,doc,reads,_,target=fixture(target_kind='rectangle')
    wire=record(doc,move(target));assert wire['version']==1
    ij.validate_saved_roi_geometry_record(wire)
    assert doc.snapshot().selection.is_path and doc.snapshot().rois[1].selection.is_path
    wire['version']=4
    with pytest.raises(ValueError,match='version does not match'):ij.validate_saved_roi_geometry_record(wire)
    assert reads.counts().plane_reads==0


@pytest.mark.parametrize('carrier',['original','replacement'])
@pytest.mark.parametrize('fault',['missing','extra','points','move','opcode','truncated','bool','text','range','empty','attributes'])
def test_malformed_paths_refuse_before_callbacks_or_io(carrier,fault):
    _,doc,reads,_,target=fixture();before=doc.snapshot();wire=record(doc,move(target))
    at=wire['original']['roi']['selection'] if carrier=='original' else wire['replacement']
    if fault=='missing':del at['path']
    elif fault=='extra':at['mask']=''
    elif fault=='points':at['points']=[[1,2]]
    elif fault=='move':at['path'][0]=1
    elif fault=='opcode':at['path'][0]=5
    elif fault=='truncated':at['path']=[0,1,1,3,1,2]
    elif fault=='bool':at['path'][1]=True
    elif fault=='text':at['path'][1]='1'
    elif fault=='range':at['path'][1]=1e9+1
    elif fault=='empty':at['path']=[]
    else:at['attributes']['path']=[0,0,0,4]
    called=[]
    for call in [lambda:ij.validate_saved_roi_geometry_record(wire),
                 lambda:ij.saved_roi_geometry_from_record(before,wire,checkpoint=lambda:called.append(True)),
                 lambda:ij.rebind_saved_roi_geometry(before,wire,ij.SavedRoiGeometryRequest(0,move(target)),checkpoint=lambda:called.append(True))]:
        with pytest.raises((ValueError,TypeError)):call()
    assert not called and doc.snapshot().shares_data(before) and reads.counts().plane_reads==0


def test_control_vertex_limit_and_scalar_codec_budget_are_distinct():
    app,doc,reads,_,target=fixture();commands=[0,0,0,2,2,4,4,0,4]*20
    target=ij.Selection.from_path(ij.ImageJCompositePath(commands)).with_attributes(target.attributes)
    edit=doc.begin_edit();rows=doc.snapshot().rois;rows[0]=ij.ROI('many',target);edit.set_rois(rows);doc.commit(edit)
    request=ij.SavedRoiGeometryRequest(0,move(target))
    limits=ij.SavedRoiGeometryLimits(max_vertices=60)
    command=ij.resolve_saved_roi_geometry(doc.snapshot(),request,limits)
    assert len(command.record()['replacement']['path'])==180
    ij.validate_saved_roi_geometry_record(command.record(),limits)
    assert ij.saved_roi_geometry_from_record(doc.snapshot(),command.record(),limits).replacement==request.replacement
    called=[]
    with pytest.raises(ValueError):ij.resolve_saved_roi_geometry(doc.snapshot(),request,ij.SavedRoiGeometryLimits(max_vertices=59),checkpoint=lambda:called.append(True))
    assert not called and reads.counts().plane_reads==0


def test_full_rebind_validates_original_and_captures_fresh_contour():
    _,doc,reads,_,target=fixture();wire=record(doc,move(target))
    changed=ij.SelectionEditor(target,(9,9)).moved_vertex(1,[7.5,-3.25])
    edit=doc.begin_edit();rows=doc.snapshot().rois;rows[0]=ij.ROI('fresh',changed);edit.set_rois(rows);doc.commit(edit)
    with pytest.raises(ij.RevisionConflict):ij.saved_roi_geometry_from_record(doc.snapshot(),wire)
    replacement=move(changed,(-12,13));new=ij.rebind_saved_roi_geometry(doc.snapshot(),wire,ij.SavedRoiGeometryRequest(0,replacement))
    assert new.original_roi.selection==changed and new.replacement==replacement and new.record()['version']==4
    assert reads.counts().plane_reads==0


def test_identity_and_stale_publication_leave_history_and_geometry_exact():
    app,doc,reads,_,target=fixture();before=doc.snapshot();history=doc.undo_steps
    command=ij.resolve_saved_roi_geometry(before,ij.SavedRoiGeometryRequest(0,target))
    prepared=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command)
    assert not app.publish_saved_roi_geometry(prepared).changed
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history
    command=ij.resolve_saved_roi_geometry(before,ij.SavedRoiGeometryRequest(0,move(target)))
    prepared=app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(),command)
    doc.select(ij.Selection('point',[(1,1)]));new=doc.snapshot()
    with pytest.raises(ij.RevisionConflict):app.publish_saved_roi_geometry(prepared)
    assert doc.snapshot().shares_data(new) and doc.undo_steps==history and reads.counts().plane_reads==0


def test_close_opcodes_do_not_consume_control_vertices_or_break_replay():
    doc,reads,_=owner('u16')
    target=ij.Selection.from_path(ij.ImageJCompositePath([0,0,0]+[4]*16))
    edit=doc.begin_edit();edit.set_rois([ij.ROI('empty contour',target)]);doc.commit(edit)
    limits=ij.SavedRoiGeometryLimits(max_vertices=1)
    request=ij.SavedRoiGeometryRequest(0,move(target))
    command=ij.resolve_saved_roi_geometry(doc.snapshot(),request,limits)
    assert command.estimate.vertices==2 and len(command.record()['replacement']['path'])==19
    ij.validate_saved_roi_geometry_record(command.record(),limits)
    replay=ij.saved_roi_geometry_from_record(doc.snapshot(),command.record(),limits)
    assert replay.replacement==request.replacement and reads.counts().plane_reads==0


def test_generous_control_limit_cannot_overflow_codec_admission():
    _,doc,reads,_,target=fixture()
    limits=ij.SavedRoiGeometryLimits(max_vertices=2**64-1)
    request=ij.SavedRoiGeometryRequest(0,move(target))
    command=ij.resolve_saved_roi_geometry(doc.snapshot(),request,limits)
    ij.validate_saved_roi_geometry_record(command.record(),limits)
    assert ij.saved_roi_geometry_from_record(doc.snapshot(),command.record(),limits).replacement==request.replacement
    assert reads.counts().plane_reads==0
