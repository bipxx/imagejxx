"""Shared native dataset preparation, targeting and atomic publication."""

from concurrent.futures import ThreadPoolExecutor

import imagejxx as ij
import numpy as np
import pytest


def controller(pixels=None, axes="TCZYX"):
    if pixels is None:
        pixels = np.arange(2 * 3 * 4 * 5 * 6, dtype=np.uint16).reshape(2, 3, 4, 5, 6)
    owner = ij.Document(ij.Dataset(pixels, axes=axes), "Source")
    app = ij.Application()
    app.adopt(owner)
    return app, owner, pixels


def test_scientific_ranges_preview_apply_replay_undo_and_navigation():
    app, owner, pixels = controller()
    request = ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(1, 2),
                               c=ij.AxisRange(0, 3, 2), z=ij.AxisRange(1, 4, 2))
    target = app.resolve_target(request, operation="add")
    expected = pixels.copy()
    expected[1, ::2, 1::2] += 9
    original = owner.snapshot()
    events = []
    prepared = app.prepare("add", target, {"value": 9}, progress=lambda done, total: events.append((done, total)))
    assert events == [(i, 4) for i in range(5)]
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("TCZYX"), expected)
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0
    assert prepared.preview.data_identity is None and prepared.active
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 0, 1, [2, 3, 0]
    owner.set_display(view)
    published = app.publish(prepared)
    assert published.document is owner and not published.created
    assert published.document_id == owner.id and owner.undo_steps == 1
    assert owner.snapshot().display.c == 1 and not prepared.active
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("TCZYX"), expected)
    with pytest.raises(ValueError, match="already published"):
        app.publish(prepared)
    with pytest.raises(ValueError, match="already published"):
        prepared.begin_edit()
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), pixels)
    fresh = app.resolve_target(request)
    restored = ij.target_from_record(owner.snapshot(), fresh.record(), "add")
    replay = ij.prepare_operation("add", restored, prepared.parameters)
    np.testing.assert_array_equal(replay.preview.dataset.numpy("TCZYX"), expected)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_irregular_roi_preserves_holes_and_untargeted_positions(dtype):
    pixels = np.arange(3 * 5 * 6, dtype=dtype).reshape(3, 5, 6)
    app, owner, _ = controller(pixels, "ZYX")
    mask = np.zeros((5, 6), np.uint8)
    mask[1:4, 1:5] = 1
    mask[2, 2:4] = 0
    owner.select(ij.Selection.from_mask(mask))
    request = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(z=1)])
    prepared = app.prepare("add", app.resolve_target(request), {"value": 5})
    expected = pixels.copy()
    expected[1][mask != 0] += 5
    app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), expected)
    assert owner.undo_steps == 1
    owner.undo()
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), pixels)


def test_positioned_roi_unions_skip_unmatched_positions():
    app, owner, pixels = controller()
    edit = owner.begin_edit()
    edit.set_rois([ij.ROI("first", ij.Selection("rectangle", [(0, 0), (2, 2)]), t=1, c=2, z=3),
                   ij.ROI("second", ij.Selection("rectangle", [(1, 1), (3, 3)]), t=1, c=2, z=3)])
    owner.commit(edit)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 2, 3), ij.PlanePosition(0, 0, 0)],
        roi=ij.RoiPolicy.POSITIONED_ROIS)
    prepared = app.prepare("add", app.resolve_target(request), {"value": 4})
    expected = pixels.copy()
    mask = np.zeros((5, 6), bool)
    mask[:2, :2] = mask[1:3, 1:3] = True
    expected[1, 2, 3][mask] += 4
    assert not prepared.outputs[0].skipped and prepared.outputs[1].skipped
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("TCZYX"), expected)
    app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), expected)


@pytest.mark.parametrize("output_dtype", ["uint8", "float32", "float64"])
def test_partial_type_conversion_preserves_outside_numeric_values(output_dtype):
    pixels = np.full((3, 5, 6), 900, np.uint16)
    app, owner, _ = controller(pixels, "ZYX")
    mask = np.zeros((5, 6), np.uint8)
    mask[1:3, 2:4] = 1
    owner.select(ij.Selection.from_mask(mask))
    target = app.resolve_target()
    prepared = app.prepare("convert", target, {"dtype": output_dtype})
    result = prepared.preview.dataset.numpy("ZYX")
    assert result[0, 0, 0] == 900 and result[2, 2, 2] == 900
    expected_value = 255 if output_dtype == "uint8" else 900
    assert result[0, 1, 2] == expected_value
    assert result.dtype == (np.uint16 if output_dtype == "uint8" else np.dtype(output_dtype))
    assert owner.snapshot().dataset.numpy("ZYX").dtype == np.uint16
    app.publish(prepared)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), result)


def test_full_conversion_honors_output_type_and_sample_changes_require_full_coverage():
    app, owner, _ = controller(np.full((3, 5, 6), 900, np.uint16), "ZYX")
    prepared = app.prepare("convert", app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z)), {"dtype": "uint8"})
    assert prepared.preview.dataset.numpy("ZYX").dtype == np.uint8
    np.testing.assert_array_equal(prepared.preview.dataset.numpy("ZYX"), 255)
    color = np.zeros((2, 5, 6, 3), np.uint8)
    color[..., 0] = 100
    app, owner, _ = controller(color, "ZYXS")
    with pytest.raises(ValueError, match="every plane"):
        app.prepare("grayscale", app.resolve_target())
    assert owner.undo_steps == 0
    gray = app.prepare("grayscale", app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z)))
    assert gray.preview.dataset.numpy("ZYX").shape == (2, 5, 6)


def test_new_document_owner_is_shared_and_source_is_preserved():
    app, owner, pixels = controller()
    request = ij.TargetRequest(destination=ij.OutputDestination.NEW_DOCUMENT)
    prepared = app.prepare("add", app.resolve_target(request), {"value": 3})
    result = app.publish(prepared, title="Derived")
    assert result.created and result.document_id != owner.id
    assert result.document.id == result.document_id and result.document.undo_steps == 1
    assert result.document.snapshot().metadata["title"] == "Derived"
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("TCZYX"), pixels)
    expected = pixels.copy()
    expected[0, 0, 0] += 3
    np.testing.assert_array_equal(result.document.snapshot().dataset.numpy("TCZYX"), expected)
    assert app.active_document_id == result.document_id
    result.document.undo()
    np.testing.assert_array_equal(result.document.snapshot().dataset.numpy("TCZYX"), pixels)


def test_stale_closed_foreign_and_frontend_rejected_publication_are_atomic():
    app, owner, _ = controller()
    prepared = app.prepare("add", app.resolve_target(), {"value": 1})
    edit = owner.begin_edit()
    edit.set_title("Changed")
    owner.commit(edit)
    before = owner.snapshot()
    with pytest.raises(ij.RevisionConflict):
        app.publish(prepared)
    assert owner.snapshot().shares_data(before) and prepared.active
    foreign, other, _ = controller()
    with pytest.raises(ij.DocumentUnavailable):
        foreign.publish(prepared)
    current = app.prepare("add", app.resolve_target(), {"value": 1})
    app.close(discard=True)
    with pytest.raises(ij.DocumentUnavailable):
        app.publish(current)
    np.testing.assert_array_equal(current.preview.dataset.plane(coordinate=0), before.dataset.plane(coordinate=0) + 1)
    assert other.undo_steps == 0


def test_cancel_and_progress_failures_publish_neither_pixels_nor_results():
    app, owner, _ = controller()
    target = app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z))
    token = ij.CancellationToken()
    original = owner.snapshot()
    events = []
    def progress(done, total):
        events.append((done, total))
        if done == 2:
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        app.prepare("add", target, {"value": 1}, cancellation=token, progress=progress)
    assert events == [(0, 4), (1, 4), (2, 4)]
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0
    token = ij.CancellationToken()
    prepared = app.prepare("add", target, {"value": 1}, cancellation=token)
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        app.publish(prepared)
    assert owner.snapshot().shares_data(original) and prepared.active
    def broken(done, total):
        raise RuntimeError("callback failed")
    with pytest.raises(RuntimeError, match="callback failed"):
        app.prepare("add", target, progress=broken)
    assert owner.snapshot().shares_data(original)


def test_metadata_parameter_failures_volume_and_geometry_preflight():
    app, owner, _ = controller()
    before = owner.snapshot()
    target = app.resolve_target()
    for operation, params in [("add", {"unknown": 1}), ("convolve", {}), ("rotate90", {})]:
        with pytest.raises(ValueError):
            app.prepare(operation, target, params)
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, mode=ij.ExecutionMode.VOLUME)
    with pytest.raises(ValueError, match="volume"):
        app.prepare("gaussian_blur", app.resolve_target(request))
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0


def test_frozen_parameters_options_and_background_preparation():
    app, owner, pixels = controller()
    target = app.resolve_target()
    params = {"kernel": np.eye(3)}
    options = ij.ExecutionOptions()
    prepared = app.prepare("convolve", target, params, options=options)
    expected = prepared.preview.dataset.plane(coordinate=0)
    params["kernel"][:] = 0
    options.dataset = ij.DatasetPolicy.LEGACY_DESKTOP
    returned = prepared.parameters
    returned["kernel"][0][0] = 99
    assert prepared.parameters["kernel"][0][0] == 1
    assert prepared.options.dataset == ij.DatasetPolicy.PRESERVE_UNTARGETED
    np.testing.assert_array_equal(prepared.preview.dataset.plane(coordinate=0), expected)
    with ThreadPoolExecutor(max_workers=1) as pool:
        background = pool.submit(app.prepare, "add", target, {"value": 2}).result()
    app.publish(background)
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(coordinate=0), pixels[0, 0, 0] + 2)


def test_noop_and_empty_roi_leave_history_and_dirty_state_unchanged():
    app, owner, _ = controller()
    prepared = app.prepare("add", app.resolve_target(), {"value": 0})
    app.publish(prepared)
    assert owner.undo_steps == 0 and not owner.snapshot().dirty
    prepared = app.prepare("add", app.resolve_target(ij.TargetRequest(roi=ij.RoiPolicy.POSITIONED_ROIS)), {"value": 7})
    assert prepared.outputs[0].skipped
    app.publish(prepared)
    assert owner.undo_steps == 0 and not owner.snapshot().dirty


def test_component_outputs_are_separate_and_roi_counts_are_restricted():
    pixels = np.zeros((2, 5, 6), np.uint8)
    pixels[:, 1, 1] = pixels[:, 3, 4] = 255
    app, owner, _ = controller(pixels, "ZYX")
    owner.select(ij.Selection("rectangle", [(0, 0), (3, 3)]))
    prepared = app.prepare("label", app.resolve_target())
    assert prepared.outputs[0].label_image and prepared.outputs[0].label_count is None
    prepared = app.prepare("label", app.resolve_target(), {"return_num": True})
    assert prepared.outputs[0].label_image and prepared.outputs[0].label_count == 1
    assert owner.undo_steps == 0
    published = app.publish(prepared)
    assert published.outputs[0].label_count == 1 and owner.undo_steps == 1
    assert published.document.snapshot().dataset.plane(coordinate=0)[3, 4] == 255


@pytest.mark.parametrize("operation,parameters,expected_count", [
    ("label", {}, None),
    ("label", {"return_num": False}, None),
    ("label", {"return_num": True}, 0),
    ("binary_watershed", {}, None),
])
def test_skipped_positioned_roi_outputs_respect_optional_counts(operation, parameters, expected_count):
    app, owner, _ = controller()
    original = owner.snapshot()
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.POSITIONED_ROIS)
    prepared = app.prepare(operation, app.resolve_target(request), parameters)
    assert len(prepared.outputs) == 4
    assert all(output.skipped and output.label_image and output.label_count == expected_count
               for output in prepared.outputs)
    published = app.publish(prepared)
    assert all(output.label_count == expected_count for output in published.outputs)
    assert owner.snapshot().shares_data(original) and owner.undo_steps == 0
    assert not owner.snapshot().dirty
