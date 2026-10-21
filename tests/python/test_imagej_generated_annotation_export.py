"""Explicit generated bases for created annotations, without fictional imports."""
import base64
import copy
import io
import zipfile
import numpy as np
import pytest
import imagejxx as ij
from test_imagej_annotation_export import fixture as imported_fixture,SEL,DIS,geometric_projection
from test_imagej_annotation_import import owner,EXTENT,SOURCES

def fixture():
    doc,reads=owner()
    selection=ij.PlanarAnnotation('created geometry',ij.Selection('rectangle',[(1.250000000001,2.5),(3.750000000001,4.5)]),'xy',1,t=1,c=None,attributes={'opaque':2**64-1})
    text=ij.PlanarDisplayAnnotation('created text',ij.TextAnnotationPayload([1.250000000001,2.5,8,4],'Created λ😀',font_size=13.500000000001,color=0x803366cc),'xz',2,t=None,c=1,attributes={'opaque':[True,2**64-1]})
    image=ij.PlanarDisplayAnnotation('created image',ij.ImageAnnotationPayload([1.250000000001,2.500000000001,4.5,2.25],2,1,4,bytes([255,0,0,128,0,0,0,255]),opacity=77,zero_transparent=True),'yz',3,t=0,c=None,attributes={'opaque':{'future':'kept'}})
    doc.commit(ij.prepare_store_planar_annotation(doc.snapshot(),selection))
    for row in (text,image):doc.commit(ij.prepare_store_planar_display_annotation(doc.snapshot(),row))
    ptext=ij.ImageJTextRoi();ptext.name=text.name;ptext.bounds=text.payload.bounds;ptext.text=text.payload.text;ptext.font_name=text.payload.font_name;ptext.font_size=14;ptext.stroke_color=text.payload.color;ptext.t=text.t;ptext.c=text.c;ptext.z=text.coordinate;ptext.group=23;ptext.properties='chosen λ😀\n'
    pimage=ij.ImageJImageRoi();pimage.name=image.name;pimage.pixels=np.array([[[255,0,0],[0,0,0]]],dtype=np.uint8);pimage.origin=[1,3];pimage.opacity=77;pimage.zero_transparent=True;pimage.t=image.t;pimage.c=image.c;pimage.z=image.coordinate;pimage.group=29;pimage.properties='chosen image\n'
    items=[ij.ImageJAnnotationExportItem('geometry.roi',None,SEL,0,geometric_projection(selection),fresh_source=True),
        ij.ImageJAnnotationExportItem('text.roi',None,DIS,0,ptext,ij.ImageJDisplayProjectionOptions(round_font_size=True),fresh_source=True),
        ij.ImageJAnnotationExportItem('image.roi',None,DIS,1,pimage,ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True,round_image_origin=True),fresh_source=True)]
    return doc,reads,(selection,text,image),items

@pytest.mark.parametrize('compressed',[False,True])
def test_fresh_zip_complete_pure_v2_equation_and_exact_generated_bases(compressed):
    doc,reads,rows,items=fixture();before=doc.snapshot();history=doc.undo_steps
    assert ij.inspect_imagej_annotation_sources(before)==[]
    result=ij.resolve_imagej_annotation_zip_export(before,items,compress=compressed);record=result.record()
    assert record['version']==2 and record['sources'] is None and [m['source_kind'] for m in record['members']]==['generated']*3
    assert all(m['source_index'] is None for m in record['members'])
    assert all(item.fresh_source and item.source_index is None for item in items)
    with zipfile.ZipFile(io.BytesIO(result.data)) as archive:
        assert archive.testzip() is None and archive.namelist()==[i.name for i in items]
        for n,(member,item,row) in enumerate(zip(result.members,items,rows)):
            assert archive.read(item.name)==member.data and member.request.fresh_source and member.request.source_index is None
            if n==0:
                mapped=ij.decode_imagej_roi_native_envelope(member.data)
                assert ij.encode_imagej_roi(mapped.source_record)==base64.b64decode(record['members'][n]['projection'])
                assert mapped.annotation.selection.points==row.selection.points
            else:
                full=ij.decode_imagej_display_roi_envelope_full(member.data);mapped=full.mapped
                assert mapped.source_bytes==base64.b64decode(record['members'][n]['projection'])
                assert mapped.annotation.payload.bounds==row.payload.bounds
                assert mapped.source_mapping.plane.extent==EXTENT and mapped.source_mapping.plane.plane==row.plane
                assert mapped.source_record.group==(23 if n==1 else 29)
                if n==1:assert mapped.annotation.payload.font_size==13.500000000001 and mapped.source_record.font_size==14
                else:assert mapped.annotation.payload.pixels==row.payload.pixels and mapped.source_record.pixels.tolist()==[[[255,0,0],[0,0,0]]]
            assert mapped.annotation.attributes==row.attributes and mapped.annotation.coordinate==row.coordinate
    ij.validate_imagej_annotation_export_record(record)
    request=ij.decode_imagej_annotation_export_request(record)
    assert all(i.fresh_source and i.source_index is None for i in request.items)
    assert ij.resolve_imagej_annotation_zip_export(before,request.items,request.limits,request.compress).data==result.data
    assert ij.imagej_annotation_export_from_record(before,record).data==result.data
    assert doc.snapshot().shares_data(before) and doc.undo_steps==history and SOURCES not in before.metadata['extensions']
    assert reads.counts().plane_reads==reads.counts().region_reads==0

@pytest.mark.parametrize('which',[0,1,2])
def test_fresh_single_preserves_native_payload_attributes_and_original_descriptor(which):
    doc,_,rows,items=fixture();r=ij.resolve_imagej_annotation_export(doc.snapshot(),items[which])
    assert r.record()['version']==2 and not r.is_zip and r.members[0].mapped.annotation.attributes==rows[which].attributes
    assert r.members[0].request.fresh_source and ij.imagej_annotation_export_from_record(doc.snapshot(),r.record()).data==r.data

def test_mixed_archive_and_generated_members_share_one_original_graph():
    doc,reads,archive=imported_fixture()
    # Use the imported document's current text, with a new explicit generated
    # basis; archive-backed members retain their pre-existing original bases.
    p=archive[1].standard_projection
    fresh=ij.ImageJAnnotationExportItem('generated.roi',None,DIS,0,p,archive[1].options,fresh_source=True)
    r=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),[archive[0],fresh,archive[2]])
    assert r.record()['version']==2 and [m['source_kind'] for m in r.record()['members']]==['archive','generated','archive']
    assert [m['source_index'] for m in r.record()['members']]==[archive[0].source_index,None,archive[2].source_index]
    assert r.members[1].mapped.source_bytes==base64.b64decode(r.record()['members'][1]['projection'])
    assert r.members[2].mapped.source_bytes==ij.resolve_imagej_annotation_export(doc.snapshot(),archive[2]).members[0].mapped.source_bytes
    ij.validate_imagej_annotation_export_record(r.record());assert reads.counts().plane_reads==0

@pytest.mark.parametrize('field,value',[('source_kind','unknown'),('source_kind',True),('source_index',0),('source_index',True),('projection','AAAA'),('annotation_index',999),('annotation_kind','selection')])
def test_v2_generated_fields_and_output_equation_refuse_tampering(field,value):
    doc,_,_,items=fixture();record=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),items).record()
    record['members'][1][field]=value
    with pytest.raises((ValueError,TypeError)):ij.validate_imagej_annotation_export_record(record)

@pytest.mark.parametrize('case',['v1-generated','v2-without-generated','v2-missing-kind','extra','false-archive','output'])
def test_version_closed_grammar_and_canonical_source_choice(case):
    doc,_,_,items=fixture();record=ij.resolve_imagej_annotation_zip_export(doc.snapshot(),items).record()
    if case=='v1-generated':record['version']=1
    elif case=='v2-without-generated':
        imported,_,old=imported_fixture();record=ij.resolve_imagej_annotation_zip_export(imported.snapshot(),old).record();record['version']=2
        for member in record['members']:member['source_kind']='archive'
    elif case=='v2-missing-kind':record['members'][1].pop('source_kind')
    elif case=='extra':record['members'][0]['extra']=0
    elif case=='false-archive':record['members'][1]['source_kind']='archive'
    else:record['output']='AAAA'
    with pytest.raises((ValueError,TypeError)):ij.validate_imagej_annotation_export_record(record)

@pytest.mark.parametrize('options',[ij.ImageJDisplayProjectionOptions(),ij.ImageJDisplayProjectionOptions(drop_image_alpha=True),ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True)])
def test_generated_basis_does_not_hide_image_projection_losses(options):
    doc,_,_,items=fixture();p=items[2].standard_projection;i=ij.ImageJAnnotationExportItem('loss.roi',None,DIS,1,p,options,fresh_source=True)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_export(doc.snapshot(),i)

def test_generated_font_rounding_and_color_zero_are_explicit():
    doc,_,_,items=fixture();p=items[1].standard_projection
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_export(doc.snapshot(),ij.ImageJAnnotationExportItem('text.roi',None,DIS,0,p,fresh_source=True))
    p.stroke_color=0
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_export(doc.snapshot(),ij.ImageJAnnotationExportItem('text.roi',None,DIS,0,p,items[1].options,fresh_source=True))
    options=ij.ImageJDisplayProjectionOptions(round_font_size=True,text_default_color=0x803366cc)
    r=ij.resolve_imagej_annotation_export(doc.snapshot(),ij.ImageJAnnotationExportItem('text.roi',None,DIS,0,p,options,fresh_source=True))
    assert r.members[0].mapped.source_mapping.text_default_color==0x803366cc
    ij.validate_imagej_annotation_export_record(r.record())

@pytest.mark.parametrize('fresh',[1,'true',None])
def test_fresh_choice_requires_real_bool(fresh):
    doc,_,_,items=fixture()
    with pytest.raises(TypeError):ij.ImageJAnnotationExportItem('a.roi',None,DIS,0,items[1].standard_projection,fresh_source=fresh)

def test_no_implicit_fresh_source_or_conflicting_archive_index():
    _,_,_,items=fixture()
    with pytest.raises(TypeError):ij.ImageJAnnotationExportItem('a.roi',None,DIS,0,items[1].standard_projection)
    with pytest.raises(ValueError):ij.ImageJAnnotationExportItem('a.roi',0,DIS,0,items[1].standard_projection,fresh_source=True)

def test_full_history_admission_precedes_generated_choice_and_rebind():
    doc,_,items=imported_fixture();fresh=ij.ImageJAnnotationExportItem('fresh.roi',None,DIS,0,items[1].standard_projection,items[1].options,fresh_source=True)
    record=ij.resolve_imagej_annotation_export(doc.snapshot(),fresh).record();bad=copy.deepcopy(record);bad['sources']['entries'][0]['source']['data']='AA=='
    class Dangerous:
        def __len__(self):raise AssertionError('new choices visited first')
        def __getitem__(self,index):raise AssertionError('new choices visited first')
    with pytest.raises(ValueError):ij.rebind_imagej_annotation_zip_export(doc.snapshot(),bad,Dangerous())
    archive=copy.deepcopy(record['sources']);archive['entries'][0]['source']['data']='AA=='
    tx=doc.begin_edit();tx.set_extension(SOURCES,archive);doc.commit(tx)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_export(doc.snapshot(),fresh)

def test_generated_replay_owner_revision_and_explicit_rebind():
    doc,_,_,items=fixture();before=doc.snapshot();r=ij.resolve_imagej_annotation_export(before,items[1]);other,_,_,_=fixture()
    with pytest.raises(ValueError):ij.imagej_annotation_export_from_record(other.snapshot(),r.record())
    tx=doc.begin_edit();tx.set_title('new title');doc.commit(tx)
    with pytest.raises(ij.RevisionConflict):ij.imagej_annotation_export_from_record(doc.snapshot(),r.record())
    rebound=ij.rebind_imagej_annotation_export(doc.snapshot(),r.record(),items[2])
    assert rebound.members[0].request.fresh_source and rebound.record()['source']['shape']==EXTENT

@pytest.mark.parametrize('case',['row','kind','position','text'])
def test_new_basis_still_requires_exact_native_projection_agreement(case):
    doc,_,_,items=fixture();p=items[1].standard_projection;row=0;kind=DIS
    if case=='row':row=999
    elif case=='kind':kind=SEL
    elif case=='position':p.c=0
    else:p.text='different native text'
    item=ij.ImageJAnnotationExportItem('bad.roi',None,kind,row,p,items[1].options,fresh_source=True)
    with pytest.raises(ValueError):ij.resolve_imagej_annotation_export(doc.snapshot(),item)
