"""BigTIFF shared ImGui form, explicit v15 workflow and renewed consent."""
import copy
import pytest
from imagejxx import scientific_io as io
from imagejxx.ui.app import Editor
from test_bigtiff_io import VALID, read_request, write_request
from test_lzw_tiff_editor import settle

CASES = [(d,r) for d,r in VALID if d.name=='scientific-bigtiff' and '-edges-' in r['file'] and r['endian']=='big']


def opened(directory,row,tmp_path):
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor.recording = True
    editor.open_file(directory/row['file'])
    state = settle(editor)
    assert not state['error'] and state['container']=='bigtiff'
    if row['pattern']!='finite':editor.action('scientific_io_draft',sample_policy=row['sample_policy'])
    editor.action('scientific_io_draft',confirm_axes=True)
    editor.action('scientific_io_apply')
    assert not settle(editor)['error']
    assert editor.live_document().native_snapshot().dataset.numpy('TCZYXS').tobytes().hex()==row['hex']
    return editor


@pytest.mark.parametrize('directory,row',CASES,ids=[r['file'] for _,r in CASES])
def test_bigtiff_open_export_and_inert_replay(directory,row,tmp_path):
    editor=opened(directory,row,tmp_path)
    record=copy.deepcopy(editor.workflow[-1])
    assert record['version']==15 and record['container']=='bigtiff'
    assert record['storage']==row['storage'] and record['format_id']==row['format_id']
    editor.action('review_scientific_io',record=record)
    state=settle(editor)
    assert state['open'] and not state['confirm_axes'] and not state['accept_loss']
    assert len(editor.document_infos)==1
    editor.action('cancel_scientific_io')
    path=tmp_path/'export.tif'
    editor.action('show_scientific_export',path=str(path))
    assert settle(editor)['container']=='classic'
    choices=dict(container='bigtiff',storage=row['storage'],compression={1:'uncompressed',8:'deflate',5:'lzw',32773:'packbits'}[row['compression']],predictor={1:'none',2:'horizontal',3:'floating'}[row['predictor']])
    editor.action('scientific_io_draft',**choices)
    editor.action('scientific_io_inspect')
    assert not settle(editor)['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True)
    editor.action('scientific_io_apply')
    assert not settle(editor)['error']
    assert io.inspect_image_file(path).container==io.TiffContainer.BIGTIFF
    assert io.inspect_image_file(path).format==write_request(row).format
    assert io.read_image_series(path,request=read_request(row)).dataset.numpy('TCZYXS').tobytes().hex()==row['hex']
    record=copy.deepcopy(editor.workflow[-1])
    assert record['version']==15 and record['container']=='bigtiff'
    before=path.read_bytes()
    editor.action('review_scientific_io',record=record)
    state=settle(editor)
    assert state['container']=='bigtiff' and state['storage']==row['storage']
    assert not state['accept_loss'] and state.get('estimate') is None
    assert path.read_bytes()==before
    editor.action('scientific_io_inspect')
    assert not settle(editor)['error']
    editor.action('scientific_io_draft',accept_pixels_and_calibration_only=True,replace_existing=True)
    editor.action('scientific_io_draft',container='classic')
    state=settle(editor)
    assert not state['accept_loss'] and not state['replace_existing'] and state.get('estimate') is None


@pytest.mark.parametrize('field',['container','storage','compression','predictor','dtype','components','sample_policy'])
def test_bigtiff_v15_requires_complete_choices(field,tmp_path):
    editor=opened(*CASES[0],tmp_path)
    record=copy.deepcopy(editor.workflow[-1]);record.pop(field)
    with pytest.raises((ValueError,RuntimeError,KeyError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty


@pytest.mark.parametrize('patch',[{'version':14},{'version':1},{'container':'classic'},{'container':True},{'storage':'unknown'},{'compression':'jpeg'},{'predictor':'unknown'},{'format_id':io.format_id},{'components':2},{'sample_policy':'unknown'}])
def test_bigtiff_v15_rejects_mutated_records(patch,tmp_path):
    editor=opened(*CASES[0],tmp_path)
    record=copy.deepcopy(editor.workflow[-1]);record.update(patch)
    with pytest.raises((ValueError,RuntimeError,KeyError)):editor.action('review_scientific_io',record=record)
    assert len(editor.document_infos)==1 and not editor.live_document().native_snapshot().dirty
