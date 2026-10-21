"""Native parametric ROI rotation and real shared-panel property input."""

import math

import numpy as np
import pytest

import imagejxx as ij


STYLE = {"label": "Tracked", "color": "#123456ff", "extension": {"tag": [1, "kept"]}}
SHAPE = (24, 32)


def selection(kind="rectangle"):
    return ij.Selection(kind, [(8, 7), (18, 15)], width=3, attributes=STYLE)


def axes(degrees, center=(13, 11), half=(5, 4)):
    radians = math.radians(degrees)
    c, s = math.cos(radians), math.sin(radians)
    return np.array(center), np.array([c, s]) * half[0], np.array([-s, c]) * half[1]


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
@pytest.mark.parametrize("degrees", [-90, 35, 90, 180, 450])
def test_parameterized_rotation_and_frame_coordinates(kind, degrees):
    source = selection(kind)
    result = ij.SelectionEditor(source, SHAPE).rotated(degrees)
    center, u, v = axes(degrees)
    assert result.kind == "rotated_" + kind
    np.testing.assert_allclose(
        result.points, [center, center + u, center + v], atol=1e-12
    )
    frame = result.box_frame
    np.testing.assert_allclose(frame.u, u, atol=1e-12)
    np.testing.assert_allclose(frame.v, v, atol=1e-12)
    np.testing.assert_allclose(
        frame.local_point(frame.image_point((0.25, -0.75))), (0.25, -0.75)
    )
    extent = np.hypot(u, v) if kind == "ellipse" else np.abs(u) + np.abs(v)
    np.testing.assert_allclose(
        frame.bounds(ellipse=kind == "ellipse"),
        [*(center - extent), *(center + extent)],
    )
    assert result.width == 3 and result.attributes == STYLE
    assert source.points == [[8, 7], [18, 15]]
    with pytest.raises(AttributeError):
        frame.u = (1, 0)


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
@pytest.mark.parametrize("degrees", [0, 360, -720])
def test_rotation_noop_preserves_legacy_kind_and_coordinates(kind, degrees):
    source = selection(kind)
    result = ij.SelectionEditor(source, SHAPE).rotated(degrees)
    assert result.kind == kind and result.points == source.points
    assert result.attributes == STYLE
    np.testing.assert_array_equal(result.mask(SHAPE), source.mask(SHAPE))


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
@pytest.mark.parametrize("degrees", [35, 90])
def test_rotated_raster_uses_declared_native_geometry(kind, degrees):
    result = ij.SelectionEditor(selection(kind), SHAPE).rotated(degrees)
    center, u, v = axes(degrees)
    if kind == "rectangle":
        corners = [center - u - v, center + u - v, center + u + v, center - u + v]
        expected = ij.polygon_mask(SHAPE, corners)
    else:
        y, x = np.indices(SHAPE, dtype=float)
        dx, dy = x + 0.5 - center[0], y + 0.5 - center[1]
        a = (dx * u[0] + dy * u[1]) / np.dot(u, u)
        b = (dx * v[0] + dy * v[1]) / np.dot(v, v)
        expected = ((a * a + b * b) <= 1).astype(np.uint8) * 255
    np.testing.assert_array_equal(result.mask(SHAPE), expected)


@pytest.mark.parametrize(
    "handle,local",
    enumerate([(-2, -2), (0, -2), (2, -2), (2, 0), (2, 2), (0, 2), (-2, 2), (-2, 0)]),
)
def test_all_rotated_resize_handles_follow_local_axes(handle, local):
    rotated = ij.SelectionEditor(selection(), SHAPE).rotated(35)
    frame = rotated.box_frame
    result = ij.SelectionEditor(rotated, SHAPE).resized(
        handle, frame.image_point(local)
    )
    xs, ys = np.sign(local)
    center = (
        np.array(frame.center) + xs * np.array(frame.u) / 2 + ys * np.array(frame.v) / 2
    )
    np.testing.assert_allclose(result.box_frame.center, center)
    np.testing.assert_allclose(
        result.box_frame.u, np.array(frame.u) * (1.5 if xs else 1)
    )
    np.testing.assert_allclose(
        result.box_frame.v, np.array(frame.v) * (1.5 if ys else 1)
    )
    assert result.kind == rotated.kind and result.attributes == STYLE


@pytest.mark.parametrize("centered", [False, True])
def test_rotated_resize_aspect_and_center_constraints(centered):
    rotated = ij.SelectionEditor(selection(), SHAPE).rotated(35)
    frame = rotated.box_frame
    result = ij.SelectionEditor(rotated, SHAPE).resized(
        4, frame.image_point((2, 1.25)), preserve_aspect=True, from_center=centered
    )
    factor = 2 if centered else 1.5
    np.testing.assert_allclose(result.box_frame.u, np.array(frame.u) * factor)
    np.testing.assert_allclose(result.box_frame.v, np.array(frame.v) * factor)
    expected_center = frame.center if centered else frame.image_point((0.5, 0.5))
    np.testing.assert_allclose(result.box_frame.center, expected_center)


def test_rotation_handle_uses_display_metric_and_native_hit_testing():
    rotated = ij.SelectionEditor(selection(), SHAPE).rotated(35)
    editor = ij.SelectionEditor(rotated, SHAPE)
    handle = editor.rotation_handle(24, (2, 7))
    frame = rotated.box_frame
    anchor = np.array(frame.center) - frame.v
    assert np.linalg.norm(
        (np.array(handle.position) - anchor) * [2, 7]
    ) == pytest.approx(24)
    assert (
        editor.hit_test(handle.position, (0.01, 0.01), handle).kind
        == ij.SelectionHandleKind.ROTATE
    )
    assert editor.rotation_degrees((13, 6), (18, 11)) == 90


def test_numeric_geometry_checks_final_rotated_extent():
    editor = ij.SelectionEditor(selection(), SHAPE)
    result = editor.box_geometry((-1, 6), (1, 12), 90)
    assert result.kind == "rotated_rectangle" and result.mask(SHAPE).any()
    expected_rotated = np.zeros(SHAPE, np.uint8)
    expected_rotated[5, :5] = 255
    np.testing.assert_array_equal(result.mask(SHAPE), expected_rotated)
    # P05e admits positive-size off-canvas geometry as an explicit zero mask.
    # Zero-angle specification retains the legacy kind and exact coordinates.
    unrotated = editor.box_geometry((-1, 6), (1, 12), 0)
    assert unrotated.kind == "rectangle"
    assert unrotated.points == [[-1.5, 0], [-0.5, 12]]
    np.testing.assert_array_equal(unrotated.mask(SHAPE), np.zeros(SHAPE, np.uint8))


@pytest.mark.parametrize("degrees,turns", [(90, -1), (-90, 1), (180, 2), (270, 1)])
def test_packed_mask_rotation_retains_holes_and_disjoint_regions(degrees, turns):
    mask = np.zeros((13, 13), np.uint8)
    mask[2:7, 3:8] = 255
    mask[3:6, 4:7] = 0
    mask[10, 1] = 255
    source = ij.Selection.from_mask(mask).with_attributes(STYLE)
    source = ij.SelectionEditor(source, mask.shape).properties(3)
    result = ij.SelectionEditor(source, mask.shape).rotated(degrees, pivot=(6.5, 6.5))
    np.testing.assert_array_equal(result.mask(mask.shape), np.rot90(mask, turns))
    assert result.attributes == STYLE and result.width == 3
    np.testing.assert_array_equal(source.mask(mask.shape), mask)


@pytest.mark.parametrize(
    "kind", ["polygon", "freehand", "polyline", "line", "angle", "point", "multipoint"]
)
def test_rotation_of_vertices_and_points_with_explicit_pivot(kind):
    points = np.array([(10, 8), (16, 8), (15, 14)], dtype=float)
    if kind == "line":
        points = points[:2]
    elif kind == "point":
        points = points[:1]
    source = ij.Selection(kind, points, width=3, attributes=STYLE)
    result = ij.SelectionEditor(source, SHAPE).rotated(90, pivot=(13, 11))
    expected = np.column_stack((24 - points[:, 1], points[:, 0] - 2))
    np.testing.assert_array_equal(result.points, expected)
    recovered = ij.SelectionEditor(result, SHAPE).rotated(-90, pivot=(13, 11))
    np.testing.assert_array_equal(recovered.points, points)
    assert result.kind == kind and result.attributes == STYLE


@pytest.mark.parametrize(
    "points",
    [
        [],
        [(13, 11), (18, 11)],
        [(13, 11), (13, 11), (13, 15)],
        [(13, 11), (18, 11), (15, 15)],
        [(13, 11), (18, float("nan")), (13, 15)],
    ],
)
def test_invalid_rotated_shape_is_rejected(points):
    with pytest.raises(ValueError):
        ij.Selection("rotated_rectangle", points).mask(SHAPE)


@pytest.mark.parametrize(
    "operation",
    [
        lambda e: e.rotated(float("nan")),
        lambda e: e.rotated(90, pivot=(float("inf"), 3)),
        lambda e: e.box_geometry((13, 11), (0, 4)),
        lambda e: e.box_geometry((13, 11), (-1, 4)),
        lambda e: e.box_geometry((13, 11), (5, 4), float("inf")),
        lambda e: e.rotation_handle(-1),
        lambda e: e.rotation_handle(5, (0, 1)),
        lambda e: e.rotation_degrees((13, 11), (18, 11)),
        lambda e: e.properties(0),
        lambda e: e.properties(2, {"kind": "ellipse"}),
    ],
)
def test_invalid_edit_is_atomic(operation):
    editor = ij.SelectionEditor(selection(), SHAPE)
    with pytest.raises(ValueError):
        operation(editor)
    assert editor.selection.points == [[8, 7], [18, 15]]
    assert editor.selection.attributes == STYLE and editor.selection.width == 3


def test_properties_merge_preserves_unknown_detached_metadata():
    editor = ij.SelectionEditor(selection(), SHAPE)
    result = editor.properties(4.25, {"label": "Changed", "shape": {"custom": True}})
    assert result.attributes == {**STYLE, "label": "Changed", "shape": {"custom": True}}
    attrs = result.attributes
    attrs["extension"]["tag"][0] = 99
    assert result.attributes["extension"] == STYLE["extension"]
    assert editor.selection.attributes == STYLE


def pos(point, space="image"):
    return {"type": "position", "point": list(point), "space": space}


def key(name, down):
    return {"type": "key", "name": name, "down": down}


def click(point):
    return [
        pos(point, "screen"),
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ]


def typing(point, value):
    return [
        *click(point),
        key("Ctrl", True),
        key("A", True),
        key("A", False),
        key("Ctrl", False),
        {"type": "text", "value": value},
        key("Enter", True),
        key("Enter", False),
    ]


@pytest.fixture
def desktop():
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor

    pixels = np.arange(np.prod(SHAPE), dtype=np.uint16).reshape(SHAPE)
    owner = ij.Document(
        ij.Dataset(
            pixels, axes="YX", calibration=ij.SpatialCalibration(spacing=(0.5, 1.5, 2))
        )
    )
    owner.select(selection())
    editor = Editor()
    editor.add_native_document(owner)
    editor.action("edit_selection")
    return editor, owner, pixels


def assert_pixels_unchanged(owner, pixels):
    assert not owner.snapshot().dirty and owner.undo_steps == 0
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), pixels)


def image_pointer(entry):
    return (np.array(entry["mouse_screen"]) - entry["canvas_origin"]) / entry[
        "canvas_scale"
    ]


@pytest.mark.parametrize("snap", [False, True])
def test_real_rotation_handle_stages_then_commits(desktop, snap):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    canvas = editor.render_headless()["ordinary_canvas"]
    native = ij.SelectionEditor(source.selection, SHAPE)
    handle = native.rotation_handle(24, canvas["scale"])
    events = [pos(handle.position)]
    if snap:
        events.append(key("Shift", True))
    events += [
        {"type": "button", "name": "left", "down": True},
        pos((21, 8 if snap else 11)),
        {"type": "wait", "frames": 2},
        {"type": "button", "name": "left", "down": False},
    ]
    if snap:
        events.append(key("Shift", False))
    rendered = editor.render_headless(events=events)
    trace = rendered["input_trace"]
    start, end = [entry for entry in trace if entry["type"] == "position"]
    angle = native.rotation_degrees(image_pointer(start), image_pointer(end))
    if snap:
        angle = math.floor(angle / 15 + 0.5) * 15
    expected = native.rotated(angle)
    assert (
        end["gesture_active"]
        and end["selection_revision"] == source.revisions.selection
    )
    np.testing.assert_allclose(end["preview"]["points"], expected.points, atol=1e-5)
    np.testing.assert_allclose(
        owner.snapshot().selection.points, expected.points, atol=1e-5
    )
    assert owner.snapshot().previous_selection.points == source.selection.points
    assert owner.snapshot().selection.attributes == STYLE
    assert_pixels_unchanged(owner, pixels)


@pytest.mark.parametrize(
    "field,value,property_name,expected",
    [
        ("Center X", "16.125", "center", [16.125, 11]),
        ("Center Y", "12.25", "center", [13, 12.25]),
        ("Width", "12.5", "size", [12.5, 8]),
        ("Height", "10.5", "size", [10, 10.5]),
        ("Angle (degrees)", "30", "angle", 30),
        ("Line width", "4.25", "width", 4.25),
        ("Name", "Nuclear α", "label", "Nuclear α"),
        ("Color", "#00ff80ff", "color", "#00ff80ff"),
    ],
)
def test_real_numeric_and_text_property_controls(
    desktop, field, value, property_name, expected
):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    editor.action("show_selection_properties")
    opened = editor.render_headless()["selection_properties"]
    draft = editor.render_headless(events=typing(opened["controls"][field], value))[
        "selection_properties"
    ]
    assert draft["open"] and not draft["error"] and draft[property_name] == expected
    assert owner.snapshot().selection.points == source.selection.points
    assert owner.snapshot().revisions.selection == source.revisions.selection
    preview = draft["preview"]
    closed = editor.render_headless(events=click(draft["apply_point"]))
    assert not closed["selection_properties"]["open"]
    assert owner.snapshot().selection.kind == preview["kind"]
    np.testing.assert_allclose(owner.snapshot().selection.points, preview["points"])
    assert owner.snapshot().selection.width == preview["width"]
    assert owner.snapshot().selection.attributes == {
        key: value
        for key, value in preview.items()
        if key not in {"kind", "points", "width"}
    }
    assert owner.snapshot().revisions.selection == source.revisions.selection + 1
    assert_pixels_unchanged(owner, pixels)


@pytest.mark.parametrize("mode", ["cancel", "escape", "stale", "other_document"])
def test_properties_discard_draft_without_replacing_source(desktop, mode):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    editor.action("show_selection_properties")
    editor.action(
        "selection_properties_draft",
        center=[16, 12],
        size=[12, 8],
        angle=90,
        label="Draft",
    )
    if mode == "cancel":
        editor.action("cancel_selection_properties")
        rendered = editor.render_headless()
    elif mode == "escape":
        rendered = editor.render_headless(
            events=[key("Escape", True), key("Escape", False)]
        )
    elif mode == "stale":
        owner.select(selection("ellipse"))
        rendered = editor.render_headless()
    else:
        editor.add_native_document(
            ij.Document(ij.Dataset(np.zeros(SHAPE, np.uint8), axes="YX"))
        )
        rendered = editor.render_headless()
    assert not rendered["selection_properties"]["open"]
    assert owner.snapshot().selection.kind == (
        "ellipse" if mode == "stale" else "rectangle"
    )
    assert owner.snapshot().selection.points == source.selection.points
    assert owner.snapshot().selection.attributes == STYLE
    assert_pixels_unchanged(owner, pixels)


@pytest.mark.parametrize(
    "patch",
    [
        {"width": 0},
        {"size": [0, 8]},
        {"color": "invalid"},
        {"angle": "30"},
        {"center": [True, 3]},
        {"missing": 3},
    ],
)
def test_invalid_properties_action_keeps_previous_draft(desktop, patch):
    editor, owner, pixels = desktop
    editor.action("show_selection_properties")
    editor.action("selection_properties_draft", angle=35, label="Valid")
    before = editor.render_headless()["selection_properties"]["preview"]
    with pytest.raises(ValueError):
        editor.action("selection_properties_draft", **patch)
    after = editor.render_headless()["selection_properties"]["preview"]
    assert after == before and owner.snapshot().selection.kind == "rectangle"
    assert_pixels_unchanged(owner, pixels)


def test_invalid_typed_size_disables_apply_and_can_be_corrected(desktop):
    editor, owner, pixels = desktop
    editor.action("show_selection_properties")
    panel = editor.render_headless()["selection_properties"]
    invalid = editor.render_headless(events=typing(panel["controls"]["Width"], "0"))[
        "selection_properties"
    ]
    assert invalid["error"] and not invalid["preview"]
    rendered = editor.render_headless(events=click(invalid["apply_point"]))
    assert rendered["selection_properties"]["open"]
    assert owner.snapshot().selection.points == [[8, 7], [18, 15]]
    valid = editor.render_headless(
        events=typing(rendered["selection_properties"]["controls"]["Width"], "12.5")
    )["selection_properties"]
    assert not valid["error"] and valid["preview"]
    editor.action("apply_selection_properties")
    assert owner.snapshot().selection.box_frame.u == [6.25, 0]
    assert_pixels_unchanged(owner, pixels)


def test_native_rotation_session_and_python_model_roundtrip(desktop, tmp_path):
    from imagejxx.ui.model import Selection as ModelSelection

    editor, owner, pixels = desktop
    editor.action("rotate_selection", degrees=35)
    source = owner.snapshot().selection
    metadata = editor.document_snapshot().selection.to_dict()
    model = ModelSelection.from_dict(metadata)
    assert model.to_dict() == metadata and model.attributes == STYLE
    np.testing.assert_array_equal(model.mask(SHAPE), source.mask(SHAPE))
    exported = model.to_dict()
    exported["extension"]["tag"][0] = 99
    assert model.attributes == STYLE
    path = tmp_path / "rotated.ijx"
    editor.action("save_workspace", path=str(path))
    editor.open_file(path)
    restored = editor.live_document().native_snapshot().selection
    assert restored.kind == source.kind and restored.points == source.points
    assert restored.attributes == STYLE and restored.width == 3
    assert_pixels_unchanged(owner, pixels)


def test_properties_apply_respects_active_job_and_cancel_is_available(desktop):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    editor.action("show_selection_properties")
    editor.action("selection_properties_draft", angle=35, label="Draft")
    editor.apply_operation("add", {"value": 5})
    assert editor.busy
    with pytest.raises(ValueError, match="Wait for processing"):
        editor.action("apply_selection_properties")
    assert owner.snapshot().selection.points == source.selection.points
    assert owner.snapshot().revisions.selection == source.revisions.selection
    editor.action("cancel_selection_properties")
    editor.wait()
    assert not editor.error and not editor.busy
    assert not editor.render_headless()["selection_properties"]["open"]
    assert owner.snapshot().selection.kind == "rectangle"
    assert owner.snapshot().selection.attributes == STYLE
    expected = pixels.copy()
    expected[7:15, 8:18] += 5
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), expected)
    assert owner.undo_steps == 1


def test_property_panel_vertex_coordinates_use_native_geometry(desktop):
    editor, owner, pixels = desktop
    source = ij.Selection(
        "polygon", [(8, 7), (18, 7), (18, 15), (8, 15)], width=3, attributes=STYLE
    )
    owner.select(source)
    editor.action("show_selection_properties")
    panel = editor.render_headless()["selection_properties"]
    draft = editor.render_headless(
        events=typing(panel["controls"]["Vertex X"], "9.25")
    )["selection_properties"]
    assert not draft["error"] and draft["preview"]["points"][0] == [9.25, 7]
    assert owner.snapshot().selection.points == source.points
    editor.render_headless(events=click(draft["apply_point"]))
    assert owner.snapshot().selection.points[0] == [9.25, 7]
    assert owner.snapshot().selection.attributes == STYLE
    assert_pixels_unchanged(owner, pixels)


@pytest.mark.parametrize("mask", [False, True])
def test_nonbox_property_translation_rotation_and_style(desktop, mask):
    editor, owner, pixels = desktop
    if mask:
        bitmap = np.zeros(SHAPE, np.uint8)
        bitmap[7:15, 8:18] = 255
        bitmap[9:11, 11:13] = 0
        bitmap[18, 25] = 255
        source = ij.Selection.from_mask(bitmap).with_attributes(STYLE)
    else:
        source = ij.Selection(
            "polygon", [(8, 7), (18, 7), (18, 15), (8, 15)], attributes=STYLE
        )
    owner.select(source)
    editor.action("show_selection_properties")
    editor.action(
        "selection_properties_draft",
        translation=[1.5, -0.5],
        rotation=45,
        width=5,
        label="Adjusted",
    )
    draft = editor.render_headless()["selection_properties"]
    assert not draft["box"] and not draft["error"]
    expected = ij.SelectionEditor(source, SHAPE).moved((1.5, -0.5))
    expected = ij.SelectionEditor(expected, SHAPE).rotated(45)
    assert owner.snapshot().selection.points == source.points
    editor.render_headless(events=click(draft["apply_point"]))
    actual = owner.snapshot().selection
    np.testing.assert_array_equal(actual.mask(SHAPE), expected.mask(SHAPE))
    np.testing.assert_allclose(actual.points, expected.points)
    assert actual.width == 5 and actual.attributes == {**STYLE, "label": "Adjusted"}
    assert_pixels_unchanged(owner, pixels)


def test_display_changes_do_not_stale_or_get_overwritten_by_properties(desktop):
    editor, owner, pixels = desktop
    editor.action("show_selection_properties")
    editor.action("selection_properties_draft", angle=35)
    display = owner.snapshot().display
    display.properties = {"lut": "Ice", "display_range": [2, 100]}
    editor.live_document().set_display(display)
    assert editor.render_headless()["selection_properties"]["open"]
    editor.action("apply_selection_properties")
    assert owner.snapshot().selection.kind == "rotated_rectangle"
    assert owner.snapshot().display.properties == display.properties
    assert_pixels_unchanged(owner, pixels)


@pytest.mark.parametrize("kind", ["rectangle", "ellipse"])
def test_python_model_attachment_roi_json_and_session_keep_rotation_style(
    tmp_path, kind
):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document, Selection as ModelSelection
    from imagejxx.ui.persistence import load_rois, load_session, save_rois, save_session

    source = ij.SelectionEditor(selection(kind), SHAPE).rotated(35)
    record = {
        **source.attributes,
        "kind": source.kind,
        "points": [tuple(point) for point in source.points],
        "width": source.width,
        "shape": {"custom": True},
    }
    model = ModelSelection.from_dict(record)
    assert model.to_dict() == record
    document = Document(np.zeros(SHAPE, np.uint16))
    document.select(model)
    document.add_roi("Stored")
    roi_path = tmp_path / "rotated-roi.json"
    save_rois(roi_path, document.rois)
    restored = load_rois(roi_path, SHAPE)[0].selection
    assert restored.to_dict() == record
    session_path = tmp_path / "rotated-python.ijx"
    save_session(session_path, [document])
    restored_document = load_session(session_path)[0][0]
    assert restored_document.selection.to_dict() == record
    editor = Editor()
    editor.add_document(restored_document)
    native = editor.live_document().native_snapshot()
    assert (
        native.selection.kind == source.kind
        and native.selection.points == source.points
    )
    assert native.selection.attributes == {**STYLE, "shape": {"custom": True}}
    assert native.rois[0].selection.attributes == native.selection.attributes
    np.testing.assert_array_equal(native.selection.mask(SHAPE), source.mask(SHAPE))


@pytest.mark.parametrize("custom_mask", [True, {"ids": [1, 2]}, "custom"])
def test_unambiguous_mask_named_metadata_survives_python_attachment(
    desktop, custom_mask
):
    from imagejxx.ui.model import Selection as ModelSelection

    editor, owner, pixels = desktop
    attributes = {**STYLE, "mask": custom_mask, "shape": {"custom": True}}
    owner.select(selection().with_attributes(attributes))
    model = editor.document_snapshot().selection
    assert model.bitmap is None and model.attributes == attributes
    assert ModelSelection.from_dict(model.to_dict()).attributes == attributes
    editor.action("show_selection_properties")
    editor.action("selection_properties_draft", label="Updated")
    editor.action("apply_selection_properties")
    assert owner.snapshot().selection.attributes == {**attributes, "label": "Updated"}
    assert_pixels_unchanged(owner, pixels)


def test_legacy_python_bitmap_on_another_kind_still_decodes():
    from imagejxx.ui.model import Selection as ModelSelection

    bitmap = np.zeros(SHAPE, np.uint8)
    bitmap[8:12, 9:14] = 255
    source = ModelSelection("rectangle", [(8, 7), (18, 15)], 3, bitmap, STYLE)
    result = ModelSelection.from_dict(source.to_dict())
    assert result.kind == source.kind and result.attributes == STYLE
    np.testing.assert_array_equal(result.bitmap, bitmap)


@pytest.mark.parametrize(
    "invalid",
    [
        {"type": "text", "value": "a\x00b"},
        {"type": "text", "value": "a" * 65537},
        {"type": "text", "value": 3},
        pos((1e308, 1)),
    ],
)
def test_input_validation_precedes_any_action(desktop, invalid):
    editor, owner, pixels = desktop
    revision = owner.snapshot().revisions.selection
    with pytest.raises(ValueError):
        editor.render_headless(
            events=[{"type": "action", "name": "select_all"}, invalid]
        )
    assert owner.snapshot().revisions.selection == revision
    assert_pixels_unchanged(owner, pixels)
