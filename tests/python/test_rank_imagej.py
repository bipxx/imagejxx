"""Finite-scalar ImageJ rank bytes, Python boundary and command workflows.

Expected numerical pixels are copied from retained original ImageJ observations.
No native or Python filtering algorithm generates an expected pixel here.
RGB/nonfinite/dialog/preview source rows remain explicitly outside this core
API acceptance; these tests do not establish UI or platform acceptance.
"""
from __future__ import annotations

import copy
from decimal import Decimal
from fractions import Fraction
import hashlib
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


FIXTURE_PATH = Path(__file__).resolve().parents[1] / "fixtures/rank_imagej_154r_001.json"
FIXTURE_SHA = "121d1688e9a26c20818fbebfe88cbc4d1704ad26d330bfff5bdcc4d8e11694e5"
FIXTURE_BYTES = FIXTURE_PATH.read_bytes()
CORPUS = json.loads(FIXTURE_BYTES)
RUNS = CORPUS["runs"]
KERNELS = CORPUS["kernels"]
METHOD_NAMES = ("minimum", "maximum", "median", "mean")
K10_RUNS = tuple(run for run in RUNS if run["source_case_id"] == "K10")
K14_RUNS = tuple(run for run in RUNS if run["source_case_id"] == "K14")
SIGNED_ZERO_RUNS = tuple(run for run in RUNS if run["source_case_id"] == "SZ01" and run["method"] != "median")


def observed(case_id, name):
    return next(run for run in RUNS if run["id"] == case_id + "/" + name)


def plane_array(run, plane, phase):
    dtype = {"uint8": "u1", "uint16": "<u2", "float32": "<f4"}[run["dtype"]]
    return np.frombuffer(bytes.fromhex(plane[phase + "_hex"]), dtype=dtype).reshape(run["shape_tczyx"][-2:]).copy()


def dataset_array(run, phase):
    result = np.empty(run["shape_tczyx"], dtype={"uint8": np.uint8, "uint16": np.uint16, "float32": np.float32}[run["dtype"]])
    for plane in run["planes"]:
        result[tuple(plane["tcz"])] = plane_array(run, plane, phase)
    return result


def bounds_and_mask(run):
    height, width = run["shape_tczyx"][-2:]
    roi = run["roi"]
    if roi and roi["kind"] in ("rectangle", "holed_area"):
        rectangle = tuple(roi.get("xywh", roi.get("outer_xywh")))
        extent = rectangle
    else:
        rectangle, extent = (0, 0, width, height), None
    x, y, w, h = rectangle
    rectangle_mask = np.zeros((height, width), bool)
    rectangle_mask[y:y + h, x:x + w] = True
    write_mask = rectangle_mask.copy()
    if roi and roi["kind"] == "holed_area":
        x, y, w, h = roi["hole_xywh"]
        write_mask[y:y + h, x:x + w] = False
    return extent, rectangle_mask, write_mask


def owner(pixels=None, axes="YX", calibration=None):
    if pixels is None:
        run = observed("K02", "uint16")
        pixels = plane_array(run, run["planes"][0], "before")
    kwargs = {} if calibration is None else {"calibration": calibration}
    return ij.Document(ij.Dataset(pixels, axes=axes, **kwargs), "Rank source")


def command(document, method="median", radius=2, request=None):
    return ij.resolve_rank_command(document.snapshot(),
        ij.TargetRequest() if request is None else request,
        ij.RankFilterCommandOptions(method, radius))


def positions(target):
    return [(p.t, p.c, p.z) for p in (target.position(i) for i in range(target.position_count))]


def unchanged(document, before, history):
    now = document.snapshot()
    assert now.shares_data(before)
    assert now.revisions.matches(before.revisions, ij.Change.ALL)
    assert now.dirty == before.dirty
    assert document.undo_steps == history


def test_retained_fixture_identity_and_complete_original_membership():
    assert len(FIXTURE_BYTES) == 311070
    assert hashlib.sha256(FIXTURE_BYTES).hexdigest() == FIXTURE_SHA
    assert CORPUS["imagej_version"] == "1.54r"
    assert CORPUS["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    provenance = CORPUS["provenance"]
    assert provenance["raw"] == {
        "path": "build/references/rank-filter-imagej154r-001/capture-001/reference.stdout",
        "bytes": 865012,
        "sha256": "f2a3677079d355b53339b11612fc39ca809e5b070b22caa80a5d4fcc6b739df5"}
    assert provenance["supplement"]["sha256"] == "bb18f7476fdb181964f1d24efa5ce8492c903d91beeadd0aed2c8609ed6a1b10"
    assert provenance["rank_source_members"]["ij/plugin/filter/RankFilters.java"]["sha256"] == "284e7647e6619a3cc62887c57992056ccfd1dd99661868571fff005afff7d6b7"
    assert provenance["original_case_ids"] == [f"K{i:02d}" for i in range(1, 17)]
    assert [row["numerical_run_count"] for row in provenance["original_case_inventory"]] == [0, 3, 3, 3, 3, 8, 1, 1, 108, 8, 12, 4, 12, 8, 0, 0]
    assert provenance["original_numerical_run_count"] == 174
    assert set(provenance["observational_only"]) == {"K12", "K13", "K15", "K16"}
    assert CORPUS["original_selected_run_count"] == 158
    assert CORPUS["supplement_run_count"] == 3
    assert len(RUNS) == CORPUS["run_count"] == 161
    assert len(KERNELS) == CORPUS["kernel_count"] == 10
    assert len({run["id"] for run in RUNS}) == 161
    assert len(K10_RUNS) == len(K14_RUNS) == 8 and len(SIGNED_ZERO_RUNS) == 2


@pytest.mark.parametrize("observation", KERNELS, ids=lambda value: value["id"])
def test_observed_imagej_kernel_geometry(observation):
    actual = ij.rank_filter_kernel_imagej(observation["radius"])
    assert set(actual) == {"half_widths", "point_count"}
    assert actual["half_widths"] == observation["half_widths"]
    assert actual["point_count"] == observation["point_count"]
    middle = len(actual["half_widths"]) // 2
    assert [[x, row - middle] for row, half in enumerate(actual["half_widths"])
            for x in range(-half, half + 1)] == observation["offsets_xy"]


@pytest.mark.parametrize("run", RUNS, ids=lambda value: value["id"])
def test_observed_imagej_finite_scalar_pixels(run):
    extent, rectangle_mask, write_mask = bounds_and_mask(run)
    selected = 0
    for plane in run["planes"]:
        if not run["all_stack"] and plane["tcz"] != run["position_tcz"]:
            assert plane["before_hex"] == plane["after_hex"]
            continue
        selected += 1
        source = plane_array(run, plane, "before")
        expected = plane_array(run, plane, "after")
        retained = source.tobytes()
        actual = ij.rank_filter_imagej(source, run["radius"], method=run["method"], extent=extent)
        assert actual.shape == source.shape and actual.dtype == source.dtype
        assert source.tobytes() == retained
        assert not np.shares_memory(actual, source)
        assert actual[write_mask].tobytes() == expected[write_mask].tobytes()
        assert actual[~rectangle_mask].tobytes() == source[~rectangle_mask].tobytes()
        # The standalone rectangle kernel does not restore irregular mask holes.
        assert expected[~write_mask].tobytes() == source[~write_mask].tobytes()
    assert selected == (len(run["planes"]) if run["all_stack"] else 1)


INVALID_RADII = [
    ("bool", True, TypeError), ("numpy_bool", np.bool_(False), TypeError),
    ("string", "2", TypeError), ("bytes", b"2", TypeError),
    ("complex", 2 + 0j, TypeError), ("none", None, TypeError),
    ("decimal", Decimal("2"), TypeError), ("negative", -1, ValueError),
    ("nan", float("nan"), ValueError), ("positive_infinity", float("inf"), ValueError),
    ("negative_infinity", -float("inf"), ValueError), ("above_limit", 1000.0001, ValueError),
]


@pytest.mark.parametrize("case", INVALID_RADII, ids=lambda value: value[0])
def test_radius_rejects_invalid_types_and_values_atomically(case):
    _, value, error = case
    pixels = np.arange(9, dtype=np.uint8).reshape(3, 3)
    retained = pixels.tobytes()
    with pytest.raises(error):
        ij.rank_filter_imagej(pixels, value, method="mean")
    with pytest.raises(error):
        ij.rank_filter_kernel_imagej(value)
    with pytest.raises(error):
        ij.RankFilterCommandOptions("mean", value)
    options = ij.RankFilterCommandOptions("mean", 3)
    with pytest.raises(error):
        options.radius = value
    assert options.method == ij.RankFilterMethod.Mean and options.radius == 3
    assert pixels.tobytes() == retained


REAL_RADII = [("numpy_integer", np.int64(2)), ("numpy_f32", np.float32(.5)),
              ("numpy_f64", np.float64(1.5)), ("fraction", Fraction(3, 2)), ("negative_zero", -0.0)]


@pytest.mark.parametrize("case", REAL_RADII, ids=lambda value: value[0])
def test_real_radii_capture_exact_intent(case):
    _, value = case
    options = ij.RankFilterCommandOptions("minimum", value)
    assert options.radius == float(value) and options.method == ij.RankFilterMethod.Minimum
    assert command(owner(), "minimum", value).parameters() == {"method": "minimum", "radius": float(value)}


INVALID_METHODS = [
    ("capitalized", "Median", ValueError), ("alias", "min", ValueError),
    ("trailing_space", "mean ", ValueError), ("empty", "", ValueError),
    ("bytes", b"median", TypeError), ("bool", True, TypeError),
    ("numpy_bool", np.bool_(False), TypeError), ("integer", 2, TypeError), ("none", None, TypeError),
]


@pytest.mark.parametrize("case", INVALID_METHODS, ids=lambda value: value[0])
def test_method_requires_enum_or_exact_unicode_and_mutation_is_atomic(case):
    _, value, error = case
    source = np.zeros((3, 3), np.uint8)
    with pytest.raises(error):
        ij.rank_filter_imagej(source, method=value)
    with pytest.raises(error):
        ij.RankFilterCommandOptions(value, 2)
    options = ij.RankFilterCommandOptions("maximum", 3)
    with pytest.raises(error):
        options.method = value
    assert options.method == ij.RankFilterMethod.Maximum and options.radius == 3


@pytest.mark.parametrize("name", METHOD_NAMES, ids=METHOD_NAMES)
def test_enum_method_and_string_method_have_identical_observed_output(name):
    run = observed({"minimum": "K02", "maximum": "K03", "median": "K04", "mean": "K05"}[name], "uint16")
    source, expected = (plane_array(run, run["planes"][0], phase) for phase in ("before", "after"))
    enum = getattr(ij.RankFilterMethod, name.title())
    assert ij.rank_filter_imagej(source, 1, method=enum).tobytes() == expected.tobytes()
    assert ij.rank_filter_imagej(source, 1, method=name).tobytes() == expected.tobytes()


class IndexCoordinate:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value


INVALID_EXTENTS = [
    ("short", (0, 0, 1), ValueError), ("long", (0, 0, 1, 1, 1), ValueError),
    ("string", "0011", TypeError), ("bytes", b"0011", TypeError),
    ("boolean", (False, 0, 1, 1), TypeError), ("numpy_boolean", (0, np.bool_(True), 1, 1), TypeError),
    ("float", (0, 0, 1.0, 1), TypeError), ("huge_positive", (2**40, 0, 0, 0), ValueError),
    ("huge_negative", (-2**40, 0, 0, 0), ValueError), ("negative_origin", (-1, 0, 1, 1), ValueError),
    ("negative_width", (0, 0, -1, 1), ValueError), ("outside", (3, 0, 1, 1), ValueError),
    ("empty_outside", (4, 0, 0, 0), ValueError), ("overflowing_end", (1, 0, 2**31 - 1, 1), ValueError),
]


@pytest.mark.parametrize("case", INVALID_EXTENTS, ids=lambda value: value[0])
def test_extent_requires_four_index_int32_coordinates(case):
    _, value, error = case
    source = np.arange(9, dtype=np.uint8).reshape(3, 3)
    with pytest.raises(error):
        ij.rank_filter_imagej(source, extent=value)
    assert source.tobytes() == bytes(range(9))


def test_index_coordinates_empty_extent_and_default_are_precise():
    source = np.arange(9, dtype=np.uint16).reshape(3, 3)
    extent = [IndexCoordinate(3), np.int32(3), np.int64(0), IndexCoordinate(0)]
    assert ij.rank_filter_imagej(source, extent=extent).tobytes() == source.tobytes()
    assert ij.rank_filter_imagej(source).tobytes() == ij.rank_filter_imagej(source, 2, method="median").tobytes()
    assert ij.rank_filter_imagej(source, 0, method="maximum").tobytes() != source.tobytes()


def test_options_validate_entire_candidate_and_resolved_returns_detached_copies():
    document = owner()
    options = ij.RankFilterCommandOptions("mean", 500)
    with pytest.raises(ValueError):
        options.method = "median"
    assert options.method == ij.RankFilterMethod.Mean and options.radius == 500
    options.radius = 2
    options.method = "median"
    with pytest.raises(ValueError):
        options.radius = 100.001
    assert options.method == ij.RankFilterMethod.Median and options.radius == 2
    resolved = ij.resolve_rank_command(document.snapshot(), options=options)
    options.method, options.radius = "minimum", 3
    detached = resolved.options
    detached.method, detached.radius = "maximum", 4
    assert resolved.options.method == ij.RankFilterMethod.Median and resolved.options.radius == 2
    parameters = resolved.parameters()
    parameters["radius"] = 99
    assert resolved.parameters() == {"method": "median", "radius": 2.0}
    assert resolved.options is not resolved.options and resolved.target is not resolved.target
    with pytest.raises(AttributeError):
        resolved.options = options
    with pytest.raises(AttributeError):
        resolved.target = resolved.target


def test_default_command_record_is_exact_detached_and_json_roundtrips():
    document = owner()
    before = document.snapshot()
    resolved = ij.resolve_rank_command(before)
    assert isinstance(resolved, ij.ResolvedRankFilterCommand)
    assert resolved.options.method == ij.RankFilterMethod.Median and resolved.options.radius == 2
    assert positions(resolved.target) == [(0, 0, 0)]
    assert resolved.region() == ij.rank_command_region(resolved.target) == {"extent": None, "mask": None}
    record = resolved.record()
    assert set(record) == {"schema_version", "operation", "profile", "options", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "rank_filter_imagej" and record["profile"] == "imagej_154r"
    assert record["options"] == {"method": "median", "radius": 2.0}
    assert ij.validate_rank_command_record(record) is None
    decoded = ij.rank_command_from_record(before, json.loads(json.dumps(record)))
    assert decoded.record() == record and decoded.parameters() == resolved.parameters()
    record["options"]["radius"] = 17
    record["target"]["source_shape"][0] = 99
    assert decoded.record() == resolved.record()
    unchanged(document, before, 0)


BAD_RECORDS = [
    ("schema_boolean", ("schema_version",), True), ("schema_version", ("schema_version",), 2),
    ("generic_operation", ("operation",), "rank_filter"), ("profile", ("profile",), "other"),
    ("root_unknown", ("unknown",), True), ("options_unknown", ("options", "unknown"), 1),
    ("method_case", ("options", "method"), "Median"), ("method_boolean", ("options", "method"), True),
    ("radius_boolean", ("options", "radius"), True), ("radius_negative", ("options", "radius"), -1),
    ("median_limit", ("options", "radius"), 101), ("radius_nonfinite", ("options", "radius"), float("inf")),
    ("radius_inexact_integer", ("options", "radius"), 2**53 + 1),
    ("target_unknown", ("target", "unknown"), True), ("volume", ("target", "execution_mode"), "volume"),
    ("rgb_shape", ("target", "source_shape"), [1, 1, 1, 7, 9, 3]),
    ("zero_shape", ("target", "source_shape"), [1, 0, 1, 7, 9, 1]),
    ("boolean_revisions", ("target", "source_revisions"), [True, 0, 0]),
    ("zero_identity", ("target", "document_id"), 0),
    ("scope_unknown", ("target", "scope"), "unknown"),
    ("roi_unknown", ("target", "roi_policy"), "unknown"),
    ("destination_unknown", ("target", "destination"), "unknown"),
]


@pytest.mark.parametrize("case", BAD_RECORDS, ids=lambda value: value[0])
def test_invalid_original_record_is_rejected_before_rebind(case):
    _, path, value = case
    document, destination = owner(), owner(np.zeros((3, 4), np.uint8))
    before, destination_before = document.snapshot(), destination.snapshot()
    record = command(document).record()
    cursor = record
    for key in path[:-1]:
        cursor = cursor[key]
    cursor[path[-1]] = value
    # Metadata construction itself rejects nonfinite nodes; all three public
    # entry points must refuse rather than repair a malformed original claim.
    for attempt in (lambda: ij.validate_rank_command_record(record),
                    lambda: ij.rank_command_from_record(before, record),
                    lambda: ij.rebind_rank_command(destination_before, record, ij.TargetRequest())):
        with pytest.raises((ValueError, IndexError)):
            attempt()
    unchanged(document, before, 0)
    unchanged(destination, destination_before, 0)


def test_record_requires_every_root_and_option_field():
    document = owner()
    record = command(document).record()
    for path in ((), ("options",)):
        for field in (record if not path else record[path[0]]):
            incomplete = copy.deepcopy(record)
            del (incomplete if not path else incomplete[path[0]])[field]
            with pytest.raises(ValueError):
                ij.validate_rank_command_record(incomplete)


def test_explicit_rebind_preserves_intent_and_new_request_without_mutating_original():
    document = owner()
    destination = owner(np.zeros((2, 2, 3, 5, 6), np.uint16), "TCZYX",
                        ij.SpatialCalibration(spacing=(.25, 8, 13), unit="um"))
    before, destination_before = document.snapshot(), destination.snapshot()
    record = command(document, "mean", 2.5).record()
    retained = copy.deepcopy(record)
    with pytest.raises(ValueError):
        ij.rank_command_from_record(destination_before, record)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 1, 2), ij.PlanePosition(0, 0, 1)],
        roi=ij.RoiPolicy.WHOLE_PLANE, destination=ij.OutputDestination.NEW_DOCUMENT)
    rebound = ij.rebind_rank_command(destination_before, record, request)
    assert rebound.parameters() == {"method": "mean", "radius": 2.5}
    assert positions(rebound.target) == [(1, 1, 2), (0, 0, 1)]
    assert rebound.target.document_id == destination.id
    assert rebound.target.roi_policy == ij.RoiPolicy.WHOLE_PLANE
    assert rebound.target.destination == ij.OutputDestination.NEW_DOCUMENT
    assert ij.rank_command_from_record(destination_before, rebound.record()).record() == rebound.record()
    assert record == retained
    with pytest.raises(TypeError):
        ij.rebind_rank_command(destination_before, record)
    with pytest.raises(TypeError):
        ij.rebind_rank_command(destination_before, record, None)
    with pytest.raises(IndexError):
        ij.rebind_rank_command(destination_before, record,
            ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(z=3)]))
    unchanged(document, before, 0)
    unchanged(destination, destination_before, 0)


UNSUPPORTED_METADATA = [("float64", np.float64, 1), ("rgb_u8", np.uint8, 3),
                        ("rgb_u16", np.uint16, 3), ("rgb_f32", np.float32, 3)]


@pytest.mark.parametrize("case", UNSUPPORTED_METADATA, ids=lambda value: value[0])
def test_unsupported_source_metadata_fails_without_edit(case):
    _, dtype, samples = case
    pixels = np.zeros((5, 7) if samples == 1 else (5, 7, samples), dtype)
    document = owner(pixels, "YX" if samples == 1 else "YXS")
    before = document.snapshot()
    with pytest.raises(ValueError):
        command(document)
    with pytest.raises(ValueError):
        ij.rank_filter_imagej(pixels)
    unchanged(document, before, 0)


def test_quiet_nan_policy_and_volume_targets_are_rejected_before_edit():
    pixels = np.zeros((2, 5, 7), np.float32)
    document = ij.Document(ij.Dataset(pixels, axes="ZYX",
        sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32))
    before = document.snapshot()
    with pytest.raises(ValueError):
        command(document)
    unchanged(document, before, 0)
    finite = owner(pixels, "ZYX")
    finite_before = finite.snapshot()
    with pytest.raises(ValueError):
        command(finite, request=ij.TargetRequest(ij.TargetScope.ALL_Z, mode=ij.ExecutionMode.VOLUME))
    unchanged(finite, finite_before, 0)


@pytest.mark.parametrize("run", K10_RUNS, ids=lambda value: value["id"])
def test_observed_runner_rectangle_and_hole_execute_as_one_history_edit(run):
    plane = run["planes"][0]
    source, expected = (plane_array(run, plane, phase) for phase in ("before", "after"))
    document = owner(source)
    _, _, mask = bounds_and_mask(run)
    document.select(ij.Selection.from_mask(mask.astype(np.uint8)))
    before, history = document.snapshot(), document.undo_steps
    resolved = command(document, run["method"], run["radius"])
    region = resolved.region()
    assert region["extent"] == (1, 1, 7, 5)
    assert region["mask"].tobytes() == (mask.astype(np.uint8) * 255).tobytes()
    region["mask"][:] = 0
    assert resolved.region()["mask"].tobytes() == (mask.astype(np.uint8) * 255).tobytes()
    with pytest.raises(IndexError):
        resolved.region(resolved.target.position_count)
    application = ij.Application()
    application.adopt(document)
    prepared = application.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    assert prepared.preview.dataset.numpy("YX").tobytes() == expected.tobytes()
    unchanged(document, before, history)
    result = application.publish(prepared)
    assert result.document is document and not result.created and not prepared.active
    changed = source.tobytes() != expected.tobytes()
    assert document.undo_steps == history + int(changed)
    assert document.snapshot().dataset.numpy("YX").tobytes() == expected.tobytes()
    if changed:
        document.undo()
        assert document.snapshot().dataset.numpy("YX").tobytes() == source.tobytes()
        document.redo()
        assert document.snapshot().dataset.numpy("YX").tobytes() == expected.tobytes()


@pytest.mark.parametrize("run", K14_RUNS, ids=lambda value: value["id"])
def test_observed_current_and_full_czt_targets_survive_navigation(run):
    source, expected = (dataset_array(run, phase) for phase in ("before", "after"))
    calibration = ij.SpatialCalibration(spacing=(.5, 2, 3), origin=(11, -7, 5), unit="um")
    document = owner(source, "TCZYX", calibration)
    view = document.snapshot().display
    view.t, view.c, view.crosshair = 1, 2, [2, 3, 3]
    document.set_display(view)
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(), c=ij.AxisRange(), z=ij.AxisRange()) if run["all_stack"] else ij.TargetRequest()
    resolved = command(document, run["method"], run["radius"], request)
    expected_positions = [(t, c, z) for t in range(2) for c in range(3) for z in range(4)] if run["all_stack"] else [(1, 2, 3)]
    assert positions(resolved.target) == expected_positions
    captured = resolved.record()
    view.t, view.c, view.crosshair, view.zoom = 0, 0, [1, 2, 0], 3
    document.set_display(view)
    before = document.snapshot()
    replay = ij.rank_command_from_record(before, captured)
    assert positions(replay.target) == expected_positions
    application = ij.Application()
    application.adopt(document)
    prepared = application.prepare("rank_filter_imagej", replay.target, replay.parameters())
    assert prepared.preview.dataset.numpy("TCZYX").tobytes() == expected.tobytes()
    unchanged(document, before, 0)
    application.publish(prepared)
    after = document.snapshot()
    assert after.dataset.numpy("TCZYX").tobytes() == expected.tobytes()
    assert (after.display.t, after.display.c, after.display.crosshair, after.display.zoom) == (0, 0, [1, 2, 0], 3)
    assert after.dataset.calibration.spacing == [.5, 2, 3]
    assert after.dataset.calibration.origin == [11, -7, 5]
    changed = source.tobytes() != expected.tobytes()
    assert document.undo_steps == int(changed)
    if changed:
        document.undo()
        assert document.snapshot().dataset.numpy("TCZYX").tobytes() == source.tobytes()


@pytest.mark.parametrize("run", SIGNED_ZERO_RUNS, ids=lambda value: value["id"])
def test_signed_zero_only_change_is_published_and_undo_restores_original_bits(run):
    source, expected = (plane_array(run, run["planes"][0], phase) for phase in ("before", "after"))
    assert np.array_equal(source, expected) and source.tobytes() != expected.tobytes()
    document = owner(source)
    before = document.snapshot()
    assert document.undo_steps == 0 and not before.dirty
    application = ij.Application()
    application.adopt(document)
    resolved = command(document, run["method"], run["radius"])
    prepared = application.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    assert prepared.preview.dataset.numpy("YX").tobytes() == expected.tobytes()
    unchanged(document, before, 0)
    application.publish(prepared)
    assert document.undo_steps == 1 and document.snapshot().dirty
    assert document.snapshot().dataset.numpy("YX").tobytes() == expected.tobytes()
    document.undo()
    assert document.undo_steps == 0 and document.redo_steps == 1
    assert document.snapshot().dataset.numpy("YX").tobytes() == source.tobytes()
    document.redo()
    assert document.snapshot().dataset.numpy("YX").tobytes() == expected.tobytes()


@pytest.mark.parametrize("kind", ["constant", "empty_selection", "unmatched_positioned"], ids=["constant", "empty_selection", "unmatched_positioned"])
def test_noop_and_empty_targets_do_not_add_dirty_history(kind):
    pixels = np.full((5, 7), 17, np.uint16)
    document = owner(pixels)
    request = ij.TargetRequest()
    if kind == "empty_selection":
        document.select(ij.Selection.from_mask(np.zeros_like(pixels, np.uint8)))
    elif kind == "unmatched_positioned":
        request = ij.TargetRequest(roi=ij.RoiPolicy.POSITIONED_ROIS)
    before, history = document.snapshot(), document.undo_steps
    resolved = command(document, "mean", 3, request)
    application = ij.Application()
    application.adopt(document)
    prepared = application.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    assert prepared.preview.dataset.numpy("YX").tobytes() == pixels.tobytes()
    if kind != "constant":
        assert len(prepared.outputs) == 1 and prepared.outputs[0].skipped
    application.publish(prepared)
    unchanged(document, before, history)
    assert not prepared.active


@pytest.mark.parametrize("mutation", ["metadata", "pixels", "selection"], ids=["metadata", "pixels", "selection"])
def test_stale_preparation_refuses_publication_atomically(mutation):
    document = owner()
    application = ij.Application()
    application.adopt(document)
    resolved = command(document, "minimum", 1)
    prepared = application.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    if mutation == "selection":
        document.select(ij.Selection("point", [(1, 2)]))
    else:
        edit = document.begin_edit()
        if mutation == "metadata":
            edit.set_title("Intervening title")
        else:
            edit.replace_plane(np.zeros((7, 9), np.uint16))
        document.commit(edit)
    before, history = document.snapshot(), document.undo_steps
    with pytest.raises(ij.RevisionConflict):
        application.publish(prepared)
    assert prepared.active
    unchanged(document, before, history)
    with pytest.raises(ij.RevisionConflict):
        ij.rank_command_from_record(before, resolved.record())


def test_pixel_radius_keeps_metadata_dependency_without_calibration_scaling():
    document = owner(calibration=ij.SpatialCalibration(spacing=(.25, 8, 3), unit="um"))
    resolved = command(document, "mean", 2.5, ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    record = resolved.record()
    assert resolved.parameters() == {"method": "mean", "radius": 2.5}
    document.select(ij.Selection("rectangle", [(1, 1), (3, 3)]))
    resolved.target.validate_source(document.snapshot())
    edit = document.begin_edit()
    edit.set_calibration(ij.SpatialCalibration(spacing=(10, 10, 3), unit="um"))
    document.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        resolved.target.validate_source(document.snapshot())
    with pytest.raises(ij.RevisionConflict):
        ij.rank_command_from_record(document.snapshot(), record)
    assert command(document, "mean", 2.5).parameters() == {"method": "mean", "radius": 2.5}


def test_precancel_and_new_document_publication_preserve_source_owner():
    run = observed("K03", "uint16")
    source, expected = (plane_array(run, run["planes"][0], phase) for phase in ("before", "after"))
    document = owner(source)
    application = ij.Application()
    application.adopt(document)
    before = document.snapshot()
    resolved = command(document, "maximum", 1,
        ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT))
    cancellation = ij.CancellationToken()
    cancellation.cancel()
    with pytest.raises(ij.OperationCancelled):
        application.prepare("rank_filter_imagej", resolved.target, resolved.parameters(), cancellation=cancellation)
    unchanged(document, before, 0)
    prepared = application.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    assert prepared.preview.dataset.numpy("YX").tobytes() == expected.tobytes()
    result = application.publish(prepared, title="Rank output")
    assert result.created and result.document_id != document.id
    assert result.document.snapshot().dataset.numpy("YX").tobytes() == expected.tobytes()
    assert result.document.undo_steps == 1
    unchanged(document, before, 0)


# Keep source-only collection auditing possible without importing imagejxx.
# These IDs are asserted by the root before executing the matching extension.
EXPECTED_COLLECTION_IDS = (
    "test_retained_fixture_identity_and_complete_original_membership",
    *("test_observed_imagej_kernel_geometry[" + item["id"] + "]" for item in KERNELS),
    *("test_observed_imagej_finite_scalar_pixels[" + item["id"] + "]" for item in RUNS),
    *("test_radius_rejects_invalid_types_and_values_atomically[" + item[0] + "]" for item in INVALID_RADII),
    *("test_real_radii_capture_exact_intent[" + item[0] + "]" for item in REAL_RADII),
    *("test_method_requires_enum_or_exact_unicode_and_mutation_is_atomic[" + item[0] + "]" for item in INVALID_METHODS),
    *("test_enum_method_and_string_method_have_identical_observed_output[" + name + "]" for name in METHOD_NAMES),
    *("test_extent_requires_four_index_int32_coordinates[" + item[0] + "]" for item in INVALID_EXTENTS),
    "test_index_coordinates_empty_extent_and_default_are_precise",
    "test_options_validate_entire_candidate_and_resolved_returns_detached_copies",
    "test_default_command_record_is_exact_detached_and_json_roundtrips",
    *("test_invalid_original_record_is_rejected_before_rebind[" + item[0] + "]" for item in BAD_RECORDS),
    "test_record_requires_every_root_and_option_field",
    "test_explicit_rebind_preserves_intent_and_new_request_without_mutating_original",
    *("test_unsupported_source_metadata_fails_without_edit[" + item[0] + "]" for item in UNSUPPORTED_METADATA),
    "test_quiet_nan_policy_and_volume_targets_are_rejected_before_edit",
    *("test_observed_runner_rectangle_and_hole_execute_as_one_history_edit[" + item["id"] + "]" for item in K10_RUNS),
    *("test_observed_current_and_full_czt_targets_survive_navigation[" + item["id"] + "]" for item in K14_RUNS),
    *("test_signed_zero_only_change_is_published_and_undo_restores_original_bits[" + item["id"] + "]" for item in SIGNED_ZERO_RUNS),
    *("test_noop_and_empty_targets_do_not_add_dirty_history[" + name + "]" for name in ("constant", "empty_selection", "unmatched_positioned")),
    *("test_stale_preparation_refuses_publication_atomically[" + name + "]" for name in ("metadata", "pixels", "selection")),
    "test_pixel_radius_keeps_metadata_dependency_without_calibration_scaling",
    "test_precancel_and_new_document_publication_preserve_source_owner",
)
EXPECTED_COLLECTION_COUNT = 274
