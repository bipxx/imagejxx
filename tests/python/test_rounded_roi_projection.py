"""Explicit rounded carriers, processing membership and native intent retention."""
from pathlib import Path
import json
import struct
import numpy as np
import pytest
import imagejxx as ij

SHAPE=(33,41)
DIAMETERS=[0,1,3,60535,60536,2**31-1]
BOXES=[((6,5),(24,19)),((-1.7,2.3),(14.6,13.8)),
       ((4.99999999,5),(15,19)),((6,5),(24.00000001,19))]

def selection(points=BOXES[0],diameter=8,angle=0,opaque=False):
    attrs={'corner_diameter':diameter,'imagejxx.roi_style':{
        'schema_version':1,'stroke_color':[12,78,120,200],
        'fill_color':[40,90,20,70],'stroke_width':2.5}}
    if opaque:attrs['future']={'values':[None,True,2**64-1]}
    value=ij.Selection('rounded_rectangle',points,2,attributes=attrs)
    return ij.SelectionEditor(value,SHAPE).rotated(angle) if angle else value

def carrier(value):
    return ij.ImageJRoi(ij.ROI('Rounded λ😀',value,t=1,c=2,z=2))

@pytest.mark.parametrize('points',BOXES)
@pytest.mark.parametrize('diameter',DIAMETERS)
@pytest.mark.parametrize('angle',[0,23,90])
def test_standard_projection_preserves_processing_and_wire_canonicality(points,diameter,angle):
    value=selection(points,diameter,angle)
    source=carrier(value);source.properties='chosen: μ😀\n';source.group=7
    projected=ij.imagej_roi_standard_projection(source,41,33)
    assert source.roi.selection==value and source.rounded_rectangle is None and source.composite_path==[]
    wire=ij.encode_imagej_roi(projected)
    decoded=ij.decode_imagej_roi(wire,mask_width=41,mask_height=33)
    assert ij.encode_imagej_roi(decoded)==wire
    np.testing.assert_array_equal(decoded.roi.selection.mask(SHAPE),value.mask(SHAPE))
    primitive=angle==0 and diameter in [1,3,60535] and points in BOXES[:2]
    assert bool(decoded.rounded_rectangle)==primitive
    assert bool(decoded.composite_path)==(not primitive)
    assert struct.unpack_from('>H',wire,54)[0]==(diameter if primitive else 0)
    assert decoded.roi.name=='Rounded λ😀' and decoded.properties==source.properties and decoded.group==7
    assert (decoded.roi.t,decoded.roi.c,decoded.roi.z)==(1,2,2)
    assert ij.roi_style(decoded.roi.selection).stroke_width==2.5

def original_mapping():
    context=ij.ImageJRoiPlaneMapping();context.extent=[2,3,33,33,41,1];context.coordinate=2
    record=ij.ImageJRoi(ij.ROI('source',ij.Selection('rectangle',[(1,1),(3,3)]),t=0,c=0))
    return ij.map_imagej_roi(record,context)

@pytest.mark.parametrize('plane',['xy','xz','yz'])
@pytest.mark.parametrize('diameter',DIAMETERS)
@pytest.mark.parametrize('angle',[0,23])
def test_native_envelope_retains_editable_parameters_attributes_and_position(plane,diameter,angle):
    value=selection(BOXES[2],diameter,angle,opaque=True)
    row=ij.PlanarAnnotation('native λ😀',value,plane,2,t=1,c=2,attributes={'row':{'unsigned':2**64-1}})
    mapped=ij.rebind_imagej_roi_mapping(original_mapping(),[2,3,33,33,41,1],row)
    plain=value.with_attributes({k:v for k,v in value.attributes.items() if k in ['corner_diameter','imagejxx.roi_style']})
    proto=ij.ImageJRoi(ij.ROI(row.name,plain,t=row.t,c=row.c,z=row.coordinate))
    width=33 if plane=='yz' else 41
    projected=ij.imagej_roi_standard_projection(proto,width,33)
    wire=ij.encode_imagej_roi_native_envelope(mapped,projected)
    restored=ij.decode_imagej_roi_native_envelope(wire)
    actual=restored.annotation
    assert actual.selection==value and actual.name==row.name
    assert actual.attributes==row.attributes and actual.plane==plane
    assert (actual.t,actual.c,actual.coordinate)==(1,2,2)
    assert ij.encode_imagej_roi_native_envelope(restored,projected)==wire
    assert ij.editable_imagej_roi_mapping(restored).annotation.selection==value

@pytest.mark.parametrize('points',BOXES[:2])
@pytest.mark.parametrize('diameter',[1,3,60535])
def test_explicit_promotion_preserves_mask_and_source_provenance(points,diameter):
    source=ij.imagej_roi_standard_projection(carrier(selection(points,diameter)),41,33)
    source.legacy_position=0
    wire=ij.encode_imagej_roi(source)
    mapping=ij.ImageJRoiPlaneMapping();mapping.extent=[2,3,33,33,41,1]
    mapped=ij.decode_imagej_roi_mapped(wire,mapping)
    assert mapped.annotation.selection.kind=='mask'
    editable=ij.editable_imagej_roi_mapping(mapped)
    assert editable.annotation.selection.kind=='rounded_rectangle'
    assert editable.annotation.selection.corner_diameter==diameter
    np.testing.assert_array_equal(editable.annotation.selection.mask(SHAPE),mapped.annotation.selection.mask(SHAPE))
    assert ij.encode_imagej_roi(editable.source_record)==wire
    assert ij.editable_imagej_roi_mapping(editable).annotation.selection==editable.annotation.selection

@pytest.mark.parametrize('field,value',[('rounded_rectangle',[6,5,18,14,8]),('composite_path',[0,0,0,4]),('source_subtype',5),('source_type',7)])
def test_retained_geometry_conflicts_are_explicit(field,value):
    proto=carrier(selection());setattr(proto,field,value)
    with pytest.raises(ValueError,match='retained geometry'):
        ij.imagej_roi_standard_projection(proto,41,33)

@pytest.mark.parametrize('width,height',[(0,33),(41,0),(-1,33),(41,-1),(2**30,33)])
def test_projection_requires_bounded_canvas(width,height):
    with pytest.raises(ValueError):ij.imagej_roi_standard_projection(carrier(selection()),width,height)

def test_projection_keeps_opaque_metadata_for_explicit_codec_refusal():
    proto=carrier(selection(opaque=True))
    projected=ij.imagej_roi_standard_projection(proto,41,33)
    assert projected.roi.selection.attributes['future']==proto.roi.selection.attributes['future']
    with pytest.raises(ValueError,match='Opaque selection'):
        ij.encode_imagej_roi(projected)

def test_absent_or_stale_rounded_source_cannot_be_promoted():
    original=original_mapping()
    with pytest.raises(ValueError,match='retained rounded'):ij.editable_imagej_roi_mapping(original)
    source=ij.imagej_roi_standard_projection(carrier(selection()),41,33)
    mapping=ij.ImageJRoiPlaneMapping();mapping.extent=[2,3,33,33,41,1]
    mapped=ij.map_imagej_roi(source,mapping)
    row=mapped.annotation
    stale=ij.PlanarAnnotation(row.name,ij.Selection('rectangle',[(1,1),(2,2)]),row.plane,row.coordinate,t=row.t,c=row.c)
    rebound=ij.rebind_imagej_roi_mapping(mapped,[2,3,33,33,41,1],stale)
    with pytest.raises(ValueError,match='differs'):ij.editable_imagej_roi_mapping(rebound)

def test_projection_byte_pixel_and_path_limits():
    for limits in [ij.ImageJRoiLimits(max_mask_pixels=100),ij.ImageJRoiLimits(max_path_work=1),
                   ij.ImageJRoiLimits(max_vertices=2),ij.ImageJRoiLimits(max_bytes=128)]:
        with pytest.raises(ValueError):ij.imagej_roi_standard_projection(carrier(selection(diameter=0)),41,33,limits)
