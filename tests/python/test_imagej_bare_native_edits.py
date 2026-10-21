"""V4 bare original mappings plus explicit independent native edits."""
import base64
import copy
import pytest
import imagejxx as ij
from test_imagej_annotation_import import owner,display,EXTENT,SOURCES
from test_imagej_roi_import import bare
from test_imagej_display_roi import projection
from test_imagej_roi_zip_export import projection as geometric_projection

def fixture():
    doc,reads=owner();g=bare();t=display('text');im=display('image')
    geo=ij.PlanarAnnotation('edited geometry',ij.Selection('point',[(1.250000000001,2.5)]),'yz',2,t=1,c=None,attributes={'opaque':2**64-1})
    text=ij.PlanarDisplayAnnotation('edited text',ij.TextAnnotationPayload([1.250000000001,2.5,8,4],'Edited λ😀',font_size=13.500000000001,color=0x803366cc),'xy',1,t=None,c=1,attributes={'future':[True,2**64-1]})
    image=ij.PlanarDisplayAnnotation('edited image',ij.ImageAnnotationPayload([1.250000000001,2.500000000001,4.5,2.25],2,1,4,bytes([255,0,0,128,0,0,0,255]),opacity=77,zero_transparent=True),'xz',2,t=0,c=None,attributes={'future':'kept'})
    values=[ij.ImageJRoiImportItem(g.name,g.data,g.mapping,ij.ImageJRoiNativeReplacement(EXTENT,geo)),ij.ImageJDisplayRoiImportItem(t.name,t.data,t.mapping,ij.ImageJDisplayRoiNativeReplacement(EXTENT,text)),ij.ImageJDisplayRoiImportItem(im.name,im.data,im.mapping,ij.ImageJDisplayRoiNativeReplacement(EXTENT,image))]
    return doc,reads,values,(geo,text,image)

@pytest.mark.parametrize('mode',[ij.ImageJRoiImportMode.APPEND,ij.ImageJRoiImportMode.REPLACE])
def test_bare_original_and_mapping_independent_of_complete_native_edit(mode):
    doc,reads,items,rows=fixture();before=doc.snapshot();r=ij.resolve_imagej_annotation_import(before,items,mode)
    assert r.provenance()['version']==4 and doc.snapshot().shares_data(before)
    for member,item,row in zip(r.records,items,rows):
        assert member.annotation.name==row.name and member.annotation.plane==row.plane and member.annotation.coordinate==row.coordinate
        assert member.annotation.attributes==row.attributes
    assert r.records[0].source_mapping.plane==items[0].mapping.plane and r.records[0].source_mapping.plane!=rows[0].plane
    assert r.records[1].source_bytes==items[1].data and r.records[1].source_record.text!='Edited λ😀'
    assert r.records[1].annotation.payload.font_size==13.500000000001
    assert r.records[2].annotation.payload.pixels==rows[2].payload.pixels and r.records[2].source_record.pixels.tolist()==[[[1,2,3],[4,5,6]]]
    saved=r.provenance()
    for source,item in zip(saved['entries'],items):
        assert base64.b64decode(source['source']['data'])==item.data
        assert source['source']['mapping'] is not None and source['source']['native_replacement'] is not None
    ij.validate_imagej_annotation_import_record(r.record());request=ij.decode_imagej_annotation_import_request(r.record())
    assert all(i.mapping is not None and i.native_replacement is not None for i in request.items)
    assert ij.imagej_annotation_import_from_record(before,r.record()).provenance()==saved
    doc.commit(ij.prepare_imagej_annotation_import(r));assert doc.undo_steps==1
    inspected=ij.inspect_imagej_annotation_sources(doc.snapshot());assert [i.data for i in inspected]==[i.data for i in items]
    doc.undo();assert doc.snapshot().shares_data(before);doc.redo()
    assert reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('which',[0,1,2])
def test_original_admission_cannot_be_replaced_by_valid_native_row(which):
    doc,_,items,_=fixture();i=items[which]
    invalid=type(i)(i.name,b'bad',i.mapping,i.native_replacement)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(doc.snapshot(),[invalid])

@pytest.mark.parametrize('case',['v3','v2','v4_without_edit','bad_map','bad_replacement','bad_original'])
def test_version_gates_and_full_archive_admission_even_for_replace(case):
    doc,_,items,_=fixture();r=ij.resolve_imagej_annotation_import(doc.snapshot(),items);archive=r.provenance()
    if case in ('v3','v2'):archive['version']=int(case[1:])
    elif case=='v4_without_edit':
        for e in archive['entries']:e['source']['native_replacement']=None
    elif case=='bad_map':archive['entries'][0]['source']['mapping']['coordinate']=999
    elif case=='bad_replacement':archive['entries'][1]['source']['native_replacement']['rows'][0]['coordinate']=999
    else:archive['entries'][2]['source']['data']='AA=='
    tx=doc.begin_edit();tx.set_extension(SOURCES,archive);doc.commit(tx);before=doc.snapshot()
    with pytest.raises((ValueError,IndexError)):ij.inspect_imagej_annotation_sources(before)
    with pytest.raises((ValueError,IndexError)):ij.resolve_imagej_annotation_import(before,[display()],ij.ImageJRoiImportMode.REPLACE)
    assert doc.snapshot().shares_data(before)

def test_v3_old_items_stay_v3_and_v4_is_retained_only_when_needed():
    doc,_,items,_=fixture();plain=display();assert ij.resolve_imagej_annotation_import(doc.snapshot(),[plain]).provenance()['version']==3
    doc.commit(ij.prepare_imagej_annotation_import(ij.resolve_imagej_annotation_import(doc.snapshot(),items)))
    assert ij.resolve_imagej_annotation_import(doc.snapshot(),[plain]).provenance()['version']==4
    assert ij.resolve_imagej_annotation_import(doc.snapshot(),[plain],ij.ImageJRoiImportMode.REPLACE).provenance()['version']==3

def test_legacy_geometric_apis_keep_mapping_replacement_exclusion():
    doc,_,items,_=fixture()
    with pytest.raises(ValueError):ij.resolve_imagej_roi_import(doc.snapshot(),[items[0]])

def test_v4_pairing_single_zip_export_and_canonical_output():
    doc,reads,items,rows=fixture();doc.commit(ij.prepare_imagej_annotation_import(ij.resolve_imagej_annotation_import(doc.snapshot(),items)))
    catalog=ij.inspect_imagej_annotation_catalog(doc.snapshot());out=[]
    for source,kind,index in [(0,ij.ImageJAnnotationKind.SELECTION,0),(1,ij.ImageJAnnotationKind.DISPLAY,0),(2,ij.ImageJAnnotationKind.DISPLAY,1)]:
        pair=ij.resolve_imagej_annotation_pairing(catalog,source,kind,index)
        p=geometric_projection(pair.mapped.annotation) if source==0 else projection(pair.mapped)
        options=ij.ImageJDisplayProjectionOptions() if source==0 else ij.ImageJDisplayProjectionOptions(round_font_size=True) if source==1 else ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True,round_image_origin=True)
        out.append(ij.ImageJAnnotationExportItem(str(source)+'.roi',source,kind,index,p,options))
    result=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),out);ij.validate_imagej_annotation_export_record(result.record())
    assert result.record()['sources']['version']==4
    assert ij.imagej_annotation_export_from_record(doc.snapshot(),result.record()).data==result.data
    assert result.members[1].mapped.source_bytes==items[1].data
    assert reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('which',[0,1,2])
def test_native_edit_requires_current_extent_and_valid_positions(which):
    doc,_,items,rows=fixture();i=items[which];row=rows[which]
    badshape=list(EXTENT);badshape[-2]+=1
    replacement=(ij.ImageJRoiNativeReplacement if which==0 else ij.ImageJDisplayRoiNativeReplacement)(badshape,row)
    item=type(i)(i.name,i.data,i.mapping,replacement)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(doc.snapshot(),[item])

def test_rebind_validates_v4_original_before_new_choices():
    doc,_,items,_=fixture();record=ij.resolve_imagej_annotation_import(doc.snapshot(),items).record();bad=copy.deepcopy(record);bad['after_sources']['entries'][1]['source']['data']='AA=='
    class Dangerous:
        def __len__(self):raise AssertionError('new choices visited first')
        def __getitem__(self,index):raise AssertionError('new choices visited first')
    with pytest.raises(ValueError):ij.rebind_imagej_annotation_import(doc.snapshot(),bad,Dangerous())

def test_session_preserves_v4_sources_and_captured_equation():
    doc,_,items,_=fixture();command=ij.resolve_imagej_annotation_import(doc.snapshot(),items)
    doc.commit(ij.prepare_imagej_annotation_import(command));record=command.record()
    restored=ij.decode_session(ij.encode_session(ij.SessionCapture([doc],workflow=[record])))
    snapshot=restored.documents[0].snapshot()
    assert snapshot.metadata['extensions'][SOURCES]==command.provenance()
    assert [v.data for v in ij.inspect_imagej_annotation_sources(snapshot)]==[v.data for v in items]
    assert restored.workflow==[record];ij.validate_imagej_annotation_import_record(restored.workflow[0])

@pytest.mark.parametrize('kind',[ij.ImageJRoiImportItem,ij.ImageJDisplayRoiImportItem])
def test_python_ownership_consent_supports_core_64mib_input_bound(kind):
    data=b'x'*(16*1024*1024+1)
    with pytest.raises(ValueError):kind('large.roi',data)
    item=kind('large.roi',data,max_input_bytes=64*1024*1024)
    assert item.data==data
    doc,_=owner()
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(doc.snapshot(),[item])

@pytest.mark.parametrize('cap',[0,-1,True,64*1024*1024+1,'64'])
@pytest.mark.parametrize('kind',[ij.ImageJRoiImportItem,ij.ImageJDisplayRoiImportItem])
def test_input_ownership_cap_is_explicit_bounded_real_integer(kind,cap):
    with pytest.raises((ValueError,OverflowError,TypeError)):kind('bad.roi',b'bad',max_input_bytes=cap)

def test_maximum_bare_rgba_edit_export_and_explicit_python_envelope_reimport():
    import numpy as np
    from test_imagej_display_roi import image_record,mapping
    doc,reads=owner();p=image_record();rgb=np.zeros((1024,1024,3),dtype=np.uint8);rgb[:,:,0]=255;p.pixels=rgb
    raw=ij.encode_imagej_image_roi(p);mapped=ij.decode_imagej_display_roi_mapped(raw,mapping())
    old=mapped.annotation;payload=ij.ImageAnnotationPayload(old.payload.bounds,1024,1024,4,bytes([255,0,0,128])*(1024*1024),opacity=old.payload.opacity,zero_transparent=old.payload.zero_transparent)
    row=ij.PlanarDisplayAnnotation(old.name,payload,old.plane,old.coordinate,t=old.t,c=old.c,attributes={'large':'bare edit'})
    wire=ij.ImageJRoiLimits(max_bytes=64*1024*1024,max_text_bytes=64*1024*1024)
    limits=ij.ImageJAnnotationImportLimits(ij.ImageJRoiImportLimits(roi=wire,max_total_bytes=64*1024*1024,max_provenance_bytes=64*1024*1024,max_decoded_bytes=128*1024*1024),max_display_retained_bytes=256*1024*1024)
    item=ij.ImageJDisplayRoiImportItem('maximum.roi',raw,mapping(),ij.ImageJDisplayRoiNativeReplacement(EXTENT,row))
    command=ij.resolve_imagej_annotation_import(doc.snapshot(),[item],limits=limits);assert command.provenance()['version']==4
    doc.commit(ij.prepare_imagej_annotation_import(command))
    export_limits=ij.ImageJAnnotationExportLimits(sources=limits,output=ij.ImageJRoiExportLimits(roi=wire,max_record_bytes=128*1024*1024),max_display_retained_bytes=256*1024*1024,zip=ij.ImageJRoiZipLimits(max_entry_bytes=64*1024*1024))
    pair=ij.resolve_imagej_annotation_pairing(ij.inspect_imagej_annotation_catalog(doc.snapshot(),limits),0,ij.ImageJAnnotationKind.DISPLAY,0)
    request=ij.ImageJAnnotationExportItem('maximum.roi',0,ij.ImageJAnnotationKind.DISPLAY,0,projection(pair.mapped),ij.ImageJDisplayProjectionOptions(drop_image_alpha=True))
    exported=ij.resolve_imagej_annotation_export(doc.snapshot(),request,export_limits);assert len(exported.data)>16*1024*1024
    with pytest.raises(ValueError):ij.ImageJDisplayRoiImportItem('envelope.roi',exported.data)
    incoming=ij.ImageJDisplayRoiImportItem('envelope.roi',exported.data,max_input_bytes=64*1024*1024)
    other,other_reads=owner();restored=ij.resolve_imagej_annotation_import(other.snapshot(),[incoming],limits=limits)
    assert restored.records[0].annotation.payload.pixels==payload.pixels and restored.records[0].source_bytes==raw
    assert reads.counts().plane_reads==other_reads.counts().plane_reads==0

@pytest.mark.parametrize('which',[0,1,2])
def test_bare_source_extent_stays_independent_of_larger_native_destination(which):
    from test_imagej_roi_import_native_replacement import target,TARGET
    _,_,items,rows=fixture();doc,reads=target();i=items[which];old=rows[which]
    if which==0:
        row=ij.PlanarAnnotation(old.name,old.selection,'yz',13,t=2,c=1,attributes=old.attributes)
    else:
        row=ij.PlanarDisplayAnnotation(old.name,old.payload,'xy' if which==1 else 'xz',7 if which==1 else 9,t=2,c=1,attributes=old.attributes)
    replacement=(ij.ImageJRoiNativeReplacement if which==0 else ij.ImageJDisplayRoiNativeReplacement)(TARGET,row)
    item=type(i)(i.name,i.data,i.mapping,replacement)
    result=ij.resolve_imagej_annotation_import(doc.snapshot(),[item]);mapped=result.records[0]
    assert mapped.target_extent==TARGET and mapped.annotation.coordinate==row.coordinate
    basis=mapped.source_mapping if which==0 else mapped.source_mapping.plane
    assert basis.extent==EXTENT and basis.extent!=TARGET
    ij.validate_imagej_annotation_import_record(result.record())
    doc.commit(ij.prepare_imagej_annotation_import(result))
    retained=ij.inspect_imagej_annotation_sources(doc.snapshot())[0]
    assert retained.data==i.data and retained.native_replacement.extent==TARGET
    assert reads.counts().plane_reads==reads.counts().region_reads==0

def test_native_edit_cannot_supply_an_unspecified_original_text_color():
    from test_imagej_display_roi import text_record,mapping
    doc,_,_,rows=fixture();p=text_record();p.stroke_color=0;wire=ij.encode_imagej_text_roi(p)
    edit=ij.ImageJDisplayRoiNativeReplacement(EXTENT,rows[1])
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(doc.snapshot(),[ij.ImageJDisplayRoiImportItem('color.roi',wire,mapping(),edit)])
    item=ij.ImageJDisplayRoiImportItem('color.roi',wire,mapping(fallback=0xff112233),edit)
    result=ij.resolve_imagej_annotation_import(doc.snapshot(),[item])
    assert result.records[0].source_mapping.text_default_color==0xff112233
    assert result.records[0].annotation.payload.color==rows[1].payload.color

def test_native_edit_cannot_hide_ambiguous_legacy_source_position():
    from test_imagej_display_roi import image_record,mapping
    doc,_,_,rows=fixture();p=image_record();p.legacy_position=1;wire=ij.encode_imagej_image_roi(p)
    edit=ij.ImageJDisplayRoiNativeReplacement(EXTENT,rows[2]);original=mapping()
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_import(doc.snapshot(),[ij.ImageJDisplayRoiImportItem('legacy.roi',wire,original,edit)])
    plane=original.plane;plane.legacy_position=ij.ImageJLegacyPositionMapping.FIXED_AXIS
    item=ij.ImageJDisplayRoiImportItem('legacy.roi',wire,ij.ImageJDisplayRoiMapping(plane),edit)
    result=ij.resolve_imagej_annotation_import(doc.snapshot(),[item]);assert result.records[0].annotation.coordinate==rows[2].coordinate
