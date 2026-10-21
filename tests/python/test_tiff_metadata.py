"""Literal metadata ledgers, calibration refusals and independent canonical wire."""
from pathlib import Path
import hashlib
import json
import math

import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest
from test_tiff_acceptance import wire

FIXTURES = Path(__file__).parents[1]/"fixtures/scientific-tiff-metadata"
CASES = json.loads((FIXTURES/"SOURCE.json").read_bytes())["files"]
LITERAL = np.array([[0,1,255],[256,32768,65535]], dtype=np.uint16)


@pytest.mark.parametrize("case", CASES, ids=lambda c: c["file"])
def test_literal_metadata_or_refusal(case, tmp_path):
    path = FIXTURES/case["file"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == case["sha256"]
    events = []
    controls = dict(progress=lambda *v: events.append(v),
                    checkpoint=lambda: events.append("checkpoint"),
                    stage_progress=lambda *v: events.append(v))
    if not case["valid"]:
        with pytest.raises(ValueError):
            io.inspect_image_file(path, **controls)
        with pytest.raises(ValueError):
            io.read_image_series(path, **controls)
        assert events == [] and list(tmp_path.iterdir()) == []
        return
    info = io.inspect_image_file(path)
    result = io.read_image_series(path)
    assert result.dataset.shape == tuple(case["shape"])
    cal = result.dataset.calibration
    assert list(cal.spacing) == case["spacing"] and cal.unit == case["unit"]
    assert cal.frame_interval == case["frame_interval"] and cal.time_unit == case["time_unit"]
    metadata = result.metadata
    assert metadata["defaulted_fields"] == case["defaulted_fields"]
    assert metadata["source_description"] == case["description"]
    assert info.series[0].metadata == metadata
    t,c,z,y,x = np.indices(case["shape"][:-1])
    expected = (10000*t+1000*c+100*z+10*y+x).astype(np.uint16)
    np.testing.assert_array_equal(result.dataset.numpy("TCZYX"), expected)
    observer = ij.DatasetReadObserver()
    source = result.dataset.observe_reads(observer)
    estimate = io.estimate_image_write(source, metadata)
    ledger = ["defaulted_fields", "source_description", "source_imagej_version"]
    if "source_software" in metadata:
        ledger.append("source_software")
    assert estimate.canonicalized_fields == ledger and observer.counts().plane_reads == 0
    output = tmp_path/"canonical.tif"
    io.write_image_series(output, source, metadata)
    shape = case["shape"]
    assert observer.counts().plane_reads == shape[0]*shape[1]*shape[2]
    pages = wire(output)
    assert len(pages) == shape[0]*shape[1]*shape[2]
    for p,(tags,pixels) in enumerate(pages):
        t,z,c = p//(shape[1]*shape[2]), (p//shape[1])%shape[2], p%shape[1]
        assert pixels == expected[t,c,z].ravel().tolist()
        for axis,tag in enumerate((282,283)):
            n,d = tags[tag]
            assert abs(d/n-case["spacing"][axis])/case["spacing"][axis] <= 1e-6
    fields = dict(line.split("=",1) for line in pages[0][0][270].splitlines())
    assert "images" not in fields and "\r" not in pages[0][0][270]
    assert [int(fields[k]) for k in ("frames","channels","slices")] == shape[:3]
    assert float(fields["spacing"]) == case["spacing"][2]
    assert float(fields["finterval"]) == case["frame_interval"]
    assert fields["unit"] == ("pixel" if case["unit"] == "px" else case["unit"])
    assert fields["tunit"] == ("sec" if case["time_unit"] == "s" else case["time_unit"])
    for key,tag in (("document_name",269),("artist",315),("date_time",306)):
        if key in metadata:
            assert pages[0][0][tag] == metadata[key]
    assert pages[0][0][305] == "imagejxx 0.2.0"


@pytest.mark.parametrize("axis", [0,1])
@pytest.mark.parametrize("spacing", [math.nextafter(1/4294967295,0), math.nextafter(4294967295,math.inf), math.nextafter(0,1), float.fromhex('0x1.fffffffffffffp+1023')])
def test_outside_rational_interval_before_reads_or_callbacks(tmp_path, axis, spacing):
    xyz = [1.,1.,1.]
    xyz[axis] = spacing
    observer = ij.DatasetReadObserver()
    source = ij.Dataset(LITERAL, axes="YX", calibration=ij.SpatialCalibration(spacing=xyz)).observe_reads(observer)
    with pytest.raises(ValueError):
        io.estimate_image_write(source)
    path = tmp_path/"preserved.tif"
    path.write_bytes(b"existing bytes")
    request = io.ImageWriteRequest()
    request.replace = io.FileReplacePolicy.REPLACE_EXISTING
    events = []
    with pytest.raises(ValueError):
        io.write_image_series(path, source, request=request,
            progress=lambda *v: events.append(v), checkpoint=lambda: events.append("checkpoint"),
            stage_progress=lambda *v: events.append(v))
    assert events == [] and observer.counts().plane_reads == 0
    assert path.read_bytes() == b"existing bytes" and sorted(p.name for p in tmp_path.iterdir()) == [path.name]


@pytest.mark.parametrize("field", ["spacing","frame_interval"])
@pytest.mark.parametrize("value", [0.,-1.,math.nan,math.inf])
def test_invalid_dataset_calibration_before_io(tmp_path, field, value):
    # These violate the Dataset contract itself, before the IO planner is called.
    kwargs = {field: (value,1.,1.) if field == "spacing" else value}
    with pytest.raises(ValueError):
        ij.Dataset(LITERAL, axes="YX", calibration=ij.SpatialCalibration(**kwargs))
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize("spacing", [math.nextafter(1/4294967295,math.inf), math.nextafter(4294967295,0)])
def test_inside_rational_boundary_raw_precision(tmp_path, spacing):
    source = ij.Dataset(LITERAL, axes="YX", calibration=ij.SpatialCalibration(spacing=(spacing,spacing,1.)))
    path = tmp_path/"inside.tif"
    io.write_image_series(path, source)
    pages = wire(path)
    assert len(pages) == 1 and pages[0][1] == LITERAL.ravel().tolist()
    for tag in (282,283):
        n,d = pages[0][0][tag]
        assert 0 < n <= 4294967295 and 0 < d <= 4294967295
        assert abs(d/n-spacing)/spacing <= 1e-6


def test_direct_unit_alias_ledger(tmp_path):
    observer = ij.DatasetReadObserver()
    source = ij.Dataset(LITERAL, axes="YX", calibration=ij.SpatialCalibration(unit="pixel", time_unit="sec")).observe_reads(observer)
    estimate = io.estimate_image_write(source)
    assert estimate.canonicalized_fields == ["unit", "time_unit"] and observer.counts().plane_reads == 0
    assert estimate.encoded_calibration.unit == "px" and estimate.encoded_calibration.time_unit == "s"
    path = tmp_path/"alias.tif"
    io.write_image_series(path, source)
    tags,pixels = wire(path)[0]
    assert pixels == LITERAL.ravel().tolist()
    fields = dict(line.split("=",1) for line in tags[270].splitlines())
    assert fields["unit"] == "pixel" and fields["tunit"] == "sec"
    assert observer.counts().plane_reads == 1


@pytest.mark.parametrize("field", ["max_file_bytes","max_ifds","max_ifd_entries_per_page","max_metadata_bytes","max_total_samples","max_plane_samples","max_codec_bytes","max_working_bytes","max_output_file_bytes"])
def test_zero_limits_before_file_access_callbacks_or_source_reads(tmp_path, field):
    observer = ij.DatasetReadObserver()
    source = ij.Dataset(LITERAL, axes="YX").observe_reads(observer)
    limits = io.ImageFileLimits()
    setattr(limits, field, 0)
    events = []
    controls = dict(progress=lambda *v:events.append(v), checkpoint=lambda:events.append("checkpoint"), stage_progress=lambda *v:events.append(v))
    with pytest.raises(ValueError):
        io.inspect_image_file(tmp_path/"missing.tif", limits=limits, **controls)
    with pytest.raises(ValueError):
        io.read_image_series(tmp_path/"missing.tif", limits=limits, **controls)
    with pytest.raises(ValueError):
        io.write_image_series(tmp_path/"new.tif", source, limits=limits, **controls)
    assert not events and observer.counts().plane_reads == 0 and list(tmp_path.iterdir()) == []
