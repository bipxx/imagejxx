"""Shared scientific open: compression provenance and inert v5 review."""
from pathlib import Path
import copy,json,time
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from imagejxx import scientific_io as io
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-deflate'
ROWS=[r for r in json.loads((ROOT/'SOURCE.json').read_bytes())['files'] if r['valid'] and r['rows']==64 and r['compression']==8]
def settle(editor):
    until=time.monotonic()+15
    while editor.job_info is not None and editor.job_info.state in (ij.JobState.QUEUED,ij.JobState.RUNNING,ij.JobState.READY):
        assert time.monotonic()<until;editor.wait()
    return editor.render_headless(1)['scientific_io']
def opened(tmp_path,row):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.open_file(ROOT/row['file']);state=settle(editor)
    assert not state['error'] and state['inspection']['shape']==row['shape']
    if row['dtype']=='f32':editor.action('scientific_io_draft',sample_policy='preserve_ieee_float32')
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open'];return editor

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_open_records_compression_and_replay_requires_new_review(row,tmp_path):
    editor=opened(tmp_path,row);owner=editor.live_document().native_owner
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes().hex()==row['hex']
    record=copy.deepcopy(editor.workflow[-1]);assert record['version']==5 and record['compression']=='deflate'
    assert record['components']==row['shape'][-1] and record['format_id']==record['reviewed_series']['metadata']['format_id']
    before=(ROOT/row['file']).read_bytes();count=len(editor.document_infos)
    editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['open'] and not state['confirm_axes'] and not state['accept_loss']
    assert len(editor.document_infos)==count and (ROOT/row['file']).read_bytes()==before
    assert owner.snapshot().dataset.numpy('TCZYXS').tobytes().hex()==row['hex'] and not owner.snapshot().dirty
    editor.action('cancel_scientific_io')

@pytest.mark.parametrize('patch',[{'version':4},{'compression':'lzw'},{'compression':1},{'components':2},{'dtype':'uint16'},{'format_id':io.format_id},{'operation':'export'}])
def test_v5_compression_contract_corruption_refuses(patch,tmp_path):
    row=next(r for r in ROWS if r['dtype']=='rgb8');editor=opened(tmp_path,row);record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty

@pytest.mark.parametrize('field',['compression','components','dtype','sample_policy'])
def test_v5_requires_explicit_codec_and_layout(field,tmp_path):
    editor=opened(tmp_path,ROWS[0]);record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_export_selects_compression_invalidates_consent_and_replays_inertly(row,tmp_path):
    editor=opened(tmp_path,row);path=tmp_path/'compressed.tif';owner=editor.live_document().native_owner
    before=owner.snapshot().dataset.numpy('TCZYXS').tobytes()
    editor.action('show_scientific_export',path=str(path));state=settle(editor)
    assert state['compression']=='uncompressed'
    editor.action('scientific_io_inspect');settle(editor)
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_draft',compression='deflate');state=settle(editor)
    assert state['compression']=='deflate' and not state['accept_loss'] and state.get('estimate') is None
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_apply')
    editor.action('scientific_io_inspect');state=settle(editor)
    assert not state['error'];editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open'];record=copy.deepcopy(editor.workflow[-1])
    assert record['version']==5 and record['operation']=='export' and record['compression']=='deflate'
    request=io.ImageReadRequest()
    if row['dtype']=='f32':request.sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    assert io.read_image_series(path,request=request).dataset.numpy('TCZYXS').tobytes()==before
    raw=path.read_bytes();editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['compression']=='deflate' and not state['accept_loss'] and state.get('estimate') is None
    assert path.read_bytes()==raw and owner.snapshot().dataset.numpy('TCZYXS').tobytes()==before
    assert not owner.snapshot().dirty and owner.undo_steps==0
    editor.action('cancel_scientific_io')
