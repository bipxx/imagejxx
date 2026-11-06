"""Pinned PolygonRoi constructor/type lengths; actual Analyzer values retained."""
import json
import math
from pathlib import Path

import imagejxx as ij
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


@pytest.mark.parametrize("points", [[], [[math.nan, 3]], [[math.inf, 3]], [[1e308, 3]], [[3, 3]] * 4097])
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
