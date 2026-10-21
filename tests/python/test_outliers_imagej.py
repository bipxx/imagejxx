"""Finite scalar Remove Outliers pixels; editor command remains a separate gate."""
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest

CORPUS=json.loads((Path(__file__).parents[1]/'fixtures/outliers-imagej154r.json').read_bytes())

@pytest.mark.parametrize('row',CORPUS['cases'],ids=lambda r:f"{r['dtype']}-s{r['signal']}-r{r['radius']}-{r['which']}-i{int(r['inverted_lut'])}-m{r['mode']}-a{r['selection']}")
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
        filtered=ij.remove_outliers_imagej(source[z],row['radius'],row['threshold'],which=row['which'],inverted_lut=row['inverted_lut'],extent=region['extent'])
        assert filtered.dtype==source.dtype and filtered.shape==source[z].shape
        assert source[z].tobytes()==original and not np.shares_memory(filtered,source)
        if region['mask'] is not None:filtered=np.where(region['mask'],filtered,source[z]).astype(source.dtype)
        result[z]=filtered
    assert result.tobytes()==expected.tobytes()
    assert row['initial_range']==row['final_range'] and row['initial_threshold']==row['final_threshold']

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('which,inverted',[('bright',False),('dark',True)])
def test_strict_boundary_threshold_rounding_and_original_source(dtype,which,inverted):
    source=np.full((5,5),40,dtype);source[2,2]=100
    assert ij.remove_outliers_imagej(source,1,60,which=which,inverted_lut=inverted).tobytes()==source.tobytes()
    assert ij.remove_outliers_imagej(source,1,np.nextafter(60.,0),which=which,inverted_lut=inverted).tobytes()==source.tobytes()
    result=ij.remove_outliers_imagej(source,1,59.99999,which=which,inverted_lut=inverted)
    assert result.tobytes()==np.full((5,5),40,dtype).tobytes()
    assert source[2,2]==100
    assert ij.remove_outliers_imagej(source,1,0,which=which,inverted_lut=not inverted).tobytes()==source.tobytes()

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
@pytest.mark.parametrize('radius',[0,.5,1,1.75,2.5,4.25])
@pytest.mark.parametrize('which,inverted',[('bright',False),('dark',False),('bright',True),('dark',True)])
def test_independent_sorted_neighborhood_oracle(dtype,radius,which,inverted):
    rng=np.random.default_rng(371)
    source=(rng.integers(0,201,size=(8,11))-50 if dtype==np.float32 else rng.integers(0,201,size=(8,11))).astype(dtype)
    if dtype==np.float32:source/=4
    # Independently construct the pinned ImageJ circular footprint, including
    # its small-radius compatibility rule; sorting does not use native median.
    r=1.75 if 1.5<=radius<1.75 else 2.85 if 2.5<=radius<2.85 else radius
    squared=int(r*r)+1;v=int(np.sqrt(squared+1e-10))
    offsets=[(dy,dx) for dy in range(-v,v+1) for dx in range(-int(np.sqrt(squared-dy*dy+1e-10)),int(np.sqrt(squared-dy*dy+1e-10))+1)]
    expected=source.copy();limit=np.float32(16.000001);sign=np.float32(-1 if inverted==(which=='dark') else 1)
    for y in range(8):
        for x in range(11):
            samples=[source[min(7,max(0,y+dy)),min(10,max(0,x+dx))] for dy,dx in offsets]
            mid=np.float32(sorted(samples)[len(samples)//2]);value=np.float32(source[y,x])
            if np.float32(value*sign+limit)<np.float32(mid*sign):expected[y,x]=mid
    actual=ij.remove_outliers_imagej(source,radius,float(limit),which=which,inverted_lut=inverted)
    assert actual.tobytes()==expected.tobytes()

@pytest.mark.parametrize('kwargs,error',[
    ({'radius':True},TypeError),({'radius':100.1},ValueError),({'radius':-1},ValueError),({'radius':float('nan')},ValueError),
    ({'threshold':True},TypeError),({'threshold':'50'},TypeError),({'threshold':-1},ValueError),({'threshold':float('inf')},ValueError),({'threshold':float(np.finfo(np.float32).max)*2},ValueError),
    ({'which':b'bright'},TypeError),({'which':'Bright'},ValueError),({'which':'both'},ValueError),({'inverted_lut':1},TypeError),({'inverted_lut':'false'},TypeError),
    ({'extent':(True,0,1,1)},TypeError),({'extent':(2,2,2,2)},ValueError),({'extent':(0,0,1.5,1)},TypeError)])
def test_argument_contract(kwargs,error):
    with pytest.raises(error):ij.remove_outliers_imagej(np.zeros((3,3),np.uint8),**kwargs)

@pytest.mark.parametrize('source',[np.zeros((3,3),np.float64),np.zeros((3,3,3),np.uint8),np.array([[np.nan]],np.float32),np.array([[np.inf]],np.float32)])
def test_unsupported_profiles(source):
    with pytest.raises(ValueError):ij.remove_outliers_imagej(source)

@pytest.mark.parametrize('dtype',[np.uint8,np.uint16,np.float32])
def test_strided_input_empty_extent_and_large_radius(dtype):
    source=np.arange(99,dtype=dtype).reshape(9,11)[::-2,::-2]
    result=ij.remove_outliers_imagej(source,100,0,extent=(1,1,0,0))
    assert result.dtype==source.dtype and result.tobytes()==source.tobytes()
    constant=np.full((1,1),7,dtype)
    assert ij.remove_outliers_imagej(constant,100,0).tobytes()==constant.tobytes()

def test_signed_zero_unchanged_and_boolean_scalar_contract():
    source=np.array([[-0.,0.],[0.,-0.]],np.float32)
    result=ij.remove_outliers_imagej(source,threshold=np.float32(50),inverted_lut=np.bool_(False))
    assert result.tobytes()==source.tobytes()
