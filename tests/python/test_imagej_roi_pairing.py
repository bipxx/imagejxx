"""Explicit snapshot-bound source/current-row choices, never ordinal inference."""
import base64
import copy
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_roi_import import owner,bare,envelope,mapping,archive,EXTENT,ROWS,SOURCES


def fixture():
    doc,reads=owner()
    for item in [bare('duplicate.roi'),envelope('duplicate.roi')]:
        doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    row=ij.PlanarAnnotation('chosen λ😀',ij.Selection('point',[(7.250000000001,.500000000001)],
        attributes={'future-selection':[None,2**64-1]}),'xz',4,t=1,c=2,attributes={'future-row':'opaque'})
    doc.commit(ij.prepare_update_planar_annotation(doc.snapshot(),1,row))
    return doc,reads,row


def test_catalog_full_history_duplicate_names_and_selected_source_only():
    doc,reads,row=fixture();before=doc.snapshot();history=doc.undo_steps
    catalog=ij.inspect_imagej_roi_sources(before)
    assert catalog.source.id==before.id and catalog.source.shares_data(before)
    assert [(s.index,s.name,s.has_mapping,s.has_native_replacement) for s in catalog.entries]==[
        (0,'duplicate.roi',True,False),(1,'duplicate.roi',False,False)]
    assert [s.data_bytes for s in catalog.entries]==[len(base64.b64decode(e['data'])) for e in archive(doc)['entries']]
    pair=ij.resolve_imagej_roi_pairing(catalog,0,1)
    assert (pair.source_index,pair.annotation_index)==(0,1)
    assert pair.source_item.data==bare('duplicate.roi').data
    assert pair.mapped.source_record.group==7 and pair.mapped.source_record.properties=='source: λ😀\n'
    assert pair.mapped.source_mapping.extent==EXTENT and pair.mapped.target_extent==EXTENT
    assert pair.mapped.annotation.name==row.name and pair.mapped.annotation.plane=='xz'
    assert pair.mapped.annotation.selection.points==row.selection.points
    assert pair.mapped.annotation.selection.attributes==row.selection.attributes
    assert pair.mapped.annotation.attributes==row.attributes
    assert (pair.mapped.annotation.coordinate,pair.mapped.annotation.t,pair.mapped.annotation.c)==(4,1,2)
    ij.validate_imagej_roi_pairing_record(pair.record())
    assert ij.imagej_roi_pairing_from_record(before,pair.record()).record()==pair.record()
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_export_requires_explicit_matching_projection_and_preserves_original():
    doc,reads,row=fixture();before=doc.snapshot();history=doc.undo_steps
    pair=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(before),0,1)
    projection=ij.ImageJRoi(ij.ROI(row.name,row.selection.with_attributes({}),t=row.t,c=row.c,z=row.coordinate))
    projection.group=9;projection.properties='export: explicit\n'
    wire=ij.encode_imagej_roi_pairing_envelope(before,pair,projection)
    decoded=ij.decode_imagej_roi_native_envelope(wire)
    assert decoded.annotation.selection.points==row.selection.points and decoded.annotation.attributes==row.attributes
    assert decoded.source_record.group==7 and decoded.source_record.properties=='source: λ😀\n'
    outer=ij.decode_imagej_roi(wire)
    assert outer.group==9
    payload=base64.b64decode(outer.properties.split(': ',1)[1]);offset=4
    def blob():
        nonlocal offset
        n=int.from_bytes(payload[offset:offset+4],'big');offset+=4
        value=payload[offset:offset+n];offset+=n;return value
    blob();offset+=24;blob();binding=blob()
    assert offset==len(payload) and ij.decode_imagej_roi(binding).properties=='export: explicit\n'
    assert pair.source_item.data==bare('duplicate.roi').data
    projection.roi=ij.ROI('wrong',row.selection,t=row.t,c=row.c,z=row.coordinate)
    with pytest.raises(ValueError):ij.encode_imagej_roi_pairing_envelope(before,pair,projection)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('change',['row-edit','row-delete','row-reorder','archive-reorder','archive-replacement','title','pixels'])
def test_captured_pair_never_silently_moves_after_document_changes(change):
    doc,reads,row=fixture();before=doc.snapshot()
    pair=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(before),0,1)
    record=pair.record();tx=doc.begin_edit()
    if change=='row-edit':
        doc.commit(ij.prepare_update_planar_annotation(before,1,ij.PlanarAnnotation('changed',row.selection,'xz',4)))
    elif change=='row-delete':doc.commit(ij.prepare_delete_planar_annotation(before,0))
    elif change=='row-reorder':
        current=before.metadata['extensions'][ROWS];current['rows'].reverse();tx.set_extension(ROWS,current);doc.commit(tx)
    elif change.startswith('archive'):
        sources=archive(doc)
        if change=='archive-reorder':sources['entries'].reverse()
        else:sources['entries'][0]['data']=sources['entries'][1]['data'];sources['entries'][0]['mapping']=None
        tx.set_extension(SOURCES,sources);doc.commit(tx)
    elif change=='title':tx.set_title('changed');doc.commit(tx)
    else:tx.replace_plane(np.ones((8,12),dtype=np.uint16));doc.commit(tx)
    observed=reads.counts()
    with pytest.raises((ValueError,RuntimeError)):ij.imagej_roi_pairing_from_record(doc.snapshot(),record)
    # The old immutable snapshot remains a valid input; it does not query live data.
    assert ij.imagej_roi_pairing_from_record(before,record).mapped.annotation.name==row.name
    assert reads.counts().plane_reads==observed.plane_reads and reads.counts().region_reads==observed.region_reads


def test_display_only_changes_allow_replay_and_new_owner_requires_explicit_rebind():
    doc,reads,row=fixture();pair=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(doc.snapshot()),0,1)
    view=doc.snapshot().display;view.t=1;doc.set_display(view)
    assert ij.imagej_roi_pairing_from_record(doc.snapshot(),pair.record()).record()==pair.record()
    other=ij.Document(doc.snapshot())
    with pytest.raises(ValueError,match='another source'):ij.imagej_roi_pairing_from_record(other.snapshot(),pair.record())
    rebound=ij.rebind_imagej_roi_pairing(other.snapshot(),pair.record(),1,0)
    assert rebound.source.id==other.snapshot().id
    assert rebound.source_item.data==envelope('duplicate.roi').data
    assert rebound.mapped.annotation.name=='duplicate.roi'
    assert rebound.record()['source']['document_id']!=pair.record()['source']['document_id']
    assert reads.counts().plane_reads==reads.counts().region_reads==0


@pytest.mark.parametrize('path,value',[
    (('version',),2),(('extra',),None),(('source_index',),True),(('source_index',),-1),
    (('source_index',),2),(('annotation_index',),2),(('sources','entries',1,'data'),'AAAA'),
    (('sources','entries',0,'mapping','extent'),[1,1,1,1,1,1]),
    (('limits','max_decoded_bytes'),1),(('annotations','rows',1,'selection','points'),[[float('inf'),0]]),
])
def test_closed_records_original_admission_and_target_refusal(path,value):
    doc,reads,row=fixture();record=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(doc.snapshot()),0,1).record()
    target=record
    for key in path[:-1]:target=target[key]
    target[path[-1]]=value
    with pytest.raises((ValueError,TypeError)):ij.validate_imagej_roi_pairing_record(record)
    with pytest.raises((ValueError,TypeError)):ij.rebind_imagej_roi_pairing(doc.snapshot(),record,0,0)
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_late_corrupt_history_refuses_catalog_and_pair_before_bad_index():
    doc,reads,row=fixture();tx=doc.begin_edit();sources=archive(doc)
    sources['entries'][-1]['data']='AAAA';tx.set_extension(SOURCES,sources);doc.commit(tx)
    with pytest.raises(ValueError):ij.inspect_imagej_roi_sources(doc.snapshot())
    # A record also validates the entire archive before applying an index.
    good,_,_=fixture();record=ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(good.snapshot()),0,0).record()
    record['sources']=sources;record['source_index']=999
    with pytest.raises(ValueError) as error:ij.validate_imagej_roi_pairing_record(record)
    assert 'index out of range' not in str(error.value)


@pytest.mark.parametrize('index',[True,1.0,'0',-1,2**100])
def test_python_indices_are_strict_nonnegative_integers(index):
    doc,_,_=fixture();catalog=ij.inspect_imagej_roi_sources(doc.snapshot())
    for a,b in [(index,0),(0,index)]:
        with pytest.raises((TypeError,ValueError,OverflowError)):ij.resolve_imagej_roi_pairing(catalog,a,b)


def test_empty_catalog_and_retention_limits():
    doc,reads=owner();assert ij.inspect_imagej_roi_sources(doc.snapshot()).entries==[]
    with pytest.raises(ValueError,match='source index'):ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(doc.snapshot()),0,0)
    doc,_,_=fixture()
    with pytest.raises(ValueError):ij.inspect_imagej_roi_sources(doc.snapshot(),ij.ImageJRoiImportLimits(max_decoded_bytes=1))
    catalog=ij.inspect_imagej_roi_sources(doc.snapshot(),ij.ImageJRoiImportLimits(max_decoded_bytes=300))
    with pytest.raises(ValueError):ij.resolve_imagej_roi_pairing(catalog,0,1)
    with pytest.raises(ValueError):ij.inspect_imagej_roi_sources(doc.snapshot(),ij.ImageJRoiImportLimits(max_total_bytes=1))


def test_v2_stored_replacement_is_retained_separately_from_chosen_current_row():
    doc,reads=owner();item=envelope();source=ij.decode_imagej_roi_envelope(item.data)
    spec=ij.ImageJRoiNativeReplacement(EXTENT,source.annotation)
    item=ij.ImageJRoiImportItem(item.name,item.data,native_replacement=spec)
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    current=ij.PlanarAnnotation('now',ij.Selection('point',[(1.000000000001,2.000000000001)]),'xy',1)
    doc.commit(ij.prepare_update_planar_annotation(doc.snapshot(),0,current))
    catalog=ij.inspect_imagej_roi_sources(doc.snapshot());assert catalog.entries[0].has_native_replacement
    pair=ij.resolve_imagej_roi_pairing(catalog,0,0)
    assert pair.source_item.native_replacement.annotation.name==source.annotation.name
    assert pair.mapped.annotation.name=='now' and pair.mapped.annotation.selection.points==current.selection.points
    assert pair.source_item.data==item.data
    assert reads.counts().plane_reads==reads.counts().region_reads==0


def test_large_history_does_not_retain_unselected_replacement_suffix():
    doc,reads=owner();first=bare('first.roi')
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[first])))
    source=envelope();row=ij.PlanarAnnotation('historical',ij.Selection('point',[(1,1)]),'xy',0,
                                            attributes={'old-payload':'x'*65536})
    spec=ij.ImageJRoiNativeReplacement(EXTENT,row)
    for i in range(10):
        item=ij.ImageJRoiImportItem(f'old-{i}.roi',source.data,native_replacement=spec)
        doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    tx=doc.begin_edit();native=doc.snapshot().metadata['extensions'][ROWS]
    native['rows']=native['rows'][:1];tx.set_extension(ROWS,native);doc.commit(tx)
    limits=ij.ImageJRoiImportLimits(max_decoded_bytes=16384)
    catalog=ij.inspect_imagej_roi_sources(doc.snapshot(),limits)
    assert len(catalog.entries)==11
    pair=ij.resolve_imagej_roi_pairing(catalog,0,0)
    assert pair.source_item.data==first.data
    ij.validate_imagej_roi_pairing_record(pair.record())
    with pytest.raises(ValueError,match='byte limit'):ij.resolve_imagej_roi_pairing(catalog,10,0)
    assert reads.counts().plane_reads==reads.counts().region_reads==0
