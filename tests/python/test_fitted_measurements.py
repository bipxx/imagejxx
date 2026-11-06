"""Actual Fit Spline command composition, distinct from explicit fitSpline(7)."""
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/fitted-measurements-imagej154r.json").read_bytes())
CASES = [case for case in REFERENCE["cases"] if case["command"]]
KINDS = {2: "polygon", 3: "freehand", 4: "freehand", 6: "polyline", 7: "polyline"}
TYPES = {2: "polygon", 3: "freehand", 4: "traced", 6: "polyline", 7: "freeline"}


def source(case):
    value = ij.Selection(KINDS[case["type"]], case["input"],
        attributes={"imagejxx.polygon_type": TYPES[case["type"]]})
    return ij.with_selection_polygon_coordinates(value, case["integer"])


@pytest.mark.parametrize("case", CASES, ids=lambda case: case["id"])
def test_actual_fit_command_controls_samples_and_captured_metric(case):
    original = source(case)
    before = original.to_dict()
    fitted = ij.fit_spline_selection(original, 40, 40)
    state = ij.selection_spline_fit(fitted)
    np.testing.assert_allclose(state.controls.points, case["controls"], atol=1e-5, rtol=1e-7)
    np.testing.assert_allclose(fitted.points, case["samples"], atol=1e-5, rtol=1e-7)
    assert original.to_dict() == before
    assert not ij.selection_polygon_integer_coordinates(fitted)
    ij.validate_selection_spline_fit(fitted)
    spatial = ij.SpatialCalibration(spacing=[*case["spacing"], 1], unit="um")
    expected = case["length"]
    assert expected != "nan"
    count = len(state.controls.points)
    bound = 8192 + 16 * (count + max(count, len(fitted.points)))
    metric = ij.imagej_calibrated_polygon_length(fitted, spatial, max_working_bytes=bound)
    assert metric == pytest.approx(expected, abs=1e-7, rel=1e-7)
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(fitted, spatial, max_working_bytes=bound-1)

    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), dtype=np.uint8), axes="YX",
        calibration=spatial).observe_reads(reads))
    owner.select(fitted)
    app = ij.Application(); app.adopt(owner)
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    before_reads = reads.counts().plane_reads
    snapshot = owner.snapshot()
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == before_reads
    assert command.record["version"] == 8
    assert command.record["options"]["rows"][0]["spline_fit"] is True
    row = ij.execute_target_analysis(command).planes[0]
    assert row.perimeter == pytest.approx(expected, abs=1e-7, rel=1e-7)
    if fitted.kind == "polyline":
        assert row.length == pytest.approx(expected, abs=1e-7, rel=1e-7)
    else:
        assert row.length is None
    assert reads.counts().plane_reads == before_reads + 1
    assert owner.snapshot().shares_data(snapshot) and owner.undo_steps == 0
    assert ij.Selection.from_dict(fitted.to_dict()) == fitted


def test_fitted_metric_is_frozen_and_source_stays_editable():
    case = next(case for case in CASES if case["id"] == "shape2-type7-float-command-2.0-0.5")
    fitted = ij.fit_spline_selection(source(case), 40, 40)
    spatial = ij.SpatialCalibration(spacing=[2, .5, 1])
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), dtype=np.uint8), axes="YX", calibration=spatial))
    owner.select(fitted)
    app = ij.Application(); app.adopt(owner)
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    owner.select(ij.Selection("polyline", [[3, 3], [13, 13]]))
    assert ij.execute_target_analysis(command).planes[0].length == pytest.approx(case["length"], abs=1e-7, rel=1e-7)
    restored = ij.remove_spline_fit(fitted, 40, 40)
    assert ij.selection_spline_fit(restored) is None
    assert restored.points == ij.selection_spline_fit(fitted).controls.points


@pytest.mark.parametrize("coordinate", [float("nan"), float("inf"), 1e308])
def test_fitted_metric_refuses_nonfinite_or_non_float32_sample_coordinates(coordinate):
    case = next(case for case in CASES if case["id"] == "shape1-type4-float-command-2.0-0.5")
    fitted = ij.fit_spline_selection(source(case), 40, 40)
    points = fitted.points
    points[0][0] = coordinate
    malformed = ij.Selection(fitted.kind, points, attributes=fitted.attributes)
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(malformed, ij.SpatialCalibration())


@pytest.mark.parametrize("profile,prefix", list(enumerate([
    "shape1-type2-float", "shape1-type3-float", "shape1-type4-float", "shape1-type7-float",
    "shape2-type3-int", "shape2-type4-float", "shape2-type7-float", "shape0-type2-float",
], start=7)))
def test_physical_fit_measure_csv_workspace(tmp_path, profile, prefix):
    import csv
    from imagejxx import _desktop
    case = next(case for case in CASES if case["id"] == prefix + "-command-2.0-0.5")
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    directory = tmp_path / "fitted-measure"
    report = json.loads(editor._subtype_measurement_ui_case(str(directory), profile))
    assert report["fitted"] and report["fit_undo_steps"] == 1
    assert report["cancel_preserved"] and report["workspace_preserved"]
    assert report["source_unchanged_by_measure"] and report["additional_measure_reads"] == 1
    if profile == 9:
        assert report["stale_fit_checked"]
    assert report["rows"][0]["Perim."] == pytest.approx(case["length"], abs=1e-7, rel=1e-7)
    with (directory / "measure.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1
    assert float(rows[0]["Perim."]) == pytest.approx(case["length"], abs=1e-6, rel=1e-7)
