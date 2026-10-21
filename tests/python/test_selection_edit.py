"""Native ROI geometry and actual input through the shared ImGui canvas."""

import numpy as np
import pytest

import imagejxx as ij


STYLE = {"label": "Tracked", "color": [12, 34, 56], "extension": {"tag": "kept"}}


def shape(kind="rectangle", points=((2, 3), (6, 5))):
    return ij.Selection(kind, points, width=3, attributes=STYLE)


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
@pytest.mark.parametrize("reverse", [False, True])
@pytest.mark.parametrize(
    "handle,target,expected",
    [
        (0, (0, 1), [(0, 1), (6, 5)]),
        (1, (4, 1), [(2, 1), (6, 5)]),
        (2, (8, 1), [(2, 1), (8, 5)]),
        (3, (8, 4), [(2, 3), (8, 5)]),
        (4, (8, 7), [(2, 3), (8, 7)]),
        (5, (4, 7), [(2, 3), (6, 7)]),
        (6, (0, 7), [(0, 3), (6, 7)]),
        (7, (0, 4), [(0, 3), (6, 5)]),
    ],
)
def test_resize_handles_and_reversed_corners(kind, reverse, handle, target, expected):
    points = [(6, 5), (2, 3)] if reverse else [(2, 3), (6, 5)]
    editor = ij.SelectionEditor(shape(kind, points), (12, 20))
    result = editor.resized(handle, target)
    np.testing.assert_allclose(result.points, expected)
    assert result.kind == kind and result.width == 3 and result.attributes == STYLE
    assert len(editor.handles()) == 8
    np.testing.assert_allclose(editor.selection.points, points)


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_aspect_center_and_crossing_anchor(kind):
    editor = ij.SelectionEditor(shape(kind), (12, 20))
    np.testing.assert_allclose(
        editor.resized(4, (10, 9), preserve_aspect=True).points, [(2, 3), (14, 9)]
    )
    np.testing.assert_allclose(
        editor.resized(3, (8, 4), preserve_aspect=True, from_center=True).points,
        [(0, 2), (8, 6)],
    )
    np.testing.assert_allclose(editor.resized(0, (10, 8)).points, [(6, 5), (10, 8)])


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
@pytest.mark.parametrize("handle", range(8))
@pytest.mark.parametrize("aspect,center", [(False,False),(True,False),(False,True),(True,True)])
def test_collapsed_resize_refuses_then_crosses_without_mutating_source(kind,handle,aspect,center):
    editor=ij.SelectionEditor(shape(kind,[(3,2),(9,6)]),(12,20))
    dx,dy=((-1,-1),(0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0))[handle]
    anchor=(6 if center or not dx else 9 if dx<0 else 3,
            4 if center or not dy else 6 if dy<0 else 2)
    with pytest.raises(ValueError,match="resize is empty"):
        editor.resized(handle,anchor,preserve_aspect=aspect,from_center=center)
    result=editor.resized(handle,(anchor[0]-dx,anchor[1]-dy),preserve_aspect=aspect,from_center=center)
    assert result.points[1][0]>result.points[0][0] and result.points[1][1]>result.points[0][1]
    assert editor.selection.points==[[3,2],[9,6]] and result.attributes==STYLE


def test_hit_testing_in_calibrated_axes_and_interiors():
    editor = ij.SelectionEditor(shape(), (12, 20))
    assert editor.hit_test((2, 1.5), (0.5, 2)).kind == ij.SelectionHandleKind.RESIZE
    assert editor.hit_test((2, 1.5), (0.5, 0.5)) is None
    assert editor.hit_test((4, 4), (0.1, 0.1)).kind == ij.SelectionHandleKind.MOVE
    assert editor.hit_test((19, 11), (0.1, 0.1)) is None
    oval = ij.SelectionEditor(shape("ellipse"), (12, 20))
    assert oval.hit_test((2.2, 3.2), (0.01, 0.01)) is None
    assert oval.hit_test((4, 4), (0.01, 0.01)).kind == ij.SelectionHandleKind.MOVE


def test_move_clamping_noop_and_detached_properties():
    editor = ij.SelectionEditor(shape(), (12, 20))
    np.testing.assert_allclose(
        editor.moved((100, -100), keep_inside=True).points, [(16, 0), (20, 2)]
    )
    assert editor.moved((0, 0)).attributes == STYLE
    attrs = editor.selection.attributes
    attrs["extension"]["tag"] = "changed"
    points = editor.selection.points
    points[0][0] = 99
    assert editor.selection.attributes == STYLE
    assert editor.selection.points[0] == [2, 3]


@pytest.mark.parametrize("kind", ["polygon", "freehand", "polyline", "line", "angle"])
def test_vertex_move_and_line_hit(kind):
    points = [(1, 1), (10, 1)] if kind == "line" else [(1, 1), (10, 1), (10, 9)]
    editor = ij.SelectionEditor(shape(kind, points), (12, 20))
    assert editor.hit_test((5, 1), (0.01, 0.01)).kind == ij.SelectionHandleKind.MOVE
    result = editor.moved_vertex(1, (11, 2))
    assert result.points[1] == [11, 2]
    assert result.attributes == STYLE and result.width == 3
    assert editor.selection.points[1] == [10, 1]


def test_nearest_insertion_preserves_order_and_calibration():
    polygon = shape("polygon", [(1, 1), (10, 1), (10, 9), (1, 9)])
    editor = ij.SelectionEditor(polygon, (12, 20))
    assert editor.insertion_index((5, 1)) == 1
    assert editor.insertion_index((1, 5)) == 4
    assert editor.insertion_index((9, 2), (1, 10)) == 2
    assert editor.insertion_index((9, 2), (10, 1)) == 1
    added = editor.inserted_vertex(4, (1, 5))
    assert added.points[-1] == [1, 5] and added.attributes == STYLE
    removed = ij.SelectionEditor(added, (12, 20)).removed_vertex(4)
    assert removed.points == polygon.points


def test_point_sets_and_line_conversion():
    points = ij.SelectionEditor(shape("multipoint", [(1, 1), (10, 1)]), (12, 20))
    assert points.hit_test((5, 1), (0.1, 0.1)) is None
    assert points.insertion_index((5, 1)) == 2
    assert points.hit_test((1, 1), (0.1, 0.1)).kind == ij.SelectionHandleKind.VERTEX
    point = ij.SelectionEditor(shape("point", [(1, 1)]), (12, 20))
    assert point.inserted_vertex(1, (2, 3)).kind == "multipoint"
    line = ij.SelectionEditor(shape("line", [(1, 1), (10, 1)]), (12, 20))
    assert line.inserted_vertex(1, (5, 2)).kind == "polyline"


@pytest.mark.parametrize("dx,shift", [(-0.5, -1), (0.5, 0), (1.5, 1), (2.25, 2)])
def test_packed_mask_translation_half_ties_and_disconnected_regions(dx, shift):
    bitmap = np.zeros((12, 20), np.uint8)
    bitmap[3, 2] = bitmap[7, 8] = 255
    selection = ij.Selection.from_mask(bitmap).with_attributes(STYLE)
    editor = ij.SelectionEditor(selection, bitmap.shape)
    assert editor.bounds == [2, 3, 9, 8]
    assert not editor.handles() and editor.hit_test((5, 5), (0.1, 0.1)) is None
    assert editor.hit_test((2.5, 3.5), (0.1, 0.1)).kind == ij.SelectionHandleKind.MOVE
    moved = editor.moved((dx, -1.5))
    expected = np.zeros_like(bitmap)
    expected[1, 2 + shift] = expected[5, 8 + shift] = 255
    np.testing.assert_array_equal(moved.mask(bitmap.shape), expected)
    assert moved.attributes == STYLE
    np.testing.assert_array_equal(editor.selection.mask(bitmap.shape), bitmap)


def test_empty_mask_and_narrow_non_byte_aligned_shape():
    bitmap = np.zeros((3, 5), np.uint8)
    empty = ij.SelectionEditor(ij.Selection.from_mask(bitmap), bitmap.shape)
    assert empty.bounds == [0, 0, 0, 0] and not empty.handles()
    assert empty.hit_test((1, 1), (0.1, 0.1)) is None
    bitmap[2, 4] = 255
    editor = ij.SelectionEditor(ij.Selection.from_mask(bitmap), bitmap.shape)
    assert editor.bounds == [4, 2, 5, 3]
    expected = np.zeros_like(bitmap)
    expected[0, 0] = 255
    np.testing.assert_array_equal(editor.moved((-4, -2)).mask(bitmap.shape), expected)


@pytest.mark.parametrize(
    "operation",
    [
        lambda e: e.resized(8, (4, 4)),
        lambda e: e.resized(0, (float("nan"), 4)),
        lambda e: e.moved((float("inf"), 0)),
        lambda e: e.hit_test((3, 4), (0, 1)),
        lambda e: e.hit_test((3, 4), (1, -1)),
        lambda e: e.moved_vertex(0, (3, 4)),
        lambda e: e.inserted_vertex(0, (3, 4)),
        lambda e: e.removed_vertex(0),
        lambda e: e.insertion_index((3, 4)),
    ],
)
def test_invalid_box_operations_are_immutable(operation):
    editor = ij.SelectionEditor(shape(), (12, 20))
    with pytest.raises(ValueError):
        operation(editor)
    assert (
        editor.selection.points == [[2, 3], [6, 5]]
        and editor.selection.attributes == STYLE
    )


@pytest.mark.parametrize(
    "kind,points",
    [
        ("polygon", [(1, 1), (10, 1), (1, 9)]),
        ("freehand", [(1, 1), (10, 1), (1, 9)]),
        ("angle", [(1, 1), (10, 1), (10, 9)]),
        ("line", [(1, 1), (10, 1)]),
        ("polyline", [(1, 1), (10, 1)]),
        ("point", [(1, 1)]),
        ("multipoint", [(1, 1)]),
    ],
)
def test_deletion_respects_geometry_minima(kind, points):
    editor = ij.SelectionEditor(shape(kind, points), (12, 20))
    with pytest.raises(ValueError, match="remove"):
        editor.removed_vertex(0)


def test_selection_transaction_previous_value_and_stale_rejection():
    document = ij.Document(
        ij.Dataset(np.arange(240, dtype=np.uint16).reshape(12, 20), axes="YX")
    )
    document.select(shape())
    source = document.snapshot()
    staged = source.begin_edit()
    staged.select(ij.SelectionEditor(source.selection, (12, 20)).moved((1, 1)))
    assert document.snapshot().selection.points == source.selection.points
    document.commit(staged)
    assert document.snapshot().previous_selection.points == source.selection.points
    assert not document.snapshot().dirty and document.undo_steps == 0
    stale = source.begin_edit()
    stale.select(shape())
    with pytest.raises(ij.RevisionConflict):
        document.commit(stale)
    np.testing.assert_array_equal(
        document.snapshot().dataset.numpy("YX"), source.dataset.numpy("YX")
    )


def pos(x, y, **extra):
    return {"type": "position", "point": [x, y], **extra}


def button(down):
    return {"type": "button", "name": "left", "down": down}


def key(name, down):
    return {"type": "key", "name": name, "down": down}


def drag(start, end, modifiers=()):
    return [
        pos(*start),
        *[key(m, True) for m in modifiers],
        button(True),
        pos(*end),
        {"type": "wait", "frames": 2},
        button(False),
        *[key(m, False) for m in modifiers],
        {"type": "wait", "frames": 2},
    ]


@pytest.fixture
def canvas():
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor

    pixels = np.arange(24 * 32, dtype=np.uint16).reshape(24, 32)
    dataset = ij.Dataset(
        pixels, axes="YX", calibration=ij.SpatialCalibration(spacing=(0.5, 1.5, 2))
    )
    owner = ij.Document(dataset)
    owner.select(shape("rectangle", [(8, 7), (18, 15)]))
    editor = Editor()
    editor.add_native_document(owner)
    editor.action("edit_selection")
    return editor, owner, pixels


def assert_clean(owner, pixels):
    assert not owner.snapshot().dirty and owner.undo_steps == 0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), pixels)


def pointer(entry):
    return (np.array(entry["mouse_screen"]) - entry["canvas_origin"]) / entry[
        "canvas_scale"
    ]


def assert_requested_points(actual, expected, result):
    # ImGui aligns delivered pointer positions to screen pixels. Keep the
    # quantization bound explicit in display units rather than relaxing the
    # native geometry tests' exact coordinate contract.
    error = np.abs(np.asarray(actual) - expected) * result["ordinary_canvas"]["scale"]
    np.testing.assert_array_less(error, 1.0001)


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_imgui_move_is_preview_until_release(canvas, kind):
    editor, owner, pixels = canvas
    owner.select(shape(kind, [(8, 7), (18, 15)]))
    original = owner.snapshot()
    result = editor.render_headless(events=drag((13, 11), (16, 13)))
    preview = result["input_trace"][2]
    assert (
        preview["gesture_active"]
        and preview["selection_revision"] == original.revisions.selection
    )
    np.testing.assert_allclose(
        preview["selection"]["points"], original.selection.points
    )
    np.testing.assert_allclose(
        preview["preview"]["points"],
        np.array(original.selection.points)
        + pointer(result["input_trace"][2])
        - pointer(result["input_trace"][0]),
        atol=1e-5,
    )
    np.testing.assert_allclose(
        owner.snapshot().selection.points, preview["preview"]["points"], atol=1e-5
    )
    assert_requested_points(
        owner.snapshot().selection.points, [(11, 9), (21, 17)], result
    )
    assert owner.snapshot().selection.kind == kind
    assert owner.snapshot().selection.attributes == STYLE
    assert owner.snapshot().revisions.selection == original.revisions.selection + 1
    assert owner.snapshot().previous_selection.points == original.selection.points
    assert not result["input_trace"][-1]["gesture_active"]
    assert_clean(owner, pixels)


@pytest.mark.parametrize(
    "modifiers,expected",
    [
        ((), [(8, 7), (23, 18)]),
        (("Shift",), [(8, 7), (23, 19)]),
        (("Ctrl",), [(3, 4), (23, 18)]),
        (("Shift", "Ctrl"), [(3, 3), (23, 19)]),
    ],
)
def test_imgui_resize_modifiers(canvas, modifiers, expected):
    editor, owner, pixels = canvas
    result = editor.render_headless(events=drag((18, 15), (23, 18), modifiers))
    assert not editor.error
    actual = np.array(owner.snapshot().selection.points)
    assert_requested_points(actual, expected, result)
    if "Shift" in modifiers:
        assert (actual[1, 0] - actual[0, 0]) / (
            actual[1, 1] - actual[0, 1]
        ) == pytest.approx(10 / 8)
    if "Ctrl" in modifiers:
        np.testing.assert_allclose(actual.mean(axis=0), [13, 11])
    else:
        np.testing.assert_array_equal(actual[0], [8, 7])
    assert_clean(owner, pixels)


def test_imgui_shift_move_uses_physical_axes(canvas):
    editor, owner, pixels = canvas
    result = editor.render_headless(events=drag((13, 11), (16, 15), ("Shift",)))
    positions = [
        entry for entry in result["input_trace"] if entry["type"] == "position"
    ]
    dy = (pointer(positions[-1]) - pointer(positions[0]))[1]
    np.testing.assert_allclose(
        owner.snapshot().selection.points, [[8, 7 + dy], [18, 15 + dy]], atol=1e-5
    )
    assert_requested_points(
        owner.snapshot().selection.points, [(8, 11), (18, 19)], result
    )
    assert_clean(owner, pixels)


@pytest.mark.parametrize("cancel", ["Escape", "stale", "tool"])
def test_imgui_cancels_drag_without_overwriting_source(canvas, cancel):
    editor, owner, pixels = canvas
    original = owner.snapshot()
    events = [pos(13, 11), button(True), pos(16, 13), {"type": "wait"}]
    if cancel == "Escape":
        events += [key("Escape", True), key("Escape", False)]
    else:
        events += [
            {
                "type": "action",
                "name": "select_all" if cancel == "stale" else "edit_selection",
            }
        ]
    events += [button(False), {"type": "wait"}]
    result = editor.render_headless(events=events)
    expected = [[0, 0], [32, 24]] if cancel == "stale" else original.selection.points
    np.testing.assert_allclose(owner.snapshot().selection.points, expected)
    assert owner.snapshot().revisions.selection == original.revisions.selection + (
        cancel == "stale"
    )
    assert not result["input_trace"][-1]["gesture_active"]
    assert_clean(owner, pixels)


def test_imgui_invalid_resize_discards_last_valid_preview(canvas):
    editor, owner, pixels = canvas
    # Positive-size off-canvas boxes are valid under the P05e area policy.
    # The held pointer moves safely left of the image; resize clamps X to0.
    # A free NE resize remains positive-width, but Ctrl uses the source's
    # center X0 and collapses its width. Discard that invalid preview rather
    # than resurrecting the preceding valid one on release.
    owner.select(shape("rectangle", [(-5, 7), (5, 15)]))
    original = owner.snapshot()
    events = [
        pos(5, 7),
        button(True),
        pos(-5, 5),
        key("Ctrl", True),
        key("Ctrl", False),
        button(False),
        {"type": "wait"},
    ]
    result = editor.render_headless(events=events)
    assert editor.error
    valid = result["input_trace"][2]
    assert valid["gesture_active"] and valid["preview"]
    assert pointer(valid)[0] < 0
    assert valid["preview"]["points"][0][0] == -5
    assert valid["preview"]["points"][1][0] == 0
    assert valid["preview"]["points"][0][1] < 15
    assert valid["selection_revision"] == original.revisions.selection
    assert all(
        not entry["gesture_active"] and not entry["preview"]
        for entry in result["input_trace"][3:]
    )
    assert owner.snapshot().selection.points == original.selection.points
    assert owner.snapshot().revisions.selection == original.revisions.selection
    assert_clean(owner, pixels)


def test_imgui_keyboard_nudge_and_shift_step(canvas):
    editor, owner, pixels = canvas
    editor.render_headless(
        events=[
            pos(13, 11),
            key("Right", True),
            key("Right", False),
            key("Shift", True),
            key("Down", True),
            key("Down", False),
            key("Shift", False),
        ]
    )
    np.testing.assert_allclose(owner.snapshot().selection.points, [(9, 16), (19, 24)])
    assert_clean(owner, pixels)


def test_imgui_vertex_drag_insert_delete_and_minimum(canvas):
    editor, owner, pixels = canvas
    owner.select(shape("polygon", [(8, 7), (18, 7), (18, 15), (8, 15)]))
    result = editor.render_headless(events=drag((8, 7), (6, 5)))
    np.testing.assert_allclose(
        owner.snapshot().selection.points[0],
        pointer(result["input_trace"][2]),
        atol=1e-5,
    )
    assert_requested_points(owner.snapshot().selection.points[0], [6, 5], result)
    result = editor.render_headless(
        events=[pos(12, 11), key("Delete", True), key("Delete", False)]
    )
    assert len(owner.snapshot().selection.points) == 3
    before = owner.snapshot()
    editor.render_headless(
        events=[pos(12, 11), key("Delete", True), key("Delete", False)]
    )
    assert editor.error and owner.snapshot().selection.points == before.selection.points
    assert_clean(owner, pixels)


def test_imgui_alt_insert_on_closing_edge(canvas):
    editor, owner, pixels = canvas
    owner.select(shape("polygon", [(8, 7), (18, 7), (18, 15), (8, 15)]))
    result = editor.render_headless(
        events=[
            pos(8, 11),
            key("Alt", True),
            button(True),
            button(False),
            key("Alt", False),
        ]
    )
    assert not editor.error
    np.testing.assert_allclose(
        owner.snapshot().selection.points[-1],
        pointer(result["input_trace"][0]),
        atol=1e-5,
    )
    assert_requested_points(owner.snapshot().selection.points[-1], [8, 11], result)
    assert len(owner.snapshot().selection.points) == 5
    editor.render_headless(
        events=[pos(8, 11), key("Delete", True), key("Delete", False)]
    )
    assert len(owner.snapshot().selection.points) == 4
    assert_clean(owner, pixels)


def test_imgui_mask_move_and_hole_hit(canvas):
    editor, owner, pixels = canvas
    bitmap = np.zeros_like(pixels, dtype=np.uint8)
    bitmap[7:9, 8:10] = bitmap[15:17, 18:20] = 255
    owner.select(ij.Selection.from_mask(bitmap).with_attributes(STYLE))
    before = owner.snapshot()
    editor.render_headless(events=drag((13, 11), (16, 13)))
    assert owner.snapshot().revisions.selection == before.revisions.selection
    result = editor.render_headless(events=drag((8.5, 7.5), (11.5, 9.5)))
    assert result["input_trace"][2]["preview"]
    expected = np.zeros_like(bitmap)
    expected[9:11, 11:13] = expected[17:19, 21:23] = 255
    np.testing.assert_array_equal(
        owner.snapshot().selection.mask(pixels.shape), expected
    )
    assert owner.snapshot().selection.attributes == STYLE
    assert_clean(owner, pixels)


@pytest.mark.parametrize(
    "invalid",
    [
        {"type": "position", "point": [1]},
        {"type": "position", "point": [1, 2], "space": 1},
        {"type": "button", "name": "unknown", "down": True},
        {"type": "key", "name": "Escape", "down": 1},
        {"type": "wait", "frames": 0},
        {"type": "wait", "frames": 1.5},
        {"type": "wait", "frames": 121},
        {"type": "action", "name": "move_selection", "parameters": []},
        {"type": "wait", "unknown": True},
        {"type": "unknown"},
    ],
)
def test_headless_input_validation_precedes_actions(canvas, invalid):
    editor, owner, pixels = canvas
    before = owner.snapshot()
    with pytest.raises(ValueError):
        editor.render_headless(
            events=[
                {
                    "type": "action",
                    "name": "move_selection",
                    "parameters": {"dx": 1, "dy": 1},
                },
                invalid,
            ]
        )
    assert owner.snapshot().selection.points == before.selection.points
    assert owner.snapshot().revisions.selection == before.revisions.selection
    assert_clean(owner, pixels)


def test_headless_unreleased_drag_is_discarded_with_context(canvas):
    editor, owner, pixels = canvas
    original = owner.snapshot()
    result = editor.render_headless(events=[pos(13, 11), button(True), pos(16, 13)])
    assert result["input_trace"][-1]["gesture_active"]
    following = editor.render_headless(
        events=[pos(16, 13), button(False), {"type": "wait"}]
    )
    assert not following["input_trace"][-1]["gesture_active"]
    assert owner.snapshot().selection.points == original.selection.points
    assert_clean(owner, pixels)


@pytest.mark.parametrize(
    "name,parameters",
    [
        ("resize_selection", {"index": 1.5, "x": 3, "y": 4}),
        ("resize_selection", {"index": -1, "x": 3, "y": 4}),
        ("resize_selection", {"x": 3, "y": 4}),
        ("resize_selection", {"index": 4, "x": "3", "y": 4}),
        ("resize_selection", {"index": 4, "x": 3, "y": 4, "preserve_aspect": 1}),
        ("move_selection", {"dx": 1, "dy": 1, "keep_inside": "yes"}),
    ],
)
def test_numeric_selection_actions_reject_invalid_parameters(canvas, name, parameters):
    editor, owner, pixels = canvas
    original = owner.snapshot()
    with pytest.raises(ValueError):
        editor.action(name, **parameters)
    assert owner.snapshot().selection.points == original.selection.points
    assert owner.snapshot().revisions.selection == original.revisions.selection
    assert_clean(owner, pixels)
