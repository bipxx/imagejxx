"""Analyzer line statistics use Float32 profile values, not a line raster."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


REFERENCE = Path(__file__).parents[1] / "fixtures/selection-measurements-imagej154r.jsonl.gz"
ROWS = [json.loads(line) for line in gzip.decompress(REFERENCE.read_bytes()).splitlines()]
ROWS += [json.loads(line) for line in gzip.decompress(
    REFERENCE.with_name("selection-measurements-types-imagej154r.jsonl.gz").read_bytes()).splitlines()]


@pytest.mark.parametrize("row", [r for r in ROWS if r["kind"] == "line"])
def test_original_imagej_analyzer_line(row):
    pixels = (np.arange(5)[:, None] * 10 + np.arange(7)[None, :]).astype(np.uint8)
    kind = row.get("type", "uint8")
    if kind == "uint16":
        pixels = pixels.astype(np.uint16) * 257
    elif kind == "float32":
        pixels = pixels.astype(np.float32) * .25 - 8
    spatial = ij.SpatialCalibration(spacing=[*row["spacing"], 1], unit="um")
    density = ij.ImageJIntensityCalibration.uncalibrated()
    if row["density"] != "raw":
        density = ij.ImageJIntensityCalibration.function(kind,
            "straight_line" if row["density"] == "linear" else "poly2",
            [10, 2] if row["density"] == "linear" else [0, 0, 1], "value")
    measured = ij.imagej_calibrated_line_measurement(pixels, spatial, density, *row["points"])
    columns = row["columns"]
    for actual, original in [("area", "Area"), ("mean", "Mean"), ("stddev", "StdDev"),
        ("minimum", "Min"), ("maximum", "Max"), ("median", "Median"),
        ("integrated_density", "IntDen"), ("length", "Length"), ("angle_degrees", "Angle")]:
        assert measured[actual] == pytest.approx(columns[original], rel=2e-14, abs=2e-14), actual
    np.testing.assert_allclose(measured["midpoint_world"][:2], [columns["X"], columns["Y"]], rtol=2e-14)
    assert measured["raw_mean"] * measured["pixel_count"] == pytest.approx(columns["RawIntDen"], rel=2e-14)
    assert pixels[0, 0] == (-8 if kind == "float32" else 0)
    assert pixels[-1, -1] == (46 * 257 if kind == "uint16" else 3.5 if kind == "float32" else 46)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32])
def test_affine_geometry_and_detached_statistics(dtype):
    pixels = np.array([[0, 1, 2], [3, 4, 5]], dtype=dtype)
    spatial = ij.SpatialCalibration(spacing=[3, 4, 1], origin=[11, -7, 5],
                                    direction=[0, -1, 0, 1, 0, 0, 0, 0, 1], unit="um")
    measured = ij.imagej_calibrated_line_measurement(pixels, spatial,
        ij.ImageJIntensityCalibration.uncalibrated(), 0, 0, 2, 1)
    assert measured["length"] == pytest.approx(np.sqrt(52))
    assert measured["area"] == 36 and measured["mean"] == 2.5 and measured["stddev"] == 2.5
    assert measured["midpoint_world"] == [9, -4, 5]
    assert measured["angle_degrees"] == pytest.approx(-np.degrees(np.arctan2(4, 6)))
    measured["histogram"][:] = 999
    assert measured["pixel_count"] == 3


@pytest.mark.parametrize("line", [[-1, 0, 1, 1], [0, 0, 99, 1], [0, 0, float("nan"), 1]])
def test_invalid_line_and_cap(line):
    pixels = np.zeros((2, 3), np.uint8)
    with pytest.raises(ValueError):
        ij.imagej_calibrated_line_measurement(pixels, ij.SpatialCalibration(),
            ij.ImageJIntensityCalibration.uncalibrated(), *line)
    with pytest.raises(ValueError, match="working"):
        ij.imagej_calibrated_line_measurement(pixels, ij.SpatialCalibration(),
            ij.ImageJIntensityCalibration.uncalibrated(), 0, 0, 1, 1, max_working_bytes=1)


def test_nonfinite_density_refuses_explicitly():
    from test_calibrated_analysis import density
    with pytest.raises(ValueError, match="finite Float32"):
        ij.imagej_calibrated_line_measurement(np.zeros((2, 3), np.uint8),
            ij.SpatialCalibration(), density("all_nan", "uint8"), 0, 0, 1, 1)


@pytest.mark.parametrize("line,angle", [([2, 0, 0, 0], 180), ([0, 0, 2, 0], 0),
                                      ([0, 0, 0, 1], -90), ([0, 0, 0, 0], 0)])
def test_image_axis_heading_sign(line, angle):
    measured = ij.imagej_calibrated_line_measurement(np.zeros((2, 3), np.uint8),
        ij.SpatialCalibration(), ij.ImageJIntensityCalibration.uncalibrated(), *line)
    assert measured["angle_degrees"] == angle
