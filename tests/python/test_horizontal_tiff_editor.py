"""Horizontal prediction records and export consent through the shared editor."""
from pathlib import Path
import copy,json
import pytest
from imagejxx import scientific_io as io
from test_lzw_tiff_editor import settle
from imagejxx.ui.app import Editor
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-horizontal'
ROWS=[r for r in json.loads((ROOT/'SOURCE.json').read_bytes())['files'] if r['valid'] and r['rows']==2 and r['pattern']=='wrap' and r['shape']==[2,2,2,5,7,3 if r['dtype']=='rgb8' else 1] and r['codec'] in [8,5]]

def opened(tmp_path,row):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.open_file(ROOT/row['file']);state=settle(editor)
    assert not state['error'];editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open'];return editor

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_horizontal_record_and_export_review(row,tmp_path):
    editor=opened(tmp_path,row);owner=editor.live_document().native_owner;before=owner.snapshot().dataset.numpy('TCZYXS').tobytes()
    record=copy.deepcopy(editor.workflow[-1]);codec='lzw' if row['codec']==5 else 'deflate'
    assert record['version']==8 and record['predictor']=='horizontal' and record['compression']==codec
    assert before.hex()==row['hex'];editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['open'] and not state['confirm_axes'];editor.action('cancel_scientific_io')
    path=tmp_path/'export.tif';editor.action('show_scientific_export',path=str(path));state=settle(editor)
    assert state['predictor']=='none'
    editor.action('scientific_io_draft',compression=codec);editor.action('scientific_io_inspect');settle(editor)
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_draft',predictor='horizontal');state=settle(editor)
    assert state['predictor']=='horizontal' and not state['accept_loss'] and state.get('estimate') is None
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_apply')
    editor.action('scientific_io_inspect');state=settle(editor);assert not state['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open']
    exported=copy.deepcopy(editor.workflow[-1]);assert exported['version']==8 and exported['predictor']=='horizontal'
    assert io.read_image_series(path).dataset.numpy('TCZYXS').tobytes()==before
    raw=path.read_bytes();editor.action('review_scientific_io',record=exported);state=settle(editor)
    assert state['compression']==codec and state['predictor']=='horizontal' and not state['accept_loss'] and state.get('estimate') is None
    editor.action('scientific_io_draft',compression='packbits');state=settle(editor)
    assert state['predictor']=='none' and state.get('estimate') is None
    assert path.read_bytes()==raw and not owner.snapshot().dirty and owner.undo_steps==0
    editor.action('cancel_scientific_io')

@pytest.mark.parametrize('patch',[{'version':7},{'predictor':'floating'},{'predictor':2},{'compression':'packbits'},{'compression':'uncompressed'},{'dtype':'float32'},{'components':2}])
def test_v8_contract_refuses_invalid_prediction(patch,tmp_path):
    editor=opened(tmp_path,ROWS[0]);record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty

@pytest.mark.parametrize('field',['predictor','compression','components','dtype','sample_policy'])
def test_v8_requires_explicit_prediction(field,tmp_path):
    editor=opened(tmp_path,ROWS[0]);record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)

@pytest.mark.parametrize('codec',['uncompressed','packbits'])
def test_incompatible_predictor_patch_is_atomic(codec,tmp_path):
    editor=opened(tmp_path,ROWS[0]);editor.action('show_scientific_export',path=str(tmp_path/'output.tif'));settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',compression=codec,predictor='horizontal')
    state=settle(editor);assert state['compression']=='uncompressed' and state['predictor']=='none'
    assert state.get('estimate') is None and not (tmp_path/'output.tif').exists()

@pytest.mark.parametrize('codec',['deflate','lzw'])
def test_float32_rejects_integer_prediction(codec,tmp_path):
    from test_lzw_tiff_editor import opened as open_float,ROWS as FLOAT_ROWS
    editor=open_float(tmp_path,next(r for r in FLOAT_ROWS if r['dtype']=='f32'))
    editor.action('show_scientific_export',path=str(tmp_path/'output.tif'));settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',compression=codec,predictor='horizontal')
    state=settle(editor);assert state['compression']=='uncompressed' and state['predictor']=='none'
    assert not (tmp_path/'output.tif').exists()
