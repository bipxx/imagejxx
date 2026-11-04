"""Actual original irregular profiles and refreshed calibrated angle rows."""
import gzip
import json
import math
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

ROWS = [json.loads(line) for line in gzip.decompress((Path(__file__).parents[1] /
    "fixtures/path-measurements-imagej154r.jsonl.gz").read_bytes()).splitlines()]


def pixels_and_density(row):
    kind = row.get("type", "uint8")
    pixels = np.arange(5)[:, None]*10 + np.arange(7)[None, :]
    pixels = (pixels*257).astype(np.uint16) if kind == "uint16" else (
        pixels*.25-8).astype(np.float32) if kind == "float32" else pixels.astype(np.uint8)
    density = ij.ImageJIntensityCalibration.uncalibrated()
    if row["density"] != "raw":
        density = ij.ImageJIntensityCalibration.function(kind,
            "straight_line" if row["density"] == "linear" else "poly2",
            [10, 2] if row["density"] == "linear" else [0, 0, 1], "value")
    return pixels, density


def compare(value, expected):
    if expected == "nan":
        assert math.isnan(value)
    else:
        assert value == pytest.approx(expected, rel=2e-14, abs=2e-14)


@pytest.mark.parametrize("row", ROWS)
def test_original_path_geometry_and_statistics(row):
    spatial = ij.SpatialCalibration(spacing=[*row["spacing"], 1], unit="um")
    points = np.array(row["points"]).reshape(-1, 2).tolist()
    if row["kind"] == "angle":
        compare(ij.imagej_calibrated_angle(points, spatial, row["reflex"]), row["columns"]["Angle"])
    elif row["kind"] == "line":
        assert ij.imagej_line_bounds(row["points"]) == row["bounds"]
    else:
        pixels, density = pixels_and_density(row)
        measured = ij.imagej_calibrated_polyline_measurement(pixels, spatial, density, points, row["interpolate"])
        values = ij.imagej_calibrated_irregular_profile(pixels, density, points, row["interpolate"])
        np.testing.assert_allclose(values, row["profile"], rtol=2e-14, atol=2e-14)
        assert measured["bounds"] == row["bounds"]
        compare(measured["length"], row["columns"]["Length"])
        for key, original in [("area", "Area"), ("mean", "Mean"), ("stddev", "StdDev"),
            ("minimum", "Min"), ("maximum", "Max"), ("median", "Median"), ("integrated_density", "IntDen")]:
            compare(measured[key], row["columns"][original])
        assert measured["pixel_count"] == len(row["profile"])


@pytest.mark.parametrize("reflex", [False, True])
def test_captured_angle_uses_no_pixels_and_freezes_options(reflex):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((5, 7), np.uint8), axes="YX",
        calibration=ij.SpatialCalibration(spacing=[.5, 2, 1], unit="um")).observe_reads(reads))
    owner.select(ij.Selection("angle", [(0, 0), (2, 1), (4, 0)]))
    app = ij.Application();app.adopt(owner)
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    options.reflex_angle = reflex
    limits = ij.TargetAnalysisLimits();limits.max_pixels = 1
    n = reads.counts().plane_reads
    command = ij.resolve_target_analysis(app.resolve_target(), options, limits)
    assert command.estimate.plane_count == command.estimate.total_pixels == 0
    assert command.record["version"] == 4
    options.reflex_angle = not reflex
    row = ij.execute_target_analysis(command).planes[0]
    assert reads.counts().plane_reads == n and row.bounds == [0, 0, 5, 2]
    assert row.angle_degrees == pytest.approx(306.86989764584405 if reflex else 53.13010235415598)
    assert row.statistics["pixel_count"] == 0 and row.statistics["mean"] == 0 and row.length is None


def test_polyline_work_admission_and_batch_budget_precede_reads():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((5, 7), np.uint8), axes="YX").observe_reads(reads))
    owner.select(ij.Selection("polyline", [(0, 0), (2, 0), (2, 2)]))
    app = ij.Application();app.adopt(owner)
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    limits = ij.TargetAnalysisLimits();limits.max_measurement_work = 399
    n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="work limit"):
        ij.resolve_target_analysis(app.resolve_target(), options, limits)
    assert reads.counts().plane_reads == n
    limits.max_measurement_work = 400
    command = ij.resolve_target_analysis(app.resolve_target(), options, limits)
    assert ij.execute_target_analysis(command).planes[0].statistics["pixel_count"] == 3
    assert reads.counts().plane_reads == n + 1


@pytest.mark.parametrize("points", [[[0, 0]], [[-1, 0], [2, 1]], [[0, 0], [99, 1]],
                                   [[0, 0], [float("nan"), 1]]])
def test_polyline_invalid_domain(points):
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polyline_measurement(np.zeros((5, 7), np.uint8),
            ij.SpatialCalibration(), ij.ImageJIntensityCalibration.uncalibrated(), points)


def test_mixed_manager_aggregate_work_and_ordered_geometry_only_groups():
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((2, 5, 7), np.uint8), axes="ZYX").observe_reads(reads))
    path = ij.Selection("polyline", [(0, 0), (2, 0), (2, 2)])
    angle = ij.Selection("angle", [(0, 0), (1, 1), (2, 0)])
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("angle", angle, z=1), ij.ROI("first", path, z=0),
                   ij.ROI("second", path, z=0)])
    owner.commit(edit)
    app = ij.Application();app.adopt(owner)
    target = app.resolve_target(ij.roi_measurement_target_request(owner.snapshot()))
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.ROI_MANAGER
    limits = ij.TargetAnalysisLimits();limits.max_measurement_work = 799
    n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="batch.*work limit"):
        ij.resolve_target_analysis(target, options, limits)
    assert reads.counts().plane_reads == n
    limits.max_measurement_work = 800
    command = ij.resolve_target_analysis(target, options, limits)
    assert command.estimate.row_count == 3 and command.estimate.plane_count == 1
    rows = ij.execute_target_analysis(command).planes
    assert reads.counts().plane_reads == n + 1
    assert [row.roi_index for row in rows] == [0, 1, 2]
    assert [row.position.z for row in rows] == [1, 0, 0]
    assert rows[0].angle_degrees == 90 and rows[1].length == rows[2].length == 4


def test_reflex_setting_default_validation_and_round_trip(tmp_path):
    from imagejxx import _desktop
    path = tmp_path / "preferences.json"
    editor = _desktop.Editor(str(path))
    assert not json.loads(editor.settings())["reflex_angle"]
    with pytest.raises((ValueError, TypeError)):
        editor.update_settings('{"reflex_angle":1}', True)
    editor.update_settings('{"reflex_angle":true}', True)
    reopened = _desktop.Editor(str(path))
    assert json.loads(reopened.settings())["reflex_angle"] is True
