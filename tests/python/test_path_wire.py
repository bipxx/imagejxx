"""Continuous ImageJ composite transport; pinned Java fixtures remain the oracle."""
import json
import struct
from pathlib import Path

import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] /
                        'fixtures/imagej-roi-composite-imagej154r.json').read_text(encoding='utf-8'))
COMMANDS = [0, -4, -3, 2, 3, 8, 8, -3, 4,
            0, 10, 2, 3, 11, 7, 13, -2, 14, 2, 4]


def record(commands=COMMANDS):
    area = ij.Selection.from_path(ij.ImageJCompositePath(commands))
    area = ij.SelectionEditor(area, (16, 16)).properties(2.5, {})
    row = ij.ImageJRoi(ij.ROI('curves λ😀', area, t=0, c=1, z=2))
    row.properties = 'owned: curves\n'
    row.group = 7
    return row


def no_raster():
    return ij.ImageJRoiLimits(max_mask_pixels=0, max_path_work=0)


@pytest.mark.parametrize('case', REFERENCE['cases'], ids=lambda c: str(c['index']))
def test_pinned_imagej_paths_without_mask_admission(case):
    original = bytes.fromhex(case['hex'])
    row = ij.decode_imagej_composite_roi(original, no_raster())
    assert row.roi.selection.kind == 'path' and row.roi.selection.is_path
    assert row.roi.selection.path.commands == row.composite_path == case['path']
    assert row.roi.selection.points == []
    assert row.roi.name == 'composite' and row.properties == 'owned: composite\n'
    wire = ij.encode_imagej_roi(row, no_raster())
    assert ij.decode_imagej_composite_roi(wire, no_raster()).composite_path == case['path']
    # Explicit mask consumption still matches actual ImageJ, including holes,
    # islands, curves, implicit closures and off-canvas fixture geometry.
    area = ij.decode_imagej_roi(original, mask_width=12, mask_height=10).roi.selection
    assert row.roi.selection.mask((10, 12)).tolist() == area.mask((10, 12)).tolist()


def test_wire_layout_and_binary32_rounding_are_explicit():
    commands = [0, -.123456789, 1.23456789, 2, 2.3456789, -3.456789, 4.56789, 5.6789, 4]
    row = record(commands)
    wire = ij.encode_imagej_roi(row, no_raster())
    assert wire[:4] == b'Iout' and wire[6] == 1
    assert struct.unpack_from('>I', wire, 36)[0] == len(commands)
    expected = list(struct.unpack('>' + 'f' * len(commands),
                                 struct.pack('>' + 'f' * len(commands), *commands)))
    assert list(struct.unpack_from('>' + 'f' * len(commands), wire, 64)) == expected
    back = ij.decode_imagej_composite_roi(wire, no_raster())
    assert back.roi.selection.path.commands == expected != commands
    assert row.roi.selection.path.commands == commands  # input remains binary64
    assert back.roi.name == row.roi.name and back.properties == row.properties and back.group == 7
    assert (back.roi.t, back.roi.c, back.roi.z) == (0, 1, 2)
    assert ij.roi_style(back.roi.selection).stroke_width == 2.5
    assert ij.encode_imagej_roi(back, no_raster()) == wire


def test_stale_retained_path_requires_explicit_clear_after_native_edit():
    row = ij.decode_imagej_composite_roi(ij.encode_imagej_roi(record()))
    moved = ij.SelectionEditor(row.roi.selection, (16, 16)).moved([.25, -.75])
    old = row.roi
    row.roi = ij.ROI(old.name, moved, t=old.t, c=old.c, z=old.z, attributes=old.attributes)
    with pytest.raises(ValueError, match='differs from native'):
        ij.encode_imagej_roi(row, no_raster())
    row.composite_path = []
    back = ij.decode_imagej_composite_roi(ij.encode_imagej_roi(row, no_raster()))
    assert back.roi.selection.path.commands == moved.path.commands


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_explicit_plane_mapping_preserves_path_and_legacy_mask_mode(plane):
    wire = ij.encode_imagej_roi(record())
    mapping = ij.ImageJRoiPlaneMapping()
    mapping.extent = [2, 2, 5, 8, 16, 1]
    mapping.plane = plane
    mapping.z_position = ij.ImageJZPositionMapping.FIXED_AXIS
    mapped = ij.decode_imagej_composite_roi_mapped(wire, mapping, no_raster())
    ij.validate_imagej_roi_mapping(mapped, no_raster())
    assert mapped.annotation.plane == plane and mapped.annotation.coordinate == 2
    assert mapped.annotation.selection.path.commands == COMMANDS
    assert mapped.source_record.composite_path == COMMANDS
    assert (mapped.annotation.t, mapped.annotation.c) == (0, 1)
    # Old entry point still requests a mask, never silently changes type.
    old = ij.decode_imagej_roi_mapped(wire, mapping)
    assert old.annotation.selection.kind == 'mask'


@pytest.mark.parametrize('key,value', [('max_vertices', 2), ('max_bytes', 128)])
def test_wire_resource_admission_on_both_directions(key, value):
    limits = ij.ImageJRoiLimits(**{key: value})
    with pytest.raises(ValueError):
        ij.decode_imagej_composite_roi(ij.encode_imagej_roi(record()), limits)
    with pytest.raises(ValueError):
        ij.encode_imagej_roi(record(), limits)


@pytest.mark.parametrize('kind', ['rectangle', 'ellipse', 'line', 'polygon'])
def test_composite_only_decoder_refuses_other_geometries(kind):
    points = [[0, 0], [4, 4], [2, 0]] if kind == 'polygon' else [[0, 0], [4, 4]]
    row = ij.ImageJRoi(ij.ROI('', ij.Selection(kind, points)))
    with pytest.raises(ValueError, match='Expected ImageJ composite'):
        ij.decode_imagej_composite_roi(ij.encode_imagej_roi(row))


@pytest.mark.parametrize('offset,fmt,value', [(4, '>H', 229), (6, '>B', 0),
    (36, '>I', 0), (48, '>H', 1), (64, '>f', 1), (68, '>f', float('nan'))])
def test_malformed_path_and_header_refuse(offset, fmt, value):
    wire = bytearray(ij.encode_imagej_roi(record()))
    struct.pack_into(fmt, wire, offset, value)
    with pytest.raises(ValueError):
        ij.decode_imagej_composite_roi(bytes(wire))


def test_all_truncations_and_opaque_attributes_refuse():
    wire = ij.encode_imagej_roi(record())
    for end in range(len(wire)):
        with pytest.raises(ValueError):
            ij.decode_imagej_composite_roi(wire[:end])
    row = record()
    area = ij.SelectionEditor(row.roi.selection, (16, 16)).properties(2.5, {'opaque': True})
    row.roi = ij.ROI(row.roi.name, area)
    with pytest.raises(ValueError, match='explicit interchange mapping'):
        ij.encode_imagej_roi(row)
