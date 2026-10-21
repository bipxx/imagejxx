"""Heterogeneous originals, atomic row publication and closed replay equations."""
import base64
import copy
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_display_roi import (EXTENT, mapping, text_record, image_record,
    edited_text, edited_image, projection)
from test_imagej_roi_import import bare, envelope, owner, SOURCES

DISPLAY='imagejxx.planar_display_annotations'

def display(kind='text',wrapped=False,name=None):
    if wrapped:
        mapped,_=edited_text() if kind=='text' else edited_image()
        options=ij.ImageJDisplayProjectionOptions(round_font_size=True) if kind=='text' else ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True,round_image_origin=True)
        data=ij.encode_imagej_display_roi_envelope(mapped,projection(mapped),options)
        return ij.ImageJDisplayRoiImportItem(name or kind+'.roi',data)
    record=text_record() if kind=='text' else image_record()
    data=(ij.encode_imagej_text_roi if kind=='text' else ij.encode_imagej_image_roi)(record)
    return ij.ImageJDisplayRoiImportItem(name or kind+'.roi',data,mapping('xz'))

def batch():return [bare(),display(),envelope(),display('image',True)]

def publish(doc,items=None,mode=ij.ImageJRoiImportMode.APPEND):
    resolved=ij.resolve_imagej_annotation_import(doc.snapshot(),items or batch(),mode)
    doc.commit(ij.prepare_imagej_annotation_import(resolved));return resolved

@pytest.mark.parametrize('mode',[ij.ImageJRoiImportMode.APPEND,ij.ImageJRoiImportMode.REPLACE])
def test_all_kinds_atomic_publication_originals_session_and_one_undo(mode):
    doc,reads=owner();before=doc.snapshot();items=batch()
    resolved=ij.resolve_imagej_annotation_import(before,items,mode)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0
    assert [type(r) for r in resolved.records]==[ij.MappedImageJRoi,ij.MappedImageJDisplayRoi,ij.MappedImageJRoi,ij.MappedImageJDisplayRoi]
    assert len(resolved.annotations)==len(resolved.display_annotations)==2
    saved=resolved.provenance();assert saved['version']==3
    assert [e['kind'] for e in saved['entries']]==['selection','display','selection','display']
    assert [base64.b64decode(e['source']['data']) for e in saved['entries']]==[i.data for i in items]
    assert all(set(e)=={'kind','source'} and set(e['source'])=={'name','data','mapping','native_replacement'} for e in saved['entries'])
    doc.commit(ij.prepare_imagej_annotation_import(resolved))
    assert len(ij.planar_annotations(doc.snapshot()))==len(ij.planar_display_annotations(doc.snapshot()))==2
    assert doc.undo_steps==1 and doc.snapshot().selection is None
    assert doc.snapshot().dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads==reads.counts().region_reads==0
    got=ij.inspect_imagej_annotation_sources(doc.snapshot());assert [i.data for i in got]==[i.data for i in items]
    assert got[-1].data!=resolved.records[-1].source_bytes
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([doc]))).documents[0]
    assert [i.data for i in ij.inspect_imagej_annotation_sources(restored.snapshot())]==[i.data for i in items]
    assert restored.snapshot().metadata['extensions'][SOURCES]==saved
    doc.undo();assert doc.snapshot().shares_data(before)
    doc.redo();assert doc.snapshot().metadata['extensions'][SOURCES]==saved

@pytest.mark.parametrize('legacy_version',[1,2])
def test_legacy_archive_migration_keeps_exact_sources_and_replacement(legacy_version):
    doc,_=owner();item=bare()
    if legacy_version==2:
        original=envelope();mapped=ij.decode_imagej_roi_envelope(original.data)
        row=ij.PlanarAnnotation('legacy replacement',mapped.annotation.selection,'xz',1,t=1,c=None)
        item=ij.ImageJRoiImportItem('legacy.roi',original.data,native_replacement=ij.ImageJRoiNativeReplacement(EXTENT,row))
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    old=doc.snapshot().metadata['extensions'][SOURCES];assert old['version']==legacy_version
    before=doc.snapshot();r=publish(doc,[display()]);saved=r.provenance()
    expected=copy.deepcopy(old['entries'][0]);expected.setdefault('native_replacement',None)
    assert saved['entries'][0]=={'kind':'selection','source':expected}
    assert ij.inspect_imagej_annotation_sources(doc.snapshot())[0].data==item.data
    assert len(ij.planar_annotations(doc.snapshot()))==1
    if legacy_version==2:assert ij.planar_annotations(doc.snapshot())[0].name=='legacy replacement'
    doc.undo();assert doc.snapshot().shares_data(before) and doc.snapshot().metadata['extensions'][SOURCES]==old

@pytest.mark.parametrize('only',['text','image','selection'])
def test_replace_clears_both_sets_append_preserves_independent_order(only):
    doc,_=owner();publish(doc);before=doc.snapshot()
    items=[bare('only.roi')] if only=='selection' else [display(only,name='only.roi')]
    appended=ij.resolve_imagej_annotation_import(before,items)
    assert len(appended.annotations)==2+(only=='selection')
    assert len(appended.display_annotations)==2+(only!='selection')
    publish(doc,items,ij.ImageJRoiImportMode.REPLACE)
    ext=doc.snapshot().metadata['extensions']
    assert len(ij.planar_annotations(doc.snapshot()))==(only=='selection')
    assert len(ij.planar_display_annotations(doc.snapshot()))==(only!='selection')
    assert (DISPLAY in ext)==(only!='selection')
    assert len(ij.inspect_imagej_annotation_sources(doc.snapshot()))==1
    doc.undo();assert doc.snapshot().shares_data(before)

@pytest.mark.parametrize('case',['invalid-last','duplicate','missing-mapping','wrong-extent','wrong-kind','empty','wrong-color'])
def test_bad_batch_never_publishes_any_prefix(case):
    doc,reads=owner();before=doc.snapshot();items=batch()
    if case=='invalid-last':items[-1]=ij.ImageJDisplayRoiImportItem('bad.roi',b'bad')
    elif case=='duplicate':items[-1]=ij.ImageJDisplayRoiImportItem(items[0].name,items[-1].data)
    elif case=='missing-mapping':items[1]=ij.ImageJDisplayRoiImportItem('text.roi',items[1].data)
    elif case=='wrong-extent':
        m=mapping().plane;m.extent=[2,3,6,8,12,3]
        items[1]=ij.ImageJDisplayRoiImportItem('text.roi',items[1].data,ij.ImageJDisplayRoiMapping(m))
    elif case=='wrong-kind':items[1]=ij.ImageJRoiImportItem('text.roi',items[1].data,mapping().plane)
    elif case=='empty':items=[]
    else:
        p=text_record();p.stroke_color=0;items[1]=ij.ImageJDisplayRoiImportItem('no-color.roi',ij.encode_imagej_text_roi(p),mapping())
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(before,items)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0
    assert reads.counts().plane_reads==reads.counts().region_reads==0

def test_envelope_native_replacement_retains_input_and_original_basis():
    doc,_=owner();item=display('image',True)
    m=ij.decode_imagej_display_roi_envelope(item.data);old=m.annotation
    row=ij.PlanarDisplayAnnotation('replacement',old.payload,'xy',4,t=None,c=1,attributes={'replaced':[True,2**64-1]})
    item=ij.ImageJDisplayRoiImportItem(item.name,item.data,native_replacement=ij.ImageJDisplayRoiNativeReplacement(EXTENT,row))
    r=publish(doc,[item]);saved=r.provenance()['entries'][0]['source']
    assert r.records[0].annotation.name=='replacement' and r.records[0].source_bytes==m.source_bytes
    assert base64.b64decode(saved['data'])==item.data and saved['mapping'] is None
    assert saved['native_replacement']['rows'][0]['attributes']==row.attributes
    retained=ij.inspect_imagej_annotation_sources(doc.snapshot())[0]
    assert retained.native_replacement.annotation.attributes==row.attributes
    record=r.record();ij.validate_imagej_annotation_import_record(record)
    assert ij.decode_imagej_annotation_import_request(record).items[0].data==item.data

@pytest.mark.parametrize('corruption',['kind','data','mapping','replacement','extra','version','base64'])
def test_complete_old_archive_admitted_before_replace(corruption):
    doc,reads=owner();r=publish(doc);saved=r.provenance()
    if corruption=='kind':saved['entries'][0]['kind']='unknown'
    elif corruption=='data':saved['entries'][0]['source']['data']='AA=='
    elif corruption=='mapping':saved['entries'][1]['source']['mapping']['coordinate']=999
    elif corruption=='replacement':saved['entries'][-1]['source']['native_replacement']={'bad':True}
    elif corruption=='extra':saved['entries'][1]['source']['extra']=0
    elif corruption=='version':saved['version']=True
    else:saved['entries'][0]['source']['data']+='\n'
    tx=doc.begin_edit();tx.set_extension(SOURCES,saved);doc.commit(tx);before=doc.snapshot();history=doc.undo_steps
    for action in (lambda:ij.inspect_imagej_annotation_sources(before),lambda:ij.resolve_imagej_annotation_import(before,[display()],ij.ImageJRoiImportMode.REPLACE)):
        with pytest.raises((ValueError,IndexError)):action()
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history
    assert reads.counts().plane_reads==0

@pytest.mark.parametrize('field',['after','after_display','after_sources','before_display','schema','version','source','destination','limits','unknown'])
def test_closed_command_equation_rejects_tampering(field):
    doc,_=owner();r=ij.resolve_imagej_annotation_import(doc.snapshot(),batch());record=r.record()
    if field=='after':record[field]['rows'][0]['name']='tampered'
    elif field=='after_display':record[field]['rows'][0]['payload']['text']='tampered'
    elif field=='after_sources':
        entry=record[field]['entries'][1]['source'];p=ij.decode_imagej_text_roi(base64.b64decode(entry['data']));p.text='tampered'
        entry['data']=base64.b64encode(ij.encode_imagej_text_roi(p)).decode()
    elif field=='before_display':record[field]['extent'][0]=99
    elif field=='schema':record[field]='other'
    elif field=='version':record[field]=True
    elif field=='source':record[field]['shape'][0]=99
    elif field=='destination':record[field]='new_document'
    elif field=='limits':record[field]['max_display_retained_bytes']=True
    else:record[field]=0
    with pytest.raises((ValueError,KeyError)):ij.validate_imagej_annotation_import_record(record)

def test_replay_rebind_owner_revision_display_and_original_first():
    doc,reads=owner();before=doc.snapshot();r=ij.resolve_imagej_annotation_import(before,batch());record=r.record()
    ij.validate_imagej_annotation_import_record(record)
    req=ij.decode_imagej_annotation_import_request(record)
    assert req.source_extent==EXTENT and len(req.items)==4
    replay=ij.imagej_annotation_import_from_record(before,record);assert replay.provenance()==r.provenance()
    other,_=owner()
    with pytest.raises(ValueError):ij.imagej_annotation_import_from_record(other.snapshot(),record)
    tx=doc.begin_edit();tx.set_title('changed');doc.commit(tx)
    with pytest.raises(ij.RevisionConflict):ij.imagej_annotation_import_from_record(doc.snapshot(),record)
    with pytest.raises(ij.RevisionConflict):doc.commit(ij.prepare_imagej_annotation_import(r))
    bound=ij.rebind_imagej_annotation_import(doc.snapshot(),record,[display('image')]);assert len(bound.display_annotations)==1
    class Dangerous:
        def __len__(self):raise AssertionError('new choices visited before original')
        def __getitem__(self,key):raise AssertionError('new choices visited before original')
    broken=copy.deepcopy(record);broken['after_sources']['entries'][0]['source']['data']='AA=='
    with pytest.raises(ValueError):ij.rebind_imagej_annotation_import(doc.snapshot(),broken,Dangerous())
    current=doc.snapshot();resolved=ij.resolve_imagej_annotation_import(current,batch());display_state=current.display;display_state.t=1;doc.set_display(display_state)
    doc.commit(ij.prepare_imagej_annotation_import(resolved));assert doc.snapshot().display.t==1
    assert reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('case',['entries','total','provenance','decoded','text','pixels','display-retained'])
def test_aggregate_and_codec_limits_refuse_without_publication(case):
    doc,_=owner();before=doc.snapshot();kwargs={};wire={}
    if case=='entries':kwargs['max_entries']=3
    elif case=='total':kwargs['max_total_bytes']=sum(len(i.data) for i in batch())-1
    elif case=='provenance':kwargs['max_provenance_bytes']=512
    elif case=='decoded':kwargs['max_decoded_bytes']=512
    elif case=='text':wire['max_text_bytes']=8
    elif case=='pixels':wire['max_image_pixels']=1
    limits=ij.ImageJAnnotationImportLimits(ij.ImageJRoiImportLimits(roi=ij.ImageJRoiLimits(**wire),**kwargs),max_display_retained_bytes=4096 if case=='display-retained' else 128*1024*1024)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(before,batch(),limits=limits)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==0

@pytest.mark.parametrize('v',[True,0,-1,1.5,2**100])
def test_strict_mixed_limits(v):
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.ImageJAnnotationImportLimits(max_display_retained_bytes=v)

def test_archive_does_not_associate_source_index_with_edited_rows():
    doc,_=owner();publish(doc);sources=ij.inspect_imagej_annotation_sources(doc.snapshot())
    doc.commit(ij.prepare_delete_planar_display_annotation(doc.snapshot(),0))
    doc.commit(ij.prepare_delete_planar_annotation(doc.snapshot(),0))
    assert len(sources)==len(ij.inspect_imagej_annotation_sources(doc.snapshot()))==4
    assert len(ij.planar_annotations(doc.snapshot()))==len(ij.planar_display_annotations(doc.snapshot()))==1
    new=ij.resolve_imagej_annotation_import(doc.snapshot(),[display(name='another.roi')])
    assert len(new.annotations)==1 and len(new.display_annotations)==2 and len(new.provenance()['entries'])==5

def test_valid_empty_v3_archive_from_python_and_session_is_admitted():
    doc,_=owner();empty={'schema':'imagejxx.imagej-roi-import-sources','version':3,'entries':[]}
    tx=doc.begin_edit();tx.set_extension(SOURCES,empty);doc.commit(tx)
    assert ij.inspect_imagej_annotation_sources(doc.snapshot())==[]
    r=ij.resolve_imagej_annotation_import(doc.snapshot(),[display()]);ij.validate_imagej_annotation_import_record(r.record())
    doc.commit(ij.prepare_imagej_annotation_import(r));assert len(ij.inspect_imagej_annotation_sources(doc.snapshot()))==1

def test_explicit_color_mapping_and_owned_copies_survive_archive():
    doc,_=owner();p=text_record();p.stroke_color=0;raw=ij.encode_imagej_text_roi(p)
    item=ij.ImageJDisplayRoiImportItem('default-color.roi',raw,mapping(fallback=0xff334455))
    r=publish(doc,[item]);assert r.display_annotations[0].payload.color==0xff334455
    retained=ij.inspect_imagej_annotation_sources(doc.snapshot())[0]
    assert retained.mapping.text_default_color==0xff334455 and retained.data==raw
    plane=retained.mapping.plane;plane.extent=[1,1,1,1,1,1]
    assert retained.mapping.plane.extent==EXTENT

def test_captured_source_prefix_cannot_be_rewritten_or_removed():
    doc,_=owner();publish(doc)
    r=ij.resolve_imagej_annotation_import(doc.snapshot(),[display(name='new.roi')]);record=r.record()
    for remove in (False,True):
        changed=copy.deepcopy(record)
        if remove:changed['after_sources']['entries'].pop(0)
        else:changed['after_sources']['entries'][0]['source']['name']='changed prefix'
        with pytest.raises(ValueError):ij.validate_imagej_annotation_import_record(changed)

def test_new_suffix_label_is_request_metadata_and_does_not_rename_native_row():
    doc,_=owner();r=ij.resolve_imagej_annotation_import(doc.snapshot(),[display()]);record=r.record()
    record['after_sources']['entries'][0]['source']['name']='explicit new transport label'
    ij.validate_imagej_annotation_import_record(record)
    replay=ij.imagej_annotation_import_from_record(doc.snapshot(),record)
    assert replay.display_annotations[0].name==text_record().name
    assert replay.provenance()['entries'][0]['source']['name']=='explicit new transport label'

@pytest.mark.parametrize('case',['entries','total'])
def test_append_limits_include_entire_historical_mixed_archive(case):
    doc,_=owner();publish(doc);before=doc.snapshot();old=ij.inspect_imagej_annotation_sources(before);new=display(name='next.roi')
    limits=ij.ImageJRoiImportLimits(max_entries=4) if case=='entries' else ij.ImageJRoiImportLimits(max_total_bytes=sum(len(i.data) for i in old)+len(new.data)-1)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(before,[new],limits=ij.ImageJAnnotationImportLimits(limits))
    assert doc.snapshot().shares_data(before)

def test_input_sequence_is_snapshotted_once_before_preflight_and_copy():
    doc,_=owner();good=display();bad=ij.ImageJDisplayRoiImportItem('huge.roi',good.data*20,good.mapping)
    class Changing:
        def __init__(self):self.calls=0
        def __len__(self):return 1
        def __getitem__(self,index):
            if index:raise IndexError
            self.calls+=1;return good if self.calls==1 else bad
    values=Changing();r=ij.resolve_imagej_annotation_import(doc.snapshot(),values)
    assert values.calls==1 and r.provenance()['entries'][0]['source']['data']==base64.b64encode(good.data).decode()

def test_maximum_native_rgb_image_import_with_explicit_aggregate_budget():
    doc,reads=owner();p=image_record();pixels=np.zeros((1024,1024,3),dtype=np.uint8);pixels[:,:,0]=255;p.pixels=pixels
    raw=ij.encode_imagej_image_roi(p);item=ij.ImageJDisplayRoiImportItem('maximum.roi',raw,mapping())
    limits=ij.ImageJAnnotationImportLimits(ij.ImageJRoiImportLimits(max_decoded_bytes=128*1024*1024))
    r=ij.resolve_imagej_annotation_import(doc.snapshot(),[item],limits=limits)
    doc.commit(ij.prepare_imagej_annotation_import(r));row=ij.planar_display_annotations(doc.snapshot())[0]
    assert (row.payload.width,row.payload.height,row.payload.channels)==(1024,1024,3)
    assert len(row.payload.pixels)==3*1024*1024 and row.payload.pixels[:6]==bytes([255,0,0,255,0,0])
    assert ij.inspect_imagej_annotation_sources(doc.snapshot(),limits)[0].data==raw
    assert reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('compressed',[False,True])
def test_mixed_zip_transport_complete_semantic_admission(compressed):
    doc,_=owner();items=batch();wire=ij.encode_imagej_roi_zip([ij.ImageJRoiZipEntry(i.name,i.data) for i in items],compress=compressed)
    members=ij.decode_imagej_roi_zip(wire)
    rebuilt=[type(i)(m.name,m.data,i.mapping,i.native_replacement) for i,m in zip(items,members)]
    r=ij.resolve_imagej_annotation_import(doc.snapshot(),rebuilt)
    assert [base64.b64decode(e['source']['data']) for e in r.provenance()['entries']]==[i.data for i in items]
    assert len(r.annotations)==len(r.display_annotations)==2
