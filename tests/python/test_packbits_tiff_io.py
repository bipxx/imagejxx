"""Independent PackBits carriers, exact wire words, bounds and atomic output."""
from pathlib import Path
import hashlib,json,struct
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-packbits'
ROWS=json.loads((ROOT/'SOURCE.json').read_bytes())['files']
VALID=[r for r in ROWS if r['valid']]
NAMES={'u8':'U8','u16':'U16','f32':'FLOAT32','rgb8':'RGB8'}
def request(row):
    r=io.ImageReadRequest()
    if row['dtype']=='f32':r.sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    return r
def write_request(row,zip=True):
    r=io.ImageWriteRequest();r.format=getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+NAMES[row['dtype']]+('_PACKBITS_V1' if zip else '_V1'));return r
def wire(path):
    raw=path.read_bytes();endian='<' if raw[:2]==b'II' else '>';offset=struct.unpack_from(endian+'I',raw,4)[0];pages=[]
    while offset:
        count=struct.unpack_from(endian+'H',raw,offset)[0];entries=count;tags={}
        for i in range(count):
            tag,kind,n,value=struct.unpack_from(endian+'HHII',raw,offset+2+12*i);tags[tag]=(kind,n,value)
        def values(tag):
            kind,n,value=tags[tag];assert kind==4
            return [value] if n==1 else struct.unpack_from(endian+'I'*n,raw,value)
        assert tags[259][2] ==32773
        strips=[]
        for start,n in zip(values(273),values(279)):
            encoded=raw[start:start+n];decoded=bytearray();at=0
            row_bytes=tags[256][2]*tags[277][2]*(tags[258][2]//8 if tags[277][2]==1 else 1)
            while at<len(encoded):
                code=encoded[at];at+=1
                if code==128:continue
                count=code+1 if code<128 else 257-code
                assert count<=row_bytes-len(decoded)%row_bytes
                if code<128:
                    assert at+count<=len(encoded);decoded.extend(encoded[at:at+count]);at+=count
                else:
                    assert at<len(encoded);decoded.extend(bytes([encoded[at]])*count);at+=1
            strips.append(bytes(decoded))
        pages.append(b''.join(strips));offset=struct.unpack_from(endian+'I',raw,offset+2+12*entries)[0]
    return pages

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_independent_carriers_and_payload_boundary(row):
    path=ROOT/row['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
    if not row['valid']:
        if row['descriptor_valid']:assert io.inspect_image_file(path).series[0].shape==tuple(row['shape'])
        else:
            with pytest.raises(ValueError):io.inspect_image_file(path)
        with pytest.raises((ValueError,RuntimeError)):io.read_image_series(path,request=request(row))
        return
    info=io.inspect_image_file(path);assert info.format==write_request(row).format
    assert info.series[0].shape==tuple(row['shape'])
    actual=io.read_image_series(path,request=request(row)).dataset
    assert actual.numpy('TCZYXS').tobytes().hex()==row['hex']
    assert tuple(actual.calibration.spacing)==(.5,1.25,2.75) and actual.calibration.frame_interval==.125
    if row['dtype']=='f32':
        with pytest.raises(ValueError):io.read_image_series(path)

@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_compressed_wire_bound_roundtrip_and_uncompressed_provenance(row,tmp_path):
    result=io.read_image_series(ROOT/row['file'],request=request(row));observer=ij.DatasetReadObserver();data=result.dataset.observe_reads(observer)
    write=write_request(row);estimate=io.estimate_image_write(data,result.metadata,request=write);assert observer.counts().plane_reads==0
    path=tmp_path/'compressed.tif';io.write_image_series(path,data,result.metadata,request=write)
    assert observer.counts().plane_reads==8 and path.stat().st_size<=estimate.encoded_upper_bound
    width={'u8':1,'u16':2,'f32':4,'rgb8':1}[row['dtype']];shape=row['shape'];plane=shape[3]*shape[4]*shape[5]*width
    raw=bytes.fromhex(row['hex']);expected=[raw[((t*2+c)*2+z)*plane:((t*2+c)*2+z+1)*plane] for t in range(2) for z in range(2) for c in range(2)]
    assert wire(path)==expected
    assert io.read_image_series(path,request=request(row)).dataset.numpy('TCZYXS').tobytes()==raw
    before=path.read_bytes()
    with pytest.raises(RuntimeError):io.write_image_series(path,data,request=write)
    assert path.read_bytes()==before;write.replace=io.FileReplacePolicy.REPLACE_EXISTING
    io.write_image_series(path,data,result.metadata,request=write)
    io.write_image_series(tmp_path/'uncompressed.tif',data,result.metadata,request=write_request(row,False))
    assert io.read_image_series(tmp_path/'uncompressed.tif',request=request(row)).dataset.numpy('TCZYXS').tobytes()==raw
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())

@pytest.mark.parametrize('dtype',['u8','u16','f32','rgb8'])
def test_compression_reduces_size_and_uses_real_bound(dtype,tmp_path):
    row=dict(dtype=dtype);a=np.zeros((130,129,3) if dtype=='rgb8' else (130,129),dtype={'u8':'u1','u16':'<u2','f32':'<f4','rgb8':'u1'}[dtype]);data=ij.Dataset(a,axes='YXS' if dtype=='rgb8' else 'YX')
    estimate=io.estimate_image_write(data,request=write_request(row));io.write_image_series(tmp_path/'zip.tif',data,request=write_request(row));io.write_image_series(tmp_path/'raw.tif',data,request=write_request(row,False))
    assert (tmp_path/'zip.tif').stat().st_size*4<(tmp_path/'raw.tif').stat().st_size
    assert (tmp_path/'zip.tif').stat().st_size<estimate.encoded_upper_bound

@pytest.mark.parametrize('field',['max_file_bytes','max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes'])
def test_read_resource_refusal_before_callbacks(field):
    row=VALID[0];limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):io.read_image_series(ROOT/row['file'],limits=limits,checkpoint=lambda:events.append(1),progress=lambda *a:events.append(a))
    assert not events

@pytest.mark.parametrize('mode',['cancel','callback'])
def test_partial_export_aborts_without_publication(mode,tmp_path):
    row=next(r for r in VALID if r['dtype']=='rgb8');data=io.read_image_series(ROOT/row['file']).dataset
    path=tmp_path/'existing.tif';path.write_bytes(b'preserved');write=write_request(row);write.replace=io.FileReplacePolicy.REPLACE_EXISTING
    token=ij.CancellationToken();events=[]
    def stage(name,n,total):
        if name=='Scientific TIFF rows':
            events.append(n)
            if n==65:
                if mode=='cancel':token.cancel()
                else:raise RuntimeError('injected callback fault')
    with pytest.raises((ij.OperationCancelled,RuntimeError)):
        io.write_image_series(path,data,request=write,cancellation=token,stage_progress=stage)
    assert 65 in events and path.read_bytes()==b'preserved' and list(tmp_path.iterdir())==[path]

def test_legacy_format_ordinals_and_default():
    assert [int(getattr(io.ImageFileFormat,n)) for n in ['SCIENTIFIC_TIFF_U16_V1','SCIENTIFIC_TIFF_FLOAT32_V1','SCIENTIFIC_TIFF_U8_V1','SCIENTIFIC_TIFF_RGB8_V1']]==list(range(4))
    assert io.ImageWriteRequest().format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_V1
    assert [int(getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+n+'_DEFLATE_V1')) for n in ['U16','FLOAT32','U8','RGB8']]==list(range(4,8))
    assert [int(getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+n+'_PACKBITS_V1')) for n in ['U16','FLOAT32','U8','RGB8']]==list(range(8,12))

@pytest.mark.parametrize('dtype',['u8','u16','f32','rgb8'])
def test_incompressible_strips_respect_bound_and_exact_words(dtype,tmp_path):
    row=dict(dtype=dtype);rng=np.random.default_rng(8521)
    shape=(67,259,3) if dtype=='rgb8' else (67,259)
    values=rng.integers(0,256 if dtype in ['u8','rgb8'] else 65536 if dtype=='u16' else 2**32,size=shape,dtype={'u8':'u1','rgb8':'u1','u16':'<u2','f32':'<u4'}[dtype])
    if dtype=='f32':values=values.view('<f4')
    data=ij.Dataset(values,axes='YXS' if dtype=='rgb8' else 'YX',sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32 if dtype=='f32' else ij.FloatingSamplePolicy.FINITE_ONLY)
    estimate=io.estimate_image_write(data,request=write_request(row));path=tmp_path/'random.tif';io.write_image_series(path,data,request=write_request(row))
    assert path.stat().st_size<=estimate.encoded_upper_bound
    assert io.read_image_series(path,request=request(row)).dataset.numpy('YXS' if dtype=='rgb8' else 'YX').tobytes()==values.tobytes()
