"""Shared RGB TIFF review/export and strict v3 descriptors."""
from pathlib import Path
import copy,json,time
import imagejxx as ij
from imagejxx.ui.app import Editor
from imagejxx import scientific_io as io
import pytest
ROOT=Path(__file__).parents[1]/'fixtures/rgb-tiff-imagej154r'
ROWS=json.loads((ROOT/'SOURCE.json').read_bytes())['files']
def settle(editor):
    end=time.monotonic()+15
    while editor.job_info is not None and editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING,ij.JobState.READY):
        assert time.monotonic()<end;editor.wait()
    return editor.render_headless(1)['scientific_io']
def opened(tmp_path,row):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.open_file(ROOT/row['file']);state=settle(editor)
    assert state['inspection']['dtype']=='uint8' and 'sample_policy' not in state['controls'] and not editor.document_infos
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['open'] and not state['error'];return editor

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_rgb_open_invert_history_export_and_reopen(tmp_path,row):
    editor=opened(tmp_path,row);owner=editor.live_document().native_owner
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes().hex()==row['source_hex'] and not owner.snapshot().dirty
    opening=copy.deepcopy(editor.workflow[-1]);assert opening['version']==4 and opening['components']==3 and opening['dtype']=='uint8' and opening['sample_policy']=='finite_only'
    assert opening['format_id']==io.rgb8_format_id
    target=ij.resolve_target(owner.snapshot(),operation='invert');prepared=ij.prepare_operation('invert',target);owner.commit(prepared.begin_edit())
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes().hex()==row['inverted_hex']
    owner.undo();assert not owner.snapshot().dirty and owner.snapshot().dataset.numpy('TCZYXS').tobytes().hex()==row['source_hex'];owner.redo()
    path=tmp_path/'export.tif';editor.action('show_scientific_export',path=str(path));editor.action('scientific_io_inspect');state=settle(editor)
    assert state['estimate']['samples']==row['shape'][0]*row['shape'][1]*row['shape'][2]*45 and not state['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open']
    assert io.read_image_series(path).dataset.numpy('TCZYXS').tobytes().hex()==row['inverted_hex']
    export=copy.deepcopy(editor.workflow[-1]);assert export['version']==4 and export['components']==3 and export['dtype']=='uint8' and export['sample_policy']=='finite_only'
    for record in [opening,export]:
        before=path.read_bytes();editor.action('review_scientific_io',record=record);state=settle(editor)
        assert state['open'] and not state['confirm_axes'] and not state['accept_loss'] and path.read_bytes()==before
        editor.action('cancel_scientific_io')


@pytest.mark.parametrize('patch',[{'version':3},{'version':5},{'components':1},{'components':2},{'components':4},{'components':True},{'dtype':'float32'},{'sample_policy':'preserve_ieee_float32'}])
def test_rgb_v4_layout_corruption_refuses_review(tmp_path,patch):
    editor=opened(tmp_path,ROWS[-1]);record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty

@pytest.mark.parametrize('field',['dtype','sample_policy','components'])
def test_rgb_v4_requires_explicit_layout(tmp_path,field):
    editor=opened(tmp_path,ROWS[0]);record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)

@pytest.mark.parametrize('dtype,relative,policy',[
    ('uint8','uint8-tiff-imagej154r/byte-le.tif','finite_only'),
    ('uint16','scientific-tiff-u16/little.tif','finite_only'),
    ('float32','float32-tiff-imagej154r/quiet-le.tif','preserve_quiet_nan_float32'),
])
def test_generic_v4_scalar_layout_compatibility(tmp_path,dtype,relative,policy):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True
    editor.open_file(Path(__file__).parents[1]/'fixtures'/relative);settle(editor)
    if dtype=='float32':editor.action('scientific_io_draft',sample_policy=policy)
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');settle(editor)
    owner=editor.live_document().native_owner;before=owner.snapshot().dataset.numpy('TCZYXS').tobytes()
    record=copy.deepcopy(editor.workflow[-1]);record.update(version=4,dtype=dtype,sample_policy=policy,components=1)
    editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['open'] and not state['confirm_axes'] and state['sample_policy']==policy
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes()==before and not owner.snapshot().dirty
    editor.action('cancel_scientific_io')
