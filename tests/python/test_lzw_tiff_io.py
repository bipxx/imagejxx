"""Independent LZW carriers, exact wire words, bounds and atomic output."""
from pathlib import Path
import hashlib,json,struct
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io
ROOT=Path(__file__).parents[1]/'fixtures/scientific-tiff-lzw'
ROWS=json.loads((ROOT/'SOURCE.json').read_bytes())['files']
VALID=[r for r in ROWS if r['valid']]
NAMES={'u8':'U8','u16':'U16','f32':'FLOAT32','rgb8':'RGB8'}
def request(row):
    r=io.ImageReadRequest()
    if row['dtype']=='f32':r.sample_policy=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32
    return r
def write_request(row,zip=True):
    r=io.ImageWriteRequest();r.format=getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+NAMES[row['dtype']]+('_LZW_V1' if zip else '_V1'));return r
def decode_lzw(raw):
    position=0;table={i:bytes([i]) for i in range(256)};width=9;next_code=258;previous=None;out=bytearray()
    def take(n):
        nonlocal position
        assert position+n<=len(raw)*8
        bits=''.join(f'{b:08b}' for b in raw[position//8:(position+n+7)//8]);value=int(bits[position%8:position%8+n],2);position+=n;return value
    assert take(9)==256
    while True:
        code=take(width)
        if code==256:table={i:bytes([i]) for i in range(256)};width=9;next_code=258;previous=None;continue
        if code==257:
            assert len(raw)*8-position<8 and all(((raw[i//8]>>(7-i%8))&1)==0 for i in range(position,len(raw)*8));return bytes(out)
        value=table[code] if code in table else previous+previous[:1] if previous is not None and code==next_code else None
        assert value is not None
        out.extend(value)
        if previous is not None and next_code<4096:
            table[next_code]=previous+value[:1];next_code+=1
            if width<12 and next_code==(1<<width)-1:width+=1
        previous=value

def wire(path):
    raw=path.read_bytes();endian='<' if raw[:2]==b'II' else '>';offset=struct.unpack_from(endian+'I',raw,4)[0];pages=[]
    while offset:
        count=struct.unpack_from(endian+'H',raw,offset)[0];entries=count;tags={}
        for i in range(count):
            tag,kind,n,value=struct.unpack_from(endian+'HHII',raw,offset+2+12*i);tags[tag]=(kind,n,value)
        def values(tag):
            kind,n,value=tags[tag];assert kind==4
            return [value] if n==1 else struct.unpack_from(endian+'I'*n,raw,value)
        assert tags[259][2]==5
        strips=[]
        for start,n in zip(values(273),values(279)):
            strips.append(decode_lzw(raw[start:start+n]))
        pages.append(b''.join(strips));offset=struct.unpack_from(endian+'I',raw,offset+2+12*entries)[0]
    return pages

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_independent_carriers_and_payload_boundary(row):
    path=ROOT/row['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
    if row['file']=='predictor.tif':
        info=io.inspect_image_file(path)
        assert info.format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_LZW_HORIZONTAL_V1
        source=np.frombuffer(bytes.fromhex(row['hex']),'<u2').reshape(row['shape'])
        expected=(np.cumsum(source,axis=4,dtype=np.uint64)&65535).astype('<u2')
        assert io.read_image_series(path).dataset.numpy('TCZYXS').tobytes()==expected.tobytes()
        return
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
    assert observer.counts().plane_reads==row['shape'][0]*row['shape'][1]*row['shape'][2] and path.stat().st_size<=estimate.encoded_upper_bound
    width={'u8':1,'u16':2,'f32':4,'rgb8':1}[row['dtype']];shape=row['shape'];plane=shape[3]*shape[4]*shape[5]*width
    raw=bytes.fromhex(row['hex']);expected=[raw[((t*shape[1]+c)*shape[2]+z)*plane:((t*shape[1]+c)*shape[2]+z+1)*plane] for t in range(shape[0]) for z in range(shape[2]) for c in range(shape[1])]
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
    assert [int(getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+n+'_LZW_V1')) for n in ['U16','FLOAT32','U8','RGB8']]==list(range(12,16))

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
