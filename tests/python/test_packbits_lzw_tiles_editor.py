"""Shared desktop workflows for tiled byte/RGB/Float32 sources."""
import copy
import numpy as np
import pytest
from imagejxx import scientific_io as io
from imagejxx.ui.app import Editor
from test_lzw_tiff_editor import settle
from test_packbits_lzw_tiles_io import VALID, POLICIES, read_request, write_request, expected_pages

CASES = [(d, r) for d, r in VALID if d.name == 'scientific-tiff-packbits-lzw-tiles'
         and '-edges-' in r['file'] and r['endian'] == 'big' and r['padding'] != 0]


def opened(directory, row, tmp_path, flat=False):
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor.recording = True
    editor.open_file(directory/row['file'])
    state = settle(editor)
    assert not state['error'] and state['storage'] == 'tiled'
    if row['pattern'] != 'finite':
        editor.action('scientific_io_draft', sample_policy=row['sample_policy'])
    editor.action('scientific_io_draft', **({'accept_flat_pixels_only': True} if flat else {'confirm_axes': True}))
    editor.action('scientific_io_flat_open' if flat else 'scientific_io_apply')
    state = settle(editor)
    assert not state['error']
    return editor


@pytest.mark.parametrize('directory,row', CASES, ids=[r['file'] for _, r in CASES])
def test_tiled_layout_records_export_and_inert_replay(directory, row, tmp_path):
    editor = opened(directory, row, tmp_path)
    data = editor.live_document().native_snapshot().dataset
    assert data.numpy('TCZYXS').tobytes().hex() == row['hex']
    record = copy.deepcopy(editor.workflow[-1])
    assert record['version'] == 13 and record['storage'] == 'tiled'
    assert record['components'] == row['shape'][-1]
    assert record['sample_policy'] == row['sample_policy']
    assert record['compression']==('packbits' if row['compression']==32773 else 'lzw') and 'predictor' not in record
    editor.action('review_scientific_io', record=record)
    state = settle(editor)
    assert state['open'] and not state['confirm_axes'] and not state['accept_loss']
    editor.action('cancel_scientific_io')
    path = tmp_path/'output.tif'
    editor.action('show_scientific_export', path=str(path))
    settle(editor)
    editor.action('scientific_io_draft', storage='tiled',compression='packbits' if row['compression']==32773 else 'lzw')
    editor.action('scientific_io_inspect')
    assert not settle(editor)['error']
    editor.action('scientific_io_draft', accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_apply')
    assert not settle(editor)['error']
    assert io.inspect_image_file(path).format == write_request(row).format
    assert io.read_image_series(path, request=read_request(row)).dataset.numpy('TCZYXS').tobytes().hex() == row['hex']
    exported = copy.deepcopy(editor.workflow[-1])
    assert exported['version'] == 13 and exported['storage'] == 'tiled'
    editor.action('review_scientific_io', record=exported)
    state = settle(editor)
    assert state['storage'] == 'tiled' and not state['accept_loss'] and state.get('estimate') is None
    assert not editor.live_document().native_snapshot().dirty


@pytest.mark.parametrize('directory,row', CASES, ids=[r['file'] for _, r in CASES])
def test_tiled_layout_flat_import_carries_float_policy(directory, row, tmp_path):
    editor = opened(directory, row, tmp_path, flat=True)
    data = editor.live_document().native_snapshot().dataset
    t, c, z, y, x, s = row['shape']
    assert data.shape == (1, 1, t*c*z, y, x, s)
    assert data.numpy('TCZYXS').tobytes() == b''.join(expected_pages(row))
    assert data.sample_policy == POLICIES[row['pattern']]
    assert data.calibration.unit == 'px'


@pytest.mark.parametrize('patch', [{'version': 12}, {'version': 11}, {'storage': 'strips'},
                                 {'components': 2}, {'dtype': 'uint16'}, {'compression': 'uncompressed'},
                                 {'predictor': 'none'}, {'sample_policy': 'unknown'}])
def test_tiled_layout_record_mutations_are_refused(patch, tmp_path):
    directory, row = CASES[0]
    editor = opened(directory, row, tmp_path)
    record = copy.deepcopy(editor.workflow[-1])
    record.update(patch)
    with pytest.raises((ValueError, RuntimeError)):
        editor.action('review_scientific_io', record=record)
    assert len(editor.document_infos) == 1 and not editor.live_document().native_snapshot().dirty
