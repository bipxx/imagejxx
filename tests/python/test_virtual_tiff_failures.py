"""Corrupt unrequested blocks stay lazy; failed reads never publish output."""
import struct
import numpy as np
import pytest
from imagejxx import scientific_io as io
from test_virtual_tiff import write

def test_unrequested_corrupt_tile_is_not_decoded(tmp_path):
    values=np.arange(130*129,dtype=np.uint16).reshape(130,129);path=tmp_path/'tiles.tif'
    write(path,values,io.ImageFileFormat.SCIENTIFIC_TIFF_U16_DEFLATE_TILED_V1)
    raw=bytearray(path.read_bytes());assert raw[:4]==b'II*\0'
    offset=struct.unpack_from('<I',raw,4)[0];count=struct.unpack_from('<H',raw,offset)[0];tags={}
    for i in range(count):
        tag,kind,n,at=struct.unpack_from('<HHII',raw,offset+2+12*i);tags[tag]=(kind,n,at)
    def table(tag):
        kind,n,at=tags[tag];assert kind==4 and n==9
        return struct.unpack_from('<'+'I'*n,raw,at)
    offsets,sizes=table(324),table(325)
    raw[offsets[-1]:offsets[-1]+sizes[-1]]=bytes(sizes[-1]);path.write_bytes(raw)
    data=io.open_virtual_image_series(path).dataset
    prior=data.region(0,0,2,2);np.testing.assert_array_equal(prior,values[:2,:2])
    with pytest.raises((ValueError,RuntimeError)):data.region(128,128,1,1)
    with pytest.raises((ValueError,RuntimeError)):data.plane()
    np.testing.assert_array_equal(prior,values[:2,:2])

def test_open_revalidates_identity_after_callbacks(tmp_path):
    path=tmp_path/'input.tif';write(path,np.arange(20,dtype=np.uint16).reshape(4,5));original=path.read_bytes();seen=[]
    def mutate(done,total):
        seen.append((done,total));path.write_bytes(original+b'changed')
    with pytest.raises(RuntimeError):io.open_virtual_image_series(path,progress=mutate)
    assert seen==[(1,1)]

def test_changed_file_during_provider_callback_abandons_staged_output(tmp_path):
    path=tmp_path/'input.tif';output=tmp_path/'output.tif'
    values=np.arange(128*128,dtype=np.uint16).reshape(128,128)
    write(path,values,io.ImageFileFormat.SCIENTIFIC_TIFF_U16_LZW_TILED_V1)
    data=io.open_virtual_image_series(path).dataset;prior=data.region(0,0,2,2);original=path.read_bytes();seen=[]
    def mutate(name,done,total):
        if name=='Virtual TIFF blocks' and not seen:seen.append(done);path.write_bytes(original+b'changed')
    with pytest.raises(RuntimeError):io.write_image_series(output,data,stage_progress=mutate)
    assert seen and not output.exists()
    np.testing.assert_array_equal(prior,values[:2,:2])
