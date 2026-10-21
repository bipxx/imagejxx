"""Integer horizontal prediction: independent carriers and independent output decoding."""
from pathlib import Path
import hashlib,json,struct,zlib
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io
from test_lzw_tiff_io import decode_lzw
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-horizontal'
ROWS=json.loads((ROOT/'SOURCE.json').read_bytes())['files']
VALID=[r for r in ROWS if r['valid']]

def write_request(row,predicted=True):
    r=io.ImageWriteRequest();name={'u8':'U8','u16':'U16','rgb8':'RGB8'}[row['dtype']]
    codec='LZW' if row['codec']==5 else 'DEFLATE'
    r.format=getattr(io.ImageFileFormat,f'SCIENTIFIC_TIFF_{name}_{codec}'+('_HORIZONTAL_V1' if predicted else '_V1'));return r

def decoded_wire(path,row):
    raw=path.read_bytes();assert raw[:2]==b'II';offset=struct.unpack_from('<I',raw,4)[0];pages=[]
    dtype='<u2' if row['dtype']=='u16' else 'u1';shape=row['shape'];mask=65535 if dtype=='<u2' else 255
    while offset:
        count=struct.unpack_from('<H',raw,offset)[0];tags={}
        for i in range(count):
            tag,kind,n,value=struct.unpack_from('<HHII',raw,offset+2+12*i);tags[tag]=(kind,n,value)
        def values(tag):
            kind,n,value=tags[tag];assert kind==4
            return [value] if n==1 else struct.unpack_from('<'+'I'*n,raw,value)
        assert tags[317]==(3,1,2) and tags[259][2]==(5 if row['codec']==5 else 8)
        strips=[(decode_lzw if row['codec']==5 else zlib.decompress)(raw[at:at+n]) for at,n in zip(values(273),values(279))]
        differences=np.frombuffer(b''.join(strips),dtype=dtype).reshape(shape[3:])
        pages.append((np.cumsum(differences,axis=1,dtype=np.uint64)&mask).astype(dtype).tobytes())
        offset=struct.unpack_from('<I',raw,offset+2+12*count)[0]
    return pages

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_independent_horizontal_carriers(row):
    path=ROOT/row['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
    if not row['valid']:
        with pytest.raises((ValueError,RuntimeError)):io.inspect_image_file(path)
        with pytest.raises((ValueError,RuntimeError)):io.read_image_series(path)
        return
    info=io.inspect_image_file(path);assert info.format==write_request(row).format
    assert info.series[0].shape==tuple(row['shape'])
    actual=io.read_image_series(path).dataset
    assert actual.numpy('TCZYXS').tobytes().hex()==row['hex']
    assert tuple(actual.calibration.spacing)==(.5,1.25,2.75) and actual.calibration.frame_interval==.125

@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_predictor_wire_bound_and_provenance(row,tmp_path):
    result=io.read_image_series(ROOT/row['file']);observer=ij.DatasetReadObserver();data=result.dataset.observe_reads(observer)
    write=write_request(row);estimate=io.estimate_image_write(data,result.metadata,request=write);assert observer.counts().plane_reads==0
    path=tmp_path/'horizontal.tif';io.write_image_series(path,data,result.metadata,request=write)
    shape=row['shape'];width=2 if row['dtype']=='u16' else 1;plane=shape[3]*shape[4]*shape[5]*width;raw=bytes.fromhex(row['hex'])
    expected=[raw[((t*shape[1]+c)*shape[2]+z)*plane:((t*shape[1]+c)*shape[2]+z+1)*plane] for t in range(shape[0]) for z in range(shape[2]) for c in range(shape[1])]
    assert decoded_wire(path,row)==expected and path.stat().st_size<=estimate.encoded_upper_bound
    assert observer.counts().plane_reads==shape[0]*shape[1]*shape[2]
    assert io.read_image_series(path).dataset.numpy('TCZYXS').tobytes()==raw
    before=path.read_bytes()
    with pytest.raises(RuntimeError):io.write_image_series(path,data,request=write)
    assert path.read_bytes()==before
    write.replace=io.FileReplacePolicy.REPLACE_EXISTING;io.write_image_series(path,data,result.metadata,request=write)
    plain=tmp_path/'plain.tif';io.write_image_series(plain,data,result.metadata,request=write_request(row,False))
    assert io.read_image_series(plain).dataset.numpy('TCZYXS').tobytes()==raw
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())

@pytest.mark.parametrize('codec',[8,5])
def test_prediction_reduces_smooth_u16_size(codec,tmp_path):
    values=np.broadcast_to(np.arange(1024,dtype='<u2'),(128,1024)).copy();data=ij.Dataset(values,axes='YX');row=dict(dtype='u16',codec=codec)
    for predicted,name in [(False,'plain'),(True,'horizontal')]:io.write_image_series(tmp_path/(name+'.tif'),data,request=write_request(row,predicted))
    assert (tmp_path/'horizontal.tif').stat().st_size*2<(tmp_path/'plain.tif').stat().st_size

def test_predictor_enum_append_and_default():
    assert [int(getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+kind+'_'+codec+'_HORIZONTAL_V1')) for codec in ['DEFLATE','LZW'] for kind in ['U16','U8','RGB8']]==list(range(16,22))
    assert io.ImageWriteRequest().format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_V1

@pytest.mark.parametrize('field',['max_file_bytes','max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes'])
def test_predictor_resource_limits_before_callbacks(field):
    limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):io.read_image_series(ROOT/VALID[0]['file'],limits=limits,checkpoint=lambda:events.append(1))
    assert not events
