"""BigTIFF: independent carriers, wire decoding, bounded 64-bit addressing."""
from pathlib import Path
import hashlib
import json
import os
import struct
import zlib

import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
from test_lzw_tiff_io import decode_lzw
from test_predicted_tiles_io import expected_pages

FIXTURES = Path(__file__).parents[1] / 'fixtures'
SOURCES = [(FIXTURES / folder, row)
           for folder in ['scientific-bigtiff', 'bigtiff-libtiff471']
           for row in json.loads((FIXTURES / folder / 'SOURCE.json').read_bytes())['files']]
VALID = [(d, r) for d, r in SOURCES if r['valid']]
POLICIES = {'finite': ij.FloatingSamplePolicy.FINITE_ONLY,
            'quiet': ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,
            'ieee': ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32}


def read_request(row):
    request = io.ImageReadRequest()
    request.sample_policy = POLICIES[row['pattern']]
    return request


def write_request(row, big=True):
    request = io.ImageWriteRequest()
    request.format = io.ImageFileFormat(row['profile_ordinal'])
    request.container = io.TiffContainer.BIGTIFF if big else io.TiffContainer.CLASSIC
    return request


def unpackbits(raw):
    out = bytearray()
    at = 0
    while at < len(raw):
        code = raw[at];at += 1
        if code == 128:
            continue
        count = code+1 if code < 128 else 257-code
        if code < 128:
            assert at+count <= len(raw)
            out.extend(raw[at:at+count]);at += count
        else:
            assert at < len(raw)
            out.extend(raw[at:at+1]*count);at += 1
    return bytes(out)


def wire(path, row):
    """tifffile directories plus independent codecs; compare integer sample bits."""
    import tifffile
    raw = path.read_bytes()
    assert raw[:8] == b'II+\0\x08\0\0\0'
    size = 4 if row['dtype'] == 'f32' else 2 if row['dtype'] == 'u16' else 1
    components = row['shape'][-1]
    pages = []
    with tifffile.TiffFile(path) as f:
        assert f.is_bigtiff
        for page in f.pages:
            assert page.offset % 8 == 0
            assert int(page.compression) == row['compression']
            assert int(page.tags[317].value) == row['predictor'] if 317 in page.tags else row['predictor'] == 1
            assert page.is_tiled == (row['storage'] == 'tiled')
            for tag in page.tags.values():
                if tag.valuebytecount > 8:
                    assert tag.valueoffset % 8 == 0
            x, y = page.imagewidth, page.imagelength
            width = page.tilewidth if page.is_tiled else x
            height = page.tilelength if page.is_tiled else page.rowsperstrip
            across = (x+width-1)//width
            plane = bytearray(x*y*components*size)
            for i, (offset, count) in enumerate(zip(page.dataoffsets, page.databytecounts)):
                assert offset % 8 == 0 and offset+count <= len(raw)
                block = raw[offset:offset+count]
                block = zlib.decompress(block) if row['compression'] == 8 else decode_lzw(block) if row['compression'] == 5 else unpackbits(block) if row['compression'] == 32773 else block
                rows = height if page.is_tiled else min(height, y-i*height)
                assert len(block) == width*rows*components*size
                decoded = bytearray()
                for at in range(0,len(block),width*components*size):
                    line = bytearray(block[at:at+width*components*size])
                    if row['predictor'] == 3:
                        for j in range(1,len(line)):
                            line[j] = (line[j]+line[j-1]) & 255
                        words = [sum(line[b*width+k] << (24-8*b) for b in range(4)) for k in range(width)]
                    else:
                        words = [int.from_bytes(line[j:j+size], 'little') for j in range(0,len(line),size)]
                        if row['predictor'] == 2:
                            for j in range(components,len(words)):
                                words[j] = (words[j]+words[j-components]) & ((1 << (8*size))-1)
                    decoded.extend(b''.join(v.to_bytes(size,'little') for v in words))
                top, left = (i//across)*height, (i%across)*width
                stride = components*size
                for dy in range(rows):
                    line = decoded[dy*width*stride:(dy+1)*width*stride]
                    visible = min(width,x-left)*stride if top+dy < y else 0
                    if visible:
                        start = ((top+dy)*x+left)*stride
                        plane[start:start+visible] = line[:visible]
                    assert not any(line[visible:])
            pages.append(bytes(plane))
    assert pages == expected_pages(row)


@pytest.mark.parametrize('directory,row', SOURCES, ids=[d.name+'/'+r['file'] for d,r in SOURCES])
def test_bigtiff_independent_inputs(directory, row):
    path = directory/row['file']
    raw = path.read_bytes()
    assert len(raw) == row['bytes'] and hashlib.sha256(raw).hexdigest() == row['sha256']
    events = []
    controls = dict(progress=lambda *v:events.append(v), checkpoint=lambda:events.append('checkpoint'), stage_progress=lambda *v:events.append(v))
    if not row['valid']:
        with pytest.raises(ValueError):
            io.inspect_image_file(path, **controls)
        with pytest.raises(ValueError):
            io.read_image_series(path, request=read_request(row), **controls)
        assert not events
        return
    info = io.inspect_image_file(path, **controls)
    assert info.container == io.TiffContainer.BIGTIFF
    assert info.format == write_request(row).format
    assert info.series[0].shape == tuple(row['shape'])
    assert info.series[0].metadata['format_id'] == row['format_id']
    assert tuple(info.series[0].calibration.spacing) == tuple(row['spacing'])
    result = io.read_image_series(path, request=read_request(row), **controls)
    assert result.dataset.numpy('TCZYXS').tobytes().hex() == row['hex']
    assert result.metadata == info.series[0].metadata and events


@pytest.mark.parametrize('directory,row', VALID, ids=[d.name+'/'+r['file'] for d,r in VALID])
def test_bigtiff_export_wire_bounds_and_classic_default(directory, row, tmp_path):
    data = io.read_image_series(directory/row['file'], request=read_request(row))
    request = write_request(row)
    estimate = io.estimate_image_write(data.dataset, data.metadata, request=request)
    path = tmp_path/'big.tif'
    io.write_image_series(path, data.dataset, data.metadata, request=request)
    assert path.stat().st_size <= estimate.encoded_upper_bound
    info = io.inspect_image_file(path)
    assert info.container == io.TiffContainer.BIGTIFF and info.format == request.format
    assert io.read_image_series(path, request=read_request(row)).dataset.numpy('TCZYXS').tobytes().hex() == row['hex']
    wire(path, row)
    (tmp_path/'expected.json').write_text(json.dumps(row))
    path = tmp_path/'classic.tif'
    io.write_image_series(path, data.dataset, data.metadata, request=write_request(row,False))
    assert path.read_bytes()[:4] == b'II*\0'
    assert io.inspect_image_file(path).container == io.TiffContainer.CLASSIC
    assert io.ImageWriteRequest().container == io.TiffContainer.CLASSIC


@pytest.mark.parametrize('endian', ['little','big'])
def test_bigtiff_sparse_offsets_above_four_gib(endian, tmp_path):
    """IFD, external ASCII and pixel offsets all exceed UINT32_MAX."""
    source = FIXTURES/'scientific-bigtiff'/('p00-one-'+('le' if endian=='little' else 'be')+'-finite.tif')
    raw = bytearray(source.read_bytes())
    shift = (1 << 32)+4096
    get = lambda at,n:int.from_bytes(raw[at:at+n],endian)
    def put(at,n,v):raw[at:at+n] = v.to_bytes(n,endian)
    directory = get(8,8)
    put(8,8,directory+shift)
    for i in range(get(directory,8)):
        at = directory+8+20*i
        tag, kind, count = get(at,2),get(at+2,2),get(at+4,8)
        if tag == 273:
            assert count == 1
            value = get(at+12,2 if kind==3 else 4 if kind==4 else 8)
            put(at+2,2,16);put(at+12,8,value+shift)
        elif count*{2:1,3:2,4:4,5:8,16:8}[kind] > 8:
            put(at+12,8,get(at+12,8)+shift)
    path = tmp_path/'sparse.tif'
    try:
        with path.open('w+b') as f:
            if os.name == 'nt':
                import ctypes,msvcrt
                returned = ctypes.c_uint32()
                kernel = ctypes.WinDLL('kernel32',use_last_error=True)
                kernel.DeviceIoControl.argtypes = [ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.c_void_p]
                assert kernel.DeviceIoControl(msvcrt.get_osfhandle(f.fileno()),0x900c4,None,0,None,0,ctypes.byref(returned),None)
            f.write(raw[:16]);f.seek(shift+16);f.write(raw[16:])
        with pytest.raises(ValueError):io.inspect_image_file(path)
        limits = io.ImageFileLimits();limits.max_file_bytes = shift+len(raw)
        assert io.inspect_image_file(path,limits=limits).container == io.TiffContainer.BIGTIFF
        assert io.read_image_series(path,limits=limits).dataset.numpy('YX').tobytes() == bytes.fromhex('0000')
        limits.max_metadata_bytes = 128
        with pytest.raises(ValueError):io.inspect_image_file(path,limits=limits)
    finally:
        path.unlink(missing_ok=True)


ATOMIC = [(d,r) for d,r in VALID if d.name=='scientific-bigtiff' and '-edges-' in r['file'] and r['endian']=='big' and r['pattern']=='finite']


@pytest.mark.parametrize('directory,row',ATOMIC,ids=[r['file'] for _,r in ATOMIC])
def test_bigtiff_admission_and_cancel_preserve_destination(directory,row,tmp_path):
    data=io.read_image_series(directory/row['file'],request=read_request(row)).dataset
    observer=ij.DatasetReadObserver();observed=data.observe_reads(observer)
    request=write_request(row);request.replace=io.FileReplacePolicy.REPLACE_EXISTING
    estimate=io.estimate_image_write(observed,request=request)
    assert observer.counts().plane_reads==0
    path=tmp_path/'preserved.tif';path.write_bytes(b'preserved destination')
    limits=io.ImageFileLimits();limits.max_output_file_bytes=estimate.encoded_upper_bound-1
    with pytest.raises(ValueError):io.write_image_series(path,observed,request=request,limits=limits)
    assert observer.counts().plane_reads==0 and path.read_bytes()==b'preserved destination'
    token=ij.CancellationToken()
    def cancel(stage,done,total):
        if done:token.cancel()
    with pytest.raises(ij.OperationCancelled):io.write_image_series(path,data,request=request,cancellation=token,stage_progress=cancel)
    assert path.read_bytes()==b'preserved destination' and [p.name for p in tmp_path.iterdir()]==[path.name]
