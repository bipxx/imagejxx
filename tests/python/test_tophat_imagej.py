"""Finite scalar Top Hat pixels; editor command remains a separate gate."""
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest

CORPUS=json.loads((Path(__file__).parents[1]/'fixtures/tophat-imagej154r.json').read_bytes())

@pytest.mark.parametrize('row',CORPUS['cases'],ids=lambda r:f"{r['dtype']}-s{r['signal']}-r{r['radius']}-b{int(r['light_background'])}-d{int(r['dont_subtract'])}-i{int(r['inverted_lut'])}-m{r['mode']}-a{r['selection']}")
def test_actual_runner_pixels(row):
    source=np.stack([np.frombuffer(bytes.fromhex(v),dtype=row['dtype']).reshape(20,25) for v in row['input_hex']])
    expected=np.stack([np.frombuffer(bytes.fromhex(v),dtype=row['dtype']).reshape(20,25) for v in row['output_hex']])
    owner=ij.Document(ij.Dataset(source,axes='ZYX'))
    if row['selection']:
        edit=owner.begin_edit()
        edit.select(ij.Selection('rectangle',[[2,3],[21,16]]) if row['selection']==1 else ij.Selection('polygon',[[2,3],[20,2],[18,17],[5,15]]))
        owner.commit(edit)
    region=ij.rank_command_region(ij.resolve_rank_command(owner.snapshot()).target)
    result=source.copy()
    for z in range(3):
        if row['mode']==0 and z!=1:continue
        original=source[z].tobytes()
        filtered=ij.top_hat_imagej(source[z],row['radius'],light_background=row['light_background'],dont_subtract=row['dont_subtract'],inverted_lut=row['inverted_lut'],extent=region['extent'])
        assert filtered.dtype==source.dtype and filtered.shape==source[z].shape
        assert source[z].tobytes()==original and not np.shares_memory(filtered,source)
        if region['mask'] is not None:filtered=np.where(region['mask'],filtered,source[z]).astype(source.dtype)
        result[z]=filtered
    assert result.tobytes()==expected.tobytes()
    if row['dont_subtract']:
        assert row['initial_range']==row['final_range'] and row['initial_threshold']==row['final_threshold']


@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('radius',[0,.5,1,1.5,2.5,4.25])
@pytest.mark.parametrize('light,dont,inverted',[(bool(i&1),bool(i&2),bool(i&4)) for i in range(8)])
def test_independent_compound_oracle_and_extent(dtype,radius,light,dont,inverted):
    rng=np.random.default_rng(371)
    source=rng.integers(0,201,size=(8,11)).astype(dtype)
    if dtype==np.float32:source=(source-100)/4
    r=1.75 if 1.5<=radius<1.75 else 2.85 if 2.5<=radius<2.85 else radius
    squared=int(r*r)+1;v=int(np.sqrt(squared+1e-10))
    offsets=[(dy,dx) for dy in range(-v,v+1) for dx in range(-int(np.sqrt(squared-dy*dy+1e-10)),int(np.sqrt(squared-dy*dy+1e-10))+1)]
    closing=light!=inverted
    def extremum(image,maximum):
        out=np.empty_like(image)
        for y in range(8):
            for x in range(11):
                values=[image[min(7,max(0,y+dy)),min(10,max(0,x+dx))] for dy,dx in offsets]
                out[y,x]=(max if maximum else min)(values)
        return out
    background=extremum(extremum(source,closing),not closing)
    expected=background if dont else (source.astype(np.float32)-background.astype(np.float32)+np.float32(0 if not closing or dtype==np.float32 else np.iinfo(dtype).max)).astype(dtype)
    # Edge-touching rectangle: its output must agree with a separately computed
    # whole-image composition, while every outside sample remains untouched.
    result=source.copy();result[1:7,0:5]=expected[1:7,0:5]
    original=source.tobytes()
    actual=ij.top_hat_imagej(source,radius,light_background=light,dont_subtract=dont,inverted_lut=inverted,extent=(0,1,5,6))
    assert actual.tobytes()==result.tobytes()
    assert source.tobytes()==original and not np.shares_memory(actual,source)

@pytest.mark.parametrize('kwargs,error',[
    ({'radius':True},TypeError),({'radius':'1'},TypeError),({'radius':-1},ValueError),
    ({'radius':1000.1},ValueError),({'radius':float('nan')},ValueError),({'radius':float('inf')},ValueError),
    ({'light_background':1},TypeError),({'dont_subtract':'false'},TypeError),({'inverted_lut':0},TypeError),
    ({'extent':(True,0,1,1)},TypeError),({'extent':(2,2,2,2)},ValueError),({'extent':(0,0,1.5,1)},TypeError)])
def test_argument_contract(kwargs,error):
    with pytest.raises(error):ij.top_hat_imagej(np.zeros((3,3),np.uint8),**kwargs)

@pytest.mark.parametrize('source',[np.zeros((3,3),np.float64),np.zeros((3,3,3),np.uint8),np.array([[np.nan]],np.float32),np.array([[np.inf]],np.float32)])
def test_unsupported_profiles(source):
    with pytest.raises(ValueError):ij.top_hat_imagej(source)

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_strided_empty_and_large_radius(dtype):
    source=np.arange(99,dtype=dtype).reshape(9,11)[::-2,::-2]
    assert ij.top_hat_imagej(source,1000,extent=(1,1,0,0)).tobytes()==source.tobytes()
    assert ij.top_hat_imagej(np.full((1,1),7,dtype),101).tobytes()==np.zeros((1,1),dtype).tobytes()

def test_negative_float_closing_and_overflow():
    source=np.full((3,3),100,np.float32);source[1,1]=40
    assert ij.top_hat_imagej(source,1,light_background=True)[1,1]==-60
    source[:]=-np.finfo(np.float32).max;source[1,1]=np.finfo(np.float32).max
    with pytest.raises(ValueError,match='finite Float32'):ij.top_hat_imagej(source,1)

def test_numpy_boolean_flags():
    source=np.arange(9,dtype=np.uint8).reshape(3,3)
    assert np.array_equal(ij.top_hat_imagej(source,light_background=np.bool_(True),dont_subtract=np.bool_(True),inverted_lut=np.bool_(False)),ij.top_hat_imagej(source,light_background=True,dont_subtract=True))
