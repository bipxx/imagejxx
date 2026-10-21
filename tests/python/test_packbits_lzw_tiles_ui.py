"""Actual Dear ImGui controls and XYZ readbacks in both desktop hosts."""
from pathlib import Path
import json
import os
import numpy as np
import pytest
from imagejxx import scientific_io as io
from test_packbits_lzw_tiles_io import VALID, write_request

CASES = [(d, r) for d, r in VALID if d.name == 'scientific-tiff-packbits-lzw-tiles'
         and '-edges-' in r['file'] and r['pattern'] == 'finite' and r['padding'] != 0]


def check(report, directory, row):
    assert report['state'] == 'observed' and not report['error']
    assert report['acceptance_claim'] is False
    assert all(f['events'] == f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    for name in ['startup_review', 'mixed_review']:
        assert all(f['events'] == f['input_frame']['raw_inputs'] for f in report[name]['frames'])
    assert report['review_open']['storage'] == report['review_export']['storage'] == 'tiled'
    assert report['storage_selected']['storage'] == 'tiled'
    assert report['compression_selected']['compression']==('packbits' if row['compression']==32773 else 'lzw')
    assert {'storage', 'storage.tiled'} <= {r['name'] for r in report['controls']}
    assert report['opened']['shape'] == row['shape']
    assert report['flat_import']['shape'] == [1, 1, 8, 17, 35, row['shape'][-1]]
    assert report['flat_import']['unit'] == 'px'
    dtype = '<f4' if row['dtype'] == 'f32' else '<u2' if row['dtype']=='u16' else 'u1'
    expected = np.frombuffer(bytes.fromhex(row['hex']), dtype=dtype).reshape(row['shape'])
    assert np.asarray(report['native_planes'], dtype=dtype).tobytes() == expected.tobytes()
    assert report['calibration']['spacing'] == row['spacing']
    assert {v['plane'] for v in report['linked_views']} == {'XY', 'XZ', 'YZ'}
    for view in report['linked_views']:
        t, c, _ = view['current_tcz']
        volume = expected[t, c]
        coordinate, plane = view['coordinate'], view['plane']
        wanted = volume[coordinate] if plane == 'XY' else volume[:, coordinate] if plane == 'XZ' else volume[:, :, coordinate]
        actual = view['readback']['source']
        assert actual['shape'] == list(wanted.shape)
        assert np.asarray(actual['pixels'], dtype=dtype).tobytes() == wanted.tobytes()
    for record in report['exported']['workflow']:
        assert record['version'] == 13 and record['storage'] == 'tiled'
    assert io.inspect_image_file(directory/'output.tif').format == write_request(row).format
    assert io.read_image_series(directory/'output.tif').dataset.numpy('TCZYXS').tobytes() == expected.tobytes()
    assert report['replay_review']['storage'] == 'tiled' and not report['replay_review']['accept_loss']
    assert report['replay_review'].get('estimate') is None
    for state in [report['opened'], report['exported'], report['after']]:
        assert state['data_identity'] == report['opened']['data_identity']
        assert state['undo'] == state['redo'] == 0 and not state['dirty']
    assert not (directory/'canceled.tif').exists()


@pytest.mark.parametrize('directory,row', CASES, ids=[r['file'] for _, r in CASES])
def test_tiled_layout_controls_and_xyz_both_hosts(directory, row, tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS') != '1':
        pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    capture = tmp_path/'capture001'
    report = json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._scientific_tiff_ui_case(str(capture), str(directory/row['file'])))
    check(report, capture, row)
    cpp = Path(os.environ['IMAGEJXX_PACKBITS_LZW_TILES_CPP_DIR'])/('cpp-'+Path(row['file']).stem+'001')/'capture001'
    check(json.loads((cpp/'case.json').read_bytes()), cpp, row)
