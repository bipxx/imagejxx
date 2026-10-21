"""Explicit scientific plane admission; no fabricated XY processing state."""
import json
from pathlib import Path
import pytest
import imagejxx as ij

EXTENT=[3,4,5,7,11,3] # TCZYXS; samples are not scientific channels
def context(plane='xy',coordinate=None):
    v=ij.ImageJRoiPlaneMapping();v.extent=EXTENT;v.plane=plane;v.coordinate=coordinate;return v
def record(z=None,t=None,c=None):
    row=ij.ImageJRoi(ij.ROI('source',ij.Selection('rectangle',[(1.25,2.5),(6.75,4.5)]),t=t,c=c,z=z))
    row.properties='opaque: λ😀\n';row.group=7;row.overlay_label_color=0xff123456;row.overlay_font_size=14
    return row

@pytest.mark.parametrize('plane,size',[('xy',5),('xz',7),('yz',11)])
@pytest.mark.parametrize('t,c',[(None,None),(0,None),(None,0),(2,3)])
def test_explicit_planes_extents_wildcards_and_zero(plane,size,t,c):
    row=record(t=t,c=c);mapped=ij.map_imagej_roi(row,context(plane,0))
    assert mapped.annotation.plane==plane and mapped.annotation.coordinate==0
    assert (mapped.annotation.t,mapped.annotation.c)==(t,c) and list(mapped.target_extent)==EXTENT
    assert mapped.annotation.selection.points==row.roi.selection.points
    assert mapped.source_record.properties==row.properties and mapped.source_record.group==7
    assert mapped.source_record.overlay_label_color==0xff123456 and mapped.source_record.overlay_font_size==14
    ij.validate_imagej_roi_mapping(mapped)
    with pytest.raises(ValueError):ij.map_imagej_roi(row,context(plane,size))

@pytest.mark.parametrize('plane',['xz','yz'])
def test_scientific_z_is_not_silently_a_fixed_x_or_y(plane):
    row=record(z=2)
    with pytest.raises(ValueError,match='Scientific Z'):ij.map_imagej_roi(row,context(plane))
    mapping=context(plane);mapping.z_position=ij.ImageJZPositionMapping.FIXED_AXIS
    assert ij.map_imagej_roi(row,mapping).annotation.coordinate==2
    mapping.coordinate=1
    with pytest.raises(ValueError,match='conflicts'):ij.map_imagej_roi(row,mapping)

def test_scientific_xy_position_and_unpositioned_requires_choice():
    assert ij.map_imagej_roi(record(z=0),context()).annotation.coordinate==0
    with pytest.raises(ValueError,match='requires an explicit'):ij.map_imagej_roi(record(),context())
    with pytest.raises(ValueError,match='conflicts'):ij.map_imagej_roi(record(z=2),context(coordinate=1))

@pytest.mark.parametrize('plane',['xy','xz','yz'])
def test_legacy_fixed_axis_is_explicit_and_one_based(plane):
    row=record();row.legacy_position=1
    with pytest.raises(ValueError,match='Ambiguous'):ij.map_imagej_roi(row,context(plane))
    mapping=context(plane);mapping.legacy_position=ij.ImageJLegacyPositionMapping.FIXED_AXIS
    mapped=ij.map_imagej_roi(row,mapping)
    assert mapped.annotation.coordinate==0 and mapped.annotation.t is None and mapped.annotation.c is None
    assert mapped.source_record.legacy_position==1 and mapped.source_record.roi.z is None

@pytest.mark.parametrize('t',range(3))
@pytest.mark.parametrize('c',range(4))
@pytest.mark.parametrize('z',range(5))
def test_hyperstack_c_fastest_all_positions(t,c,z):
    row=record();row.legacy_position=1+c+4*z+4*5*t
    mapping=context();mapping.legacy_position=ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT
    mapped=ij.map_imagej_roi(row,mapping)
    assert (mapped.annotation.t,mapped.annotation.c,mapped.annotation.coordinate)==(t,c,z)

def test_conflicting_and_outside_positions_do_not_rebind_implicitly():
    mapping=context();mapping.legacy_position=ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT
    row=record(t=1,c=1,z=1);row.legacy_position=1
    with pytest.raises(ValueError,match='conflict'):ij.map_imagej_roi(row,mapping)
    row=record();row.legacy_position=61
    with pytest.raises(ValueError,match='outside'):ij.map_imagej_roi(row,mapping)
    row.legacy_position=1;mapping.plane='xz'
    with pytest.raises(ValueError,match='XY mapping'):ij.map_imagej_roi(row,mapping)
    for row in [record(t=3),record(c=4),record(z=5)]:
        with pytest.raises(ValueError,match='source extent'):ij.map_imagej_roi(row,context(coordinate=0))

def test_source_copies_and_rebinding_keep_original_provenance():
    row=record(z=2,t=0,c=None);mapping=context();source=ij.map_imagej_roi(row,mapping)
    row.properties='changed';mapping.coordinate=4
    source.source_record.group=99;source.source_mapping.extent=[1]*6
    assert source.source_record.properties=='opaque: λ😀\n' and source.source_record.group==7
    replacement=ij.PlanarAnnotation('new',ij.Selection('rectangle',[(0,0),(2,2)]),'yz',3,t=None,c=1,attributes={'opaque':{'λ':[None,0,'😀']}})
    target=[2,2,4,6,8,1]
    rebound=ij.rebind_imagej_roi_mapping(source,target,replacement)
    assert rebound.annotation.plane=='yz' and rebound.annotation.coordinate==3 and rebound.annotation.attributes==replacement.attributes
    assert rebound.annotation.t is None and rebound.annotation.c==1 and list(rebound.target_extent)==target
    assert rebound.source_record.roi.z==2 and list(rebound.source_mapping.extent)==EXTENT
    assert source.annotation.name=='source' and source.annotation.plane=='xy'
    with pytest.raises(ValueError,match='vertex limit'):
        ij.rebind_imagej_roi_mapping(source,[0]*6,replacement,ij.ImageJRoiLimits(max_vertices=1))

@pytest.mark.parametrize('plane,width,height',[('xy',11,7),('xz',11,5),('yz',7,5)])
def test_packed_mask_uses_declared_plane_canvas(plane,width,height):
    path=[0,1,1,1,3,1,1,3,3,1,1,3,4]
    row=ij.ImageJRoi(ij.ROI('mask',ij.imagej_shape_mask(path,width,height)));row.composite_path=path
    wire=ij.encode_imagej_roi(row);mapped=ij.decode_imagej_roi_mapped(wire,context(plane,0))
    assert mapped.annotation.selection.mask((height,width)).tobytes()==row.roi.selection.mask((height,width)).tobytes()
    wrong=context('xy',0)
    if plane!='xy':
        with pytest.raises(ValueError):ij.map_imagej_roi(mapped.source_record,wrong)

@pytest.mark.parametrize('value',[True,1.5,-1,2**100])
def test_strict_context_coordinate(value):
    with pytest.raises((ValueError,TypeError,OverflowError)):context(coordinate=value)

def test_unknown_native_envelope_never_falls_back_to_xy():
    row=record(z=0);row.properties='imagejxx.roi.v99: unreadable\n'
    with pytest.raises(ValueError,match='envelope decoder'):ij.map_imagej_roi(row,context())

def test_reserved_namespace_is_a_property_key_not_arbitrary_text():
    row=record(z=0);row.properties='note: imagejxx.roi.v1 mentioned as text\n'
    assert ij.map_imagej_roi(row,context()).source_record.properties==row.properties
    row.properties='note: preceding value\n  imagejxx.roi.v2: unreadable\n'
    with pytest.raises(ValueError,match='envelope decoder'):ij.map_imagej_roi(row,context())

@pytest.mark.parametrize('plane,expected',[('xy',[.75,2,3]),('xz',[.75,3,2]),('yz',[3,.75,2])])
def test_mapped_boundary_stays_on_the_explicit_3d_plane(plane,expected):
    mapped=ij.map_imagej_roi(record(),context(plane,3))
    assert ij.planar_annotation_voxel(mapped.annotation,[1.25,2.5])==expected

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-hyperstack-index-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',REFERENCE['cases'],ids=lambda c:str(c['ordinal']))
def test_actual_imagej_hyperstack_indices_and_legacy_records(case):
    mapping=context();mapping.legacy_position=ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT
    mapped=ij.decode_imagej_roi_mapped(bytes.fromhex(case['hex']),mapping)
    assert (mapped.annotation.t,mapped.annotation.c,mapped.annotation.coordinate)==(case['t'],case['c'],case['z'])
    assert mapped.source_record.legacy_position==case['index'] and mapped.source_record.roi.z is None
