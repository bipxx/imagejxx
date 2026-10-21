"""Native Float32 TIFF transport: independent wire fixtures and exact IEEE words."""
from pathlib import Path
import json,struct
import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
FIXTURES=Path(__file__).parents[1]/'fixtures/scientific-tiff-float32'
CASES=json.loads((FIXTURES/'SOURCE.json').read_bytes())['files']
VALID=[r for r in CASES if r['valid']]
POLICIES=[ij.FloatingSamplePolicy.FINITE_ONLY,ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32]

def expected(row):return np.frombuffer(bytes.fromhex(row['words_hex']),'<u4').reshape(2,2,3,5,7)
def read_request(policy):
    request=io.ImageReadRequest();request.sample_policy=policy;return request
def write_request():
    request=io.ImageWriteRequest();request.format=io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_V1;return request

@pytest.mark.parametrize('row',CASES,ids=lambda r:r['file'])
def test_independent_layout_inspection(row):
    events=[];path=FIXTURES/row['file']
    if not row['valid']:
        with pytest.raises(ValueError):io.inspect_image_file(path,progress=lambda *v:events.append(v))
        with pytest.raises(ValueError):io.read_image_series(path,progress=lambda *v:events.append(v))
        assert not events
        return
    info=io.inspect_image_file(path)
    assert info.format==io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_V1
    assert info.series[0].dtype=='float32' and info.series[0].shape==(2,2,3,5,7,1)
    assert info.series[0].sample_policy==POLICIES[2]
    assert info.series[0].metadata['format_id']==io.float32_format_id

@pytest.mark.parametrize('policy',POLICIES)
@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_source_policy_and_exact_words(row,policy):
    allowed=row['group']=='finite' or (row['group']=='quiet' and policy!=POLICIES[0]) or policy==POLICIES[2]
    if not allowed:
        with pytest.raises(ValueError):io.read_image_series(FIXTURES/row['file'],request=read_request(policy))
        return
    result=io.read_image_series(FIXTURES/row['file'],request=read_request(policy))
    assert result.dataset.sample_policy==policy
    assert result.dataset.numpy('TCZYX').tobytes()==expected(row).tobytes()
    assert tuple(result.dataset.calibration.spacing)==(.5,1.25,2.75)
    assert result.dataset.calibration.frame_interval==.125


def wire_words(path):
    raw=path.read_bytes();assert raw[:4]==b'II*\0'
    offset=struct.unpack_from('<I',raw,4)[0];pages=[]
    while offset:
        n=struct.unpack_from('<H',raw,offset)[0];tags={}
        for i in range(n):
            tag,kind,count,value=struct.unpack_from('<HHII',raw,offset+2+12*i);tags[tag]=(kind,count,value)
        assert tags[258]==(3,1,32) and tags[339]==(3,1,3)
        def values(tag):
            kind,count,value=tags[tag];assert kind==4
            return [value] if count==1 else struct.unpack_from('<'+'I'*count,raw,value)
        pixels=b''.join(raw[a:a+b] for a,b in zip(values(273),values(279)))
        pages.append(pixels);offset=struct.unpack_from('<I',raw,offset+2+12*n)[0]
    return pages

@pytest.mark.parametrize('row',VALID,ids=lambda r:r['file'])
def test_write_wire_roundtrip_atomic_replace_and_processing(tmp_path,row):
    result=io.read_image_series(FIXTURES/row['file'],request=read_request(POLICIES[2]))
    observer=ij.DatasetReadObserver();data=result.dataset.observe_reads(observer);request=write_request()
    estimate=io.estimate_image_write(data,result.metadata,request=request)
    assert observer.counts().plane_reads==0
    path=tmp_path/'result.tif';io.write_image_series(path,data,result.metadata,request=request)
    assert observer.counts().plane_reads==12 and path.stat().st_size==estimate.encoded_upper_bound
    words=expected(row)
    assert wire_words(path)==[words[t,c,z].tobytes() for t in range(2) for z in range(3) for c in range(2)]
    actual=io.read_image_series(path,request=read_request(POLICIES[2])).dataset
    assert actual.numpy('TCZYX').tobytes()==words.tobytes()
    with pytest.raises(RuntimeError):io.write_image_series(path,data,request=request)
    request.replace=io.FileReplacePolicy.REPLACE_EXISTING
    io.write_image_series(path,data,request=request)
    assert actual.numpy('TCZYX').tobytes()==io.read_image_series(path,request=read_request(POLICIES[2])).dataset.numpy('TCZYX').tobytes()
    owner=ij.Document(actual);command=ij.resolve_remove_nans_command(owner.snapshot())
    prepared=ij.prepare_operation(command.operation,command.target,command.parameters());owner.commit(prepared.begin_edit())
    io.write_image_series(path,owner.snapshot().dataset,request=request)
    assert io.read_image_series(path,request=read_request(POLICIES[2])).dataset.numpy('TCZYX').tobytes()==owner.snapshot().dataset.numpy('TCZYX').tobytes()
    if owner.undo_steps:
        owner.undo();assert owner.snapshot().dataset.numpy('TCZYX').tobytes()==words.tobytes()
    assert not any('.tmp.' in p.name for p in tmp_path.iterdir())

@pytest.mark.parametrize('field',['max_total_samples','max_plane_samples','max_codec_bytes','max_working_bytes','max_file_bytes','max_ifds'])
def test_resource_limits_before_publication(tmp_path,field):
    limits=io.ImageFileLimits();setattr(limits,field,1)
    with pytest.raises((ValueError,RuntimeError)):
        io.read_image_series(FIXTURES/VALID[0]['file'],limits=limits,request=read_request(POLICIES[2]))
    assert not list(tmp_path.iterdir())


def test_cancellation_and_read_free_write_planning(tmp_path):
    data=io.read_image_series(FIXTURES/'ieee-le-r2.tif',request=read_request(POLICIES[2])).dataset
    reads=ij.DatasetReadObserver();observed=data.observe_reads(reads)
    with pytest.raises(ValueError):io.estimate_image_write(observed)
    limits=io.ImageFileLimits();limits.max_output_file_bytes=1
    with pytest.raises(ValueError):io.estimate_image_write(observed,request=write_request(),limits=limits)
    assert reads.counts().plane_reads==0
    cancel=ij.CancellationToken();cancel.cancel()
    with pytest.raises(ij.OperationCancelled):io.write_image_series(tmp_path/'cancel.tif',observed,request=write_request(),cancellation=cancel)
    assert not list(tmp_path.iterdir()) and reads.counts().plane_reads==0
