"""Canonical region values, versioned files and projection-only pixel consumption."""
import base64
import copy
import json
import struct
import zlib
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.model import Selection as UiSelection, Document as UiDocument
from imagejxx.ui import persistence
from test_selection_region import FIXTURE, SOURCES, sample
from test_selection_enlargement import geometry


def stored():
    pixels = np.zeros((9, 9), np.uint8)
    pixels[0, 0] = 255
    base = ij.SelectionEditor(ij.Selection.from_mask(pixels), (9, 9)).properties(
        3, {'origin': 'opaque attribute', 'attributes': {'future': [True, 2**64-1]}})
    return ij.SelectionRegion(base, (9, 9)).enlarged(2).as_selection()


def owner(kind='u16'):
    dtype = np.uint8 if kind in ('u8', 'rgb') else np.float32 if kind in ('float', 'quiet', 'ieee') else np.uint16
    shape = (2, 2, 2, 9, 9, 3 if kind == 'rgb' else 1)
    values = np.full(shape, 30, dtype)
    policy = ij.FloatingSamplePolicy.FINITE_ONLY
    if kind in ('quiet', 'ieee'):
        policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if kind == 'quiet' else ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
        words = [0x7fc12345, 0x80000000, 1] if kind == 'quiet' else [0x7fc12345, 0x7f800000, 0xff800000, 0x80000000, 1]
        values.view(np.uint32).flat[:len(words)] = words
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(values, axes='TCZYXS', sample_policy=policy).observe_reads(reads))
    return doc, reads, values


def unpack_session(data):
    raw = zlib.decompress(data[18:])
    assert len(raw) == struct.unpack('<Q', data[10:18])[0]
    return json.loads(raw)


def pack_session(root, version=4):
    raw = json.dumps(root, separators=(',', ':'), allow_nan=False).encode()
    return f'IMAGEJXX{version}\n'.encode()+struct.pack('<Q', len(raw))+zlib.compress(raw)


@pytest.mark.parametrize('kind', ['u8', 'u16', 'float', 'rgb', 'quiet', 'ieee'])
def test_canonical_region_and_v4_session_preserve_all_source_words(kind):
    doc, reads, values = owner(kind)
    before = doc.snapshot()
    area = stored()
    assert area.kind == 'region' and area.is_region and area.mask_origin == [-3, -3]
    doc.select(area)
    assert doc.undo_steps == 0 and not doc.snapshot().dirty
    assert doc.snapshot().dataset.shares_pixels(before.dataset)
    assert reads.counts().plane_reads == reads.counts().region_reads == 0
    capture = ij.SessionCapture([doc.snapshot()])
    estimate = ij.estimate_session(capture)
    data = ij.encode_session(capture)
    root = unpack_session(data)
    assert data[:10] == b'IMAGEJXX4\n' and root['version'] == 4
    assert estimate.json_bytes == len(zlib.decompress(data[18:]))
    assert root['documents'][0]['selection'] == area.to_dict()
    decoded = ij.decode_session(data)
    assert decoded.source_version == 4
    snapshot = decoded.documents[0].snapshot()
    assert snapshot.selection == area and snapshot.selection.attributes == area.attributes
    actual = snapshot.dataset.numpy('TCZYXS')
    np.testing.assert_array_equal(actual.view(np.uint32) if values.dtype == np.float32 else actual,
                                  values.view(np.uint32) if values.dtype == np.float32 else values)
    assert ij.SelectionRegion(snapshot.selection, (9, 9)).bounds == [-2, -2, 3, 3]
    assert np.count_nonzero(snapshot.selection.mask((9, 9))) == 8


@pytest.mark.parametrize('carrier', ['active', 'previous', 'saved_roi'])
def test_all_typed_selection_carriers_choose_v4_and_reopen_without_clipping(carrier):
    doc, _, _ = owner()
    area = stored()
    if carrier == 'saved_roi':
        edit = doc.begin_edit()
        edit.set_rois([ij.ROI('Full region', area, t=1, c=1, z=1)])
        doc.commit(edit)
    else:
        doc.select(area)
        if carrier == 'previous': doc.select(ij.Selection('rectangle', [(4, 4), (6, 6)]))
    decoded = ij.decode_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    snapshot = decoded.documents[0].snapshot()
    result = snapshot.selection if carrier == 'active' else snapshot.previous_selection if carrier == 'previous' else snapshot.rois[0].selection
    assert decoded.source_version == 4 and result == area
    shrunk = ij.SelectionEditor(result, (9, 9)).enlarged(-2)
    assert np.count_nonzero(shrunk.mask((9, 9))) == 1


@pytest.mark.parametrize('kind,sequence', [(kind, seq) for kind in range(9) for seq in range(8)])
def test_complete_imagej_geometry_survives_each_document_session_boundary(kind, sequence):
    doc = ij.Document(ij.Dataset(np.zeros((40, 48), np.uint8), axes='YX'))
    region = ij.SelectionRegion(geometry(SOURCES[kind]), (40, 48))
    observations = [row for row in FIXTURE['cases'] if row['kind'] == kind and row['sequence'] == sequence]
    for row in observations:
        result = region.enlarged(row['distance']).as_selection()
        doc.select(result)
        data = ij.encode_session(ij.SessionCapture([doc.snapshot()]))
        restored = ij.decode_session(data).documents[0].snapshot().selection
        assert restored == result
        has_region = result.is_region or (doc.snapshot().previous_selection is not None and doc.snapshot().previous_selection.is_region)
        assert data[:10] == (b'IMAGEJXX4\n' if has_region else b'IMAGEJXX3\n')
        region = ij.SelectionRegion(restored, (40, 48))
        _, _, width, height = row['sample']
        expected = np.unpackbits(np.frombuffer(base64.b64decode(row['mask']), np.uint8))[:width*height].reshape(height, width)
        np.testing.assert_array_equal(sample(region, row['sample']), expected)


@pytest.mark.parametrize('delta,expected', [((2, 3), (-1, 0)), ((.5, -.5), (-3, -4)), ((-.49, .51), (-3, -2))])
def test_native_translation_uses_full_frame_and_legacy_nearest_ties(delta, expected):
    area = stored()
    moved = ij.SelectionEditor(area, (9, 9)).moved(delta)
    assert moved.kind == 'region' and moved.mask_origin == list(expected)
    assert ij.SelectionRegion(moved, (9, 9)).local_selection.mask((7, 7)).sum() == 21*255
    assert moved.attributes == area.attributes and moved.width == 3


def test_pixel_filter_consumes_only_visible_mask_and_keeps_canonical_geometry():
    doc, reads, values = owner()
    doc.select(stored())
    before = doc.snapshot()
    target = ij.TargetRequest(ij.TargetScope.CURRENT_PLANE, roi=ij.RoiPolicy.ACTIVE_SELECTION)
    prepared = ij.prepare_operation('add', ij.resolve_target(before, target), {'value': 7})
    assert prepared.preview.selection == before.selection
    expected = values.copy()
    mask = before.selection.mask((9, 9)) != 0
    expected[0, 0, 0][mask] += 7
    np.testing.assert_array_equal(prepared.preview.dataset.numpy('TCZYXS'), expected)
    assert doc.snapshot().shares_data(before)
    doc.commit(prepared.begin_edit())
    assert doc.undo_steps == 1 and doc.snapshot().selection == before.selection
    doc.undo()
    assert doc.snapshot().shares_data(before)


@pytest.mark.parametrize('bad', ['origin_bool', 'origin_float', 'origin_large', 'origin_small', 'origin_size',
    'shape', 'payload', 'padding', 'points', 'extra', 'attributes', 'width'])
def test_regional_value_codec_is_closed_and_exact(bad):
    record = stored().to_dict()
    if bad == 'origin_bool': record['origin'][0] = True
    elif bad == 'origin_float': record['origin'][0] = -3.
    elif bad == 'origin_large': record['origin'][0] = 2**31
    elif bad == 'origin_small': record['origin'][0] = -2**31-1
    elif bad == 'origin_size': record['origin'] = [0]
    elif bad == 'shape': record['shape'][0] = 0
    elif bad == 'payload': record['mask'] = record['mask'][:-1]
    elif bad == 'padding':
        raw = bytearray(base64.b64decode(record['mask']));raw[-1] |= 1
        record['mask'] = base64.b64encode(raw).decode()
    elif bad == 'points': record['points'] = [[0, 0]]
    elif bad == 'extra': record['future'] = 1
    elif bad == 'attributes': record['attributes']['kind'] = 'mask'
    else: record['width'] = 0
    with pytest.raises((ValueError, TypeError, OverflowError)):
        value = ij.Selection.from_dict(record)
        value.mask((9, 9))


@pytest.mark.parametrize('version', [2, 3])
def test_old_file_grammars_do_not_accept_regional_payloads(version):
    doc, _, _ = owner()
    doc.select(stored())
    root = unpack_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    if version == 3:
        root['version'] = 3
        root['documents'][0]['planes'][0] = 'bad'
        with pytest.raises(ValueError, match='version 4'):
            ij.decode_session(pack_session(root, 3))
    else:
        # Public V2 decode stays closed even though the explicit facade upgrade
        # can accept its own private intermediate container.
        item = dict(shape=[9, 9], dtype='uint8', is_stack=False,
                    pixels=base64.b64encode(np.zeros(81, '<f8').tobytes()).decode(),
                    selection=stored().to_dict())
        root = dict(format='imagejxx-session', version=2, documents=[item], results=[], workflow=[])
        raw = json.dumps(root).encode()
        with pytest.raises(ValueError, match='version 4'):
            ij.decode_session(b'IMAGEJXX2\n'+struct.pack('>I', len(raw))+zlib.compress(raw))


def test_legacy_only_and_opaque_region_looking_metadata_keep_v3():
    doc, _, _ = owner()
    edit = doc.begin_edit();edit.set_extension('opaque', {'kind': 'region', 'origin': [-3, -3], 'shape': [7, 7]});doc.commit(edit)
    data = ij.encode_session(ij.SessionCapture([doc.snapshot()]))
    assert data[:10] == b'IMAGEJXX3\n' and ij.decode_session(data).source_version == 3


def test_python_model_and_persistence_use_native_region_values_and_v4(tmp_path):
    area = stored()
    model = UiSelection.from_dict(area.to_dict())
    assert model.origin == (-3, -3) and model.to_dict() == area.to_dict()
    assert model.native() == area and model.bounds((9, 9)) == (-2, -2, 5, 5)
    assert model.moved(2, 3, (9, 9)).origin == (-1, 0)
    document = UiDocument(np.zeros((9, 9), np.uint8))
    document.selection = model
    path = tmp_path/'region.ijx'
    persistence.save_session(path, [document])
    assert path.read_bytes()[:10] == b'IMAGEJXX4\n'
    native = ij.load_session(str(path))
    assert native.documents[0].snapshot().selection == area
    docs, results, workflow = persistence.load_session(path)
    assert docs[0].selection.to_dict() == area.to_dict() and results == workflow == []


def test_remaining_unported_transforms_refuse_without_geometry_loss():
    area = stored()
    rotated = ij.SelectionEditor(area, (9, 9)).rotated(30)
    assert rotated.is_region and rotated.attributes == area.attributes
    assert rotated.width == area.width
    with pytest.raises(ValueError): ij.combine_selections(9, 9, [area, area], 'union')
    assert ij.SelectionRegion(area, (9, 9)).bounds == [-2, -2, 3, 3]


@pytest.mark.parametrize('carrier', ['active', 'previous', 'saved_roi'])
@pytest.mark.parametrize('bad', ['origin', 'width', 'padding', 'attributes'])
def test_v4_rejects_invalid_region_in_every_typed_carrier_before_pixel_decode(carrier, bad):
    doc, _, _ = owner()
    doc.select(stored())
    root = unpack_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    item = root['documents'][0]
    area = item['selection']
    item['selection'] = None
    if bad == 'origin': area['origin'][0] = False
    elif bad == 'width': area['width'] = 0
    elif bad == 'padding':
        raw = bytearray(base64.b64decode(area['mask']));raw[-1] |= 1
        area['mask'] = base64.b64encode(raw).decode()
    else: area['attributes']['kind'] = 'mask'
    if carrier == 'saved_roi': item['rois'] = [dict(name='full', selection=area, t=None, c=None, z=None, attributes={})]
    else: item['selection' if carrier == 'active' else 'previous_selection'] = area
    item['planes'][0] = 'bad'
    messages = dict(origin='exact signed integers', width='positive', padding='unused', attributes='geometry field')
    with pytest.raises(ValueError, match=messages[bad]): ij.decode_session(pack_session(root))


def test_regional_codec_limits_and_cancelled_atomic_save_leave_owner_and_destination(tmp_path):
    doc, reads, _ = owner()
    doc.select(stored())
    snapshot = doc.snapshot()
    capture = ij.SessionCapture([snapshot])
    estimate = ij.estimate_session(capture)
    limits = ij.SessionLimits();limits.max_json_bytes = estimate.json_bytes
    assert unpack_session(ij.encode_session(capture, limits))['version'] == 4
    limits.max_json_bytes -= 1
    with pytest.raises((ValueError, OverflowError)): ij.encode_session(capture, limits)
    token = ij.CancellationToken();token.cancel()
    path = tmp_path/'kept.ijx';path.write_bytes(b'previous destination')
    with pytest.raises(ij.OperationCancelled): ij.save_session(str(path), capture, cancellation=token)
    assert path.read_bytes() == b'previous destination'
    assert doc.snapshot().shares_data(snapshot) and doc.undo_steps == 0


def test_previous_region_can_belong_to_an_earlier_extent_but_requires_valid_intrinsic_geometry():
    doc = ij.Document(ij.Dataset(np.zeros((2, 2), np.uint8), axes='YX'))
    edit = doc.begin_edit();edit.set_previous_selection(stored());doc.commit(edit)
    loaded = ij.decode_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    assert loaded.documents[0].snapshot().previous_selection == stored()
    invalid = ij.Selection('region', [], 0)
    edit = doc.begin_edit()
    with pytest.raises(ValueError, match='positive'): edit.set_previous_selection(invalid)


def test_region_is_an_area_for_fill_style_and_visible_stroke_consumers():
    area = ij.SelectionEditor(stored(), (9, 9)).roi_style({'fill_color': '#80123456'})
    style = ij.resolve_roi_style(area)
    assert style.fill_color == [18, 52, 86, 128] and style.stroke_color is None
    assert area.kind == 'region' and area.mask_origin == [-3, -3]
    doc, _, _ = owner();doc.select(area)
    restored = ij.decode_session(ij.encode_session(ij.SessionCapture([doc.snapshot()])))
    assert ij.resolve_roi_style(restored.documents[0].snapshot().selection).fill_color == style.fill_color


@pytest.mark.parametrize('carrier', ['active', 'previous', 'saved_roi'])
@pytest.mark.parametrize('native_origin', [False, True])
def test_detached_facade_preserves_all_regional_carriers_and_native_markers(tmp_path, carrier, native_origin):
    from imagejxx.ui.model import NamedROI
    area = stored()
    if native_origin:
        owner = ij.Document(ij.Dataset(np.zeros((9, 9), np.uint8), axes='YX'))
        if carrier == 'active': owner.select(area)
        else:
            edit = owner.begin_edit()
            if carrier == 'previous': edit.set_previous_selection(area)
            else: edit.set_rois([ij.ROI('full', area)])
            owner.commit(edit)
        path = tmp_path/'source.ijx'
        ij.save_session(str(path), ij.SessionCapture([owner.snapshot()]))
        document = persistence.load_session(path)[0][0]
    else:
        document = UiDocument(np.zeros((9, 9), np.uint8))
        model = UiSelection.from_dict(area.to_dict())
        if carrier == 'active': document.selection = model
        elif carrier == 'previous': document.previous_selection = model
        else: document.rois = [NamedROI('full', model)]
    path = tmp_path/'saved.ijx'
    persistence.save_session(path, [document])
    assert path.read_bytes()[:10] == b'IMAGEJXX4\n'
    restored = ij.load_session(str(path)).documents[0].snapshot()
    value = restored.selection if carrier == 'active' else restored.previous_selection if carrier == 'previous' else restored.rois[0].selection
    assert value == area


@pytest.mark.parametrize('point,inside', [((.5, .5), True), ((-.5, -.5), True), ((-3.5, -3.5), False)])
def test_region_hit_testing_uses_complete_membership_including_off_canvas(point, inside):
    hit = ij.SelectionEditor(stored(), (9, 9)).hit_test(point, (.1, .1))
    assert (hit is not None) == inside
    if hit is not None: assert hit.kind == ij.SelectionHandleKind.MOVE
