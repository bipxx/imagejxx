"""Excluded TIFF carriers: guarded native admission and independent controls."""
from pathlib import Path
import hashlib
import json
import struct

import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest

FIXTURES=Path(__file__).parents[1]/"fixtures/scientific-tiff-refusals"
CASES=json.loads((FIXTURES/"SOURCE.json").read_bytes())["files"]


@pytest.mark.parametrize("case", CASES, ids=lambda c:c["file"])
def test_independent_refusals_before_user_hooks(case, tmp_path):
    path=FIXTURES/case["file"]
    raw=path.read_bytes()
    assert len(raw)==case["bytes"] and hashlib.sha256(raw).hexdigest()==case["sha256"]
    for witness in case["witness"]:
        expected=bytes.fromhex(witness["hex"])
        assert raw[witness["offset"]:witness["offset"]+len(expected)]==expected
    events=[]
    controls=dict(progress=lambda *v:events.append(v), checkpoint=lambda:events.append("checkpoint"), stage_progress=lambda *v:events.append(v))
    if case['file']=='bigtiff-complete.tif':
        info=io.inspect_image_file(path, **controls)
        assert info.container==io.TiffContainer.BIGTIFF
        np.testing.assert_array_equal(io.read_image_series(path, **controls).dataset.numpy('YX'),np.array([[10*y+x for x in range(5)] for y in range(3)],dtype=np.uint16))
        assert events and list(tmp_path.iterdir())==[]
        return
    if case['file']=='tiled-complete.tif':
        info=io.inspect_image_file(path, **controls)
        assert info.format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_TILED_V1
        np.testing.assert_array_equal(io.read_image_series(path, **controls).dataset.numpy('YX'),np.array([[10*y+x for x in range(16)] for y in range(16)],dtype=np.uint16))
        assert events and list(tmp_path.iterdir())==[]
        return
    if case['file']=='float-complete.tif':
        info=io.inspect_image_file(path, **controls)
        assert info.series[0].dtype=='float32'
        actual=io.read_image_series(path, **controls).dataset.numpy('YX')
        np.testing.assert_array_equal(actual,np.arange(15,dtype=np.float32).reshape(3,5))
        assert events and list(tmp_path.iterdir())==[]
        return
    if case['file']=='packbits-complete.tif':
        info=io.inspect_image_file(path,**controls)
        assert info.format==io.ImageFileFormat.SCIENTIFIC_TIFF_U16_PACKBITS_V1
        np.testing.assert_array_equal(io.read_image_series(path,**controls).dataset.numpy('YX'),np.array([[10*y+x for x in range(5)] for y in range(3)],dtype=np.uint16))
        assert events and list(tmp_path.iterdir())==[]
        return
    with pytest.raises(ValueError):
        io.inspect_image_file(path, **controls)
    with pytest.raises(ValueError):
        io.read_image_series(path, **controls)
    assert not events and list(tmp_path.iterdir())==[]


@pytest.mark.parametrize("options", [
    {"origin":(1.,0.,0.)},
    {"direction":(-1.,0.,0.,0.,1.,0.,0.,0.,1.)},
    {"time_origin":1.}, {"unit":"nm"}, {"time_unit":"hour"},
])
def test_unsupported_writer_calibration_before_reads_or_publication(tmp_path, options):
    source=ij.Dataset(np.array([[0,1,255],[256,32768,65535]],dtype=np.uint16), axes="YX", calibration=ij.SpatialCalibration(**options))
    observer=ij.DatasetReadObserver();source=source.observe_reads(observer)
    path=tmp_path/"preserved.tif";path.write_bytes(b"preserved target")
    request=io.ImageWriteRequest();request.replace=io.FileReplacePolicy.REPLACE_EXISTING
    events=[]
    with pytest.raises(ValueError):
        io.estimate_image_write(source)
    with pytest.raises(ValueError):
        io.write_image_series(path,source,request=request,progress=lambda *v:events.append(v), checkpoint=lambda:events.append("checkpoint"), stage_progress=lambda *v:events.append(v))
    assert not events and observer.counts().plane_reads==0
    assert path.read_bytes()==b"preserved target" and sorted(p.name for p in tmp_path.iterdir())==[path.name]


@pytest.mark.parametrize("name", ["rgb-complete","palette-complete","float-complete","signed-samples","tiled-complete"])
def test_complete_excluded_layout_control_with_independent_decoder(name):
    import tifffile
    assert tifffile.__version__=="2024.12.12"
    with tifffile.TiffFile(FIXTURES/(name+".tif")) as f:
        page=f.pages[0]
        actual=page.asarray()
        if name=="rgb-complete":
            expected=np.repeat(np.arange(15,dtype=np.uint16).reshape(3,5,1),3,axis=2)
        elif name=="float-complete":
            expected=np.arange(15,dtype=np.float32).reshape(3,5)
        else:
            y,x=np.indices((16,16) if name=="tiled-complete" else (3,5))
            expected=(10*y+x).astype(np.int16 if name=="signed-samples" else np.uint16)
        assert actual.dtype==expected.dtype
        np.testing.assert_array_equal(actual,expected)
        if name=="palette-complete":
            assert page.colormap.shape==(3,65536)
            np.testing.assert_array_equal(page.colormap,np.tile(np.arange(65536,dtype=np.uint16),(3,1)))


def test_packbits_complete_stream_with_independent_packet_decoder():
    import tifffile
    path=FIXTURES/"packbits-complete.tif"
    raw=path.read_bytes()
    with tifffile.TiffFile(path) as f:
        page=f.pages[0]
        assert page.tags[259].value==32773
        offsets,counts=page.dataoffsets,page.databytecounts
    assert len(offsets)==len(counts)==1
    encoded=raw[offsets[0]:offsets[0]+counts[0]]
    decoded=bytearray();at=0
    while at<len(encoded):
        code=encoded[at];at+=1
        if code<128:
            n=code+1;assert at+n<=len(encoded);decoded.extend(encoded[at:at+n]);at+=n
        elif code>128:
            n=257-code;assert at<len(encoded);decoded.extend(bytes([encoded[at]])*n);at+=1
    assert struct.unpack('<15H',decoded)==tuple(10*y+x for y in range(3) for x in range(5))


def test_subifd_complete_child_control():
    import tifffile
    with tifffile.TiffFile(FIXTURES/"subifd-child.tif") as f:
        assert len(f.pages)==1 and len(f.pages[0].pages)==1
        root=f.pages[0].asarray();child=f.pages[0].pages[0].asarray()
        np.testing.assert_array_equal(root,child)
        np.testing.assert_array_equal(root,np.array([[10*y+x for x in range(5)] for y in range(3)],dtype=np.uint16))


def test_bigtiff_complete_64bit_directory_control():
    import tifffile
    with tifffile.TiffFile(FIXTURES/"bigtiff-complete.tif") as f:
        assert f.is_bigtiff and len(f.pages)==1
        np.testing.assert_array_equal(f.pages[0].asarray(),np.array([[10*y+x for x in range(5)] for y in range(3)],dtype=np.uint16))
