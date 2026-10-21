"""PackBits/LZW tiled layouts: independent words, padding, budgets and publication."""
from pathlib import Path
import hashlib
import json
import struct
import zlib
from test_lzw_tiff_io import decode_lzw

import numpy as np
import pytest
import imagejxx as ij
from imagejxx import scientific_io as io

ROOT = Path(__file__).parents[1] / 'fixtures'
SOURCES = [(ROOT / folder, row)
           for folder in ['scientific-tiff-predicted-tiles', 'predicted-tiles-libtiff471']
           for row in json.loads((ROOT / folder / 'SOURCE.json').read_bytes())['files']]
VALID = [(directory, row) for directory, row in SOURCES if row['valid']]
POLICIES = {'finite': ij.FloatingSamplePolicy.FINITE_ONLY,
            'quiet': ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32,
            'ieee': ij.FloatingSamplePolicy.PRESERVE_IEEE_FLOAT32}


def decode_packbits(encoded,width):
    out=bytearray();at=0
    while at<len(encoded):
        code=encoded[at];at+=1
        if code==128:continue
        n=code+1 if code<128 else 257-code
        assert n<=width-len(out)%width
        if code<128:
            assert at+n<=len(encoded);out.extend(encoded[at:at+n]);at+=n
        else:
            assert at<len(encoded);out.extend(encoded[at:at+1]*n);at+=1
    return bytes(out)


def read_request(row):
    request = io.ImageReadRequest()
    request.sample_policy = POLICIES[row['pattern']]
    return request


def write_request(row):
    request=io.ImageWriteRequest()
    name={'u16':'U16','u8':'U8','rgb8':'RGB8','f32':'FLOAT32'}[row['dtype']]
    request.format=getattr(io.ImageFileFormat,'SCIENTIFIC_TIFF_'+name+('_DEFLATE' if row['compression'] in [8,32946] else '_LZW')+('_FLOATING' if row['dtype']=='f32' else '_HORIZONTAL')+'_TILED_V1')
    return request


def undo_prediction(tile, width, size, components):
    decoded=bytearray()
    for at in range(0,len(tile),width*components*size):
        row=tile[at:at+width*components*size]
        if size==4:
            shuffled=bytearray();value=0
            for v in row:value=(value+v)&255;shuffled.append(value)
            words=[sum(shuffled[b*width+x]<<(24-8*b) for b in range(4)) for x in range(width)]
        else:
            words=[];mask=(1<<(8*size))-1
            for x in range(width*components):words.append((int.from_bytes(row[x*size:(x+1)*size],'little')+(words[x-components] if x>=components else 0))&mask)
        decoded.extend(b''.join(v.to_bytes(size,'little') for v in words))
    return bytes(decoded)


def expected_pages(row):
    t, c, z, y, x, s = row['shape']
    size = (4 if row['dtype'] == 'f32' else 2 if row['dtype'] == 'u16' else 1) * y * x * s
    raw = bytes.fromhex(row['hex'])
    return [raw[((tt*c+cc)*z+zz)*size:((tt*c+cc)*z+zz+1)*size]
            for tt in range(t) for zz in range(z) for cc in range(c)]


def wire(path, row):
    """Decode original uncompressed bytes without floating-point conversion."""
    raw = path.read_bytes()
    assert raw[:4] == b'II*\0'
    offset = struct.unpack_from('<I', raw, 4)[0]
    pages = []
    sample_bytes = 4 if row['dtype'] == 'f32' else 2 if row['dtype'] == 'u16' else 1
    components = row['shape'][-1]
    while offset:
        n = struct.unpack_from('<H', raw, offset)[0]
        tags = {}
        for i in range(n):
            tag, kind, count, value = struct.unpack_from('<HHII', raw, offset+2+12*i)
            assert tag not in tags
            tags[tag] = kind, count, value
        assert not ({273, 278, 279} & tags.keys())
        assert tags[259] == (3, 1, 8 if row['compression'] in [8,32946] else 5)
        assert tags[317] == (3,1,3 if row['dtype']=='f32' else 2)
        assert tags[277] == (3, 1, components)
        assert tags[262] == (3, 1, 2 if components == 3 else 1)

        def values(tag):
            kind, count, value = tags[tag]
            size = 2 if kind == 3 else 4
            payload = struct.pack('<I', value)[:count*size] if count*size <= 4 else raw[value:value+count*size]
            return struct.unpack('<' + ('H' if size == 2 else 'I') * count, payload)

        assert values(258) == (sample_bytes*8,) * components
        assert values(339) == (3 if sample_bytes == 4 else 1,) * components
        x, y, tw, th = [tags[tag][2] for tag in [256, 257, 322, 323]]
        assert tw == th == 64
        offsets, counts = values(324), values(325)
        assert len(offsets) == len(counts) == ((x+tw-1)//tw)*((y+th-1)//th)
        stride = components * sample_bytes
        plane = bytearray(x*y*stride)
        for i, (at, size) in enumerate(zip(offsets, counts)):
            assert size > 0 and at+size <= len(raw)
            tile=zlib.decompress(raw[at:at+size]) if row['compression'] in [8,32946] else decode_lzw(raw[at:at+size])
            tile=undo_prediction(tile,tw,sample_bytes,components)
            assert len(tile)==tw*th*stride
            top, left = i//((x+tw-1)//tw)*th, i%((x+tw-1)//tw)*tw
            width = min(tw, x-left)
            for dy in range(th):
                tile_row = tile[dy*tw*stride:(dy+1)*tw*stride]
                if top+dy < y:
                    start = ((top+dy)*x+left)*stride
                    plane[start:start+width*stride] = tile_row[:width*stride]
                    assert not any(tile_row[width*stride:])
                else:
                    assert not any(tile_row)
        pages.append(bytes(plane))
        offset = struct.unpack_from('<I', raw, offset+2+12*n)[0]
    assert pages == expected_pages(row)


@pytest.mark.parametrize('directory,row', SOURCES, ids=[d.name+'/'+r['file'] for d, r in SOURCES])
def test_independent_tiled_layouts(directory, row):
    path = directory / row['file']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
    if not row['valid']:
        events=[]
        if row['refusal_phase']=='inspect':
            for operation in [io.inspect_image_file,io.read_image_series]:
                with pytest.raises((ValueError,RuntimeError)):
                    operation(path,progress=lambda *a:events.append(a),checkpoint=lambda:events.append(1))
            assert not events
        else:
            assert io.inspect_image_file(path).format==write_request(row).format
            with pytest.raises((ValueError,RuntimeError)):
                io.read_image_series(path,request=read_request(row))
        return
    info = io.inspect_image_file(path)
    assert info.format == write_request(row).format
    assert info.series[0].shape == tuple(row['shape'])
    result = io.read_image_series(path, request=read_request(row))
    assert result.dataset.numpy('TCZYXS').tobytes().hex() == row['hex']
    assert tuple(result.dataset.calibration.spacing) == tuple(row['spacing'])
    assert result.dataset.sample_policy == POLICIES[row['pattern']]
    key = {'f32': 'float32'}.get(row['dtype'], row['dtype'])
    assert result.metadata['format_id'] == getattr(io, key+('_deflate' if row['compression'] in [8,32946] else '_lzw')+('_floating' if row['dtype']=='f32' else '_horizontal')+'_tiled_format_id')
    if row['pattern'] != 'finite' and row['shape'][3]*row['shape'][4] > 1:
        with pytest.raises(ValueError):
            io.read_image_series(path)
    if row['pattern'] == 'ieee' and row['shape'][3]*row['shape'][4] > 1:
        request = io.ImageReadRequest()
        request.sample_policy = POLICIES['quiet']
        with pytest.raises(ValueError):
            io.read_image_series(path, request=request)


@pytest.mark.parametrize('directory,row', VALID, ids=[d.name+'/'+r['file'] for d, r in VALID])
def test_predicted_tile_export_words_and_bounds(directory, row, tmp_path):
    result = io.read_image_series(directory/row['file'], request=read_request(row))
    observer = ij.DatasetReadObserver()
    data = result.dataset.observe_reads(observer)
    estimate = io.estimate_image_write(data, result.metadata, request=write_request(row))
    assert observer.counts().plane_reads == 0
    path = tmp_path/'output.tif'
    io.write_image_series(path, data, result.metadata, request=write_request(row))
    assert path.stat().st_size <= estimate.encoded_upper_bound
    assert observer.counts().plane_reads == np.prod(row['shape'][:3])
    wire(path, row)
    assert io.read_image_series(path, request=read_request(row)).dataset.numpy('TCZYXS').tobytes().hex() == row['hex']
    before = path.read_bytes()
    with pytest.raises(RuntimeError):
        io.write_image_series(path, data, request=write_request(row))
    assert path.read_bytes() == before and list(tmp_path.iterdir()) == [path]


@pytest.mark.parametrize('codec',[8,32946,5])
@pytest.mark.parametrize('dtype', ['u16', 'u8', 'rgb8', 'f32'])
@pytest.mark.parametrize('action', ['read', 'write'])
@pytest.mark.parametrize('fault', ['cancel', 'callback'])
def test_predicted_tile_mid_row_faults(codec,dtype, action, fault, tmp_path):
    directory, row = next((d, r) for d, r in VALID if r['file'] == dtype+'-edges-be-finite-poison-c'+str(codec)+'.tif')
    token = ij.CancellationToken()
    path = tmp_path/'preserved.tif'
    path.write_bytes(b'preserved')

    def stage(name, n, total):
        assert name == 'Scientific TIFF tile rows'
        if n == 3:
            if fault == 'cancel':
                token.cancel()
            else:
                raise RuntimeError('injected tile row fault')

    with pytest.raises((ij.OperationCancelled, RuntimeError)):
        if action == 'read':
            io.read_image_series(directory/row['file'], cancellation=token, stage_progress=stage)
        else:
            data = io.read_image_series(directory/row['file']).dataset
            request = write_request(row)
            request.replace = io.FileReplacePolicy.REPLACE_EXISTING
            io.write_image_series(path, data, request=request, cancellation=token, stage_progress=stage)
    assert path.read_bytes() == b'preserved' and list(tmp_path.iterdir()) == [path]


def test_predicted_tile_ordinals_preserve_existing_contract():
    assert int(io.ImageFileFormat.SCIENTIFIC_TIFF_U16_TILED_V1) == 24
    assert [int(write_request({'dtype':dtype,'compression':codec}).format) for codec in [8,5] for dtype in ['u16','u8','rgb8']] + [int(write_request({'dtype':'f32','compression':codec}).format) for codec in [8,5]] == list(range(40,48))
    assert io.ImageWriteRequest().format == io.ImageFileFormat.SCIENTIFIC_TIFF_U16_V1


@pytest.mark.parametrize('codec',[8,32946,5])
@pytest.mark.parametrize('dtype',['u16','u8','rgb8','f32'])
@pytest.mark.parametrize('field',['max_file_bytes','max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes','max_metadata_bytes'])
def test_predicted_tile_read_budget_before_hooks(codec,dtype,field):
    directory,row=next((d,r) for d,r in VALID if r['file']==dtype+'-edges-be-finite-poison-c'+str(codec)+'.tif')
    limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):
        io.read_image_series(directory/row['file'],limits=limits,checkpoint=lambda:events.append(1))
    assert not events

@pytest.mark.parametrize('codec',[8,32946,5])
@pytest.mark.parametrize('dtype',['u16','u8','rgb8','f32'])
@pytest.mark.parametrize('field',['max_ifds','max_plane_samples','max_total_samples','max_working_bytes','max_codec_bytes','max_metadata_bytes','max_output_file_bytes'])
def test_predicted_tile_write_budget_before_reads(codec,dtype,field,tmp_path):
    directory,row=next((d,r) for d,r in VALID if r['file']==dtype+'-edges-be-finite-poison-c'+str(codec)+'.tif')
    observer=ij.DatasetReadObserver();data=io.read_image_series(directory/row['file']).dataset.observe_reads(observer)
    limits=io.ImageFileLimits();setattr(limits,field,1);events=[]
    with pytest.raises((ValueError,RuntimeError)):
        io.write_image_series(tmp_path/'absent.tif',data,request=write_request(row),limits=limits,checkpoint=lambda:events.append(1))
    assert observer.counts().plane_reads==0 and not events and not list(tmp_path.iterdir())
