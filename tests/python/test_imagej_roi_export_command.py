"""Closed, path-free pairing/projection/output equations with guarded replay."""
import base64
import copy
import json
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_roi_pairing import fixture


def resolved(limits=None):
    doc,reads,row=fixture();pair=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(doc.snapshot()),0,1)
    projection=ij.ImageJRoi(ij.ROI(row.name,row.selection.with_attributes({}),t=row.t,c=row.c,z=row.coordinate))
    projection.group=9;projection.properties='review: λ😀\n'
    command=ij.resolve_imagej_roi_export(doc.snapshot(),pair,projection,limits or ij.ImageJRoiExportLimits())
    return doc,reads,pair,projection,command


def test_closed_equation_exact_projection_output_and_pure_replay():
    doc,reads,pair,projection,command=resolved();before=doc.snapshot();record=command.record()
    assert set(record)=={'schema','version','pairing','limits','projection','output'}
    assert record['schema']=='imagejxx.imagej-roi-export' and record['version']==1
    assert record['pairing']==pair.record() and command.pairing.record()==pair.record()
    assert base64.b64decode(record['output'])==command.data
    assert base64.b64decode(record['projection'])==ij.encode_imagej_roi(projection)
    decoded=ij.decode_imagej_roi_native_envelope(command.data)
    assert decoded.annotation.selection.points==[[7.250000000001,.500000000001]]
    assert decoded.annotation.attributes=={'future-row':'opaque'} and decoded.annotation.selection.attributes=={'future-selection':[None,2**64-1]}
    assert decoded.source_record.group==7 and ij.decode_imagej_roi(command.data).group==9
    ij.validate_imagej_roi_export_record(record)
    replay=ij.imagej_roi_export_from_record(before,record)
    assert replay.record()==record and replay.data==command.data
    request=ij.decode_imagej_roi_export_request(record)
    assert request.pairing_record==pair.record() and ij.encode_imagej_roi(request.standard_projection)==base64.b64decode(record['projection'])
    assert doc.snapshot().shares_data(before) and doc.undo_steps==3
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('path,value',[
    (('schema',),'wrong'),(('version',),True),(('version',),2),(('destination',),'C:/must-not-be-used.roi'),
    (('overwrite',),True),(('projection',),'AAAA'),(('output',),'AAAA'),
    (('pairing','sources','entries',1,'data'),'AAAA'),(('pairing','annotation_index'),999),
    (('limits','max_record_bytes'),1),(('limits','max_bytes'),63),(('limits','max_bytes'),2**64-1),
    (('limits','max_vertices'),False),(('limits','extra'),1),
])
def test_closed_record_original_and_output_tampering_refuse(path,value):
    doc,reads,pair,projection,command=resolved();record=command.record();target=record
    for key in path[:-1]:target=target[key]
    target[path[-1]]=value
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.validate_imagej_roi_export_record(record)
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.decode_imagej_roi_export_request(record)
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.rebind_imagej_roi_export(doc.snapshot(),record,pair,projection)
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('field',['projection','output'])
def test_wire_mutation_and_noncanonical_base64(field):
    doc,_,_,_,command=resolved()
    record=command.record();wire=bytearray(base64.b64decode(record[field]));wire[0]=0
    record[field]=base64.b64encode(wire).decode()
    with pytest.raises(ValueError):ij.validate_imagej_roi_export_record(record)
    record=command.record();record[field]+='\n'
    with pytest.raises(ValueError):ij.validate_imagej_roi_export_record(record)


def test_original_pair_admission_precedes_malformed_projection_and_new_owner():
    doc,_,pair,projection,command=resolved();record=command.record()
    record['pairing']['sources']['entries'][1]['data']='AAAA';record['projection']='not base64'
    with pytest.raises(ValueError) as error:ij.rebind_imagej_roi_export(doc.snapshot(),record,pair,projection)
    assert 'base64' not in str(error.value).lower()


def test_stale_owner_display_only_replay_and_explicit_new_owner_rebind():
    doc,reads,pair,projection,command=resolved();before=doc.snapshot();record=command.record()
    view=before.display;view.t=1;doc.set_display(view)
    assert ij.imagej_roi_export_from_record(doc.snapshot(),record).data==command.data
    other=ij.Document(before)
    with pytest.raises(ValueError,match='another source'):ij.imagej_roi_export_from_record(other.snapshot(),record)
    new_pair=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(other.snapshot()),0,1)
    rebound=ij.rebind_imagej_roi_export(other.snapshot(),record,new_pair,projection)
    assert rebound.data==command.data and rebound.record()['pairing']['source']['document_id']==other.snapshot().id
    tx=doc.begin_edit();tx.set_title('changed');doc.commit(tx)
    with pytest.raises(ij.RevisionConflict):ij.imagej_roi_export_from_record(doc.snapshot(),record)
    with pytest.raises(ij.RevisionConflict):ij.resolve_imagej_roi_export(doc.snapshot(),pair,projection)
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_projection_mismatch_and_record_budget():
    doc,reads,pair,projection,command=resolved()
    projection.roi=ij.ROI('wrong',projection.roi.selection,t=1,c=2,z=4)
    with pytest.raises(ValueError):ij.resolve_imagej_roi_export(doc.snapshot(),pair,projection)
    with pytest.raises(ValueError):resolved(ij.ImageJRoiExportLimits(max_record_bytes=1))
    with pytest.raises(ValueError):resolved(ij.ImageJRoiExportLimits(roi=ij.ImageJRoiLimits(max_bytes=64)))
    def storage(value):
        if isinstance(value,dict):return 32+sum(len(k.encode('utf-8'))+storage(v) for k,v in value.items())
        if isinstance(value,list):return 32+sum(map(storage,value))
        if isinstance(value,str):return 32+len(value.encode('utf-8'))
        return 32
    record=command.record();bound=sum(len(k.encode('utf-8'))+storage(v) for k,v in record.items())
    assert resolved(ij.ImageJRoiExportLimits(max_record_bytes=bound))[-1].limits.max_record_bytes==bound
    with pytest.raises(ValueError):resolved(ij.ImageJRoiExportLimits(max_record_bytes=bound-1))


@pytest.mark.parametrize('value',[True,0,-1,1.0,2**100,128*1024*1024+1])
def test_record_limit_constructor_types_and_bounds(value):
    with pytest.raises((TypeError,ValueError,OverflowError)):ij.ImageJRoiExportLimits(max_record_bytes=value)


def test_request_copies_and_json_transport_do_not_mutate_original():
    doc,_,_,_,command=resolved();record=command.record();request=ij.decode_imagej_roi_export_request(record)
    changed=request.pairing_record;changed['source_index']=999
    projection=request.standard_projection;projection.group=42
    assert request.pairing_record==record['pairing'] and request.standard_projection.group==9
    restored=json.loads(json.dumps(record,ensure_ascii=False))
    assert ij.imagej_roi_export_from_record(doc.snapshot(),restored).data==command.data


def test_generic_workflow_opens_native_export_review(tmp_path):
    from imagejxx import _desktop
    doc,reads,_,_,command=resolved();before=doc.snapshot()
    view=_desktop.Editor(str(tmp_path/'preferences.json'));view.add_native_document(doc)
    view.workflow=json.dumps([command.record()]);initial=view.workflow
    view.action('run_workflow','{}')
    assert view.busy and doc.snapshot().shares_data(before)
    view.action('cancel_native_workflow','{}')
    assert view.workflow==initial and not view.busy and doc.snapshot().shares_data(before)
    assert not list(tmp_path.glob('*.roi'))
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_session_workflow_transport_and_explicit_restored_owner_rebind():
    doc,_,_,_,command=resolved();record=command.record()
    session=ij.decode_session(ij.encode_session(ij.SessionCapture([doc],workflow=[record])))
    assert session.workflow==[record]
    restored=session.documents[0];ij.validate_imagej_roi_export_record(session.workflow[0])
    with pytest.raises(ValueError,match='another source'):ij.imagej_roi_export_from_record(restored.snapshot(),record)
    pair=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(restored.snapshot()),0,1)
    request=ij.decode_imagej_roi_export_request(record)
    rebound=ij.rebind_imagej_roi_export(restored.snapshot(),record,pair,request.standard_projection,request.limits)
    assert rebound.data==command.data and rebound.record()['pairing']['source']['document_id']==restored.snapshot().id
