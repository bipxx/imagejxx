"""Exact originals/native rows and explicit standard projection losses."""
import base64
import gc
import json
import math
import weakref
import numpy as np
import pytest
import imagejxx as ij

EXTENT=[2,3,6,8,12,1]

def mapping(plane='xz',coordinate=0,fallback=None):
    p=ij.ImageJRoiPlaneMapping();p.extent=EXTENT;p.plane=plane;p.coordinate=coordinate;p.z_position=ij.ImageJZPositionMapping.FIXED_AXIS
    return ij.ImageJDisplayRoiMapping(p,fallback)

def text_record():
    p=ij.ImageJTextRoi();p.name='source λ😀';p.text='First λ😀\nSecond';p.font_name='SansSerif';p.bounds=[1.25,2.5,8,4]
    p.font_size=14;p.stroke_color=0xff112233;p.fill_color=0x20445566;p.group=7;p.properties='source: λ😀\n';p.t=0;p.c=2;p.z=0
    return p

def image_record():
    p=ij.ImageJImageRoi();p.name='source image';p.pixels=np.array([[[1,2,3],[4,5,6]]],dtype=np.uint8)
    p.origin=[1,2];p.z=0;p.t=0;p.c=2;p.opacity=123;p.zero_transparent=True;p.group=7;p.properties='source: λ😀\n';return p

def source(kind='text',plane='xz'):
    p=text_record() if kind=='text' else image_record();wire=(ij.encode_imagej_text_roi if kind=='text' else ij.encode_imagej_image_roi)(p)
    return ij.decode_imagej_display_roi_mapped(wire,mapping(plane)),wire

def projection(mapped):
    row=mapped.annotation;p=mapped.source_record
    p.name=row.name;p.t=row.t;p.c=row.c;p.z=row.coordinate;p.legacy_position=0;p.group=29;p.properties='projection: λ😀\n'
    native=row.payload
    if isinstance(native,ij.TextAnnotationPayload):
        if not isinstance(p,ij.ImageJTextRoi):p=ij.ImageJTextRoi();p.name=row.name;p.t=row.t;p.c=row.c;p.z=row.coordinate
        p.bounds=native.bounds;p.text=native.text;p.font_name=native.font_name;p.font_size=math.floor(native.font_size+.5)
        p.angle=native.angle;p.font_style=native.font_style;p.justification=native.justification;p.draw_string_mode=native.draw_string_mode
        p.stroke_color=native.color;p.fill_color=native.background or 0
    else:
        if not isinstance(p,ij.ImageJImageRoi):p=ij.ImageJImageRoi();p.name=row.name;p.t=row.t;p.c=row.c;p.z=row.coordinate
        pixels=np.frombuffer(native.pixels,dtype=np.uint8).reshape(native.height,native.width,native.channels)
        p.pixels=pixels[:,:,:3].copy();p.serialized_image=b''
        p.origin=[math.floor(n+.5) if n>=0 else math.ceil(n-.5) for n in native.bounds[:2]]
        p.opacity=native.opacity;p.zero_transparent=native.zero_transparent
    return p

def edited_text():
    mapped,original=source();p=ij.TextAnnotationPayload([1.250000000001,2.5,8,4],'edited λ😀',font_size=13.500000000001,
        angle=23.500000000001,font_style=3,justification=2,draw_string_mode=True,color=0x80223344,background=0x20445566)
    row=ij.PlanarDisplayAnnotation('native λ😀##',p,'yz',3,t=None,c=1,attributes={'opaque':[None,True,2**64-1,{'future':'kept'}]})
    return ij.rebind_imagej_display_roi_mapping(mapped,EXTENT,row),original

def edited_image():
    mapped,original=source('image');p=ij.ImageAnnotationPayload([1.250000000001,2.500000000001,4.5,2.25],2,1,4,bytes([255,0,0,128,0,0,0,255]),opacity=77,zero_transparent=True)
    row=ij.PlanarDisplayAnnotation('native image',p,'yz',3,t=0,c=None,attributes={'opaque':{'future':[None,2**64-1]}})
    return ij.rebind_imagej_display_roi_mapping(mapped,EXTENT,row),original

@pytest.mark.parametrize('kind',['text','image'])
@pytest.mark.parametrize('plane',['xy','xz','yz'])
def test_bare_typed_mapping_all_planes_exact_originals(kind,plane):
    mapped,original=source(kind,plane);row=mapped.annotation
    assert mapped.source_bytes==original and mapped.target_extent==EXTENT and (row.plane,row.coordinate,row.t,row.c)==(plane,0,0,2)
    ij.validate_imagej_display_roi_mapping(mapped)
    if kind=='text':assert row.payload.text=='First λ😀\nSecond' and row.payload.font_size==14 and row.payload.color==0xff112233
    else:assert row.payload.pixels==bytes([1,2,3,4,5,6]) and row.payload.bounds==[1,2,2,1] and row.payload.opacity==123
    wire=ij.encode_imagej_display_roi_envelope(mapped,projection(mapped));decoded=ij.decode_imagej_display_roi_envelope_full(wire)
    assert decoded.mapped.source_bytes==original and decoded.mapped.annotation.payload.bounds==row.payload.bounds
    assert decoded.standard_projection.group==29 and decoded.standard_projection.properties=='projection: λ😀\n'
    assert ij.encode_imagej_display_roi_envelope(decoded.mapped,decoded.standard_projection,decoded.options)==wire

def test_native_text_precision_attributes_and_explicit_font_rounding():
    mapped,original=edited_text();p=projection(mapped)
    with pytest.raises(ValueError,match='Fractional font size'):ij.encode_imagej_display_roi_envelope(mapped,p)
    options=ij.ImageJDisplayProjectionOptions(round_font_size=True);wire=ij.encode_imagej_display_roi_envelope(mapped,p,options)
    decoded=ij.decode_imagej_display_roi_envelope_full(wire);row=decoded.mapped.annotation
    assert decoded.mapped.source_bytes==original and decoded.mapped.source_record.group==7 and decoded.standard_projection.group==29
    assert row.payload.font_size==13.500000000001 and row.payload.bounds==mapped.annotation.payload.bounds and row.payload.angle==23.500000000001
    assert row.attributes==mapped.annotation.attributes and row.plane=='yz' and row.coordinate==3 and row.t is None and row.c==1
    assert decoded.options.round_font_size and decoded.standard_projection.font_size==14
    assert ij.decode_imagej_text_roi(wire).properties.startswith('imagejxx.display-roi.v1: ')

@pytest.mark.parametrize('flags',[{},dict(drop_image_alpha=True),dict(drop_image_alpha=True,reset_image_scale=True)])
def test_each_image_projection_loss_requires_explicit_acknowledgment(flags):
    mapped,_=edited_image()
    with pytest.raises(ValueError):ij.encode_imagej_display_roi_envelope(mapped,projection(mapped),ij.ImageJDisplayProjectionOptions(**flags))

def test_native_rgba_scale_origin_and_exact_original_tiff_roundtrip():
    mapped,original=edited_image();options=ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True,round_image_origin=True)
    wire=ij.encode_imagej_display_roi_envelope(mapped,projection(mapped),options);decoded=ij.decode_imagej_display_roi_envelope_full(wire)
    assert decoded.mapped.source_bytes==original and decoded.mapped.source_record.serialized_image==ij.decode_imagej_image_roi(original).serialized_image
    assert decoded.mapped.annotation.payload.pixels==mapped.annotation.payload.pixels and decoded.mapped.annotation.payload.bounds==mapped.annotation.payload.bounds
    assert decoded.mapped.annotation.attributes==mapped.annotation.attributes and decoded.mapped.annotation.c is None and decoded.mapped.annotation.t==0
    p=decoded.standard_projection;assert p.origin==[1,3] and p.opacity==77 and p.zero_transparent
    np.testing.assert_array_equal(p.pixels,np.array([[[255,0,0],[0,0,0]]],dtype=np.uint8))
    assert decoded.options.drop_image_alpha and decoded.options.reset_image_scale and decoded.options.round_image_origin
    assert ij.encode_imagej_display_roi_envelope(decoded.mapped,p,decoded.options)==wire

def test_explicit_text_default_color_and_projection_interpretation():
    p=text_record();p.stroke_color=0;wire=ij.encode_imagej_text_roi(p)
    with pytest.raises(ValueError,match='explicit default color'):ij.decode_imagej_display_roi_mapped(wire,mapping())
    mapped=ij.decode_imagej_display_roi_mapped(wire,mapping(fallback=0xff334455));assert mapped.annotation.payload.color==0xff334455
    standard=mapped.source_record
    with pytest.raises(ValueError,match='exact explicit native default'):ij.encode_imagej_display_roi_envelope(mapped,standard)
    options=ij.ImageJDisplayProjectionOptions(text_default_color=0xff334455)
    output=ij.encode_imagej_display_roi_envelope(mapped,standard,options);back=ij.decode_imagej_display_roi_envelope_full(output)
    assert back.mapped.source_mapping.text_default_color==0xff334455 and back.options.text_default_color==0xff334455
    assert back.standard_projection.stroke_color==0 and back.mapped.annotation.payload.color==0xff334455

@pytest.mark.parametrize('case',['missing-fixed','scientific-z','legacy-reject','conflict-fixed','conflict-t','outside-c','outside-fixed'])
def test_shared_position_policies_refuse_ambiguity(case):
    p=text_record();plane=mapping().plane
    if case=='missing-fixed':p.z=None;plane.coordinate=None
    elif case=='scientific-z':plane.z_position=ij.ImageJZPositionMapping.SCIENTIFIC_Z
    elif case=='legacy-reject':p.legacy_position=1
    elif case=='conflict-fixed':plane.coordinate=1
    elif case=='conflict-t':plane.plane='xy';p.c=p.z=None;p.legacy_position=19;plane.legacy_position=ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT
    elif case=='outside-c':p.c=3
    else:plane.coordinate=8;p.z=None
    with pytest.raises(ValueError):ij.decode_imagej_display_roi_mapped(ij.encode_imagej_text_roi(p),ij.ImageJDisplayRoiMapping(plane))

def test_explicit_legacy_hyperstack_position_and_fixed_axis_zero():
    p=text_record();p.z=p.t=p.c=None;p.legacy_position=4
    plane=mapping('xy',None).plane;plane.legacy_position=ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT
    mapped=ij.decode_imagej_display_roi_mapped(ij.encode_imagej_text_roi(p),ij.ImageJDisplayRoiMapping(plane))
    assert (mapped.annotation.t,mapped.annotation.c,mapped.annotation.coordinate)==(0,0,1)

@pytest.mark.parametrize('field',['text','name','font_name','font_size','angle','stroke_color','fill_color','t','c','z','bounds'])
def test_projection_field_mismatches_refuse(field):
    mapped,_=source();p=projection(mapped)
    if field in ('text','name','font_name'):setattr(p,field,'different')
    elif field=='bounds':p.bounds=[2,2.5,8,4]
    elif field in ('t','c','z'):setattr(p,field,1)
    else:setattr(p,field,getattr(p,field)+1)
    with pytest.raises(ValueError):ij.encode_imagej_display_roi_envelope(mapped,p)

@pytest.mark.parametrize('case',['wrong-kind','legacy','retained-tiff','pixels','opacity','zero'])
def test_image_projection_mismatches_and_retained_tiff_refuse(case):
    mapped,_=source('image');p=projection(mapped)
    if case=='wrong-kind':p=text_record()
    elif case=='legacy':p.legacy_position=1
    elif case=='retained-tiff':
        p.pixels=np.zeros((1,2,3),dtype=np.uint8);p.serialized_image=mapped.source_record.serialized_image
    elif case=='pixels':p.pixels=np.zeros((1,2,3),dtype=np.uint8)
    elif case=='opacity':p.opacity=124
    else:p.zero_transparent=False
    with pytest.raises(ValueError):ij.encode_imagej_display_roi_envelope(mapped,p)

def payload(wire):
    p=ij.decode_imagej_text_roi(wire);return p,bytearray(base64.b64decode(p.properties.split(': ',1)[1]))

def rewritten(wire,mutate):
    p,b=payload(wire);mutate(b);p.properties='imagejxx.display-roi.v1: '+base64.b64encode(b).decode()+'\n';return ij.encode_imagej_text_roi(p)

@pytest.mark.parametrize('offset,value',[(3,2),(7,7),(35,7),(43,7),(47,7),(51,7)])
def test_envelope_version_kind_plane_and_position_policy_tampering(offset,value):
    mapped,_=source();wire=ij.encode_imagej_display_roi_envelope(mapped,projection(mapped))
    altered=rewritten(wire,lambda b:b.__setitem__(offset,value))
    with pytest.raises(ValueError):ij.decode_imagej_display_roi_envelope(altered)

@pytest.mark.parametrize('case',['trailing','native','projection','original','outer','noncanonical-json','duplicate-key'])
def test_closed_original_native_projection_and_outer_equation(case):
    mapped,_=source();wire=ij.encode_imagej_display_roi_envelope(mapped,projection(mapped));p,b=payload(wire)
    original_size=int.from_bytes(b[56:60],'big');native_length_at=60+original_size+24+12
    n=int.from_bytes(b[native_length_at:native_length_at+4],'big');a=native_length_at+4
    if case=='trailing':b.extend(b'junk')
    elif case=='original':b[60]=0
    elif case=='native':
        native=json.loads(b[a:a+n]);native['rows'][0]['name']='forged';new=json.dumps(native,separators=(',',':'),sort_keys=True,ensure_ascii=False).encode();b[native_length_at:a+n]=len(new).to_bytes(4,'big')+new
    elif case=='projection':b[a+n+4]=0
    elif case=='noncanonical-json':
        new=b' '+b[a:a+n];b[native_length_at:a+n]=len(new).to_bytes(4,'big')+new
    elif case=='duplicate-key':
        new=bytes(b[a:a+n]).replace(b'"version":1',b'"version":1,"version":1');b[native_length_at:a+n]=len(new).to_bytes(4,'big')+new
    else:p.group=30
    p.properties='imagejxx.display-roi.v1: '+base64.b64encode(b).decode()+'\n'
    with pytest.raises(ValueError):ij.decode_imagej_display_roi_envelope(ij.encode_imagej_text_roi(p))

@pytest.mark.parametrize('value',[True,-1,0,4095,256*1024*1024+1,1.0])
def test_retention_limit_types_and_hard_bounds(value):
    with pytest.raises((ValueError,TypeError,OverflowError)):ij.ImageJDisplayRoiLimits(max_retained_bytes=value)

@pytest.mark.parametrize('field',['round_font_size','drop_image_alpha','reset_image_scale','round_image_origin'])
def test_projection_flags_require_bool(field):
    with pytest.raises(TypeError):ij.ImageJDisplayProjectionOptions(**{field:1})

@pytest.mark.parametrize('case',['bytes','text','retained','image-pixels'])
def test_mapping_resource_refusals(case):
    mapped,wire=source('image' if case=='image-pixels' else 'text')
    if case=='bytes':limits=ij.ImageJDisplayRoiLimits(wire=ij.ImageJRoiLimits(max_bytes=64))
    elif case=='text':limits=ij.ImageJDisplayRoiLimits(wire=ij.ImageJRoiLimits(max_text_bytes=1))
    elif case=='retained':limits=ij.ImageJDisplayRoiLimits(max_retained_bytes=4096)
    else:limits=ij.ImageJDisplayRoiLimits(wire=ij.ImageJRoiLimits(max_image_pixels=1))
    with pytest.raises(ValueError):ij.decode_imagej_display_roi_mapped(wire,mapped.source_mapping,limits)

def test_envelope_resource_caps_and_original_before_new_choices():
    mapped,_=source();p=projection(mapped)
    with pytest.raises(ValueError):ij.encode_imagej_display_roi_envelope(mapped,p,limits=ij.ImageJDisplayRoiLimits(wire=ij.ImageJRoiLimits(max_text_bytes=100)))
    class Dangerous:
        def __len__(self):raise AssertionError('new target visited before original admission')
        def __getitem__(self,i):raise AssertionError('new target visited before original admission')
    limits=ij.ImageJDisplayRoiLimits(max_retained_bytes=4096)
    with pytest.raises(ValueError):ij.rebind_imagej_display_roi_mapping(mapped,Dangerous(),mapped.annotation,limits)
    with pytest.raises(ValueError):ij.encode_imagej_display_roi_envelope(mapped,object(),limits=limits)

def test_result_ownership_full_decode_lifetime_and_native_rebind():
    mapped,wire=source();old=mapped.source_record;old.text='mutated';assert mapped.source_record.text!='mutated'
    plane=mapped.source_mapping.plane;plane.coordinate=4;assert mapped.source_mapping.plane.coordinate==0
    output=ij.encode_imagej_display_roi_envelope(mapped,projection(mapped));full=ij.decode_imagej_display_roi_envelope_full(output)
    ref=weakref.ref(full);view=full.mapped;del full;gc.collect();assert ref() is not None and view.source_bytes==wire
    del view;gc.collect();assert ref() is None
    row=ij.PlanarDisplayAnnotation('bigger',mapped.annotation.payload,'xy',7,t=3,c=4)
    rebound=ij.rebind_imagej_display_roi_mapping(mapped,[4,5,8,10,14,3],row)
    assert rebound.target_extent==[4,5,8,10,14,3] and rebound.source_mapping.plane.extent==EXTENT and rebound.source_bytes==wire

def test_source_and_projection_reserved_envelopes_require_explicit_decoder():
    mapped,_=source();p=projection(mapped);wire=ij.encode_imagej_display_roi_envelope(mapped,p)
    with pytest.raises(ValueError,match='explicit decoder'):ij.decode_imagej_display_roi_mapped(wire,mapping())
    p.properties='imagejxx.display-roi.v9: nope\n'
    with pytest.raises(ValueError,match='explicit decoder'):ij.encode_imagej_display_roi_envelope(mapped,p)

def test_cross_kind_rebind_requires_correct_projection_and_retains_text_basis():
    text,original=source();image,_=edited_image();mapped=ij.rebind_imagej_display_roi_mapping(text,EXTENT,image.annotation)
    p=projection(mapped);options=ij.ImageJDisplayProjectionOptions(drop_image_alpha=True,reset_image_scale=True,round_image_origin=True)
    wire=ij.encode_imagej_display_roi_envelope(mapped,p,options);back=ij.decode_imagej_display_roi_envelope_full(wire)
    assert isinstance(back.mapped.source_record,ij.ImageJTextRoi) and back.mapped.source_bytes==original
    assert isinstance(back.standard_projection,ij.ImageJImageRoi) and back.mapped.annotation.payload.pixels==image.annotation.payload.pixels

def test_mapped_text_and_image_integrate_with_native_document_without_processing_selection():
    reads=ij.DatasetReadObserver();data=ij.Dataset(np.zeros(EXTENT[:-1],dtype=np.uint16),axes='TCZYX').observe_reads(reads)
    doc=ij.Document(data);before=doc.snapshot();originals=[]
    for kind in ('text','image'):
        mapped,original=source(kind);originals.append(original)
        doc.commit(ij.prepare_store_planar_display_annotation(doc.snapshot(),mapped.annotation))
        assert mapped.source_bytes==original
    saved=ij.planar_display_annotations(doc.snapshot())
    assert len(saved)==2 and isinstance(saved[0].payload,ij.TextAnnotationPayload) and isinstance(saved[1].payload,ij.ImageAnnotationPayload)
    assert not doc.snapshot().rois and doc.snapshot().selection is None and ij.planar_annotations(doc.snapshot())==[]
    assert doc.snapshot().dataset.shares_pixels(before.dataset) and reads.counts().plane_reads==reads.counts().region_reads==0
    doc.undo();doc.undo();assert ij.planar_display_annotations(doc.snapshot())==[]
