"""Pinned PolygonRoi constructor/type lengths; actual Analyzer values retained."""
import json
import math
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/subtype-measurements-imagej154r.json").read_bytes())
KINDS = {2: "polygon", 3: "freehand", 4: "freehand", 6: "polyline", 7: "polyline"}
TYPES = {2: "polygon", 3: "freehand", 4: "traced", 6: "polyline", 7: "freeline"}


def selection(case):
    return ij.Selection(KINDS[case["type"]], case["points"],
        attributes={"imagejxx.polygon_type": TYPES[case["type"]]})


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_actual_polygonroi_constructor_lengths(case):
    assert "error" not in case
    spatial = ij.SpatialCalibration(spacing=[*case["spacing"], 1])
    source = selection(case)
    value = ij.imagej_calibrated_polygon_length(source, spatial,
        integer_coordinates=case["integer"])
    if case["length"] == "nan":
        assert math.isnan(value)
    else:
        assert value == pytest.approx(case["length"], abs=1e-11, rel=1e-12)
    assert source.points == case["points"]
    assert len(case["rows"]) == 1
    measured = case["rows"][0]["Perim."]
    if case["selection_retained"]:
        assert measured == case["length"]
    else:
        # ImagePlus drops zero-extent area ROIs and Analyzer then measures the
        # whole 40x40 image. This is distinct from direct PolygonRoi.getLength.
        assert case["type"] in (2, 3, 4)
        assert case["length"] in (0, "nan") and measured == 160
        assert case["rows"][0]["Area"] == 1600


@pytest.mark.parametrize("integer", [False, True])
def test_exact_kernel_admission_and_origin_independence(integer):
    case = next(c for c in REFERENCE["cases"] if c["id"] ==
        f"shape0-type7-{'integer' if integer else 'float'}-2.0-0.5")
    spatial = ij.SpatialCalibration(spacing=[2, .5, 1], origin=[1234, -987, 42])
    source = selection(case)
    bound = 8192 + 16 * len(source.points)
    assert ij.imagej_calibrated_polygon_length(source, spatial,
        integer_coordinates=integer, max_working_bytes=bound) == pytest.approx(case["length"])
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(source, spatial,
            integer_coordinates=integer, max_working_bytes=bound-1)


@pytest.mark.parametrize("points", [[], [[math.nan, 3]], [[math.inf, 3]], [[1e308, 3]], [[3, 3]] * 65537])
def test_invalid_polygon_coordinates_and_count(points):
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(ij.Selection("polyline", points), ij.SpatialCalibration())


@pytest.mark.parametrize("point", [[3.5, 4], [2**31, 4], [-2**31-1, 4]])
def test_integer_constructor_requires_integral_int32_coordinates(point):
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(ij.Selection("polyline", [point]),
            ij.SpatialCalibration(), integer_coordinates=True)


@pytest.mark.parametrize("kind", ["line", "point", "rectangle", "ellipse"])
def test_other_roi_kinds_remain_with_their_existing_measurement_owners(kind):
    with pytest.raises(ValueError):
        ij.imagej_calibrated_polygon_length(ij.Selection(kind, [[3, 3], [13, 13]]), ij.SpatialCalibration())


def test_constructor_switch_is_strict_boolean():
    with pytest.raises(TypeError):
        ij.imagej_calibrated_polygon_length(ij.Selection("polyline", [[3, 3], [13, 13]]),
            ij.SpatialCalibration(), integer_coordinates=1)


def captured(case):
    source = ij.with_selection_polygon_coordinates(selection(case), case["integer"])
    pixels = (np.arange(40)[None, :] + 2*np.arange(40)[:, None]).astype(np.uint8)
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="YX", calibration=ij.SpatialCalibration(
        spacing=[*case["spacing"], 1], unit="um")).observe_reads(reads))
    owner.select(source)
    app = ij.Application(); app.adopt(owner)
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    before = reads.counts().plane_reads
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == before
    return app, owner, reads, command


def same_number(actual, expected):
    if expected == "nan":
        assert math.isnan(actual)
    else:
        assert actual == pytest.approx(expected, abs=1e-7, rel=1e-7)


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_captured_constructor_metric_and_original_statistics(case):
    _, owner, reads, command = captured(case)
    original = owner.snapshot(); before = reads.counts().plane_reads
    result = ij.execute_target_analysis(command)
    row = result.planes[0]
    same_number(row.perimeter, case["length"])
    if case["type"] in (6, 7):
        same_number(row.length, case["length"])
    else:
        assert row.length is None
    if case["selection_retained"]:
        same_number(row.statistics["area"], case["rows"][0]["Area"])
        same_number(row.statistics["mean"], case["rows"][0]["Mean"])
        same_number(row.statistics["integrated_density"], case["rows"][0]["IntDen"])
    else:
        assert row.statistics["area"] == row.statistics["pixel_count"] == 0
        assert math.isnan(row.statistics["mean"])
    assert command.record["version"] == (7 if case["type"] in (6, 7) else 11)
    assert command.record["options"]["rows"][0]["integer_coordinates"] == case["integer"]
    assert reads.counts().plane_reads == before+1
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0


@pytest.mark.parametrize("code", [2, 3, 4, 6, 7])
def test_constructor_state_session_wire_interchange_and_edit(code):
    case = next(c for c in REFERENCE["cases"] if c["id"] == f"shape0-type{code}-integer-1.0-1.0")
    source = ij.with_selection_polygon_coordinates(selection(case), True)
    assert ij.selection_polygon_integer_coordinates(source)
    assert ij.Selection.from_dict(source.to_dict()) == source
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), np.uint8), axes="YX"))
    owner.select(source)
    decoded = ij.decode_session(ij.encode_session(ij.SessionCapture([owner.snapshot()])))
    assert decoded.documents[0].snapshot().selection == source
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(owner.snapshot(), ij.SelectionCreationRequest(source))
    request = ij.SelectionCreationRequest(source, ij.SelectionCreationOperation.REPLACE,
        ij.SelectionCreationGeometry.NATIVE_PATH_V4)
    command = ij.resolve_selection_creation_command(owner.snapshot(), request)
    ij.validate_selection_creation_command_record(command.record())
    record = ij.decode_imagej_roi(ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI("integer", source))))
    assert not record.options & 128
    assert ij.selection_polygon_integer_coordinates(record.roi.selection)
    assert record.roi.selection.points == source.points
    assert ij.selection_polygon_type(record.roi.selection) == ij.selection_polygon_type(source)
    same_number(ij.imagej_calibrated_polygon_length(record.roi.selection, ij.SpatialCalibration(),
        integer_coordinates=True), case["length"])
    editor = ij.SelectionEditor(source, (40, 40))
    assert ij.selection_polygon_integer_coordinates(editor.moved([1, 2]))
    fractional = editor.moved_vertex(0, [3.25, 3])
    assert not ij.selection_polygon_integer_coordinates(fractional)
    assert fractional.points[0] == [3.25, 3]
    sampled = ij.interpolate_selection(source, 40, 40)
    assert not ij.selection_polygon_integer_coordinates(sampled)
    if code in (2, 6, 7):
        fitted = ij.fit_spline_selection(source, 40, 40)
        assert not ij.selection_polygon_integer_coordinates(fitted)


@pytest.mark.parametrize("attributes", [
    {"imagejxx.polygon_integer_coordinates": True},
    {"imagejxx.polygon_type": "polygon", "imagejxx.polygon_integer_coordinates": False},
    {"imagejxx.polygon_type": "polygon", "imagejxx.polygon_integer_coordinates": 1},
    {"imagejxx.polygon_type": "polygon", "imagejxx.polygon_integer_coordinates": "true"},
])
def test_constructor_state_is_owned_and_canonical(attributes):
    source = ij.Selection("polygon", [[3, 3], [13, 3], [13, 13]], attributes=attributes)
    with pytest.raises(ValueError):
        ij.selection_polygon_integer_coordinates(source)


def test_integer_constructor_setter_rejects_fractional_and_non_boolean():
    source = ij.Selection("polygon", [[3.25, 3], [13, 3], [13, 13]])
    with pytest.raises(ValueError):
        ij.with_selection_polygon_coordinates(source, True)
    with pytest.raises(TypeError):
        ij.with_selection_polygon_coordinates(source, 1)


def test_large_area_metric_keeps_existing_native_vertex_extent():
    square = [[3, 3], [13, 3], [13, 13], [3, 13]]
    source = ij.Selection("polygon", square*1250)
    spatial = ij.SpatialCalibration(spacing=[2, .5, 1])
    expected = 1250*ij.imagej_calibrated_polygon_length(ij.Selection("polygon", square), spatial)
    assert ij.imagej_calibrated_polygon_length(source, spatial) == pytest.approx(expected)


def test_captured_constructor_freeze_and_geometry_admission():
    case = next(c for c in REFERENCE["cases"] if c["id"] == "shape0-type4-integer-1.0-1.0")
    app, owner, reads, command = captured(case)
    owner.select(ij.with_selection_polygon_coordinates(owner.snapshot().selection, False))
    result = ij.execute_target_analysis(command)
    same_number(result.planes[0].perimeter, case["length"])
    assert not ij.selection_polygon_integer_coordinates(owner.snapshot().selection)
    before = reads.counts().plane_reads
    limits = ij.TargetAnalysisLimits(); limits.max_measurement_work = 3
    with pytest.raises(ValueError):
        ij.resolve_target_analysis(app.resolve_target(), command.options, limits)
    assert reads.counts().plane_reads == before
    with pytest.raises(ValueError):
        ij.execute_target_analysis(command, runtime_max_working_bytes=command.estimate.working_bytes-1)
    assert reads.counts().plane_reads == before


@pytest.mark.parametrize("profile", range(7))
def test_physical_subtype_measure_csv_and_workspace(tmp_path, profile):
    import csv
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    directory = tmp_path / "measure-ui"
    report = json.loads(editor._subtype_measurement_ui_case(str(directory), profile))
    assert report == json.loads((directory / "case.json").read_bytes())
    assert report["context"] == "imgui_python"
    assert report["columns"] == ["ROI", "Area", "Length", "Perim.", "Unit"]
    assert report["cancel_preserved"] and report["workspace_preserved"]
    assert report["source_unchanged_by_measure"] and report["additional_measure_reads"] == 1
    with (directory / "measure.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1 and rows[0]["Unit"] == "um"
    expected = report["rows"][0]["Perim."]
    if expected == "nan":
        assert rows[0]["Perim."].lower() == "nan"
    else:
        assert float(rows[0]["Perim."]) == pytest.approx(expected, abs=1e-6)


def test_zero_length_open_path_keeps_original_one_step_quota():
    owner = ij.Document(ij.Dataset(np.zeros((5, 7), np.uint8), axes="YX"))
    owner.select(ij.Selection("polyline", [[3, 3], [3, 3]]))
    app = ij.Application(); app.adopt(owner)
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    limits = ij.TargetAnalysisLimits(); limits.max_measurement_work = 1
    command = ij.resolve_target_analysis(app.resolve_target(), options, limits)
    row = ij.execute_target_analysis(command).planes[0]
    assert row.length == row.perimeter == 0
