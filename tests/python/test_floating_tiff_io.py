"""Predictor 3: independent byte-plane streams, libtiff carriers and exact IEEE words."""
from pathlib import Path
import hashlib,json,struct,zlib
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io
from test_lzw_tiff_io import decode_lzw
ROOT=Path(__file__).parents[1]/'fixtures'
ROWS=json.loads((ROOT/'scientific-tiff-floating/SOURCE.json').read_bytes())['files']
ORACLE=json.loads((ROOT/'floating-tiff-libtiff471/SOURCE.json').read_bytes())['files']
VALID=[r for r in ROWS if r['valid']]
def read_request(row):
    r=io.ImageReadRequest();r.sample_policy={'finite':ij.FloatingSamplePolicy.FINITE_ONLY,'quiet':ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,'ieee':ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32}[row['pattern']];return r
def write_request(row,predicted=True):
    r=io.ImageWriteRequest();codec='LZW' if row['codec']==5 else 'DEFLATE';r.format=getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_FLOAT32_'+codec+('_FLOATING_V1' if predicted else '_V1'));return r
@pytest.mark.parametrize('directory,row', [('scientific-tiff-floating',r) for r in ROWS]+[('floating-tiff-libtiff471',r) for r in ORACLE],ids=[r['file'] for r in ROWS]+['libtiff-'+r['file'] for r in ORACLE])
def test_independent_and_libtiff_carriers(directory,row):
    path=ROOT/directory/row['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
    if not row.get('valid',True):
        with pytest.raises((ValueError,RuntimeError)):io.inspect_image_file(path)
        with pytest.raises((ValueError,RuntimeError)):io.read_image_series(path,request=read_request(row))
        return
    info=io.inspect_image_file(path);assert info.format==write_request(row).format and info.series[0].shape==tuple(row['shape'])
    actual=io.read_image_series(path,request=read_request(row)).dataset
    assert actual.numpy('TCZYXS').tobytes().hex()==row['hex']
    # TIFFSetField stores libtiff's resolution argument as binary32 before
    # choosing its rational; our struct carriers use the exact rational 4/5.
    spacing_y=1/struct.unpack('<f',struct.pack('<f',.8))[0] if directory=='floating-tiff-libtiff471' else 1.25
    assert tuple(actual.calibration.spacing)==(.5,spacing_y,2.75) and actual.calibration.frame_interval==.125
    if row['pattern']!='finite':
        with pytest.raises(ValueError):io.read_image_series(path)
    if row['pattern']=='ieee':
        request=io.ImageReadRequest();request.sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
        with pytest.raises(ValueError):io.read_image_series(path,request=request)
def decoded_wire(path,row):
    raw=path.read_bytes();assert raw[:2]==b'II';offset=struct.unpack_from('<I',raw,4)[0];pages=[];Y,X=row['shape'][3:5]
    while offset:
        count=struct.unpack_from('<H',raw,offset)[0];tags={}
        for i in range(count):
            tag,kind,n,value=struct.unpack_from('<HHII',raw,offset+2+12*i);tags[tag]=(kind,n,value)
        def values(tag):
            kind,n,value=tags[tag];assert kind==4
            return [value] if n==1 else struct.unpack_from('<'+'I'*n,raw,value)
        assert tags[317]==(3,1,3) and tags[259][2]==(5 if row['codec']==5 else 8)
        strips=[(decode_lzw if row['codec']==5 else zlib.decompress)(raw[at:at+n]) for at,n in zip(values(273),values(279))]
        differences=np.frombuffer(b''.join(strips),'u1').reshape(Y,4*X)
        planes=(np.cumsum(differences,axis=1,dtype=np.uint64)&255).astype('u1').reshape(Y,4,X)
        pages.append(planes.transpose(0,2,1)[:,:,::-1].tobytes());offset=struct.unpack_from('<I',raw,offset+2+12*count)[0]
    return pages
@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_output_wire_bound_and_plain_provenance(row,tmp_path):
    result=io.read_image_series(ROOT/'scientific-tiff-floating'/row['file'],request=read_request(row));observer=ij.DatasetReadObserver();data=result.dataset.observe_reads(observer)
    write=write_request(row);estimate=io.estimate_image_write(data,result.metadata,request=write);assert observer.counts().plane_reads==0
    path=tmp_path/'floating.tif';io.write_image_series(path,data,result.metadata,request=write)
    T,C,Z,Y,X,S=row['shape'];plane=Y*X*4;raw=bytes.fromhex(row['hex'])
    expected=[raw[((t*C+c)*Z+z)*plane:((t*C+c)*Z+z+1)*plane] for t in range(T) for z in range(Z) for c in range(C)]
    assert decoded_wire(path,row)==expected and path.stat().st_size<=estimate.encoded_upper_bound
    assert observer.counts().plane_reads==T*C*Z and io.read_image_series(path,request=read_request(row)).dataset.numpy('TCZYXS').tobytes()==raw
    before=path.read_bytes()
    with pytest.raises(RuntimeError):io.write_image_series(path,data,request=write)
    assert path.read_bytes()==before;write.replace=io.FileReplacePolicy.REPLACE_EXISTING;io.write_image_series(path,data,result.metadata,request=write)
    plain=tmp_path/'plain.tif';io.write_image_series(plain,data,result.metadata,request=write_request(row,False))
    assert io.read_image_series(plain,request=read_request(row)).dataset.numpy('TCZYXS').tobytes()==raw
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())
@pytest.mark.parametrize('field',['max_file_bytes','max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes'])
def test_resource_limits_before_callbacks(field):
    limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):io.read_image_series(ROOT/'scientific-tiff-floating'/VALID[0]['file'],limits=limits,checkpoint=lambda:events.append(1))
    assert not events
@pytest.mark.parametrize('codec',[8,5])
def test_prediction_scratch_charged_and_smooth_image_compresses(codec,tmp_path):
    row=dict(codec=codec);a=np.broadcast_to(np.arange(1024,dtype='<f4'),(128,1024)).copy();data=ij.Dataset(a,axes='YX')
    predicted=io.estimate_image_write(data,request=write_request(row));plain=io.estimate_image_write(data,request=write_request(row,False))
    assert predicted.working_bytes>=plain.working_bytes+a.shape[1]*4
    for enabled,name in [(True,'floating'),(False,'plain')]:io.write_image_series(tmp_path/(name+'.tif'),data,request=write_request(row,enabled))
    assert (tmp_path/'floating.tif').stat().st_size<(tmp_path/'plain.tif').stat().st_size
@pytest.mark.parametrize('mode',['cancel','callback'])
def test_partial_prediction_export_aborts_atomically(mode,tmp_path):
    row=next(r for r in VALID if r['file']=='tall-ieee-le-c5.tif');data=io.read_image_series(ROOT/'scientific-tiff-floating'/row['file'],request=read_request(row)).dataset
    path=tmp_path/'existing.tif';path.write_bytes(b'preserved');write=write_request(row);write.replace=io.FileReplacePolicy.REPLACE_EXISTING;token=ij.CancellationToken();events=[]
    def stage(name,n,total):
        if name=='Scientific TIFF rows' and n==65:
            events.append(n)
            if mode=='cancel':token.cancel()
            else:raise RuntimeError('injected callback fault')
    with pytest.raises((ij.OperationCancelled,RuntimeError)):io.write_image_series(path,data,request=write,cancellation=token,stage_progress=stage)
    assert events==[65] and path.read_bytes()==b'preserved' and list(tmp_path.iterdir())==[path]
def test_append_only_formats_and_default():
    assert [int(getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_FLOAT32_'+codec+'_FLOATING_V1')) for codec in ['DEFLATE','LZW']]==[22,23]
    assert io.ImageWriteRequest().format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_V1
