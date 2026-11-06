"""Pinned original lifecycle commands; direct count is limited to segmented ROIs."""
import json
import math
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from test_fitted_measurements import REFERENCE as INITIAL_REFERENCE, source


REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/spline-lifecycle-imagej154r.json").read_bytes())


@pytest.mark.parametrize("case", [case for case in INITIAL_REFERENCE["cases"]
    if not case["command"] and case["type"] in (2, 6)], ids=lambda case: case["id"])
def test_initial_segmented_explicit_count_reuses_original_observations(case):
    original = source(case)
    options = ij.SplineFitOptions(); options.evaluation_points = 7
    fitted = ij.fit_spline_selection(original, 40, 40, options)
    np.testing.assert_allclose(fitted.points, case["samples"], atol=1e-5, rtol=1e-7)
    np.testing.assert_allclose(ij.selection_spline_fit(fitted).controls.points,
        case["controls"], atol=1e-5, rtol=1e-7)
    calibration = ij.SpatialCalibration(spacing=[*case["spacing"], 1])
    assert ij.imagej_calibrated_polygon_length(fitted, calibration) == pytest.approx(
        case["length"], abs=1e-7, rel=1e-7)


def lifecycle(original, case):
    options = ij.SplineFitOptions()
    if case["mode"] == "straighten":
        options.mode = ij.SplineFitMode.STRAIGHTEN
        return ij.fit_spline_selection(original, 512, 512, options)
    fitted = ij.fit_spline_selection(original, 512, 512)
    if case["mode"] == "remove":
        return ij.remove_spline_fit(fitted, 512, 512)
    if case["mode"] == "fit-straighten":
        options.mode = ij.SplineFitMode.STRAIGHTEN
    elif case["mode"] == "refit7":
        assert case["type"] in (2, 6)
        options.evaluation_points = 7
    else:
        assert case["mode"] == "refit"
    return ij.fit_spline_selection(fitted, 512, 512, options)


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_original_lifecycle_geometry_and_metric(case):
    original = source(case)
    before = original.to_dict()
    if "error" in case:
        with pytest.raises((ValueError, RuntimeError)):
            lifecycle(original, case)
    else:
        result = lifecycle(original, case)
        fit = ij.selection_spline_fit(result)
        assert bool(fit) == case["fitted"]
        controls = fit.controls.points if fit else result.points
        np.testing.assert_allclose(controls, case["controls"], atol=1e-5, rtol=1e-7)
        np.testing.assert_allclose(result.points, case["samples"], atol=1e-5, rtol=1e-7)
        calibration = ij.SpatialCalibration(spacing=[*case["spacing"], 1])
        measured = ij.imagej_calibrated_polygon_length(result, calibration,
            integer_coordinates=ij.selection_polygon_integer_coordinates(result))
        if case["length"] == "nan":
            assert math.isnan(measured)
        else:
            assert measured == pytest.approx(case["length"], abs=1e-7, rel=1e-7)
        if fit:
            ij.validate_selection_spline_fit(result)
        assert ij.Selection.from_dict(result.to_dict()) == result
    assert original.to_dict() == before


def test_new_basis_preserves_legacy_v1_reader():
    fitted = ij.fit_spline_selection(ij.Selection("polygon", [[3, 3], [13, 3], [13, 13], [3, 13]]), 40, 40)
    attrs = fitted.attributes
    record = attrs["imagejxx.spline_fit"]
    assert record["version"] == 2
    record["version"] = 1
    del record["origin"], record["relative_controls"]
    legacy = fitted.with_attributes(attrs)
    ij.validate_selection_spline_fit(legacy)
    assert ij.selection_spline_fit(legacy).controls.points == [[3, 3], [13, 3], [13, 13], [3, 13]]
    assert ij.Selection.from_dict(legacy.to_dict()) == legacy


@pytest.mark.parametrize("defect", ["origin_nan", "origin_overflow", "origin_count", "relative_count", "relative_nan", "inconsistent", "extra"])
def test_malformed_basis_is_rejected_before_work(defect):
    fitted = ij.fit_spline_selection(ij.Selection("polygon", [[3, 3], [13, 3], [13, 13]]), 40, 40)
    attrs = fitted.attributes
    record = attrs["imagejxx.spline_fit"]
    assert record["version"] == 2
    if defect == "origin_nan":
        record["origin"][0] = float("nan")
    elif defect == "origin_overflow":
        record["origin"][0] = 1e308
    elif defect == "origin_count":
        record["origin"] = [3]
    elif defect == "relative_count":
        record["relative_controls"].pop()
    elif defect == "relative_nan":
        record["relative_controls"][0][0] = float("nan")
    elif defect == "inconsistent":
        record["relative_controls"][0][0] += 1
    else:
        record["unexpected"] = True
    with pytest.raises(ValueError):
        ij.selection_spline_fit(fitted.with_attributes(attrs))
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(fitted.with_attributes(attrs), ij.SpatialCalibration())


@pytest.mark.parametrize("profile,prefix", list(enumerate([
    "shape2-type2-float-refit", "shape3-type2-float-refit",
    "shape0-type6-float-fit-straighten", "shape0-type3-float-remove",
    "shape2-type2-float-remove", "shape1-type2-float-fit-straighten",
    "shape0-type4-float-remove",
], start=15)))
def test_physical_lifecycle_measure_csv_workspace(tmp_path, profile, prefix):
    import csv
    from imagejxx import _desktop
    case = next(case for case in REFERENCE["cases"] if case["id"] == prefix + "-2.0-0.5")
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    directory = tmp_path / "lifecycle"
    report = json.loads(editor._subtype_measurement_ui_case(str(directory), profile))
    assert report["lifecycle"] and report["fit_undo_steps"] == 2
    assert report["fitted"] == case["fitted"]
    assert report["cancel_preserved"] and report["workspace_preserved"]
    assert report["source_unchanged_by_measure"] and report["additional_measure_reads"] == 1
    if profile == 15:
        assert report["stale_fit_checked"]
    assert report["rows"][0]["Perim."] == pytest.approx(case["length"], abs=1e-7, rel=1e-7)
    with (directory / "measure.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1
    assert float(rows[0]["Perim."]) == pytest.approx(case["length"], abs=1e-6, rel=1e-7)


def test_python_host_rejected_straighten_keeps_source_history_and_memory(tmp_path):
    from imagejxx import _desktop
    case = next(case for case in REFERENCE["cases"] if case["id"] == "shape1-type3-float-fit-straighten-2.0-0.5")
    assert "error" in case
    fitted = ij.fit_spline_selection(source(case), 40, 40)
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), np.uint8), axes="YX"))
    owner.select(fitted)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    before = owner.snapshot(); steps = owner.undo_steps
    working = json.loads(editor.render_memory_info())["working_bytes"]
    editor.action("fit_spline_selection", json.dumps({"mode": "straighten"})); editor.wait()
    assert editor.error
    assert owner.snapshot().shares_data(before) and owner.undo_steps == steps
    assert json.loads(editor.render_memory_info())["working_bytes"] == working
    editor.clear_error()
