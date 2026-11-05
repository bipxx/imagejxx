"""Resliced TIFF pixels and world transforms, with independent wire observations."""
import struct
import json
import os
from pathlib import Path

import imagejxx as ij
from imagejxx import scientific_io as io
import numpy as np
import pytest


def description(path):
    data = path.read_bytes()
    assert data[:2] == b"II"
    big = struct.unpack_from("<H", data, 2)[0] == 43
    offset = struct.unpack_from("<Q" if big else "<I", data, 8 if big else 4)[0]
    count = struct.unpack_from("<Q" if big else "<H", data, offset)[0]
    for i in range(count):
        at = offset + (8 if big else 2) + i * (20 if big else 12)
        tag, kind, length, value = struct.unpack_from("<HHQQ" if big else "<HHII", data, at)
        if tag == 270:
            assert kind == 2 and length > (8 if big else 4)
            assert data[value + length - 1] == 0
            return data[value:value + length - 1].decode("ascii")
    pytest.fail("Missing ImageDescription")


@pytest.mark.parametrize("plane", ["xy", "xz", "yz"])
@pytest.mark.parametrize("dtype,format_name", [
    (np.uint16, "SCIENTIFIC_TIFF_U16_V1"),
    (np.uint8, "SCIENTIFIC_TIFF_U8_V1"),
    (np.float32, "SCIENTIFIC_TIFF_FLOAT32_V1"),
])
@pytest.mark.parametrize("container", [io.TiffContainer.CLASSIC, io.TiffContainer.BIGTIFF])
def test_reslice_export_reopen_preserves_pixels_and_affine(tmp_path, plane, dtype, format_name, container):
    values = np.arange(2*3*4*5*6).reshape(2, 3, 4, 5, 6) % 251
    values = values.astype(dtype)
    direction = np.array([[-1, .25, .5], [0, 1, .5], [.5, 0, 1]])
    spacing = np.array([.5, 1.25, 2])
    origin = np.array([11, -7, 19])
    source = ij.Dataset(values, axes="TCZYX", calibration=ij.SpatialCalibration(
        spacing=spacing.tolist(), origin=origin.tolist(), direction=direction.ravel().tolist(),
        unit="um", frame_interval=.75, time_origin=2.25))
    result = ij.reslice_axis_range(source, ij.AxisRangeResliceRequest(plane, 1, 3, 2, t=1, c=2))
    reads = ij.DatasetReadObserver()
    exported = result.dataset.observe_reads(reads)
    request = io.ImageWriteRequest()
    request.format = getattr(io.ImageFileFormat, format_name)
    request.container = container
    estimate = io.estimate_image_write(exported, request=request)
    assert reads.counts().plane_reads == 0
    target = tmp_path / "resliced.tif"
    io.write_image_series(target, exported, request=request)
    assert target.stat().st_size <= estimate.encoded_upper_bound
    restored = io.read_image_series(target).dataset
    volume = values[1, 2]
    expected = (volume[[1, 3], :, :] if plane == "xy" else
                volume[:, [1, 3], :].transpose(1, 0, 2) if plane == "xz" else
                volume[:, :, [1, 3]].transpose(2, 0, 1))
    np.testing.assert_array_equal(restored.numpy("ZYX"), expected)
    axes = {"xy": (0, 1, 2), "xz": (0, 2, 1), "yz": (1, 2, 0)}[plane]
    cal = restored.calibration
    np.testing.assert_array_equal(cal.direction, direction[:, axes].ravel())
    np.testing.assert_array_equal(cal.spacing, spacing[list(axes)] * [1, 1, 2])
    shift = np.zeros(3); shift[axes[2]] = spacing[axes[2]]
    np.testing.assert_array_equal(cal.origin, origin + direction @ shift)
    assert cal.time_origin == 3 and cal.frame_interval == .75
    assert cal.unit == "um" and cal.time_unit == "s"
    text = description(target)
    fields = dict(line.split("=", 1) for line in text.splitlines())
    assert fields["imagejxx_spatial"] == "1"
    assert [float(x) for x in fields["imagejxx_direction"].split(",")] == direction[:, axes].ravel().tolist()
    assert [float(x) for x in fields["imagejxx_origin"].split(",")] == (origin + direction @ shift).tolist()
    assert float(fields["imagejxx_time_origin"]) == 3
    virtual = io.open_virtual_image_series(target).dataset
    np.testing.assert_array_equal(virtual.numpy("ZYX"), expected)
    assert virtual.calibration.origin == cal.origin
    assert virtual.calibration.direction == cal.direction
    assert virtual.calibration.time_origin == cal.time_origin


@pytest.mark.parametrize("dtype,samples,format_name", [
    (np.uint16, 1, "SCIENTIFIC_TIFF_U16_DEFLATE_V1"),
    (np.uint16, 1, "SCIENTIFIC_TIFF_U16_LZW_V1"),
    (np.uint8, 1, "SCIENTIFIC_TIFF_U8_PACKBITS_V1"),
    (np.uint8, 3, "SCIENTIFIC_TIFF_RGB8_DEFLATE_HORIZONTAL_TILED_V1"),
    (np.float32, 1, "SCIENTIFIC_TIFF_FLOAT32_LZW_FLOATING_TILED_V1"),
])
def test_compressed_and_tiled_spatial_roundtrip(tmp_path, dtype, samples, format_name):
    values = (np.arange(2*5*7*samples) % 251).reshape(2, 5, 7, samples).astype(dtype)
    cal = ij.SpatialCalibration(origin=(11, -7, 19), time_origin=2.25,
        direction=(1, 0, 0, 0, 0, 1, 0, 1, 0))
    request = io.ImageWriteRequest()
    request.format = getattr(io.ImageFileFormat, format_name)
    target = tmp_path / "compressed.tif"
    io.write_image_series(target, ij.Dataset(values, axes="ZYXS", calibration=cal), request=request)
    restored = io.read_image_series(target).dataset
    np.testing.assert_array_equal(restored.numpy("ZYXS"), values)
    assert restored.calibration.origin == [11, -7, 19]
    assert restored.calibration.direction == [1, 0, 0, 0, 0, 1, 0, 1, 0]
    assert restored.calibration.time_origin == 2.25


def test_spatial_metadata_admission_precedes_reads_and_file_creation(tmp_path):
    reads = ij.DatasetReadObserver()
    source = ij.Dataset(np.array([[1, 2]], np.uint16), axes="YX",
        calibration=ij.SpatialCalibration(origin=(11, -7, 19))).observe_reads(reads)
    estimate = io.estimate_image_write(source)
    limits = io.ImageFileLimits()
    limits.max_metadata_bytes = estimate.metadata_bytes - 1
    target = tmp_path / "not-created.tif"
    events = []
    with pytest.raises(ValueError):
        io.estimate_image_write(source, limits=limits)
    with pytest.raises(ValueError):
        io.write_image_series(target, source, limits=limits,
            progress=lambda *x: events.append(x), checkpoint=lambda: events.append("checkpoint"))
    assert reads.counts().plane_reads == 0 and not events
    assert list(tmp_path.iterdir()) == []


@pytest.mark.parametrize("options", [
    {"origin": (1, 0, 0)},
    {"direction": (-1, 0, 0, 0, 1, 0, 0, 0, 1)},
    {"time_origin": 1},
])
def test_previously_unsupported_writer_transforms_now_roundtrip(tmp_path, options):
    calibration = ij.SpatialCalibration(**options)
    source = ij.Dataset(np.array([[0, 1, 255], [256, 32768, 65535]], np.uint16),
                        axes="YX", calibration=calibration)
    target = tmp_path / "spatial.tif"
    io.write_image_series(target, source)
    restored = io.read_image_series(target).dataset
    np.testing.assert_array_equal(restored.numpy("YX"), [[0, 1, 255], [256, 32768, 65535]])
    assert restored.calibration.origin == calibration.origin
    assert restored.calibration.direction == calibration.direction
    assert restored.calibration.time_origin == calibration.time_origin


def replace_description(path, text):
    """Mutate only a fresh classic TIFF carrier, retaining every pixel/IFD."""
    data = bytearray(path.read_bytes())
    assert data[:4] == b"II*\0"
    offset = struct.unpack_from("<I", data, 4)[0]
    count = struct.unpack_from("<H", data, offset)[0]
    for i in range(count):
        at = offset + 2 + 12*i
        if struct.unpack_from("<H", data, at)[0] == 270:
            encoded = text.encode("ascii") + b"\0"
            struct.pack_into("<II", data, at+4, len(encoded), len(data))
            data.extend(encoded)
            path.write_bytes(data)
            return
    pytest.fail("Missing description to mutate")


@pytest.mark.parametrize("extension", [
    "imagejxx_spatial=2\nimagejxx_origin=0,0,0\nimagejxx_direction=1,0,0,0,1,0,0,0,1\nimagejxx_time_origin=0\n",
    "imagejxx_origin=0,0,0\n",
    "imagejxx_spatial=1\nimagejxx_origin=0,0\nimagejxx_direction=1,0,0,0,1,0,0,0,1\nimagejxx_time_origin=0\n",
    "imagejxx_spatial=1\nimagejxx_origin=0,0,0,0\nimagejxx_direction=1,0,0,0,1,0,0,0,1\nimagejxx_time_origin=0\n",
    "imagejxx_spatial=1\nimagejxx_origin=0,0,0\nimagejxx_direction=0,0,0,0,0,0,0,0,0\nimagejxx_time_origin=0\n",
    "imagejxx_spatial=1\nimagejxx_origin=nan,0,0\nimagejxx_direction=1,0,0,0,1,0,0,0,1\nimagejxx_time_origin=0\n",
    "imagejxx_spatial=1\nimagejxx_origin=0,0,0\nimagejxx_direction=1,0,0,0,1,0,0,0,1\nimagejxx_time_origin=1e999\n",
])
def test_malformed_spatial_extension_refuses_before_decode(tmp_path, extension):
    target = tmp_path / "malformed.tif"
    io.write_image_series(target, ij.Dataset(np.array([[1, 2]], np.uint16), axes="YX"))
    original = description(target)
    assert "imagejxx_spatial" not in original
    replace_description(target, original + extension)
    before = target.read_bytes()
    events = []
    with pytest.raises(ValueError):
        io.inspect_image_file(target, progress=lambda *x: events.append(x))
    with pytest.raises(ValueError):
        io.read_image_series(target, progress=lambda *x: events.append(x))
    assert not events and target.read_bytes() == before


def test_scientific_open_czt_xyz_reslice_export_and_reopen(tmp_path):
    from imagejxx import _desktop
    from test_binary_options import click
    import json

    values = np.arange(2*3*4*5*6, dtype=np.uint16).reshape(2, 3, 4, 5, 6)
    source = ij.Dataset(values, axes="TCZYX", calibration=ij.SpatialCalibration(
        spacing=(.5, 1.25, 2), unit="um", frame_interval=.75))
    source_path = tmp_path / "source.tif"
    io.write_image_series(source_path, source)
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    def action(name, **options):
        editor.action(name, json.dumps(options))
    def frame(events=()):
        result = json.loads(editor.render_headless(events=json.dumps(list(events))))
        assert not result["window_error"]
        return result["scientific_io"]

    editor.open_file(str(source_path)); editor.wait()
    assert frame()["inspection"]["shape"] == [2, 3, 4, 5, 6, 1]
    action("scientific_io_draft", confirm_axes=True)
    action("scientific_io_apply"); editor.wait()
    assert not frame()["open"]
    original = editor.native_snapshot()
    view = original.display; view.t = 1; view.c = 2
    view.crosshair = [3, 2, 1]; view.orthogonal = True
    editor.set_native_display(view)
    np.testing.assert_array_equal(editor.plane("xy"), values[1, 2, 1])
    np.testing.assert_array_equal(editor.plane("xz"), values[1, 2, :, 2, :])
    np.testing.assert_array_equal(editor.plane("yz"), values[1, 2, :, :, 3])
    action("reslice_axis_range", document_id=original.id, plane="xz", first=1, last=3, step=2)
    expected = values[1, 2, :, [1, 3], :]
    np.testing.assert_array_equal(editor.native_snapshot().dataset.numpy("ZYX"), expected)
    output = tmp_path / "resliced.tif"
    editor.recording = True
    action("show_scientific_export", path=str(output))
    action("scientific_io_inspect"); editor.wait()
    estimated = frame()
    assert not estimated["error"]
    assert estimated["estimate"]["calibration"]["spatial_version"] == 1
    assert estimated["estimate"]["calibration"]["origin"] == [0, 1.25, 0]
    assert estimated["controls"]["spatial_calibration"]["enabled"]
    accepted = frame(click(estimated["controls"]["accept_loss"]["point"]))
    assert accepted["accept_loss"] and accepted["controls"]["apply"]["enabled"]
    frame(click(accepted["controls"]["apply"]["point"])); editor.wait()
    assert not frame()["error"] and not frame()["open"]
    recorded = json.loads(editor.workflow)[-1]
    assert recorded["encoded_calibration"]["origin"] == [0, 1.25, 0]
    action("review_scientific_io", record=recorded)
    assert frame()["open"] and not frame()["accept_loss"]
    action("cancel_scientific_io")
    restored = io.read_image_series(output).dataset
    np.testing.assert_array_equal(restored.numpy("ZYX"), expected)
    assert restored.calibration.origin == [0, 1.25, 0]
    assert restored.calibration.direction == [1, 0, 0, 0, 0, 1, 0, 1, 0]
    assert restored.calibration.time_origin == .75
    assert editor.native_document(original.id).snapshot().shares_data(original)
    reopened = _desktop.Editor(str(tmp_path / "reopened-preferences.json"))
    reopened.recording = True
    reopened.open_file(str(output)); reopened.wait()
    review = json.loads(reopened.render_headless())["scientific_io"]
    assert review["inspection"]["calibration"]["origin"] == [0, 1.25, 0]
    assert review["controls"]["spatial_calibration"]["enabled"]
    reopened.action("scientific_io_draft", json.dumps({"confirm_axes": True}))
    reopened.action("scientific_io_apply", "{}"); reopened.wait()
    np.testing.assert_array_equal(reopened.native_snapshot().dataset.numpy("ZYX"), expected)
    assert reopened.native_snapshot().dataset.calibration.direction == [1, 0, 0, 0, 0, 1, 0, 1, 0]
    open_record = json.loads(reopened.workflow)[-1]
    assert open_record["reviewed_series"]["calibration"]["time_origin"] == .75
    reopened.action("review_scientific_io", json.dumps({"record": open_record}))
    assert json.loads(reopened.render_headless())["scientific_io"]["open"]
    reopened.action("cancel_scientific_io", "{}")


def test_standalone_scientific_workflow_wire_observations():
    from test_scientific_tiff_io import independent_pages
    path = os.environ.get("IMAGEJXX_SPATIAL_CPP_REPORT")
    if not path:
        pytest.skip("Requires standalone native scientific workflow capture")
    path = Path(path)
    report = json.loads(path.read_bytes())
    assert report["context"] == "imgui_cpp" and report["shape"] == [1, 1, 2, 4, 6, 1]
    assert report["origin"] == [0, 1.25, 0]
    assert report["direction"] == [1, 0, 0, 0, 0, 1, 0, 1, 0]
    assert report["time_origin"] == .75
    assert report["export_record"]["encoded_calibration"]["spatial_version"] == 1
    pages = independent_pages(path.with_name("resliced.tif"))
    assert len(pages) == 2
    for k, (tags, samples) in enumerate(pages):
        assert samples == [12000+100*z+10*(1+2*k)+x for z in range(4) for x in range(6)]
        if k == 0:
            fields = dict(line.split("=", 1) for line in tags[270].splitlines())
            assert fields["imagejxx_spatial"] == "1"
            assert fields["imagejxx_origin"] == "0,1.25,0"
            assert fields["imagejxx_time_origin"] == "0.75"
