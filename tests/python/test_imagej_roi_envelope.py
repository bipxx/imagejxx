"""Self describing standard-wire bindings, never an implicit target remap."""
import base64
import json
from pathlib import Path
import struct
import pytest
import imagejxx as ij

EXTENT = [3, 4, 5, 7, 11, 3]

def mapped(plane='xy', coordinate=0, kind='rectangle'):
    m = ij.ImageJRoiPlaneMapping()
    m.extent = EXTENT
    m.plane = plane
    m.coordinate = coordinate
    r = ij.ImageJRoi(ij.ROI('λ😀', ij.Selection(kind, [(1.25, 2.5), (3.75, 4.5)]), t=0, c=2))
    r.properties = 'opaque: λ😀\nnote: imagejxx.roi.v1 as a value\n'
    r.group = 7
    r.overlay_font_size = 14
    r.overlay_label_color = 0xff123456
    return ij.map_imagej_roi(r, m)

def wire_and_record(value=None):
    wire = ij.encode_imagej_roi_envelope(value or mapped())
    return wire, ij.decode_imagej_roi(wire)

def change_payload(transform):
    _, r = wire_and_record()
    raw = bytearray(base64.b64decode(r.properties.split(': ', 1)[1]))
    raw = transform(raw)
    r.properties = 'imagejxx.roi.v1: ' + base64.b64encode(raw).decode() + '\n'
    return ij.encode_imagej_roi(r)

@pytest.mark.parametrize('plane,size', [('xy', 5), ('xz', 7), ('yz', 11)])
@pytest.mark.parametrize('kind', ['rectangle', 'ellipse', 'line', 'polyline'])
def test_all_planes_wire_metadata_and_first_last_coordinate(plane, size, kind):
    for coordinate in (0, size-1):
        original = mapped(plane, coordinate, kind)
        wire, standard = wire_and_record(original)
        restored = ij.decode_imagej_roi_envelope(wire)
        assert restored.annotation.plane == plane
        assert restored.annotation.coordinate == coordinate
        assert restored.annotation.selection.points == original.annotation.selection.points
        assert restored.source_record.properties == original.source_record.properties
        assert restored.source_record.group == 7
        assert restored.source_record.overlay_label_color == 0xff123456
        assert restored.source_record.overlay_font_size == 14
        assert list(restored.source_mapping.extent) == EXTENT
        assert (restored.annotation.t, restored.annotation.c) == (0, 2)
        assert standard.properties.isascii()
        assert ij.encode_imagej_roi_envelope(restored) == wire
        with pytest.raises(ValueError, match='envelope decoder'):
            ij.decode_imagej_roi_mapped(wire, original.source_mapping)

@pytest.mark.parametrize('field,value', [('name','edited'), ('t',1), ('c',None), ('z',0)])
def test_external_row_edit_is_stale(field, value):
    _, r = wire_and_record()
    row = r.roi
    fields = dict(name=row.name, t=row.t, c=row.c, z=row.z)
    fields[field] = value
    r.roi = ij.ROI(selection=row.selection, attributes=row.attributes, **fields)
    with pytest.raises(ValueError, match='Stale'):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(r))

@pytest.mark.parametrize('field,value', [('group',8), ('overlay_font_size',15), ('overlay_label_color',0xff112233)])
def test_external_display_metadata_edit_is_stale(field, value):
    _, r = wire_and_record()
    setattr(r, field, value)
    with pytest.raises(ValueError, match='Stale'):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(r))

def test_external_geometry_edit_is_stale():
    _, r = wire_and_record()
    row = r.roi
    r.roi = ij.ROI(row.name, ij.Selection('rectangle', [(0,0),(2,2)]),
                   t=row.t, c=row.c, z=row.z, attributes=row.attributes)
    with pytest.raises(ValueError, match='Stale'):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(r))

@pytest.mark.parametrize('properties', ['', 'imagejxx.roi.v2: AAAA\n', 'imagejxx.roi.v1: AAAA\nextra: value\n', 'imagejxx.roi.v1: AA==', 'imagejxx.roi.v1: AB==\n'])
def test_absent_unknown_extra_noncanonical_envelope_refuses(properties):
    r = mapped().source_record
    r.properties = properties
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(r))

@pytest.mark.parametrize('offset,value', [(0,0), (4,0), (4,2**31), (24,0), (28,3), (32,2**31), (36,2), (40,3)])
def test_invalid_schema_extent_plane_coordinate_policy(offset, value):
    def edit(raw):
        struct.pack_into('>I', raw, offset, value)
        return raw
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(change_payload(edit))

@pytest.mark.parametrize('amount', [1,4,20,52,10000])
def test_truncated_payload(amount):
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(change_payload(lambda raw: raw[:-amount]))

def test_trailing_fields_and_properties_disagreement():
    with pytest.raises(ValueError, match='Trailing'):
        ij.decode_imagej_roi_envelope(change_payload(lambda raw: raw + b'junk'))
    def edit(raw):
        raw[48] ^= 1
        return raw
    with pytest.raises(ValueError, match='properties disagree'):
        ij.decode_imagej_roi_envelope(change_payload(edit))

def test_rebinding_requires_new_standard_projection():
    original = mapped()
    replacement = ij.PlanarAnnotation('new', ij.Selection('rectangle',[(0,0),(2,2)]), 'yz', 0)
    rebound = ij.rebind_imagej_roi_mapping(original, EXTENT, replacement)
    with pytest.raises(ValueError, match='standard projection'):
        ij.encode_imagej_roi_envelope(rebound)

def test_limits_and_no_partial_admission():
    wire, _ = wire_and_record()
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(wire, ij.ImageJRoiLimits(max_bytes=len(wire)-1))
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(wire, ij.ImageJRoiLimits(max_text_bytes=40))
    with pytest.raises(ValueError):
        ij.encode_imagej_roi_envelope(mapped(), ij.ImageJRoiLimits(max_text_bytes=128))
    for n in (0, 4, 63, 64, len(wire)-1):
        with pytest.raises(ValueError):
            ij.decode_imagej_roi_envelope(wire[:n])

@pytest.mark.parametrize('plane,width,height', [('xy',11,7), ('xz',11,5), ('yz',7,5)])
def test_enveloped_composite_uses_original_plane_canvas(plane,width,height):
    path=[0,1,1,1,3,1,1,3,3,1,1,3,4]
    row=ij.ImageJRoi(ij.ROI('mask',ij.imagej_shape_mask(path,width,height)))
    row.composite_path=path
    context=ij.ImageJRoiPlaneMapping();context.extent=EXTENT;context.plane=plane;context.coordinate=0
    restored=ij.decode_imagej_roi_envelope(ij.encode_imagej_roi_envelope(ij.map_imagej_roi(row,context)))
    assert restored.annotation.plane==plane
    assert restored.annotation.selection.mask((height,width)).tobytes()==row.roi.selection.mask((height,width)).tobytes()
    assert restored.source_record.composite_path==path

@pytest.mark.parametrize('policy', [ij.ImageJLegacyPositionMapping.FIXED_AXIS, ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT])
def test_legacy_policy_and_optional_coordinate_are_not_guessed(policy):
    row=mapped().source_record
    row.roi=ij.ROI(row.roi.name,row.roi.selection)
    row.legacy_position=1
    context=ij.ImageJRoiPlaneMapping();context.extent=EXTENT;context.legacy_position=policy
    restored=ij.decode_imagej_roi_envelope(ij.encode_imagej_roi_envelope(ij.map_imagej_roi(row,context)))
    assert restored.source_mapping.coordinate is None
    assert restored.source_mapping.legacy_position==policy
    assert restored.source_record.legacy_position==1
    assert restored.annotation.coordinate==0
    expected=(0,0) if policy==ij.ImageJLegacyPositionMapping.HYPERSTACK_CZT else (None,None)
    assert (restored.annotation.t,restored.annotation.c)==expected

def test_ignored_point_flag_or_added_zero_counters_are_still_metadata_edits():
    original=mapped(kind='multipoint')
    wire=ij.encode_imagej_roi_envelope(original)
    row=ij.decode_imagej_roi(wire)
    row.options &= ~8192
    row.point_counters=[0]*len(row.roi.selection.points)
    with pytest.raises(ValueError,match='Stale'):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(row))

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/imagej-roi-envelope-imagej154r.json').read_text(encoding='utf-8'))
@pytest.mark.parametrize('case',REFERENCE['cases'],ids=lambda c:f"{c['ordinal']}-{c['plane']}-{c['kind']}")
def test_actual_imagej_preserves_envelope_and_rejects_edited_standard_record(case):
    value=ij.decode_imagej_roi_envelope(bytes.fromhex(case['unchanged']))
    assert value.annotation.plane==case['plane'] and value.annotation.coordinate==case['coordinate']
    assert list(value.target_extent)==EXTENT
    assert value.source_record.properties=='opaque: λ😀\nnote: imagejxx.roi.v1 in a value\n'
    assert (value.annotation.t,value.annotation.c)==(0,2)
    with pytest.raises(ValueError,match='Stale'):
        ij.decode_imagej_roi_envelope(bytes.fromhex(case['edited']))
