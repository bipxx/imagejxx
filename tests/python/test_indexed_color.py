"""Exact indexed palettes and indices, global stacks and callback ownership."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=json.loads((Path(__file__).parents[1]/'fixtures/indexed-color-imagej154r.json').read_bytes())


def pixels(row):
    h,w=row['shape'];array=np.array(row['input'],np.uint8).reshape(-1,h,w,3)
    return array[0] if row['kind']=='plane' else array


@pytest.mark.parametrize('row',REFERENCE['cases'],ids=lambda r:f"{r['kind']}-{r['pattern']}-{r['max_colors']}")
def test_pinned_imagej_palette_and_exact_indices(row):
    source=pixels(row);before=source.copy();progress=[]
    result=ij.quantize_indexed_color(source,row['max_colors'],progress=lambda a,b:progress.append((a,b)))
    count=len(row['palette']);assert result['color_count']==count and result['palette'].dtype==np.uint8 and result['palette'].shape==(256,3)
    np.testing.assert_array_equal(result['palette'][:count],row['palette']);assert not result['palette'][count:].any()
    expected=np.array(row['indices'],np.uint8).reshape(source.shape[:-1]);np.testing.assert_array_equal(result['indices'],expected)
    assert result['indices'].dtype==np.uint8 and result['indices'].max()<count
    np.testing.assert_array_equal(source,before)
    assert progress and progress[-1][0]==progress[-1][1] and all(a/b<=c/d for (a,b),(c,d) in zip(progress,progress[1:]))
    expanded=result['palette'][result['indices']]
    for n,plane in enumerate(result['indices'].reshape(-1,*row['shape'])):
        np.testing.assert_array_equal(ij.convert_rgb(plane,lut=result['palette']),expanded.reshape(-1,*row['shape'],3)[n])


@pytest.mark.parametrize('maximum',[False,True,1,0,-1,257,2.5,'16',np.bool_(True)])
def test_bad_palette_limits(maximum):
    with pytest.raises((TypeError,ValueError,OverflowError)):ij.quantize_indexed_color(np.zeros((1,2,3),np.uint8),maximum)


@pytest.mark.parametrize('shape,dtype',[((0,2,3),np.uint8),((2,3),np.uint8),((2,3,4),np.uint8),((1,2,3),np.float32),((1,2,3),np.uint16),((1,1,1,1,3),np.uint8)])
def test_bad_rgb_buffers(shape,dtype):
    with pytest.raises(ValueError):ij.quantize_indexed_color(np.zeros(shape,dtype))


@pytest.mark.parametrize('cap',[True,False,0,1,1024,2.5])
def test_bad_working_limits_refuse_before_callbacks(cap):
    calls=[]
    with pytest.raises((ValueError,TypeError)):ij.quantize_indexed_color(np.zeros((2,3,3),np.uint8),checkpoint=lambda:calls.append(1),max_working_bytes=cap)
    assert not calls


def test_precancel_and_midstage_cancellation():
    source=np.arange(192,dtype=np.uint8).reshape(2,4,8,3);calls=[];token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.quantize_indexed_color(source,cancellation=token,checkpoint=lambda:calls.append(1))
    assert not calls
    token=ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):ij.quantize_indexed_color(source,cancellation=token,progress=lambda a,b:token.cancel())


def test_checkpoint_exception_and_input_snapshot():
    row=REFERENCE['cases'][20];source=pixels(row);expected=ij.quantize_indexed_color(source,row['max_colors'])
    actual=ij.quantize_indexed_color(source,row['max_colors'],checkpoint=lambda:source.fill(0))
    np.testing.assert_array_equal(actual['indices'],expected['indices']);np.testing.assert_array_equal(actual['palette'],expected['palette'])
    def fail():raise RuntimeError('owned callback failure')
    with pytest.raises(RuntimeError,match='owned callback failure'):ij.quantize_indexed_color(source,checkpoint=fail)


def test_strided_and_fortran_inputs_remain_owned():
    row=REFERENCE['cases'][-1];source=pixels(row)
    reference=ij.quantize_indexed_color(source,np.int64(31))
    reversed=ij.quantize_indexed_color(source[:,:,::-1,:],np.int64(31))
    np.testing.assert_array_equal(reversed['palette'],reference['palette']);np.testing.assert_array_equal(reversed['indices'],reference['indices'][:,:,::-1])
    fortran=ij.quantize_indexed_color(np.asfortranarray(source),31)
    np.testing.assert_array_equal(fortran['indices'],reference['indices'])
    source.fill(0);assert fortran['palette'].any()
