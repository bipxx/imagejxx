"""Owned analytic fixtures for the calibrated, source-bound Gaussian command.

These are Python API and executor checks, not substitutes for the separately
pinned ImageJ numerical corpus or the native lazy-source read-count tests.
"""

import copy
import json
from concurrent.futures import ThreadPoolExecutor

import imagejxx as ij
import numpy as np
import pytest


def owner(pixels=None, axes="YX", calibration=None):
    if pixels is None:
        pixels = np.arange(17 * 23, dtype=np.uint16).reshape(17, 23)
    kwargs = {} if calibration is None else {"calibration": calibration}
    return ij.Document(ij.Dataset(pixels, axes=axes, **kwargs), "Gaussian source")


def positions(target):
    return [(p.t, p.c, p.z) for p in
            (target.position(i) for i in range(target.position_count))]


def calibrated_owner():
    calibration = ij.SpatialCalibration(
        spacing=(0.5, 2, 3), origin=(11, -7, 5),
        direction=(0, -1, 0, 1, 0, 0, 0, 0, 1), unit="µm",
        frame_interval=0.25, time_origin=8, time_unit="min")
    return owner(calibration=calibration)


def resolved(document, *, sigma=2, scaled_units=False, request=None):
    return ij.resolve_gaussian_command(
        document.snapshot(),
        target_request=ij.TargetRequest() if request is None else request,
        options=ij.GaussianBlurCommandOptions(sigma, scaled_units))


def test_defaults_and_canonical_python_parameter_shape():
    document = owner()
    options = ij.GaussianBlurCommandOptions()
    assert options.sigma == 2 and options.scaled_units is False
    command = ij.resolve_gaussian_command(document.snapshot())
    assert isinstance(command, ij.ResolvedGaussianBlurCommand)
    assert command.sigma_x == command.sigma_y == 2
    assert command.scaled_units_available is False
    assert type(command.parameters()) is dict
    assert command.parameters() == {"sigma": [2.0, 2.0]}
    assert type(command.parameters()["sigma"]) is list
    assert positions(command.target) == [(0, 0, 0)]
    assert document.undo_steps == 0 and not document.snapshot().dirty


def test_mutable_options_are_captured_and_returned_copies_stay_isolated():
    document = calibrated_owner()
    options = ij.GaussianBlurCommandOptions(2, True)
    command = ij.resolve_gaussian_command(document.snapshot(), options=options)
    options.sigma, options.scaled_units = 9, False
    returned = command.options
    returned.sigma, returned.scaled_units = 0, False
    assert command.options.sigma == 2 and command.options.scaled_units is True
    assert (command.sigma_x, command.sigma_y) == (4, 1)
    assert command.parameters() == {"sigma": [1.0, 4.0]}
    parameters = command.parameters()
    parameters["sigma"][0] = 999
    target = command.target
    target.ranges[2].start = 99
    assert command.parameters() == {"sigma": [1.0, 4.0]}
    assert command.target.ranges[2].start == 0
    assert command.options is not command.options
    assert command.target is not command.target
    assert command.calibration is not command.calibration


@pytest.mark.parametrize("name", ["options", "calibration", "target", "sigma_x",
                                 "sigma_y", "scaled_units_available"])
def test_resolved_properties_are_readonly(name):
    command = resolved(owner())
    with pytest.raises(AttributeError):
        setattr(command, name, getattr(command, name))


@pytest.mark.parametrize("value", [True, False, np.bool_(True), np.bool_(False)])
def test_boolean_sigma_constructor_and_mutation_are_rejected(value):
    with pytest.raises(TypeError, match="Boolean"):
        ij.GaussianBlurCommandOptions(value)
    options = ij.GaussianBlurCommandOptions(3)
    with pytest.raises(TypeError, match="Boolean"):
        options.sigma = value
    assert options.sigma == 3


@pytest.mark.parametrize("value,error", [(-1, ValueError), (np.nan, ValueError),
                                        (np.inf, ValueError), (-np.inf, ValueError),
                                        ("2", TypeError), (1j, TypeError)])
def test_invalid_sigma_is_rejected_without_changing_options(value, error):
    with pytest.raises(error):
        ij.GaussianBlurCommandOptions(value)
    options = ij.GaussianBlurCommandOptions(3)
    with pytest.raises(error):
        options.sigma = value
    assert options.sigma == 3


@pytest.mark.parametrize("value", [np.int64(2), np.float32(0.5), np.float64(1.5)])
def test_real_numpy_scalar_sigmas_are_supported(value):
    command = resolved(owner(), sigma=value)
    assert command.sigma_x == command.sigma_y == float(value)


@pytest.mark.parametrize("unit,available", [("px", False), ("pixel", False),
                                           ("pixels", False), ("µm", True)])
def test_scaled_units_availability_and_raw_intent(unit, available):
    calibration = ij.SpatialCalibration(spacing=(0.5, 2, 3), unit=unit)
    document = owner(calibration=calibration)
    assert ij.gaussian_scaled_units_available(calibration) is available
    command = resolved(document, scaled_units=True)
    assert command.scaled_units_available is available
    assert command.options.scaled_units is True
    assert command.record()["options"] == {"sigma": 2.0, "scaled_units": True}
    assert (command.sigma_x, command.sigma_y) == ((4, 1) if available else (2, 2))


def test_full_calibration_json_roundtrip_and_detached_record():
    document = calibrated_owner()
    command = resolved(document, scaled_units=True)
    record = command.record()
    assert set(record) == {"schema_version", "operation", "options", "calibration",
                           "effective_sigma_xy", "target"}
    assert record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["effective_sigma_xy"] == [4.0, 1.0]
    assert record["calibration"] == {
        "spacing": [0.5, 2.0, 3.0], "origin": [11.0, -7.0, 5.0],
        "direction": [0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0],
        "unit": "µm", "frame_interval": 0.25, "time_origin": 8.0,
        "time_unit": "min"}
    decoded = ij.gaussian_command_from_record(
        document.snapshot(), json.loads(json.dumps(record)))
    assert decoded.record() == record and decoded.parameters() == command.parameters()
    record["options"]["sigma"] = 77
    record["calibration"]["origin"][0] = 999
    record["target"]["source_shape"][0] = 100
    assert command.record() == decoded.record()


@pytest.mark.parametrize("sigma,spacing", [(1e308, (1, 1, 1)),
                                         (1, (1e-308, 1, 1))])
def test_effective_sigma_overflow_is_metadata_preflight(sigma, spacing):
    document = owner(calibration=ij.SpatialCalibration(spacing=spacing, unit="µm"))
    before = document.snapshot()
    with pytest.raises(ValueError):
        resolved(document, sigma=sigma, scaled_units=True)
    assert before.shares_data(document.snapshot()) and document.undo_steps == 0


@pytest.mark.parametrize("dtype,samples", [(np.float64, 1), (np.uint8, 4),
                                          (np.uint16, 3), (np.float32, 3)])
def test_unsupported_source_metadata_fails_before_an_edit(dtype, samples):
    pixels = np.zeros((5, 7) if samples == 1 else (5, 7, samples), dtype)
    document = owner(pixels, "YX" if samples == 1 else "YXS")
    before = document.snapshot()
    with pytest.raises(ValueError):
        resolved(document)
    assert before.shares_data(document.snapshot()) and document.undo_steps == 0
    assert document.snapshot().dataset.numpy("YX" if samples == 1 else "YXS").tobytes() == pixels.tobytes()


def test_volume_and_invalid_position_requests_are_preflight_failures():
    document = owner(np.zeros((3, 5, 7), np.uint8), "ZYX")
    before = document.snapshot()
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, mode=ij.ExecutionMode.VOLUME)
    with pytest.raises(ValueError):
        resolved(document, request=request)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS,
                               positions=[ij.PlanePosition(z=3)])
    with pytest.raises(IndexError):
        resolved(document, request=request)
    assert before.shares_data(document.snapshot()) and document.undo_steps == 0


@pytest.mark.parametrize("path,value", [
    (("schema_version",), True), (("schema_version",), 2),
    (("operation",), "gaussian_blur"), (("unknown",), 1),
    (("options", "sigma"), True), (("options", "sigma"), -1),
    (("options", "scaled_units"), 1), (("options", "unknown"), False),
    (("calibration", "spacing"), [0, 2, 3]),
    (("calibration", "origin"), [12, -7, 5]),
    (("calibration", "direction"), [1, 0, 0, 0, 1, 0, 0, 0, 1]),
    (("calibration", "unknown"), "extra"),
    (("effective_sigma_xy",), [1, 4]),
    (("target", "document_id"), 0),
    (("target", "source_shape"), [1, 1, 1, 17, 24, 1]),
    (("target", "source_revisions"), [99, 99, 99]),
    (("target", "unknown"), 1),
])
def test_record_tampering_rejected_without_source_or_history_mutation(path, value):
    document = calibrated_owner()
    before = document.snapshot()
    record = resolved(document, scaled_units=True).record()
    node = record
    for key in path[:-1]:
        node = node[key]
    node[path[-1]] = value
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.gaussian_command_from_record(before, record)
    assert before.shares_data(document.snapshot()) and document.undo_steps == 0


def test_records_require_all_root_option_and_calibration_fields():
    document = calibrated_owner()
    record = resolved(document, scaled_units=True).record()
    for path in [()] + [("options",), ("calibration",)]:
        node = record if not path else record[path[0]]
        for field in node:
            incomplete = copy.deepcopy(record)
            target = incomplete if not path else incomplete[path[0]]
            del target[field]
            with pytest.raises(ValueError):
                ij.gaussian_command_from_record(document.snapshot(), incomplete)


@pytest.mark.parametrize("integer", [2**53 + 1, 2**70])
def test_record_numeric_precision_cannot_silently_round(integer):
    document = owner()
    record = resolved(document).record()
    record["options"]["sigma"] = integer
    with pytest.raises(ValueError):
        ij.gaussian_command_from_record(document.snapshot(), record)


def test_records_are_source_bound_and_rebind_is_explicit():
    document = owner()
    command = resolved(document, sigma=1)
    record = command.record()
    other = owner()
    with pytest.raises(ValueError):
        ij.gaussian_command_from_record(other.snapshot(), record)
    rebound = ij.resolve_gaussian_command(other.snapshot(), options=command.options)
    assert rebound.target.document_id == other.id
    assert rebound.parameters() == command.parameters()
    edit = document.begin_edit()
    edit.replace_plane(np.zeros((17, 23), np.uint16))
    document.commit(edit)
    with pytest.raises(ij.RevisionConflict):
        command.target.validate_source(document.snapshot())
    with pytest.raises(ij.RevisionConflict):
        ij.gaussian_command_from_record(document.snapshot(), record)


def test_calibration_edits_invalidate_record_and_captured_target():
    document = calibrated_owner()
    command = resolved(document, scaled_units=True)
    edit = document.begin_edit()
    edit.set_calibration(ij.SpatialCalibration(spacing=(1, 2, 3), unit="µm"))
    document.commit(edit)
    with pytest.raises(ValueError):
        ij.gaussian_command_from_record(document.snapshot(), command.record())
    with pytest.raises(ij.RevisionConflict):
        command.target.validate_source(document.snapshot())
    assert command.calibration.spacing == [0.5, 2, 3]
    assert (command.sigma_x, command.sigma_y) == (4, 1)
    assert (resolved(document, scaled_units=True).sigma_x,
            resolved(document, scaled_units=True).sigma_y) == (2, 1)


def test_whole_plane_region_is_explicit_and_ignores_active_selection():
    document = owner()
    assert ij.gaussian_command_region(resolved(document).target) == {"extent": None, "mask": None}
    document.select(ij.Selection("rectangle", [(2, 3), (7, 9)]))
    request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    assert ij.gaussian_command_region(resolved(document, request=request).target) == {"extent": None, "mask": None}


@pytest.mark.parametrize("kind,points", [
    ("line", [(1, 1), (4, 3)]), ("polyline", [(1, 1), (2, 3), (4, 2)]),
    ("angle", [(1, 1), (2, 3), (4, 2)]), ("point", [(2, 2)]),
    ("multipoint", [(1, 1), (4, 3)]),
])
def test_non_area_selections_mean_whole_plane(kind, points):
    document = owner()
    document.select(ij.Selection(kind, points))
    before = document.snapshot()
    history = document.undo_steps
    assert ij.gaussian_command_region(resolved(document).target) == {"extent": None, "mask": None}
    assert before.shares_data(document.snapshot()) and document.undo_steps == history


@pytest.mark.parametrize("points,extent", [
    ([(-3, 2), (9, 13)], (0, 2, 9, 11)),
    ([(97, 43), (118, 62)], (97, 43, 21, 19)),
])
def test_rectangular_geometry_and_full_image_mask(points, extent):
    document = owner(np.zeros((101, 173), np.uint8))
    document.select(ij.Selection("rectangle", points))
    region = ij.gaussian_command_region(resolved(document, sigma=12).target)
    x, y, width, height = extent
    expected = np.zeros((101, 173), np.uint8)
    expected[y:y + height, x:x + width] = 255
    assert region["extent"] == extent
    assert region["mask"].dtype == np.uint8
    np.testing.assert_array_equal(region["mask"], expected)


def test_polygon_geometry_extent_does_not_shrink_to_mask_occupancy():
    document = owner()
    document.select(ij.Selection("polygon", [(2.2, 1.4), (9.1, 1.4), (2.2, 8.4)]))
    region = ij.gaussian_command_region(resolved(document, sigma=12).target)
    assert region["extent"] == (2, 1, 8, 8)
    ys, xs = np.nonzero(region["mask"])
    assert (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1) == (2, 1, 9, 8)
    assert region["mask"].dtype == np.uint8
    assert set(np.unique(region["mask"])) == {0, 255}


def test_rotated_box_retains_original_geometry_bounds():
    document = owner()
    document.select(ij.Selection("rotated_rectangle", [(8.1, 6.2), (11.1, 8.2), (7.1, 7.7)]))
    region = ij.gaussian_command_region(resolved(document, sigma=12).target)
    assert region["extent"] == (4, 2, 9, 8)
    assert region["mask"].shape == (17, 23) and region["mask"].dtype == np.uint8
    assert np.count_nonzero(region["mask"]) > 0
    assert np.count_nonzero(region["mask"]) < 9 * 8


@pytest.mark.parametrize("empty", [False, True])
def test_packed_mask_extent_holes_and_empty_identity(empty):
    document = owner()
    mask = np.zeros((17, 23), np.uint8)
    if not empty:
        mask[3:8, 5:11] = 1
        mask[4:7, 6:10] = 0
    document.select(ij.Selection.from_mask(mask))
    region = ij.gaussian_command_region(resolved(document).target)
    assert region["extent"] == ((0, 0, 0, 0) if empty else (5, 3, 6, 5))
    np.testing.assert_array_equal(region["mask"], mask * 255)


def test_positioned_rois_union_unmatched_empty_and_non_area_override():
    document = owner(np.zeros((3, 17, 23), np.uint8), "ZYX")
    edit = document.begin_edit()
    edit.set_rois([
        ij.ROI("first", ij.Selection("rectangle", [(1, 2), (4, 5)]), z=1),
        ij.ROI("second", ij.Selection("rectangle", [(8, 6), (11, 9)]), z=1),
        ij.ROI("line", ij.Selection("line", [(2, 3), (5, 6)]), z=2)])
    document.commit(edit)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(z=1), ij.PlanePosition(z=0), ij.PlanePosition(z=2)],
        roi=ij.RoiPolicy.POSITIONED_ROIS)
    command = resolved(document, request=request)
    region = ij.gaussian_command_region(command.target, 0)
    expected = np.zeros((17, 23), np.uint8)
    expected[2:5, 1:4] = expected[6:9, 8:11] = 255
    assert region["extent"] == (1, 2, 10, 7)
    np.testing.assert_array_equal(region["mask"], expected)
    unmatched = ij.gaussian_command_region(command.target, 1)
    assert unmatched["extent"] == (0, 0, 0, 0)
    np.testing.assert_array_equal(unmatched["mask"], 0)
    assert ij.gaussian_command_region(command.target, 2) == {"extent": None, "mask": None}


def test_region_array_is_an_owned_copy_and_index_is_checked():
    document = owner()
    document.select(ij.Selection("rectangle", [(1, 2), (4, 5)]))
    command = resolved(document)
    region = ij.gaussian_command_region(command.target)
    region["mask"][:] = 0
    assert np.count_nonzero(ij.gaussian_command_region(command.target)["mask"]) == 9
    with pytest.raises(IndexError):
        ij.gaussian_command_region(command.target, command.target.position_count)


def test_analytic_two_pixel_preview_apply_one_undo_redo_and_replay():
    # At sigma 1, nearest-edge Gaussian weighting of [0,255] rounds to
    # [77,178]. These owned constants do not depend on another native call.
    pixels = np.array([[0, 255]], np.uint8)
    expected = np.array([[77, 178]], np.uint8)
    document = owner(pixels)
    application = ij.Application()
    application.adopt(document)
    command = resolved(document, sigma=1)
    before = document.snapshot()
    prepared = application.prepare("gaussian_blur_imagej", command.target, command.parameters())
    assert prepared.operation == "gaussian_blur_imagej"
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("YX"), expected)
    assert before.shares_data(document.snapshot()) and document.undo_steps == 0
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), pixels)
    result = application.publish(prepared)
    assert result.document is document and not result.created
    assert document.undo_steps == 1 and not prepared.active
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), expected)
    document.undo()
    assert document.undo_steps == 0 and document.redo_steps == 1
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), pixels)
    document.redo()
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), expected)
    assert document.undo_steps == 1 and document.redo_steps == 0
    fresh = owner(pixels)
    rebound = ij.resolve_gaussian_command(fresh.snapshot(), options=command.options)
    replay = ij.prepare_operation("gaussian_blur_imagej", rebound.target, rebound.parameters())
    np.testing.assert_array_equal(replay.preview.dataset.numpy("YX"), expected)
    assert fresh.undo_steps == 0
    np.testing.assert_array_equal(pixels, [[0, 255]])


@pytest.mark.parametrize("dtype,samples", [(np.uint16, 1), (np.uint8, 3)])
def test_analytic_integer_and_rgb_channels_are_preserved(dtype, samples):
    if samples == 1:
        pixels = np.array([[0, 255]], dtype)
        expected = np.array([[77, 178]], dtype)
    else:
        pixels = np.array([[[0, 255, 0], [255, 0, 255]]], dtype)
        expected = np.array([[[77, 178, 77], [178, 77, 178]]], dtype)
    axes = "YX" if samples == 1 else "YXS"
    document = owner(pixels, axes)
    command = resolved(document, sigma=1)
    prepared = ij.prepare_operation("gaussian_blur_imagej", command.target, command.parameters())
    result = prepared.preview.dataset.numpy(axes)
    assert result.dtype == np.dtype(dtype) and result.shape == pixels.shape
    np.testing.assert_array_equal(result, expected)
    assert document.snapshot().dataset.numpy(axes).tobytes() == pixels.tobytes()
    assert document.undo_steps == 0


@pytest.mark.parametrize("scope", ["current", "ranges"])
def test_czt_targets_remain_frozen_after_navigation(scope):
    pixels = np.broadcast_to(np.array([0, 255], np.uint8), (2, 2, 3, 1, 2)).copy()
    document = owner(pixels, "TCZYX")
    view = document.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [0, 0, 1]
    document.set_display(view)
    request = (ij.TargetRequest() if scope == "current" else
               ij.TargetRequest(ij.TargetScope.RANGES, z=ij.AxisRange(0, 3, 2)))
    command = resolved(document, sigma=1, request=request)
    expected_positions = [(1, 1, 1)] if scope == "current" else [(1, 1, 0), (1, 1, 2)]
    assert positions(command.target) == expected_positions
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 0]
    document.set_display(view)
    decoded = ij.gaussian_command_from_record(document.snapshot(), command.record())
    assert positions(decoded.target) == expected_positions
    expected = pixels.copy()
    for t, c, z in expected_positions:
        expected[t, c, z] = [77, 178]
    application = ij.Application()
    application.adopt(document)
    prepared = application.prepare("gaussian_blur_imagej", decoded.target, decoded.parameters())
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("TCZYX"), expected)
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), pixels)
    application.publish(prepared)
    assert document.undo_steps == 1
    assert (document.snapshot().display.t, document.snapshot().display.c,
            document.snapshot().display.crosshair[2]) == (0, 0, 0)
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), expected)
    document.undo()
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("TCZYX"), pixels)


def test_analytic_impulse_mask_restores_hole_and_every_exterior_pixel():
    pixels = np.zeros((7, 7), np.uint8)
    pixels[3, 3] = 255
    mask = np.zeros((7, 7), np.uint8)
    mask[2:5, 2:5] = 1
    mask[2, 3] = 0
    # Independent sigma-1 Gaussian impulse rounding: center 41, axial
    # neighbors 25, diagonal neighbors 15. The omitted axial cell stays 0.
    expected = np.zeros_like(pixels)
    expected[2:5, 2:5] = [[15, 0, 15], [25, 41, 25], [15, 25, 15]]
    document = owner(pixels)
    document.select(ij.Selection.from_mask(mask))
    application = ij.Application()
    application.adopt(document)
    before = document.snapshot()
    history = document.undo_steps
    command = resolved(document, sigma=1)
    assert ij.gaussian_command_region(command.target)["extent"] == (2, 2, 3, 3)
    prepared = application.prepare("gaussian_blur_imagej", command.target, command.parameters())
    preview = prepared.preview.dataset.numpy("YX")
    np.testing.assert_array_equal(preview, expected)
    assert preview[mask == 0].tobytes() == pixels[mask == 0].tobytes()
    assert before.shares_data(document.snapshot()) and document.undo_steps == history
    application.publish(prepared)
    assert document.undo_steps == history + 1
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), expected)
    document.undo()
    np.testing.assert_array_equal(document.snapshot().dataset.numpy("YX"), pixels)


@pytest.mark.parametrize("dtype,samples", [(np.uint8, 1), (np.uint16, 1),
                                          (np.float32, 1), (np.uint8, 3)])
def test_sigma_zero_is_bit_identity_without_a_history_step(dtype, samples):
    pixels = np.arange(5 * 7 * samples, dtype=dtype).reshape(
        (5, 7) if samples == 1 else (5, 7, samples))
    if dtype == np.float32:
        pixels[0, 0], pixels[0, 1] = -0.0, -1.5
    axes = "YX" if samples == 1 else "YXS"
    document = owner(pixels, axes)
    application = ij.Application()
    application.adopt(document)
    command = resolved(document, sigma=0)
    before = document.snapshot()
    prepared = application.prepare("gaussian_blur_imagej", command.target, command.parameters())
    assert prepared.preview.dataset.numpy(axes).tobytes() == pixels.tobytes()
    application.publish(prepared)
    assert before.shares_data(document.snapshot())
    assert document.undo_steps == 0 and not document.snapshot().dirty
    assert document.snapshot().dataset.numpy(axes).tobytes() == pixels.tobytes()


def test_empty_roi_skips_work_without_a_new_history_step():
    pixels = np.array([[0, 255]], np.uint8)
    document = owner(pixels)
    document.select(ij.Selection.from_mask(np.zeros_like(pixels)))
    application = ij.Application()
    application.adopt(document)
    before = document.snapshot()
    history = document.undo_steps
    command = resolved(document, sigma=12)
    prepared = application.prepare("gaussian_blur_imagej", command.target, command.parameters())
    assert len(prepared.outputs) == 1 and prepared.outputs[0].skipped
    assert prepared.preview.dataset.numpy("YX").tobytes() == pixels.tobytes()
    application.publish(prepared)
    assert before.shares_data(document.snapshot()) and document.undo_steps == history
    assert document.snapshot().dataset.numpy("YX").tobytes() == pixels.tobytes()


def test_background_prepare_new_document_and_precancel_are_atomic():
    pixels = np.array([[0, 255]], np.uint8)
    document = owner(pixels)
    application = ij.Application()
    application.adopt(document)
    command = resolved(document, sigma=1, request=ij.TargetRequest(
        destination=ij.OutputDestination.NEW_DOCUMENT))
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        application.prepare("gaussian_blur_imagej", command.target,
                            command.parameters(), cancellation=token)
    assert document.undo_steps == 0
    with ThreadPoolExecutor(max_workers=1) as pool:
        prepared = pool.submit(application.prepare, "gaussian_blur_imagej",
                               command.target, command.parameters()).result()
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("YX"), [[77, 178]])
    result = application.publish(prepared, title="Gaussian derived")
    assert result.created and result.document_id != document.id
    assert result.document.undo_steps == 1
    np.testing.assert_array_equal(result.document.snapshot().dataset.numpy("YX"), [[77, 178]])
    assert document.undo_steps == 0
    assert document.snapshot().dataset.numpy("YX").tobytes() == pixels.tobytes()
    result.document.undo()
    np.testing.assert_array_equal(result.document.snapshot().dataset.numpy("YX"), pixels)
