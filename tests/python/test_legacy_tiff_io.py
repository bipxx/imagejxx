"""Freeze legacy raster/stack behavior before changing ordinary-open routing."""
from pathlib import Path
import imagejxx as ij
from imagejxx.ui.app import Editor
from imagejxx import scientific_io as io
import numpy as np
import pytest
import tifffile


def pixels(dtype,samples,planes):
    z,y,x,s=np.indices((planes,3,5,samples))
    value=(-2.5+z*64+y*8+x+s*.125) if np.dtype(dtype).kind=='f' else z*64+y*8+x+s
    return value.astype(dtype)


@pytest.mark.parametrize('dtype',['uint8','uint16','float32','float64'])
@pytest.mark.parametrize('samples',[1,3,4])
@pytest.mark.parametrize('planes',[1,2])
def test_legacy_tiff_open_export_literal_types_and_samples(tmp_path,dtype,samples,planes):
    expected=pixels(dtype,samples,planes)
    data=expected if samples>1 else expected[...,0]
    if planes==1:data=data[0]
    path=tmp_path/'input.tif'
    options={'photometric':'rgb','planarconfig':'contig'} if samples>1 else {'photometric':'minisblack'}
    tifffile.imwrite(path,data,metadata=None,**options)
    with tifffile.TiffFile(path) as f:
        assert len(f.pages)==planes and all(page.samplesperpixel==samples for page in f.pages)
    editor=Editor(preferences=tmp_path/'preferences.json');editor.open_legacy_file(path)
    live=editor.live_document();before=live.native_snapshot()
    assert before.dataset.shape==(1,1,planes,3,5,samples) and tuple(before.dataset.calibration.spacing)==(1,1,1)
    np.testing.assert_array_equal(before.dataset.numpy('TCZYXS'),expected[None,None])
    output=tmp_path/'output.tif';unrelated=Path(str(output)+'.imagejxx.tmp');unrelated.write_bytes(b'unrelated owned file')
    editor.action('save_pixels',path=str(output))
    assert unrelated.read_bytes()==b'unrelated owned file'
    with tifffile.TiffFile(output) as f:
        assert len(f.pages)==planes
        for i,page in enumerate(f.pages):
            observed=page.asarray();assert observed.dtype==np.dtype(dtype)
            np.testing.assert_array_equal(observed,expected[i] if samples>1 else expected[i,...,0])
    assert live.native_snapshot().shares_data(before) and not live.native_snapshot().dirty and live.native_owner.undo_steps==0
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())


@pytest.mark.parametrize('dtype',['uint8','uint16'])
@pytest.mark.parametrize('photo',[0,1])
@pytest.mark.parametrize('orientation',[1,2,3,4])
def test_legacy_orientation_and_miniswhite(tmp_path,dtype,photo,orientation):
    original=np.arange(15,dtype=dtype).reshape(3,5)
    path=tmp_path/'oriented.tiff'
    tifffile.imwrite(path,original,photometric=photo,metadata=None,extratags=[(274,'H',1,orientation,False)])
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_legacy_file(path)
    expected=np.iinfo(dtype).max-original if photo==0 else original
    if orientation in (2,3):expected=np.flip(expected,axis=1)
    if orientation in (3,4):expected=np.flip(expected,axis=0)
    np.testing.assert_array_equal(editor.live_document().native_snapshot().dataset.numpy('YX'),expected)


def test_explicit_scientific_and_ordinary_flat_route_contract(tmp_path):
    path=Path(__file__).parents[1]/'fixtures/scientific-tiff-u16/little.tif'
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_legacy_file(path)
    snapshot=editor.live_document().native_snapshot()
    assert snapshot.dataset.shape==(1,1,12,3,5,1) and tuple(snapshot.dataset.calibration.spacing)==(1,1,1)
    for p in range(12):
        t,z,c=p//6,(p//3)%2,p%3
        expected=np.array([[10000*t+1000*c+100*z+5*y+x for x in range(5)] for y in range(3)],dtype=np.uint16)
        np.testing.assert_array_equal(snapshot.dataset.plane(coordinate=p),expected)
    scientific=io.read_image_series(path)
    assert scientific.dataset.shape==(2,3,2,3,5,1) and tuple(scientific.dataset.calibration.spacing)==(.5,1.25,2.75)


@pytest.mark.parametrize('suffix',['png','npy'])
def test_other_memory_encoded_formats_use_shared_publication(tmp_path,suffix):
    expected=np.arange(15,dtype=np.uint8).reshape(3,5)
    source=tmp_path/'input.tif';tifffile.imwrite(source,expected,metadata=None)
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_legacy_file(source)
    output=tmp_path/('output.'+suffix);editor.action('save_pixels',path=str(output))
    np.testing.assert_array_equal(ij.imread(output),expected)
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())


def test_failed_legacy_publication_preserves_occupied_directory(tmp_path):
    path=tmp_path/'input.tif';tifffile.imwrite(path,np.arange(15,dtype=np.uint16).reshape(3,5),metadata=None)
    editor=Editor(preferences=tmp_path/'prefs.json');editor.open_legacy_file(path)
    before=editor.live_document().native_snapshot()
    occupied=tmp_path/'occupied.tif';occupied.mkdir();(occupied/'keep').write_bytes(b'keep')
    with pytest.raises((RuntimeError,OSError)):editor.action('save_pixels',path=str(occupied))
    assert (occupied/'keep').read_bytes()==b'keep' and editor.live_document().native_snapshot().shares_data(before)
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())
