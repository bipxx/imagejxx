"""Pinned ImageJ Boolean masks and selection-only native publication contracts."""

import hashlib
import json
from pathlib import Path

import imagejxx as ij
from imagejxx import _native as native
import numpy as np
import pytest


WIDTH, HEIGHT = 13, 9
OPERATIONS = ("union", "intersection", "subtraction", "xor")
OPERAND_SETS = (
    "overlap_rectangles", "overlap_rectangles_reversed",
    "disconnected_rectangle_ellipse", "disconnected_rectangle_ellipse_reversed",
    "ring_and_inner_island", "ring_and_partial_hole_fill", "disconnected_rings",
    "negative_edges", "right_bottom_edges", "entirely_outside",
    "empty_area_and_rectangle", "empty_area_and_rectangle_reversed",
    "two_empty_areas", "fractional_rectangle_ellipse",
    "fractional_rectangle_ellipse_reversed", "fractional_polygon_rectangle",
    "fractional_polygon_rectangle_reversed", "duplicate_polygon",
)
ZERO_ROWS = ("0000000000000",) * HEIGHT
SOURCE_ATTRIBUTES = {"label": "Retained operand", "extension": {"key": [1, "kept"]}}

# Hand-authored from the outer/hole coordinates, independently of oracle mask
# rows and all target rasterizers. These are inputs, never computed expectations.
RING_ROWS = {
    ((1, 1, 10, 7), (3, 3, 6, 3)): (
        "0000000000000", "0111111111100", "0111111111100",
        "0110000001100", "0110000001100", "0110000001100",
        "0111111111100", "0111111111100", "0000000000000",
    ),
    ((0, 0, 5, 5), (1, 1, 3, 3)): (
        "1111100000000", "1000100000000", "1000100000000",
        "1000100000000", "1111100000000", "0000000000000",
        "0000000000000", "0000000000000", "0000000000000",
    ),
    ((7, 3, 6, 6), (8, 4, 4, 4)): (
        "0000000000000", "0000000000000", "0000000000000",
        "0000000111111", "0000000100001", "0000000100001",
        "0000000100001", "0000000100001", "0000000111111",
    ),
}


def _fixture_cases():
    path = Path(__file__).resolve().parents[1] / "fixtures/roi-boolean-imagej154r.json"
    raw = path.read_bytes()
    assert len(raw) == 88018
    assert hashlib.sha256(raw).hexdigest() == (
        "407a52c8804adf0b63e9efb2951cd701f985714b696a109db0a1a174e95e4488"
    )
    fixture = json.loads(raw)
    assert fixture["imagej_version"] == "1.54r"
    assert fixture["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    cases = fixture["cases"]
    assert [case["id"] for case in cases] == [
        f"{name}__{operation}" for name in OPERAND_SETS for operation in OPERATIONS
    ]
    assert len(cases) == 72
    return cases


CASES = _fixture_cases()


def _bitmap(rows):
    assert rows and all(len(row) == len(rows[0]) and set(row) <= {"0", "1"} for row in rows)
    values = np.frombuffer("".join(rows).encode("ascii"), dtype=np.uint8)
    return ((values - ord("0")) * 255).reshape(len(rows), len(rows[0])).copy()


def _packed(rows):
    return native.Selection.from_mask(_bitmap(rows))


def _geometry(value):
    kind = value["kind"]
    if kind == "empty_area":
        result = _packed(ZERO_ROWS)
    elif kind == "ring":
        key = (tuple(value["outer_xywh"]), tuple(value["hole_xywh"]))
        result = _packed(RING_ROWS[key])
    elif kind in ("rectangle", "ellipse"):
        x, y, width, height = value["xywh"]
        result = native.Selection(kind, [(x, y), (x + width, y + height)], width=3)
    elif kind == "polygon":
        result = native.Selection("polygon", value["points"], width=3)
    else:
        raise AssertionError(f"Unknown independently constructed geometry: {kind}")
    return result.with_attributes(SOURCE_ATTRIBUTES)


def _selection_record(value):
    if value is None:
        return None
    return (
        value.kind, tuple(tuple(point) for point in value.points),
        value.width, value.attributes,
    )


def _rois(snapshot):
    return [
        (roi.name, roi.t, roi.c, roi.z, roi.attributes, _selection_record(roi.selection))
        for roi in snapshot.rois
    ]


def _calibration(snapshot):
    value = snapshot.dataset.calibration
    return (
        tuple(value.spacing), tuple(value.origin), tuple(value.direction),
        value.unit, value.time_unit, value.frame_interval, value.time_origin,
    )


def _display(snapshot):
    value = snapshot.display
    return (
        value.t, value.c, tuple(value.crosshair), value.orthogonal,
        value.linked_zoom, value.zoom, tuple(value.plane_zoom),
        tuple(tuple(row) for row in value.pan), value.properties,
    )


def _assert_result(actual, expected_rows):
    assert isinstance(actual, native.Selection)
    assert actual.kind == "mask" and actual.width == 1
    assert actual.attributes == {} and actual.points == []
    bitmap = actual.mask((len(expected_rows), len(expected_rows[0])))
    assert bitmap.dtype == np.uint8
    np.testing.assert_array_equal(bitmap, _bitmap(expected_rows))


def _owner(dtype=np.uint16, samples=1):
    shape = (2, 2, 2, HEIGHT, WIDTH)
    axes = "TCZYX"
    if samples != 1:
        shape += (samples,)
        axes += "S"
    pixels = (np.arange(np.prod(shape)).reshape(shape) % 173).astype(dtype)
    calibration = native.SpatialCalibration(
        spacing=(0.25, 1.5, 3), origin=(-2, 4, 9),
        direction=(0, -1, 0, 1, 0, 0, 0, 0, 1), unit="um",
        frame_interval=2.5, time_origin=-3, time_unit="min",
    )
    owner = native.Document(native.Dataset(pixels, axes=axes, calibration=calibration), "Boolean owner")
    edit = owner.begin_edit()
    edit.set_property("retained", {"values": [1, None, "kept"]})
    edit.set_extension("opaque", {"tag": "extension"})
    edit.set_record_layout({"presence": True})
    edit.set_overlays([{"retained": "overlay"}])
    edit.set_rois([
        native.ROI(
            "first", native.Selection("rectangle", [(1, 1), (7, 5)], width=3,
                                      attributes=SOURCE_ATTRIBUTES),
            t=1, c=1, z=1, attributes={"role": "left"},
        ),
        native.ROI(
            "second", native.Selection("rectangle", [(4, 3), (10, 7)]),
            t=0, c=0, z=0, attributes={"role": "right"},
        ),
    ])
    edit.select(native.Selection("point", [(2, 2)]))
    edit.set_previous_selection(native.Selection("line", [(0, 0), (3, 3)]))
    owner.commit(edit)
    owner.mark_saved()
    return owner, pixels, axes


@pytest.mark.parametrize("case", CASES, ids=lambda case: case["id"])
@pytest.mark.parametrize("backend", ["native", "public"])
def test_all72_geometry_rasters_and_results(case, backend):
    combine = native.combine_selections if backend == "native" else ij.combine_selections
    operands = [_geometry(value["geometry"]) for value in case["operands"]]
    originals = [_selection_record(value) for value in operands]
    for operand, reference in zip(operands, case["operands"]):
        _assert_result(
            combine(WIDTH, HEIGHT, [operand, _packed(ZERO_ROWS)], operation="union"),
            reference["mask_rows"],
        )
    result = combine(WIDTH, HEIGHT, operands, operation=case["operator"])
    _assert_result(result, case["result"]["mask_rows"])
    assert [_selection_record(value) for value in operands] == originals


@pytest.mark.parametrize("case", CASES, ids=lambda case: case["id"])
def test_all72_captured_packed_inputs(case):
    operands = [_packed(value["mask_rows"]) for value in case["operands"]]
    before = [value.mask((HEIGHT, WIDTH)).tobytes() for value in operands]
    result = native.combine_selections(WIDTH, HEIGHT, operands, case["operator"])
    _assert_result(result, case["result"]["mask_rows"])
    assert [value.mask((HEIGHT, WIDTH)).tobytes() for value in operands] == before


@pytest.mark.parametrize("case", CASES, ids=lambda case: case["id"])
def test_all72_saved_roi_preparation(case):
    owner, _, _ = _owner()
    replace = owner.begin_edit()
    replace.set_rois([
        native.ROI("left", _geometry(case["operands"][0]["geometry"]),
                   t=1, c=1, z=1, attributes={"tag": "left"}),
        native.ROI("right", _geometry(case["operands"][1]["geometry"]),
                   t=0, c=0, z=0, attributes={"tag": "right"}),
    ])
    owner.commit(replace)
    owner.mark_saved()
    source = owner.snapshot()
    transaction = native.prepare_roi_boolean(source, [0, 1], case["operator"])
    assert transaction.active and transaction.document_id == source.id
    assert owner.snapshot().shares_data(source)
    _assert_result(transaction.preview().selection, case["result"]["mask_rows"])
    owner.commit(transaction)
    _assert_result(owner.snapshot().selection, case["result"]["mask_rows"])
    assert _rois(owner.snapshot()) == _rois(source)
    assert owner.snapshot().dataset.shares_pixels(source.dataset)
    assert not owner.snapshot().dirty


@pytest.mark.parametrize("operation,expected", [
    ("union", ("11110", "11110", "10110")),
    ("intersection", ("00000", "00000", "00000")),
    ("subtraction", ("10000", "10000", "00100")),
    ("xor", ("10010", "10010", "10110")),
])
def test_three_operand_ordered_folds(operation, expected):
    operands = [_packed(rows) for rows in [
        ("11100", "11100", "00100"),
        ("01010", "01110", "00010"),
        ("00100", "00000", "10000"),
    ]]
    _assert_result(native.combine_selections(5, 3, operands, operation), expected)
    owner = native.Document(native.Dataset(np.zeros((3, 5), np.uint8), axes="YX"))
    seed = owner.begin_edit()
    seed.set_rois([native.ROI(str(index), value) for index, value in enumerate(operands)])
    owner.commit(seed)
    transaction = native.prepare_roi_boolean(owner.snapshot(), [0, 1, 2], operation)
    owner.commit(transaction)
    _assert_result(owner.snapshot().selection, expected)


@pytest.mark.parametrize("kind,points", [
    ("freehand", [(3, 2), (7, 2), (7, 4), (3, 4)]),
    ("rotated_rectangle", [(5, 3), (7, 3), (5, 4)]),
    ("rotated_ellipse", [(5, 3), (7, 3), (5, 4)]),
])
def test_additional_area_kinds(kind, points):
    expected = (
        "0000000000000", "0000000000000", "0001111000000",
        "0001111000000", "0000000000000", "0000000000000",
        "0000000000000", "0000000000000", "0000000000000",
    )
    result = native.combine_selections(
        WIDTH, HEIGHT, [native.Selection(kind, points), _packed(ZERO_ROWS)]
    )
    _assert_result(result, expected)


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
@pytest.mark.parametrize("samples", [1, 3, 4])
def test_selection_publication_preserves_scientific_pixels_records_and_history(dtype, samples):
    owner, pixels, axes = _owner(dtype, samples)
    edit = owner.begin_edit()
    edit.replace_plane(np.full_like(owner.snapshot().dataset.plane(), 99), t=1, c=1, z=1)
    owner.commit(edit)
    owner.undo()
    source = owner.snapshot()
    before = (owner.undo_steps, owner.redo_steps, source.dirty, source.data_identity)
    transaction = native.prepare_roi_boolean(source, [0, 1])
    assert transaction.dependencies == ij.Change.DATA
    _assert_result(transaction.preview().selection, CASES[0]["result"]["mask_rows"])
    assert owner.snapshot().shares_data(source)
    owner.commit(transaction)
    result = owner.snapshot()
    _assert_result(result.selection, CASES[0]["result"]["mask_rows"])
    assert result.dataset.shares_pixels(source.dataset)
    assert (owner.undo_steps, owner.redo_steps, result.dirty, result.data_identity) == before
    assert result.revisions.pixels == source.revisions.pixels
    assert result.revisions.metadata == source.revisions.metadata
    assert result.revisions.selection == source.revisions.selection + 1
    assert result.metadata == source.metadata and result.record_layout == source.record_layout
    assert result.overlays == source.overlays and _rois(result) == _rois(source)
    assert _calibration(result) == _calibration(source) and _display(result) == _display(source)
    assert _selection_record(result.previous_selection) == _selection_record(source.selection)
    assert result.dataset.numpy(axes).tobytes() == pixels.tobytes()
    assert source.dataset.numpy(axes).tobytes() == pixels.tobytes()
    # Exact selection equality is a no-op, including previous selection and redo.
    repeated = native.prepare_roi_boolean(result, [0, 1])
    owner.commit(repeated)
    after_noop = owner.snapshot()
    assert after_noop.revisions.selection == result.revisions.selection
    assert _selection_record(after_noop.previous_selection) == _selection_record(result.previous_selection)
    assert (owner.undo_steps, owner.redo_steps, after_noop.dirty) == before[:3]


@pytest.mark.parametrize("operation", OPERATIONS)
def test_quiet_nan_payloads_and_signed_zero_survive_without_policy_change(operation):
    raw = np.zeros((HEIGHT, WIDTH), dtype=np.uint32)
    raw.flat[:6] = [0x7FC00001, 0xFFC12345, 0x80000000, 0x00000001, 0x3F800000, 0xC0200000]
    pixels = raw.view(np.float32)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    owner = native.Document(native.Dataset(pixels, axes="YX", sample_policy=policy))
    seed = owner.begin_edit()
    seed.set_rois([
        native.ROI("left", native.Selection("rectangle", [(1, 1), (7, 5)])),
        native.ROI("right", native.Selection("rectangle", [(4, 3), (10, 7)])),
    ])
    owner.commit(seed)
    owner.mark_saved()
    source = owner.snapshot()
    tx = native.prepare_roi_boolean(source, [0, 1], operation)
    owner.commit(tx)
    assert owner.snapshot().dataset.shares_pixels(source.dataset)
    assert owner.snapshot().dataset.sample_policy == policy
    assert owner.snapshot().dataset.numpy("YX").view(np.uint32).tobytes() == raw.tobytes()
    assert pixels.view(np.uint32).tobytes() == raw.tobytes()
    assert not owner.snapshot().dirty


def test_store_result_is_separate_unpositioned_roi_edit_with_undo_redo():
    owner, _, _ = _owner()
    assert owner.combine_rois([0, 1]) is None
    source = owner.snapshot()
    prior_undo = owner.undo_steps
    rois = source.rois + [native.ROI("stored result", source.selection, attributes={"note": "separate"})]
    save = owner.begin_edit()
    save.set_rois(rois)
    owner.commit(save)
    stored = owner.snapshot().rois[-1]
    assert (stored.t, stored.c, stored.z) == (None, None, None)
    assert owner.undo_steps == prior_undo + 1 and owner.snapshot().dirty
    owner.undo()
    assert _rois(owner.snapshot()) == _rois(source) and not owner.snapshot().dirty
    _assert_result(owner.snapshot().selection, CASES[0]["result"]["mask_rows"])
    owner.redo()
    assert owner.snapshot().rois[-1].name == "stored result" and owner.snapshot().dirty


def test_foreign_owner_closed_rollback_and_retained_snapshot():
    owner, _, _ = _owner()
    source = owner.snapshot()
    transaction = native.prepare_roi_boolean(source, [0, 1])
    preview = transaction.preview()
    foreign = native.Document(source)
    with pytest.raises(ValueError, match="another document"):
        foreign.commit(transaction)
    assert transaction.active and owner.snapshot().shares_data(source)
    owner.commit(transaction)
    with pytest.raises(ValueError, match="closed"):
        owner.commit(transaction)
    aborted = native.prepare_roi_boolean(owner.snapshot(), [0, 1], "xor")
    aborted.rollback()
    with pytest.raises(ValueError, match="closed"):
        owner.commit(aborted)
    del owner
    _assert_result(preview.selection, CASES[0]["result"]["mask_rows"])
    assert len(source.rois) == 2 and source.metadata["title"] == "Boolean owner"


@pytest.mark.parametrize("change", ["pixels", "metadata", "rois", "selection"])
def test_stale_data_categories_reject_without_mutating_current_owner(change):
    owner, _, _ = _owner()
    transaction = native.prepare_roi_boolean(owner.snapshot(), [0, 1])
    if change == "selection":
        owner.select(native.Selection("point", [(4, 4)]))
    else:
        concurrent = owner.begin_edit()
        if change == "pixels":
            concurrent.replace_plane(np.full((HEIGHT, WIDTH), 91, np.uint16))
        elif change == "metadata":
            concurrent.set_title("concurrent metadata")
        else:
            rois = owner.snapshot().rois
            concurrent.set_rois([
                native.ROI("renamed", rois[0].selection, t=1, c=1, z=1,
                           attributes=rois[0].attributes), rois[1],
            ])
        owner.commit(concurrent)
    current = owner.snapshot()
    with pytest.raises(ij.RevisionConflict):
        owner.commit(transaction)
    assert transaction.active and owner.snapshot().shares_data(current)


def test_display_navigation_and_saved_positions_are_geometrically_independent():
    owner, _, _ = _owner()
    transaction = native.prepare_roi_boolean(owner.snapshot(), [0, 1])
    view = owner.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = 1, 1, [12, 8, 1], 2.25
    owner.set_display(view)
    navigated = owner.snapshot()
    owner.commit(transaction)
    assert _display(owner.snapshot()) == _display(navigated)
    _assert_result(owner.snapshot().selection, CASES[0]["result"]["mask_rows"])
    assert _rois(owner.snapshot()) == _rois(navigated)


def test_distinct_rows_with_duplicate_geometry_and_explicit_empty_result():
    owner, _, _ = _owner()
    edit = owner.begin_edit()
    first = owner.snapshot().rois[0]
    edit.set_rois([first, native.ROI("same geometry, distinct row", first.selection)])
    owner.commit(edit)
    owner.mark_saved()
    before = owner.snapshot()
    owner.combine_rois([0, 1], "xor")
    _assert_result(owner.snapshot().selection, ZERO_ROWS)
    assert owner.snapshot().selection is not None
    assert _rois(owner.snapshot()) == _rois(before) and not owner.snapshot().dirty


@pytest.mark.parametrize("indices", [[], [0], [0, 0], [0, 2], [99, 1], [-1, 1], [True, 1], [0.0, 1]])
def test_invalid_manager_indices_are_atomic(indices):
    owner, _, _ = _owner()
    source = owner.snapshot()
    before = owner.undo_steps, owner.redo_steps
    with pytest.raises((ValueError, IndexError, TypeError, OverflowError)):
        native.prepare_roi_boolean(source, indices)
    with pytest.raises((ValueError, IndexError, TypeError, OverflowError)):
        owner.combine_rois(indices)
    assert owner.snapshot().shares_data(source)
    assert (owner.undo_steps, owner.redo_steps) == before


@pytest.mark.parametrize("operation", ["", "OR", "subtract", "unknown", None, 1])
def test_invalid_operations_reject_pure_and_manager_paths(operation):
    owner, _, _ = _owner()
    source = owner.snapshot()
    operands = [roi.selection for roi in source.rois]
    with pytest.raises((ValueError, TypeError)):
        native.combine_selections(WIDTH, HEIGHT, operands, operation)
    with pytest.raises((ValueError, TypeError)):
        native.prepare_roi_boolean(source, [0, 1], operation)
    with pytest.raises((ValueError, TypeError)):
        owner.combine_rois([0, 1], operation)
    assert owner.snapshot().shares_data(source)


@pytest.mark.parametrize("kind,points", [
    ("none", []), ("point", [(1, 1)]), ("multipoint", [(1, 1), (2, 2)]),
    ("line", [(1, 1), (2, 2)]), ("polyline", [(1, 1), (2, 2), (3, 1)]),
    ("angle", [(1, 1), (2, 2), (3, 1)]),
])
def test_nonarea_operands_reject(kind, points):
    rectangle = native.Selection("rectangle", [(1, 1), (5, 4)])
    with pytest.raises(ValueError):
        native.combine_selections(WIDTH, HEIGHT, [rectangle, native.Selection(kind, points)])
    if kind != "none":
        owner, _, _ = _owner()
        edit = owner.begin_edit()
        edit.set_rois([
            native.ROI("area", rectangle),
            native.ROI("nonarea", native.Selection(kind, points)),
        ])
        owner.commit(edit)
        source = owner.snapshot()
        with pytest.raises(ValueError):
            native.prepare_roi_boolean(source, [0, 1])
        assert owner.snapshot().shares_data(source)


@pytest.mark.parametrize("width,height", [
    (0, 9), (-1, 9), (13, 0), (13, -1), (2**31 - 1, 1), (1, 2**31 - 1),
])
def test_invalid_dimensions_reject(width, height):
    rectangle = native.Selection("rectangle", [(1, 1), (5, 4)])
    with pytest.raises(ValueError):
        native.combine_selections(width, height, [rectangle, rectangle])


@pytest.mark.parametrize("operands", [[], [native.Selection("rectangle", [(1, 1), (5, 4)])]])
def test_insufficient_operands_reject(operands):
    with pytest.raises(ValueError):
        native.combine_selections(WIDTH, HEIGHT, operands)


def test_mismatched_mask_shape_rejects_without_changing_inputs():
    rectangle = native.Selection("rectangle", [(1, 1), (5, 4)])
    wrong = _packed(("11100", "00100", "00000"))
    before = wrong.mask((3, 5)).tobytes()
    with pytest.raises(ValueError):
        native.combine_selections(WIDTH, HEIGHT, [rectangle, wrong])
    with pytest.raises(ValueError):
        native.combine_selections(WIDTH, HEIGHT, [rectangle, native.Selection("mask")])
    assert wrong.mask((3, 5)).tobytes() == before


@pytest.mark.parametrize("coordinate", [float("nan"), float("inf"), -float("inf")])
def test_nonfinite_geometry_rejects(coordinate):
    rectangle = native.Selection("rectangle", [(1, 1), (5, 4)])
    invalid = native.Selection("rectangle", [(coordinate, 1), (3, 4)])
    with pytest.raises(ValueError):
        native.combine_selections(WIDTH, HEIGHT, [rectangle, invalid])


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_zero_extent_rejects(kind):
    invalid = native.Selection(kind, [(2, 2), (2, 4)])
    with pytest.raises(ValueError):
        native.combine_selections(WIDTH, HEIGHT, [invalid, _packed(ZERO_ROWS)])


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_wide_finite_extension_remains_full_canvas(kind):
    value = native.Selection(kind, [(-1e100, -1e100), (1e100, 1e100)])
    _assert_result(
        native.combine_selections(WIDTH, HEIGHT, [value, _packed(ZERO_ROWS)]),
        ("1111111111111",) * HEIGHT,
    )


@pytest.mark.parametrize("operation", OPERATIONS)
def test_detached_manager_uses_native_boolean_and_preserves_document_state(operation):
    from imagejxx.ui.model import Document, NamedROI, Selection

    pixels = np.arange(WIDTH * HEIGHT, dtype=np.uint16).reshape(HEIGHT, WIDTH)
    document = Document(pixels)
    document.selection = Selection("point", [(2, 8)], attributes={"label": "previous"})
    document.rois = [
        NamedROI("first", Selection("rectangle", [(1, 1), (7, 5)], width=3),
                 slice_index=0, attributes={"kept": [1, 2]}),
        NamedROI("second", Selection("rectangle", [(4, 3), (10, 7)], width=4),
                 slice_index=None, attributes={"kept": [3, 4]}),
    ]
    saved_rois = [roi.to_dict() for roi in document.rois]
    old_selection = document.selection.to_dict()
    calibration = document.calibration
    state = (document.revision, document.dirty, document.can_undo, document.can_redo)
    expected = next(case for case in CASES if case["id"] == f"overlap_rectangles__{operation}")

    result = document.combine_rois([0, 1], operation)
    assert result.kind == "mask" and result.width == 1 and result.attributes == {}
    np.testing.assert_array_equal(result.mask(document.shape), _bitmap(expected["result"]["mask_rows"]))
    assert document.previous_selection.to_dict() == old_selection
    assert [roi.to_dict() for roi in document.rois] == saved_rois
    assert document.calibration is calibration
    assert (document.revision, document.dirty, document.can_undo, document.can_redo) == state
    np.testing.assert_array_equal(document.data, pixels)

    # A repeated exact result preserves the previous selection; the returned
    # detached record cannot mutate the document's authoritative selection.
    repeated = document.combine_rois([0, 1], operation)
    assert document.previous_selection.to_dict() == old_selection
    repeated.bitmap.fill(0)
    np.testing.assert_array_equal(document.selection.mask(document.shape), _bitmap(expected["result"]["mask_rows"]))


@pytest.mark.parametrize("dtype", [
    np.bool_, np.uint8, np.uint16, np.float32, np.float64,
    np.int8, np.int16, np.int32, np.int64, np.uint32, np.uint64, np.float16,
])
def test_detached_bitmap_transport_retains_finite_nonzero_samples(dtype):
    from imagejxx.ui.model import Selection

    values = [[0, 0.5, -0.25], [1, 0, 0.125]]
    if np.dtype(dtype).kind in "iu":
        values = [[0, 1, 2], [3, 0, 4]]
    selection = Selection("mask", bitmap=np.asarray(values, dtype=dtype),
                          width=2, attributes={"label": "bitmap"})
    expected = np.asarray([[0, 255, 255], [255, 0, 255]], dtype=np.uint8)
    np.testing.assert_array_equal(selection.mask((2, 3)), expected)
    native_selection = selection.native()
    assert native_selection.width == 2 and native_selection.attributes == {"label": "bitmap"}


@pytest.mark.parametrize("operation", OPERATIONS)
def test_detached_initial_equivalent_mask_is_an_exact_native_noop(operation):
    from imagejxx.ui.model import Document, NamedROI, Selection

    document = Document(np.zeros((HEIGHT, WIDTH), dtype=np.uint8))
    document.rois = [
        NamedROI("first", Selection("rectangle", [(1, 1), (7, 5)])),
        NamedROI("second", Selection("rectangle", [(4, 3), (10, 7)])),
    ]
    expected = next(case for case in CASES if case["id"] == f"overlap_rectangles__{operation}")
    document.selection = Selection("mask", bitmap=_bitmap(expected["result"]["mask_rows"]))
    document.previous_selection = Selection("point", [(2, 2)])
    original, previous = document.selection, document.previous_selection
    result = document.combine_rois([0, 1], operation)
    assert document.selection is original and document.previous_selection is previous
    assert result.native() == original.native()
    assert result.native() != ij.Selection("point", [(2, 2)])


@pytest.mark.parametrize("indices", [[], [0], [0, 0], [0, 2], [99, 1], [-1, 1], [True, 1], [0.0, 1]])
def test_detached_invalid_indices_preserve_selection_and_history(indices):
    from imagejxx.ui.model import Document, NamedROI, Selection

    document = Document(np.zeros((HEIGHT, WIDTH), dtype=np.uint8))
    document.selection = Selection("point", [(1, 1)])
    document.previous_selection = Selection("point", [(2, 2)])
    document.rois = [
        NamedROI("first", Selection("rectangle", [(1, 1), (7, 5)])),
        NamedROI("second", Selection("rectangle", [(4, 3), (10, 7)])),
    ]
    before = (document.selection.to_dict(), document.previous_selection.to_dict(),
              document.revision, document.dirty, document.can_undo, document.can_redo)
    with pytest.raises((ValueError, IndexError, TypeError)):
        document.combine_rois(indices)
    assert (document.selection.to_dict(), document.previous_selection.to_dict(),
            document.revision, document.dirty, document.can_undo, document.can_redo) == before
