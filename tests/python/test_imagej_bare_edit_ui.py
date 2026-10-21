"""Bare-source native edits through physical input in both desktop hosts."""
import base64
import json
import os
from pathlib import Path
import zipfile

import pytest
import imagejxx as ij
from imagejxx import _desktop
from test_display_annotations_ui import png_rgb

SOURCES = 'imagejxx.imagej_roi_import_sources'
GEOMETRY = 'imagejxx.planar_annotations'
DISPLAY = 'imagejxx.planar_display_annotations'
EXTENT = [1, 1, 4, 12, 16, 1]
TARGET = [2, 1, 6, 10, 18, 1]


@pytest.fixture(scope='module')
def reports(tmp_path_factory):
    cpp = json.loads(Path(os.environ['IMAGEJXX_BARE_EDIT_CPP_REPORT']).read_bytes())
    directory = tmp_path_factory.mktemp('bare-edit-ui') / 'capture001'
    editor = _desktop.Editor(str(directory.parent / 'preferences.json'))
    python = json.loads(editor._bare_edit_ui_case(str(directory)))
    assert python == json.loads((directory / 'case.json').read_bytes())
    return cpp, python


def test_physical_controls_private_drafts_and_single_publication(reports):
    for r in reports:
        assert r['contract'] == 'p06q.bare-edit.ui.001'
        assert r['state'] == 'observed' and r['error'] == '' and not r['acceptance_claim']
        assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in r['frames'])
        assert all(c['control']['id'] and c['control']['enabled'] for c in r['controls'])
        s = r['states']
        assert s['before'] == s['private_applied'] == s['review']
        assert s['applied']['undo'] == s['before']['undo'] + 1
        assert len(s['applied']['workflow']) == 1 and r['shared_pixels']
        assert s['redone']['metadata'] == s['applied']['metadata'] == s['discarded']['metadata']
        assert s['redone']['workflow'] == s['discarded']['workflow']
        reads = r['private_open_reads']
        assert reads['before_plane'] == reads['after_plane']
        assert reads['before_region'] == reads['after_region']


def test_original_first_admission_and_frozen_mapping_controls(reports):
    for r in reports:
        assert r['mapping_refusal']['replacement_editor'] is None and r['mapping_refusal']['error']
        assert r['color_refusal']['replacement_editor'] is None and r['color_refusal']['error']
        assert r['invalid_payload']['error']
        assert r['geometry_staged']['entries'][0]['mapping_frozen']
        assert not r['geometry_staged']['controls']['plane']['enabled']
        assert not r['reset']['entries'][0]['mapping_frozen']
        assert not r['reset']['entries'][0]['native_edit'] and r['reset']['controls']['plane']['enabled']
        entries = r['states']['applied']['metadata'][SOURCES]['entries']
        assert entries[1]['source']['mapping']['text_default_color'] == 0xff112233
        assert entries[0]['source']['mapping']['plane'] == 'xy'
        assert entries[1]['source']['mapping']['plane'] == 'XY'
        assert entries[2]['source']['mapping']['plane'] == 'XZ'


def test_exact_original_bytes_are_independent_from_native_geometry(reports):
    for r in reports:
        metadata = r['states']['applied']['metadata']
        archive = metadata[SOURCES]
        assert archive['version'] == 4
        with zipfile.ZipFile(Path(r['directory']) / 'bare.zip') as original:
            for entry in archive['entries']:
                source = entry['source']
                assert base64.b64decode(source['data']) == original.read(source['name'])
                assert source['mapping']['extent'] == source['native_replacement']['extent'] == EXTENT
        row = metadata[GEOMETRY]['rows'][0]
        assert row == archive['entries'][0]['source']['native_replacement']['annotation']
        assert row['name'] == 'Edited geometry' and row['plane'] == 'YZ' and row['coordinate'] == 1
        assert row['selection']['kind'] == 'point'
        assert row['selection']['points'] == [[1.250000000001, 2.500000000001]]
        source = archive['entries'][0]['source']
        assert ij.decode_imagej_roi(base64.b64decode(source['data'])).roi.name == 'Source geometry'


def test_committed_text_payload_precision_and_ignored_unapplied_fields(reports):
    for r in reports:
        row = r['states']['applied']['metadata'][DISPLAY]['rows'][1]
        assert row['name'] == 'Edited text' and row['plane'] == 'XZ' and row['coordinate'] == 1
        p = row['payload']
        assert p['text'] == 'Reviewed λ😀\nLine' and p['font_name'] == 'SansSerif'
        assert p['font_size'] == 3.500000000001 and p['angle'] == 17.250000000001
        assert p['bounds'] == [1.250000000001, .5, 9., 3.]
        assert p['color'] == 0x803366cc and p['background'] is None
        assert p['font_style'] == 3 and p['justification'] == 2 and p['draw_string_mode']
        source = r['states']['applied']['metadata'][SOURCES]['entries'][1]['source']
        assert source['native_replacement']['rows'] == [row]
        assert ij.decode_imagej_text_roi(base64.b64decode(source['data'])).text == 'Original'


def test_rgba_template_attributes_and_image_payload_properties(reports):
    for r in reports:
        metadata = r['states']['applied']['metadata']
        row = metadata[DISPLAY]['rows'][2]
        assert row['name'] == 'Edited image' and row['attributes'] == {'opaque': 42}
        assert row['plane'] == 'XY' and row['coordinate'] == 0
        p = row['payload']
        assert p['bounds'] == [4.250000000001, 2., 5.5, 2.]
        assert [p['width'], p['height'], p['channels'], p['opacity']] == [2, 1, 4, 90]
        assert base64.b64decode(p['pixels']) == bytes([255, 0, 0, 128, 0, 0, 255, 255])
        assert p['zero_transparent']
        source = metadata[SOURCES]['entries'][2]['source']
        assert source['native_replacement']['rows'] == [row]
        assert ij.decode_imagej_image_roi(base64.b64decode(source['data'])).pixels.tolist() == [[[1, 2, 3], [4, 5, 6]]]


def test_rebind_preserves_old_basis_and_session_preserves_native_rows(reports):
    for r in reports:
        s = r['states']
        assert s['rebound']['workflow'] == s['applied']['workflow']
        original = s['applied']['metadata'][SOURCES]['entries']
        rebound = s['rebound']['metadata'][SOURCES]['entries']
        for old, new in zip(original, rebound):
            a, b = old['source'], new['source']
            assert a['data'] == b['data'] and a['mapping'] == b['mapping']
            assert b['mapping']['extent'] == EXTENT and b['native_replacement']['extent'] == TARGET
        row = s['rebound']['metadata'][GEOMETRY]['rows'][0]
        assert row['plane'] == 'YZ' and row['coordinate'] == 17 and row['t'] == 1
        assert s['final']['metadata'] == s['rebound']['metadata'] == s['reopened']['metadata']
        for record in s['applied']['workflow']:
            ij.validate_imagej_annotation_import_record(record)


def test_failed_reservation_and_private_context_disposal(reports):
    for r in reports:
        assert r['reservation_refusal']['error'] and not r['reservation_refusal']['ready']
        assert r['cap_disposal']['replacement_editor'] is None
        assert 'memory' in r['cap_disposal']['error']
        assert not r['stale_disposal']['visible']
        assert r['stale_disposal']['cancel_reason'] == 'source_changed'
        assert r['stale_disposal']['replacement_editor'] is None


def test_draft_overlay_rgba_and_native_opengl_capture(reports):
    for r in reports:
        layers = [v for p in r['preview_layers'] for v in p['display_annotations'] if v.get('preview')]
        image = next(v for v in layers if v['kind'] == 'image')
        assert image['rgba_prefix'] == [255, 0, 0, 45, 0, 0, 255, 90]
        assert any(v['kind'] == 'text' for v in layers)
        pixels = png_rgb(Path(r['directory']) / 'draft-gl.png').astype('int32')
        assert pixels.shape[0] >= 700 and pixels.shape[1] >= 1000
        assert ((pixels[:, :, 0] > pixels[:, :, 1] + 35) & (pixels[:, :, 0] > pixels[:, :, 2] + 35)).sum() > 100
