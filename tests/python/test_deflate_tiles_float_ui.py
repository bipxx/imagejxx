"""Rendered policy failure/recovery and exact IEEE XYZ sources for tiled Deflate."""
from pathlib import Path
import json
import os
import numpy as np
import pytest
from imagejxx import scientific_io as io
from test_deflate_tiles_io import VALID, POLICIES, read_request, write_request

CASES = [(case, directory, row) for case, pattern in [('TF02', 'quiet'), ('TF04', 'ieee')]
         for directory, row in VALID if directory.name == 'scientific-tiff-deflate-tiles'
         and row['dtype'] == 'f32' and '-edges-' in row['file'] and row['pattern'] == pattern
         and row['compression'] == 8 and row['padding'] != 0]


def check(report, case, directory, row):
    assert report['state'] == 'observed' and not report['error']
    assert report['acceptance_claim'] is False
    assert report['input_storage'] == 'tiled' and report['input_compression'] == 'deflate'
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    states = report['states']
    if case == 'TF04':
        assert states['file.default_failed']['error']
    assert not states['file.policy_changed']['confirm_axes']
    assert states['opened']['shape'] == row['shape']
    assert states['opened']['policy'] == int(POLICIES[row['pattern']])
    assert ''.join(states['opened']['planes']) == row['hex']
    source = np.frombuffer(bytes.fromhex(row['hex']), '<u4').reshape(row['shape'])
    assert {v['plane'] for v in report['linked_views']} == {'XY', 'XZ', 'YZ'}
    for view in report['linked_views']:
        t, c, _ = view['current_tcz']
        volume = source[t, c]
        coordinate, plane = view['coordinate'], view['plane']
        expected = volume[coordinate] if plane == 'XY' else volume[:, coordinate] if plane == 'XZ' else volume[:, :, coordinate]
        actual = view['readback']['source']
        assert actual['shape'] == list(expected.shape)
        assert bytes(actual['bytes']) == expected.astype('<u4').tobytes()
    assert states['linked_source']['planes'] == states['ordinary_restored']['planes'] == states['opened']['planes']
    for key in ['opened', 'exported']:
        record = states[key]['workflow'][-1]
        assert record['version'] == 12 and record['storage'] == 'tiled'
        assert record['compression'] == 'deflate' and record['format_id'] == io.float32_deflate_tiled_format_id
    assert states['exported']['planes'] == states['processed']['planes']
    assert states['after_replay']['planes'] == states['processed']['planes']
    assert states['replay.reviewed']['storage'] == 'tiled'
    assert not states['replay.reviewed']['accept_loss']
    assert states['replay.reviewed'].get('estimate') is None
    output = directory/'output.tif'
    assert io.inspect_image_file(output).format == write_request(row).format
    actual = io.read_image_series(output, request=read_request(row)).dataset.numpy('TCZYXS').tobytes()
    assert actual.hex() == ''.join(states['processed']['planes'])
    assert not (directory/'canceled.tif').exists()


@pytest.mark.parametrize('case,directory,row', CASES, ids=[case+'-'+r['file'] for case, _, r in CASES])
def test_deflate_tile_ieee_recovery_and_xyz_both_hosts(case, directory, row, tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS') != '1':
        pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    capture = tmp_path/'capture001'
    report = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._float32_tiff_ui_case(case, str(capture), str(directory/row['file'])))
    check(report, case, capture, row)
    cpp = Path(os.environ['IMAGEJXX_DEFLATE_TILES_FLOAT_CPP_DIR'])/('float-'+case+'-'+row['endian']+'001')/'capture001'
    check(json.loads((cpp/'case.json').read_bytes()), case, cpp, row)
