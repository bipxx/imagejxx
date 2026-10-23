"""V3 native source/current curves, standard-wire equations and real workflows."""
import base64
import copy
import json
import struct

import pytest
import imagejxx as ij
from test_imagej_roi_import import owner, EXTENT, ROWS, SOURCES

COMMANDS = [0, -.12345678901234, -3, 2, 3.23456789012345, 8, 8, -3, 4,
            0, 10, 2, 3, 11, 7.34567890123456, 13, -2, 14, 2, 4]


def area(commands=COMMANDS, opaque=False):
    result = ij.Selection.from_path(ij.ImageJCompositePath(commands))
    result = ij.SelectionEditor(result, (8, 12)).properties(2.5, {})
    result = ij.with_roi_style(result, {'stroke_color': '#80112233', 'fill_color': '#40224466'})
    return result.with_attributes({**result.attributes, **({'opaque': [None, True, 2**64-1]} if opaque else {})})


def source(path=True, plane='xy'):
    selection = area() if path else ij.Selection('rectangle', [[1, 1], [3, 3]])
    record = ij.ImageJRoi(ij.ROI('source λ😀', selection, t=0, c=1))
    record.properties = 'original: curves\n'
    record.group = 7
    mapping = ij.ImageJRoiPlaneMapping()
    mapping.extent = EXTENT
    mapping.plane = plane
    mapping.coordinate = 0
    return ij.map_imagej_roi(record, mapping)


def value(source_path=True, current_path=True, plane='xz'):
    row = ij.PlanarAnnotation('current λ😀', area(opaque=True) if current_path else
                             ij.Selection('rectangle', [[1.123456789, 1], [3, 3]]),
                             plane, 0, t=1, c=2, attributes={'opaque-row': ['λ', 2**64-1]})
    return ij.rebind_imagej_roi_mapping(source(source_path), EXTENT, row)


def projection(mapped):
    row = mapped.annotation
    selection = row.selection.with_attributes({k: v for k, v in row.selection.attributes.items()
                                              if k == 'imagejxx.roi_style'})
    result = ij.ImageJRoi(ij.ROI(row.name, selection, t=row.t, c=row.c, z=row.coordinate))
    result.properties = 'projection: chosen\n'
    result.group = 9
    return result


def wire(mapped):
    return ij.encode_imagej_roi_native_envelope(mapped, projection(mapped))


def unpack(data):
    # Standard composite remains explicitly available to other ImageJ readers.
    standard = ij.decode_imagej_roi(data, mask_width=12, mask_height=8)
    version = int(standard.properties.split(':', 1)[0][-1])
    payload = base64.b64decode(standard.properties.split(': ', 1)[1])
    at = 4
    def blob():
        nonlocal at
        size = struct.unpack_from('>I', payload, at)[0]
        at += 4
        result = payload[at:at+size]
        at += size
        return result
    original = blob()
    source_mode = struct.unpack_from('>I', payload, at)[0] if version == 3 else 0
    if version == 3:
        at += 4
    native_source = blob() if version == 3 else b''
    extent = payload[at:at+24]
    at += 24
    row, binding = blob(), blob()
    assert at == len(payload)
    return standard, version, [original, native_source, extent, row, binding, source_mode]


def rebuild(standard, version, parts, magic=None):
    original, source_json, extent, row, binding, source_mode = parts
    blob = lambda b: struct.pack('>I', len(b)) + b
    payload = b'IJX' + bytes([magic or version]) + blob(original)
    if version == 3:
        payload += struct.pack('>I', source_mode) + blob(source_json)
    payload += extent + blob(row) + blob(binding)
    standard.properties = f'imagejxx.roi.v{version}: ' + base64.b64encode(payload).decode() + '\n'
    return ij.encode_imagej_roi(standard)


def encode_json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode()


@pytest.mark.parametrize('source_path,current_path', [(True, True), (True, False), (False, True)])
@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_v3_preserves_full_source_current_precision_style_and_metadata(source_path, current_path, plane):
    mapped = value(source_path, current_path, plane)
    data = wire(mapped)
    standard, version, parts = unpack(data)
    assert version == 3 and bool(parts[1]) == source_path
    for decode in (ij.decode_imagej_roi_envelope, ij.decode_imagej_roi_native_envelope):
        restored = decode(data)
        assert restored.annotation.selection == mapped.annotation.selection
        assert restored.annotation.attributes == mapped.annotation.attributes
        assert restored.annotation.plane == plane
        assert (restored.annotation.t, restored.annotation.c, restored.annotation.coordinate) == (1, 2, 0)
        assert restored.source_record.roi.selection.is_path == source_path
        if source_path:
            assert restored.source_record.roi.selection == mapped.source_record.roi.selection
            assert restored.source_record.composite_path == []
        assert restored.source_record.properties == 'original: curves\n'
        assert restored.source_record.group == 7 and standard.group == 9
        assert ij.encode_imagej_roi_native_envelope(restored, projection(restored)) == data


def test_pure_path_envelope_needs_no_raster_canvas_or_work():
    mapped = value()
    limits = ij.ImageJRoiLimits(max_mask_pixels=0, max_path_work=0)
    data = ij.encode_imagej_roi_native_envelope(mapped, projection(mapped), limits)
    restored = ij.decode_imagej_roi_envelope(data, limits)
    assert restored.annotation.selection == mapped.annotation.selection
    assert restored.source_record.roi.selection.path.commands == COMMANDS
    with pytest.raises(ValueError, match='version3'):
        ij.encode_imagej_roi_envelope(source())


@pytest.mark.parametrize('source_path,current_path', [(True, True), (True, False), (False, True), (False, False)])
def test_versions_follow_actual_geometry_and_do_not_silently_upgrade_legacy(source_path, current_path):
    mapped = value(source_path, current_path)
    standard, version, parts = unpack(wire(mapped))
    assert version == (3 if source_path or current_path else 2)
    wrong = 2 if version == 3 else 3
    payload = bytearray(base64.b64decode(standard.properties.split(': ', 1)[1]))
    payload[3] = wrong
    standard.properties = f'imagejxx.roi.v{wrong}: ' + base64.b64encode(payload).decode() + '\n'
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(ij.encode_imagej_roi(standard))
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(rebuild(standard, version, parts, magic=wrong))


@pytest.mark.parametrize('field', ['path', 'name', 'plane', 'coordinate', 't', 'c', 'attributes'])
def test_original_native_row_must_match_wire_and_mapping(field):
    standard, version, parts = unpack(wire(value()))
    original = json.loads(parts[1])
    row = original['rows'][0]
    if field == 'path':
        row['selection']['path'][1] += 1
    else:
        row[field] = {'name': 'wrong', 'plane': 'YZ', 'coordinate': 1,
                      't': 1, 'c': 2, 'attributes': {'opaque': True}}[field]
    parts[1] = encode_json(original)
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(rebuild(standard, version, parts))


@pytest.mark.parametrize('which', [1, 3])
@pytest.mark.parametrize('replacement', [b'{}', b'{"version":3,"version":3}', b'['*70+b'0'+b']'*70, b''])
def test_strict_source_and_current_json(which, replacement):
    standard, version, parts = unpack(wire(value()))
    parts[which] = replacement
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(rebuild(standard, version, parts))


@pytest.mark.parametrize('mode', [0, 2, 2**32-1])
def test_missing_or_unknown_source_mode_refuses(mode):
    standard, version, parts = unpack(wire(value()))
    assert parts[5] == 1
    parts[5] = mode
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(rebuild(standard, version, parts))


def test_nested_provenance_and_outer_edits_refuse():
    data = wire(value())
    standard, version, parts = unpack(data)
    parts[0] = data
    with pytest.raises(ValueError, match='version1 source'):
        ij.decode_imagej_roi_envelope(rebuild(standard, version, parts))
    standard, version, parts = unpack(data)
    standard.group += 1
    with pytest.raises(ValueError, match='Stale native'):
        ij.decode_imagej_roi_envelope(rebuild(standard, version, parts))


@pytest.mark.parametrize('limit,value_', [('max_bytes', 128), ('max_text_bytes', 128), ('max_vertices', 2)])
def test_codec_limits_apply_before_publication(limit, value_):
    mapped = value()
    limits = ij.ImageJRoiLimits(**{limit: value_})
    with pytest.raises(ValueError):
        ij.encode_imagej_roi_native_envelope(mapped, projection(mapped), limits)
    with pytest.raises(ValueError):
        ij.decode_imagej_roi_envelope(wire(mapped), limits)


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_import_pair_export_replay_history_and_session(plane):
    mapped = value(plane=plane)
    doc, reads = owner()
    before = doc.snapshot()
    item = ij.ImageJRoiImportItem('curves.roi', wire(mapped))
    imported = ij.resolve_imagej_roi_import(before, [item])
    record = imported.record()
    assert record['after']['version'] == 3
    ij.validate_imagej_roi_import_record(record)
    replay = ij.imagej_roi_import_from_record(before, record)
    doc.commit(ij.prepare_imagej_roi_import(replay))
    assert ij.planar_annotations(doc.snapshot())[0].selection == mapped.annotation.selection
    assert len(doc.snapshot().metadata['extensions'][SOURCES]['entries']) == 1
    pair = ij.resolve_imagej_roi_pairing(ij.inspect_imagej_roi_sources(doc.snapshot()), 0, 0)
    ij.validate_imagej_roi_pairing_record(pair.record())
    exported = ij.resolve_imagej_roi_export(doc.snapshot(), pair, projection(mapped))
    ij.validate_imagej_roi_export_record(exported.record())
    assert ij.imagej_roi_export_from_record(doc.snapshot(), exported.record()).data == exported.data
    out = ij.decode_imagej_roi_envelope(exported.data)
    assert out.annotation.selection == mapped.annotation.selection
    assert out.source_record.roi.selection == mapped.source_record.roi.selection
    archive = doc.snapshot().metadata['extensions'][SOURCES]
    assert base64.b64decode(archive['entries'][0]['data']) == item.data
    doc.undo()
    assert ij.planar_annotations(doc.snapshot()) == []
    doc.redo()
    assert ij.planar_annotations(doc.snapshot())[0].selection == mapped.annotation.selection
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    # Session capture deliberately reads pixels; the metadata commands above do not.
    saved = ij.decode_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    assert ij.planar_annotations(saved.documents[0].snapshot())[0].selection == mapped.annotation.selection


@pytest.mark.parametrize('plane', ['xy', 'xz', 'yz'])
def test_explicit_path_replacement_keeps_original_and_replays(plane):
    doc, reads = owner()
    mapped = value()
    replacement = ij.PlanarAnnotation('replacement', area(opaque=True), plane, 1, t=0, c=1)
    item = ij.ImageJRoiImportItem('replacement.roi', wire(mapped), native_replacement=
                                 ij.ImageJRoiNativeReplacement(EXTENT, replacement))
    before = doc.snapshot()
    command = ij.resolve_imagej_roi_import(before, [item])
    ij.validate_imagej_roi_import_record(command.record())
    request = ij.decode_imagej_roi_import_request(command.record())
    assert request.items[0].native_replacement.annotation.selection == replacement.selection
    assert request.items[0].data == item.data
    doc.commit(ij.prepare_imagej_roi_import(ij.imagej_roi_import_from_record(before, command.record())))
    assert ij.planar_annotations(doc.snapshot())[0].selection == replacement.selection
    assert reads.counts().plane_reads == reads.counts().region_reads == 0


def test_import_retention_cap_charges_private_path_storage_and_is_atomic():
    def item(closes):
        commands = [0, 0, 0] + [4]*closes
        original = ij.ImageJRoi(ij.ROI('dense source', area(commands), t=0, c=1))
        context = ij.ImageJRoiPlaneMapping()
        context.extent = EXTENT
        context.coordinate = 0
        mapped = ij.map_imagej_roi(original, context)
        # Only the private source path grows; current row metadata stays fixed.
        r = ij.PlanarAnnotation('rectangle', ij.Selection('rectangle', [[1, 1], [3, 3]]), 'xy', 0)
        mapped = ij.rebind_imagej_roi_mapping(mapped, EXTENT, r)
        return ij.ImageJRoiImportItem('dense.roi', wire(mapped))
    doc, reads = owner()
    before = doc.snapshot()
    small, large = item(2), item(2000)
    low, high = 1, 200000
    while low < high:
        middle = (low+high)//2
        try:
            ij.resolve_imagej_roi_import(before, [small], limits=ij.ImageJRoiImportLimits(max_decoded_bytes=middle))
            high = middle
        except ValueError:
            low = middle+1
    ij.resolve_imagej_roi_import(before, [small], limits=ij.ImageJRoiImportLimits(max_decoded_bytes=low))
    with pytest.raises(ValueError, match='retention byte limit'):
        ij.resolve_imagej_roi_import(before, [large], limits=ij.ImageJRoiImportLimits(max_decoded_bytes=low+10000))
    assert doc.snapshot().shares_data(before) and doc.undo_steps == 0
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
