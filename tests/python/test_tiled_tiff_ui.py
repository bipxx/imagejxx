"""Actual ImGui input and native CPU readbacks in both desktop hosts."""
from pathlib import Path
import json,os
import numpy as np
import pytest
from imagejxx import scientific_io as io

ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-tiled'
ROWS={r['file']:r for r in json.loads((ROOT/'SOURCE.json').read_bytes())['files']}


def check(report,directory,row):
    assert report['state']=='observed' and not report['error']
    assert report['acceptance_claim'] is False
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    for name in ['startup_review','mixed_review']:
        assert all(f['events']==f['input_frame']['raw_inputs'] for f in report[name]['frames'])
    assert report['review_open']['storage']==report['review_export']['storage']=='tiled'
    assert report['storage_selected']['storage']=='tiled'
    assert {'storage','storage.tiled'}<={r['name'] for r in report['controls']}
    assert report['opened']['shape']==row['shape']
    assert report['flat_import']['shape']==[1,1,8,17,35,1] and report['flat_import']['unit']=='px'
    expected=np.frombuffer(bytes.fromhex(row['hex']),dtype='<u2').reshape(row['shape'][:-1])
    assert np.asarray(report['native_planes'],dtype='<u2').tobytes()==expected.tobytes()
    assert report['calibration']['spacing']==row['spacing'] and report['calibration']['unit']=='um'
    assert len(report['linked_views'])==3
    for view in report['linked_views']:
        t,c,_=view['current_tcz'];volume=expected[t,c];coordinate=view['coordinate'];plane=view['plane']
        actual=view['readback']['source'];assert actual['dtype']=='uint16'
        wanted=volume[coordinate] if plane=='XY' else volume[:,coordinate,:] if plane=='XZ' else volume[:,:,coordinate]
        assert actual['shape']==[*wanted.shape,1]
        assert np.asarray(actual['pixels'],dtype='<u2').tobytes()==wanted.tobytes()
    assert {v['plane'] for v in report['linked_views']}=={'XY','XZ','YZ'}
    for record in report['exported']['workflow']:
        assert record['version']==10 and record['storage']=='tiled'
        assert record['format_id']==io.u16_tiled_format_id
    assert io.inspect_image_file(directory/'output.tif').format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_TILED_V1
    assert io.read_image_series(directory/'output.tif').dataset.numpy('TCZYXS').tobytes()==expected.tobytes()
    assert report['replay_review']['storage']=='tiled' and not report['replay_review']['accept_loss']
    assert report['replay_review'].get('estimate') is None
    for state in [report['opened'],report['exported'],report['after']]:
        assert state['data_identity']==report['opened']['data_identity'] and state['undo']==state['redo']==0 and not state['dirty']
    assert not (directory/'canceled.tif').exists()


@pytest.mark.parametrize('case',['le','be'])
def test_tiled_controls_and_xyz_readbacks_both_hosts(case,tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    row=ROWS['edges-le-poison.tif' if case=='le' else 'edges-be-zero.tif']
    directory=tmp_path/'capture001'
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._scientific_tiff_ui_case(str(directory),str(ROOT/row['file'])))
    cpp_dir=Path(os.environ['IMAGEJXX_TILED_TIFF_CPP_DIR'])/('cpp-'+case+'001')/'capture001'
    check(python,directory,row);check(json.loads((cpp_dir/'case.json').read_bytes()),cpp_dir,row)
