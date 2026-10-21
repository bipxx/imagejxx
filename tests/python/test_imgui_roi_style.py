"""Real input in the shared ROI-style panel through the Python ImGui launch."""

from copy import deepcopy

import imagejxx as ij
import numpy as np
import pytest


STYLE_KEY = "imagejxx.roi_style"
SHAPE = (24, 32)
LEGACY = {"color": "#12345678", "extension": {"flags": [None, True, 2**64 - 1]}}


def key(name, down):
    return {"type": "key", "name": name, "down": down}


def click(point):
    return [
        {"type": "position", "point": list(point), "space": "screen"},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ]


def typing(point, value):
    return [*click(point), key("Ctrl", True), key("A", True), key("A", False),
            key("Ctrl", False), {"type": "text", "value": value},
            key("Enter", True), key("Enter", False)]


def selection(kind="rectangle"):
    return ij.Selection(kind, [(8, 7), (18, 15)], 3, attributes=LEGACY)


@pytest.fixture
def desktop(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    pixels = np.arange(np.prod(SHAPE), dtype=np.uint16).reshape(SHAPE)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    owner.select(selection())
    editor = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    editor.add_native_document(owner)
    return editor, owner, pixels


def opened(editor):
    editor.action("show_selection_properties")
    return editor.render_headless()["selection_properties"]


def typed_style(editor, panel, *, stroke="#80403020", fill="#80123456", width="0"):
    events = typing(panel["controls"]["Stroke color"], stroke)
    events += typing(panel["controls"]["Fill color"], fill)
    events += typing(panel["controls"]["Stroke width"], width)
    return editor.render_headless(events=events)["selection_properties"]


def assert_clean(owner, pixels, source):
    snapshot = owner.snapshot()
    assert not snapshot.dirty and owner.undo_steps == 0
    assert snapshot.revisions.pixels == source.revisions.pixels
    np.testing.assert_array_equal(snapshot.dataset.numpy("YX"), pixels)


def assert_stable_view_cache(rendered, orthogonal):
    """Compare consecutive actual frames inside one headless ImGui context."""
    trace = rendered["cache_trace"]
    assert len(trace) >= 2
    previous, current = trace[-2:]
    observed = {"orthogonal": orthogonal, "previous_frame": previous,
                "final_frame": current}
    assert current["frame"] == previous["frame"] + 1, observed
    for field in ["data_revision", "slice_index", "mask_outline_builds"]:
        assert previous[field] == current[field], observed
    before = previous["orthogonal_planes"][0] if orthogonal else previous["ordinary_canvas"]
    after = current["orthogonal_planes"][0] if orthogonal else current["ordinary_canvas"]
    for field in ["origin", "scale", "extent", "style_layers", "overlay_layers"]:
        # Complete layer summaries include actual device keys, bounds and counts.
        assert before[field] == after[field], observed
    build_field = "builds" if orthogonal else "overlay_builds"
    assert before[build_field] == after[build_field], observed


@pytest.mark.parametrize("stroke,fill,width", [
    ("#80403020", "#80123456", "0"),
    ("green", "none", "2.5"),
    ("none", "#80123456", "0"),
    ("#00123456", "none", "0"),
    ("none", "none", "0"),
])
def test_actual_style_inputs_stage_and_apply_once_preserving_view(desktop, stroke, fill, width):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    panel = opened(editor)
    assert panel["area"] and panel["stroke_color"] == "#78123456"
    assert panel["fill_color"] == "none" and panel["stroke_width"] == 3
    draft = typed_style(editor, panel, stroke=stroke, fill=fill, width=width)
    assert draft["open"] and not draft["error"]
    assert draft["stroke_color"] == stroke and draft["fill_color"] == fill
    assert draft["stroke_width"] == float(width)
    assert owner.snapshot().selection.attributes == source.selection.attributes
    assert owner.snapshot().revisions.selection == source.revisions.selection
    # Identical input text is a no-op: an absent fill field must stay absent.
    # A changed fill field also reapplies the stroke displayed by the dialog.
    expected_patch = {}
    if fill != panel["fill_color"]:
        expected_patch.update(fill_color=fill, stroke_color=stroke)
    if stroke != panel["stroke_color"]:
        expected_patch["stroke_color"] = stroke
    if float(width) != panel["stroke_width"]:
        expected_patch["stroke_width"] = float(width)
    expected = ij.with_roi_style(source.selection, expected_patch)
    assert draft["preview"][STYLE_KEY] == expected.attributes[STYLE_KEY]
    display = owner.snapshot().display
    display.zoom, display.pan = 2, [[4, 5], [0, 0], [0, 0]]
    display.properties = {"lut": "Ice", "display_range": [3, 100]}
    editor.live_document().set_display(display)
    closed = editor.render_headless(events=click(draft["apply_point"]))
    assert not closed["selection_properties"]["open"]
    final = owner.snapshot()
    assert final.selection.attributes == expected.attributes
    assert final.selection.points == source.selection.points and final.selection.width == 3
    assert final.previous_selection.attributes == source.selection.attributes
    assert final.revisions.selection == source.revisions.selection + 1
    assert final.display.zoom == 2 and final.display.pan[0] == [4, 5]
    assert final.display.properties == display.properties
    assert_clean(owner, pixels, source)


def test_actual_fill_clear_retains_explicit_null_in_the_record(desktop):
    editor, owner, pixels = desktop
    styled = ij.with_roi_style(owner.snapshot().selection, {
        "fill_color": "#80123456", "stroke_color": "green", "stroke_width": 0})
    owner.select(styled)
    source = owner.snapshot()
    panel = opened(editor)
    assert panel["fill_color"] == "#80123456"
    draft = editor.render_headless(
        events=typing(panel["controls"]["Fill color"], "none"))["selection_properties"]
    expected = ij.with_roi_style(styled, {
        "fill_color": "none", "stroke_color": panel["stroke_color"]})
    assert draft["preview"][STYLE_KEY] == expected.attributes[STYLE_KEY]
    assert "fill_color" in draft["preview"][STYLE_KEY]
    assert draft["preview"][STYLE_KEY]["fill_color"] is None
    assert owner.snapshot().selection.attributes == source.selection.attributes
    editor.render_headless(events=click(draft["apply_point"]))
    selected = owner.snapshot()
    assert selected.selection.attributes == expected.attributes
    assert selected.selection.attributes[STYLE_KEY]["fill_color"] is None
    assert selected.revisions.selection == source.revisions.selection + 1
    assert selected.previous_selection.attributes == source.selection.attributes
    assert_clean(owner, pixels, source)


@pytest.mark.parametrize("mode", ["cancel", "escape", "stale", "closed", "other_document"])
def test_typed_style_draft_lifecycle_discards_without_partial_publication(desktop, mode):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    draft = typed_style(editor, opened(editor))
    assert draft["preview"][STYLE_KEY]["stroke_width"] == 0
    if mode == "cancel":
        rendered = editor.render_headless(events=click(draft["cancel_point"]))
    elif mode == "escape":
        rendered = editor.render_headless(events=[key("Escape", True), key("Escape", False)])
    elif mode == "stale":
        owner.select(selection("ellipse"))
        rendered = editor.render_headless(events=click(draft["apply_point"]))
    elif mode == "closed":
        editor.live_document().close(discard=True)
        rendered = editor.render_headless()
    else:
        editor.add_native_document(ij.Document(ij.Dataset(np.zeros(SHAPE, np.uint8), axes="YX")))
        rendered = editor.render_headless(events=click(draft["apply_point"]))
    assert not rendered["selection_properties"]["open"]
    assert owner.snapshot().selection.kind == ("ellipse" if mode == "stale" else "rectangle")
    assert owner.snapshot().selection.attributes == LEGACY
    assert owner.snapshot().revisions.selection == source.revisions.selection + (mode == "stale")
    assert_clean(owner, pixels, source)


@pytest.mark.parametrize("already_styled", [False, True])
def test_actual_apply_without_changes_is_a_noop(desktop, already_styled):
    editor, owner, pixels = desktop
    if already_styled:
        owner.select(ij.with_roi_style(selection(), {"stroke_color": "green",
            "fill_color": "#80123456", "stroke_width": 0}))
    source = owner.snapshot()
    panel = opened(editor)
    rendered = editor.render_headless(events=click(panel["apply_point"]))
    assert not rendered["selection_properties"]["open"]
    final = owner.snapshot()
    assert final.selection.attributes == source.selection.attributes
    if source.previous_selection is None:
        assert final.previous_selection is None
    else:
        assert final.previous_selection.attributes == source.previous_selection.attributes
    assert final.revisions.selection == source.revisions.selection
    assert_clean(owner, pixels, source)


def test_reopened_style_disables_superseded_legacy_fields(desktop):
    editor, owner, pixels = desktop
    styled = ij.with_roi_style(selection(), {"stroke_color": "green",
        "fill_color": "#80123456", "stroke_width": 0})
    owner.select(styled)
    source = owner.snapshot()
    panel = opened(editor)
    assert not panel["legacy_color_enabled"] and not panel["legacy_width_enabled"]
    assert panel["color"] == LEGACY["color"] and panel["width"] == 3
    assert panel["stroke_color"] == "#ff00ff00" and panel["stroke_width"] == 0
    events = typing(panel["controls"]["Color"], "#ff0000ff")
    events += typing(panel["controls"]["Line width"], "9")
    rejected = editor.render_headless(events=events)["selection_properties"]
    after_typing = owner.snapshot()
    observed = {"before": panel, "after": rejected,
                "before_selection_revision": source.revisions.selection,
                "after_selection_revision": after_typing.revisions.selection}
    assert after_typing.revisions.selection == source.revisions.selection, observed
    assert after_typing.selection.kind == source.selection.kind, observed
    assert after_typing.selection.points == source.selection.points, observed
    assert after_typing.selection.attributes == source.selection.attributes, observed
    assert rejected["open"] and not rejected["error"], observed
    assert rejected["preview"] == panel["preview"], observed
    assert rejected["color"] == LEGACY["color"] and rejected["width"] == 3, observed
    closed = editor.render_headless(events=click(rejected["apply_point"]))
    assert not closed["selection_properties"]["open"]
    assert owner.snapshot().selection.attributes == source.selection.attributes
    assert owner.snapshot().revisions.selection == source.revisions.selection
    assert_clean(owner, pixels, source)


def test_legacy_edits_keep_unstyled_displayed_stroke_fields_truthful(desktop):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    panel = opened(editor)
    assert panel["legacy_color_enabled"] and panel["legacy_width_enabled"]
    events = typing(panel["controls"]["Color"], "#abcdef80")
    events += typing(panel["controls"]["Line width"], "4.5")
    draft = editor.render_headless(events=events)["selection_properties"]
    assert draft["stroke_color"] == "#80abcdef" and draft["stroke_width"] == 4.5
    assert STYLE_KEY not in draft["preview"]
    editor.render_headless(events=click(draft["apply_point"]))
    selected = owner.snapshot().selection
    assert selected.width == 4.5 and selected.attributes["color"] == "#abcdef80"
    assert STYLE_KEY not in selected.attributes
    assert_clean(owner, pixels, source)


@pytest.mark.parametrize("field,value", [
    ("Stroke color", "#invalid"), ("Fill color", "invalid"),
    ("Stroke width", "-1"),
])
def test_invalid_typed_styles_disable_apply_and_allow_correction(desktop, field, value):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    panel = opened(editor)
    invalid = editor.render_headless(events=typing(panel["controls"][field], value))["selection_properties"]
    assert invalid["error"] and not invalid["preview"]
    rejected = editor.render_headless(events=click(invalid["apply_point"]))["selection_properties"]
    assert rejected["open"] and owner.snapshot().selection.attributes == LEGACY
    valid = "0" if field == "Stroke width" else "green"
    corrected = editor.render_headless(events=typing(rejected["controls"][field], valid))["selection_properties"]
    assert not corrected["error"] and corrected["preview"]
    editor.render_headless(events=click(corrected["apply_point"]))
    style = ij.roi_style(owner.snapshot().selection)
    if field == "Stroke width":
        assert style.stroke_width == 0 and owner.snapshot().selection.width == 3
    elif field == "Stroke color":
        assert style.stroke_color == [0, 255, 0, 255]
    else:
        assert style.fill_color == [0, 255, 0, 255]
        # The properties dialog reapplies its displayed stroke after area fill.
        assert style.stroke_color == [18, 52, 86, 120]
    assert_clean(owner, pixels, source)


@pytest.mark.parametrize("kind", ["line", "polyline", "angle", "point", "multipoint"])
def test_fill_control_is_disabled_for_lines_and_points(desktop, kind):
    editor, owner, pixels = desktop
    points = [(8, 10)] if kind == "point" else [(8, 10), (18, 10)]
    owner.select(ij.Selection(kind, points, 3, attributes=LEGACY))
    source = owner.snapshot()
    panel = opened(editor)
    attempted = editor.render_headless(events=typing(panel["controls"]["Fill color"], "red"))["selection_properties"]
    assert not attempted["area"] and attempted["fill_color"] == "none"
    assert attempted["preview"] == panel["preview"] and not attempted["error"]
    draft = editor.render_headless(events=typing(attempted["controls"]["Stroke width"], "0"))["selection_properties"]
    editor.render_headless(events=click(draft["apply_point"]))
    selected = owner.snapshot().selection
    assert selected.width == 3
    if kind in ("point", "multipoint"):
        assert not panel["stroke_width_enabled"]
        assert not ij.roi_style(selected).present
        np.testing.assert_array_equal(selected.mask(SHAPE), source.selection.mask(SHAPE))
    else:
        assert panel["stroke_width_enabled"] and ij.roi_style(selected).stroke_width == 0
        np.testing.assert_array_equal(selected.mask(SHAPE), ij.Selection(kind, points, 1).mask(SHAPE))
    assert_clean(owner, pixels, source)


@pytest.mark.parametrize("kind", ["point", "multipoint"])
def test_point_marker_width_remains_a_separate_legacy_control(desktop, kind):
    editor, owner, pixels = desktop
    points = [(8, 10)] if kind == "point" else [(8, 10), (18, 10)]
    styled = ij.with_roi_style(ij.Selection(kind, points, 3), {
        "stroke_color": "red", "stroke_width": 1e300})
    owner.select(styled)
    source = owner.snapshot()
    panel = opened(editor)
    assert not panel["stroke_width_enabled"] and panel["legacy_width_enabled"]
    events = typing(panel["controls"]["Stroke width"], "0")
    events += typing(panel["controls"]["Line width"], "5")
    draft = editor.render_headless(events=events)["selection_properties"]
    assert draft["width"] == 5 and draft["preview"][STYLE_KEY] == styled.attributes[STYLE_KEY]
    editor.render_headless(events=click(draft["apply_point"]))
    selected = owner.snapshot().selection
    assert selected.width == 5 and ij.roi_style(selected).stroke_width == 1e300
    np.testing.assert_array_equal(selected.mask(SHAPE), ij.Selection(kind, points, 5).mask(SHAPE))
    assert_clean(owner, pixels, source)


def test_apply_rejects_busy_job_and_cancel_preserves_style(desktop):
    editor, owner, pixels = desktop
    source = owner.snapshot()
    draft = typed_style(editor, opened(editor))
    editor.apply_operation("add", {"value": 5}, asynchronous=True)
    assert editor.busy
    with pytest.raises(ValueError, match="Wait for processing"):
        editor.action("apply_selection_properties")
    assert owner.snapshot().selection.attributes == LEGACY
    # Cancel uses the real button; a completed job may independently stale the
    # draft before the click, which must still leave its unpublished style absent.
    editor.render_headless(events=click(draft["cancel_point"]))
    editor.wait()
    assert not editor.render_headless()["selection_properties"]["open"]
    assert owner.snapshot().selection.attributes == LEGACY
    expected = pixels.copy()
    expected[source.selection.mask(SHAPE) != 0] += 5
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), expected)


def test_ui_created_style_survives_metadata_undo_session_and_frozen_replay(desktop, tmp_path):
    from imagejxx.ui import workflows
    from imagejxx.ui.model import Document

    editor, owner, pixels = desktop
    owner.select(ij.Selection("line", [(8, 10), (18, 10)], 3, attributes=LEGACY))
    source = owner.snapshot()
    panel = opened(editor)
    events = typing(panel["controls"]["Stroke color"], "#80123456")
    events += typing(panel["controls"]["Stroke width"], "5")
    draft = editor.render_headless(events=events)["selection_properties"]
    editor.render_headless(events=click(draft["apply_point"]))
    styled = owner.snapshot().selection
    editor.action("add_roi", name="Styled")
    saved = owner.snapshot()
    assert saved.rois[0].selection.attributes == styled.attributes
    edit = editor.live_document().begin_edit()
    changed = ij.with_roi_style(styled, {"stroke_color": "green"})
    edit.set_rois([ij.ROI("Styled", changed)])
    editor.live_document().commit(edit)
    assert owner.undo_steps == 2
    editor.action("undo")
    assert owner.snapshot().rois[0].selection.attributes == styled.attributes
    editor.action("redo")
    assert ij.roi_style(owner.snapshot().rois[0].selection).stroke_color == [0, 255, 0, 255]
    session = tmp_path / "typed-style.ijx"
    editor.action("save_workspace", path=str(session))
    editor.open_file(session)
    restored = editor.live_document().native_snapshot()
    assert restored.selection.attributes == styled.attributes
    assert restored.previous_selection.attributes == source.selection.attributes
    assert restored.rois[0].selection.attributes == changed.attributes
    editor.recording = True
    editor.apply_operation("add", {"value": 7}, asynchronous=False)
    frozen = deepcopy(editor.workflow)
    assert frozen[0]["selection"][STYLE_KEY] == styled.attributes[STYLE_KEY]
    expected = pixels.copy()
    expected[styled.mask(SHAPE) != 0] += 7
    np.testing.assert_array_equal(editor.document.data, expected)
    assert workflows.loads(workflows.dumps(frozen)) == frozen
    np.testing.assert_array_equal(workflows.run(Document(pixels), frozen), expected)
    editor.action("undo")
    editor.live_document().native_owner.select(ij.Selection("point", [(0, 0)]))
    editor.action("run_workflow")
    editor.wait()
    np.testing.assert_array_equal(editor.document.data, expected)


def test_individual_stored_roi_fill_alpha_and_holes_are_consumed_by_flatten(desktop):
    from imagejxx.ui.model import Selection as ModelSelection

    editor, owner, _ = desktop
    pixels = np.full((*SHAPE, 3), [100, 30, 210], np.uint8)
    rgb_owner = ij.Document(ij.Dataset(pixels, axes="YXS"))
    bitmap = np.zeros(SHAPE, np.uint8)
    bitmap[2:7, 2:7] = 255
    bitmap[4, 4] = 0
    bitmap[10, 10] = 255
    first = ij.with_roi_style(ij.Selection.from_mask(bitmap), {"fill_color": "#800ac81e"})
    second = ij.with_roi_style(ij.Selection("rectangle", [(20, 15), (25, 20)]), {"fill_color": "#4028dc50"})
    tx = rgb_owner.begin_edit()
    tx.set_rois([ij.ROI("Holed", first), ij.ROI("Separate", second)])
    tx.set_overlays([
        ModelSelection("mask", bitmap=bitmap, attributes=first.attributes).to_dict(),
        ModelSelection(second.kind, second.points, second.width,
                       attributes=second.attributes).to_dict(),
    ])
    rgb_owner.commit(tx)
    editor.add_native_document(rgb_owner)
    editor.action("flatten")
    expected = ij.render_roi_style(ij.render_roi_style(pixels, first), second)
    np.testing.assert_array_equal(editor.document.data, expected)
    np.testing.assert_array_equal(editor.document.data[4, 4], pixels[4, 4])
    np.testing.assert_array_equal(editor.document.data[10, 10], [55, 115, 120])
    np.testing.assert_array_equal(rgb_owner.snapshot().dataset.numpy("YXS"), pixels)
    assert rgb_owner.snapshot().rois[0].selection.attributes == first.attributes


@pytest.mark.parametrize("orthogonal", [False, True])
def test_individual_roi_viewing_styles_and_device_contours_are_cached(desktop, orthogonal):
    editor, _, _ = desktop
    owner = ij.Document(ij.Dataset(np.zeros((2, *SHAPE), np.uint16), axes="ZYX"))
    bitmap = np.zeros(SHAPE, np.uint8)
    bitmap[2:7, 2:7] = 255
    bitmap[4, 4] = 0
    bitmap[10, 10] = 255
    first = ij.with_roi_style(ij.Selection.from_mask(bitmap), {
        "stroke_color": "#80ff0000", "stroke_width": 0})
    second = ij.with_roi_style(ij.Selection("rectangle", [(20, 15), (25, 20)]), {
        "fill_color": "#400000ff", "stroke_color": "green", "stroke_width": 2.5})
    active = ij.with_roi_style(ij.Selection("line", [(8, 10), (18, 10)], 3), {
        "stroke_color": "#c8123456", "stroke_width": 0})
    tx = owner.begin_edit()
    tx.set_rois([ij.ROI("Holed", first), ij.ROI("Separate", second, z=0)])
    tx.select(active)
    owner.commit(tx)
    editor.add_native_document(owner)
    editor.update_settings(show_rois=True, persist=False)
    editor.live_document().set_view(orthogonal=orthogonal, crosshair=[1, 2, 0], zoom=2)

    def render():
        frame = editor.render_headless(frames=4)
        assert editor.live_document().id == owner.id, {
            "expected_document": owner.id, "active_document": editor.live_document().id,
            "active_index": editor.active_index, "cache_trace": frame["cache_trace"]}
        assert_stable_view_cache(frame, orthogonal)
        return frame

    def layers(frame):
        return frame["orthogonal_planes"][0]["style_layers"] if orthogonal else frame["ordinary_canvas"]["style_layers"]

    rendered = render()
    drawn = layers(rendered)
    assert len(drawn) == 3
    assert [item["stroke_color"] for item in drawn] == [[255, 0, 0, 128], [0, 255, 0, 255], [18, 52, 86, 200]]
    assert [item["fill_color"] for item in drawn] == [None, [0, 0, 255, 64], None]
    assert [item["stroke_width"] for item in drawn] == [0, 2.5, 0]
    assert [item["cosmetic"] for item in drawn] == [True, False, True]
    assert drawn[0]["area"] and drawn[0]["contour_edges"] > 0
    assert drawn[0]["device_spans"] > 0 and drawn[0]["device_builds"] > 0
    assert drawn[1]["fill_spans"] > 0 and not drawn[2]["area"]
    repeated_frame = render()
    repeated = layers(repeated_frame)
    assert repeated_frame["mask_outline_builds"] == rendered["mask_outline_builds"]
    for field in ["stroke_color", "fill_color", "stroke_width", "cosmetic",
                  "area", "fill_spans", "contour_edges", "role"]:
        assert [item[field] for item in repeated] == [item[field] for item in drawn]
    if orthogonal:
        assert not rendered["orthogonal_planes"][1]["style_layers"]
        assert not rendered["orthogonal_planes"][2]["style_layers"]
    edit = owner.begin_edit()
    changed = ij.with_roi_style(second, {"stroke_color": "magenta"})
    edit.set_rois([ij.ROI("Holed", first), ij.ROI("Separate", changed, z=0)])
    owner.commit(edit)
    refreshed = layers(render())
    assert refreshed[0]["stroke_color"] == drawn[0]["stroke_color"]
    assert refreshed[1]["stroke_color"] == [255, 0, 255, 255]
    assert refreshed[2]["stroke_color"] == drawn[2]["stroke_color"]


@pytest.mark.parametrize("orthogonal", [False, True])
def test_ordered_overlays_keep_device_widths_and_cache_across_zoom_and_slices(desktop, orthogonal):
    editor, _, _ = desktop
    owner = ij.Document(ij.Dataset(np.zeros((2, *SHAPE), np.uint16), axes="ZYX"))
    cosmetic = ij.with_roi_style(ij.Selection("line", [(8, 8), (18, 8)], 3), {
        "stroke_color": "#80ff0000", "stroke_width": 0})
    positive = ij.with_roi_style(ij.Selection("line", [(8, 14), (18, 14)], 3), {
        "stroke_color": "#80ff00ff", "stroke_width": 5})
    fill = ij.with_roi_style(ij.Selection("rectangle", [(5, 4), (22, 20)]), {
        "fill_color": "#4000ffff", "stroke_width": 0})
    positioned = ij.with_roi_style(ij.Selection("line", [(8, 18), (18, 18)]), {
        "stroke_color": "green", "stroke_width": 0})

    def record(value):
        return {"kind": value.kind, "points": [list(point) for point in value.points],
                "width": value.width, **value.attributes}

    overlays = [
        record(cosmetic),
        {"kind": "rectangle", "points": [[6, 8], [20, 18]], "width": 1, "color": "#0000ff"},
        {"kind": "text", "points": [[10, 9]], "text": "M", "font_size": 8, "color": "#00ff00"},
        {"kind": "roi", "selection": record(positive), "vendor": {"id": 2**64 - 1}},
        record(fill),
        {**record(positioned), "slice_index": 1},
    ]
    tx = owner.begin_edit()
    tx.set_overlays(overlays)
    owner.commit(tx)
    original = owner.snapshot()
    editor.add_native_document(owner)
    editor.live_document().set_view(orthogonal=orthogonal, crosshair=[1, 2, 0], zoom=1)

    def render():
        frame = editor.render_headless(frames=4)
        assert editor.live_document().id == owner.id, {
            "expected_document": owner.id, "active_document": editor.live_document().id,
            "active_index": editor.active_index, "cache_trace": frame["cache_trace"]}
        assert_stable_view_cache(frame, orthogonal)
        return frame

    def canvas(frame):
        return frame["orthogonal_planes"][0] if orthogonal else frame["ordinary_canvas"]

    def cache_builds(view):
        return view["builds" if orthogonal else "overlay_builds"]

    def assert_widths(layers, zoom):
        for index, wanted in [(0, 1), (3, 5 * zoom)]:
            layer = layers[index]
            assert layer["device_stroke_width"] == wanted
            bounds = layer["device_bounds"]
            assert len(bounds) == 4 and bounds[3] - bounds[1] == wanted
            assert layer["device_spans"] > 0

    first_frame = render()
    first = canvas(first_frame)
    layers = first["overlay_layers"]
    assert [layer["index"] for layer in layers] == list(range(5))
    assert [layer["kind"] for layer in layers] == ["line", "rectangle", "text", "roi", "rectangle"]
    assert [layer["styled"] for layer in layers] == [True, False, False, True, True]
    assert all(layer["role"] == "overlay" for layer in layers)
    assert layers[1]["raster_spans"] > 0 and layers[2]["raster_spans"] > 0
    assert layers[0]["stroke_color"] == [255, 0, 0, 128]
    assert layers[3]["stroke_color"] == [255, 0, 255, 128]
    assert layers[4]["fill_color"] == [0, 255, 255, 64]
    assert layers[4]["device_bounds"] is None
    assert_widths(layers, 1)
    assert [layer["role"] for layer in first["style_layers"]] == ["overlay"] * 3
    repeated_frame = render()
    repeated = canvas(repeated_frame)
    assert_widths(repeated["overlay_layers"], 1)
    assert cache_builds(repeated) == cache_builds(first)
    editor.live_document().set_view(zoom=2)
    zoomed_frame = render()
    zoomed = canvas(zoomed_frame)
    assert_widths(zoomed["overlay_layers"], 2)
    for index in [0, 3]:
        assert layers[index]["device_key"][2] == 1
        assert zoomed["overlay_layers"][index]["device_key"][2] == 2
    zoom_repeated_frame = render()
    zoom_repeated = canvas(zoom_repeated_frame)
    assert_widths(zoom_repeated["overlay_layers"], 2)
    assert cache_builds(zoom_repeated) == cache_builds(zoomed)
    editor.live_document().set_view(crosshair=[1, 2, 1])
    sliced = render()
    slice_layers = canvas(sliced)["overlay_layers"]
    assert [layer["index"] for layer in slice_layers] == list(range(6))
    assert slice_layers[5]["stroke_color"] == [0, 255, 0, 255]
    if orthogonal:
        assert not sliced["orthogonal_planes"][1]["overlay_layers"]
        assert not sliced["orthogonal_planes"][2]["overlay_layers"]
    editor.live_document().set_view(crosshair=[1, 2, 0])
    overlays[0], overlays[1] = overlays[1], overlays[0]
    edit = owner.begin_edit()
    edit.set_overlays(overlays)
    owner.commit(edit)
    reordered = canvas(render())["overlay_layers"]
    assert [layer["styled"] for layer in reordered] == [False, True, False, True, True]
    assert reordered[2]["kind"] == "text" and reordered[3]["kind"] == "roi"
    assert owner.snapshot().overlays == overlays
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYX"), original.dataset.numpy("ZYX"))
    assert owner.snapshot().overlays[3]["vendor"]["id"] == 2**64 - 1
