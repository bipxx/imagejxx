"""Actual shared file and compression controls in both ImGui hosts."""
from pathlib import Path
import json,os
import numpy as np
import pytest
from imagejxx import scientific_io as io
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-packbits'
ROWS={r['file']:r for r in json.loads((ROOT/'SOURCE.json').read_bytes())['files']}
def check(report,directory,row):
    assert report['state']=='observed' and not report['error']
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    for key in ['startup_review','mixed_review']:
        assert all(f['events']==f['input_frame']['raw_inputs'] for f in report[key]['frames'])
    assert report['opened']['shape']==row['shape'] and not report['opened']['dirty']
    dtype='<u2' if row['dtype']=='u16' else 'u1'
    assert np.asarray(report['native_planes'],dtype=dtype).tobytes().hex()==row['hex']
    assert report['review_open']['compression']=='packbits' and report['compression_selected']['compression']=='packbits'
    assert not report['review_export']['accept_loss'] and report['review_export']['compression']=='packbits'
    assert report['replay_review']['compression']=='packbits' and not report['replay_review']['accept_loss']
    assert {'compression','compression.packbits'}<={c['name'] for c in report['controls']}
    records=report['exported']['workflow'];assert len(records)==2
    for record in records:assert record['version']==6 and record['compression']=='packbits' and record['components']==row['shape'][-1]
    assert io.inspect_image_file(directory/'output.tif').format==getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+{'u8':'U8','u16':'U16','rgb8':'RGB8'}[row['dtype']]+'_PACKBITS_V1')
    assert io.read_image_series(directory/'output.tif').dataset.numpy('TCZYXS').tobytes().hex()==row['hex']
    assert not (directory/'canceled.tif').exists()
    assert report['after']['data_identity']==report['opened']['data_identity'] and not report['after']['dirty']

@pytest.mark.parametrize('case',[dtype+'-'+endian for dtype in ['u8','u16','rgb8'] for endian in ['le','be']])
def test_actual_compression_controls_both_hosts(case,tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    row=ROWS[case+'-r64-c32773.tif'];directory=tmp_path/'capture001'
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._scientific_tiff_ui_case(str(directory),str(ROOT/row['file'])))
    cpp_dir=Path(os.environ['IMAGEJXX_PACKBITS_TIFF_CPP_DIR'])/case/'capture001'
    check(python,directory,row);check(json.loads((cpp_dir/'case.json').read_bytes()),cpp_dir,row)
