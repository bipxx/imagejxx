"""Native precision/opaque metadata with an independently checked wire projection."""
import base64
import json
import struct
from pathlib import Path
import pytest
import imagejxx as ij

EXTENT=[3,4,5,7,11,3]
TARGET=[2,3,6,8,12,1]

def original():
    context=ij.ImageJRoiPlaneMapping();context.extent=EXTENT;context.coordinate=2
    record=ij.ImageJRoi(ij.ROI('source',ij.Selection('rectangle',[(1,1),(3,3)]),t=0,c=1))
    record.properties='original: λ😀\n';record.group=7
    return ij.map_imagej_roi(record,context)

def native(plane='yz',kind='rectangle',coordinate=0):
    selection=ij.Selection(kind,[(1.12345678901234,2.23456789012345),(3.34567890123456,4.45678901234567)],
                           attributes={'future-selection':{'λ':[None,True,2**64-1]}})
    row=ij.PlanarAnnotation('native λ😀',selection,plane,coordinate,t=1,c=None,
                            attributes={'future-row':{'array':[False,-7,2**64-1,'😀']}})
    return ij.rebind_imagej_roi_mapping(original(),TARGET,row)

def projection(value):
    row=value.annotation
    selection=row.selection.with_attributes({k:v for k,v in row.selection.attributes.items() if k=='imagejxx.roi_style'})
    out=ij.ImageJRoi(ij.ROI(row.name,selection,t=row.t,c=row.c,z=row.coordinate))
    out.properties='projection: chosen λ😀\n'
    if selection.kind in ('point','multipoint'):
        out.options=0;out.point_counters=[0]*len(selection.points)
    return out

def envelope(value=None):
    value=value or native()
    return ij.encode_imagej_roi_native_envelope(value,projection(value))

def parts(wire):
    record=ij.decode_imagej_roi(wire)
    raw=base64.b64decode(record.properties.split(': ',1)[1])
    pos=4
    n=struct.unpack_from('>I',raw,pos)[0];pos+=4
    source=raw[pos:pos+n];pos+=n
    extent=raw[pos:pos+24];pos+=24
    n=struct.unpack_from('>I',raw,pos)[0];pos+=4
    row=raw[pos:pos+n];pos+=n
    n=struct.unpack_from('>I',raw,pos)[0];pos+=4
    binding=raw[pos:pos+n];pos+=n
    assert pos==len(raw)
    return record,source,extent,row,binding

def modified(transform):
    record,source,extent,row,binding=parts(envelope())
    source,extent,row,binding=transform(source,extent,row,binding)
    blob=lambda b:struct.pack('>I',len(b))+b
    payload=b'IJX\x02'+blob(source)+extent+blob(row)+blob(binding)
    record.properties='imagejxx.roi.v2: '+base64.b64encode(payload).decode()+'\n'
    return ij.encode_imagej_roi(record)

@pytest.mark.parametrize('plane,last',[('xy',5),('xz',7),('yz',11)])
@pytest.mark.parametrize('kind',['rectangle','ellipse','line','polyline','multipoint'])
def test_native_precision_metadata_source_and_target_survive(plane,last,kind):
    for coordinate in (0,last):
        value=native(plane,kind,coordinate)
        wire=envelope(value)
        for decode in (ij.decode_imagej_roi_native_envelope,ij.decode_imagej_roi_envelope):
            restored=decode(wire)
            assert restored.annotation.selection.points==value.annotation.selection.points
            assert restored.annotation.selection.attributes==value.annotation.selection.attributes
            assert restored.annotation.attributes==value.annotation.attributes
            assert restored.annotation.plane==plane and restored.annotation.coordinate==coordinate
            assert (restored.annotation.t,restored.annotation.c)==(1,None)
            assert list(restored.target_extent)==TARGET
            assert list(restored.source_mapping.extent)==EXTENT
            assert restored.source_mapping.coordinate==2
            assert restored.source_record.properties=='original: λ😀\n'
            assert restored.source_record.group==7
            assert ij.encode_imagej_roi_native_envelope(restored,projection(restored))==wire

@pytest.mark.parametrize('field',['geometry','name','t','c','z'])
def test_projection_cannot_disagree_with_native_row(field):
    value=native();p=projection(value);r=p.roi
    values=dict(name=r.name,selection=r.selection,t=r.t,c=r.c,z=r.z)
    values[{'geometry':'selection'}.get(field,field)]={'geometry':ij.Selection('rectangle',[(0,0),(2,2)]),'name':'wrong','t':0,'c':0,'z':1}[field]
    p.roi=ij.ROI(**values)
    with pytest.raises(ValueError):ij.encode_imagej_roi_native_envelope(value,p)

def test_modified_native_geometry_must_still_match_projection():
    def edit(s,e,r,b):
        value=json.loads(r);value['rows'][0]['name']='tampered native name'
        return s,e,json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode(),b
    with pytest.raises(ValueError,match='projection disagrees'):
        ij.decode_imagej_roi_envelope(modified(edit))

@pytest.mark.parametrize('row',[b'{"schema":1,"schema":2}',b'['*70+b'0'+b']'*70,b'{',b'{}'])
def test_strict_bounded_json(row):
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(modified(lambda s,e,r,b:(s,e,row,b)))

def test_original_is_admitted_before_invalid_target():
    def edit(s,e,r,b):
        source=ij.decode_imagej_roi(s)
        source.group=9
        return ij.encode_imagej_roi(source),b'\0'*24,r,b
    with pytest.raises(ValueError,match='Stale ROI envelope'):
        ij.decode_imagej_roi_envelope(modified(edit))

def test_nested_native_provenance_refuses_without_recursion():
    nested=envelope()
    with pytest.raises(ValueError,match='version1 source'):
        ij.decode_imagej_roi_envelope(modified(lambda s,e,r,b:(nested,e,r,b)))

def test_external_edit_and_envelope_limits():
    value=native();wire=envelope(value)
    record=ij.decode_imagej_roi(wire);record.group=8
    with pytest.raises(ValueError,match='Stale native'):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(record))
    with pytest.raises(ValueError):
        ij.encode_imagej_roi_native_envelope(value,projection(value),ij.ImageJRoiLimits(max_text_bytes=1000))
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(wire,ij.ImageJRoiLimits(max_bytes=len(wire)-1))
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(modified(lambda s,e,r,b:(s,e,r+b' ',b)))

def test_projection_legacy_must_be_zero():
    value=native();p=projection(value);p.legacy_position=1
    with pytest.raises(ValueError,match='legacy position0'):
        ij.encode_imagej_roi_native_envelope(value,p)

@pytest.mark.parametrize('plane,width,height',[('xy',12,8),('xz',12,6),('yz',8,6)])
def test_native_mask_and_opaque_metadata_use_current_canvas(plane,width,height):
    shape=ij.imagej_shape_mask([0,1,1,1,4,1,1,4,4,1,1,4,4],width,height)
    shape=shape.with_attributes({'opaque':{'kept':'λ'}})
    row=ij.PlanarAnnotation('mask',shape,plane,0,attributes={'future':[None,True]})
    value=ij.rebind_imagej_roi_mapping(original(),TARGET,row)
    wire=envelope(value)
    restored=ij.decode_imagej_roi_envelope(wire)
    assert restored.annotation.selection.mask((height,width)).tobytes()==shape.mask((height,width)).tobytes()
    assert restored.annotation.selection.attributes==shape.attributes
    assert restored.annotation.attributes==row.attributes

def test_native_style_is_checked_independently_of_wire_template():
    value=native()
    row=value.annotation
    selection=ij.with_roi_style(row.selection,{'stroke_color':'#80112233','stroke_width':2.5})
    styled=ij.PlanarAnnotation(row.name,selection,row.plane,row.coordinate,t=row.t,c=row.c,attributes=row.attributes)
    value=ij.rebind_imagej_roi_mapping(value,TARGET,styled)
    restored=ij.decode_imagej_roi_envelope(envelope(value))
    assert restored.annotation.selection.attributes==selection.attributes
    wrong=projection(value)
    wrong.roi=ij.ROI(row.name,row.selection.with_attributes({}),t=row.t,c=row.c,z=row.coordinate)
    with pytest.raises(ValueError,match='projection disagrees'):
        ij.encode_imagej_roi_native_envelope(value,wrong)

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-native-envelope-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',REFERENCE['cases'],ids=lambda c:f"{c['ordinal']}-{c['plane']}-{c['kind']}")
def test_actual_imagej_native_precision_and_point_provenance(case):
    restored=ij.decode_imagej_roi_envelope(bytes.fromhex(case['unchanged']))
    assert restored.annotation.selection.points==case['points']
    assert restored.annotation.plane==case['plane'] and restored.annotation.coordinate==case['coordinate']
    assert restored.annotation.selection.attributes=={'opaque-selection':{'kept':[None,True,2**64-1]}}
    assert restored.annotation.attributes=={'opaque-row':{'λ':['😀',-7]}}
    assert list(restored.target_extent)==TARGET and list(restored.source_mapping.extent)==EXTENT
    assert restored.source_record.group==7 and restored.source_record.options & 8192
    assert restored.source_record.point_counters==[] and restored.source_record.properties=='source: λ😀\n'
    with pytest.raises(ValueError,match='Stale native'):
        ij.decode_imagej_roi_envelope(bytes.fromhex(case['edited']))

def test_actual_imagej_float_rectangle_arithmetic_changes_binding():
    case=REFERENCE['wire_drift_case']
    ij.decode_imagej_roi_envelope(bytes.fromhex(case['input']))
    with pytest.raises(ValueError,match='Stale native'):
        ij.decode_imagej_roi_envelope(bytes.fromhex(case['unchanged']))
