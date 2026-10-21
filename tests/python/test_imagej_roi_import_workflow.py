"""Closed retained-input equations and original-first replay/rebinding."""
import copy
import json
import pytest
import imagejxx as ij
from test_imagej_roi_import import owner,bare,envelope,archive,EXTENT,SOURCES

@pytest.mark.parametrize('mode',[ij.ImageJRoiImportMode.APPEND,ij.ImageJRoiImportMode.REPLACE])
def test_record_equation_source_archive_and_display_compatible_replay(mode):
    doc,reads=owner()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[bare('prior.roi')])))
    before=doc.snapshot();items=[bare(),envelope()]
    command=ij.resolve_imagej_roi_import(before,items,mode)
    record=json.loads(json.dumps(command.record()))
    assert record['before_sources']==archive(doc) and record['mode']==('append' if mode==ij.ImageJRoiImportMode.APPEND else 'replace')
    assert len(record['after_sources']['entries'])==(3 if mode==ij.ImageJRoiImportMode.APPEND else 2)
    ij.validate_imagej_roi_import_record(record)
    view=before.display;view.t=1;doc.set_display(view)
    replay=ij.imagej_roi_import_from_record(doc.snapshot(),record)
    assert replay.provenance()==command.provenance()
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    history=doc.undo_steps;doc.commit(ij.prepare_imagej_roi_import(replay))
    assert doc.undo_steps==history+1 and doc.snapshot().display.t==1
    assert archive(doc)==record['after_sources']
    with pytest.raises(ij.RevisionConflict):ij.imagej_roi_import_from_record(doc.snapshot(),record)
    doc.undo();assert archive(doc)==record['before_sources']

@pytest.mark.parametrize('path,value',[
    (('version',),True),(('version',),2),(('mode',),'guess'),(('destination',),'new_document'),
    (('source','shape',2),True),(('source','shape',5),3),
    (('limits','max_entries'),True),(('limits','max_total_bytes'),1),
    (('limits','max_decoded_bytes'),0),(('limits','roi_max_vertices'),1),
    (('after','rows',0,'name'),'forged'),(('after','rows'),[]),
    (('after_sources','entries',0,'mapping'),None),
    (('after_sources','entries',1,'data'),'AA=='),
])
def test_original_equation_is_validated_before_explicit_rebind(path,value):
    doc,reads=owner();target,_=owner()
    command=ij.resolve_imagej_roi_import(doc.snapshot(),[bare(),envelope()])
    bad=command.record();field=bad
    for key in path[:-1]:field=field[key]
    field[path[-1]]=value
    for action in (lambda:ij.validate_imagej_roi_import_record(bad),
                   lambda:ij.imagej_roi_import_from_record(doc.snapshot(),bad),
                   lambda:ij.rebind_imagej_roi_import(target.snapshot(),bad,[bare('explicit.roi')])):
        with pytest.raises((ValueError,IndexError,OverflowError)):action()
    assert doc.undo_steps==target.undo_steps==0 and reads.counts().plane_reads==0

def test_append_prefix_cannot_be_replaced_while_claiming_original_sources():
    doc,_=owner()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[bare('prior.roi')])))
    command=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()])
    bad=command.record()
    bad['after_sources']['entries'][0]['name']='different prior label'
    with pytest.raises(ValueError,match='equation differs'):ij.validate_imagej_roi_import_record(bad)
    bad=command.record();bad['before_sources']['entries'][0]['data']='AA=='
    with pytest.raises(ValueError):ij.rebind_imagej_roi_import(doc.snapshot(),bad,[bare('explicit.roi')])

def test_explicit_new_owner_rebind_and_stale_publication():
    doc,reads=owner();other,other_reads=owner()
    command=ij.resolve_imagej_roi_import(doc.snapshot(),[bare(),envelope()])
    record=command.record()
    with pytest.raises(ValueError,match='another source'):ij.imagej_roi_import_from_record(other.snapshot(),record)
    rebound=ij.rebind_imagej_roi_import(other.snapshot(),record,[bare('explicit.roi','xz',7)])
    assert rebound.record()['source']['document_id']!=record['source']['document_id']
    other.commit(ij.prepare_imagej_roi_import(rebound))
    assert ij.planar_annotations(other.snapshot())[0].plane=='xz'
    stale=ij.prepare_imagej_roi_import(ij.imagej_roi_import_from_record(doc.snapshot(),record))
    edit=doc.begin_edit();edit.set_title('concurrent');doc.commit(edit)
    with pytest.raises(ij.RevisionConflict):doc.commit(stale)
    assert archive(doc) is None and not ij.planar_annotations(doc.snapshot())
    assert reads.counts().plane_reads==other_reads.counts().plane_reads==0

def test_workflow_roundtrip_and_original_validation_without_live_document():
    doc,reads=owner();items=[bare(),envelope()]
    command=ij.resolve_imagej_roi_import(doc.snapshot(),items)
    record=command.record();ij.validate_imagej_roi_import_record(record)
    assert reads.counts().plane_reads==0
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([doc],workflow=[record])))
    assert restored.workflow[0]==record
    ij.validate_imagej_roi_import_record(restored.workflow[0])
    rebound=ij.rebind_imagej_roi_import(restored.documents[0].snapshot(),restored.workflow[0],items)
    restored.documents[0].commit(ij.prepare_imagej_roi_import(rebound))
    assert len(archive(restored.documents[0])['entries'])==2

def test_copies_unknown_fields_and_bounded_nested_record():
    doc,_=owner();command=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()])
    bad=command.record();bad['after']['rows'][0]['name']='mutated copy'
    assert command.record()['after']['rows'][0]['name']!='mutated copy'
    bad=command.record();bad['unexpected']=0
    with pytest.raises(ValueError):ij.validate_imagej_roi_import_record(bad)
    nested=0
    for _ in range(82):nested=[nested]
    bad=command.record();bad['unexpected']=nested
    with pytest.raises(ValueError,match='depth/node'):ij.validate_imagej_roi_import_record(bad)

def test_zero_optional_codec_budgets_and_exact_limit_roundtrip():
    doc,_=owner();limits=ij.ImageJRoiImportLimits(roi=ij.ImageJRoiLimits(max_mask_pixels=0,max_path_work=0,max_image_pixels=0))
    command=ij.resolve_imagej_roi_import(doc.snapshot(),[bare()],limits=limits)
    record=command.record()
    assert record['limits']['roi_max_mask_pixels']==record['limits']['roi_max_path_work']==0
    ij.validate_imagej_roi_import_record(record)
    assert ij.imagej_roi_import_from_record(doc.snapshot(),record).record()==record


def test_validated_request_exposes_only_new_inputs_and_independent_copies():
    doc,reads=owner()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[bare('prior.roi')])))
    items=[bare('new.roi'),envelope()]
    limits=ij.ImageJRoiImportLimits(roi=ij.ImageJRoiLimits(max_mask_pixels=0,max_path_work=0,max_image_pixels=0))
    command=ij.resolve_imagej_roi_import(doc.snapshot(),items,limits=limits)
    record=command.record();request=ij.decode_imagej_roi_import_request(record)
    assert request.source_extent==[2,3,6,8,12,1] and request.mode==ij.ImageJRoiImportMode.APPEND
    assert [i.name for i in request.items]==['new.roi','native.roi']
    assert [i.data for i in request.items]==[i.data for i in items]
    assert request.items[0].mapping.extent==request.source_extent and request.items[1].mapping is None
    assert request.limits.roi.max_mask_pixels==request.limits.roi.max_path_work==0
    copy=request.items;copy[0].mapping.coordinate=5;copy.clear()
    assert len(request.items)==2 and request.items[0].mapping.coordinate==0
    record['after']['rows'][0]['name']='forged prefix'
    with pytest.raises(ValueError):ij.decode_imagej_roi_import_request(record)
    assert command.record()['after']['rows'][0]['name']!='forged prefix'
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_replace_request_retains_whole_replacement():
    doc,_=owner()
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[bare('prior.roi')])))
    record=ij.resolve_imagej_roi_import(doc.snapshot(),[bare('replacement.roi')],ij.ImageJRoiImportMode.REPLACE).record()
    request=ij.decode_imagej_roi_import_request(record)
    assert request.mode==ij.ImageJRoiImportMode.REPLACE
    assert [i.name for i in request.items]==['replacement.roi']
