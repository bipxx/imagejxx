"""Scientific UInt8 TIFF: independent carriers, bounded IO and ImageJ bytes."""
from pathlib import Path
import json,struct
import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
FIXTURES=Path(__file__).parents[1]/'fixtures/scientific-tiff-u8'
ROWS=json.loads((FIXTURES/'SOURCE.json').read_bytes())['files'];VALID=[r for r in ROWS if r['valid']]
IMAGEJ=Path(__file__).parents[1]/'fixtures/uint8-tiff-imagej154r'
IMAGEJ_ROWS=json.loads((IMAGEJ/'SOURCE.json').read_bytes())['files']
def write_request():
    request=io.ImageWriteRequest();request.format=io.ImageFileFormat.SCIENTIFIC_TIFF_U8_V1;return request

def wire(path):
    raw=path.read_bytes();assert raw[:4]==b'II*\0';offset=struct.unpack_from('<I',raw,4)[0];pages=[]
    while offset:
        n=struct.unpack_from('<H',raw,offset)[0];tags={}
        for i in range(n):
            tag,kind,count,value=struct.unpack_from('<HHII',raw,offset+2+12*i);tags[tag]=(kind,count,value)
        assert tags[258]==(3,1,8) and tags[339]==(3,1,1)
        def values(tag):
            kind,count,value=tags[tag];assert kind==4
            return [value] if count==1 else struct.unpack_from('<'+'I'*count,raw,value)
        offsets,counts=values(273),values(279)
        assert sum(counts)==tags[256][2]*tags[257][2]
        pages.append(b''.join(raw[a:a+b] for a,b in zip(offsets,counts)))
        offset=struct.unpack_from('<I',raw,offset+2+12*n)[0]
    return pages

@pytest.mark.parametrize('row',ROWS,ids=lambda r:r['file'])
def test_independent_byte_carriers(row):
    path=FIXTURES/row['file'];events=[]
    if not row['valid']:
        with pytest.raises(ValueError):io.inspect_image_file(path,progress=lambda *v:events.append(v))
        with pytest.raises(ValueError):io.read_image_series(path,progress=lambda *v:events.append(v))
        assert not events;return
    info=io.inspect_image_file(path);result=io.read_image_series(path)
    assert info.format==io.ImageFileFormat.SCIENTIFIC_TIFF_U8_V1
    assert info.series[0].dtype=='uint8' and info.series[0].sample_policy==ij.FloatingSamplePolicy.FINITE_ONLY
    assert info.series[0].shape==tuple(row['shape']) and result.dataset.shape==tuple(row['shape'])
    assert result.dataset.numpy('TCZYX').tobytes().hex()==row['hex']
    assert result.metadata['format_id']==io.uint8_format_id and result.metadata==info.series[0].metadata

@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_byte_export_wire_history_and_replace(tmp_path,row):
    result=io.read_image_series(FIXTURES/row['file']);request=write_request()
    reads=ij.DatasetReadObserver();data=result.dataset.observe_reads(reads)
    estimate=io.estimate_image_write(data,result.metadata,request=request);assert reads.counts().plane_reads==0
    path=tmp_path/'bytes.tif';io.write_image_series(path,data,result.metadata,request=request)
    assert path.stat().st_size==estimate.encoded_upper_bound
    shape=row['shape'];words=np.frombuffer(bytes.fromhex(row['hex']),np.uint8).reshape(shape[:-1])
    assert wire(path)==[words[t,c,z].tobytes() for t in range(shape[0]) for z in range(shape[2]) for c in range(shape[1])]
    assert io.read_image_series(path).dataset.numpy('TCZYX').tobytes()==words.tobytes()
    with pytest.raises(RuntimeError):io.write_image_series(path,data,request=request)
    request.replace=io.FileReplacePolicy.REPLACE_EXISTING;io.write_image_series(path,data,request=request)
    assert io.read_image_series(path).dataset.numpy('TCZYX').tobytes()==words.tobytes()
    with pytest.raises(ValueError):io.estimate_image_write(data)
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())

@pytest.mark.parametrize('policy',[ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32])
@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_byte_sources_refuse_float_sample_policies(row,policy):
    request=io.ImageReadRequest();request.sample_policy=policy;events=[]
    with pytest.raises(ValueError):io.read_image_series(FIXTURES/row['file'],request=request,progress=lambda *v:events.append(v))
    assert not events

@pytest.mark.parametrize('row',IMAGEJ_ROWS,ids=lambda r:r['file'])
def test_actual_imagej_byte_transport_and_current_plane_invert(tmp_path,row):
    assert row['source_hex']==row['reopened_hex']
    data=io.read_image_series(IMAGEJ/row['file']).dataset
    assert data.shape==tuple(row['shape']) and data.numpy('TCZYX').tobytes().hex()==row['source_hex']
    shape=data.shape;expected=bytes((113*t+71*c+43*z+17*y+7*x)&255 for t in range(shape[0]) for c in range(shape[1]) for z in range(shape[2]) for y in range(shape[3]) for x in range(shape[4]))
    assert data.numpy('TCZYX').tobytes()==expected
    owner=ij.Document(data);target=ij.resolve_target(owner.snapshot(),operation='invert')
    prepared=ij.prepare_operation('invert',target);owner.commit(prepared.begin_edit())
    assert owner.snapshot().dataset.numpy('TCZYX').tobytes().hex()==row['inverted_hex']
    owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==expected
    owner.redo();path=tmp_path/'inverted.tif';io.write_image_series(path,owner.snapshot().dataset,request=write_request())
    assert io.read_image_series(path).dataset.numpy('TCZYX').tobytes().hex()==row['inverted_hex']


def test_byte_mid_row_cancellation_and_metadata_limits(tmp_path):
    data=io.read_image_series(FIXTURES/'tall-le-r64.tif').dataset
    path=tmp_path/'existing.tif';path.write_bytes(b'previous file')
    request=write_request();request.replace=io.FileReplacePolicy.REPLACE_EXISTING
    cancel=ij.CancellationToken();events=[]
    def progress(stage,n,total):
        events.append((stage,n,total))
        if stage=='Scientific TIFF rows' and n==65:cancel.cancel()
    with pytest.raises(ij.OperationCancelled):io.write_image_series(path,data,request=request,cancellation=cancel,stage_progress=progress)
    assert any(n==65 for stage,n,total in events) and path.read_bytes()==b'previous file'
    assert list(tmp_path.iterdir())==[path]
    reads=ij.DatasetReadObserver();observed=data.observe_reads(reads);limits=io.ImageFileLimits();limits.max_output_file_bytes=1
    with pytest.raises(ValueError):io.estimate_image_write(observed,request=write_request(),limits=limits)
    assert reads.counts().plane_reads==0
