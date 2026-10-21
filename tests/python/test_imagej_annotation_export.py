"""Explicit heterogeneous pairing, pure canonical ROI/ZIP equations and replay."""
import base64
import copy
import gc
import io
import json
import weakref
import zipfile
import pytest
import imagejxx as ij
from test_imagej_annotation_import import owner,batch,publish,display,EXTENT,SOURCES
from test_imagej_display_roi import edited_text,projection as display_projection
from test_imagej_roi_zip_export import projection as geometric_projection

SEL=ij.ImageJAnnotationKind.SELECTION
DIS=ij.ImageJAnnotationKind.DISPLAY

def fixture():
    doc,reads=owner();publish(doc)
    native,_=edited_text();doc.commit(ij.prepare_update_planar_display_annotation(doc.snapshot(),0,native.annotation))
    catalog=ij.inspect_imagej_annotation_catalog(doc.snapshot())
    choices=[(2,SEL,0,'geometry.roi'),(1,DIS,0,'text-λ😀##.roi'),(3,DIS,1,'image.roi')]
    items=[]
    for source,kind,row,name in choices:
        pair=ij.resolve_imagej_annotation_pairing(catalog,source,kind,row)
        p=geometric_projection(pair.mapped.annotation) if kind==SEL else display_projection(pair.mapped)
        options=ij.ImageJDisplayProjectionOptions() if kind==SEL else (ij.ImageJDisplayProjectionOptions(round_font_size=True) if row==0 else ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True,round_image_origin=True))
        items.append(ij.ImageJAnnotationExportItem(name,source,kind,row,p,options))
    return doc,reads,items

@pytest.mark.parametrize('compress',[False,True])
def test_complete_mixed_zip_exact_rows_originals_and_independent_transport(compress):
    doc,reads,items=fixture();before=doc.snapshot();history=doc.undo_steps
    result=ij.resolve_imagej_annotation_zip_export(before,items,compress=compress);record=result.record()
    assert result.is_zip and result.compress==compress
    assert set(record)=={'schema','version','source','limits','annotations','display_annotations','sources','members','packaging','compress','output'}
    assert record['packaging']=='zip' and record['sources']==before.metadata['extensions'][SOURCES]
    assert json.dumps(record).count('imagejxx.imagej-roi-import-sources')==1
    assert base64.b64decode(record['output'])==result.data
    with zipfile.ZipFile(io.BytesIO(result.data)) as transport:
        assert transport.namelist()==[i.name for i in items]
        assert [i.compress_type for i in transport.infolist()]==[zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED]*3
        for member,item in zip(result.members,items):
            assert member.data==transport.read(item.name)
            assert member.request.name==item.name and member.request.source_index==item.source_index
            if item.annotation_kind==SEL:
                decoded=ij.decode_imagej_roi_native_envelope(member.data)
                assert decoded.source_record.group==7 and decoded.annotation.selection.points==member.mapped.annotation.selection.points
            else:
                decoded=ij.decode_imagej_display_roi_envelope(member.data)
                assert decoded.source_bytes==member.mapped.source_bytes
                assert decoded.annotation.payload.bounds==member.mapped.annotation.payload.bounds
                assert decoded.annotation.attributes==member.mapped.annotation.attributes
                if item.annotation_index==0:assert decoded.annotation.payload.font_size==13.500000000001
                else:assert decoded.annotation.payload.pixels==bytes([255,0,0,128,0,0,0,255])
    ij.validate_imagej_annotation_export_record(record)
    assert ij.imagej_annotation_export_from_record(before,record).data==result.data
    request=ij.decode_imagej_annotation_export_request(record)
    assert request.is_zip and request.compress==compress and len(request.items)==3
    assert ij.resolve_imagej_annotation_zip_export(before,request.items,request.limits,request.compress).data==result.data
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history and reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('member',[0,1,2])
def test_single_roi_same_native_envelope_and_pairing_guard(member):
    doc,reads,items=fixture();before=doc.snapshot();item=items[member]
    result=ij.resolve_imagej_annotation_export(before,item)
    assert not result.is_zip and not result.compress and result.data==result.members[0].data
    assert result.record()['packaging']=='roi' and len(result.record()['members'])==1
    pair=ij.resolve_imagej_annotation_pairing(ij.inspect_imagej_annotation_catalog(before),item.source_index,item.annotation_kind,item.annotation_index)
    ij.validate_imagej_annotation_pairing_record(pair.record())
    assert ij.encode_imagej_annotation_pairing_envelope(before,pair,item.standard_projection,item.options)==result.data
    assert ij.imagej_annotation_pairing_from_record(before,pair.record()).mapped.annotation.name==pair.mapped.annotation.name
    assert ij.imagej_annotation_export_from_record(before,result.record()).data==result.data
    request=ij.decode_imagej_annotation_export_request(result.record())
    assert not request.is_zip and ij.resolve_imagej_annotation_export(before,request.items[0],request.limits).data==result.data
    assert reads.counts().plane_reads==reads.counts().region_reads==0

def test_catalog_incoming_envelopes_and_independent_row_choices():
    doc,_,items=fixture();catalog=ij.inspect_imagej_annotation_catalog(doc.snapshot())
    assert [s.kind for s in catalog.entries]==[SEL,DIS,SEL,DIS]
    assert [s.index for s in catalog.entries]==list(range(4))
    assert [s.data_bytes for s in catalog.entries]==[len(i.data) for i in batch()]
    assert [s.has_mapping for s in catalog.entries]==[True,True,False,False]
    # Display-source kind may be explicitly rebound to the other display payload.
    pair=ij.resolve_imagej_annotation_pairing(catalog,1,DIS,1)
    assert isinstance(pair.mapped.source_record,ij.ImageJTextRoi) and isinstance(pair.mapped.annotation.payload,ij.ImageAnnotationPayload)
    p=display_projection(pair.mapped);item=ij.ImageJAnnotationExportItem('cross-kind.roi',1,DIS,1,p,items[2].options)
    decoded=ij.decode_imagej_display_roi_envelope(ij.resolve_imagej_annotation_export(doc.snapshot(),item).data)
    assert isinstance(decoded.source_record,ij.ImageJTextRoi) and decoded.annotation.payload.pixels==pair.mapped.annotation.payload.pixels
    doc.commit(ij.prepare_delete_planar_display_annotation(doc.snapshot(),0))
    fresh=ij.inspect_imagej_annotation_catalog(doc.snapshot())
    assert len(fresh.entries)==4
    pair=ij.resolve_imagej_annotation_pairing(fresh,1,DIS,0)
    assert isinstance(pair.mapped.annotation.payload,ij.ImageAnnotationPayload)

@pytest.mark.parametrize('source,kind,row',[(999,SEL,0),(0,SEL,999),(0,DIS,0),(1,SEL,0)])
def test_explicit_pairing_refuses_bad_indices_or_category(source,kind,row):
    doc,_,_=fixture()
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_pairing(ij.inspect_imagej_annotation_catalog(doc.snapshot()),source,kind,row)

@pytest.mark.parametrize('case',['duplicate','late-source','late-row','late-category','late-projection','late-options','empty'])
def test_complete_batch_refuses_without_mutation(case):
    doc,reads,items=fixture();before=doc.snapshot();history=doc.undo_steps;last=items[-1]
    if case=='empty':items=[]
    else:
        name=items[0].name if case=='duplicate' else last.name
        source=999 if case=='late-source' else last.source_index
        row=999 if case=='late-row' else last.annotation_index
        kind=SEL if case=='late-category' else last.annotation_kind
        p=items[1].standard_projection if case=='late-projection' else last.standard_projection
        options=ij.ImageJDisplayProjectionOptions() if case=='late-options' else last.options
        items[-1]=ij.ImageJAnnotationExportItem(name,source,kind,row,p,options)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_zip_export(before,items)
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history and reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('path,value',[
    (('version',),True),(('schema',),'other'),(('extra',),0),(('compress',),1),(('packaging',),'other'),
    (('members',1,'source_index'),True),(('members',2,'annotation_index'),999),(('members',0,'annotation_kind'),'display'),
    (('members',2,'projection_kind'),'text'),(('members',1,'options','round_font_size'),False),
    (('members',2,'options','drop_image_alpha'),1),(('members',2,'options','extra'),0),
    (('members',2,'name'),'geometry.roi'),(('members',1,'projection'),'AAAA'),
    (('sources','entries',0,'source','data'),'AAAA'),(('output',),'AAAA'),
    (('limits','output','max_record_bytes'),1),(('limits','zip','max_entries'),0),
    (('display_annotations','rows',0,'payload','text'),'tampered')])
def test_closed_export_equation_and_original_first(path,value):
    doc,reads,items=fixture();record=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),items).record();node=record
    for key in path[:-1]:node=node[key]
    node[path[-1]]=value
    for action in (lambda:ij.validate_imagej_annotation_export_record(record),lambda:ij.decode_imagej_annotation_export_request(record),lambda:ij.rebind_imagej_annotation_zip_export(doc.snapshot(),record,items)):
        with pytest.raises((ValueError,TypeError)):action()
    assert reads.counts().plane_reads==0

@pytest.mark.parametrize('case',['source','decoded','projection','record','member','total','archive','working','count','display'])
def test_aggregate_source_projection_output_and_zip_limits(case):
    doc,_,items=fixture();kwargs={}
    if case=='source':kwargs['sources']=ij.ImageJAnnotationImportLimits(ij.ImageJRoiImportLimits(max_total_bytes=64))
    elif case=='decoded':kwargs['sources']=ij.ImageJAnnotationImportLimits(ij.ImageJRoiImportLimits(max_decoded_bytes=512))
    elif case=='projection':kwargs['output']=ij.ImageJRoiExportLimits(roi=ij.ImageJRoiLimits(max_bytes=64))
    elif case=='record':kwargs['output']=ij.ImageJRoiExportLimits(max_record_bytes=1000)
    elif case=='display':kwargs['max_display_retained_bytes']=4096
    else:
        values={'member':{'max_entry_bytes':64},'total':{'max_total_bytes':64},'archive':{'max_archive_bytes':22},'working':{'max_working_bytes':64},'count':{'max_entries':2}}[case]
        kwargs['zip']=ij.ImageJRoiZipLimits(**values)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_zip_export(doc.snapshot(),items,ij.ImageJAnnotationExportLimits(**kwargs))

def test_owner_revision_display_navigation_and_explicit_rebind():
    doc,reads,items=fixture();before=doc.snapshot();r=ij.resolve_imagej_annotation_zip_export(before,items);pair=ij.resolve_imagej_annotation_pairing(ij.inspect_imagej_annotation_catalog(before),1,DIS,0)
    other,_,_=fixture()
    with pytest.raises(ValueError):ij.imagej_annotation_export_from_record(other.snapshot(),r.record())
    with pytest.raises(ValueError):ij.imagej_annotation_pairing_from_record(other.snapshot(),pair.record())
    state=before.display;state.t=1;doc.set_display(state)
    assert ij.imagej_annotation_export_from_record(doc.snapshot(),r.record()).data==r.data
    tx=doc.begin_edit();tx.set_title('changed');doc.commit(tx)
    with pytest.raises(ij.RevisionConflict):ij.imagej_annotation_export_from_record(doc.snapshot(),r.record())
    with pytest.raises(ij.RevisionConflict):ij.encode_imagej_annotation_pairing_envelope(doc.snapshot(),pair,items[1].standard_projection,items[1].options)
    assert ij.rebind_imagej_annotation_zip_export(doc.snapshot(),r.record(),items).data==r.data
    assert not ij.rebind_imagej_annotation_export(doc.snapshot(),r.record(),items[0]).is_zip
    assert ij.rebind_imagej_annotation_pairing(doc.snapshot(),pair.record(),3,DIS,1).annotation_index==1
    assert reads.counts().plane_reads==0

def test_corrupt_historical_member_blocks_unrelated_export_and_new_choices():
    doc,_,items=fixture();good=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),items).record();broken=copy.deepcopy(good)
    broken['sources']['entries'][0]['source']['data']='AA=='
    class Dangerous:
        def __len__(self):raise AssertionError('choices visited before original')
        def __getitem__(self,index):raise AssertionError('choices visited before original')
    with pytest.raises(ValueError):ij.rebind_imagej_annotation_zip_export(doc.snapshot(),broken,Dangerous())
    saved=doc.snapshot().metadata['extensions'][SOURCES];saved['entries'][0]['source']['data']='AA=='
    tx=doc.begin_edit();tx.set_extension(SOURCES,saved);doc.commit(tx)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_export(doc.snapshot(),items[1])
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_zip_export(doc.snapshot(),Dangerous())

@pytest.mark.parametrize('name',['../a.roi','a/b.roi','a\\b.roi','CON.roi','a.ROI','.roi','x\x00.roi','a?.roi','a\n.roi','x'*1025+'.roi'])
def test_flat_portable_member_names(name):
    doc,_,items=fixture();i=items[0]
    with pytest.raises((ValueError,UnicodeError)):ij.ImageJAnnotationExportItem(name,i.source_index,i.annotation_kind,i.annotation_index,i.standard_projection)

@pytest.mark.parametrize('v',[True,-1,1.5,2**100])
def test_strict_export_indices(v):
    _,_,items=fixture();i=items[0]
    with pytest.raises((TypeError,ValueError,OverflowError)):ij.ImageJAnnotationExportItem('a.roi',v,SEL,0,i.standard_projection)

def test_immutable_owned_projection_and_nested_result_lifetimes():
    doc,_,items=fixture();source=items[2].standard_projection;original=source.pixels.copy();source.pixels[:]=0
    result=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),items);ref=weakref.ref(result);members=result.members;mapped=members[2].mapped
    request=members[2].request;copy=request.standard_projection;copy.pixels[:]=0
    assert request.standard_projection.pixels.tolist()==original.tolist()
    del result,members;gc.collect();assert ref() is not None and mapped.annotation.payload.channels==4
    del mapped;gc.collect();assert ref() is None

def test_same_source_multiple_rows_and_case_distinct_names():
    doc,_,items=fixture();a,b=items[1:]
    other=ij.ImageJAnnotationExportItem('A.roi',a.source_index,DIS,b.annotation_index,b.standard_projection,b.options)
    first=ij.ImageJAnnotationExportItem('a.roi',a.source_index,DIS,a.annotation_index,a.standard_projection,a.options)
    r=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),[first,other])
    assert [m.request.name for m in r.members]==['a.roi','A.roi']
    assert all(isinstance(m.mapped.source_record,ij.ImageJTextRoi) for m in r.members)
    for member in r.members:assert ij.decode_imagej_display_roi_envelope(member.data).source_bytes==r.members[0].mapped.source_bytes

@pytest.mark.parametrize('legacy',[1,2])
def test_mixed_export_engine_accepts_legacy_geometric_archives(legacy):
    from test_imagej_roi_import import bare,envelope
    doc,_=owner();item=bare() if legacy==1 else envelope()
    if legacy==2:
        m=ij.decode_imagej_roi_envelope(item.data)
        item=ij.ImageJRoiImportItem(item.name,item.data,native_replacement=ij.ImageJRoiNativeReplacement(EXTENT,m.annotation))
    doc.commit(ij.prepare_imagej_roi_import(ij.resolve_imagej_roi_import(doc.snapshot(),[item])))
    catalog=ij.inspect_imagej_annotation_catalog(doc.snapshot());pair=ij.resolve_imagej_annotation_pairing(catalog,0,SEL,0)
    i=ij.ImageJAnnotationExportItem('legacy.roi',0,SEL,0,geometric_projection(pair.mapped.annotation))
    result=ij.resolve_imagej_annotation_export(doc.snapshot(),i)
    assert result.record()['sources']['version']==legacy and ij.decode_imagej_roi_native_envelope(result.data).source_record.group==7

@pytest.mark.parametrize('zip_output',[False,True])
@pytest.mark.parametrize('channels',[3,4])
def test_maximum_rgb_annotation_single_and_zip_export(zip_output,channels):
    import numpy as np
    from test_imagej_display_roi import image_record,mapping
    doc,reads=owner();p=image_record();pixels=np.zeros((1024,1024,3),dtype=np.uint8);pixels[:,:,0]=255;p.pixels=pixels
    raw=ij.encode_imagej_image_roi(p);item=ij.ImageJDisplayRoiImportItem('maximum.roi',raw,mapping())
    source_limits=ij.ImageJAnnotationImportLimits(ij.ImageJRoiImportLimits(max_decoded_bytes=128*1024*1024))
    doc.commit(ij.prepare_imagej_annotation_import(ij.resolve_imagej_annotation_import(doc.snapshot(),[item],limits=source_limits)))
    if channels==4:
        row=ij.planar_display_annotations(doc.snapshot())[0];rgba=np.zeros((1024,1024,4),dtype=np.uint8);rgba[:,:,0]=255;rgba[:,:,3]=128
        payload=ij.ImageAnnotationPayload(row.payload.bounds,1024,1024,4,rgba.tobytes(),opacity=row.payload.opacity,zero_transparent=row.payload.zero_transparent)
        row=ij.PlanarDisplayAnnotation(row.name,payload,row.plane,row.coordinate,t=row.t,c=row.c,attributes=row.attributes)
        doc.commit(ij.prepare_update_planar_display_annotation(doc.snapshot(),0,row))
    p.serialized_image=b''
    item=ij.ImageJAnnotationExportItem('maximum.roi',0,DIS,0,p,ij.ImageJDisplayProjectionOptions(drop_image_alpha=channels==4))
    limits=ij.ImageJAnnotationExportLimits(sources=source_limits,output=ij.ImageJRoiExportLimits(roi=ij.ImageJRoiLimits(max_bytes=64*1024*1024,max_text_bytes=64*1024*1024),max_record_bytes=128*1024*1024),max_display_retained_bytes=256*1024*1024,zip=ij.ImageJRoiZipLimits(max_entry_bytes=64*1024*1024))
    r=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),[item],limits) if zip_output else ij.resolve_imagej_annotation_export(doc.snapshot(),item,limits)
    back=ij.decode_imagej_display_roi_envelope(r.members[0].data,ij.ImageJDisplayRoiLimits(wire=limits.output.roi,max_retained_bytes=256*1024*1024))
    assert back.source_bytes==raw and len(back.annotation.payload.pixels)==channels*1024*1024
    if channels==4:assert back.annotation.payload.pixels[:8]==bytes([255,0,0,128,255,0,0,128])
    ij.validate_imagej_annotation_export_record(r.record())
    assert reads.counts().plane_reads==reads.counts().region_reads==0
