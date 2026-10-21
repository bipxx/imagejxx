"""P06g's twelve public Python contracts, with test-owned literal geometry.

These are API calls, including A12's desktop forwards, never physical input.
The installed gate must verify real package/module origins and impose its
declared per-case/parent deadlines. Missing required bindings fail, not skip.

A11 requires the separately compiled, test-only public PlaneSource fixture at
IMAGEJXX_SAVED_ROI_PROVIDER. It counts and rejects real plane/strip/window
acquisitions; neither product hooks nor Python method replacements supply it.
"""

import base64
from copy import deepcopy
import json
import math
import struct
import zlib

import imagejxx as ij
import numpy as np
import pytest

from saved_roi_geometry_observations import RawCase


EXPECTED_COLLECTION_IDS = (
    "A01.typed_request_detached_values",
    "A02.area_geometry_and_row_preservation",
    "A03.bool_lossy_overflow_unknown_refusal",
    "A04.private_prepare_no_mutation_and_exact_noop",
    "A05.atomic_active_owner_capture",
    "A06.stale_all_data_and_tcz",
    "A07.pointer_display_numeric_camera",
    "A08.strict_record_replay_and_stale_repeat",
    "A09.full_rebind_invalid_original",
    "A10.reopen_owner_and_native_v3",
    "A11.control_limits_callback_and_zero_reads",
    "A12.desktop_binding_wrapper_same_controller_recording",
)
EXPECTED_COLLECTION_COUNT = 12
SHAPE = (2, 3, 3, 10, 12, 1)
CALIBRATION = {
    "spacing": [0.5, 1.25, 2.5], "origin": [11.0, -7.0, 19.0],
    "direction": [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
    "unit": "um", "frame_interval": 0.75, "time_origin": 2.25,
    "time_unit": "s",
}
ATTRIBUTES = {
    "stroke_color": "#2878b8", "fill_color": "none",
    "future": {"version": 9, "values": [True, "opaque"], "unsigned": 2**64 - 1},
}
RING = (
    "000000000000", "000000000000", "001111111100", "001111111100",
    "001100001100", "001100001100", "001111111100", "001111111100",
    "000000000000", "000000000000",
)
MOVED_RING = (
    "000000000000", "000111111110", "000111111110", "000110000110",
    "000110000110", "000111111110", "000111111110", "000000000000",
    "000000000000", "000000000000",
)
ROTATED_RING = (
    "000000000000", "000111111000", "000111111000", "000110011000",
    "000110011000", "000110011000", "000110011000", "000111111000",
    "000111111000", "000000000000",
)
CLIPPED_RING = (
    "000000000000", "000000000000", "000000011111", "000000011111",
    "000000011000", "000000011000", "000000011111", "000000011111",
    "000000000000", "000000000000",
)


def _styled(kind="rectangle", points=((2, 2), (8, 7))):
    return ij.Selection(kind, points, 2.5, attributes=deepcopy(ATTRIBUTES))


def _replacement():
    return _styled(points=((3, 1), (9, 6)))


def _bitmap(rows):
    assert len(rows) == 10 and all(len(row) == 12 for row in rows)
    return np.array([[255 if bit == "1" else 0 for bit in row] for row in rows], np.uint8)


def _mask(rows):
    raw = ij.Selection.from_mask(_bitmap(rows))
    # Public fixture setup changes style only. Expected mask memberships remain
    # the independently authored rows, never SelectionEditor's geometry output.
    raw = raw.with_attributes(deepcopy(ATTRIBUTES))
    styled = ij.SelectionEditor(raw, (10, 12)).properties(2.5)
    assert styled.width == 2.5 and styled.attributes == ATTRIBUTES
    np.testing.assert_array_equal(styled.mask((10, 12)), _bitmap(rows))
    return styled


def _pixels(dtype):
    t, c, z, y, x = np.indices(SHAPE[:-1])
    values = 1000*t + 100*c + 10*z + 2*y + x
    if np.dtype(dtype) == np.dtype(np.uint8):
        values = values % 256
    elif np.dtype(dtype) == np.dtype(np.float32):
        values = values / 4
    return values.astype(dtype)[..., None]


def _fixture(dtype=np.uint16, *, target=None, spectrum=True, dataset=None,
             desktop_admitted=False):
    target = _styled() if target is None else target
    pixels = _pixels(dtype)
    if dataset is None:
        dataset = ij.Dataset(pixels, axes="TCZYXS",
                             calibration=ij.SpatialCalibration(**CALIBRATION))
    seed = ij.Document(dataset, "Scientific saved ROI")
    transaction = seed.begin_edit()
    transaction.set_property("unrelated", {"v": 7, "nested": [None, False, -0.0]})
    transaction.set_extension("future", ["retained", False])
    transaction.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
    transaction.set_previous_selection(ij.Selection("line", [(0, 9), (11, 0)], 3))
    transaction.set_rois([
        ij.ROI("target", target, attributes={"row": "first"}),
        ij.ROI("explicit zero", _styled("ellipse", ((2, 2), (8, 8))),
               t=0, c=0, z=0, attributes={"row": 2}),
        ij.ROI("mixed", _styled("polygon", ((2, 2), (8, 2), (8, 7), (2, 7))),
               t=1, z=2, attributes={"row": 3}),
    ])
    transaction.set_overlays([{
        "kind": "rectangle", "points": [[0, 0], [1, 1]],
        "t": 0, "c": 0, "z": 0, "slice_index": 0,
        "version": 37, "opaque": [3, 1, 4],
    }])
    if spectrum:
        transaction.set_spectrum(ij.FourierState(np.array([[3 + 4j]], np.complex128),
                                                original_shape=(1, 1)))
    seed.commit(transaction)
    # Public snapshot construction creates a clean fresh owner, rather than
    # manufacturing revisions or deleting history through a private hook.
    owner = ij.Document(seed.snapshot())
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 2, [6, 5, 2]
    view.orthogonal, view.zoom, view.pan = True, 1.5, [[2, 3], [0, 0], [0, 0]]
    # Core cases preserve opaque Display metadata. The desktop positive fixture
    # starts with supported nontrivial display state under its existing policy.
    view.properties = ({"display_range": [0.0, 65535.0], "lut": "gray"}
                       if desktop_admitted else {"unrelated": "display"})
    owner.set_display(view)
    transaction = owner.begin_edit()
    transaction.set_extension("temporary", True)
    owner.commit(transaction)
    owner.undo()
    owner.mark_saved()
    assert owner.undo_steps == 0 and owner.redo_steps == 1 and not owner.snapshot().dirty
    app = ij.Application()
    app.adopt(owner)
    return app, owner, pixels


def _revisions(snapshot):
    return tuple(getattr(snapshot.revisions, key)
                 for key in ("pixels", "metadata", "selection", "display"))


def _display(snapshot):
    view = snapshot.display
    return (view.t, view.c, view.crosshair, view.orthogonal, view.linked_zoom,
            view.zoom, view.plane_zoom, view.pan, view.properties, view.revision)


def _selection(selection):
    if selection is None:
        return None
    result = {"kind": selection.kind, "points": selection.points,
              "width": selection.width, "attributes": selection.attributes}
    if selection.kind == "mask":
        result["mask_bytes"] = selection.mask((10, 12)).tobytes()
    return result


def _roi(row):
    return {"name": row.name, "selection": _selection(row.selection),
            "t": row.t, "c": row.c, "z": row.z, "attributes": row.attributes}


def _unchanged(owner, before, history):
    after = owner.snapshot()
    assert after.shares_data(before)
    assert _revisions(after) == _revisions(before)
    assert _display(after) == _display(before)
    assert after.dirty == before.dirty
    assert (owner.undo_steps, owner.redo_steps) == history


def _unchanged_value(owner, before, history):
    after = owner.snapshot()
    return (after.shares_data(before) and _revisions(after) == _revisions(before) and
            _display(after) == _display(before) and after.dirty == before.dirty and
            (owner.undo_steps, owner.redo_steps) == history)


def _point_text(selection):
    return ";".join(",".join(format(value, ".17g") for value in point)
                    for point in selection.points)


def _unrelated(before, after):
    assert after.dataset.shares_pixels(before.dataset)
    assert after.dataset.dtype == before.dataset.dtype and after.dataset.shape == SHAPE
    assert after.dataset.sample_policy == before.dataset.sample_policy
    np.testing.assert_array_equal(after.dataset.numpy("TCZYXS"), before.dataset.numpy("TCZYXS"))
    calibration = after.dataset.calibration
    for name, expected in CALIBRATION.items():
        assert getattr(calibration, name) == expected
    assert after.metadata == before.metadata and after.record_layout == before.record_layout
    assert _selection(after.selection) == _selection(before.selection)
    assert _selection(after.previous_selection) == _selection(before.previous_selection)
    assert after.overlays == before.overlays
    if before.spectrum is None:
        assert after.spectrum is None
    else:
        assert after.spectrum.shares_storage(before.spectrum)
        assert after.spectrum.original_shape == before.spectrum.original_shape
        np.testing.assert_array_equal(after.spectrum.numpy(), [[3 + 4j]])


def _resolve(app, replacement=None, row=0, **controls):
    request = ij.SavedRoiGeometryRequest(row, _replacement() if replacement is None else replacement)
    return ij.resolve_saved_roi_geometry(app.snapshot(), request, **controls)


def _prepare(app, command=None, **controls):
    command = _resolve(app) if command is None else command
    capture = app.capture_saved_roi_geometry()
    return app.prepare_saved_roi_geometry(capture, command, **controls)


def _publish(app, command=None, **controls):
    prepared = _prepare(app, command, **controls)
    assert prepared.active and prepared.retained_working_bytes > 0
    result = app.publish_saved_roi_geometry(prepared)
    assert not prepared.active and prepared.retained_working_bytes == 0
    return result


def _changed(app, owner, replacement, *, row=0):
    before = owner.snapshot()
    history, redo = owner.undo_steps, owner.redo_steps
    command = _resolve(app, replacement, row)
    prepared = _prepare(app, command)
    assert prepared.record() == command.record()
    _unchanged(owner, before, (history, redo))
    result = app.publish_saved_roi_geometry(prepared)
    after = owner.snapshot()
    assert result.document_id == owner.id and result.changed is True
    assert not prepared.active and prepared.retained_working_bytes == 0
    assert _revisions(after) == tuple(a + d for a, d in zip(_revisions(before), (0, 1, 1, 0)))
    assert owner.undo_steps == history + 1 and owner.redo_steps == 0 and after.dirty
    assert _display(after) == _display(before)
    _unrelated(before, after)
    expected = [_roi(value) for value in before.rois]
    expected[row]["selection"] = _selection(replacement)
    assert [_roi(value) for value in after.rois] == expected
    owner.undo()
    assert [_roi(value) for value in owner.snapshot().rois] == [_roi(value) for value in before.rois]
    assert not owner.snapshot().dirty
    owner.redo()
    assert [_roi(value) for value in owner.snapshot().rois] == expected
    return command.record()


def _navigate(owner, t, c, z):
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = t, c, [6, 5, z]
    owner.set_display(view)


def _a01(tmp_path, observed):
    del tmp_path
    app, owner, _ = _fixture()
    before = owner.snapshot()
    request = ij.SavedRoiGeometryRequest(np.int64(0), _replacement())
    limits = ij.SavedRoiGeometryLimits()
    command = ij.resolve_saved_roi_geometry(before, request, limits)
    request.row_index, request.replacement = 2, _styled("line", ((0, 0), (1, 1)))
    limits.max_working_bytes = 1
    assert command.row_index == 0 and command.replacement == _replacement()
    assert command.position == (1, 2, 2) and not command.is_replay
    assert command.source.shares_data(before)
    assert command.limits.max_working_bytes == 268435456
    detached = command.limits
    detached.max_working_bytes = 1
    assert command.limits.max_working_bytes == 268435456
    attributes = command.replacement.attributes
    attributes["future"]["values"][0] = False
    original = command.original_roi.attributes
    original["row"] = "mutated"
    record = command.record()
    record["replacement"]["points"][0][0] = -999
    record["original"]["roi"]["attributes"]["row"] = "mutated"
    assert command.replacement.attributes == ATTRIBUTES
    assert command.original_roi.attributes == {"row": "first"}
    assert command.record()["replacement"]["points"] == [[3.0, 1.0], [9.0, 6.0]]
    capture = app.capture_saved_roi_geometry()
    assert capture.document_id == owner.id and capture.source.shares_data(before)
    with pytest.raises(TypeError):
        ij.SavedRoiGeometryCapture(owner.id)
    with pytest.raises(TypeError):
        ij.PreparedSavedRoiGeometryEdit()
    _unchanged(owner, before, (0, 1))
    observed.scalar("saved_roi.rectangle.moved_points", _point_text(command.replacement))
    observed.scalar("saved_roi.geometry.preserved_width", format(command.replacement.width, ".17g"))
    observed.scalar("saved_roi.geometry.attributes_preserved",
                    command.replacement.attributes == before.rois[0].selection.attributes)
    observed.scalar("saved_roi.position.wildcard_constraint_count",
                    sum(value is not None for value in
                        (command.original_roi.t, command.original_roi.c, command.original_roi.z)))
    observed.scalar("saved_roi.entry.global_selection_changes",
                    owner.snapshot().revisions.selection - before.revisions.selection)
    observed.scalar("saved_roi.target.row_attributes_preserved",
                    command.original_roi.attributes == before.rois[0].attributes)


def _a02(tmp_path, observed):
    del tmp_path
    literal_pairs = [
        (_styled(), _replacement()),
        (_styled("ellipse", ((2, 2), (8, 8))), _styled("ellipse", ((1, 3), (7, 9)))),
        (_styled("polygon", ((2, 2), (8, 2), (8, 7), (2, 7))),
         _styled("polygon", ((2, 2), (9, 3), (8, 7), (2, 7)))),
        (_styled("freehand", ((2, 2), (8, 2), (8, 7), (2, 7))),
         _styled("freehand", ((2, 2), (9, 3), (8, 7), (2, 7)))),
    ]
    q = math.sqrt(3)
    for kind in ("rotated_rectangle", "rotated_ellipse"):
        literal_pairs.append((_styled(kind, ((6, 5), (6 + 1.5*q, 6.5), (5, 5 + q))),
                              _styled(kind, ((6, 5), (6, 8), (4, 5)))))
    literal_pairs.append((_mask(RING), _mask(MOVED_RING)))
    for dtype in (np.uint8, np.uint16, np.float32):
        for original, replacement in literal_pairs:
            app, owner, _ = _fixture(dtype, target=original)
            _changed(app, owner, replacement)
            if replacement.kind == "mask":
                np.testing.assert_array_equal(owner.snapshot().rois[0].selection.mask((10, 12)),
                                              _bitmap(MOVED_RING))
    # Explicit zero and wildcard positions are different values and visibility
    # is enforced for interactive/full-rebind resolution.
    app, owner, _ = _fixture()
    with pytest.raises(ValueError) as hidden:
        _resolve(app, _styled("ellipse", ((1, 3), (7, 9))), row=1)
    observed.scalar("saved_roi.entry.hidden_fresh_refused", isinstance(hidden.value, ValueError))
    _navigate(owner, 0, 0, 0)
    zero = _resolve(app, _styled("ellipse", ((1, 3), (7, 9))), row=1).record()
    assert [zero["original"]["roi"][key] for key in ("t", "c", "z")] == [0, 0, 0]
    observed.scalar("saved_roi.position.explicit_zero_tcz",
                    ",".join(str(zero["original"]["roi"][key]) for key in ("t", "c", "z")))
    _navigate(owner, 1, 0, 2)
    mixed = _resolve(app, _styled("polygon", ((2, 2), (9, 3), (8, 7), (2, 7))), row=2)
    assert mixed.position == (1, 0, 2)
    assert [mixed.record()["original"]["roi"][key] for key in ("t", "c", "z")] == [1, None, 2]

    # Exercise the public native editor; complete memberships come from these
    # owned literal rows, never from a second product geometry calculation.
    editor = ij.SelectionEditor(_mask(RING), (10, 12))
    for name, candidate, literal, count in (
        ("translated", editor.moved((1, -1), keep_inside=False), MOVED_RING, 40),
        ("rotated", editor.rotated(90, (6, 5)), ROTATED_RING, 40),
        ("clipped", editor.moved((5, 0), keep_inside=False), CLIPPED_RING, 24),
    ):
        np.testing.assert_array_equal(candidate.mask((10, 12)), _bitmap(literal))
        assert candidate.width == 2.5 and candidate.attributes == ATTRIBUTES
        app, owner, _ = _fixture(target=_mask(RING))
        _changed(app, owner, candidate)
        actual_mask = owner.snapshot().rois[0].selection.mask((10, 12))
        np.testing.assert_array_equal(actual_mask, _bitmap(literal))
        actual_count = int(np.count_nonzero(actual_mask))
        assert actual_count == count
        observed.scalar("saved_roi.mask." + name + "_members", actual_count)
    empty = _mask(("0" * 12,) * 10)
    handles = ij.SelectionEditor(empty, (10, 12)).handles()
    assert handles == []
    observed.scalar("saved_roi.mask.empty_handle_count", len(handles))

    app, owner, _ = _fixture()
    before = owner.snapshot()
    undo_before = owner.undo_steps
    numeric = ij.SelectionEditor(_styled(), (10, 12)).box_geometry((6, 5), (4, 4), 0)
    assert numeric == _styled(points=((4, 3), (8, 7)))
    assert _publish(app, _resolve(app, numeric)).changed
    after = owner.snapshot()
    assert after.rois[0].selection == numeric
    assert _revisions(after) == tuple(value + delta for value, delta in zip(_revisions(before), (0, 1, 1, 0)))
    assert owner.undo_steps == 1 and owner.redo_steps == 0 and after.dirty
    _unrelated(before, after)
    observed.scalar("saved_roi.rectangle.numeric_points", _point_text(after.rois[0].selection))
    for field in ("metadata", "selection", "pixels"):
        observed.scalar("saved_roi.changed." + field + "_revision_delta",
                        getattr(after.revisions, field) - getattr(before.revisions, field))
    observed.scalar("saved_roi.changed.undo_steps_delta", owner.undo_steps - undo_before)
    observed.scalar("saved_roi.changed.redo_steps_after", owner.redo_steps)
    owner.undo()
    owner.redo()
    assert owner.snapshot().rois[0].selection == numeric
    assert owner.snapshot().revisions.pixels == before.revisions.pixels
    observed.scalar("saved_roi.undo_redo.pixels_revision_delta",
                    owner.snapshot().revisions.pixels - before.revisions.pixels)


def _a03(tmp_path, observed):
    del tmp_path
    app, owner, _ = _fixture()
    before = owner.snapshot()
    for invalid in (True, np.bool_(False), 0.0, 0.5, -1, 2**128, "0", None):
        with pytest.raises((TypeError, ValueError, OverflowError)):
            ij.SavedRoiGeometryRequest(invalid, _replacement())
    names = ("max_xy_pixels", "max_vertices", "max_working_bytes", "max_record_bytes", "metadata_depth")
    for name in names:
        for invalid in (True, np.bool_(True), 0, -1, 1.0, 1.5, 2**128):
            with pytest.raises((TypeError, ValueError, OverflowError)):
                ij.SavedRoiGeometryLimits(**{name: invalid})
            limits = ij.SavedRoiGeometryLimits()
            with pytest.raises((TypeError, ValueError, OverflowError)):
                setattr(limits, name, invalid)
    with pytest.raises(ValueError):
        ij.SavedRoiGeometryLimits(metadata_depth=129)
    record = _resolve(app).record()
    mutations = []
    for path, value in (
        (("version",), True), (("version",), 1.0), (("extra",), None),
        (("operation",), "select_roi"), (("source", "id"), True),
        (("source", "roi_count"), 0), (("source", "shape"), [2, 3, 3, 10, 12, True]),
        (("source", "shape"), [2, 3, 3, 10, 12, 2**64]),
        (("source", "sample_policy"), "unknown"), (("position", "t"), 0.0),
        (("position", "z"), 3), (("original", "index"), True),
        (("replacement", "kind"), "line"), (("replacement", "width"), True),
        (("replacement", "width"), 4.0), (("replacement", "extra"), False),
        (("replacement", "points"), [[True, 1], [9, 6]]),
        (("replacement", "points"), [[float("inf"), 1], [9, 6]]),
        (("replacement", "attributes"), {"changed": True}),
    ):
        bad = deepcopy(record)
        part = bad
        for key in path[:-1]:
            part = part[key]
        part[path[-1]] = value
        mutations.append(bad)
    missing = deepcopy(record)
    del missing["original"]["roi"]["t"]
    mutations.append(missing)
    for bad in mutations:
        with pytest.raises((TypeError, ValueError, OverflowError)):
            ij.validate_saved_roi_geometry_record(bad)
    for replacement in (_styled("line", ((0, 0), (1, 1))),
                        ij.Selection("rectangle", [(3, 1), (9, 6)], 4, attributes=ATTRIBUTES),
                        _replacement().with_attributes({"changed": True})):
        with pytest.raises(ValueError):
            _resolve(app, replacement)
    with pytest.raises(ValueError):
        _resolve(app, row=3)
    _unchanged(owner, before, (0, 1))
    observed.scalar("saved_roi.invalid_request.publication_delta",
                    owner.snapshot().revisions.metadata - before.revisions.metadata)


def _a04(tmp_path, observed):
    del tmp_path
    app, owner, _ = _fixture()
    before = owner.snapshot()
    prepared = _prepare(app)
    assert prepared.active and prepared.working_bytes > 0
    assert prepared.retained_working_bytes == prepared.working_bytes
    returned = prepared.record()
    returned["replacement"]["points"][0][0] = -100
    assert prepared.record()["replacement"]["points"][0] == [3.0, 1.0]
    _unchanged(owner, before, (0, 1))
    del prepared
    _unchanged(owner, before, (0, 1))
    for target in (_styled(), _mask(("0" * 12,) * 10)):
        app, owner, _ = _fixture(target=target)
        before = owner.snapshot()
        undo_before = owner.undo_steps
        no_op = _prepare(app, _resolve(app, target))
        result = app.publish_saved_roi_geometry(no_op)
        assert result.document_id == owner.id and result.changed is False
        assert not no_op.active and no_op.retained_working_bytes == 0
        _unchanged(owner, before, (0, 1))
        if target.kind == "rectangle":
            observed.scalar("saved_roi.noop.undo_steps_delta", owner.undo_steps - undo_before)
            observed.scalar("saved_roi.terminal.retained_working_bytes", no_op.retained_working_bytes)
        with pytest.raises(ValueError):
            app.publish_saved_roi_geometry(no_op)
        _unchanged(owner, before, (0, 1))


def _a05(tmp_path, observed):
    del tmp_path
    app, owner, _ = _fixture()
    other_id = app.create(ij.Dataset(np.zeros((10, 12), np.uint16), axes="YX"), "Other owner")
    app.activate(owner.id)
    before, members = owner.snapshot(), list(app.document_ids)
    prepared = _prepare(app)
    app.activate(other_id)
    app.activate(owner.id)
    with pytest.raises(ij.RevisionConflict):
        app.publish_saved_roi_geometry(prepared)
    assert not prepared.active and prepared.retained_working_bytes == 0
    assert app.document_ids == members and app.active_document_id == owner.id
    _unchanged(owner, before, (0, 1))
    observed.scalar("saved_roi.entry.owner_roundtrip_publication_delta",
                    owner.snapshot().revisions.metadata - before.revisions.metadata)
    capture, command = app.capture_saved_roi_geometry(), _resolve(app)
    foreign = ij.Application()
    foreign.adopt(owner)
    with pytest.raises(ij.DocumentUnavailable):
        foreign.prepare_saved_roi_geometry(capture, command)
    assert foreign.document_ids == [owner.id]
    _unchanged(owner, before, (0, 1))
    prepared = _prepare(app)
    app.close(owner.id, discard=True)
    with pytest.raises(ij.DocumentUnavailable):
        app.publish_saved_roi_geometry(prepared)
    assert not prepared.active and prepared.retained_working_bytes == 0
    assert not app.contains(owner.id) and app.document_ids == [other_id]
    _unchanged(owner, before, (0, 1))
    # A preparation callback runs outside controller locks, and cannot refresh
    # its own frozen capture by activating another owner and returning.
    app, owner, _ = _fixture()
    other_id = app.create(ij.Dataset(np.zeros((10, 12), np.uint16), axes="YX"))
    app.activate(owner.id)
    before = owner.snapshot()
    callbacks = []

    def checkpoint():
        callbacks.append(app.snapshot().id)
        app.activate(other_id)
        app.activate(owner.id)

    prepared = _prepare(app, checkpoint=checkpoint)
    assert callbacks and all(identity == owner.id for identity in callbacks)
    with pytest.raises(ij.RevisionConflict):
        app.publish_saved_roi_geometry(prepared)
    assert not prepared.active and prepared.retained_working_bytes == 0
    _unchanged(owner, before, (0, 1))


def _a06(tmp_path, observed):
    del tmp_path
    for mutation in ("pixels", "metadata", "selection", "row", "row_count", "t", "c", "z"):
        app, owner, _ = _fixture()
        prepared = _prepare(app)
        if mutation == "selection":
            owner.select(ij.Selection("ellipse", [(0, 0), (2, 2)]))
        elif mutation in {"t", "c", "z"}:
            position = {"t": (0, 2, 2), "c": (1, 0, 2), "z": (1, 2, 0)}[mutation]
            _navigate(owner, *position)
        else:
            transaction = owner.begin_edit()
            if mutation == "pixels":
                transaction.replace_plane(np.full((10, 12), 77, np.uint16), t=1, c=2, z=2)
            elif mutation == "metadata":
                transaction.set_property("new metadata", True)
            else:
                rows = owner.snapshot().rois
                if mutation == "row_count":
                    rows = rows[:2]
                else:
                    rows[0] = ij.ROI("renamed row", rows[0].selection,
                                     attributes=rows[0].attributes)
                transaction.set_rois(rows)
            owner.commit(transaction)
        guarded = owner.snapshot()
        history = (owner.undo_steps, owner.redo_steps)
        with pytest.raises(ij.RevisionConflict):
            app.publish_saved_roi_geometry(prepared)
        assert not prepared.active and prepared.retained_working_bytes == 0
        _unchanged(owner, guarded, history)
        if mutation == "pixels":
            observed.scalar("saved_roi.failure.native_state_unchanged",
                            _unchanged_value(owner, guarded, history))
    # Native contract observes final TCZ, not a fabricated navigation epoch.
    app, owner, _ = _fixture()
    prepared = _prepare(app)
    _navigate(owner, 0, 0, 0)
    _navigate(owner, 1, 2, 2)
    result = app.publish_saved_roi_geometry(prepared)
    assert result.changed and owner.snapshot().rois[0].selection == _replacement()


def _a07(tmp_path, observed):
    del tmp_path
    for interaction in (ij.SavedRoiGeometryInteraction.Pointer, ij.SavedRoiGeometryInteraction.Numeric):
        app, owner, _ = _fixture()
        before = owner.snapshot()
        prepared = _prepare(app, interaction=interaction)
        view = owner.snapshot().display
        view.zoom, view.pan, view.crosshair = 3.0, [[7, -4], [2, 1], [3, 4]], [8, 7, 2]
        owner.set_display(view)
        fresh = owner.snapshot()
        if interaction == ij.SavedRoiGeometryInteraction.Pointer:
            with pytest.raises(ij.RevisionConflict):
                app.publish_saved_roi_geometry(prepared)
            _unchanged(owner, fresh, (0, 1))
        else:
            result = app.publish_saved_roi_geometry(prepared)
            assert result.changed and _display(owner.snapshot()) == _display(fresh)
            _unrelated(before, owner.snapshot())
            assert owner.snapshot().rois[0].selection == _replacement()
            observed.scalar("saved_roi.numeric.fresh_camera_preserved",
                            _display(owner.snapshot()) == _display(fresh))
        assert not prepared.active and prepared.retained_working_bytes == 0
    app, owner, _ = _fixture()
    command = _resolve(app)
    view = owner.snapshot().display
    view.zoom = 4
    owner.set_display(view)
    capture = app.capture_saved_roi_geometry()
    callbacks = []
    with pytest.raises(ij.RevisionConflict):
        app.prepare_saved_roi_geometry(capture, command,
            interaction=ij.SavedRoiGeometryInteraction.Pointer,
            checkpoint=lambda: callbacks.append(True))
    assert callbacks == []
    fresh = owner.snapshot()
    prepared = app.prepare_saved_roi_geometry(capture, command,
        interaction=ij.SavedRoiGeometryInteraction.Numeric)
    assert app.publish_saved_roi_geometry(prepared).changed
    assert _display(owner.snapshot()) == _display(fresh)


def _a08(tmp_path, observed):
    del tmp_path
    app, owner, _ = _fixture()
    before = owner.snapshot()
    record = _resolve(app).record()
    assert set(record) == {"schema", "version", "operation", "source", "position", "original", "replacement"}
    assert record["schema"] == "imagejxx.saved-roi-geometry-command"
    assert record["version"] == 1 and type(record["version"]) is int
    assert record["operation"] == "replace_saved_roi_geometry"
    observed.scalar("saved_roi.record.operation", record["operation"])
    assert record["source"] == {
        "id": owner.id, "pixels_revision": before.revisions.pixels,
        "metadata_revision": before.revisions.metadata,
        "selection_revision": before.revisions.selection,
        "shape": list(SHAPE), "type": "uint16", "sample_policy": "finite_only", "roi_count": 3,
    }
    assert record["position"] == {"t": 1, "c": 2, "z": 2}
    assert record["original"] == {"index": 0, "roi": {
        "name": "target", "selection": {"kind": "rectangle", "points": [[2.0, 2.0], [8.0, 7.0]],
        "width": 2.5, "attributes": ATTRIBUTES},
        "t": None, "c": None, "z": None, "attributes": {"row": "first"},
    }}
    assert record["replacement"] == {"kind": "rectangle", "points": [[3.0, 1.0], [9.0, 6.0]],
                                      "width": 2.5, "attributes": ATTRIBUTES}
    ij.validate_saved_roi_geometry_record(record)
    owner.select(ij.Selection("ellipse", [(0, 0), (2, 2)]))
    _navigate(owner, 0, 0, 0)
    fresh = owner.snapshot()
    command = ij.saved_roi_geometry_from_record(fresh, record)
    assert command.is_replay and command.record() == record
    assert command.source.shares_data(fresh) and command.position == (0, 0, 0)
    assert command.source.revisions.selection != record["source"]["selection_revision"]
    assert _publish(app, command).changed
    assert owner.snapshot().rois[0].selection == _replacement()
    assert _selection(owner.snapshot().selection) == _selection(fresh.selection)
    assert _display(owner.snapshot()) == _display(fresh)
    after = owner.snapshot()
    observed.scalar("saved_roi.strict_replay.metadata_revision_delta",
                    after.revisions.metadata - fresh.revisions.metadata)
    observed.scalar("saved_roi.strict_replay.fresh_navigation_preserved", _display(after) == _display(fresh))
    with pytest.raises(ij.RevisionConflict):
        ij.saved_roi_geometry_from_record(after, record)
    _unchanged(owner, after, (1, 0))
    observed.scalar("saved_roi.strict_repeat.metadata_revision_delta",
                    owner.snapshot().revisions.metadata - after.revisions.metadata)


def _a09(tmp_path, observed):
    del tmp_path
    app, owner, _ = _fixture()
    source_before = owner.snapshot()
    original = _resolve(app).record()
    new_owner = ij.Document(owner.snapshot())
    destination = ij.Application()
    destination.adopt(new_owner)
    before = new_owner.snapshot()
    with pytest.raises(ij.RevisionConflict):
        ij.saved_roi_geometry_from_record(before, original)
    fresh_replacement = _styled(points=((4, 3), (8, 7)))
    complete = ij.SavedRoiGeometryRequest(0, fresh_replacement)
    rebound = ij.rebind_saved_roi_geometry(before, original, complete)
    assert not rebound.is_replay and rebound.source.id == new_owner.id != owner.id
    assert rebound.record()["source"]["id"] == new_owner.id
    assert rebound.replacement == fresh_replacement
    assert _publish(destination, rebound).changed
    assert new_owner.snapshot().rois[0].selection == fresh_replacement
    observed.scalar("saved_roi.full_rebind.metadata_revision_delta",
                    new_owner.snapshot().revisions.metadata - before.revisions.metadata)
    observed.scalar("saved_roi.rebind.explicit_target_preserved",
                    rebound.row_index == complete.row_index and rebound.replacement == complete.replacement and
                    rebound.source.id == new_owner.id)
    guarded = new_owner.snapshot()
    for index, malformed in enumerate(({**original, "unknown": True}, {**original, "version": True})):
        with pytest.raises(ValueError) as expected:
            ij.validate_saved_roi_geometry_record(malformed)
        callbacks = []
        with pytest.raises(ValueError) as actual:
            ij.rebind_saved_roi_geometry(object(), malformed, object(),
                                        checkpoint=lambda: callbacks.append(True))
        assert str(actual.value) == str(expected.value) and callbacks == []
        if index == 0:
            observed.scalar("saved_roi.malformed.callback_count", len(callbacks))
        with pytest.raises(ValueError) as actual:
            ij.saved_roi_geometry_from_record(object(), malformed)
        assert str(actual.value) == str(expected.value)
    with pytest.raises(TypeError):
        ij.rebind_saved_roi_geometry(before, original, {"row_index": 0, "replacement": fresh_replacement})
    _unchanged(owner, source_before, (0, 1))
    observed.scalar("saved_roi.malformed_original.publication_delta",
                    new_owner.snapshot().revisions.metadata - guarded.revisions.metadata)


def _decode_wire(data):
    assert 18 < len(data) <= 32 * 1024 * 1024 and data[:10] == b"IMAGEJXX3\n"
    length, = struct.unpack("<Q", data[10:18])
    assert 0 < length <= 32 * 1024 * 1024
    decoder = zlib.decompressobj()
    payload = decoder.decompress(data[18:], length + 1)
    assert len(payload) == length and decoder.eof and not decoder.unused_data
    return json.loads(payload)


def _a10(tmp_path, observed):
    # NativeV3 has no scientific Fourier position binding. Assert its existing
    # refusal without deleting that unrelated source state to get a green save.
    app, owner, _ = _fixture()
    before = owner.snapshot()
    callbacks = []
    with pytest.raises(ValueError):
        ij.encode_session(ij.SessionCapture([before]),
                          checkpoint=lambda: callbacks.append(True))
    assert callbacks == [] and owner.snapshot().spectrum.shares_storage(before.spectrum)
    _unchanged(owner, before, (0, 1))
    for dtype in (np.uint8, np.uint16, np.float32):
        app, owner, pixels = _fixture(dtype, spectrum=False)
        original = _resolve(app).record()
        assert _publish(app).changed
        before = owner.snapshot()
        path = tmp_path / (np.dtype(dtype).name + "-saved-area.ijx")
        capture = ij.SessionCapture([before], results=[{"value": 7}], workflow=[original],
                                    extensions={"future": [None, True, 9]})
        ij.save_session(str(path), capture)
        _unchanged(owner, before, (1, 0))
        root = _decode_wire(path.read_bytes())
        wire, = root["documents"]
        assert wire["axes"] == "TCZYXS" and wire["shape"] == list(SHAPE)
        assert wire["pixel_type"] == np.dtype(dtype).name and wire["calibration"] == CALIBRATION
        assert wire["rois"][0]["selection"] == original["replacement"]
        assert wire["rois"][1]["t"] == wire["rois"][1]["c"] == wire["rois"][1]["z"] == 0
        assert [wire["rois"][2][key] for key in ("t", "c", "z")] == [1, None, 2]
        assert len(wire["planes"]) == 18
        for plane, tcz in zip(wire["planes"], ((t, c, z) for t in range(2) for c in range(3) for z in range(3))):
            assert base64.b64decode(plane, validate=True) == pixels[tcz].astype("<f8").tobytes()
        restored = ij.load_session(str(path))
        assert restored.source_version == 3 and len(restored.documents) == 1
        reopened, = restored.documents
        snapshot = reopened.snapshot()
        assert snapshot.id != owner.id and not snapshot.dirty
        assert _revisions(snapshot) == (0, 0, 0, 0)
        assert reopened.undo_steps == reopened.redo_steps == 0
        np.testing.assert_array_equal(snapshot.dataset.numpy("TCZYXS"), pixels)
        assert [_roi(value) for value in snapshot.rois] == [_roi(value) for value in before.rois]
        assert snapshot.metadata == before.metadata and snapshot.overlays == before.overlays
        assert _selection(snapshot.selection) == _selection(before.selection)
        assert _selection(snapshot.previous_selection) == _selection(before.previous_selection)
        assert _display(snapshot)[:-1] == _display(before)[:-1]
        assert restored.workflow == [original] and restored.results == [{"value": 7}]
        assert restored.extensions == {"future": [None, True, 9]}
        with pytest.raises(ij.RevisionConflict):
            ij.saved_roi_geometry_from_record(snapshot, original)
        if np.dtype(dtype) == np.dtype(np.uint8):
            observed.scalar("saved_roi.reopen.saved_row_count_delta", len(snapshot.rois) - len(before.rois))
            observed.scalar("saved_roi.reopen.undo_steps", reopened.undo_steps)
            observed.scalar("saved_roi.reopen.redo_steps", reopened.redo_steps)
            observed.scalar("saved_roi.reopen.native_geometry_preserved",
                            [_roi(value) for value in snapshot.rois] == [_roi(value) for value in before.rois])
            observed.scalar("saved_roi.reopen.explicit_zero_and_null_distinct",
                            all(getattr(snapshot.rois[1], axis) == 0 and getattr(snapshot.rois[0], axis) is None
                                for axis in ("t", "c", "z")))
            observed.scalar("saved_roi.reopened_owner.strict_replay_publication_delta",
                            reopened.snapshot().revisions.metadata - snapshot.revisions.metadata)
            # Actual API readbacks at the fixed linked XZ/YZ coordinates. This
            # proves source raw-sample preservation, not renderer/GPU behavior.
            old_pixels = before.dataset.numpy("TCZYXS")
            new_pixels = snapshot.dataset.numpy("TCZYXS")
            observed.scalar("saved_roi.linked_xzyz.raw_samples_preserved",
                            old_pixels.dtype == new_pixels.dtype and
                            old_pixels[:, :, :, 5, :, :].tobytes() == new_pixels[:, :, :, 5, :, :].tobytes() and
                            old_pixels[:, :, :, :, 6, :].tobytes() == new_pixels[:, :, :, :, 6, :].tobytes())
        reopened_app = ij.Application()
        reopened_app.adopt(reopened)
        replacement = _styled(points=((4, 3), (8, 7)))
        rebound = ij.rebind_saved_roi_geometry(snapshot, original,
                    ij.SavedRoiGeometryRequest(0, replacement))
        assert _publish(reopened_app, rebound).changed
        assert reopened.snapshot().rois[0].selection == replacement


def _provider_module():
    import importlib.machinery
    import importlib.util
    import os
    from pathlib import Path
    import sys

    # Required exact module path, with no source checkout/package fallback.
    path = Path(os.environ["IMAGEJXX_SAVED_ROI_PROVIDER"]).resolve(strict=True)
    assert path.is_file() and 0 < path.stat().st_size <= 64 * 1024 * 1024
    assert any(str(path).endswith(suffix) for suffix in importlib.machinery.EXTENSION_SUFFIXES)
    name = "_imagejxx_test_saved_roi_source"
    if name in sys.modules:
        module = sys.modules[name]
        assert Path(module.__file__).resolve(strict=True) == path
    else:
        spec = importlib.util.spec_from_file_location(name, path)
        assert spec is not None and spec.loader is not None
        assert Path(spec.origin).resolve(strict=True) == path
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        try:
            spec.loader.exec_module(module)
        except BaseException:
            if sys.modules.get(name) is module:
                del sys.modules[name]
            raise
    assert Path(module.__file__).resolve(strict=True) == path
    assert module.fixture_contract == "p06g.saved-roi-provider.001"
    return module


def _zero_provider_reads(module):
    # A separate real provider demonstrates all three counters/overrides. These
    # reads are intentional positive controls, never reset into a claimed zero.
    probe, observed = module.make_source()
    assert isinstance(probe, ij.Dataset)
    assert observed.counts == {"plane": 0, "strip": 0, "window": 0}
    with pytest.raises(RuntimeError, match=r"saved-roi-observer\.plane acquisition"):
        probe.plane()
    with pytest.raises(RuntimeError, match=r"saved-roi-observer\.strip acquisition"):
        probe.region(0, 0, 12, 1)
    with pytest.raises(RuntimeError, match=r"saved-roi-observer\.window acquisition"):
        probe.region(1, 1, 2, 2)
    assert observed.counts == {"plane": 1, "strip": 1, "window": 1}
    detached = observed.counts
    detached["plane"] = 100
    assert observed.counts == {"plane": 1, "strip": 1, "window": 1}

    terminal_counts = []
    for dtype in ("uint8", "uint16", "float32", "float64"):
        for samples in (1, 3, 4):
            dataset, observed = module.make_source(dtype, samples)
            zero = {"plane": 0, "strip": 0, "window": 0}
            assert isinstance(dataset, ij.Dataset)
            assert dataset.shape == SHAPE[:-1] + (samples,)
            assert dataset.dtype == np.dtype(dtype)
            assert dataset.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
            assert observed.counts == zero
            app, owner, _ = _fixture(np.dtype(dtype), dataset=dataset)
            before = owner.snapshot()
            assert before.dataset.shares_pixels(dataset) and observed.counts == zero
            request = ij.SavedRoiGeometryRequest(0, _replacement())
            estimate = ij.estimate_saved_roi_geometry(before, request)
            command = ij.resolve_saved_roi_geometry(before, request)
            assert estimate.xy_pixels == 120 and command.source.dataset.shares_pixels(dataset)
            record = command.record()
            assert record["source"]["shape"] == list(SHAPE[:-1] + (samples,))
            assert record["source"]["type"] == dtype
            ij.validate_saved_roi_geometry_record(record)
            replay = ij.saved_roi_geometry_from_record(before, record)
            assert replay.is_replay and replay.record() == record
            rebound = ij.rebind_saved_roi_geometry(before, record, request)
            assert not rebound.is_replay and rebound.record() == record
            capture = app.capture_saved_roi_geometry()
            prepared = app.prepare_saved_roi_geometry(capture, command)
            assert prepared.active and prepared.retained_working_bytes > 0
            assert prepared.record() == record and observed.counts == zero
            _unchanged(owner, before, (0, 1))
            publication = app.publish_saved_roi_geometry(prepared)
            assert publication.changed and publication.document_id == owner.id
            assert not prepared.active and prepared.retained_working_bytes == 0
            assert owner.snapshot().rois[0].selection == _replacement()
            assert owner.snapshot().dataset.shares_pixels(dataset)
            assert _revisions(owner.snapshot()) == tuple(
                value + delta for value, delta in zip(_revisions(before), (0, 1, 1, 0)))
            assert owner.undo_steps == 1 and owner.redo_steps == 0 and observed.counts == zero
            owner.undo()
            assert owner.snapshot().rois[0].selection == _styled()
            owner.redo()
            assert owner.snapshot().rois[0].selection == _replacement()
            unchanged = owner.snapshot()
            no_op = _prepare(app, _resolve(app, _replacement()))
            assert not app.publish_saved_roi_geometry(no_op).changed
            _unchanged(owner, unchanged, (1, 0))
            assert observed.counts == zero
            token = ij.CancellationToken()
            canceled = _prepare(app, _resolve(app, _styled()), cancellation=token)
            token.cancel()
            with pytest.raises(ij.OperationCancelled):
                app.publish_saved_roi_geometry(canceled)
            assert not canceled.active and canceled.retained_working_bytes == 0
            with pytest.raises(ValueError):
                ij.resolve_saved_roi_geometry(owner.snapshot(), request,
                    ij.SavedRoiGeometryLimits(max_xy_pixels=119))
            _unchanged(owner, unchanged, (1, 0))
            assert observed.counts == zero
            terminal_counts.append(observed.counts)
    return {name: sum(value[name] for value in terminal_counts) for name in ("plane", "strip", "window")}


def _a11(tmp_path, observed):
    del tmp_path
    provider = _provider_module()
    app, owner, _ = _fixture()
    before = owner.snapshot()
    request = ij.SavedRoiGeometryRequest(0, _replacement())
    estimate = ij.estimate_saved_roi_geometry(before, request)
    assert estimate.xy_pixels == 120 and estimate.vertices == 4
    for name in ("source_data_bytes", "roi_bytes", "original_selection_bytes",
                 "replacement_selection_bytes", "record_metadata_bytes", "record_wire_bytes",
                 "fixed_bytes", "working_bytes"):
        assert getattr(estimate, name) > 0
    assert estimate.working_bytes > estimate.fixed_bytes
    for limits in (ij.SavedRoiGeometryLimits(max_xy_pixels=119),
                   ij.SavedRoiGeometryLimits(max_vertices=1),
                   ij.SavedRoiGeometryLimits(max_working_bytes=1),
                   ij.SavedRoiGeometryLimits(max_record_bytes=1),
                   ij.SavedRoiGeometryLimits(metadata_depth=1)):
        calls = []
        with pytest.raises(ValueError):
            ij.resolve_saved_roi_geometry(before, request, limits,
                                          checkpoint=lambda: calls.append(True))
        assert calls == []
    token = ij.CancellationToken()
    token.cancel()
    calls = []
    with pytest.raises(ij.OperationCancelled):
        ij.resolve_saved_roi_geometry(before, request, cancellation=token,
                                      checkpoint=lambda: calls.append(True))
    assert calls == []
    observed.scalar("saved_roi.pre_cancel.callback_count", len(calls))
    limits = ij.SavedRoiGeometryLimits()
    callbacks, progress, stages = [], [], []
    captured_row = request.row_index
    captured_replacement = _selection(request.replacement)
    captured_limit = limits.max_working_bytes

    def checkpoint():
        callbacks.append(app.snapshot().id)
        request.row_index = 99
        request.replacement = _styled("line", ((0, 0), (1, 1)))
        limits.max_working_bytes = 1

    command = ij.resolve_saved_roi_geometry(before, request, limits,
        checkpoint=checkpoint, progress=lambda done, total: progress.append((done, total)),
        stage_progress=lambda name, done, total: stages.append((name, done, total)))
    assert callbacks and all(identity == owner.id for identity in callbacks)
    assert command.row_index == 0 and command.replacement == _replacement()
    assert command.limits.max_working_bytes == 268435456
    assert stages == [("saved_roi_geometry.capture", 0, 1), ("saved_roi_geometry.capture", 1, 1)]
    # This metadata resolver emits named stage reports, not legacy coarse
    # progress. Passing a progress owner must not invent a completed kernel.
    assert progress == []
    observed.scalar("saved_roi.control.captured_values_preserved",
                    command.row_index == captured_row and _selection(command.replacement) == captured_replacement and
                    command.limits.max_working_bytes == captured_limit)
    token = ij.CancellationToken()
    reports = []

    def cancel_after_metadata(name, done, total):
        reports.append((name, done, total))
        if done > 0:
            token.cancel()

    with pytest.raises(ij.OperationCancelled):
        _resolve(app, cancellation=token, stage_progress=cancel_after_metadata)
    assert any(done > 0 for _, done, _ in reports)
    # Positive metadata capture progress is not a raster/kernel-entry claim.
    class CallbackFailure(RuntimeError):
        pass

    def fail():
        assert app.snapshot().id == owner.id
        raise CallbackFailure("owned saved-area callback")

    with pytest.raises(CallbackFailure, match="owned saved-area callback"):
        _resolve(app, checkpoint=fail)
    with pytest.raises(CallbackFailure, match="owned saved-area callback"):
        _prepare(app, command, checkpoint=fail)
    token = ij.CancellationToken()
    cancel_before = owner.snapshot()
    prepared = _prepare(app, command, cancellation=token)
    assert prepared.active and prepared.retained_working_bytes > 0
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        app.publish_saved_roi_geometry(prepared)
    assert not prepared.active and prepared.retained_working_bytes == 0
    assert owner.snapshot().dataset.shares_pixels(before.dataset)
    _unchanged(owner, before, (0, 1))
    observed.scalar("saved_roi.cancel.document_dirty_changes", int(owner.snapshot().dirty != cancel_before.dirty))
    reads = _zero_provider_reads(provider)
    observed.scalar("saved_roi.provider.read_plane_calls", reads["plane"])
    observed.scalar("saved_roi.provider.read_region_calls", reads["strip"] + reads["window"])
    observed.scalar("saved_roi.target.provider_reads", sum(reads.values()))

    # The retained predicate is pure C++, installed through public Document
    # APIs by the private fixture. No Python callback can run under commit locks.
    assert provider.constraint_contract == "p06g.saved-roi-constraint.001"
    app, owner, _ = _fixture()
    before = owner.snapshot()
    constraint = provider.reject_replacement(owner, _replacement())
    assert constraint.active and constraint.rejections == 0
    prepared = _prepare(app)
    calls_before, rejections_before = constraint.calls, constraint.rejections
    with pytest.raises(RuntimeError, match=r"saved-roi-observer\.constraint rejection"):
        app.publish_saved_roi_geometry(prepared)
    assert constraint.active and constraint.calls == calls_before + 1
    assert constraint.rejections == rejections_before + 1
    assert not prepared.active and prepared.retained_working_bytes == 0
    _unchanged(owner, before, (0, 1))
    observed.scalar("saved_roi.constraint_rejection.publication_delta",
                    owner.snapshot().revisions.metadata - before.revisions.metadata)
    observed.scalar("saved_roi.failure.metadata_revision_delta",
                    owner.snapshot().revisions.metadata - before.revisions.metadata)


def _desktop_result(value, owner, *, changed):
    assert type(value) is dict and set(value) == {"document_id", "changed", "record"}
    assert type(value["document_id"]) is int and value["document_id"] == owner.id
    assert type(value["changed"]) is bool and value["changed"] is changed
    assert type(value["record"]) is dict
    ij.validate_saved_roi_geometry_record(value["record"])


def _a12(tmp_path, observed):
    from imagejxx import _desktop
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    # Both actual public wrappers must reach the same native controller. This
    # remains API evidence; no Window action here represents a physical click.
    for route in ("native_desktop", "python_editor"):
        if route == "native_desktop":
            editor = _desktop.Editor(str(tmp_path / "native-preferences.json"))
        else:
            editor = Editor(preferences=Preferences(tmp_path / "python-preferences.json"))
        _, owner, _ = _fixture(spectrum=False, desktop_admitted=True)
        editor.add_native_document(owner)
        if route == "native_desktop":
            assert editor.native_document(owner.id) is owner
        else:
            assert editor.live_document(owner.id).native_owner is owner
        editor.recording = True
        before = owner.snapshot()
        expected = ij.resolve_saved_roi_geometry(before,
                       ij.SavedRoiGeometryRequest(0, _replacement())).record()
        result = editor.apply_saved_roi_geometry(ij.SavedRoiGeometryRequest(0, _replacement()))
        _desktop_result(result, owner, changed=True)
        assert result["record"] == expected
        assert owner.snapshot().rois[0].selection == _replacement()
        assert owner.undo_steps == 1 and owner.redo_steps == 0
        _unrelated(before, owner.snapshot())
        workflow = json.loads(editor.workflow) if route == "native_desktop" else editor.workflow
        assert workflow == [expected]
        results = json.loads(editor.results) if route == "native_desktop" else editor.results
        assert results == []
        current = owner.snapshot()
        no_op = editor.apply_saved_roi_geometry(ij.SavedRoiGeometryRequest(0, _replacement()))
        _desktop_result(no_op, owner, changed=False)
        _unchanged(owner, current, (1, 0))
        workflow = json.loads(editor.workflow) if route == "native_desktop" else editor.workflow
        assert workflow == [expected]
        with pytest.raises(ij.RevisionConflict):
            editor.replay_saved_roi_geometry(expected)
        _unchanged(owner, current, (1, 0))
        # Detached return mutation cannot alter the published row/workflow.
        result["record"]["replacement"]["points"][0][0] = -100
        workflow = json.loads(editor.workflow) if route == "native_desktop" else editor.workflow
        assert workflow == [expected]
        new_replacement = _styled(points=((4, 3), (8, 7)))
        rebound = editor.replay_saved_roi_geometry(expected,
                       complete_rebind_request=ij.SavedRoiGeometryRequest(0, new_replacement))
        _desktop_result(rebound, owner, changed=True)
        assert rebound["record"]["replacement"]["points"] == [[4.0, 3.0], [8.0, 7.0]]
        assert owner.snapshot().rois[0].selection == new_replacement
        assert owner.undo_steps == 2
        workflow = json.loads(editor.workflow) if route == "native_desktop" else editor.workflow
        assert workflow == [expected, rebound["record"]]
        editor.recording = False
        records_before_off = len(workflow)
        off = editor.apply_saved_roi_geometry(ij.SavedRoiGeometryRequest(0, _styled()))
        _desktop_result(off, owner, changed=True)
        assert owner.snapshot().rois[0].selection == _styled() and owner.undo_steps == 3
        off_workflow = json.loads(editor.workflow) if route == "native_desktop" else editor.workflow
        assert len(off_workflow) == records_before_off
        if route == "native_desktop":
            observed.scalar("saved_roi.recording_off.workflow_append_delta", len(off_workflow) - records_before_off)
        # Strict replay of an unpublished same-owner record uses the same
        # controller; disabling recording changes only the workflow append.
        strict = ij.resolve_saved_roi_geometry(owner.snapshot(),
                    ij.SavedRoiGeometryRequest(0, _replacement())).record()
        replayed = editor.replay_saved_roi_geometry(strict)
        _desktop_result(replayed, owner, changed=True)
        assert replayed["record"] == strict and owner.undo_steps == 4
        assert owner.snapshot().rois[0].selection == _replacement()
        workflow = json.loads(editor.workflow) if route == "native_desktop" else editor.workflow
        assert workflow == [expected, rebound["record"]]
        malformed = {**expected, "extra": True}
        current = owner.snapshot()
        with pytest.raises(ValueError):
            editor.replay_saved_roi_geometry(malformed,
                complete_rebind_request=ij.SavedRoiGeometryRequest(0, _replacement()))
        _unchanged(owner, current, (4, 0))


_CASES = dict(zip(EXPECTED_COLLECTION_IDS, (
    _a01, _a02, _a03, _a04, _a05, _a06,
    _a07, _a08, _a09, _a10, _a11, _a12,
)))


@pytest.mark.parametrize("case_id", EXPECTED_COLLECTION_IDS, ids=EXPECTED_COLLECTION_IDS)
def test_saved_roi_geometry(case_id, tmp_path):
    observed = RawCase(case_id)
    _CASES[case_id](tmp_path, observed)
    observed.finish()
