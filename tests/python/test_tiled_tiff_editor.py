"""Explicit tiled output, inert records, consent invalidation and flat ordering."""
from pathlib import Path
import copy,json
import numpy as np
import pytest
from imagejxx import scientific_io as io
from imagejxx.ui.app import Editor
from test_lzw_tiff_editor import settle

ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-tiled'
ROWS=[row for row in json.loads((ROOT/'SOURCE.json').read_bytes())['files']
      if row['file'] in ['edges-le-poison.tif','edges-be-zero.tif','wide-le-zero.tif','nonsquare-be-poison.tif']]


def opened(tmp_path,row=ROWS[0]):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.recording=True
    editor.open_file(ROOT/row['file']);state=settle(editor)
    assert state['storage']=='tiled' and not state['confirm_axes'] and not state['error']
    editor.action('scientific_io_draft',confirm_axes=True);editor.action('scientific_io_apply');state=settle(editor)
    assert not state['error'] and not state['open'];return editor


@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_tiled_file_records_require_explicit_reconfirmation(row,tmp_path):
    editor=opened(tmp_path,row);owner=editor.live_document().native_owner
    source=owner.snapshot().dataset.numpy('TCZYXS').tobytes();assert source.hex()==row['hex']
    record=copy.deepcopy(editor.workflow[-1])
    assert record['version']==10 and record['storage']=='tiled' and record['components']==1
    assert record['dtype']=='uint16' and record['sample_policy']=='finite_only'
    assert 'compression' not in record and 'predictor' not in record
    editor.action('review_scientific_io',record=record);state=settle(editor)
    assert state['open'] and not state['confirm_axes'] and not state['accept_loss'];editor.action('cancel_scientific_io')
    path=tmp_path/'tiled.tif';editor.action('show_scientific_export',path=str(path));state=settle(editor)
    assert state['storage']=='strips'
    editor.action('scientific_io_inspect');settle(editor)
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_draft',storage='tiled');state=settle(editor)
    assert state['storage']=='tiled' and not state['accept_loss'] and state.get('estimate') is None
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_apply')
    editor.action('scientific_io_inspect');state=settle(editor);assert not state['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_apply');state=settle(editor);assert not state['error'] and not state['open']
    exported=copy.deepcopy(editor.workflow[-1]);assert exported['version']==10 and exported['storage']=='tiled'
    assert io.inspect_image_file(path).format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_TILED_V1
    assert io.read_image_series(path).dataset.numpy('TCZYXS').tobytes()==source
    raw=path.read_bytes();editor.action('review_scientific_io',record=exported);state=settle(editor)
    assert state['storage']=='tiled' and state.get('estimate') is None and not state['accept_loss']
    editor.action('scientific_io_draft',storage='strips');state=settle(editor)
    assert state['storage']=='strips' and not state['accept_loss']
    assert path.read_bytes()==raw and owner.undo_steps==0 and not owner.snapshot().dirty
    editor.action('cancel_scientific_io')


@pytest.mark.parametrize('patch',[{'version':1},{'version':9},{'storage':'strips'},{'storage':True},
                                {'compression':'uncompressed'},{'predictor':'none'},
                                {'components':3},{'dtype':'uint8'},{'dtype':'float32'},
                                {'sample_policy':'preserve_ieee_float32'}])
def test_tiled_record_mutations_are_refused(patch,tmp_path):
    editor=opened(tmp_path);record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty


@pytest.mark.parametrize('field',['storage','dtype','components','sample_policy','format_id'])
def test_tiled_record_required_fields(field,tmp_path):
    editor=opened(tmp_path);record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError)):editor.action('review_scientific_io',record=record)


@pytest.mark.parametrize('patch',[{'storage':'tiled','compression':'lzw','predictor':'floating'},
                                {'storage':'tiled','compression':'deflate','predictor':'floating'},
                                {'storage':'unknown'},{'storage':False}])
def test_incompatible_storage_patch_is_atomic(patch,tmp_path):
    editor=opened(tmp_path);editor.action('show_scientific_export',path=str(tmp_path/'absent.tif'));settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_draft',**patch)
    state=settle(editor);assert state['storage']=='strips' and state['compression']=='uncompressed'
    assert state.get('estimate') is None and not (tmp_path/'absent.tif').exists()


@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_explicit_flat_tile_import_preserves_c_fast_page_order(row,tmp_path):
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_file(ROOT/row['file']);settle(editor)
    with pytest.raises((ValueError,RuntimeError)):editor.action('scientific_io_flat_open')
    editor.action('scientific_io_draft',accept_flat_pixels_only=True)
    editor.action('scientific_io_flat_open');state=settle(editor);assert not state['error']
    data=editor.live_document().native_snapshot().dataset;shape=row['shape']
    original=np.frombuffer(bytes.fromhex(row['hex']),dtype='<u2').reshape(shape[:-1])
    assert data.shape==(1,1,shape[0]*shape[1]*shape[2],shape[3],shape[4],1)
    assert data.numpy('ZYX').tobytes()==original.transpose(0,2,1,3,4).tobytes()
    assert data.calibration.unit=='px' and not editor.live_document().native_snapshot().dirty


def test_flat_tile_import_reserves_the_copy_before_decoding(tmp_path):
    path=ROOT/'wide-le-zero.tif';before=path.read_bytes();low,high=64*1024*1024,80*1024*1024
    while low<high:
        middle=(low+high)//2;limits=io.ImageFileLimits();limits.max_working_bytes=middle
        try:io.read_image_series(path,limits=limits)
        except (ValueError,RuntimeError):low=middle+1
        else:high=middle
    limits=io.ImageFileLimits();limits.max_working_bytes=low
    assert io.read_image_series(path,limits=limits).dataset.shape[1]==2
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_file(path);settle(editor)
    editor.action('scientific_io_draft',limits={'max_working_bytes':low})
    editor.action('scientific_io_inspect');state=settle(editor);assert not state['error']
    editor.action('scientific_io_draft',accept_flat_pixels_only=True)
    with pytest.raises((ValueError,RuntimeError),match='working-byte limit'):
        editor.action('scientific_io_flat_open')
    assert not editor.document_infos and path.read_bytes()==before
