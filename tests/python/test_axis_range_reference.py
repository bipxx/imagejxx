"""Captured48 integer-range pixels and retained12 singleton parity.

Full native affine/time/stride is separately owned. ImageJ selected-stack
calibration copies its contiguous calibration without native stride rebasing.
No ImageJ GUI, reference process or expected-output generation runs here.
"""

import hashlib
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


KINDS = ("u8", "u16", "f32", "rgb")
PLANES = ("xy", "xz", "yz")
PROFILES = ("lower", "full", "interior_stride", "upper_stride")
CASE_IDS = tuple(f"{kind}.{plane}.{profile}" for kind in KINDS
                 for plane in PLANES for profile in PROFILES)
SINGLETON_IDS = (
    "u8.xy.lower", "u8.xz.lower", "u8.yz.lower",
    "u16.xy.interior", "u16.xz.interior", "u16.yz.interior",
    "f32.xy.upper", "f32.xz.upper", "f32.yz.upper",
    "rgb.xy.interior", "rgb.xz.interior", "rgb.yz.interior",
)
CORPUS_PIN = (7_876_991, "8ddde981534e0ccf55840316485c29c3dbcf61eef03dacee291b83477b4a8347")
FORMULA_PIN = (15_181, "b88c4b86a14bf16f8db84adbfbe99dc1aee454f6ae739d123e4c897cdf79ae79")
SINGLETON_PIN = (1_307_528, "05cb09b95b2dcfd2e1e492150d3883497c0bb5a203c8dde3b5b5b1830f3b5d08")
AXES = {"xy": (0, 1, 2), "xz": (0, 2, 1), "yz": (1, 2, 0)}
EXTENTS = (32, 17, 9)
ORIGIN = (-2.0, 4.0, 9.0)
DIRECTION = ((-1.0, .5, .25), (0.0, 1.0, .5), (.5, 0.0, 1.0))
FORMATS = {"u8": (0, 8, "UInt8"), "u16": (1, 16, "UInt16_le"),
           "f32": (2, 32, "Float32_le"), "rgb": (4, 24, "RGB8_interleaved")}


def _pairs(items):
    result = {}
    for key, value in items:
        assert key not in result
        result[key] = value
    return result


def _nonfinite(value):
    raise AssertionError(f"Nonfinite reference metadata: {value}")


def _load(path, identity):
    assert path.is_file() and not path.is_symlink()
    assert path.stat().st_size == identity[0] <= 16 * 1024**2
    data = path.read_bytes()
    assert (len(data), hashlib.sha256(data).hexdigest()) == identity
    return json.loads(data, object_pairs_hook=_pairs, parse_constant=_nonfinite)


@pytest.fixture(scope="module")
def references():
    roots = [parent for parent in tuple(Path(__file__).resolve().parents)[:8]
             if (parent / "tests/fixtures/axis-plane-imagej154r.json").is_file()]
    assert len(roots) == 1
    root = roots[0]
    preparation = "build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001"
    def location(name, historical):
        configured = os.environ.get(name, historical)
        assert configured, f"Empty original reference path: {name}"
        return root / configured
    corpus_path = location("IMAGEJXX_AXIS_RANGE_REFERENCE", preparation + "/capture-001/corpus.json")
    corpus = _load(corpus_path, CORPUS_PIN)
    formula = _load(location("IMAGEJXX_AXIS_RANGE_FORMULA_REFERENCE",
                             preparation + "/formula-verification-001.json"), FORMULA_PIN)
    singleton = _load(root / "tests/fixtures/axis-plane-imagej154r.json", SINGLETON_PIN)
    assert corpus["schema_version"] == 1 and corpus["imagej_version"] == "1.54r"
    assert corpus["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert tuple(corpus["exact_ordered_case_ids"]) == CASE_IDS
    assert tuple(row["case_id"] for row in corpus["cases"]) == CASE_IDS
    assert formula["state"] == "passed_independent48_reference_formula_verification"
    assert formula["observed_cases"] == 48 and formula["target_product_invoked"] is False
    # This is the original capture identity, independent of its transport path.
    assert formula["corpus"] == {"path": preparation + "/capture-001/corpus.json",
                                 "sha256": CORPUS_PIN[1], "bytes": CORPUS_PIN[0]}
    assert tuple(row["case_id"] for row in formula["cases"]) == CASE_IDS
    assert tuple(singleton["exact_ordered_case_ids"]) == SINGLETON_IDS
    assert tuple(row["case_id"] for row in singleton["cases"]) == SINGLETON_IDS
    return ({row["case_id"]: row for row in corpus["cases"]},
            {row["case_id"]: row for row in singleton["cases"]})


def literal_source(kind):
    z, y, x = np.indices((9, 17, 32), dtype=np.int64)
    if kind == "u8":
        return ((37 * x + 61 * y + 13 * x * y + 43 * z) % 251).astype(np.uint8)
    if kind == "u16":
        return (6000 * z + 32 * y + x).astype(np.uint16)
    if kind == "f32":
        return (10000 * z + 100 * y + x + .25).astype(np.float32)
    assert kind == "rgb"
    return np.stack(((17 * x + 31 * y + 47 * z) % 256,
                     (71 * x + 11 * y + 29 * z) % 256,
                     (13 * x + 61 * y + 23 * z) % 256), axis=-1).astype(np.uint8)


def raw(array):
    value = np.asarray(array)
    return np.ascontiguousarray(value.astype(value.dtype.newbyteorder("<"))).tobytes()


def calibration_signature(cal):
    return (tuple(cal.spacing), tuple(cal.origin), tuple(cal.direction), cal.unit,
            cal.time_unit, cal.frame_interval, cal.time_origin)


def source(kind):
    pixels = literal_source(kind)
    spacing = (1.0, 1.0, 1.0) if kind in ("u8", "u16") else (.4, 1.2, 3.0)
    cal = ij.SpatialCalibration(spacing=spacing, origin=ORIGIN,
                               direction=tuple(value for row in DIRECTION for value in row),
                               unit="um", time_unit="min", frame_interval=2.5, time_origin=-3.0)
    dataset = ij.Dataset(pixels, axes="ZYXS" if kind == "rgb" else "ZYX", calibration=cal)
    return pixels, dataset, spacing


def owned_geometry(result, spacing, plane, first, step, count):
    order = AXES[plane]
    observed = result.dataset.calibration
    expected_spacing = [spacing[axis] * (step if index == 2 else 1)
                        for index, axis in enumerate(order)]
    expected_direction = [DIRECTION[row][axis] for row in range(3) for axis in order]
    expected_origin = [ORIGIN[row] + DIRECTION[row][order[2]] * spacing[order[2]] * first
                       for row in range(3)]
    np.testing.assert_array_equal(observed.spacing, expected_spacing)
    np.testing.assert_array_equal(observed.direction, expected_direction)
    np.testing.assert_allclose(observed.origin, expected_origin, rtol=0, atol=1e-12)
    assert (observed.unit, observed.time_unit, observed.frame_interval,
            observed.time_origin) == ("um", "min", 2.5, -3.0)
    geometry = result.first_plane_geometry
    np.testing.assert_allclose(geometry["origin"], expected_origin, rtol=0, atol=1e-12)
    np.testing.assert_array_equal(geometry["horizontal_step"],
                                 [DIRECTION[row][order[0]] * spacing[order[0]] for row in range(3)])
    np.testing.assert_array_equal(geometry["vertical_step"],
                                 [DIRECTION[row][order[1]] * spacing[order[1]] for row in range(3)])
    assert (geometry["horizontal_axis"], geometry["vertical_axis"], geometry["unit"]) == (
        "XYZ"[order[0]], "XYZ"[order[1]], "um")
    for k in range(count):
        for v in (0, EXTENTS[order[1]] - 1):
            for u in (0, EXTENTS[order[0]] - 1):
                xyz = [0, 0, 0]
                xyz[order[0]], xyz[order[1]], xyz[order[2]] = u, v, first + k * step
                expected_world = [ORIGIN[row] + sum(DIRECTION[row][axis] * spacing[axis] * xyz[axis]
                                                   for axis in range(3)) for row in range(3)]
                np.testing.assert_allclose(observed.voxel_to_world((u, v, k)), expected_world,
                                           rtol=0, atol=1e-12)


@pytest.mark.parametrize("case_id", CASE_IDS)
def test_reference48_exact_typed_range_pixels_and_owned_geometry(references, case_id):
    corpus, _ = references
    row = corpus[case_id]
    kind, plane, profile = case_id.split(".")
    assert (row["dtype"], row["plane"], row["profile"]) == (kind, plane, profile)
    pixels, dataset, spacing = source(kind)
    before = raw(dataset.numpy("ZYXS"))
    before_cal = calibration_signature(dataset.calibration)
    before_shape, before_policy = tuple(dataset.shape), dataset.sample_policy
    assert before.hex() == row["source_before"]["raw_hex"] == row["source_after"]["raw_hex"]
    first, last, step = row["first_zero_based"], row["last_inclusive_zero_based"], row["positive_step"]
    coordinates = tuple(range(first, last + 1, step))
    assert coordinates == tuple(row["source_coordinates_zero_based"])
    assert len(coordinates) == row["requested_plane_count"]
    result = ij.reslice_axis_range(dataset, ij.AxisRangeResliceRequest(plane, first, last, step))
    expected = row["output"]
    samples = 3 if kind == "rgb" else 1
    actual = result.dataset.numpy("ZYXS")
    assert actual.dtype == pixels.dtype
    assert tuple(result.dataset.shape) == (1, 1, len(coordinates), expected["height"], expected["width"], samples)
    assert expected["stack_size"] == len(coordinates)
    assert (expected["imagej_type"], expected["bit_depth"], expected["encoding"]) == FORMATS[kind]
    assert raw(actual).hex() == expected["raw_hex"]
    assert result.dataset.sample_policy == before_policy and not result.dataset.shares_pixels(dataset)
    assert (result.request.plane, result.request.first, result.request.last, result.request.step,
            result.request.t, result.request.c) == (plane, first, last, step, 0, 0)
    owned_geometry(result, spacing, plane, first, step, len(coordinates))
    # Only in-plane spacing agrees with this reference profile. ImageJ copied
    # selected-stack calibration unchanged; native fixed-axis spacing uses step.
    assert tuple(result.dataset.calibration.spacing[:2]) == tuple(expected["calibration"]["spacing"][:2])
    assert raw(dataset.numpy("ZYXS")) == before and tuple(dataset.shape) == before_shape
    assert dataset.sample_policy == before_policy and calibration_signature(dataset.calibration) == before_cal


@pytest.mark.parametrize("case_id", SINGLETON_IDS)
def test_retained12_singletons_match_reference_and_plane_export(references, case_id):
    _, singleton = references
    row = singleton[case_id]
    kind, plane, _ = case_id.split(".")
    pixels, dataset, spacing = source(kind)
    before = raw(dataset.numpy("ZYXS"))
    before_cal = calibration_signature(dataset.calibration)
    coordinate = row["coordinate_zero_based"]
    result = ij.reslice_axis_range(dataset, ij.AxisRangeResliceRequest(plane, coordinate, coordinate, 1))
    single = ij.export_axis_plane(dataset, ij.AxisPlaneExportRequest(plane, coordinate))
    expected = row["output"]
    assert raw(pixels).hex() == row["source_before"]["raw_hex"] == row["source_after"]["raw_hex"]
    assert result.dataset.shape == single.dataset.shape == (
        1, 1, 1, expected["height"], expected["width"], 3 if kind == "rgb" else 1)
    actual = result.dataset.numpy("ZYXS")
    assert actual.dtype == pixels.dtype
    assert raw(actual).hex() == raw(single.dataset.numpy("ZYXS")).hex() == expected["raw_hex"]
    assert result.dataset.sample_policy == single.dataset.sample_policy == dataset.sample_policy
    assert result.working_bytes == single.working_bytes
    assert calibration_signature(result.dataset.calibration) == calibration_signature(single.dataset.calibration)
    owned_geometry(result, spacing, plane, coordinate, 1, 1)
    assert raw(dataset.numpy("ZYXS")) == before and calibration_signature(dataset.calibration) == before_cal
