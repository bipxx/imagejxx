"""Typed native ROI style semantics, persistence and frozen operation records."""

from copy import deepcopy
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


STYLE_KEY = "imagejxx.roi_style"
SHAPE = (9, 9)
REFERENCE_PATH = Path(__file__).resolve().parents[1] / "fixtures/roi-style-imagej154r.json"
REFERENCE = json.loads(REFERENCE_PATH.read_text(encoding="utf-8"))
LEGACY = {
    "color": "#12345678",
    "label": "Retained",
    "vendor": {"id": 2**64 - 1, "flags": [None, True]},
}


def area():
    return ij.Selection("rectangle", [(2, 2), (7, 7)], 3, attributes=LEGACY)


def styled_area():
    return ij.with_roi_style(
        area(),
        {
            "fill_color": "#80123456",
            "stroke_color": "green",
            "stroke_width": 0,
            "vendor": {"values": [None, True, 2**64 - 1]},
        },
    )


def selection_record(selection):
    return {
        **selection.attributes,
        "kind": selection.kind,
        "points": [tuple(point) for point in selection.points],
        "width": selection.width,
    }


@pytest.mark.parametrize(
    "text,expected",
    [
        ("red", [255, 0, 0, 255]),
        ("green", [0, 255, 0, 255]),
        ("blue", [0, 0, 255, 255]),
        ("magenta", [255, 0, 255, 255]),
        ("cyan", [0, 255, 255, 255]),
        ("yellow", [255, 255, 0, 255]),
        ("orange", [255, 200, 0, 255]),
        ("black", [0, 0, 0, 255]),
        ("white", [255, 255, 255, 255]),
        ("gray", [128, 128, 128, 255]),
        ("grey", [128, 128, 128, 255]),
        ("Light Grey", [192, 192, 192, 255]),
        ("darkgray", [64, 64, 64, 255]),
        ("pink", [255, 175, 175, 255]),
        ("#123456", [18, 52, 86, 255]),
        ("#80123456", [18, 52, 86, 128]),
        ("#00123456", [18, 52, 86, 0]),
        ("#FFABCDEF", [171, 205, 239, 255]),
        ("none", None),
        (" NULL ", None),
    ],
)
def test_public_imagej_colors_and_canonical_round_trip(text, expected):
    parsed = ij.parse_roi_color(text)
    assert parsed == expected
    assert ij.parse_roi_color(ij.roi_color_string(parsed)) == expected


@pytest.mark.parametrize("text", ["", "#12345", "#1234567", "#gg123456", "unknown"])
def test_invalid_public_colors(text):
    with pytest.raises(ValueError):
        ij.parse_roi_color(text)


def test_legacy_hex_and_green_remain_distinct_from_new_style():
    assert not ij.roi_style(area()).present
    assert ij.resolve_roi_style(area()).stroke_color == [18, 52, 86, 120]
    legacy_green = area().with_attributes({"color": "green"})
    assert ij.resolve_roi_style(legacy_green).stroke_color == [0, 128, 0, 255]
    styled = ij.with_roi_style(area(), {"stroke_color": "#80123456"})
    assert ij.resolve_roi_style(styled).stroke_color == [18, 52, 86, 128]
    assert styled.attributes["color"] == LEGACY["color"]


def test_fill_then_stroke_order_null_and_transparent_values():
    fallback = [9, 8, 7, 6]
    stroke = ij.with_roi_style(area(), {"stroke_color": "green"})
    filled = ij.with_roi_style(stroke, {"fill_color": "#80123456"})
    style = ij.roi_style(filled)
    assert style.present and style.has_fill and style.has_stroke
    assert style.fill_color == [18, 52, 86, 128] and style.stroke_color is None
    assert ij.resolve_roi_style(filled, fallback).stroke_color is None
    both = ij.with_roi_style(
        stroke, {"stroke_color": "#80403020", "fill_color": "#80123456"}
    )
    assert ij.roi_style(both).stroke_color == [64, 48, 32, 128]
    assert ij.roi_style(both).fill_color == style.fill_color
    cleared = ij.with_roi_style(filled, {"fill_color": None})
    assert ij.resolve_roi_style(cleared, fallback).fill_color is None
    assert ij.resolve_roi_style(cleared, fallback).stroke_color == fallback
    transparent_fill = ij.with_roi_style(area(), {"fill_color": [18, 52, 86, 0]})
    assert ij.roi_style(transparent_fill).fill_color == [18, 52, 86, 0]
    assert ij.resolve_roi_style(transparent_fill, fallback).stroke_color is None
    transparent_stroke = ij.with_roi_style(area(), {"stroke_color": "#00123456"})
    assert ij.resolve_roi_style(transparent_stroke, fallback).stroke_color == [18, 52, 86, 0]
    empty = ij.with_roi_style(area(), {})
    assert ij.roi_style(empty).present and not ij.roi_style(empty).has_stroke
    assert ij.resolve_roi_style(empty, fallback).stroke_color == [18, 52, 86, 120]


def test_canonical_detached_editor_patch_preserves_nested_extensions():
    source = styled_area()
    editor = ij.SelectionEditor(source, SHAPE)
    patch = {"stroke_width": 2.5, "stroke_color": [1, 2, 3, 4]}
    result = editor.roi_style(patch)
    patch["stroke_color"][0] = 200
    attrs = result.attributes
    assert attrs[STYLE_KEY] == {
        "schema_version": 1,
        "stroke_color": [1, 2, 3, 4],
        "fill_color": [18, 52, 86, 128],
        "stroke_width": 2.5,
        "vendor": {"values": [None, True, 2**64 - 1]},
    }
    assert {key: attrs[key] for key in LEGACY} == LEGACY
    assert result.points == source.points and result.width == source.width == 3
    attrs[STYLE_KEY]["vendor"]["values"][0] = "mutated detached copy"
    assert result.attributes[STYLE_KEY]["vendor"]["values"][0] is None
    assert editor.selection.attributes == source.attributes
    assert ij.roi_style(source).stroke_width == 0


@pytest.mark.parametrize(
    "patch",
    [
        {"schema_version": 2}, {"schema_version": True},
        {"schema_version": 1.0}, {"schema_version": "1"},
        {"stroke_width": -1}, {"stroke_width": True},
        {"stroke_width": None}, {"stroke_width": "2"},
        {"stroke_width": float("inf")}, {"stroke_width": float("nan")},
        {"stroke_color": [1, 2, 3]}, {"stroke_color": [1, 2, 3, 256]},
        {"stroke_color": [1.0, 2, 3, 4]}, {"stroke_color": [True, 2, 3, 4]},
        {"fill_color": [1, 2, -1, 4]}, {"stroke_color": 12},
        {"fill_color": "#invalid"}, {"vendor": {"nan": float("nan")}},
    ],
)
def test_invalid_patch_is_atomic(patch):
    source = styled_area()
    before = deepcopy(source.attributes)
    editor = ij.SelectionEditor(source, SHAPE)
    with pytest.raises((ValueError, TypeError, OverflowError)):
        editor.roi_style({"extension": "Must not publish", **patch})
    assert source.attributes == editor.selection.attributes == before
    assert editor.selection.width == 3 and editor.selection.points == source.points


@pytest.mark.parametrize(
    "record",
    [None, "wrong", {}, {"schema_version": 1, "stroke_color": "red"}],
)
def test_malformed_persisted_style_rejected_by_native_ingestion(record):
    bad = area().with_attributes({STYLE_KEY: record})
    document = ij.Document(ij.Dataset(np.zeros(SHAPE, np.uint16), axes="YX"))
    with pytest.raises(ValueError):
        bad.mask(SHAPE)
    with pytest.raises(ValueError):
        document.select(bad)
    assert document.snapshot().selection is None and document.undo_steps == 0


@pytest.mark.parametrize("kind", ["line", "polyline", "angle"])
@pytest.mark.parametrize("width", [0, 0.5, 5, 1e300])
def test_styled_line_processing_width_preserves_raw_legacy_width(kind, width):
    points = [(2, 4), (6, 4)]
    source = ij.Selection(kind, points, 3)
    styled = ij.with_roi_style(source, {"stroke_width": width, "fill_color": "red"})
    assert styled.width == source.width == 3
    assert ij.resolve_roi_style(styled).cosmetic == (width == 0)
    assert ij.resolve_roi_style(styled).fill_color is None
    if width == 1e300:
        np.testing.assert_array_equal(styled.mask(SHAPE), 255)
    else:
        expected = ij.Selection(kind, points, width if width else 1).mask(SHAPE)
        np.testing.assert_array_equal(styled.mask(SHAPE), expected)


@pytest.mark.parametrize("kind,points", [("point", [(2, 2)]), ("multipoint", [(2, 2), (7, 7)])])
def test_new_stroke_width_does_not_change_point_markers(kind, points):
    source = ij.Selection(kind, points, 3)
    styled = ij.with_roi_style(source, {"stroke_width": 1e300})
    np.testing.assert_array_equal(styled.mask(SHAPE), source.mask(SHAPE))


def test_area_processing_mask_and_exact_alpha_fill_preserve_holes():
    bitmap = np.zeros(SHAPE, np.uint8)
    bitmap[1:6, 1:6] = 255
    bitmap[3, 3] = 0
    bitmap[7, 7] = 255
    selection = ij.Selection.from_mask(bitmap)
    styled = ij.with_roi_style(selection, {"fill_color": [10, 200, 30, 128], "stroke_width": 6})
    np.testing.assert_array_equal(styled.mask(SHAPE), bitmap)
    rgb = np.full((*SHAPE, 3), [100, 30, 210], np.uint8)
    before = rgb.copy()
    expected = rgb.copy()
    expected[bitmap != 0] = np.floor(
        np.array([100, 30, 210]) * (1 - 128 / 255)
        + np.array([10, 200, 30]) * (128 / 255) + 0.5
    ).astype(np.uint8)
    np.testing.assert_array_equal(ij.render_roi_style(rgb, styled), expected)
    np.testing.assert_array_equal(rgb, before)
    transparent = ij.with_roi_style(area(), {"stroke_color": [1, 2, 3, 0]})
    np.testing.assert_array_equal(ij.render_roi_style(rgb, transparent), before)


def test_alpha_outline_blends_after_fill_and_bounds_huge_width():
    rgb = np.full((*SHAPE, 3), [100, 30, 210], np.uint8)
    fill = np.array([10, 200, 30])
    stroke = np.array([200, 10, 60])
    styled = ij.with_roi_style(area(), {"fill_color": [*fill.tolist(), 128], "stroke_color": [*stroke.tolist(), 64], "stroke_width": 0})
    rendered = ij.render_roi_style(rgb, styled)
    interior = np.floor(np.array([100, 30, 210]) * (1 - 128 / 255) + fill * (128 / 255) + 0.5)
    outline = np.floor(interior * (1 - 64 / 255) + stroke * (64 / 255) + 0.5)
    np.testing.assert_array_equal(rendered[4, 4], interior)
    np.testing.assert_array_equal(rendered[2, 2], outline)
    np.testing.assert_array_equal(rendered[0, 0], rgb[0, 0])
    np.testing.assert_array_equal(styled.mask(SHAPE), area().mask(SHAPE))
    huge = ij.with_roi_style(area(), {"stroke_color": "green", "stroke_width": 1e300})
    np.testing.assert_array_equal(ij.render_roi_style(rgb, huge), np.full_like(rgb, [0, 255, 0]))


def test_styled_area_color_only_inherits_raw_positive_width_for_export():
    source = ij.Selection("rectangle", [(2, 2), (7, 7)], 3)
    styled = ij.with_roi_style(source, {"stroke_color": "red"})
    assert ij.roi_style(styled).stroke_width is None
    assert ij.resolve_roi_style(styled).stroke_width == 3
    rgb = np.zeros((*SHAPE, 3), np.uint8)
    rendered = ij.render_roi_style(rgb, styled)
    np.testing.assert_array_equal(rendered[4, 1], [255, 0, 0])
    np.testing.assert_array_equal(rendered[4, 2], [255, 0, 0])
    np.testing.assert_array_equal(rendered[4, 4], [0, 0, 0])
    np.testing.assert_array_equal(rendered[4, 0], [0, 0, 0])
    explicit = ij.with_roi_style(styled, {"stroke_width": 3})
    np.testing.assert_array_equal(rendered, ij.render_roi_style(rgb, explicit))
    np.testing.assert_array_equal(styled.mask(SHAPE), source.mask(SHAPE))


@pytest.mark.parametrize("pixels", [np.zeros(SHAPE, np.uint8), np.zeros((*SHAPE, 3), np.uint16)])
def test_render_requires_uint8_rgb(pixels):
    with pytest.raises(ValueError):
        ij.render_roi_style(pixels, styled_area())


def test_native_active_previous_stored_roi_history_and_pixel_sharing():
    document = ij.Document(ij.Dataset(np.zeros(SHAPE, np.uint16), axes="YX"))
    document.select(area())
    styled = styled_area()
    document.select(styled)
    source = document.snapshot()
    assert source.previous_selection.attributes == LEGACY
    assert not source.dirty and document.undo_steps == 0
    tx = document.begin_edit()
    tx.set_rois([ij.ROI("Stored", styled, t=0, c=0, z=0, attributes={"vendor": "ROI extension"})])
    document.commit(tx)
    stored = document.snapshot()
    edit = document.begin_edit()
    changed = ij.with_roi_style(styled, {"stroke_color": "blue"})
    edit.set_rois([ij.ROI("Stored", changed, t=0, c=0, z=0, attributes=stored.rois[0].attributes)])
    document.commit(edit)
    assert document.undo_steps == 2 and document.history().pixels.obsolete_bytes == 0
    assert stored.rois[0].selection.attributes == styled.attributes
    document.undo()
    assert document.snapshot().rois[0].selection.attributes == styled.attributes
    document.redo()
    assert ij.roi_style(document.snapshot().rois[0].selection).stroke_color == [0, 0, 255, 255]
    clone = ij.Document(stored)
    assert clone.snapshot().selection.attributes == styled.attributes
    assert clone.snapshot().previous_selection.attributes == LEGACY
    assert clone.snapshot().rois[0].attributes == {"vendor": "ROI extension"}


@pytest.mark.parametrize("bad_style", [
    {"schema_version": 2, "stroke_color": [1, 2, 3, 4]},
    {"schema_version": 1, "stroke_color": [1.0, 2, 3, 4]},
    {"schema_version": 1, "stroke_width": -1},
])
def test_nested_roi_overlay_style_is_validated_before_transaction_replacement(bad_style):
    owner = ij.Document(ij.Dataset(np.zeros(SHAPE, np.uint16), axes="YX"), "Original")
    styled = styled_area()
    valid = json.loads(json.dumps({"kind": "roi", "selection": selection_record(styled),
                                  "vendor": {"values": [None, True, 2**64 - 1]}}))
    initial = owner.begin_edit()
    initial.set_overlays([valid])
    owner.commit(initial)
    source = owner.snapshot()
    tx = owner.begin_edit()
    tx.set_title("Prepared")
    malformed = deepcopy(valid)
    malformed["selection"][STYLE_KEY] = bad_style
    with pytest.raises(ValueError):
        tx.set_overlays([valid, malformed])
    assert tx.active and tx.preview().metadata["title"] == "Prepared"
    assert tx.preview().overlays == source.overlays == [valid]
    assert owner.snapshot().shares_data(source) and owner.undo_steps == 1
    owner.commit(tx)
    assert owner.snapshot().overlays == [valid] and owner.undo_steps == 2
    owner.undo()
    assert owner.snapshot().metadata["title"] == "Original"
    assert owner.snapshot().overlays == [valid]
    owner.redo()
    assert owner.snapshot().overlays[0]["selection"][STYLE_KEY] == styled.attributes[STYLE_KEY]


def test_python_model_roi_json_and_session_preserve_style(tmp_path):
    from imagejxx.ui.model import Document, Selection
    from imagejxx.ui.persistence import load_rois, load_session, save_rois, save_session

    record = selection_record(styled_area())
    selection = Selection.from_dict(record)
    assert selection.to_dict() == record
    document = Document(np.zeros(SHAPE, np.uint16))
    document.select(selection)
    document.add_roi("Stored")
    path = tmp_path / "roi.json"
    save_rois(path, document.rois)
    assert load_rois(path, SHAPE)[0].selection.to_dict() == record
    session = tmp_path / "model.ijx"
    save_session(session, [document])
    restored = load_session(session)[0][0]
    assert restored.selection.to_dict() == restored.rois[0].selection.to_dict() == record
    line = ij.with_roi_style(ij.Selection("line", [(2, 4), (6, 4)], 3), {"stroke_width": 5})
    model_line = Selection.from_dict(selection_record(line))
    np.testing.assert_array_equal(model_line.mask(SHAPE), line.mask(SHAPE))


@pytest.mark.parametrize("bad_style", [
    {"schema_version": 2, "stroke_color": [1, 2, 3, 4]},
    {"schema_version": 1, "fill_color": [1.0, 2, 3, 4]},
    {"schema_version": 1, "stroke_width": -1},
])
def test_python_model_metadata_load_rejects_late_overlay_style_atomically(bad_style):
    from imagejxx.ui.model import Document, NamedROI, Overlay, Selection

    document = Document(np.arange(81, dtype=np.uint16).reshape(SHAPE), "Original")
    selection = Selection.from_dict(selection_record(styled_area()))
    document.select(selection)
    document.select(Selection("point", [(1, 1)], attributes={"label": "Prior"}))
    document.rois = [NamedROI("Stored", selection)]
    document.overlays = [Overlay("rectangle", [(2, 2), (7, 7)],
        color=selection.attributes["color"], width=selection.width,
        attributes={key: value for key, value in selection.attributes.items() if key != "color"})]
    document.view_state = {"zoom": 2, "pan": [[3, 4], [0, 0], [0, 0]]}
    before = document.metadata()
    incoming = deepcopy(before)
    incoming["title"] = "Must not publish"
    incoming["view_state"] = {"zoom": 7}
    incoming["calibration"]["pixel_width"] = 2
    incoming["selection"] = selection.to_dict()
    incoming["previous_selection"] = Selection("point", [(4, 4)]).to_dict()
    incoming["rois"] = [NamedROI("Replacement", Selection("point", [(5, 5)])).to_dict()]
    incoming["overlays"].append({"kind": "rectangle", "points": [[1, 1], [5, 5]],
                                  STYLE_KEY: bad_style})
    with pytest.raises(ValueError):
        document.load_metadata(incoming)
    assert document.metadata() == before and document.title == "Original"
    np.testing.assert_array_equal(document.data, np.arange(81, dtype=np.uint16).reshape(SHAPE))


@pytest.fixture
def desktop(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def test_native_owner_workspace_and_roi_json_keep_all_styled_records(desktop, tmp_path):
    from imagejxx.ui.model import Selection

    owner = ij.Document(ij.Dataset(np.zeros((2, *SHAPE), np.uint16), axes="ZYX"))
    styled = styled_area()
    owner.select(area())
    owner.select(styled)
    tx = owner.begin_edit()
    tx.set_rois([ij.ROI("Stored", styled, z=1, attributes={"vendor": "ROI extension"})])
    tx.set_overlays([{ "kind": "roi", "selection": selection_record(styled), "vendor": {"id": 2**64 - 1}}])
    owner.commit(tx)
    desktop.add_native_document(owner)
    before = desktop.live_document().native_snapshot()
    path = tmp_path / "native.ijx"
    desktop.action("save_workspace", path=str(path))
    desktop.open_file(path)
    restored = desktop.live_document().native_snapshot()
    assert restored.selection.attributes == before.selection.attributes
    assert restored.previous_selection.attributes == before.previous_selection.attributes
    assert restored.rois[0].selection.attributes == styled.attributes
    assert restored.rois[0].z == 1 and restored.rois[0].attributes == {"vendor": "ROI extension"}
    assert restored.overlays == before.overlays
    roi_path = tmp_path / "native-rois.json"
    desktop.action("save_rois", path=str(roi_path))
    clear = desktop.live_document().begin_edit()
    clear.set_rois([])
    desktop.live_document().commit(clear)
    desktop.action("load_rois", path=str(roi_path))
    loaded = desktop.live_document().native_snapshot().rois[0]
    assert loaded.selection.attributes == styled.attributes and loaded.z == 1
    assert Selection.from_dict(selection_record(loaded.selection)).attributes == styled.attributes


def test_nested_roi_overlay_style_workspace_model_and_flatten(desktop, tmp_path):
    rgb = np.full((*SHAPE, 3), [100, 30, 210], np.uint8)
    owner = ij.Document(ij.Dataset(rgb, axes="YXS"))
    styled = ij.with_roi_style(area(), {"fill_color": "#80123456", "stroke_color": None,
                                       "stroke_width": 0})
    overlay = json.loads(json.dumps({"kind": "roi", "selection": selection_record(styled),
                                    "vendor": {"id": 2**64 - 1}}))
    tx = owner.begin_edit()
    tx.set_overlays([overlay])
    owner.commit(tx)
    desktop.add_native_document(owner)
    path = tmp_path / "nested-overlay.ijx"
    desktop.action("save_workspace", path=str(path))
    desktop.open_file(path)
    restored = desktop.live_document().native_snapshot()
    assert restored.overlays == [overlay]
    model_record = desktop.document.overlays[0].to_dict()
    assert model_record["kind"] == "roi"
    assert model_record["selection"] == overlay["selection"]
    assert model_record["vendor"] == overlay["vendor"]
    desktop.action("flatten")
    np.testing.assert_array_equal(desktop.document.data, ij.render_roi_style(rgb, styled))
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YXS"), rgb)


def test_pixel_operation_freezes_styled_line_and_replays_after_owner_changes(desktop):
    from imagejxx.ui import workflows
    from imagejxx.ui.model import Document, Selection

    pixels = np.arange(81, dtype=np.uint16).reshape(SHAPE)
    styled = ij.with_roi_style(ij.Selection("line", [(2, 4), (6, 4)], 3), {"stroke_color": "#80123456", "stroke_width": 5, "vendor": {"id": 2**64 - 1}})
    model = Document(pixels)
    model.select(Selection.from_dict(selection_record(styled)))
    desktop.add_document(model)
    desktop.recording = True
    desktop.apply_operation("add", {"value": 7}, asynchronous=False)
    frozen = deepcopy(desktop.workflow)
    assert len(frozen) == 1 and frozen[0]["selection"][STYLE_KEY] == styled.attributes[STYLE_KEY]
    wanted = pixels.copy()
    wanted[styled.mask(SHAPE) != 0] += 7
    np.testing.assert_array_equal(desktop.document.data, wanted)
    assert workflows.loads(workflows.dumps(frozen)) == frozen
    np.testing.assert_array_equal(workflows.run(Document(pixels), frozen), wanted)
    desktop.action("undo")
    desktop.live_document().native_owner.select(ij.Selection("point", [(0, 0)]))
    desktop.action("run_workflow")
    desktop.wait()
    np.testing.assert_array_equal(desktop.document.data, wanted)
    assert frozen[0]["selection"][STYLE_KEY] == styled.attributes[STYLE_KEY]


def test_pinned_roi_reference_metadata_and_scope():
    assert REFERENCE["imagej_version"] == "1.54r"
    assert REFERENCE["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert REFERENCE["source_archive_sha256"] == "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
    assert REFERENCE["scope"].startswith("Direct Colors.decode and Roi setter state")
    assert "Not RoiProperties GUI, processing-mask or display/antialias raster certification" in REFERENCE["scope"]
    assert len(REFERENCE["cases"]) == 73


@pytest.mark.parametrize(
    "case", REFERENCE["cases"],
    ids=lambda case: "color-" + case["input"] if case["operation"] == "color"
        else case["operation"] + "-" + case["kind"] + "-" + case["case"],
)
def test_pinned_direct_roi_setter_and_color_reference(case):
    shapes = {
        "rectangle": [(2, 2), (7, 7)], "ellipse": [(2, 2), (7, 7)],
        "polygon": [(2, 2), (7, 2), (4, 7)],
        "line": [(2, 4), (6, 4)], "polyline": [(2, 4), (6, 4), (6, 7)],
        "multipoint": [(2, 2), (7, 7)],
    }
    if case["operation"] == "color":
        assert ij.parse_roi_color(case["input"]) == case["rgba"]
        return
    selection = ij.Selection(case["kind"], shapes[case["kind"]])
    for patch in case["patches"]:
        selection = ij.with_roi_style(selection, patch)
    style = ij.roi_style(selection)
    assert style.stroke_color == case["stroke"]
    assert style.fill_color == case["fill"]
    if case["operation"] == "width":
        assert style.stroke_width == case["width"]
