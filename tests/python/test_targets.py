"""Native resolved positions, replay records and ROI policies."""

import copy
import json

import imagejxx as ij
import numpy as np
import pytest


def owner():
    return ij.Document(ij.Dataset(np.zeros((2, 3, 5, 4, 6), np.uint16), axes="TCZYX"))


def positions(target):
    return [(p.t, p.c, p.z) for p in map(target.position, range(target.position_count))]


def test_captured_positions_survive_navigation_and_record_round_trip():
    document = owner()
    view = document.snapshot().display
    view.t, view.c, view.crosshair = 1, 2, [0, 0, 3]
    document.set_display(view)
    target = ij.resolve_target(document.snapshot(), operation="add")
    assert positions(target) == [(1, 2, 3)]
    record = json.loads(json.dumps(target.record()))
    view.t, view.c, view.crosshair = 0, 0, [0, 0, 4]
    document.set_display(view)
    target.validate_source(document.snapshot())
    restored = ij.target_from_record(document.snapshot(), record, "add")
    assert positions(restored) == [(1, 2, 3)]
    assert restored.record() == record
    assert target.source.id == document.id
    assert target.source.shares_data(document.snapshot())
    assert document.undo_steps == 0 and not document.snapshot().dirty


def test_ranges_are_copied_and_unmentioned_axes_capture_current_position():
    document = owner()
    view = document.snapshot().display
    view.t, view.c = 1, 2
    document.set_display(view)
    request = ij.TargetRequest(ij.TargetScope.RANGES, z=ij.AxisRange(1, 5, 2))
    target = ij.resolve_target(document.snapshot(), request)
    request.z = ij.AxisRange(0, 2)
    assert positions(target) == [(1, 2, 1), (1, 2, 3)]
    exposed_ranges = target.ranges
    exposed_ranges[2].start = 0
    assert target.ranges[2].start == 1
    assert target.contains(ij.PlanePosition(1, 2, 3))
    assert not target.contains(ij.PlanePosition(1, 1, 3))
    with pytest.raises(AttributeError):
        target.destination = ij.OutputDestination.NEW_DOCUMENT
    with pytest.raises(IndexError):
        target.position(2)


def test_explicit_positions_keep_order_and_destination():
    document = owner()
    request = ij.TargetRequest(
        ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 2, 4), ij.PlanePosition(0, 1, 2)],
        destination=ij.OutputDestination.NEW_DOCUMENT,
    )
    target = ij.resolve_target(document.snapshot(), request)
    record = target.record()
    assert record["ranges"] == []
    assert positions(target) == [(1, 2, 4), (0, 1, 2)]
    assert target.destination == ij.OutputDestination.NEW_DOCUMENT
    view = document.snapshot().display
    view.crosshair = [1, 2, 3]
    document.set_display(view)
    assert ij.target_from_record(document.snapshot(), record).record() == record


@pytest.mark.parametrize("all_slices", [False, True])
def test_legacy_slice_adapter_keeps_scientific_tc(all_slices):
    document = owner()
    view = document.snapshot().display
    view.t, view.c = 1, 2
    document.set_display(view)
    target = ij.resolve_legacy_target(document.snapshot(), all_slices, 2, "add")
    assert positions(target) == ([(1, 2, z) for z in range(5)] if all_slices else [(1, 2, 2)])
    with pytest.raises(IndexError):
        ij.resolve_legacy_target(document.snapshot(), all_slices, 5)


@pytest.mark.parametrize(
    "scope,kwargs",
    [
        (ij.TargetScope.CURRENT_PLANE, {"z": ij.AxisRange()}),
        (ij.TargetScope.ALL_Z, {"t": ij.AxisRange()}),
        (ij.TargetScope.RANGES, {"positions": [ij.PlanePosition()]}),
        (ij.TargetScope.POSITIONS, {}),
        (ij.TargetScope.POSITIONS, {"positions": [ij.PlanePosition(), ij.PlanePosition()]}),
        (ij.TargetScope.POSITIONS, {"positions": [ij.PlanePosition(z=5)]}),
        (ij.TargetScope.RANGES, {"z": ij.AxisRange(-1, 3)}),
        (ij.TargetScope.RANGES, {"z": ij.AxisRange(0, 6)}),
        (ij.TargetScope.RANGES, {"z": ij.AxisRange(3, 3)}),
        (ij.TargetScope.RANGES, {"z": ij.AxisRange(0, 5, 0)}),
        (ij.TargetScope.RANGES, {"z": ij.AxisRange(0, 5, -1)}),
        (ij.TargetScope.RANGES, {"z": ij.AxisRange(0, 5, 2), "mode": ij.ExecutionMode.VOLUME}),
        (ij.TargetScope.POSITIONS, {"positions": [ij.PlanePosition(z=0), ij.PlanePosition(z=2)], "mode": ij.ExecutionMode.VOLUME}),
    ],
)
def test_invalid_targets_fail_without_a_document_edit(scope, kwargs):
    document = owner()
    before = document.snapshot()
    with pytest.raises((ValueError, IndexError)):
        ij.resolve_target(before, ij.TargetRequest(scope, **kwargs))
    assert before.shares_data(document.snapshot())
    assert document.undo_steps == 0 and not document.snapshot().dirty


def test_volume_and_unknown_methods_rejected_before_execution():
    source = owner().snapshot()
    request = ij.TargetRequest(ij.TargetScope.ALL_Z, mode=ij.ExecutionMode.VOLUME)
    with pytest.raises(ValueError, match="volume execution"):
        ij.resolve_target(source, request, "gaussian_blur")
    target = ij.resolve_target(source, request)
    with pytest.raises(ValueError, match="volume execution"):
        target.validate_operation("rank_filter")
    with pytest.raises(ValueError):
        ij.resolve_target(source, operation="missing")


def test_membership_foreign_sources_and_stale_target_publication():
    document = owner()
    app = ij.Application()
    app.adopt(document)
    target = app.resolve_target(operation="add")
    edit = target.begin_edit()
    edit.replace_plane(np.ones((4, 6), np.uint16))
    app.commit(edit)
    assert app.history().undo_steps == 1
    with pytest.raises(ij.RevisionConflict):
        app.validate_target(target)
    with pytest.raises(ij.RevisionConflict):
        ij.target_from_record(document.snapshot(), target.record())
    current = app.resolve_target()
    with pytest.raises(ValueError, match="another document"):
        current.validate_source(owner().snapshot())
    app.close(discard=True)
    with pytest.raises(ij.DocumentUnavailable):
        app.validate_target(current)
    current.validate_source(document.snapshot())


def test_masks_preserve_holes_and_positioned_unions():
    document = owner()
    hole = np.ones((4, 6), np.uint8)
    hole[1:3, 2:4] = 0
    edit = document.begin_edit()
    edit.select(ij.Selection.from_mask(hole))
    first = ij.ROI("first", ij.Selection("rectangle", [(0, 0), (2, 2)]), t=1, c=2, z=4)
    second = ij.ROI("second", ij.Selection("rectangle", [(1, 1), (3, 3)]), t=1, c=2, z=4)
    edit.set_rois([first, second])
    document.commit(edit)
    selected = ij.resolve_target(document.snapshot())
    np.testing.assert_array_equal(selected.mask(), hole * 255)
    request = ij.TargetRequest(
        ij.TargetScope.POSITIONS,
        positions=[ij.PlanePosition(1, 2, 4), ij.PlanePosition()],
        roi=ij.RoiPolicy.POSITIONED_ROIS,
    )
    target = ij.resolve_target(document.snapshot(), request)
    expected = np.zeros((4, 6), np.uint8)
    expected[:2, :2] = expected[1:3, 1:3] = 255
    np.testing.assert_array_equal(target.mask(0), expected)
    np.testing.assert_array_equal(target.mask(1), 0)
    whole = ij.resolve_target(document.snapshot(), ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE))
    assert whole.mask() is None
    document.select(None)
    whole.validate_source(document.snapshot())
    with pytest.raises(ij.RevisionConflict):
        selected.validate_source(document.snapshot())


@pytest.mark.parametrize(
    "field,value",
    [
        ("schema_version", True), ("schema_version", 2),
        ("document_id", 0), ("scope", "current_plane"),
        ("source_shape", [2, 3, 5]), ("source_shape", [2, 3, 5, 4, 7, 1]),
        ("source_revisions", [0, 0]), ("source_revisions", [0.0, 0, 0]),
        ("ranges", [[0, 1, True], [0, 1, 1], [0, 1, 1]]),
        ("ranges", [[0, 1, 1], [0, 1, 1], [0, 2**63, 1]]),
        ("positions", [[0, 0, 0]]), ("roi_policy", "missing"),
        ("execution_mode", "missing"), ("destination", "missing"),
        ("unknown", 1),
    ],
)
def test_malformed_record_is_rejected(field, value):
    document = owner()
    record = ij.resolve_target(document.snapshot()).record()
    record[field] = value
    with pytest.raises((ValueError, ij.RevisionConflict)):
        ij.target_from_record(document.snapshot(), record)
    assert document.undo_steps == 0


def test_record_requires_fields_and_does_not_accept_ranges_with_positions():
    source = owner().snapshot()
    record = ij.resolve_target(source).record()
    for field in record:
        incomplete = copy.deepcopy(record)
        del incomplete[field]
        with pytest.raises(ValueError):
            ij.target_from_record(source, incomplete)
    record["scope"] = "positions"
    record["positions"] = [[0, 0, 0]]
    with pytest.raises(ValueError, match="cannot contain ranges"):
        ij.target_from_record(source, record)
