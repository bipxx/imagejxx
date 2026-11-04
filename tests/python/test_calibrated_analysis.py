"""Original scalar statistics/profile references; UI publication is a separate gate."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REVIEW = Path(__file__).resolve().parents[2] / "docs/parity/reviews"


def references(number):
    return [json.loads(row) for row in gzip.decompress(
        (REVIEW / f"G034-calibrated-analysis-reference{number:03}.jsonl.gz").read_bytes()).splitlines()]


def density(mode, kind):
    if mode == "raw":
        return ij.ImageJIntensityCalibration.uncalibrated()
    if mode == "signed16":
        return ij.ImageJIntensityCalibration.signed16()
    if mode in ("linear", "decreasing"):
        return ij.ImageJIntensityCalibration.function(kind, "straight_line", [10, 2 if mode == "linear" else -2], "OD")
    table = np.arange(65536 if kind == "uint16" else 256, dtype=np.float64) ** 2
    table[:5] = [4, 1, 9, 0, 2]
    if mode == "nan":
        table[1] = np.nan
    if mode == "nan_gap":
        table[2] = np.nan
    if mode == "infinite":
        table[1] = np.inf
    if mode == "all_nan":
        table[:] = np.nan
    words = table.astype(np.float32).view(np.uint32)
    return ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration", version=1,
        mode="lookup", value_unit="table", bit_depth=16 if kind == "uint16" else 8,
        table_f32_hex="".join(f"{int(word):08x}" for word in words), source_parameters={}, signed16=False))


def compare(actual, expected):
    for key in ("pixel_count", "mode_index", "area", "mean", "raw_mean", "stddev", "minimum", "maximum", "median", "mode_value", "integrated_density"):
        value = expected[key]
        if value == "nan":
            assert np.isnan(actual[key]), key
        elif value in ("inf", "-inf"):
            assert actual[key] == float(value), key
        else:
            assert actual[key] == pytest.approx(value, rel=2e-14, abs=2e-14), key
    for key in ("histogram", "histogram16"):
        h = actual[key]
        assert h.dtype == np.uint64
        sparse = [[int(i), int(h[i])] for i in np.flatnonzero(h)]
        assert sparse == (expected[key] or []), key


@pytest.mark.parametrize("row", references(1), ids=lambda r: r["id"])
def test_original_analysis(row):
    kind = row["type"]
    values = np.array([[0, 1, 2, 3, 4], [4, 3, 2, 1, 0]] if row["kind"] == "profile" else
                      [[0, 1, 1, 3], [2, 3, 4, 4]], dtype=kind)
    owner = ij.Document(ij.Dataset(values, axes="YX", calibration=ij.SpatialCalibration(spacing=[3, 4, 1], unit="local")))
    app = ij.Application()
    app.adopt(owner)
    calibrated = density(row["mode"], "uint8" if kind == "float32" else kind)
    spatial = ij.SpatialCalibration(spacing=[.5, 1.25, 1], unit="um")
    if row.get("global", True):
        app.set_global_calibration(ij.GlobalCalibration(spatial, calibrated, "uint8" if kind == "float32" else kind))
        app.set_ignore_global_calibration(row.get("ignore", False), owner.id)
    else:
        edit = owner.begin_edit()
        edit.set_calibration(spatial)
        owner.commit(edit)
        ij.set_imagej_intensity_calibration(owner, calibrated)
    target = app.resolve_target(ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    original = values.copy()
    if row["kind"] == "profile":
        result = ij.imagej_calibrated_line_profile(values, target.intensity_calibration(), 0, 0, 4,
            1 if row["diagonal"] else 0, row["interpolate"])
        np.testing.assert_allclose(result, row["values"], rtol=2e-14, atol=2e-14)
    else:
        mask = None
        if row["roi"]:
            mask = np.zeros_like(values)
            mask[:, 1:3] = 255
        result = ij.imagej_calibrated_statistics(values, target.calibration, target.intensity_calibration(), mask)
        compare(result, row)
    np.testing.assert_array_equal(values, original)


@pytest.mark.parametrize("row", references(2), ids=lambda r: r["id"])
def test_original_edges(row):
    kind = row["type"]
    a = np.array([[0, 1, 1, 3], [2, 3, 4, 4]], dtype=kind)
    mode = row["membership"]
    mask = None
    if mode in ("empty", "single"):
        mask = np.zeros_like(a)
        if mode == "single":
            mask[0, 1] = 255
    elif mode == "constant":
        a[:] = 5
    elif mode == "gap":
        a[1, 0] = 3
    elif mode == "signed":
        a[:] = [0, 32767, 32768, 65535]
    result = ij.imagej_calibrated_statistics(a, ij.SpatialCalibration(spacing=[.5, 1.25, 1]), density(row["mode"], kind), mask)
    compare(result, row)


@pytest.mark.parametrize("row", references(3), ids=lambda r: r["id"])
def test_function_median_precision(row):
    a = np.array([[0, 1, 1, 3], [2, 3, 4, 4]], dtype=row["type"])
    p = [.123456789, .987654321] if row["mode"] == "line" else [.123456789, .987654321, .111111111] if row["mode"] == "poly" else [-10, 1]
    c = ij.ImageJIntensityCalibration.function(row["type"], "poly2" if row["mode"] == "poly" else "straight_line", p, "OD", zero_clip=row["mode"] == "clip")
    result = ij.imagej_calibrated_statistics(a, ij.SpatialCalibration(), c)
    for key in ("mean", "stddev", "minimum", "maximum", "median"):
        assert result[key] == pytest.approx(row[key], rel=2e-14, abs=2e-14), key


def test_frozen_context_and_detached_histograms():
    a = np.array([[0, 1], [3, 4]], dtype=np.uint8)
    owner = ij.Document(ij.Dataset(a, axes="YX"))
    app = ij.Application()
    app.adopt(owner)
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(spacing=[.5, 1.25, 1]), density("linear", "uint8"), "uint8"))
    target = app.resolve_target(ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    app.clear_global_calibration()
    with pytest.raises(ij.RevisionConflict):
        app.validate_target(target)
    first = ij.imagej_calibrated_statistics(a, target.calibration, target.intensity_calibration())
    assert first["mean"] == 14 and first["area"] == 2.5
    first["histogram"][:] = 0
    second = ij.imagej_calibrated_statistics(a, target.calibration, target.intensity_calibration())
    assert second["histogram"].sum() == 4
    assert owner.undo_steps == 0


def test_malformed_recognized_function_provenance_refuses():
    record = density("lookup", "uint8").record
    record["source_parameters"] = {"producer": "imagej154r.function", "version": 1}
    c = ij.ImageJIntensityCalibration.from_record(record)
    with pytest.raises(ValueError, match="provenance"):
        ij.imagej_calibrated_statistics(np.array([[0, 1]], dtype=np.uint8), ij.SpatialCalibration(), c)
    assert c.record == record


def test_future_table_provenance_stays_opaque():
    record = density("lookup", "uint8").record
    record["source_parameters"] = {"producer": "imagej154r.function", "version": 2}
    c = ij.ImageJIntensityCalibration.from_record(record)
    result = ij.imagej_calibrated_statistics(np.array([[0, 1]], dtype=np.uint8), ij.SpatialCalibration(), c)
    assert result["median"] == 1 and result["mean"] == 2.5
    assert c.record == record


@pytest.mark.parametrize("kind", ["uint8", "uint16", "float32"])
def test_admission_and_profile_domain(kind):
    a = np.arange(8, dtype=kind).reshape(2, 4)
    c = density("raw", kind)
    with pytest.raises(ValueError, match="working limit"):
        ij.imagej_calibrated_statistics(a, ij.SpatialCalibration(), c, max_working_bytes=0)
    with pytest.raises(ValueError, match="working limit"):
        ij.imagej_calibrated_line_profile(a, c, 0, 0, 3, 1, max_working_bytes=0)
    with pytest.raises(ValueError, match="inside"):
        ij.imagej_calibrated_line_profile(a, c, -1, 0, 3, 1)
    with pytest.raises(ValueError, match="mask"):
        ij.imagej_calibrated_statistics(a, ij.SpatialCalibration(), c, np.zeros((1, 1), dtype=kind))
    assert ij.imagej_calibrated_line_profile(a, c, 2, 0, 2, 0).tolist() == [2]
