"""Actual BigTIFF container control and XYZ readbacks in both ImGui hosts."""
from pathlib import Path
import json
import os
import numpy as np
import pytest
from imagejxx import scientific_io as io
from test_bigtiff_io import VALID,write_request

CASES=[(d,r) for d,r in VALID if d.name=='scientific-bigtiff' and '-edges-' in r['file'] and r['pattern']=='finite']


def check(report,directory,row):
    assert report['state']=='observed' and not report['error']
    assert report['acceptance_claim'] is False
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    for name in ['startup_review','mixed_review']:
        assert all(f['events']==f['input_frame']['raw_inputs'] for f in report[name]['frames'])
    assert report['review_open']['container']==report['review_export']['container']=='bigtiff'
    assert report['review_open']['storage']==report['review_export']['storage']==row['storage']
    assert report['container_selected']['container']=='bigtiff'
    assert {'container','container.bigtiff'}<={r['name'] for r in report['controls']}
    assert report['opened']['shape']==row['shape']
    assert report['flat_import']['shape']==[1,1,8,17,35,row['shape'][-1]]
    dtype='<f4' if row['dtype']=='f32' else '<u2' if row['dtype']=='u16' else 'u1'
    expected=np.frombuffer(bytes.fromhex(row['hex']),dtype=dtype).reshape(row['shape'])
    assert np.asarray(report['native_planes'],dtype=dtype).tobytes()==expected.tobytes()
    assert report['calibration']['spacing']==row['spacing']
    assert {v['plane'] for v in report['linked_views']}=={'XY','XZ','YZ'}
    for view in report['linked_views']:
        t,c,_=view['current_tcz'];volume=expected[t,c];coordinate=view['coordinate'];plane=view['plane']
        wanted=volume[coordinate] if plane=='XY' else volume[:,coordinate] if plane=='XZ' else volume[:,:,coordinate]
        actual=view['readback']['source']
        assert actual['shape']==list(wanted.shape)
        assert np.asarray(actual['pixels'],dtype=dtype).tobytes()==wanted.tobytes()
    for record in report['exported']['workflow']:
        assert record['version']==15 and record['container']=='bigtiff'
    info=io.inspect_image_file(directory/'output.tif')
    assert info.container==io.TiffContainer.BIGTIFF and info.format==write_request(row).format
    assert io.read_image_series(directory/'output.tif').dataset.numpy('TCZYXS').tobytes()==expected.tobytes()
    assert report['replay_review']['container']=='bigtiff' and not report['replay_review']['accept_loss']
    assert report['replay_review'].get('estimate') is None
    for state in [report['opened'],report['exported'],report['after']]:
        assert state['data_identity']==report['opened']['data_identity']
        assert state['undo']==state['redo']==0 and not state['dirty']
    assert not (directory/'canceled.tif').exists()


@pytest.mark.parametrize('directory,row',CASES,ids=[r['file'] for _,r in CASES])
def test_bigtiff_controls_and_xyz_both_hosts(directory,row,tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    capture=tmp_path/'capture001'
    report=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._scientific_tiff_ui_case(str(capture),str(directory/row['file'])))
    check(report,capture,row)
    cpp=Path(os.environ['IMAGEJXX_BIGTIFF_CPP_DIR'])/Path(row['file']).stem/'capture001'
    check(json.loads((cpp/'case.json').read_bytes()),cpp,row)
