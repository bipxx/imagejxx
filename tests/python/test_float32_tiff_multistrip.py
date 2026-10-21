"""Float32 strip boundaries and cancellation after rows reach staging."""
import struct
import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
IEEE=ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32

def data():
    words=np.arange(2*2*2*130*17,dtype=np.uint32)*np.uint32(2654435761)
    words[:9]=[0,0x80000000,1,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0xff812345]
    words=words.reshape(2,2,2,130,17)
    return words,ij.Dataset(words.view(np.float32),axes='TCZYX',sample_policy=IEEE)

def request():
    r=io.ImageWriteRequest();r.format=io.ImageFileFormat.SCIENTIFIC_TIFF_FLOAT32_V1;return r


def test_exact_float32_multi_strip_word_corpus(tmp_path):
    words,source=data();path=tmp_path/'multi.tif';io.write_image_series(path,source,request=request())
    raw=path.read_bytes();offset=struct.unpack_from('<I',raw,4)[0];pages=[]
    while offset:
        n=struct.unpack_from('<H',raw,offset)[0]
        tags={}
        for i in range(n):
            tag,kind,count,value=struct.unpack_from('<HHII',raw,offset+2+12*i);tags[tag]=(kind,count,value)
        assert tags[258]==(3,1,32) and tags[339]==(3,1,3) and tags[278]==(4,1,64)
        assert tags[273][1]==tags[279][1]==3
        offsets=struct.unpack_from('<3I',raw,tags[273][2]);counts=struct.unpack_from('<3I',raw,tags[279][2])
        assert counts==(64*17*4,64*17*4,2*17*4)
        pages.append(b''.join(raw[a:a+b] for a,b in zip(offsets,counts)))
        offset=struct.unpack_from('<I',raw,offset+2+12*n)[0]
    assert pages==[words[t,c,z].tobytes() for t in range(2) for z in range(2) for c in range(2)]
    read=io.ImageReadRequest();read.sample_policy=IEEE
    assert io.read_image_series(path,request=read).dataset.numpy('TCZYX').tobytes()==words.tobytes()
    assert source.numpy('TCZYX').tobytes()==words.tobytes()


def test_float32_mid_row_cancellation_preserves_existing_target(tmp_path):
    words,source=data();path=tmp_path/'existing.tif';path.write_bytes(b'previous destination')
    write=request();write.replace=io.FileReplacePolicy.REPLACE_EXISTING
    token=ij.CancellationToken();events=[]
    def progress(stage,completed,total):
        events.append((stage,completed,total))
        if stage=='Scientific TIFF rows' and completed==65:token.cancel()
    with pytest.raises(ij.OperationCancelled):
        io.write_image_series(path,source,request=write,cancellation=token,stage_progress=progress)
    assert any(n==65 for stage,n,total in events)
    assert path.read_bytes()==b'previous destination'
    assert list(tmp_path.iterdir())==[path]
    assert source.numpy('TCZYX').tobytes()==words.tobytes()
