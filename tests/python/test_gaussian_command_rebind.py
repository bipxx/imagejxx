"""H3 explicit rebind/offline validation, with independent finite expectations.

Public Python Dataset construction is eager. These checks preserve owned bytes
and state; native/installed-SDK throwing lazy sources establish zero-read paths.
No UI, original-source existence, or independent upstream parity is claimed.
"""

import copy
import gc

import imagejxx as ij
import numpy as np
import pytest


def analytic(shape):
    t, c, z, y, x = np.indices(shape)
    return ((37 * x + 61 * y + 13 * x * y + 97 * t + 503 * c + 997 * z)
            / 8.0 - 0.125).astype(np.float32)


def owner(destination=False, unit="nm"):
    shape = (3, 2, 4, 5, 7) if destination else (2, 3, 5, 7, 9)
    pixels = analytic(shape)
    calibration = (ij.SpatialCalibration(
        spacing=(2, 0.25, 4), origin=(-3, 19, 2),
        direction=(1, 0, 0, 0, 0, -1, 0, 1, 0), unit=unit,
        frame_interval=2, time_origin=-1, time_unit="s") if destination else
        ij.SpatialCalibration(
            spacing=(0.5, 2, 3), origin=(11, -7, 5),
            direction=(0, -1, 0, 1, 0, 0, 0, 0, 1), unit="um",
            frame_interval=0.25, time_origin=8, time_unit="min"))
    document = ij.Document(ij.Dataset(pixels, axes="TCZYX", calibration=calibration))
    mask = np.zeros(shape[-2:], np.uint8)
    mask[1, 2] = mask[2, 3] = 255
    document.select(ij.Selection.from_mask(mask))
    view = document.snapshot().display
    view.t, view.c, view.crosshair = shape[0] - 1, shape[1] - 1, [1, 2, shape[2] - 1]
    document.set_display(view)
    return document, pixels


def original_request():
    return ij.TargetRequest(
        ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 2, 4), ij.PlanePosition(0, 1, 2)],
        roi=ij.RoiPolicy.ACTIVE_SELECTION,
        destination=ij.OutputDestination.NEW_DOCUMENT)


def destination_request():
    return ij.TargetRequest(
        ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(2, 1, 3), ij.PlanePosition(0, 0, 1)],
        roi=ij.RoiPolicy.POSITIONED_ROIS,
        destination=ij.OutputDestination.IN_PLACE)


def command(document, scaled=True, request=None):
    return ij.resolve_gaussian_command(
        document.snapshot(), original_request() if request is None else request,
        ij.GaussianBlurCommandOptions(2, scaled))


def positions(target):
    return [(p.t, p.c, p.z) for p in
            (target.position(i) for i in range(target.position_count))]


def unchanged(document, before, pixels, history):
    now = document.snapshot()
    assert now.shares_data(before)
    assert now.revisions.matches(before.revisions, ij.Change.ALL)
    assert now.dirty == before.dirty
    assert document.undo_steps == history and document.redo_steps == 0
    assert now.dataset.numpy("TCZYX").tobytes() == pixels.tobytes()


@pytest.mark.parametrize("invalid_binding", ["foreign", "stale", "shape", "calibration"])
def test_H3_P01_strict_binding_remains_required(invalid_binding):
    original, original_pixels = owner()
    destination, destination_pixels = owner(True)
    record = command(original).record()
    before = original.snapshot()
    destination_before, destination_history = destination.snapshot(), destination.undo_steps
    if invalid_binding == "foreign":
        strict_source = destination.snapshot()
    elif invalid_binding == "stale":
        edit = original.begin_edit(ij.Change.METADATA)
        edit.set_title("Explicit intervening source edit")
        original.commit(edit)
        strict_source = original.snapshot()
    elif invalid_binding == "shape":
        record["target"]["source_shape"] = [2, 3, 5, 8, 10, 1]
        strict_source = before
    else:
        record["calibration"]["origin"] = [12.0, -7.0, 5.0]
        strict_source = before
    expected_source, expected_history = original.snapshot(), original.undo_steps
    saved = copy.deepcopy(record)
    with pytest.raises(ij.RevisionConflict if invalid_binding == "stale" else ValueError):
        ij.gaussian_command_from_record(strict_source, record)
    assert ij.validate_gaussian_command_record(record) is None
    rebound = ij.rebind_gaussian_command(destination.snapshot(), record, destination_request())
    assert positions(rebound.target) == [(2, 1, 3), (0, 0, 1)]
    assert rebound.parameters() == {"sigma": [8.0, 1.0]}
    assert record == saved
    unchanged(original, expected_source, original_pixels, expected_history)
    unchanged(destination, destination_before, destination_pixels, destination_history)


@pytest.mark.parametrize("scaled", [False, True], ids=["pixel_intent", "scaled_intent"])
@pytest.mark.parametrize("unit", ["nm", "px", "pixel", "pixels"])
def test_H3_P02_destination_calibration_resolves_raw_intent(scaled, unit):
    original, source_pixels = owner()
    destination, destination_pixels = owner(True, unit)
    before, source_before = destination.snapshot(), original.snapshot()
    history, source_history = destination.undo_steps, original.undo_steps
    record = command(original, scaled).record()
    saved = copy.deepcopy(record)
    rebound = ij.rebind_gaussian_command(
        destination=before, record=record, request=destination_request())
    effective = scaled and unit == "nm"
    assert (rebound.sigma_x, rebound.sigma_y) == ((1, 8) if effective else (2, 2))
    assert rebound.parameters() == {"sigma": [8.0, 1.0] if effective else [2.0, 2.0]}
    assert rebound.options.sigma == 2 and rebound.options.scaled_units is scaled
    assert rebound.scaled_units_available is (unit == "nm")
    assert rebound.record()["calibration"] == {
        "spacing": [2.0, 0.25, 4.0], "origin": [-3.0, 19.0, 2.0],
        "direction": [1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0, 0.0],
        "unit": unit, "frame_interval": 2.0, "time_origin": -1.0, "time_unit": "s"}
    assert ij.gaussian_command_from_record(before, rebound.record()).record() == rebound.record()
    assert record == saved
    unchanged(destination, before, destination_pixels, history)
    unchanged(original, source_before, source_pixels, source_history)


@pytest.mark.parametrize("scope", ["current", "all_z", "ranges", "positions"])
def test_H3_P03_explicit_request_owns_destination_target(scope):
    original, source_pixels = owner()
    destination, pixels = owner(True)
    source_before, source_history = original.snapshot(), original.undo_steps
    before, history = destination.snapshot(), destination.undo_steps
    if scope == "current":
        request, expected = ij.TargetRequest(), [(2, 1, 3)]
    elif scope == "all_z":
        request = ij.TargetRequest(ij.TargetScope.ALL_Z)
        expected = [(2, 1, z) for z in range(4)]
    elif scope == "ranges":
        request = ij.TargetRequest(ij.TargetScope.RANGES,
                                   t=ij.AxisRange(0, 3, 2), c=ij.AxisRange(0, 2),
                                   z=ij.AxisRange(1, 4, 2))
        expected = [(t, c, z) for t in (0, 2) for c in (0, 1) for z in (1, 3)]
    else:
        request, expected = destination_request(), [(2, 1, 3), (0, 0, 1)]
    record = command(original).record()
    rebound = ij.rebind_gaussian_command(before, record, request)
    assert positions(rebound.target) == expected
    assert rebound.target.document_id == destination.id
    assert rebound.target.source.shares_data(before)
    assert rebound.target.roi_policy == request.roi
    assert rebound.target.destination == request.destination
    view = before.display
    view.t, view.c, view.crosshair = 0, 0, [1, 2, 0]
    destination.set_display(view)
    navigated = destination.snapshot()
    assert positions(ij.gaussian_command_from_record(navigated, rebound.record()).target) == expected
    assert positions(rebound.target) == expected
    unchanged(destination, navigated, pixels, history)
    unchanged(original, source_before, source_pixels, source_history)


@pytest.mark.parametrize("missing", ["omitted", "none"])
def test_H3_P04_request_argument_is_required(missing):
    original, source_pixels = owner()
    destination, pixels = owner(True)
    source_before, source_history = original.snapshot(), original.undo_steps
    before, history = destination.snapshot(), destination.undo_steps
    record = command(original).record()
    with pytest.raises(TypeError):
        if missing == "omitted":
            ij.rebind_gaussian_command(before, record)
        else:
            ij.rebind_gaussian_command(before, record, None)
    unchanged(destination, before, pixels, history)
    unchanged(original, source_before, source_pixels, source_history)


TARGET_CASES = [
    ("H3.T01_shape_length", "positions", ("target", "source_shape"), [2, 3, 5, 7, 9]),
    ("H3.T02_shape_zero", "positions", ("target", "source_shape"), [2, 0, 5, 7, 9, 1]),
    ("H3.T03_shape_boolean", "positions", ("target", "source_shape"), [True, 3, 5, 7, 9, 1]),
    ("H3.T04_shape_fraction", "positions", ("target", "source_shape"), [2.5, 3, 5, 7, 9, 1]),
    ("H3.T05_shape_position_conflict", "positions", ("target", "source_shape"), [2, 1, 5, 7, 9, 1]),
    ("H3.T06_positions_empty", "positions", ("target", "positions"), []),
    ("H3.T07_positions_duplicate", "positions", ("target", "positions"), [[1, 2, 4], [1, 2, 4]]),
    ("H3.T08_position_original_bound", "positions", ("target", "positions"), [[2, 1, 3]]),
    ("H3.T09_position_negative", "positions", ("target", "positions"), [[-1, 1, 3]]),
    ("H3.T10_position_length", "positions", ("target", "positions"), [[1, 2]]),
    ("H3.T11_position_boolean", "positions", ("target", "positions"), [[True, 2, 4]]),
    ("H3.T12_positions_and_ranges", "positions", ("target", "ranges"), [[0, 1, 1]]),
    ("H3.T13_scope_unknown", "positions", ("target", "scope"), "current_plane"),
    ("H3.T14_document_zero", "positions", ("target", "document_id"), 0),
    ("H3.T15_revisions_length", "positions", ("target", "source_revisions"), [0, 0]),
    ("H3.T16_revisions_boolean", "positions", ("target", "source_revisions"), [True, 0, 0]),
    ("H3.T17_roi_unknown", "positions", ("target", "roi_policy"), "ignore"),
    ("H3.T18_volume_operation", "positions", ("target", "execution_mode"), "volume"),
    ("H3.T19_destination_unknown", "positions", ("target", "destination"), "replace_any"),
    ("H3.T20_target_unknown", "positions", ("target", "unknown"), True),
    ("H3.T21_range_count", "ranges", ("target", "ranges"), [[0, 2, 1]]),
    ("H3.T22_range_triplet", "ranges", ("target", "ranges"), [[0, 2], [1, 3, 1], [1, 5, 2]]),
    ("H3.T23_range_original_bound", "ranges", ("target", "ranges"), [[0, 3, 1], [1, 3, 1], [1, 5, 2]]),
    ("H3.T24_range_empty", "ranges", ("target", "ranges"), [[1, 1, 1], [1, 3, 1], [1, 5, 2]]),
    ("H3.T25_range_step_zero", "ranges", ("target", "ranges"), [[0, 2, 0], [1, 3, 1], [1, 5, 2]]),
    ("H3.T26_ranges_and_positions", "ranges", ("target", "positions"), [[1, 2, 4]]),
]

GAUSSIAN_CASES = [
    ("H3.G01_schema_boolean", "positions", ("schema_version",), True),
    ("H3.G02_schema_version", "positions", ("schema_version",), 2),
    ("H3.G03_operation_generic", "positions", ("operation",), "gaussian_blur"),
    ("H3.G04_root_unknown", "positions", ("unknown",), True),
    ("H3.G05_effective_conflict", "positions", ("effective_sigma_xy",), [1.0, 4.0]),
    ("H3.G06_effective_length", "positions", ("effective_sigma_xy",), [4.0]),
    ("H3.G07_effective_boolean", "positions", ("effective_sigma_xy",), [True, 1.0]),
    ("H3.G08_sigma_boolean", "positions", ("options", "sigma"), True),
    ("H3.G09_sigma_negative", "positions", ("options", "sigma"), -1.0),
    ("H3.G10_sigma_nonfinite", "positions", ("options", "sigma"), float("inf")),
    ("H3.G11_sigma_lossy_integer", "positions", ("options", "sigma"), 2**53 + 1),
    ("H3.G12_scaled_boolean_type", "positions", ("options", "scaled_units"), 1),
    ("H3.G13_raw_effective_conflict", "positions", ("options", "sigma"), 3.0),
    ("H3.G14_options_unknown", "positions", ("options", "unknown"), True),
    ("H3.G15_spacing_zero", "positions", ("calibration", "spacing"), [0.0, 2.0, 3.0]),
    ("H3.G16_spacing_length", "positions", ("calibration", "spacing"), [0.5, 2.0]),
    ("H3.G17_direction_invalid", "positions", ("calibration", "direction"), [0] * 9),
    ("H3.G18_origin_length", "positions", ("calibration", "origin"), [11.0, -7.0]),
    ("H3.G19_frame_interval_zero", "positions", ("calibration", "frame_interval"), 0.0),
    ("H3.G20_calibration_unknown", "positions", ("calibration", "unknown"), True),
]


def reject_all(original, destination, record, target_only):
    saved = copy.deepcopy(record)
    with pytest.raises((ValueError, IndexError)):
        ij.validate_gaussian_command_record(record)
    with pytest.raises((ValueError, IndexError)):
        ij.rebind_gaussian_command(destination.snapshot(), record, destination_request())
    with pytest.raises((ValueError, IndexError)):
        ij.gaussian_command_from_record(original.snapshot(), record)
    if target_only:
        with pytest.raises((ValueError, IndexError)):
            ij.target_from_record(original.snapshot(), record["target"], "gaussian_blur_imagej")
    assert record == saved


@pytest.mark.parametrize("case", TARGET_CASES + GAUSSIAN_CASES, ids=lambda case: case[0])
def test_H3_P05_complete_original_record_is_validated(case):
    original, source_pixels = owner()
    destination, destination_pixels = owner(True)
    source_before, before = original.snapshot(), destination.snapshot()
    source_history, history = original.undo_steps, destination.undo_steps
    _, scope, path, value = case
    request = (original_request() if scope == "positions" else ij.TargetRequest(
        ij.TargetScope.RANGES, t=ij.AxisRange(0, 2), c=ij.AxisRange(1, 3),
        z=ij.AxisRange(1, 5, 2)))
    record = command(original, request=request).record()
    node = record
    for key in path[:-1]:
        node = node[key]
    node[path[-1]] = value
    reject_all(original, destination, record, case[0].startswith("H3.T"))
    unchanged(original, source_before, source_pixels, source_history)
    unchanged(destination, before, destination_pixels, history)


REQUIRED_FIELDS = [
    (container, field)
    for container, fields in [
        ("", ["schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"]),
        ("options", ["sigma", "scaled_units"]),
        ("calibration", ["spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit"]),
        ("target", ["schema_version", "document_id", "source_revisions", "source_shape", "scope", "ranges", "positions", "roi_policy", "execution_mode", "destination"]),
    ]
    for field in fields
]


@pytest.mark.parametrize("container,field", REQUIRED_FIELDS,
                         ids=[f"H3.Missing/{container}/{field}" for container, field in REQUIRED_FIELDS])
def test_H3_P06_all_schema_fields_are_required(container, field):
    original, source_pixels = owner()
    destination, destination_pixels = owner(True)
    a, b = original.snapshot(), destination.snapshot()
    ah, bh = original.undo_steps, destination.undo_steps
    record = command(original).record()
    del (record[container] if container else record)[field]
    reject_all(original, destination, record, container == "target")
    unchanged(original, a, source_pixels, ah)
    unchanged(destination, b, destination_pixels, bh)


def test_H3_P07_offline_validation_does_not_prove_original_source_existence():
    original, _ = owner()
    record = command(original).record()
    del original
    gc.collect()
    saved = copy.deepcopy(record)
    destination, pixels = owner(True)
    before, history = destination.snapshot(), destination.undo_steps
    assert ij.validate_gaussian_command_record(record) is None
    assert ij.rebind_gaussian_command(before, record, destination_request()).parameters() == {"sigma": [8.0, 1.0]}
    detached = copy.deepcopy(record)
    detached["target"]["document_id"] = 2**63 + 17
    detached["target"]["source_revisions"] = [9, 8, 7]
    detached["target"]["source_shape"] = [2, 3, 5, 11, 13, 1]
    assert ij.validate_gaussian_command_record(detached) is None
    assert ij.rebind_gaussian_command(before, detached, destination_request()).sigma_x == 1
    with pytest.raises(ValueError):
        ij.gaussian_command_from_record(before, detached)
    assert record == saved
    unchanged(destination, before, pixels, history)


def test_H3_P08_strict_target_consistency_and_legacy_numeric_records():
    original, pixels = owner()
    before, history = original.snapshot(), original.undo_steps
    record = command(original).record()
    restored = ij.gaussian_command_from_record(before, record)
    target = ij.target_from_record(before, record["target"], "gaussian_blur_imagej")
    assert target.record() == restored.target.record()
    assert positions(target) == [(1, 2, 4), (0, 1, 2)]
    generic = {"sigma": 1, "border": "reflect"}
    imagej_numeric = {"sigma": [2.0, 2.0]}
    generic_saved, imagej_saved = copy.deepcopy(generic), copy.deepcopy(imagej_numeric)
    assert ij.validate_operation_record("gaussian_blur", generic) is None
    assert ij.validate_operation_record("gaussian_blur_imagej", imagej_numeric) is None
    defaults = ij.operation_defaults("gaussian_blur", ui=False)
    assert defaults["sigma"] == 1 and defaults["border"] == "reflect"
    assert generic == generic_saved and imagej_numeric == imagej_saved
    unchanged(original, before, pixels, history)
