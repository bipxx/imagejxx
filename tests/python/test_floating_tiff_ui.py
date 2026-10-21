"""Actual LZW Float32 policy, processing and export controls in both hosts."""
from pathlib import Path
import hashlib,json,os
import pytest
import numpy as np
import imagejxx as ij
from imagejxx import scientific_io as io
from test_float32_tiff_ui import check as check_float,CASES
ROOT=Path(__file__).parents[1]/'fixtures/floating-float32-tiff-corpus'
MANIFEST=json.loads((ROOT/'SOURCE.json').read_bytes())
ALL_ROWS=MANIFEST['files']

def check(report,case,directory,codec):
    rows={r['file'].replace('-c'+str(codec)+'.tif','.tif'):r for r in ALL_ROWS if r['codec']==codec}
    compression='lzw' if codec==5 else 'deflate'
    check_float(report,case,directory,rows=rows,record_version=9)
    assert report['input_compression']==compression and report['input_predictor']=='floating'
    row=rows[CASES[case]]
    if case in ['TF05','TF06']:return
    states=report['states'];labels={f['label'] for f in report['frames']}
    assert {'compression.down','compression.'+compression+'.down','predictor.down','predictor.floating.down'}<=labels
    source=np.frombuffer(bytes.fromhex(row['source_hex']),'<u4').reshape(row['shape'][:-1])
    assert len(report['linked_views'])==3
    for view in report['linked_views']:
        t,c,_=view['current_tcz'] if 'current_tcz' in view else view['range_tcz']
        volume=source[t,c];coordinate=view['coordinate'];plane=view['plane']
        expected=volume[coordinate] if plane=='XY' else volume[:,coordinate,:] if plane=='XZ' else volume[:,:,coordinate]
        actual=view['readback']['source']
        raw=bytes(actual['bytes']) if 'bytes' in actual else np.asarray(actual['pixels'],dtype='<f4').tobytes()
        assert raw==expected.astype('<u4').tobytes()
    for key in ['linked_source','ordinary_restored']:
        assert states[key]['planes']==states['opened']['planes'] and states[key]['undo']==0 and not states[key]['dirty']
    for key in ['opened','exported']:
        record=states[key]['workflow'][-1]
        assert record['format_id']==getattr(io,'float32_'+compression+'_floating_format_id') and record['predictor']=='floating'
        assert record['compression']==compression and record['dtype']=='float32' and record['components']==1
    assert states['export.reviewed']['predictor']=='floating'
    assert states['replay.reviewed']['predictor']=='floating'
    assert states['export.reviewed']['compression']==compression and not states['export.reviewed']['accept_loss']
    assert states['replay.reviewed']['compression']==compression and not states['replay.reviewed']['accept_loss']
    assert states['after_replay']['planes']==states['processed']['planes']
    assert states['after_replay']['undo']==states['processed']['undo'] and states['after_replay']['dirty']==states['processed']['dirty']
    assert io.inspect_image_file(directory/'output.tif').format==getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_FLOAT32_'+compression.upper()+'_FLOATING_V1')
    assert states['export.reviewed']['estimate']['encoded_upper_bound']>=(directory/'output.tif').stat().st_size

@pytest.mark.parametrize('codec',[8,5])
@pytest.mark.parametrize('case',CASES)
def test_actual_floating_float_controls_both_hosts(case,codec,tmp_path):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    directory=tmp_path/'capture001'
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._float32_tiff_ui_case(case,str(directory),str(ROOT/CASES[case].replace('.tif','-c'+str(codec)+'.tif'))))
    cpp_dir=Path(os.environ['IMAGEJXX_FLOATING_TIFF_CPP_DIR'])/('c'+str(codec))/case
    check(python,case,directory,codec);check(json.loads((cpp_dir/'case.json').read_bytes()),case,cpp_dir,codec)

@pytest.mark.parametrize('row',MANIFEST['files'],ids=lambda r:r['file'])
def test_floating_carrier_preserves_pinned_source_and_policies(row):
    raw=(ROOT/row['file']).read_bytes();assert len(raw)==row['bytes'] and hashlib.sha256(raw).hexdigest()==row['sha256']
    request=io.ImageReadRequest();request.sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    result=io.read_image_series(ROOT/row['file'],request=request)
    assert result.dataset.numpy('TCZYXS').tobytes().hex()==row['source_hex'] and result.dataset.shape==tuple(row['shape'])
    assert tuple(result.dataset.calibration.spacing)==tuple(row['spacing'])
    if row['file'].startswith('finite-'):assert io.read_image_series(ROOT/row['file']).dataset.numpy('TCZYXS').tobytes().hex()==row['source_hex']
    else:
        with pytest.raises(ValueError):io.read_image_series(ROOT/row['file'])
