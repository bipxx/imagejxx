"""Exact TIFF acceptance literals and independent raw-rational checks."""
from pathlib import Path
import hashlib
import json
import math
import struct

import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest

FIXTURES = Path(__file__).parents[1] / "fixtures/scientific-tiff-acceptance"
CASES = json.loads((FIXTURES / "SOURCE.json").read_bytes())["files"]


def wire(path):
    raw = path.read_bytes()
    endian = "<" if raw[:2] == b"II" else ">"
    def unpack(fmt, at):
        return struct.unpack_from(endian+fmt, raw, at)
    assert unpack("H", 2) == (42,)
    offset = unpack("I", 4)[0]
    pages, seen = [], set()
    while offset:
        assert offset not in seen
        seen.add(offset)
        count = unpack("H", offset)[0]
        tags = {}
        for i in range(count):
            at = offset+2+12*i
            tag, kind, n, value = unpack("HHII", at)
            size = n*{2:1, 3:2, 4:4, 5:8}[kind]
            start = at+8 if size <= 4 else value
            assert start+size <= len(raw)
            tags[tag] = (raw[start:start+size-1].decode("ascii") if kind == 2 else
                         unpack(("H" if kind == 3 else "I")*(n*(2 if kind == 5 else 1)), start))
        pixels = []
        x, y = (tags[k][0] for k in (256,257))
        rows = tags.get(278, (y,))[0]
        assert len(tags[273]) == len(tags[279]) == (y+rows-1)//rows
        for i, (start, size) in enumerate(zip(tags[273], tags[279])):
            assert size == 2*x*min(rows, y-i*rows)
            pixels.extend(unpack("H"*(size//2), start))
        pages.append((tags, pixels))
        offset = unpack("I", offset+2+12*count)[0]
    return pages


@pytest.mark.parametrize("case", CASES, ids=lambda c: c["file"])
def test_exact_independent_literals(case):
    path = FIXTURES/case["file"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == case["sha256"]
    info = io.inspect_image_file(path)
    result = io.read_image_series(path)
    assert info.series[0].shape == tuple(case["shape"])
    pages = wire(path)
    assert len(pages) == case["pages"]
    assert list(pages[0][0][279]) == case["strip_byte_counts"]
    shape = case["shape"]
    if case["file"].startswith("literal"):
        expected = np.array([0,1,255,256,32768,65535], dtype=np.uint16).reshape(shape[:-1])
    else:
        t,c,z,y,x = np.indices(shape[:-1])
        expected = (10000*t+1000*c+100*z+10*y+x).astype(np.uint16)
    np.testing.assert_array_equal(result.dataset.numpy("TCZYX"), expected)
    t_count,c_count,z_count = shape[:3]
    for p, (tags, pixels) in enumerate(pages):
        t,z,c = p//(c_count*z_count), (p//c_count)%z_count, p%c_count
        assert pixels == expected[t,c,z].ravel().tolist()
        assert tags[282] == (2,1) and tags[283] == (4,5)
    cal = result.dataset.calibration
    assert tuple(cal.spacing) == (.5,1.25,2.5) and cal.unit == "um"
    assert cal.frame_interval == 2.5 and cal.time_unit == "ms"


@pytest.mark.parametrize("spacing", [(.5,1.25,2.5), (.108,math.pi/10,1.23456789), (1/4294967295,4294967295,2.5)])
def test_writer_raw_rationals_and_literal_precision(tmp_path, spacing):
    literal = np.array([[0,1,255],[256,32768,65535]], dtype=np.uint16)
    data = ij.Dataset(literal, axes="YX", calibration=ij.SpatialCalibration(
        spacing=spacing, unit="um", frame_interval=2.5, time_unit="ms"))
    observer = ij.DatasetReadObserver()
    data = data.observe_reads(observer)
    path = tmp_path/"raw.tif"
    io.write_image_series(path, data)
    assert observer.counts().plane_reads == 1
    pages = wire(path)
    assert len(pages) == 1 and pages[0][1] == literal.ravel().tolist()
    tags = pages[0][0]
    for axis, tag in enumerate((282,283)):
        n,d = tags[tag]
        assert 0 < n <= 4294967295 and 0 < d <= 4294967295
        assert math.gcd(n,d) == 1 and abs(d/n-spacing[axis])/spacing[axis] <= 1e-6
    fields = dict(line.split("=",1) for line in tags[270].splitlines())
    assert "images" not in fields
    assert float(fields["spacing"]) == spacing[2] and float(fields["finterval"]) == 2.5
    assert fields["unit"] == "um" and fields["tunit"] == "ms"
