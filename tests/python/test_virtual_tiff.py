"""Virtual TIFF regions against pinned independent wire carriers."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import os
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io

ROOT=Path(__file__).parents[1]/'fixtures'
FOLDERS={'scientific-tiff-horizontal':None,'scientific-tiff-floating':'<f4',
         'scientific-tiff-tiled':'<u2','scientific-bigtiff':None,'bigtiff-libtiff471':None}
TYPES={'u8':'u1','uint8':'u1','rgb8':'u1','u16':'<u2','uint16':'<u2','f32':'<f4','float32':'<f4'}
CASES=[]
for folder,default in FOLDERS.items():
    for row in json.loads((ROOT/folder/'SOURCE.json').read_bytes())['files']:
        dtype=TYPES.get(row.get('dtype'),default)
        if row['valid'] and 'hex' in row and dtype is not None:
            a=np.frombuffer(bytes.fromhex(row['hex']),dtype=dtype)
            if np.isfinite(a).all():CASES.append((folder,row,dtype))

@pytest.mark.parametrize('folder,row,dtype',CASES,ids=[f+'/'+r['file'] for f,r,d in CASES])
def test_independent_carriers(folder,row,dtype):
    path=ROOT/folder/row['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
    expected=np.frombuffer(bytes.fromhex(row['hex']),dtype=dtype).reshape(row['shape'])
    result=io.open_virtual_image_series(path);data=result.dataset
    assert not data.has_eager_planes and data.shape==tuple(row['shape'])
    actual=data.numpy('TCZYXS');assert actual.tobytes()==expected.tobytes()
    t,c,z,y,x,s=row['shape'];w,h=min(3,x),min(2,y);left,top=x-w,y-h
    region=data.region(left,top,w,h,t=t-1,c=c-1,z=z-1)
    assert region.tobytes()==expected[t-1,c-1,z-1,top:,left:].tobytes()
    for view,coordinate,answer in [('xz',y-1,expected[t-1,c-1,:,y-1]),('yz',x-1,expected[t-1,c-1,:,:,x-1])]:
        assert data.plane(view,coordinate,t=t-1,c=c-1).tobytes()==answer.tobytes()

def write(path,pixels,fmt=None,policy=None):
    axes='TCZYXS' if pixels.ndim==6 else 'TCZYX' if pixels.ndim==5 else 'YX'
    data=ij.Dataset(pixels,axes=axes,**({'sample_policy':policy} if policy is not None else {}))
    request=io.ImageWriteRequest()
    request.format=fmt or io.ImageFileFormat.SCIENTIFIC_TIFF_U16_V1
    io.write_image_series(path,data,request=request)

@pytest.mark.parametrize('tiled',[False,True])
@pytest.mark.parametrize('codec',['','DEFLATE_','LZW_','PACKBITS_'])
def test_cross_block_regions_and_concurrent_reads(tiled,codec,tmp_path):
    name='SCIENTIFIC_TIFF_U16_'+codec+('TILED_V1' if tiled else 'V1')
    values=(np.arange(2*2*3*130*129).reshape(2,2,3,130,129)%65536).astype(np.uint16)
    path=tmp_path/'data.tif';write(path,values,getattr(io.ImageFileFormat,name))
    data=io.open_virtual_image_series(path).dataset
    def read(i):
        t,c,z=i%2,(i//2)%2,(i//4)%3
        answer=data.region(61,62,9,8,t=t,c=c,z=z)
        np.testing.assert_array_equal(answer,values[t,c,z,62:70,61:70])
    with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(read,range(24)))

def test_metadata_only_open_and_region_do_not_validate_unread_pixels(tmp_path):
    values=np.arange(64,dtype=np.float32).reshape(8,8);values[7,7]=np.nan
    path=tmp_path/'nan.tif';write(path,values,io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_V1,ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    data=io.open_virtual_image_series(path).dataset
    np.testing.assert_array_equal(data.region(0,0,2,2),values[:2,:2])
    with pytest.raises(ValueError):data.plane()

def test_bounded_region_can_open_when_full_decoded_plane_does_not_fit(tmp_path):
    values=np.arange(512*512,dtype=np.uint16).reshape(512,512);path=tmp_path/'large.tif';write(path,values)
    limits=io.ImageFileLimits();limits.max_codec_bytes=65536;limits.max_working_bytes=300000
    data=io.open_virtual_image_series(path,limits=limits).dataset
    np.testing.assert_array_equal(data.region(400,401,3,2),values[401:403,400:403])
    with pytest.raises((ValueError,RuntimeError)):data.plane()
    with pytest.raises((ValueError,RuntimeError)):io.read_image_series(path,limits=limits)

@pytest.mark.parametrize('change',['replace','rewrite','remove','truncate'])
def test_file_change_tokens_are_checked_for_every_new_read(change,tmp_path):
    path=tmp_path/'input.tif';write(path,np.arange(64,dtype=np.uint16).reshape(8,8));data=io.open_virtual_image_series(path).dataset
    old=data.plane().copy();original=path.read_bytes()
    if change=='replace':
        new=tmp_path/'new.tif';new.write_bytes(original);path.rename(tmp_path/'retired.tif');os.replace(new,path)
    elif change=='rewrite':path.write_bytes(original+b'changed')
    elif change=='truncate':path.write_bytes(original[:20])
    else:path.unlink()
    with pytest.raises((ValueError,RuntimeError,OSError)):data.region(0,0,1,1)
    np.testing.assert_array_equal(old,np.arange(64,dtype=np.uint16).reshape(8,8))

@pytest.mark.parametrize('policy',[ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32])
def test_virtual_sample_policy_is_explicitly_finite_only(policy,tmp_path):
    request=io.ImageReadRequest();request.sample_policy=policy
    with pytest.raises(ValueError):io.open_virtual_image_series(tmp_path/'not-opened.tif',request=request)

def test_cancellation_in_provider_blocks_uses_callers_execution_scope(tmp_path):
    values=np.arange(128*128,dtype=np.uint16).reshape(128,128);path=tmp_path/'data.tif';write(path,values,io.ImageFileFormat.SCIENTIFIC_TIFF_U16_LZW_TILED_V1)
    data=io.open_virtual_image_series(path).dataset;token=ij.CancellationToken();seen=[]
    def stage(name,done,total):
        if name=='Virtual TIFF blocks':seen.append(done);token.cancel()
    with pytest.raises(ij.OperationCancelled):
        io.write_image_series(tmp_path/'cancelled.tif',data,cancellation=token,stage_progress=stage)
    assert seen
    assert not (tmp_path/'cancelled.tif').exists()
    np.testing.assert_array_equal(data.region(0,0,2,2),values[:2,:2])
