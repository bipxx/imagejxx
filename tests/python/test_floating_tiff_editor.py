"""Horizontal prediction records and export consent through the shared editor."""
from pathlib import Path
import copy,json
import pytest
from imagejxx import scientific_io as io
from test_floating_tiff_io import read_request
from test_lzw_tiff_editor import settle
from imagejxx.ui.app import Editor
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-floating'
ROWS=[r for r in json.loads((ROOT/'SOURCE.json').read_bytes())['files'] if r['valid'] and r['rows']==2 and r['shape']==[2,2,2,5,7,1] and r['codec'] in [8,5]]

def opened(tmp_path,row):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True;editor.open_file(ROOT/row['file']);state=settle(editor)
    assert not state['error']
    editor.action('scientific_io_draft',sample_policy={'finite':'finite_only','quiet':'preserve_quiet_nan_float32','ieee':'preserve_ieee_float32'}[row['pattern']])
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open'];return editor

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_floating_record_and_export_review(row,tmp_path):
    editor=opened(tmp_path,row);owner=editor.live_document().native_owner;before=owner.snapshot().dataset.numpy('TCZYXS').tobytes()
    record=copy.deepcopy(editor.workflow[-1]);codec='lzw' if row['codec']==5 else 'deflate'
    assert record['version']==9 and record['predictor']=='floating' and record['compression']==codec
    assert before.hex()==row['hex'];editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['open'] and not state['confirm_axes'];editor.action('cancel_scientific_io')
    path=tmp_path/'export.tif';editor.action('show_scientific_export',path=str(path));state=settle(editor)
    assert state['predictor']=='none'
    editor.action('scientific_io_draft',compression=codec);editor.action('scientific_io_inspect');settle(editor)
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_draft',predictor='floating');state=settle(editor)
    assert state['predictor']=='floating' and not state['accept_loss'] and state.get('estimate') is None
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_apply')
    editor.action('scientific_io_inspect');state=settle(editor);assert not state['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open']
    exported=copy.deepcopy(editor.workflow[-1]);assert exported['version']==9 and exported['predictor']=='floating'
    assert io.read_image_series(path,request=read_request(row)).dataset.numpy('TCZYXS').tobytes()==before
    raw=path.read_bytes();editor.action('review_scientific_io',record=exported);state=settle(editor)
    assert state['compression']==codec and state['predictor']=='floating' and not state['accept_loss'] and state.get('estimate') is None
    editor.action('scientific_io_draft',compression='packbits');state=settle(editor)
    assert state['predictor']=='none' and state.get('estimate') is None
    assert path.read_bytes()==raw and not owner.snapshot().dirty and owner.undo_steps==0
    editor.action('cancel_scientific_io')

@pytest.mark.parametrize('patch',[{'version':8},{'predictor':'horizontal'},{'predictor':2},{'compression':'packbits'},{'compression':'uncompressed'},{'dtype':'uint16'},{'components':2}])
def test_v9_contract_refuses_invalid_prediction(patch,tmp_path):
    editor=opened(tmp_path,ROWS[0]);record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty

@pytest.mark.parametrize('field',['predictor','compression','components','dtype','sample_policy'])
def test_v9_requires_explicit_prediction(field,tmp_path):
    editor=opened(tmp_path,ROWS[0]);record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)

@pytest.mark.parametrize('codec',['uncompressed','packbits'])
def test_incompatible_predictor_patch_is_atomic(codec,tmp_path):
    editor=opened(tmp_path,ROWS[0]);editor.action('show_scientific_export',path=str(tmp_path/'output.tif'));settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',compression=codec,predictor='floating')
    state=settle(editor);assert state['compression']=='uncompressed' and state['predictor']=='none'
    assert state.get('estimate') is None and not (tmp_path/'output.tif').exists()

@pytest.mark.parametrize('codec',['deflate','lzw'])
def test_integer_rejects_floating_prediction(codec,tmp_path):
    from test_horizontal_tiff_editor import opened as open_integer,ROWS as INTEGER_ROWS
    editor=open_integer(tmp_path,INTEGER_ROWS[0]);editor.action('show_scientific_export',path=str(tmp_path/'output.tif'));settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',compression=codec,predictor='floating')
    state=settle(editor);assert state['compression']=='uncompressed' and state['predictor']=='none'
    assert not (tmp_path/'output.tif').exists()
