"""Tiled IO against independent carriers, raw wire decoding and resource faults."""
from pathlib import Path
import hashlib,json,struct
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io

FIXTURES=Path(__file__).parents[1]/'fixtures'
SOURCES=[(FIXTURES/folder,row) for folder in ['scientific-tiff-tiled','tiled-tiff-libtiff471']
         for row in json.loads((FIXTURES/folder/'SOURCE.json').read_bytes())['files']]
VALID=[(directory,row) for directory,row in SOURCES if row['valid']]


def request():
    result=io.ImageWriteRequest();result.format=io.ImageFileFormat.SCIENTIFIC_TIFF_U16_TILED_V1
    return result


def wire(path):
    """Decode original output bytes without native or third-party TIFF code."""
    raw=path.read_bytes();assert raw[:4]==b'II*\0'
    offset=struct.unpack_from('<I',raw,4)[0];pages=[]
    while offset:
        n=struct.unpack_from('<H',raw,offset)[0];tags={}
        for i in range(n):
            tag,kind,count,value=struct.unpack_from('<HHII',raw,offset+2+12*i)
            assert tag not in tags;tags[tag]=(kind,count,value)
        assert not ({273,278,279}&tags.keys())
        assert tags[259]==(3,1,1) and tags[258]==(3,1,16)
        x,y,tw,th=[tags[tag][2] for tag in [256,257,322,323]]
        assert tw==th==64
        def table(tag):
            kind,count,value=tags[tag];assert kind==4
            return [value] if count==1 else struct.unpack_from('<'+'I'*count,raw,value)
        offsets,counts=table(324),table(325);expected=((x+tw-1)//tw)*((y+th-1)//th)
        assert len(offsets)==len(counts)==expected
        plane=np.empty((y,x),dtype='<u2')
        for i,(at,size) in enumerate(zip(offsets,counts)):
            assert size==tw*th*2 and at+size<=len(raw)
            tile=np.frombuffer(raw[at:at+size],dtype='<u2').reshape(th,tw)
            top,left=(i//((x+tw-1)//tw))*th,(i%((x+tw-1)//tw))*tw
            height,width=min(th,y-top),min(tw,x-left)
            plane[top:top+height,left:left+width]=tile[:height,:width]
            assert not tile[height:,:].any() and not tile[:height,width:].any()
        pages.append(plane.tobytes());offset=struct.unpack_from('<I',raw,offset+2+12*n)[0]
    return pages


@pytest.mark.parametrize('directory,row',SOURCES,ids=[d.name+'/'+r['file'] for d,r in SOURCES])
def test_independent_tiled_admission(directory,row):
    path=directory/row['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
    if not row['valid']:
        events=[]
        for action in [io.inspect_image_file,io.read_image_series]:
            with pytest.raises((ValueError,RuntimeError)):
                action(path,progress=lambda *a:events.append(a),checkpoint=lambda:events.append('checkpoint'))
        assert not events
        return
    info=io.inspect_image_file(path);assert info.format==request().format
    assert info.series[0].shape==tuple(row['shape'])
    result=io.read_image_series(path);data=result.dataset
    assert data.numpy('TCZYXS').tobytes().hex()==row['hex']
    assert tuple(data.calibration.spacing)==tuple(row['spacing'])
    assert data.calibration.frame_interval==.125 and data.calibration.unit=='um'
    assert result.metadata['format_id']==io.u16_tiled_format_id


@pytest.mark.parametrize('directory,row',VALID,ids=[d.name+'/'+r['file'] for d,r in VALID])
def test_tiled_export_wire_and_read_free_estimate(directory,row,tmp_path):
    result=io.read_image_series(directory/row['file']);observer=ij.DatasetReadObserver();data=result.dataset.observe_reads(observer)
    estimate=io.estimate_image_write(data,result.metadata,request=request());assert observer.counts().plane_reads==0
    path=tmp_path/'tiled.tif';events=[]
    io.write_image_series(path,data,result.metadata,request=request(),stage_progress=lambda *args:events.append(args))
    assert path.stat().st_size==estimate.encoded_upper_bound
    shape=row['shape'];plane=shape[3]*shape[4]*2;raw=bytes.fromhex(row['hex'])
    expected=[raw[((t*shape[1]+c)*shape[2]+z)*plane:((t*shape[1]+c)*shape[2]+z+1)*plane]
              for t in range(shape[0]) for z in range(shape[2]) for c in range(shape[1])]
    assert wire(path)==expected
    assert observer.counts().plane_reads==shape[0]*shape[1]*shape[2]
    assert io.read_image_series(path).dataset.numpy('TCZYXS').tobytes()==raw
    import tifffile
    with tifffile.TiffFile(path) as tif:
        assert [p.asarray().astype('<u2').tobytes() for p in tif.pages]==expected
    assert events[0][1]==0 and events[-1][1]==events[-1][2]
    assert all(a[0]=='Scientific TIFF tile rows' for a in events)
    assert [a[1] for a in events]==sorted(a[1] for a in events)
    before=path.read_bytes()
    with pytest.raises(RuntimeError):io.write_image_series(path,data,request=request())
    assert path.read_bytes()==before
    replacement=request();replacement.replace=io.FileReplacePolicy.REPLACE_EXISTING
    io.write_image_series(path,data,result.metadata,request=replacement)
    assert path.read_bytes()==before and list(tmp_path.iterdir())==[path]


@pytest.mark.parametrize('field',['max_file_bytes','max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes','max_metadata_bytes'])
def test_tiled_read_limits_before_hooks(field):
    limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):
        io.read_image_series(FIXTURES/'scientific-tiff-tiled/edges-le-poison.tif',limits=limits,progress=lambda *a:events.append(a),checkpoint=lambda:events.append(1))
    assert not events


@pytest.mark.parametrize('field',['max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes','max_metadata_bytes','max_output_file_bytes'])
def test_tiled_write_limits_before_reads(field,tmp_path):
    data=ij.Dataset(np.arange(2*65*67,dtype='<u2').reshape(2,65,67),axes='ZYX');observer=ij.DatasetReadObserver();data=data.observe_reads(observer)
    limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):
        io.write_image_series(tmp_path/'not-published.tif',data,request=request(),limits=limits,checkpoint=lambda:events.append(1))
    assert not events and observer.counts().plane_reads==0 and not list(tmp_path.iterdir())


@pytest.mark.parametrize('action',['read','write'])
@pytest.mark.parametrize('fault',['cancel','callback'])
def test_tiled_mid_row_faults_preserve_publication(action,fault,tmp_path):
    token=ij.CancellationToken();events=[];path=tmp_path/'preserved.tif';path.write_bytes(b'preserved')
    def stage(name,n,total):
        assert name=='Scientific TIFF tile rows';events.append(n)
        if n==3:
            if fault=='cancel':token.cancel()
            else:raise RuntimeError('injected tile row fault')
    with pytest.raises((ij.OperationCancelled,RuntimeError)):
        if action=='read':io.read_image_series(FIXTURES/'scientific-tiff-tiled/edges-be-poison.tif',cancellation=token,stage_progress=stage)
        else:
            data=ij.Dataset(np.arange(65*67,dtype='<u2').reshape(65,67),axes='YX');write=request();write.replace=io.FileReplacePolicy.REPLACE_EXISTING
            io.write_image_series(path,data,request=write,cancellation=token,stage_progress=stage)
    assert 3 in events and path.read_bytes()==b'preserved' and list(tmp_path.iterdir())==[path]


def test_tiled_append_only_identity_and_default():
    assert int(request().format)==24
    assert io.ImageWriteRequest().format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_V1
    assert io.u16_tiled_format_id=='F.G080.tiff-u16-uncompressed-tiled-ifd-czt-v1'


@pytest.mark.parametrize('dtype,components',[('u1',1),('<f4',1),('u1',3),('<u2',3)])
def test_tiled_descriptor_mismatch_before_provider_reads(dtype,components,tmp_path):
    shape=(3,5,components) if components>1 else (3,5)
    data=ij.Dataset(np.zeros(shape,dtype=dtype),axes='YXS' if components>1 else 'YX');observer=ij.DatasetReadObserver();data=data.observe_reads(observer)
    with pytest.raises(ValueError):io.write_image_series(tmp_path/'rejected.tif',data,request=request())
    assert observer.counts().plane_reads==0 and not list(tmp_path.iterdir())
