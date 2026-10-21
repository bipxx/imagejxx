"""Actual shared ImGui file-policy controls with pinned ImageJ word oracles."""
import json,os
from pathlib import Path
import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
FIXTURES=Path(__file__).parents[1]/'fixtures/float32-tiff-imagej154r'
ROWS={r['file']:r for r in json.loads((FIXTURES/'SOURCE.json').read_bytes())['files']}
CASES={'TF01':'finite-le.tif','TF02':'quiet-be.tif','TF03':'ieee-le.tif','TF04':'ieee-be.tif','TF05':'quiet-le.tif','TF06':'ieee-le.tif'}
def planes(raw):return [raw[i:i+280] for i in range(0,len(raw),280)]

def check(report,case,directory,*,rows=ROWS,record_version=2):
    row=rows[CASES[case]];states=report['states'];labels={f['label'] for f in report['frames']}
    assert report['state']=='observed' and report['case_id']==case and report['acceptance_claim'] is False
    assert all(f['events']==f['rendered']['input_frame']['raw_inputs'] for f in report['frames'])
    assert {'browser.path.text','browser.submit.down','confirm_axes.down'}<=labels
    changed=states['file.policy_changed'];assert not changed['confirm_axes'] and changed['inspection']['dtype']=='float32'
    policy='finite_only' if case=='TF01' else 'preserve_quiet_nan_float32' if case in ['TF02','TF05'] else 'preserve_ieee_float32'
    assert changed['sample_policy']==policy and 'sample_policy.'+policy+'.down' in labels
    if case in ['TF05','TF06']:
        assert report['document_count']==0 and report['workflow']==[] and 'cancel.down' in labels
        if case=='TF06':assert states['file.applied']['error'] and report['preparation']
        else:assert not report['preparation']
        assert not (directory/'output.tif').exists()
        return
    if case=='TF04':assert states['file.default_failed']['error']
    opened=states['opened'];source=planes(row['source_hex']);expected=planes(row['filtered_hex'])
    assert opened['shape']==row['shape'] and opened['planes']==source and opened['undo']==0 and not opened['dirty']
    assert opened['policy']=={'finite_only':0,'preserve_quiet_nan_float32':1,'preserve_ieee_float32':2}[policy]
    record=opened['workflow'][-1];assert record['version']==record_version and record['operation']=='open' and record['sample_policy']==policy
    preview=states['preview_source'];assert preview['planes']==source and preview['undo']==0 and not preview['dirty']
    assert report['preview_pixels']['pixels_source']=='preview'
    readback=report['preview_pixels']['readback']['source']
    if policy=='finite_only':
        assert np.asarray(readback['pixels'],dtype='<f4').tobytes().hex()==expected[0]
    else:
        assert readback['encoding']=='IEEE754 Float32LE'
        assert bytes(readback['bytes']).hex()==expected[0]
    processed=states['processed'];assert processed['planes']==expected and processed['display']==opened['display']
    is_changed=source!=expected;assert processed['undo']==int(is_changed) and processed['dirty']==is_changed
    if is_changed:
        assert states['undone']['planes']==source and not states['undone']['dirty']
        assert states['redone']['planes']==expected and states['redone']['dirty']
        assert processed['workflow'][-1]['operation']=='remove_nans_imagej'
    exported=states['exported'];assert exported['planes']==expected and exported['undo']==processed['undo'] and exported['dirty']==processed['dirty']
    assert exported['workflow'][-1]['operation']=='export' and exported['workflow'][-1]['version']==record_version
    assert exported['workflow'][-1]['sample_policy']==policy
    read=io.ImageReadRequest();read.sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    result=io.read_image_series(directory/'output.tif',request=read)
    assert result.dataset.numpy('TCZYX').tobytes().hex()==row['filtered_hex']
    assert not (directory/'canceled.tif').exists()
    assert {'rank.radius.text','rank.preview.down','rank.ok.down','accept_loss.down','cancel.down'}<=labels

@pytest.mark.parametrize('case',CASES)
def test_actual_file_policy_controls_both_hosts(tmp_path,case):
    if os.environ.get('IMAGEJXX_RENDER_TESTS')!='1':pytest.skip('render checks are opt in')
    from imagejxx import _desktop
    directory=tmp_path/'capture001'
    python=json.loads(_desktop.Editor(str(tmp_path/'prefs.json'))._float32_tiff_ui_case(case,str(directory),str(FIXTURES/CASES[case])))
    cpp_dir=Path(os.environ['IMAGEJXX_FLOAT32_TIFF_CPP_DIR'])/case
    cpp=json.loads((cpp_dir/'case.json').read_bytes())
    check(python,case,directory);check(cpp,case,cpp_dir)
