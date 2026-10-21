"""Literal PNM encoding and file ownership across core and desktop callers."""
from pathlib import Path
import imagejxx as ij
from imagejxx.ui.app import Editor
import numpy as np
import pytest


def save(route,path,pixels,tmp_path):
    if route == 'core':
        ij.imwrite(path,pixels)
        return
    editor = Editor(preferences=tmp_path/'prefs.json')
    editor.add_document(pixels)
    live = editor.live_document(); before = live.native_snapshot()
    history = live.native_owner.undo_steps
    editor.action('save_pixels',path=str(path))
    assert live.native_snapshot().shares_data(before)
    assert live.native_owner.undo_steps == history and live.native_snapshot().dirty == before.dirty


@pytest.mark.parametrize('route',['core','desktop'])
@pytest.mark.parametrize('suffix',['pgm','ppm','pnm'])
@pytest.mark.parametrize('dtype',['uint8','uint16'])
@pytest.mark.parametrize('samples',[1,3])
def test_literal_pnm_bytes_preserve_fixed_suffix(tmp_path,route,suffix,dtype,samples):
    y,x,s = np.indices((129,257,samples))
    pixels = ((y*1021+x*257+s) % (256 if dtype=='uint8' else 65536)).astype(dtype)
    pixels.flat[:4] = [10,13,32,35]
    if samples == 1: pixels = pixels[...,0]
    path = tmp_path/f'输出.{suffix}'
    sentinel = Path(str(path)+'.imagejxx.tmp'); sentinel.write_bytes(b'unrelated owned bytes')
    path.write_bytes(b'previous complete target')
    save(route,path,pixels,tmp_path)
    maximum = 255 if dtype=='uint8' else 65535
    header = f'P{5 if samples==1 else 6}\n257 129\n{maximum}\n'.encode('ascii')
    expected = pixels.astype('u1' if dtype=='uint8' else '>u2').tobytes()
    assert path.read_bytes() == header+expected
    assert sentinel.read_bytes() == b'unrelated owned bytes'
    np.testing.assert_array_equal(ij.imread(path),pixels)
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())


@pytest.mark.parametrize('route',['core','desktop'])
@pytest.mark.parametrize('invalid',['float','rgba'])
def test_rejected_pnm_keeps_target_and_unrelated_suffix(tmp_path,route,invalid):
    pixels = np.zeros((3,5),dtype=np.float32) if invalid=='float' else np.zeros((3,5,4),dtype=np.uint8)
    path = tmp_path/'target.pnm'; path.write_bytes(b'previous target')
    sentinel = Path(str(path)+'.imagejxx.tmp'); sentinel.write_bytes(b'other owner')
    with pytest.raises((ValueError,RuntimeError)):
        save(route,path,pixels,tmp_path)
    assert path.read_bytes() == b'previous target' and sentinel.read_bytes() == b'other owner'
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())


@pytest.mark.parametrize('route',['core','desktop'])
def test_failed_pnm_publication_leaves_occupied_directory(tmp_path,route):
    path = tmp_path/'occupied.pnm'; path.mkdir(); (path/'keep').write_bytes(b'keep')
    sentinel = Path(str(path)+'.imagejxx.tmp'); sentinel.write_bytes(b'other owner')
    with pytest.raises((ValueError,RuntimeError,OSError)):
        save(route,path,np.zeros((3,5),dtype=np.uint8),tmp_path)
    assert (path/'keep').read_bytes() == b'keep' and sentinel.read_bytes() == b'other owner'
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())


@pytest.mark.parametrize('route',['core','desktop'])
def test_pnm_nul_path_cannot_replace_prefix_target(tmp_path,route):
    path = tmp_path/'prefix.pnm'; path.write_bytes(b'prefix target')
    with pytest.raises((ValueError,RuntimeError,OSError)):
        save(route,str(path)+'\x00suffix.pnm',np.zeros((3,5),dtype=np.uint8),tmp_path)
    assert path.read_bytes() == b'prefix target'
