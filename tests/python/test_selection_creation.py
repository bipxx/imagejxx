"""P05l native Python contracts with independent literal cells and geometry.

Python supplies inputs/assertions, never Boolean processing for the product.
Public Python datasets are eager; the paired installed C++ consumer supplies
the throwing lazy PlaneSource proof. Limits describe the owned core working
set, excluding source/history/caller operands, allocator bookkeeping, host
publication copies and arbitrary retained previews, as documented publicly.
"""

import base64
import copy
import gc
import hashlib
import json
from pathlib import Path
import sys
import threading
import weakref

import numpy as np
import pytest


OPERATIONS = ("REPLACE", "ADD", "SUBTRACT", "INTERSECT", "XOR")
CANDIDATES = ("rectangle", "ellipse", "polygon", "freehand")
METHODS = ("compose", "resolve", "validate", "replay", "rebind", "prepare")
EXPECTED_CASE_COUNT = 207
SDK_CASE_IDS = (
    "P05l.sdk.L01_literal_composition_admission", "P05l.sdk.L02_publication_noop_redo_display",
    "P05l.sdk.L03_records_replay_rebind_refusals", "P05l.sdk.L04_lazy_limits_callbacks",
    "P05l.sdk.L05_qualified_geometry_values", "P05l.sdk.L06_native_session_owner_binding",
)
STYLE = {"label": "draft", "future": {"version": 37, "values": [True, 7, None]}}
RING = ("0111110", "0100010", "0100010", "0100010", "0111110")
RIGHT = ("0000000", "0001111", "0001111", "0001111", "0000000")
ELLIPSE = ("0000000", "0000110", "0001111", "0000110", "0000000")
EMPTY = ("0000000",) * 5
RECT_RESULTS = {
    "ADD": ("0111110", "0101111", "0101111", "0101111", "0111110"),
    "SUBTRACT": ("0111110", "0100000", "0100000", "0100000", "0111110"),
    "INTERSECT": ("0000000", "0000010", "0000010", "0000010", "0000000"),
    "XOR": ("0111110", "0101101", "0101101", "0101101", "0111110"),
}
ELLIPSE_RESULTS = {
    "ADD": ("0111110", "0100110", "0101111", "0100110", "0111110"),
    "SUBTRACT": ("0111110", "0100000", "0100000", "0100000", "0111110"),
    "INTERSECT": RECT_RESULTS["INTERSECT"],
    "XOR": ("0111110", "0100100", "0101101", "0100100", "0111110"),
}


@pytest.fixture(autouse=True)
def public_api():
    global ij
    import imagejxx as ij


def bitmap(rows):
    return np.array([[255 if cell == "1" else 0 for cell in row] for row in rows], np.uint8)


def packed(rows):
    cells = "".join(rows)
    out = bytearray((len(cells) + 7) // 8)
    for i, cell in enumerate(cells):
        if cell == "1":
            out[i // 8] |= 1 << (7 - i % 8)
    return bytes(out)


def mask(rows=RING, *, style=False):
    value = ij.Selection.from_mask(bitmap(rows))
    return value.with_attributes(STYLE) if style else value


def candidate(kind="rectangle", *, styled=True):
    points = ((3, 1), (7, 4)) if kind in ("rectangle", "ellipse") else ((3, 1), (7, 1), (7, 4), (3, 4))
    return ij.Selection(kind, points, width=3 if styled else 1, attributes=STYLE if styled else {})


def request(kind="rectangle", operation="ADD", *, styled=True):
    return ij.SelectionCreationRequest(candidate(kind, styled=styled), getattr(ij.SelectionCreationOperation, operation))


def selection_signature(value):
    if value is None:
        return None
    return (value.kind, tuple(map(tuple, value.points)), value.width, value.attributes,
            value.mask((5, 7)).tobytes() if value.kind == "mask" else None)


def display_signature(snapshot):
    view = snapshot.display
    return (view.t, view.c, tuple(view.crosshair), view.zoom, view.orthogonal, view.linked_zoom,
            tuple(view.plane_zoom), tuple(map(tuple, view.pan)), view.properties)


def records_signature(snapshot):
    cal = snapshot.dataset.calibration
    spectrum = snapshot.spectrum
    return (snapshot.metadata, snapshot.record_layout, snapshot.overlays,
            [(r.name, r.t, r.c, r.z, r.attributes, selection_signature(r.selection)) for r in snapshot.rois],
            (tuple(cal.spacing), tuple(cal.origin), tuple(cal.direction), cal.unit,
             cal.frame_interval, cal.time_origin, cal.time_unit), snapshot.dataset.shape,
            snapshot.dataset.dtype.str, snapshot.dataset.sample_policy,
            None if spectrum is None else (tuple(spectrum.shape), spectrum.original_shape, spectrum.numpy().tobytes()))


def history(owner):
    value = owner.history()
    return value.undo_steps, value.redo_steps, value.step_limit, value.pixel_budget


def owner(*, base="ring", dtype=np.uint16, samples=1, quiet=False, scientific=True):
    shape = (2, 3, 4, 5, 7, samples) if scientific else (1, 1, 4, 5, 7, samples)
    t, c, z, y, x, s = np.indices(shape)
    pixels = (40 * t + 30 * c + 5 * z + 2 * y + x + s).astype(dtype)
    policy = ij.FloatingSamplePolicy.FINITE_ONLY
    if quiet:
        pixels = pixels.astype(np.float32)
        raw = pixels.view(np.uint32)
        raw[1, 2, 3, 1, 2, 0] = 0x7FC12345
        raw[1, 2, 3, 2, 3, 0] = 0x80000000
        policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    calibration = ij.SpatialCalibration(spacing=(.5, 1.5, 2), origin=(11, -7, 19),
        direction=(-1, .25, .5, 0, 1, .5, .5, 0, 1), unit="um",
        frame_interval=.75, time_origin=2.25, time_unit="s")
    result = ij.Document(ij.Dataset(pixels, axes="TCZYXS", calibration=calibration, sample_policy=policy), "creation")
    edit = result.begin_edit()
    edit.set_property("retained", {"nested": [True, 7, "kept"]})
    edit.set_extension("opaque", {"version": 37})
    edit.set_record_layout({"source_presence": True})
    edit.set_rois([ij.ROI("positioned", ij.Selection("rectangle", [(1, 0), (3, 2)]),
                          t=0, c=0, z=0, attributes={"retained": True})])
    edit.set_overlays([{"future_annotation": {"version": 37, "values": [3, 1, 4]}}])
    edit.set_spectrum(ij.FourierState(np.full((8, 8), 3 - 2j, np.complex128), original_shape=(5, 7)))
    edit.select(mask(style=True) if base == "ring" else None)
    edit.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)], width=2))
    result.commit(edit)
    result.mark_saved()
    # A nonempty redo stack must survive this new, selection-only path.
    change = result.begin_edit()
    change.set_property("redo_probe", True)
    result.commit(change)
    result.undo()
    view = result.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = shape[0] - 1, shape[1] - 1, [2, 1, 3], 2.25
    view.properties = {"retained_display": {"values": [1, 2, 3]}}
    result.set_display(view)
    return result, pixels


def unchanged(document, before, old_history):
    after = document.snapshot()
    assert after.shares_data(before) and after.revisions.matches(before.revisions, ij.Change.ALL)
    assert after.data_identity == before.data_identity and after.dirty == before.dirty
    assert history(document) == old_history and display_signature(after) == display_signature(before)


def preserved(document, before, old_history):
    after = document.snapshot()
    assert after.dataset.shares_pixels(before.dataset)
    assert after.dataset.numpy("TCZYXS").tobytes() == before.dataset.numpy("TCZYXS").tobytes()
    assert records_signature(after) == records_signature(before)
    assert after.spectrum.shares_storage(before.spectrum)
    assert after.data_identity == before.data_identity and after.dirty == before.dirty
    assert history(document) == old_history
    assert after.revisions.pixels == before.revisions.pixels and after.revisions.metadata == before.revisions.metadata


def exact_mask(value, rows):
    assert value is not None and value.kind == "mask" and value.points == []
    assert value.width == 1 and value.attributes == {}
    np.testing.assert_array_equal(value.mask((5, 7)), bitmap(rows))


def wire_mask(rows, attributes=None):
    return dict(attributes or {}, kind="mask", points=[], width=1,
                shape=[5, 7], mask=base64.b64encode(packed(rows)).decode("ascii"))


def wire_candidate(kind="rectangle"):
    points = [[3, 1], [7, 4]] if kind in ("rectangle", "ellipse") else [[3, 1], [7, 1], [7, 4], [3, 4]]
    return dict(copy.deepcopy(STYLE), kind=kind, points=points, width=3)


@pytest.mark.parametrize("kind", CANDIDATES)
@pytest.mark.parametrize("operation", OPERATIONS)
def test_four_candidate_kinds_and_five_operations(kind, operation):
    document, pixels = owner()
    before, old_history = document.snapshot(), history(document)
    req = request(kind, operation)
    command = ij.resolve_selection_creation_command(before, req)
    result = ij.compose_selection_creation(before, req)
    expected_rows = (ELLIPSE_RESULTS if kind == "ellipse" else RECT_RESULTS).get(operation)
    if operation == "REPLACE":
        assert selection_signature(result) == selection_signature(req.candidate)
        np.testing.assert_array_equal(result.mask((5, 7)), bitmap(ELLIPSE if kind == "ellipse" else RIGHT))
    else:
        exact_mask(result, expected_rows)
    assert selection_signature(command.result) == selection_signature(result)
    assert command.changes_selection and selection_signature(command.base) == selection_signature(before.selection)
    transaction = ij.prepare_selection_creation(command)
    assert transaction.active and transaction.document_id == before.id and transaction.dependencies == ij.Change.DATA
    unchanged(document, before, old_history)
    assert selection_signature(transaction.preview().selection) == selection_signature(result)
    document.commit(transaction)
    after = document.snapshot()
    assert after.revisions.selection == before.revisions.selection + 1
    assert selection_signature(after.previous_selection) == selection_signature(before.selection)
    preserved(document, before, old_history)
    assert display_signature(after) == display_signature(before)
    assert after.dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()


@pytest.mark.parametrize("operation", OPERATIONS)
def test_absent_base_policy_is_explicit(operation):
    document, _ = owner(base=None)
    before, old_history = document.snapshot(), history(document)
    if operation in ("REPLACE", "ADD"):
        command = ij.resolve_selection_creation_command(before, request(operation=operation))
        assert command.base is None
        assert selection_signature(command.result) == selection_signature(candidate())
    else:
        with pytest.raises(ValueError, match="area base"):
            ij.resolve_selection_creation_command(before, request(operation=operation))
    unchanged(document, before, old_history)


@pytest.mark.parametrize("kind,points", [("line", [(0, 0), (2, 2)]), ("point", [(2, 1)])])
def test_nonarea_base_refuses_combination_but_allows_replace(kind, points):
    document, _ = owner()
    document.select(ij.Selection(kind, points))
    before, old_history = document.snapshot(), history(document)
    for operation in OPERATIONS[1:]:
        with pytest.raises(ValueError, match="area base"):
            ij.compose_selection_creation(before, request(operation=operation))
    command = ij.resolve_selection_creation_command(before, request(operation="REPLACE"))
    assert selection_signature(command.result) == selection_signature(candidate())
    unchanged(document, before, old_history)


@pytest.mark.parametrize("kind,points", [("rotated_rectangle", [(3, 2), (5, 2), (3, 3)]),
                                        ("rotated_ellipse", [(3, 2), (5, 2), (3, 3)])])
def test_native_rotated_area_bases_are_admitted(kind, points):
    document, _ = owner()
    document.select(ij.Selection(kind, points, width=4, attributes=STYLE))
    req = ij.SelectionCreationRequest(ij.Selection("rectangle", [(-1, -1), (8, 6)]), ij.SelectionCreationOperation.INTERSECT)
    command = ij.resolve_selection_creation_command(document.snapshot(), req)
    # At y=1.5/2.5 all four cell centers x=1.5..4.5 lie inside
    # the ellipse with radii(2,1): worst normalized distance is .8125.
    rows = ("0000000", "0111100", "0111100", "0000000", "0000000")
    exact_mask(command.result, rows)


def test_empty_result_and_same_pixels_different_representation_are_changes():
    document, _ = owner()
    request_xor = ij.SelectionCreationRequest(ij.Selection("rectangle", [(1, 0), (6, 5)]), ij.SelectionCreationOperation.INTERSECT)
    # A mask equal in raster membership remains a mask, not the old geometry.
    document.select(ij.Selection("rectangle", [(1, 0), (6, 5)]))
    before = document.snapshot()
    command = ij.resolve_selection_creation_command(before, request_xor)
    assert command.changes_selection and command.result.kind == "mask"
    document.commit(ij.prepare_selection_creation(command))
    assert document.snapshot().revisions.selection == before.revisions.selection + 1
    outside = ij.SelectionCreationRequest(ij.Selection("rectangle", [(9, 1), (12, 3)]), ij.SelectionCreationOperation.INTERSECT)
    empty = ij.resolve_selection_creation_command(document.snapshot(), outside)
    exact_mask(empty.result, EMPTY)
    document.commit(ij.prepare_selection_creation(empty))
    assert document.snapshot().selection is not None


def test_exact_noop_preserves_previous_every_revision_and_redo():
    document, _ = owner()
    document.commit(ij.prepare_selection_creation(ij.resolve_selection_creation_command(document.snapshot(), request())))
    before, old_history = document.snapshot(), history(document)
    same = ij.SelectionCreationRequest(candidate(), ij.SelectionCreationOperation.ADD)
    command = ij.resolve_selection_creation_command(before, same)
    assert not command.changes_selection
    transaction = ij.prepare_selection_creation(command)
    assert selection_signature(transaction.preview().previous_selection) == selection_signature(before.previous_selection)
    document.commit(transaction)
    unchanged(document, before, old_history)


@pytest.mark.parametrize("kind", CANDIDATES)
def test_record_exact_fields_types_original_equation_and_detached_copies(kind):
    document, _ = owner()
    before = document.snapshot()
    req = request(kind)
    command = ij.resolve_selection_creation_command(before, req)
    rows = ELLIPSE_RESULTS["ADD"] if kind == "ellipse" else RECT_RESULTS["ADD"]
    expected = {"schema": "imagejxx.selection-creation-command", "version": 1,
        "source": {"id": before.id, "pixels_revision": before.revisions.pixels,
                   "metadata_revision": before.revisions.metadata, "selection_revision": before.revisions.selection,
                   "shape": [2, 3, 4, 5, 7, 1], "type": "uint16", "sample_policy": "finite_only"},
        "position": {"t": 1, "c": 2, "z": 3}, "operation": "add",
        "base": wire_mask(RING, STYLE), "candidate": wire_candidate(kind), "result": wire_mask(rows)}
    assert command.record() == expected
    assert type(command.record()["version"]) is int
    assert all(type(v) is int for v in command.record()["source"]["shape"])
    ij.validate_selection_creation_command_record(copy.deepcopy(expected))
    req.candidate = ij.Selection("point", [(0, 0)])
    detached = command.request
    detached.operation = ij.SelectionCreationOperation.XOR
    detached.candidate = ij.Selection("point", [(0, 0)])
    for value in (command.result, command.base):
        # Selection's public properties are readonly. Their returned Python
        # metadata/list containers are copies, as are command.request/record.
        with pytest.raises(AttributeError):
            value.kind = "point"
        attributes = value.attributes
        attributes["label"] = "edited copy"
        points = value.points
        points.append([999, 999])
    edited = command.record()
    edited["source"]["id"] += 99
    assert command.record() == expected
    for attr in ("source", "request", "base", "result", "estimate", "changes_selection"):
        with pytest.raises(AttributeError):
            setattr(command, attr, None)


def test_strict_replay_allows_new_selection_display_and_rebind_is_explicit():
    document, _ = owner()
    original = ij.resolve_selection_creation_command(document.snapshot(), request())
    record = original.record()
    document.commit(ij.prepare_selection_creation(original))
    replacement = ij.Selection("ellipse", [(0, 0), (2, 2)], width=2, attributes={"fresh": True})
    document.select(replacement)
    view = document.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = 0, 0, [1, 2, 0], 4
    document.set_display(view)
    source, old_history = document.snapshot(), history(document)
    replay = ij.selection_creation_command_from_record(source, record)
    assert replay.record() == record and selection_signature(replay.base) != selection_signature(source.selection)
    assert replay.source.shares_data(source)
    exact_mask(replay.result, RECT_RESULTS["ADD"])
    tx = ij.prepare_selection_creation(replay)
    view.zoom = 7
    document.set_display(view)
    navigated = document.snapshot()
    document.commit(tx, preserve_concurrent_display=True)
    assert display_signature(document.snapshot()) == display_signature(navigated)
    assert selection_signature(document.snapshot().previous_selection) == selection_signature(replacement)
    preserved(document, source, old_history)
    other, _ = owner(base=None)
    with pytest.raises(ValueError, match="owner"):
        ij.selection_creation_command_from_record(other.snapshot(), record)
    rebound = ij.rebind_selection_creation_command(other.snapshot(), record, request(operation="REPLACE"))
    assert rebound.record()["source"]["id"] == other.id and rebound.record()["operation"] == "replace"
    assert rebound.base is None and selection_signature(rebound.result) == selection_signature(candidate())


@pytest.mark.parametrize("change", ("pixels", "metadata", "selection", "foreign", "closed"))
def test_prepare_publication_and_record_staleness_are_atomic(change):
    document, _ = owner()
    command = ij.resolve_selection_creation_command(document.snapshot(), request())
    tx = ij.prepare_selection_creation(command)
    target = document
    if change == "foreign":
        target, _ = owner()
    elif change == "closed":
        tx.rollback()
    else:
        edit = document.begin_edit()
        if change == "pixels":
            edit.replace_plane(np.zeros((5, 7), np.uint16), t=0, c=0, z=0)
        elif change == "metadata":
            edit.set_property("new", 7)
        else:
            edit.select(ij.Selection("point", [(0, 0)]))
        document.commit(edit)
    before, old_history = target.snapshot(), history(target)
    with pytest.raises(ValueError if change in ("foreign", "closed") else ij.RevisionConflict):
        target.commit(tx)
    unchanged(target, before, old_history)
    if change in ("pixels", "metadata"):
        with pytest.raises(ij.RevisionConflict):
            ij.selection_creation_command_from_record(document.snapshot(), command.record())
    elif change == "selection":
        ij.selection_creation_command_from_record(document.snapshot(), command.record())


DELETE = object()
MALFORMED = [
    *(("missing_" + key, (key,), DELETE) for key in ("schema", "version", "source", "position", "operation", "base", "candidate", "result")),
    ("unknown_top", ("extra",), 1), ("schema", ("schema",), "foreign"),
    ("version_bool", ("version",), True), ("version_float", ("version",), 1.0), ("version_future", ("version",), 2),
    *(("missing_source_" + key, ("source", key), DELETE) for key in ("id", "pixels_revision", "metadata_revision", "selection_revision", "shape", "type", "sample_policy")),
    ("source_extra", ("source", "extra"), 1), ("source_id_zero", ("source", "id"), 0),
    ("source_id_bool", ("source", "id"), True), ("revision_float", ("source", "pixels_revision"), 1.0),
    ("revision_bool", ("source", "selection_revision"), False), ("shape_short", ("source", "shape"), [2, 3, 4, 5, 7]),
    ("shape_bool", ("source", "shape", 5), True), ("shape_float", ("source", "shape", 4), 7.0),
    ("shape_zero", ("source", "shape", 4), 0), ("shape_native_overflow", ("source", "shape", 4), 2**31),
    ("shape_overbudget", ("source", "shape"), [1, 1, 1, 2**30, 2**30, 1]),
    ("type", ("source", "type"), "UInt16"), ("policy", ("source", "sample_policy"), "unknown"),
    ("quiet_policy_type", ("source", "sample_policy"), "preserve_quiet_nan_float32"),
    ("position_extra", ("position", "extra"), 0), ("position_missing", ("position", "z"), DELETE),
    ("position_bool", ("position", "t"), True), ("position_float", ("position", "z"), 3.0),
    ("position_t", ("position", "t"), 2), ("position_c", ("position", "c"), 3), ("position_z", ("position", "z"), 4),
    ("operation", ("operation",), "union"), ("operation_bool", ("operation",), True),
    ("base_null_add_equation", ("base",), None), ("candidate_null", ("candidate",), None),
    ("candidate_kind", ("candidate", "kind"), "point"), ("candidate_short", ("candidate", "points"), [[1, 1]]),
    ("point_row", ("candidate", "points", 0), [3, 1, 0]), ("point_bool", ("candidate", "points", 0, 0), True),
    ("point_lossy", ("candidate", "points", 0, 0), 2**53 + 1),
    ("width_bool", ("candidate", "width"), True), ("width_zero", ("candidate", "width"), 0),
    ("mask_candidate", ("candidate",), wire_mask(RIGHT)), ("result_null", ("result",), None),
    ("result_shape", ("result", "shape"), [7, 5]), ("result_storage", ("result", "mask"), "!!!!"),
    ("result_tail", ("result", "mask"), base64.b64encode(packed(RECT_RESULTS["ADD"])[:-1] + b"\xff").decode()),
    ("equation", ("result",), wire_mask(EMPTY)), ("result_style", ("result", "label"), "forged"),
]


@pytest.mark.parametrize("case", MALFORMED, ids=lambda item: item[0])
def test_entire_original_record_is_validated_before_replay_or_rebind(case):
    document, _ = owner()
    destination, _ = owner()
    record = ij.resolve_selection_creation_command(document.snapshot(), request()).record()
    node = record
    for key in case[1][:-1]:
        node = node[key]
    if case[2] is DELETE:
        del node[case[1][-1]]
    else:
        node[case[1][-1]] = copy.deepcopy(case[2])
    saved = copy.deepcopy(record)
    before, old_history = document.snapshot(), history(document)
    other_before, other_history = destination.snapshot(), history(destination)
    for call in (lambda: ij.validate_selection_creation_command_record(record),
                 lambda: ij.selection_creation_command_from_record(before, record),
                 lambda: ij.rebind_selection_creation_command(other_before, record, request(operation="REPLACE"))):
        with pytest.raises(ValueError):
            call()
    assert record == saved
    unchanged(document, before, old_history)
    unchanged(destination, other_before, other_history)


def test_semantically_wrong_result_precedes_foreign_owner_and_new_request():
    document, _ = owner()
    other, _ = owner()
    record = ij.resolve_selection_creation_command(document.snapshot(), request()).record()
    record["result"] = wire_mask(EMPTY)
    with pytest.raises(ValueError, match="result differs"):
        ij.selection_creation_command_from_record(other.snapshot(), record)
    invalid_request = ij.SelectionCreationRequest(ij.Selection("point", [(0, 0)]))
    with pytest.raises(ValueError, match="result differs"):
        ij.rebind_selection_creation_command(other.snapshot(), record, invalid_request)


@pytest.mark.parametrize("field", ("max_pixels", "max_candidate_vertices", "max_working_bytes"))
@pytest.mark.parametrize("value,error", [(True, TypeError), (np.bool_(False), TypeError), (1.5, TypeError),
                                         ("3", TypeError), (-1, ValueError), (2**200, ValueError)])
def test_limits_use_exact_native_integers(field, value, error):
    limits = ij.SelectionCreationLimits()
    with pytest.raises(error):
        setattr(limits, field, value)


def test_estimate_and_exact_budget_edges_admit_before_any_callback():
    document, _ = owner()
    req, before, old_history = request(), document.snapshot(), history(document)
    limits = ij.SelectionCreationLimits()
    assert (limits.max_pixels, limits.max_candidate_vertices, limits.max_working_bytes) == (16_777_216, 65_536, 268_435_456)
    limits.max_pixels = np.int64(35)
    limits.max_candidate_vertices = 2
    estimate = ij.estimate_selection_creation(before, req, limits)
    assert estimate.pixels == 35 and estimate.candidate_vertices == 2
    assert 0 < estimate.retained_bytes <= estimate.working_bytes <= 268_435_456
    for attr in ("pixels", "candidate_vertices", "retained_bytes", "working_bytes"):
        with pytest.raises(AttributeError):
            setattr(estimate, attr, 0)
    limits.max_working_bytes = estimate.working_bytes
    command = ij.resolve_selection_creation_command(before, req, limits)
    ij.prepare_selection_creation(command, execution_max_working_bytes=estimate.working_bytes)
    for field, value in (("max_pixels", 34), ("max_candidate_vertices", 1), ("max_working_bytes", estimate.working_bytes - 1)):
        small = ij.SelectionCreationLimits()
        setattr(small, field, value)
        callbacks = []
        with pytest.raises(ValueError):
            ij.resolve_selection_creation_command(before, req, small, checkpoint=lambda: callbacks.append("checkpoint"),
                                                   progress=lambda *args: callbacks.append(args))
        assert callbacks == []
    callbacks = []
    with pytest.raises(ValueError):
        ij.prepare_selection_creation(command, execution_max_working_bytes=estimate.working_bytes - 1,
            checkpoint=lambda: callbacks.append("checkpoint"), progress=lambda *args: callbacks.append(args))
    assert callbacks == []
    unchanged(document, before, old_history)


def invoke(method, source, req, command, **callbacks):
    if method == "compose":
        return ij.compose_selection_creation(source, req, **callbacks)
    if method == "resolve":
        return ij.resolve_selection_creation_command(source, req, **callbacks)
    if method == "validate":
        return ij.validate_selection_creation_command_record(command.record(), **callbacks)
    if method == "replay":
        return ij.selection_creation_command_from_record(source, command.record(), **callbacks)
    if method == "rebind":
        return ij.rebind_selection_creation_command(source, command.record(), req, **callbacks)
    return ij.prepare_selection_creation(command, **callbacks)


@pytest.mark.parametrize("method", METHODS)
def test_progress_checkpoint_precancel_and_success_are_native_atomic(method):
    document, _ = owner()
    before, old_history, req = document.snapshot(), history(document), request()
    command = ij.resolve_selection_creation_command(before, req)
    progress, checkpoints = [], []
    invoke(method, before, req, command, progress=lambda *args: progress.append(args), checkpoint=lambda: checkpoints.append(True))
    assert progress == [(0, 1), (1, 1)] and checkpoints
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        invoke(method, before, req, command, cancellation=token)
    unchanged(document, before, old_history)


@pytest.mark.parametrize("method", METHODS)
@pytest.mark.parametrize("phase", (0, 1))
def test_progress_exception_and_cancellation_at_each_boundary(method, phase):
    document, _ = owner()
    before, old_history, req = document.snapshot(), history(document), request()
    command = ij.resolve_selection_creation_command(before, req)
    class CallbackFailure(RuntimeError):
        pass
    def fail(done, total):
        if done == phase:
            raise CallbackFailure("creation callback sentinel")
    with pytest.raises(CallbackFailure, match="sentinel"):
        invoke(method, before, req, command, progress=fail)
    token = ij.CancellationToken()
    def cancel(done, total):
        if done == phase:
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        invoke(method, before, req, command, progress=cancel, cancellation=token)
    unchanged(document, before, old_history)


@pytest.mark.parametrize("method", METHODS)
def test_checkpoint_exception_and_callback_types_leave_no_edit(method):
    document, _ = owner()
    before, old_history, req = document.snapshot(), history(document), request()
    command = ij.resolve_selection_creation_command(before, req)
    def fail():
        raise RuntimeError("checkpoint sentinel")
    with pytest.raises(RuntimeError, match="checkpoint sentinel"):
        invoke(method, before, req, command, checkpoint=fail)
    for key in ("progress", "checkpoint"):
        with pytest.raises(ValueError, match="callable"):
            invoke(method, before, req, command, **{key: []})
    unchanged(document, before, old_history)


def test_source_and_nested_request_are_captured_before_reentrant_callbacks():
    document, _ = owner()
    before, req = document.snapshot(), request()
    def mutate(done, total):
        if done == 0:
            req.candidate = ij.Selection("point", [(0, 0)])
            document.select(ij.Selection("rectangle", [(0, 0), (1, 1)]))
    command = ij.resolve_selection_creation_command(before, req, progress=mutate)
    exact_mask(command.result, RECT_RESULTS["ADD"])
    assert selection_signature(command.base) == selection_signature(before.selection)
    with pytest.raises(ij.RevisionConflict):
        document.commit(ij.prepare_selection_creation(command))


def test_gil_release_allows_another_python_thread_to_cancel_in_native_work():
    pixels = np.zeros((1024, 1024), np.uint8)
    document = ij.Document(ij.Dataset(pixels, axes="YX"))
    document.select(ij.Selection("ellipse", [(1, 1), (1023, 1023)]))
    req = ij.SelectionCreationRequest(ij.Selection("polygon", [(0, 0), (1024, 0), (1024, 1024), (0, 1024)]),
                                      ij.SelectionCreationOperation.XOR)
    token = ij.CancellationToken()
    ready, entered = threading.Event(), threading.Event()
    actor_errors = []
    def actor():
        ready.set()
        if not entered.wait(2):
            actor_errors.append("native checkpoint never signaled")
        token.cancel()
    thread = threading.Thread(target=actor, name="selection-creation-canceller")
    thread.start()
    assert ready.wait(2)
    interval = sys.getswitchinterval()
    try:
        # The callback only sets an event and returns. Suppress interpreter
        # time-slice handoffs there; cancellation needs the native GIL release.
        sys.setswitchinterval(5)
        with pytest.raises(ij.OperationCancelled):
            ij.compose_selection_creation(document.snapshot(), req, cancellation=token, checkpoint=entered.set)
    finally:
        sys.setswitchinterval(interval)
        entered.set()
        thread.join(2)
    assert not thread.is_alive() and not actor_errors and token.canceled


def test_commands_retain_source_but_release_python_callbacks_and_returned_copies():
    document, pixels = owner()
    class Callback:
        def __call__(self, *args):
            pass
    callback = Callback()
    ref = weakref.ref(callback)
    command = ij.resolve_selection_creation_command(document.snapshot(), request(), progress=callback, checkpoint=callback)
    del document, callback
    gc.collect()
    assert ref() is None
    assert command.source.dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()
    tx = ij.prepare_selection_creation(command)
    del command
    gc.collect()
    exact_mask(tx.preview().selection, RECT_RESULTS["ADD"])
    tx.rollback()


@pytest.mark.parametrize("dtype,samples,quiet", [(np.uint8, 3, False), (np.uint8, 4, False),
                                                 (np.float64, 1, False), (np.float32, 1, True)])
def test_core_admits_sample_descriptors_without_changing_ieee_bits(dtype, samples, quiet):
    document, pixels = owner(dtype=dtype, samples=samples, quiet=quiet)
    before, old_history = document.snapshot(), history(document)
    command = ij.resolve_selection_creation_command(before, request())
    document.commit(ij.prepare_selection_creation(command))
    preserved(document, before, old_history)
    assert document.snapshot().dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()
    record = command.record()
    assert record["source"]["shape"][-1] == samples
    assert record["source"]["sample_policy"] == ("preserve_quiet_nan_float32" if quiet else "finite_only")


def test_native_session_retains_original_action_owner_and_requires_rebind():
    scientific, _ = owner()
    # V3 has no positioned scientific Fourier representation. This established
    # codec refusal must not be mistaken for a creation-action persistence gap.
    with pytest.raises(ValueError, match="Scientific Fourier position"):
        ij.encode_session(ij.SessionCapture([scientific]))
    document, _ = owner(scientific=False)
    command = ij.resolve_selection_creation_command(document.snapshot(), request())
    record = command.record()
    document.commit(ij.prepare_selection_creation(command))
    capture = ij.SessionCapture([document], workflow=[record], extensions={"opaque": {"version": 37}})
    decoded = ij.decode_session(ij.encode_session(capture))
    reopened = decoded.documents[0]
    assert reopened.id != document.id and decoded.workflow == [record]
    assert decoded.extensions == {"opaque": {"version": 37}}
    assert records_signature(reopened.snapshot()) == records_signature(document.snapshot())
    assert selection_signature(reopened.snapshot().selection) == selection_signature(document.snapshot().selection)
    assert selection_signature(reopened.snapshot().previous_selection) == selection_signature(document.snapshot().previous_selection)
    with pytest.raises(ValueError, match="owner"):
        ij.selection_creation_command_from_record(reopened.snapshot(), decoded.workflow[0])
    rebound = ij.rebind_selection_creation_command(reopened.snapshot(), decoded.workflow[0], request(operation="REPLACE"))
    assert rebound.record()["source"]["id"] == reopened.id


QUALIFIED_NAMES = ("overlap_rectangles", "disconnected_rectangle_ellipse", "ring_and_partial_hole_fill",
                   "negative_edges", "fractional_polygon_rectangle", "duplicate_polygon")
QUALIFIED_OPERATIONS = (("union", "ADD"), ("intersection", "INTERSECT"),
                        ("subtraction", "SUBTRACT"), ("xor", "XOR"))
QUALIFIED_IDS = tuple(f"{name}__{op}" for name in QUALIFIED_NAMES for op, _ in QUALIFIED_OPERATIONS)


@pytest.fixture(scope="module")
def qualified_masks():
    path = Path(__file__).resolve().parents[1] / "fixtures/roi-boolean-imagej154r.json"
    raw = path.read_bytes()
    assert len(raw) == 88018 and hashlib.sha256(raw).hexdigest() == "407a52c8804adf0b63e9efb2951cd701f985714b696a109db0a1a174e95e4488"
    value = json.loads(raw)
    assert value["imagej_version"] == "1.54r" and value["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert len(value["cases"]) == 72
    return {row["id"]: row for row in value["cases"] if row["id"] in QUALIFIED_IDS}


@pytest.mark.parametrize("case_id", QUALIFIED_IDS)
def test_new_creation_path_reuses_identical_qualified_P05e_masks(case_id, qualified_masks):
    case = qualified_masks[case_id]
    def geometry(operand):
        spec = operand["geometry"]
        if spec["kind"] == "ring":
            # Reuse the already-qualified operand raster; do not recapture or
            # rerun upstream Boolean algebra to produce a new expected value.
            return ij.Selection.from_mask(bitmap(operand["mask_rows"]))
        if spec["kind"] in ("rectangle", "ellipse"):
            x, y, w, h = spec["xywh"]
            return ij.Selection(spec["kind"], [(x, y), (x + w, y + h)], width=3)
        return ij.Selection("polygon", spec["points"], width=3)
    document = ij.Document(ij.Dataset(np.zeros((9, 13), np.uint8), axes="YX"))
    document.select(geometry(case["operands"][0]))
    operation = dict(QUALIFIED_OPERATIONS)[case["operator"]]
    req = ij.SelectionCreationRequest(geometry(case["operands"][1]), getattr(ij.SelectionCreationOperation, operation))
    result = ij.resolve_selection_creation_command(document.snapshot(), req).result
    assert result.kind == "mask" and result.width == 1 and result.attributes == {} and result.points == []
    np.testing.assert_array_equal(result.mask((9, 13)), bitmap(case["result"]["mask_rows"]))


INVALID_CANDIDATES = (
    ("line", [(0, 0), (2, 2)], 1), ("point", [(1, 1)], 1),
    ("rotated_rectangle", [(3, 2), (5, 2), (3, 3)], 1),
    ("rotated_ellipse", [(3, 2), (5, 2), (3, 3)], 1),
    ("rectangle", [(1, 1), (1, 3)], 1), ("ellipse", [(0, 0), (float("nan"), 2)], 1),
    ("polygon", [(1, 1), (2, 2)], 1), ("freehand", [(1, 1), (2, 2)], 1),
    ("rectangle", [(1, 1), (2, 3)], 0),
)


@pytest.mark.parametrize("kind,points,width", INVALID_CANDIDATES)
def test_incomplete_nonarea_or_nonfinite_candidate_is_pre_callback_atomic(kind, points, width):
    document, _ = owner()
    before, old_history, callbacks = document.snapshot(), history(document), []
    with pytest.raises(ValueError):
        req = ij.SelectionCreationRequest(ij.Selection(kind, points, width=width))
        ij.resolve_selection_creation_command(before, req, checkpoint=lambda: callbacks.append("checkpoint"),
                                               progress=lambda *args: callbacks.append(args))
    assert callbacks == []
    unchanged(document, before, old_history)


@pytest.mark.parametrize("value,error", [(True, TypeError), (np.bool_(False), TypeError), (1.5, TypeError),
                                         ("3", TypeError), (-1, ValueError), (2**200, ValueError)])
def test_preparation_budget_uses_strict_native_integer(value, error):
    document, _ = owner()
    command = ij.resolve_selection_creation_command(document.snapshot(), request())
    with pytest.raises(error):
        ij.prepare_selection_creation(command, execution_max_working_bytes=value)


def test_estimate_charges_unrelated_retained_annotations_not_pixel_sample_storage():
    document, _ = owner()
    first = ij.estimate_selection_creation(document.snapshot(), request())
    assert first.pixels == 35
    payload = "x" * 50_000
    edit = document.begin_edit()
    edit.set_overlays([{"opaque": payload}])
    document.commit(edit)
    before, old_history = document.snapshot(), history(document)
    second = ij.estimate_selection_creation(before, request())
    # The preparation phase owns at least this UTF8 string copy in addition to
    # retained command storage. Phase peaks use max(), and replace an existing
    # overlay; no linear difference between two estimates is promised.
    assert second.working_bytes >= second.retained_bytes + len(payload.encode("utf-8"))
    command = ij.resolve_selection_creation_command(before, request())
    small = ij.SelectionCreationLimits()
    small.max_working_bytes = second.working_bytes - 1
    callbacks = []
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(before, request(), small,
            progress=lambda *args: callbacks.append(args), checkpoint=lambda: callbacks.append("checkpoint"))
    with pytest.raises(ValueError):
        ij.prepare_selection_creation(command, execution_max_working_bytes=second.working_bytes - 1,
            progress=lambda *args: callbacks.append(args), checkpoint=lambda: callbacks.append("checkpoint"))
    assert callbacks == []
    unchanged(document, before, old_history)


# Source-qualified Q paths: input flags describe geometry only. Modifier-to-mode
# classification belongs to Window, and is not inferred from these pure calls.
GEOMETRY_PATHS = (
    ("Q01", "RECTANGLE", (18, 6), (((21, 8), False, False, (18, 6, 3, 2)), ((25, 10), False, False, (18, 6, 7, 4)))),
    ("Q02", "RECTANGLE", (18, 6), (((21, 8), True, False, (18, 6, 4, 4)), ((25, 10), True, False, (18, 6, 8, 8)))),
    ("Q03", "RECTANGLE", (8, 7), (((10, 8), False, False, (8, 7, 2, 1)), ((12, 10), False, False, (8, 7, 4, 3)))),
    ("Q05", "RECTANGLE", (18, 6), (((21, 8), True, False, (18, 6, 4, 4)), ((25, 10), False, False, (18, 6, 7, 4)))),
    ("Q06", "RECTANGLE", (18, 6), (((21, 8), False, False, (18, 6, 3, 2)), ((25, 10), True, False, (18, 6, 8, 8)))),
    ("Q07", "RECTANGLE", (10, 7), (((13, 9), True, False, (10, 7, 4, 4)), ((17, 11), True, False, (10, 7, 8, 8)))),
    ("Q08", "RECTANGLE", (10, 7), (((13, 9), False, False, (10, 7, 3, 2)), ((17, 11), False, False, (10, 7, 7, 4)))),
    ("Q09", "ELLIPSE", (18, 6), (((21, 8), True, False, (17, 5, 4, 4)), ((25, 10), True, False, (17, 4, 8, 8)))),
    ("Q10", "RECTANGLE", (18, 6), (((21, 8), False, False, (18, 6, 3, 2)),)),
    ("Q17", "RECTANGLE", (18, 6), (((21, 8), True, True, (16, 4, 4, 4)), ((25, 10), True, True, (14, 2, 8, 8)))),
)


@pytest.mark.parametrize("case_id,kind,anchor,steps", GEOMETRY_PATHS, ids=[row[0] for row in GEOMETRY_PATHS])
def test_pure_box_geometry_matches_qualified_literal_drag_states(case_id, kind, anchor, steps):
    state = ij.begin_selection_creation_box(getattr(ij.SelectionCreationBoxKind, kind), 32, 24, anchor)
    assert tuple(state.bounds) == (*anchor, 0, 0) and ij.selection_creation_box_candidate(state) is None
    assert (state.image_width, state.image_height, tuple(state.anchor)) == (32, 24, anchor)
    for attr in ("kind", "image_width", "image_height", "anchor", "bounds"):
        with pytest.raises(AttributeError):
            setattr(state, attr, None)
    for pointer, constrain, center, bounds in steps:
        old = tuple(state.bounds)
        grown = ij.update_selection_creation_box(state, pointer, constrain=constrain, from_center=center)
        assert tuple(state.bounds) == old  # A value result, no mutation of the retained prior drag.
        state = grown
        assert tuple(state.bounds) == bounds
        result = ij.selection_creation_box_candidate(state)
        x, y, w, h = bounds
        assert result.kind == ("ellipse" if kind == "ELLIPSE" else "rectangle")
        assert result.points == [[x, y], [x + w, y + h]] and result.width == 1 and result.attributes == {}
    # Candidate retrieval/release performs no synthetic grow at another pointer.
    assert tuple(state.bounds) == steps[-1][-1]


@pytest.mark.parametrize("value", (True, np.bool_(False), 3.5, "3", 2**200))
def test_box_dimensions_and_points_reject_coercion(value):
    with pytest.raises((TypeError, ValueError)):
        ij.begin_selection_creation_box(ij.SelectionCreationBoxKind.RECTANGLE, value, 24, (3, 2))
    state = ij.begin_selection_creation_box(ij.SelectionCreationBoxKind.RECTANGLE, 32, 24, (3, 2))
    with pytest.raises((TypeError, ValueError)):
        ij.update_selection_creation_box(state, (value, 3))


