"""Actual shared scientific TIFF controls with UInt8 ImageJ carriers."""
from pathlib import Path
import json,os
import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
ROOT=Path(__file__).parents[1]/'fixtures/uint8-tiff-imagej154r'
ROWS={r['file']:r for r in json.loads((ROOT/'SOURCE.json').read_bytes())['files']}

def check(report,directory,row):
    assert report['state']=='observed' and report['acceptance_claim'] is False and not report['error']
    assert all(frame['events']==frame['rendered']['input_frame']['raw_inputs'] for frame in report['frames'])
    for key in ['startup_review','mixed_review']:
        assert all(frame['events']==frame['input_frame']['raw_inputs'] for frame in report[key]['frames'])
    assert report['documents_before_open']==0
    assert not report['review_open']['confirm_axes'] and report['review_open']['inspection']['dtype']=='uint8'
    assert 'sample_policy' not in report['review_open']['controls']
    assert report['opened']['shape']==row['shape'] and report['opened']['undo']==0 and not report['opened']['dirty']
    assert np.asarray(report['native_planes'],dtype=np.uint8).tobytes().hex()==row['source_hex']
    assert report['calibration']['spacing']==row['spacing'] and report['calibration']['unit']=='um'
    assert report['calibration']['frame_interval']==.125 and report['calibration']['time_unit']=='ms'
    assert report['flat_import']['shape']==[1,1,12,3,5,1] and report['flat_import']['unit']=='px'
    assert report['startup_review']['documents_after_first']==0
    assert report['startup_review']['shapes']==[row['shape'],row['shape']]
    assert report['mixed_review']['shapes']==[[1,1,1,3,5,1],row['shape'],[1,1,1,3,5,1]]
    opened=report['opened'];exported=report['exported']
    for snapshot in [opened,exported,report['after']]:
        assert not snapshot['dirty'] and snapshot['undo']==snapshot['redo']==0
        assert snapshot['id']==opened['id'] and snapshot['data_identity']==opened['data_identity']
    records=exported['workflow'];assert len(records)==2
    for record in records:
        assert record['version']==3 and record['dtype']=='uint8' and record['sample_policy']=='finite_only'
        assert record['format_id']==io.uint8_format_id
    assert [record['operation'] for record in records]==['open','export']
    result=io.read_image_series(directory/'output.tif')
    assert result.dataset.numpy('TCZYX').tobytes().hex()==row['source_hex']
    assert result.dataset.shape==tuple(row['shape']) and tuple(result.dataset.calibration.spacing)==tuple(row['spacing'])
    assert not (directory/'canceled.tif').exists()
    assert report['replay_review']['open'] and not report['replay_review']['accept_loss']
    assert {'confirm_axes','accept_loss','cancel','browser.submit','ordinary.browser.submit'} <= {r['name'] for r in report['controls']}

@pytest.mark.parametrize('order',['le','be'])
def test_actual_byte_file_controls_both_hosts(tmp_path,order):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    row=ROWS['byte-'+order+'.tif'];directory=tmp_path/'capture001'
    python=json.loads(_desktop.Editor(str(tmp_path/'preferences.json'))._scientific_tiff_ui_case(str(directory),str(ROOT/row['file'])))
    cpp_dir=Path(os.environ['IMAGEJXX_UINT8_TIFF_CPP_DIR'])/order/'capture001';cpp=json.loads((cpp_dir/'case.json').read_bytes())
    check(python,directory,row);check(cpp,cpp_dir,row)
