"""Typed Python consumers of the native service, with independent wire checks."""
from importlib.util import find_spec
import json
from pathlib import Path
import struct
import sys
import threading
import time

import imagejxx as ij
import numpy as np
import pytest

if find_spec("imagejxx._scientific_io") is None:
    pytest.skip("Optional native scientific IO is not built", allow_module_level=True)
from imagejxx import scientific_io as io

FIXTURES = Path(__file__).parents[1] / "fixtures/scientific-tiff-u16"
CASES = json.loads((FIXTURES / "SOURCE.json").read_text())["files"]


def expected():
    t, c, z, y, x = np.indices((2, 3, 2, 3, 5))
    return (10000*t + 1000*c + 100*z + 5*y + x).astype(np.uint16)


def dataset():
    return ij.Dataset(expected(), axes="TCZYX", calibration=ij.SpatialCalibration(
        spacing=(.3, 2.5, 2.75), unit="um", frame_interval=.125, time_unit="ms"))


@pytest.mark.parametrize("case", CASES, ids=lambda case: case["file"])
def test_independent_carriers_and_admission(case):
    events = []
    progress = lambda *value: events.append(value)
    path = FIXTURES / case["file"]
    if not case["valid"]:
        with pytest.raises(ValueError):
            io.inspect_image_file(path, progress=progress)
        with pytest.raises(ValueError):
            io.read_image_series(path, progress=progress,
                                 checkpoint=lambda: events.append("checkpoint"))
        assert events == []
        return
    info = io.inspect_image_file(path, progress=progress)
    assert events == [(0, 1), (1, 1)]
    assert info.file_bytes == path.stat().st_size
    assert info.series[0].shape == tuple(case["shape"])
    assert info.series[0].dtype == "uint16"
    assert info.series[0].sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
    events.clear()
    result = io.read_image_series(path, progress=progress)
    assert events == [(0, 1), (1, 1)]
    assert isinstance(result.dataset, ij.Dataset)
    if case["file"] == "defaults.tif":
        np.testing.assert_array_equal(result.dataset.numpy("YX"), [[0, 1]])
        assert "legacy_z_from_ifds" in result.metadata["defaulted_fields"]
    else:
        np.testing.assert_array_equal(result.dataset.numpy("TCZYX"), expected())
        assert result.dataset.calibration.unit == "um"
        assert result.dataset.calibration.frame_interval == .125
    metadata = result.metadata
    metadata.clear()
    assert result.metadata["format_id"] == io.format_id
    info.series.clear()
    assert len(info.series) == 1


def independent_pages(path):
    """Original stdlib TIFF reader, independent of native and Python codecs."""
    raw = path.read_bytes()
    assert raw[:4] == b"II*\0"
    off = struct.unpack_from("<I", raw, 4)[0]
    pages = []
    while off:
        count = struct.unpack_from("<H", raw, off)[0]
        tags = {}
        for i in range(count):
            at = off+2+12*i
            tag, kind, n, value = struct.unpack_from("<HHII", raw, at)
            size = n*{2: 1, 3: 2, 4: 4, 5: 8}[kind]
            start = at+8 if size <= 4 else value
            if kind == 2:
                tags[tag] = raw[start:start+size-1].decode("ascii")
            else:
                tags[tag] = struct.unpack_from("<" + ("H" if kind == 3 else "I")*(n*(2 if kind == 5 else 1)), raw, start)
        pixels = []
        for start, size in zip(tags[273], tags[279]):
            pixels.extend(struct.unpack_from("<"+"H"*(size//2), raw, start))
        pages.append((tags, pixels))
        off = struct.unpack_from("<I", raw, off+2+12*count)[0]
    return pages


def test_writer_independent_pixels_rationals_estimate_and_provenance(tmp_path):
    source = dataset()
    observer = ij.DatasetReadObserver()
    source = source.observe_reads(observer)
    metadata = {"document_name": "Python", "artist": "Original fixture"}
    estimate = io.estimate_image_write(source, metadata)
    assert observer.counts().plane_reads == observer.counts().region_reads == 0
    assert (estimate.planes, estimate.samples) == (12, 180)
    assert all(error <= 1e-6 for error in estimate.xy_relative_error)
    path = tmp_path / "显微镜.tif"
    events = []
    io.write_image_series(path, source, metadata, progress=lambda *v: events.append(v))
    assert events == [(0, 1), (1, 1)]
    assert observer.counts().plane_reads == 12
    assert path.stat().st_size == estimate.encoded_upper_bound
    pages = independent_pages(path)
    assert len(pages) == 12
    for p, (tags, values) in enumerate(pages):
        c, z, t = p % 3, (p//3) % 2, p//6
        assert values == expected()[t, c, z].ravel().tolist()
        for axis, tag in enumerate((282, 283)):
            assert tuple(estimate.xy_resolution[axis]) == tags[tag]
            actual = tags[tag][1]/tags[tag][0]
            assert abs(actual - (.3, 2.5)[axis])/(.3, 2.5)[axis] <= 1e-6
    assert "images=" not in pages[0][0][270]
    assert pages[0][0][305] == "imagejxx 0.2.0"
    loaded = io.read_image_series(path)
    np.testing.assert_array_equal(loaded.dataset.numpy("TCZYX"), expected())
    new_estimate = io.estimate_image_write(loaded.dataset, loaded.metadata)
    assert "source_description" in new_estimate.canonicalized_fields
    assert "source_imagej_version" in new_estimate.canonicalized_fields


@pytest.mark.parametrize("field", [
    "max_file_bytes", "max_ifds", "max_ifd_entries_per_page", "max_metadata_bytes",
    "max_total_samples", "max_plane_samples", "max_codec_bytes", "max_working_bytes",
])
def test_read_limits_zero_callbacks(field):
    limits = io.ImageFileLimits()
    setattr(limits, field, 1)
    events = []
    with pytest.raises(ValueError):
        io.read_image_series(FIXTURES / "little.tif", limits=limits,
                             progress=lambda *v: events.append(v),
                             checkpoint=lambda: events.append("checkpoint"))
    assert events == []


@pytest.mark.parametrize("metadata", [
    {"unknown": "no"}, {"artist": 1}, {"artist": "µm"}, {"artist": "a\0b"},
    {"defaulted_fields": {}}, {"defaulted_fields": [1]}, {1: "name"},
])
def test_metadata_strict_types_before_callbacks_or_reads(tmp_path, metadata):
    observer = ij.DatasetReadObserver()
    source = dataset().observe_reads(observer)
    events = []
    with pytest.raises((TypeError, ValueError)):
        io.write_image_series(tmp_path / "bad.tif", source, metadata,
                              progress=lambda *v: events.append(v))
    assert events == [] and observer.counts().plane_reads == 0
    assert list(tmp_path.iterdir()) == []


def test_python_conversion_budget_and_strict_limit_fields(tmp_path):
    limits = io.ImageFileLimits()
    limits.max_metadata_bytes = 1024
    with pytest.raises(ValueError):
        io.write_image_series(tmp_path / "large.tif", dataset(), {"artist": "a"*4096}, limits=limits)
    for path in ("a"*4096, "µ"*600, "nul\0path", "surrogate\ud800"):
        events = []
        with pytest.raises(ValueError):
            io.inspect_image_file(path, limits=limits, progress=lambda *v: events.append(v))
        assert events == []
    for value in (True, 1.0, "1"):
        with pytest.raises(TypeError):
            limits.max_ifds = value
    with pytest.raises((ValueError, OverflowError)):
        limits.max_ifds = -1
    request = io.ImageReadRequest()
    request.series_index = 1
    with pytest.raises(IndexError):
        io.read_image_series(tmp_path / "missing", request=request)
    with pytest.raises(TypeError):
        io.inspect_image_file(b"bytes-path")


@pytest.mark.parametrize("which", ["read", "write"])
@pytest.mark.parametrize("callback", ["progress", "checkpoint", "stage_progress"])
def test_callback_exception_identity_and_cleanup(tmp_path, which, callback):
    class Marker(Exception):
        pass
    failure = Marker("owned exception")
    def throws(*_):
        raise failure
    with pytest.raises(Marker) as caught:
        if which == "read":
            io.read_image_series(FIXTURES / "little.tif", **{callback: throws})
        else:
            io.write_image_series(tmp_path / "failure.tif", dataset(), **{callback: throws})
    assert caught.value is failure
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize("which", ["read", "write"])
def test_cross_module_cancellation_and_row_stage(tmp_path, which):
    token = ij.CancellationToken()
    stages = []
    def stage(name, done, total):
        assert name == "Scientific TIFF rows" and total == 36
        stages.append(done)
        if done == 2:
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        if which == "read":
            io.read_image_series(FIXTURES / "little.tif", cancellation=token, stage_progress=stage)
        else:
            io.write_image_series(tmp_path / "cancel.tif", dataset(), cancellation=token, stage_progress=stage)
    assert stages == [0, 1, 2]
    assert list(tmp_path.iterdir()) == []


def test_atomic_replace_completion_cancel_and_race_winner(tmp_path):
    path = tmp_path / "atomic.tif"
    source = dataset()
    io.write_image_series(path, source)
    original = path.read_bytes()
    with pytest.raises(RuntimeError):
        io.write_image_series(path, source)
    assert path.read_bytes() == original
    request = io.ImageWriteRequest()
    request.replace = io.FileReplacePolicy.REPLACE_EXISTING
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        io.write_image_series(path, source, request=request, cancellation=token,
                              progress=lambda n, total: token.cancel() if n == 1 else None)
    assert path.read_bytes() == original
    race = tmp_path / "race.tif"
    with pytest.raises(RuntimeError):
        io.write_image_series(race, source, progress=lambda n, total: race.write_bytes(b"winner") if n == 1 else None)
    assert race.read_bytes() == b"winner"
    assert sorted(p.name for p in tmp_path.iterdir()) == ["atomic.tif", "race.tif"]


def test_frozen_arguments_detached_metadata_and_output_limit(tmp_path):
    source = dataset()
    metadata = {"artist": "original"}
    limits = io.ImageFileLimits()
    request = io.ImageWriteRequest()
    def mutate(n, total):
        if n == 0:
            metadata["artist"] = "changed"
            limits.max_output_file_bytes = 1
            request.replace = io.FileReplacePolicy.REPLACE_EXISTING
    path = tmp_path / "frozen.tif"
    io.write_image_series(path, source, metadata, limits=limits, request=request, progress=mutate)
    assert io.read_image_series(path).metadata["artist"] == "original"
    with pytest.raises(ValueError):
        io.write_image_series(tmp_path / "too-big.tif", source, limits=limits)
    assert len(list(tmp_path.iterdir())) == 1


def test_native_body_releases_gil(tmp_path):
    source = ij.Dataset(np.arange(1024*768, dtype=np.uint16).reshape(768, 1024), axes="YX")
    path = tmp_path / "thread.tif"
    io.write_image_series(path, source)
    go, stop = threading.Event(), threading.Event()
    ticks = []
    def worker():
        go.wait()
        while not stop.is_set():
            ticks.append(1)
            time.sleep(.001)
    thread = threading.Thread(target=worker)
    thread.start()
    old_interval = sys.getswitchinterval()
    def progress(n, total):
        if n == 0:
            go.set()
        else:
            assert ticks, "Python worker did not run during native decode"
    try:
        sys.setswitchinterval(1)
        result = io.read_image_series(path, progress=progress)
        assert result.dataset.shape == (1, 1, 1, 768, 1024, 1)
    finally:
        sys.setswitchinterval(old_interval)
        stop.set()
        go.set()
        thread.join(timeout=5)
    assert not thread.is_alive()


def test_legacy_tiff_ndarray_remains_separate(tmp_path):
    # A generic TIFF with no ImageJ description still follows legacy ndarray IO.
    import tifffile
    array = np.arange(3*5*7, dtype=np.uint16).reshape(3, 5, 7)
    path = tmp_path / "legacy.tif"
    ij.imwrite(path, array, photometric="minisblack", description="legacy ndarray")
    restored = ij.imread(path)
    assert restored.dtype == array.dtype
    np.testing.assert_array_equal(restored, array)
    with pytest.raises(ValueError):
        io.read_image_series(path)
    assert tifffile.__version__ == "2024.12.12"
