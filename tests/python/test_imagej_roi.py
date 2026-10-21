"""Independent binary-layout expectations, not a self-roundtrip-only oracle."""
import struct
import json
import math
from pathlib import Path
import pytest
import imagejxx as ij

def record(kind='rectangle',points=((1.25,2.5),(6.75,7.5))):
    return ij.ImageJRoi(ij.ROI('ROI λ😀',ij.Selection(kind,points),t=0,c=1,z=2))

@pytest.mark.parametrize('kind,points',[
    ('rectangle',[(1.25,2.5),(6.75,7.5)]),('ellipse',[(-2.5,1.25),(8.5,4.75)]),
    ('line',[(1.25,2.5),(6.75,7.5)]),('polygon',[(1.25,1),(6,1),(3,5)]),
    ('freehand',[(1,1),(6,1),(3,5)]),('polyline',[(1,1),(6,1),(3,5)]),
    ('angle',[(1,1),(6,1),(3,5)]),('point',[(1.25,2.5)]),('multipoint',[(1,1),(6,5)])])
def test_wire_layout_and_metadata(kind,points):
    value=record(kind,points);value.properties='k: μ😀\n';value.group=7
    wire=ij.encode_imagej_roi(value)
    assert wire[:4]==b'Iout' and struct.unpack_from('>H',wire,4)[0]==228
    h2=struct.unpack_from('>I',wire,60)[0]
    assert struct.unpack_from('>III',wire,h2+4)==(2,3,1)
    off,count=struct.unpack_from('>II',wire,h2+16)
    assert wire[off:off+2*count].decode('utf-16-be')=='ROI λ😀'
    restored=ij.decode_imagej_roi(wire)
    assert restored.roi.name=='ROI λ😀' and restored.properties==value.properties and restored.group==7
    assert (restored.roi.t,restored.roi.c,restored.roi.z)==(0,1,2)
    assert restored.roi.selection.points==[list(p) for p in points]
    assert restored.roi.selection.kind==kind
    assert ij.encode_imagej_roi(restored)==wire

def test_legacy_position_is_not_guessed_as_scientific_z():
    wire=bytearray(64);wire[:4]=b'Iout';struct.pack_into('>H',wire,4,217);wire[6]=1
    struct.pack_into('>hhhh',wire,8,-3,-2,5,7);struct.pack_into('>I',wire,56,9)
    value=ij.decode_imagej_roi(bytes(wire))
    assert value.legacy_position==9 and value.roi.z is None
    assert value.roi.selection.points==[[-2,-3],[7,5]]

@pytest.mark.parametrize('offset,fmt,value',[(0,'>I',0),(4,'>H',229),(6,'>B',6),(36,'>I',1),(48,'>H',1),(50,'>H',1),(54,'>H',2),(60,'>I',8),(56,'>I',0xffffffff),(18,'>I',0x7f800000)])
def test_malformed_or_unmodeled_records_are_explicit(offset,fmt,value):
    wire=bytearray(ij.encode_imagej_roi(record()));struct.pack_into(fmt,wire,offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_roi(bytes(wire))

def test_all_truncations_and_text_alias_refuse():
    wire=ij.encode_imagej_roi(record());h2=struct.unpack_from('>I',wire,60)[0]
    for end in range(len(wire)):
        with pytest.raises(ValueError):ij.decode_imagej_roi(wire[:end])
    changed=bytearray(wire);struct.pack_into('>I',changed,h2+16,18)
    with pytest.raises(ValueError,match='Overlapping'):ij.decode_imagej_roi(bytes(changed))

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_bytes=64),ij.ImageJRoiLimits(max_text_bytes=2),ij.ImageJRoiLimits(max_vertices=1)])
def test_declared_limits(limits):
    value=record('polygon',[(1,1),(2,1),(1,2)])
    with pytest.raises(ValueError):ij.encode_imagej_roi(value,limits)
    with pytest.raises(ValueError):ij.decode_imagej_roi(ij.encode_imagej_roi(value),limits)

def test_opaque_attributes_and_composite_masks_are_not_silently_lost():
    value=ij.ImageJRoi(ij.ROI('opaque',ij.Selection('rectangle',[(1,1),(2,2)],attributes={'future':True})))
    with pytest.raises(ValueError,match='explicit interchange mapping'):ij.encode_imagej_roi(value)

@pytest.mark.parametrize('bad',[True,1.5,-1,2**100])
def test_strict_limit_types(bad):
    with pytest.raises((TypeError,ValueError,OverflowError)):ij.ImageJRoiLimits(max_bytes=bad)


REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',REFERENCE['cases'],ids=lambda c:c['id'])
def test_actual_pinned_imagej_encoder_records(case):
    wire=bytes.fromhex(case['hex']);decoded=ij.decode_imagej_roi(wire)
    assert decoded.roi.name==case['name'] and decoded.group==case['group']
    assert (decoded.roi.t,decoded.roi.c,decoded.roi.z)==(case['t'],case['c'],case['z'])
    assert decoded.roi.selection.kind==case['kind'] and decoded.roi.selection.points==case['points']
    style=decoded.roi.selection.attributes['imagejxx.roi_style']
    for key in ('stroke_color','fill_color','stroke_width'):assert style[key]==case[key]
    assert decoded.properties==case['observed_raw_properties']
    again=ij.decode_imagej_roi(ij.encode_imagej_roi(decoded))
    assert again.roi.selection.points==case['points'] and again.properties==case['observed_raw_properties']


def test_legacy_scaling_and_signed_stroke_follow_wire_version():
    wire=bytearray(64);wire[:4]=b'Iout';struct.pack_into('>H',wire,4,227);wire[6]=1
    struct.pack_into('>hhhh',wire,8,1,2,5,7);struct.pack_into('>H',wire,34,65535)
    value=ij.decode_imagej_roi(bytes(wire))
    assert value.options & 8192 and value.roi.selection.attributes['imagejxx.roi_style']['stroke_width']==0
    struct.pack_into('>H',wire,4,228)
    assert not ij.decode_imagej_roi(bytes(wire)).options & 8192

def test_unknown_trailing_data_and_unpaired_surrogates_refuse():
    wire=ij.encode_imagej_roi(record())
    with pytest.raises(ValueError,match='trailing'):ij.decode_imagej_roi(wire+b'future extension')
    changed=bytearray(wire);h2=struct.unpack_from('>I',changed,60)[0];offset,count=struct.unpack_from('>II',changed,h2+16)
    struct.pack_into('>H',changed,offset,0xdc00)
    with pytest.raises(ValueError,match='surrogate'):ij.decode_imagej_roi(bytes(changed))


@pytest.mark.parametrize('kind,code',[('polyline',4),('freehand',8)])
def test_freeline_and_traced_wire_identity_survives(kind,code):
    wire=bytearray(ij.encode_imagej_roi(record(kind,[(1,1),(4,2),(6,5)])));wire[6]=code
    value=ij.decode_imagej_roi(bytes(wire));assert value.source_type==code
    assert ij.encode_imagej_roi(value)[6]==code

def test_point_counters_are_bounded_and_preserved():
    value=record('multipoint',[(1,1),(4,5)]);value.point_counters=[0x01000003,0xff000007]
    wire=ij.encode_imagej_roi(value);decoded=ij.decode_imagej_roi(wire)
    assert decoded.point_counters==[0x01000003,0xff000007]
    value.point_counters=[1]
    with pytest.raises(ValueError,match='counters'):ij.encode_imagej_roi(value)
    changed=bytearray(wire);h2=struct.unpack_from('>I',changed,60)[0]
    struct.pack_into('>I',changed,h2+48,h2)
    with pytest.raises(ValueError,match='Overlapping'):ij.decode_imagej_roi(bytes(changed))

def test_opaque_roi_metadata_requires_mapping():
    value=ij.ImageJRoi(ij.ROI('opaque',ij.Selection('rectangle',[(1,1),(2,2)]),attributes={'future':'kept'}))
    with pytest.raises(ValueError,match='explicit interchange mapping'):ij.encode_imagej_roi(value)

def test_point_processing_width_cannot_be_silently_lost():
    value=ij.ImageJRoi(ij.ROI('wide point',ij.Selection('point',[(2,3)],width=3)))
    with pytest.raises(ValueError,match='processing width'):ij.encode_imagej_roi(value)


@pytest.mark.parametrize('kind',['rotated_rectangle','rotated_ellipse'])
@pytest.mark.parametrize('angle',[-135,-30,0,30,90,179])
@pytest.mark.parametrize('size',[(6,4),(4,6)])
def test_rotated_wire_geometry_has_native_shape_and_canonical_axes(kind,angle,size):
    a=math.radians(angle);c,s=math.cos(a),math.sin(a);u=[size[0]*c/2,size[0]*s/2];v=[-size[1]*s/2,size[1]*c/2]
    points=[[6,4],[6+u[0],4+u[1]],[6+v[0],4+v[1]]]
    wire=ij.encode_imagej_roi(record(kind,points))
    assert wire[6]==7 and struct.unpack_from('>H',wire,48)[0]==(5 if kind=='rotated_rectangle' else 3)
    assert struct.unpack_from('>H',wire,16)[0]==(4 if kind=='rotated_rectangle' else 72)
    result=ij.decode_imagej_roi(wire)
    assert result.roi.selection.kind==kind and result.ignored_rotated_parameters is None
    center,p,q=result.roi.selection.points;du=[p[i]-center[i] for i in range(2)];dv=[q[i]-center[i] for i in range(2)]
    assert center==pytest.approx([6,4],abs=1e-6)
    # Geometry is invariant under equivalent axis/sign choices, including
    # canonicalizing an ellipse's longer axis to ImageJ's major-axis parameter.
    for i in range(2):
        for j in range(2):assert du[i]*du[j]+dv[i]*dv[j]==pytest.approx(u[i]*u[j]+v[i]*v[j],rel=1e-5,abs=1e-6)
    assert (result.roi.t,result.roi.c,result.roi.z)==(0,1,2)
    again=ij.decode_imagej_roi(ij.encode_imagej_roi(result))
    for actual,expected in zip(again.roi.selection.points,result.roi.selection.points):assert actual==pytest.approx(expected,abs=1e-6)

def test_tagged_polygon_keeps_ignored_parameters_without_promoting_geometry():
    value=record('polygon',[(1,1),(4,1),(1,6)]);value.source_subtype=5
    value.ignored_rotated_parameters=[100,200,300,400,5]
    wire=ij.encode_imagej_roi(value);result=ij.decode_imagej_roi(wire)
    assert result.roi.selection.kind=='polygon' and result.roi.selection.points==[[1,1],[4,1],[1,6]]
    assert result.source_subtype==5 and result.source_type==0
    assert result.ignored_rotated_parameters==[100,200,300,400,5]
    assert ij.encode_imagej_roi(result)==wire

@pytest.mark.parametrize('bad',[0,-1,float('inf'),float('nan')])
def test_invalid_active_rotated_width_refuses(bad):
    wire=bytearray(ij.encode_imagej_roi(record('rotated_rectangle',[(6,4),(9,4),(6,6)])))
    struct.pack_into('>f',wire,52,bad)
    with pytest.raises(ValueError):ij.decode_imagej_roi(bytes(wire))

def test_rotated_generated_fallback_obeys_vertex_limit():
    with pytest.raises(ValueError,match='vertex limit'):
        ij.encode_imagej_roi(record('rotated_ellipse',[(6,4),(9,4),(6,6)]),ij.ImageJRoiLimits(max_vertices=4))

ROTATED_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-rotated-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',ROTATED_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_rotated_records(case):
    value=ij.decode_imagej_roi(bytes.fromhex(case['hex']))
    index=case['index']
    assert value.roi.selection.kind==('polygon' if index==0 else 'rotated_rectangle' if index in (1,3) else 'rotated_ellipse')
    assert (value.roi.t,value.roi.c,value.roi.z)==(0,1,2)
    assert value.roi.name=='rotated' and value.properties=='owned: rotated\n'
    assert value.roi.selection.attributes['imagejxx.roi_style']['stroke_width']==2.5
    if index==0:
        assert value.ignored_rotated_parameters is not None and len(value.roi.selection.points)==4
    else:
        center,p,q=value.roi.selection.points
        assert center==pytest.approx([6,4],abs=1e-5)
        # Independent declared input axis (6,4), full width4 or ratio0.5.
        u=[p[i]-center[i] for i in range(2)];v=[q[i]-center[i] for i in range(2)]
        assert sum(x*x for x in u)==pytest.approx(13,abs=1e-4)
        assert sum(x*x for x in v)==pytest.approx(4 if index in (1,3) else 3.25,abs=1e-4)


def rectangle_path(left,top,right,bottom):
    return [0,left,top,1,right,top,1,right,bottom,1,left,bottom,4]

@pytest.mark.parametrize('reverse',[False,True])
def test_composite_holes_islands_and_retained_path(reverse):
    path=rectangle_path(1,1,8,8)+rectangle_path(3,3,6,6)+rectangle_path(9,1,11,3)
    if reverse:
        path=rectangle_path(8,8,1,1)+rectangle_path(3,3,6,6)+rectangle_path(9,1,11,3)
    mask=ij.imagej_shape_mask(path,12,10)
    image=mask.mask((10,12))
    for y in range(10):
        for x in range(12):
            expected=(1<=x<8 and 1<=y<8 and not(3<=x<6 and 3<=y<6)) or (9<=x<11 and 1<=y<3)
            assert bool(image[y,x])==expected
    value=ij.ImageJRoi(ij.ROI('composite',mask,t=0,c=1,z=2));value.composite_path=path
    wire=ij.encode_imagej_roi(value)
    with pytest.raises(ValueError,match='canvas'):ij.decode_imagej_roi(wire)
    back=ij.decode_imagej_roi(wire,mask_width=12,mask_height=10)
    assert back.composite_path==path
    assert back.roi.selection.mask((10,12)).tobytes()==mask.mask((10,12)).tobytes()
    assert (back.roi.t,back.roi.c,back.roi.z)==(0,1,2)
    back.roi=ij.ROI('edited',ij.imagej_shape_mask(rectangle_path(1,1,2,2),12,10))
    with pytest.raises(ValueError,match='differs from mask'):ij.encode_imagej_roi(back)
    back.composite_path=[]
    edited=ij.decode_imagej_roi(ij.encode_imagej_roi(back),mask_width=12,mask_height=10)
    assert edited.roi.selection.mask((10,12)).tobytes()==back.roi.selection.mask((10,12)).tobytes()

@pytest.mark.parametrize('path',[[1,1,2],[0,1],[0,1,1,5],[0,1,1,1,float('nan'),2],[0,1,1,2,1,2,3],[0,1,1,3,1,2,3,4,5],[0,1,1,0.5,2,3]])
def test_bad_composite_grammar_refuses(path):
    with pytest.raises(ValueError):ij.imagej_shape_mask(path,12,10)

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_mask_pixels=10),ij.ImageJRoiLimits(max_vertices=2),ij.ImageJRoiLimits(max_path_work=10)])
def test_composite_resource_bounds(limits):
    with pytest.raises(ValueError):ij.imagej_shape_mask(rectangle_path(1,1,8,8),12,10,limits)

@pytest.mark.parametrize('path',[
    [0,1,1,2,8,1,8,8,1,1,8,4],
    [0,1,1,3,8,-2,10,10,1,8,4],
    rectangle_path(-4,-3,5,6),[0,1,1,1,8,1,1,4,8],
    [0,1,1,4]])
def test_curved_open_clipped_and_empty_masks_can_export(path):
    mask=ij.imagej_shape_mask(path,12,10)
    row=ij.ImageJRoi(ij.ROI('shape',mask));row.composite_path=path
    wire=ij.encode_imagej_roi(row)
    back=ij.decode_imagej_roi(wire,mask_width=12,mask_height=10)
    assert back.roi.selection.mask((10,12)).tobytes()==mask.mask((10,12)).tobytes()
    assert back.composite_path==path

COMPOSITE_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-composite-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',COMPOSITE_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_composite_path_and_mask(case):
    row=ij.decode_imagej_roi(bytes.fromhex(case['hex']),mask_width=12,mask_height=10)
    assert row.composite_path==case['path'] and row.roi.name=='composite'
    assert (row.roi.t,row.roi.c,row.roi.z)==(0,1,2)
    assert row.properties=='owned: composite\n'
    assert row.roi.selection.attributes['imagejxx.roi_style']['stroke_width']==2.5
    assert ''.join('1' if v else '0' for v in row.roi.selection.mask((10,12)).flat)==case['mask']

@pytest.mark.parametrize('offset,fmt,value',[(6,'>B',0),(16,'>H',1),(36,'>I',0xffffffff),(48,'>H',3),(60,'>I',66),(64,'>f',5),(68,'>f',float('inf'))])
def test_composite_header_and_payload_corruption_refuses(offset,fmt,value):
    wire=bytearray.fromhex(COMPOSITE_REFERENCE['cases'][0]['hex'])
    struct.pack_into(fmt,wire,offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_roi(bytes(wire),mask_width=12,mask_height=10)

def test_all_composite_truncations_refuse():
    wire=bytes.fromhex(COMPOSITE_REFERENCE['cases'][0]['hex'])
    for end in range(len(wire)):
        with pytest.raises(ValueError):ij.decode_imagej_roi(wire[:end],mask_width=12,mask_height=10)

def test_solid_mask_export_merges_adjacent_runs():
    mask=ij.imagej_shape_mask(rectangle_path(1,1,9,99),10,100)
    wire=ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI('merged',mask)),ij.ImageJRoiLimits(max_vertices=5))
    assert struct.unpack_from('>I',wire,36)[0]==13
    back=ij.decode_imagej_roi(wire,mask_width=10,mask_height=100)
    assert back.roi.selection.mask((100,10)).tobytes()==mask.mask((100,10)).tobytes()

@pytest.mark.parametrize('box',[[1,1,8,6,4],[1.25,1.75,8.5,6.5,4],[-1.25,-1.75,8.5,6.5,4],[1,1,8,6,40]])
def test_rounded_parameters_and_processing_mask_survive(box):
    mask=ij.imagej_rounded_rectangle_mask(box,12,10)
    row=ij.ImageJRoi(ij.ROI('rounded',mask,t=0,c=1,z=2));row.rounded_rectangle=box
    wire=ij.encode_imagej_roi(row)
    assert wire[6]==1 and struct.unpack_from('>H',wire,54)[0]==box[4]
    assert struct.unpack_from('>I',wire,36)[0]==0
    back=ij.decode_imagej_roi(wire,mask_width=12,mask_height=10)
    assert back.rounded_rectangle==box and back.composite_path==[]
    assert back.roi.selection.mask((10,12)).tobytes()==mask.mask((10,12)).tobytes()
    assert (back.roi.t,back.roi.c,back.roi.z)==(0,1,2)
    with pytest.raises(ValueError):ij.decode_imagej_roi(wire)
    row.roi=ij.ROI('edited',ij.imagej_shape_mask(rectangle_path(1,1,2,2),12,10))
    with pytest.raises(ValueError,match='differs from mask'):ij.encode_imagej_roi(row)
    row.rounded_rectangle=None
    assert ij.decode_imagej_roi(ij.encode_imagej_roi(row),mask_width=12,mask_height=10).roi.name=='edited'

@pytest.mark.parametrize('diameter',[0,-1,1.5,60536,float('inf'),float('nan')])
def test_invalid_rounded_diameter_refuses(diameter):
    with pytest.raises(ValueError):ij.imagej_rounded_rectangle_mask([1,1,8,6,diameter],12,10)

@pytest.mark.parametrize('flags',range(32))
def test_overlay_wire_flags_transport(flags):
    bits=[8,16,32,64,2048];options=8192+sum(bit for i,bit in enumerate(bits) if flags&(1<<i))
    row=record();row.options=options;row.overlay_label_color=0xff123456;row.overlay_font_size=14
    back=ij.decode_imagej_roi(ij.encode_imagej_roi(row))
    assert back.options&~128==options and back.overlay_label_color==0xff123456 and back.overlay_font_size==14

@pytest.mark.parametrize('flags',[0,1024,4096,5120])
def test_point_label_and_delete_options(flags):
    row=record('multipoint',[(1,1),(4,5)]);row.options=8192|flags
    back=ij.decode_imagej_roi(ij.encode_imagej_roi(row));assert back.options&5120==flags
    row=record();row.options|=flags
    if flags:
        with pytest.raises(ValueError,match='point behavior'):ij.encode_imagej_roi(row)

ROUNDED_OPTIONS_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-rounded-options-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',ROUNDED_OPTIONS_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_rounded_and_display_records(case):
    row=ij.decode_imagej_roi(bytes.fromhex(case['hex']),mask_width=12,mask_height=10)
    assert row.roi.name=='options' and row.properties=='owned: options\n'
    assert (row.roi.t,row.roi.c,row.roi.z)==(0,1,2)
    if case['index']<20:
        assert row.rounded_rectangle==case['bounds']+[case['diameter']]
        assert ''.join('1' if v else '0' for v in row.roi.selection.mask((10,12)).flat)==case['mask']
    else:assert row.rounded_rectangle is None
    overlay=case['overlay']
    for index,flag in [(0,8),(1,16),(2,32),(5,2048)]:assert bool(row.options&flag)==overlay[index]
    assert bool(row.options&64)==bool(overlay[3]&1)
    assert row.overlay_font_size==overlay[4] and row.overlay_label_color==overlay[6]
    if case['index']>=52:
        assert bool(row.options&1024)==case['point'][0]
        assert bool(row.options&4096)==case['point'][1]

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_mask_pixels=1),ij.ImageJRoiLimits(max_vertices=2),ij.ImageJRoiLimits(max_path_work=1)])
def test_rounded_mask_limits(limits):
    wire=bytes.fromhex(ROUNDED_OPTIONS_REFERENCE['cases'][0]['hex'])
    with pytest.raises(ValueError):ij.decode_imagej_roi(wire,limits,mask_width=12,mask_height=10)

def test_rounded_requires_unambiguous_mask_sidecar():
    row=record();row.rounded_rectangle=[1,1,8,6,4]
    with pytest.raises(ValueError,match='require a mask'):ij.encode_imagej_roi(row)
    row=ij.ImageJRoi(ij.ROI('rounded',ij.imagej_rounded_rectangle_mask([1,1,8,6,4],12,10)))
    row.rounded_rectangle=[1,1,8,6,4];row.composite_path=rectangle_path(1,1,8,6)
    with pytest.raises(ValueError,match='Composite path'):ij.encode_imagej_roi(row)

@pytest.mark.parametrize('style',range(5))
@pytest.mark.parametrize('flags',[0,2,4,6])
def test_arrow_wire_and_mask_staleness(style,flags):
    arrow=ij.ImageJArrow([8.25,12.5,48.75,32.25],style,10)
    mask=ij.imagej_arrow_mask(arrow,2.5,64,48,bool(flags&2))
    row=ij.ImageJRoi(ij.ROI('arrow',mask,t=0,c=1,z=2));row.arrow=arrow;row.options|=flags
    wire=ij.encode_imagej_roi(row)
    assert wire[6]==3 and struct.unpack_from('>H',wire,48)[0]==2 and wire[52]==style and wire[53]==10
    back=ij.decode_imagej_roi(wire,mask_width=64,mask_height=48)
    assert back.arrow.endpoints==arrow.endpoints and back.arrow.style==style and back.arrow.head_size==10
    assert back.roi.selection.mask((48,64)).tobytes()==mask.mask((48,64)).tobytes()
    assert back.options&6==flags and (back.roi.t,back.roi.c,back.roi.z)==(0,1,2)
    with pytest.raises(ValueError,match='canvas'):ij.decode_imagej_roi(wire)
    row.roi=ij.ROI('edited',ij.imagej_shape_mask(rectangle_path(1,1,2,2),64,48))
    with pytest.raises(ValueError,match='differs from mask'):ij.encode_imagej_roi(row)
    row.arrow=None;row.options&=~6
    assert ij.decode_imagej_roi(ij.encode_imagej_roi(row),mask_width=64,mask_height=48).arrow is None

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_mask_pixels=1),ij.ImageJRoiLimits(max_vertices=2),ij.ImageJRoiLimits(max_path_work=1)])
def test_arrow_mask_resource_limits(limits):
    with pytest.raises(ValueError):ij.imagej_arrow_mask(ij.ImageJArrow([1,1,8,8]),2.5,12,10,limits=limits)

@pytest.mark.parametrize('stroke',[0,-1,float('inf'),float('nan'),1e7])
def test_invalid_arrow_stroke_refuses(stroke):
    with pytest.raises(ValueError):ij.imagej_arrow_mask(ij.ImageJArrow([1,1,8,8]),stroke,12,10)

def test_ambiguous_arrow_geometry_and_stroke_default():
    with pytest.raises(ValueError):ij.imagej_arrow_mask(ij.ImageJArrow([1,1,1,1]),2.5,12,10)
    with pytest.raises(ValueError):ij.imagej_arrow_mask(ij.ImageJArrow([1,1,8,8],5),2.5,12,10)
    row=record();row.arrow=ij.ImageJArrow([1,1,8,8])
    with pytest.raises(ValueError,match='unambiguous mask'):ij.encode_imagej_roi(row)
    row=ij.ImageJRoi(ij.ROI('arrow',ij.imagej_arrow_mask(ij.ImageJArrow([1,1,8,8]),2,12,10)))
    row.arrow=ij.ImageJArrow([1,1,8,8])
    wire=bytearray(ij.encode_imagej_roi(row));h2=struct.unpack_from('>I',wire,60)[0]
    struct.pack_into('>H',wire,34,0);struct.pack_into('>f',wire,h2+36,0)
    decoded=ij.decode_imagej_roi(bytes(wire),mask_width=12,mask_height=10)
    assert decoded.roi.selection.attributes['imagejxx.roi_style']['stroke_width']==2
    assert decoded.roi.selection.mask((10,12)).tobytes()==row.roi.selection.mask((10,12)).tobytes()

@pytest.mark.parametrize('flags',[2,4,6])
def test_arrow_only_flags_refuse_on_other_geometry(flags):
    row=record();row.options|=flags
    with pytest.raises(ValueError,match='arrow flags'):ij.encode_imagej_roi(row)

ARROW_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-arrow-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',ARROW_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_arrow_records_and_processing_masks(case):
    row=ij.decode_imagej_roi(bytes.fromhex(case['hex']),mask_width=64,mask_height=48)
    assert row.roi.name=='arrow' and row.properties=='owned: arrow\n'
    assert (row.roi.t,row.roi.c,row.roi.z)==(0,1,2)
    assert row.arrow.endpoints==case['endpoints'] and row.arrow.style==case['style'] and row.arrow.head_size==case['head_size']
    assert bool(row.options&2)==case['double_headed'] and bool(row.options&4)==case['outline']
    assert row.roi.selection.attributes['imagejxx.roi_style']['stroke_width']==case['width']
    assert ''.join('1' if v else '0' for v in row.roi.selection.mask((48,64)).flat)==case['mask']

@pytest.mark.parametrize('offset,fmt,value',[(6,'>B',1),(48,'>H',1),(52,'>B',5),(18,'>f',float('inf')),(60,'>I',65)])
def test_arrow_payload_corruption_refuses(offset,fmt,value):
    wire=bytearray.fromhex(ARROW_REFERENCE['cases'][0]['hex']);struct.pack_into(fmt,wire,offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_roi(bytes(wire),mask_width=64,mask_height=48)

def test_all_arrow_truncations_refuse():
    wire=bytes.fromhex(ARROW_REFERENCE['cases'][0]['hex'])
    for end in range(len(wire)):
        with pytest.raises(ValueError):ij.decode_imagej_roi(wire[:end],mask_width=64,mask_height=48)

@pytest.mark.parametrize('type',[0,4,5,7,8,9])
@pytest.mark.parametrize('integer',[False,True])
def test_spline_control_transport_and_stale_samples(type,integer):
    controls=[[2,3],[24,4],[17,18]]+([] if type==9 else [[4,16]])
    spline=ij.ImageJSpline(controls,type,integer)
    sampled=ij.imagej_spline_selection(spline)
    assert sampled.kind==('polygon' if type in (0,7,8) else 'polyline') and len(sampled.points)>=100
    row=ij.ImageJRoi(ij.ROI('spline',sampled,t=0,c=1,z=2));row.spline=spline
    wire=ij.encode_imagej_roi(row);assert wire[6]==type
    assert struct.unpack_from('>H',wire,50)[0]&1 and bool(wire[51]&128)!=integer
    back=ij.decode_imagej_roi(wire)
    assert back.spline.control_points==controls and back.spline.type==type and back.spline.integer_coordinates==integer
    assert back.roi.selection.points==sampled.points and (back.roi.t,back.roi.c,back.roi.z)==(0,1,2)
    row.roi=ij.ROI('edited',ij.Selection(sampled.kind,sampled.points[:-1]))
    with pytest.raises(ValueError,match='differs from sampled'):ij.encode_imagej_roi(row)
    row.spline=None;row.options&=~1
    assert ij.decode_imagej_roi(ij.encode_imagej_roi(row)).spline is None

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_vertices=99),ij.ImageJRoiLimits(max_bytes=128),ij.ImageJRoiLimits(max_path_work=100)])
def test_spline_resource_limits(limits):
    with pytest.raises(ValueError,match='resource limit'):
        ij.imagej_spline_selection(ij.ImageJSpline([[2,3],[24,4],[17,18],[4,16]]),limits)

@pytest.mark.parametrize('controls,type,integer',[
    ([[1,1]],0,False),([[1,1],[2,2]],0,False),([[1,1],[2,2]],9,False),
    ([[1,1],[2,2],[3,3]],3,False),([[1,1],[2,float('inf')],[3,3]],0,False),
    ([[1.5,1],[2,2],[3,3]],0,True),([[-5001,1],[2,2],[3,3]],0,True)])
def test_invalid_spline_controls_refuse(controls,type,integer):
    with pytest.raises(ValueError):ij.imagej_spline_selection(ij.ImageJSpline(controls,type,integer))

def test_spline_fit_requires_control_sidecar_and_consistent_type():
    row=record('polygon',[[2,3],[24,4],[17,18],[4,16]]);row.options|=1
    with pytest.raises(ValueError,match='requires retained control'):ij.encode_imagej_roi(row)
    spline=ij.ImageJSpline([[2,3],[24,4],[17,18],[4,16]],4)
    row=ij.ImageJRoi(ij.ROI('curve',ij.imagej_spline_selection(spline)));row.spline=spline;row.source_type=5
    with pytest.raises(ValueError,match='source type differs'):ij.encode_imagej_roi(row)

SPLINE_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-spline-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',SPLINE_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_spline_samples_and_area_masks(case):
    wire=bytes.fromhex(case['hex']);row=ij.decode_imagej_roi(wire)
    assert row.roi.name=='spline' and row.properties=='owned: spline\n'
    assert (row.roi.t,row.roi.c,row.roi.z)==(0,1,2)
    assert row.spline.control_points==case['control_points'] and row.spline.type==case['wire_type']
    assert row.spline.integer_coordinates==(not bool(wire[51]&128))
    assert row.roi.selection.points==case['points']
    if case['mask']:
        assert ''.join('1' if v else '0' for v in row.roi.selection.mask((24,32)).flat)==case['mask']

def test_all_spline_truncations_refuse():
    for index in [0,48]:
        wire=bytes.fromhex(SPLINE_REFERENCE['cases'][index]['hex'])
        for end in range(len(wire)):
            with pytest.raises(ValueError):ij.decode_imagej_roi(wire[:end])

def text_record():
    row=ij.ImageJTextRoi();row.bounds=[3.25,4.5,50,20];row.text='line λ😀\nsecond\n'
    row.name='annotation λ';row.properties='owned: text\n';row.font_name='SansSerif'
    row.t,row.c,row.z=0,1,2;row.stroke_color=0x80112233;row.fill_color=0xff445566;row.stroke_width=2.5
    return row

@pytest.mark.parametrize('style',range(4))
@pytest.mark.parametrize('justify',range(4))
@pytest.mark.parametrize('draw',[False,True])
def test_text_annotation_raw_metadata_roundtrip(style,justify,draw):
    row=text_record();row.font_style=style;row.justification=justify;row.draw_string_mode=draw;row.angle=-30.5
    row.options|=8|16|32|64|2048;row.overlay_font_size=14;row.overlay_label_color=0xff123456
    wire=ij.encode_imagej_text_roi(row);back=ij.decode_imagej_text_roi(wire)
    assert wire[6]==1 and struct.unpack_from('>H',wire,48)[0]==1
    for key in ['bounds','text','name','properties','font_name','font_size','font_style','justification','draw_string_mode','angle','t','c','z','stroke_color','fill_color','stroke_width','overlay_font_size','overlay_label_color']:assert getattr(back,key)==getattr(row,key)
    assert not hasattr(back,'roi')
    with pytest.raises(ValueError,match='font layout'):ij.decode_imagej_roi(wire)
    with pytest.raises(ValueError,match='Expected ImageJ text'):ij.decode_imagej_text_roi(ij.encode_imagej_roi(record()))

@pytest.mark.parametrize('version',[218,224])
def test_legacy_text_without_angle(version):
    row=text_record();row.properties=''
    wire=bytearray(ij.encode_imagej_text_roi(row));h2=struct.unpack_from('>I',wire,60)[0]
    del wire[h2-4:h2];h2-=4
    struct.pack_into('>I',wire,60,h2);struct.pack_into('>H',wire,4,version)
    for field in [16,40]:
        offset=struct.unpack_from('>I',wire,h2+field)[0]
        if offset:struct.pack_into('>I',wire,h2+field,offset-4)
    if version<222:struct.pack_into('>H',wire,50,8192);struct.pack_into('>hhhh',wire,8,4,3,24,53)
    back=ij.decode_imagej_text_roi(bytes(wire));assert back.angle==0 and back.text==row.text
    assert back.bounds==([3,4,50,20] if version<222 else row.bounds)

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_bytes=100),ij.ImageJRoiLimits(max_text_bytes=2)])
def test_text_annotation_limits(limits):
    row=text_record()
    with pytest.raises(ValueError):ij.encode_imagej_text_roi(row,limits)
    with pytest.raises(ValueError):ij.decode_imagej_text_roi(ij.encode_imagej_text_roi(row),limits)

@pytest.mark.parametrize('offset,fmt,value',[(6,'>B',2),(48,'>H',4),(64,'>I',0xffffffff),(68,'>I',0x80000000),(72,'>I',0xffffffff),(76,'>I',0xffffffff),(80,'>H',0xdc00),(60,'>I',80)])
def test_text_corruption_refuses(offset,fmt,value):
    wire=bytearray(ij.encode_imagej_text_roi(text_record()));struct.pack_into(fmt,wire,offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_text_roi(bytes(wire))

def test_text_annotation_all_truncations_and_unknown_trailing_refuse():
    wire=ij.encode_imagej_text_roi(text_record())
    for end in range(len(wire)):
        with pytest.raises(ValueError):ij.decode_imagej_text_roi(wire[:end])
    with pytest.raises(ValueError,match='trailing'):ij.decode_imagej_text_roi(wire+b'future')

@pytest.mark.parametrize('field,value',[('font_size',-1),('font_style',4),('justification',4),('angle',float('nan')),('bounds',[1,1,0,2])])
def test_invalid_text_annotation_parameters(field,value):
    row=text_record();setattr(row,field,value)
    with pytest.raises(ValueError):ij.encode_imagej_text_roi(row)

TEXT_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-text-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',TEXT_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_text_annotation_metadata(case):
    import base64
    row=ij.decode_imagej_text_roi(bytes.fromhex(case['hex']))
    assert row.bounds==[struct.unpack('>f',struct.pack('>f',v))[0] for v in case['wire_bounds']]
    assert row.text==base64.b64decode(case['wire_text_b64']).decode('utf-8')
    assert row.font_name==base64.b64decode(case['font_b64']).decode('utf-8')
    assert row.name==base64.b64decode(case['name_b64']).decode('utf-8') and row.properties=='owned: text\n'
    for field in ['font_size','font_style','justification','angle']:assert getattr(row,field)==case[field]
    assert row.stroke_color==case['stroke'] and row.fill_color==case['fill'] and row.stroke_width==2.5
    assert (row.t,row.c,row.z)==(0,1,2)

def image_record():
    import numpy as np
    row=ij.ImageJImageRoi();row.pixels=np.arange(18,dtype=np.uint8).reshape(2,3,3)
    row.origin=[-2,5];row.opacity=127;row.zero_transparent=True
    row.name='image';row.properties='owned: image\n';row.t,row.c,row.z=0,1,2
    return row

def replace_image_payload(wire,payload):
    old=struct.unpack_from('>I',wire,60)[0];new=64+len(payload)
    out=bytearray(wire[:64]+payload+wire[old:]);struct.pack_into('>I',out,60,new);struct.pack_into('>I',out,new+32,len(payload))
    for field in [16,40]:
        p=struct.unpack_from('>I',out,new+field)[0]
        if p:struct.pack_into('>I',out,new+field,p+new-old)
    return bytes(out)

def test_image_annotation_native_pixels_and_stale_payload():
    row=image_record();wire=ij.encode_imagej_image_roi(row);back=ij.decode_imagej_image_roi(wire)
    assert back.pixels.tobytes()==row.pixels.tobytes() and back.origin==row.origin
    assert back.opacity==127 and back.zero_transparent and back.name==row.name and back.properties==row.properties
    assert (back.t,back.c,back.z)==(0,1,2) and not hasattr(back,'roi')
    with pytest.raises(ValueError,match='decode_imagej_image_roi'):ij.decode_imagej_roi(wire)
    with pytest.raises(ValueError,match='Expected ImageJ image'):ij.decode_imagej_image_roi(ij.encode_imagej_roi(record()))
    edited=back.pixels;edited[0,0]=[255,254,253];back.pixels=edited
    with pytest.raises(ValueError,match='differs from pixels'):ij.encode_imagej_image_roi(back)
    back.serialized_image=b''
    assert ij.decode_imagej_image_roi(ij.encode_imagej_image_roi(back)).pixels.tobytes()==edited.tobytes()

@pytest.mark.parametrize('limits',[ij.ImageJRoiLimits(max_bytes=100),ij.ImageJRoiLimits(max_image_pixels=1),ij.ImageJRoiLimits(max_path_work=1)])
def test_image_annotation_limits(limits):
    with pytest.raises(ValueError):ij.encode_imagej_image_roi(image_record(),limits)
    with pytest.raises(ValueError):ij.decode_imagej_image_roi(ij.encode_imagej_image_roi(image_record()),limits)

@pytest.mark.parametrize('tag,field,fmt,value',[(256,8,'I',0xffffffff),(258,4,'I',0xffffffff),(259,8,'H',5),(273,8,'I',0),(279,8,'I',1),(284,8,'H',2),(257,0,'H',256),(256,0,'H',65000)])
def test_embedded_tiff_tag_corruption_refuses(tag,field,fmt,value):
    wire=ij.encode_imagej_image_roi(image_record());p=bytearray(ij.decode_imagej_image_roi(wire).serialized_image)
    offset=next(i for i in range(10,130,12) if struct.unpack_from('<H',p,i)[0]==tag)
    struct.pack_into('<'+fmt,p,offset+field,value)
    with pytest.raises(ValueError):ij.decode_imagej_image_roi(replace_image_payload(wire,bytes(p)))

@pytest.mark.parametrize('offset,fmt,value',[(0,'H',0),(2,'H',43),(4,'I',0xffffffff),(8,'H',129),(130,'I',8)])
def test_embedded_tiff_header_corruption_refuses(offset,fmt,value):
    wire=ij.encode_imagej_image_roi(image_record());p=bytearray(ij.decode_imagej_image_roi(wire).serialized_image)
    struct.pack_into('<'+fmt,p,offset,value)
    with pytest.raises(ValueError):ij.decode_imagej_image_roi(replace_image_payload(wire,bytes(p)))

def test_all_image_and_embedded_tiff_truncations_refuse():
    wire=ij.encode_imagej_image_roi(image_record());payload=ij.decode_imagej_image_roi(wire).serialized_image
    for end in range(len(wire)):
        with pytest.raises(ValueError):ij.decode_imagej_image_roi(wire[:end])
    for end in range(len(payload)):
        with pytest.raises(ValueError):ij.decode_imagej_image_roi(replace_image_payload(wire,payload[:end]))
    with pytest.raises(ValueError,match='trailing'):ij.decode_imagej_image_roi(replace_image_payload(wire,payload+b'unknown'))

IMAGE_REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-image-annotation-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',IMAGE_REFERENCE['cases'],ids=lambda c:str(c['index']))
def test_actual_imagej_embedded_pixels_and_annotation_fields(case):
    wire=bytes.fromhex(case['hex']);row=ij.decode_imagej_image_roi(wire);h2=struct.unpack_from('>I',wire,60)[0]
    assert row.pixels.tobytes().hex()==case['pixels'] and row.origin==case['bounds'][:2]
    assert row.opacity==wire[h2+31] and row.zero_transparent==case['zero_transparent']
    assert row.serialized_image==wire[64:h2] and row.name=='image' and row.properties=='owned: image\n'
    assert (row.t,row.c,row.z)==(0,1,2)

@pytest.mark.parametrize('order',['<','>'])
@pytest.mark.parametrize('photo',[0,1])
def test_independent_multistrip_gray_tiff(order,photo):
    import numpy as np
    # Independent literal two-strip2x4 TIFF: no native encoder used for payload.
    entries=[(256,4,1,2),(257,4,1,4),(258,3,1,8),(259,3,1,1),(262,3,1,photo),
             (273,4,2,122),(277,3,1,1),(278,4,1,2),(279,4,2,130)]
    payload=bytearray((b'II' if order=='<' else b'MM')+struct.pack(order+'HIH',42,8,9))
    for tag,type,count,value in entries:
        payload+=struct.pack(order+'HHI',tag,type,count)
        payload+=struct.pack(order+'H',value)+b'\0\0' if type==3 else struct.pack(order+'I',value)
    payload+=struct.pack(order+'I',0)+struct.pack(order+'IIII',138,142,4,4)+bytes(range(8))
    wire=replace_image_payload(ij.encode_imagej_image_roi(image_record()),bytes(payload))
    result=ij.decode_imagej_image_roi(wire)
    expected=np.arange(8,dtype=np.uint8).reshape(4,2)
    if photo==0:expected=255-expected
    assert result.pixels.shape==(4,2,3)
    for channel in range(3):assert result.pixels[:,:,channel].tobytes()==expected.tobytes()
    struct.pack_into(order+'I',payload,126,138)
    with pytest.raises(ValueError,match='Overlapping'):ij.decode_imagej_image_roi(replace_image_payload(wire,bytes(payload)))

@pytest.mark.parametrize('shape,dtype',[((2,3),'uint8'),((2,3,4),'uint8'),((2,3,3),'uint16'),((2,3,3),'float32')])
def test_image_annotation_requires_explicit_rgb8_pixels(shape,dtype):
    import numpy as np
    row=image_record();row.pixels=np.zeros(shape,dtype=dtype)
    with pytest.raises(ValueError,match='RGB8'):ij.encode_imagej_image_roi(row)

def test_image_annotation_header2_alias_and_declared_size_refuse():
    wire=bytearray(ij.encode_imagej_image_roi(image_record()));header=struct.unpack_from('>I',wire,60)[0]
    for size in [0,0xffffffff]:
        copy=bytearray(wire);struct.pack_into('>I',copy,header+32,size)
        with pytest.raises(ValueError):ij.decode_imagej_image_roi(bytes(copy))
    struct.pack_into('>I',wire,60,65)
    with pytest.raises(ValueError):ij.decode_imagej_image_roi(bytes(wire))
