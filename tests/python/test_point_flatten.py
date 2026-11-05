"""Styled point flatten/export integration and existing desktop font resource."""
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop
from test_roi_style import selection_record


MARKERS = [ij.PointMarker.HYBRID, ij.PointMarker.CROSS, ij.PointMarker.DOT, ij.PointMarker.CIRCLE]
FONTS = [8, 9, 11, 12, 14, 18, 20]


def action(editor, name, **values):editor.action(name, json.dumps(values))


def fixture(tmp_path, marker, size, labels=True, nested=False, style_stroke=True):
    source = np.full((2, 48, 64, 3), [31, 72, 109], np.uint8)
    selected = ij.Selection("multipoint", [(12, 12), (32, 24)], width=17)
    selected = ij.with_point_annotations(selected, [ij.PointAnnotation(0, ij.PlanePosition(0, 0, 1)), ij.PointAnnotation(2)])
    style = ij.PointStyle();style.marker = marker;style.size = size;style.labels = labels
    style.counter_colors = {2: [120, 40, 80, 200]}
    selected = ij.with_point_style(selected, style)
    if style_stroke:selected = ij.with_roi_style(selected, {"stroke_color": "#c8782850"})
    overlay = {**selection_record(selected), "color": "#ffcc00", "slice_index": None}
    if nested:overlay = {"kind": "roi", "selection": selection_record(selected), "color": "#ffcc00", "slice_index": None}
    owner = ij.Document(ij.Dataset(source, axes="ZYXS"));tx = owner.begin_edit();tx.set_overlays([overlay]);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    return editor, owner, source, selected


@pytest.mark.parametrize("marker", MARKERS)
@pytest.mark.parametrize("size", range(7))
def test_flatten_uses_native_glyphs_font_and_current_point_pose(marker, size, tmp_path):
    editor, owner, source, selected = fixture(tmp_path, marker, size)
    before = owner.snapshot();undo = owner.undo_steps
    options = ij.PointPresentationOptions();options.position = ij.PlanePosition(0, 0, 0);options.color = [120, 40, 80, 200]
    font = _desktop.load_point_raster_font(font_size=FONTS[size])
    expected = ij.render_point_style(source[0], selected, options, font)
    action(editor, "flatten")
    actual = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    np.testing.assert_array_equal(actual, expected)
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("ZYXS"), source)
    assert not np.array_equal(expected, source[0])


@pytest.mark.parametrize("nested", [False, True])
@pytest.mark.parametrize("show_all", [False, True])
def test_export_reopen_and_global_visibility(nested, show_all, tmp_path):
    editor, owner, source, selected = fixture(tmp_path, ij.PointMarker.CIRCLE, 5, nested=nested)
    editor.update_settings(json.dumps({"point_show_all": show_all}), False)
    options = ij.PointPresentationOptions();options.position = ij.PlanePosition(0, 0, 0)
    options.color = [120, 40, 80, 200];options.show_all = show_all
    expected = ij.render_point_style(source[0], selected, options, _desktop.load_point_raster_font(font_size=18))
    path = tmp_path/"flatten.png";action(editor, "export_display", path=str(path))
    restored = _desktop.Editor(str(tmp_path/"reopened.json"));restored.open_file(str(path))
    np.testing.assert_array_equal(restored.native_document(json.loads(restored.document_infos())[0]["id"]).snapshot().dataset.numpy("YXS"), expected)
    assert owner.snapshot().overlays[0]["slice_index"] is None
    action(editor, "flatten")
    session = tmp_path/"flatten.ijx";action(editor, "save_document", path=str(session))
    reopened = _desktop.Editor(str(tmp_path/"session.json"));reopened.open_file(str(session))
    np.testing.assert_array_equal(reopened.native_document(json.loads(reopened.document_infos())[0]["id"]).snapshot().dataset.numpy("YXS"), expected)
    assert not ij.point_style(selected).show_all


def test_missing_font_is_atomic_and_null_stroke_retains_fallback(tmp_path):
    editor, owner, source, selected = fixture(tmp_path, ij.PointMarker.DOT, 2)
    overlays = owner.snapshot().overlays
    overlays[0]["font_path"] = str(tmp_path/"missing.ttf")
    tx = owner.begin_edit();tx.set_overlays(overlays);owner.commit(tx)
    before = owner.snapshot();count = len(json.loads(editor.document_infos()));undo = owner.undo_steps
    with pytest.raises(RuntimeError):action(editor, "flatten")
    assert len(json.loads(editor.document_infos())) == count and owner.undo_steps == undo and owner.snapshot().shares_data(before)
    selected = ij.with_roi_style(selected, {"stroke_color": None})
    tx = owner.begin_edit();tx.set_overlays([selection_record(selected)]);owner.commit(tx)
    action(editor, "flatten")
    result = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    options = ij.PointPresentationOptions();options.position = ij.PlanePosition(0, 0, 0);options.color = [255, 204, 0, 255]
    expected = ij.render_point_style(source[0], selected, options, _desktop.load_point_raster_font(font_size=11))
    np.testing.assert_array_equal(result, expected)


def test_font_resource_error_and_staging_budget(tmp_path):
    with pytest.raises((ValueError, RuntimeError)): _desktop.load_point_raster_font(str(tmp_path/"missing.ttf"))
    path = tmp_path/"short.ttf";path.write_bytes(b"bad")
    with pytest.raises(ValueError):_desktop.load_point_raster_font(str(path))
    for size in [0, -1, float("nan"), 513]:
        with pytest.raises(ValueError):_desktop.load_point_raster_font(font_size=size)
    with pytest.raises((ValueError, RuntimeError)):_desktop.load_point_raster_font(max_working_bytes=65536)
    font = _desktop.load_point_raster_font(font_size=12)
    assert font.font_size == 12 and len(font.digits) == 10
    assert all(g.width*g.height == len(g.coverage) and g.advance > 0 for g in font.digits)


@pytest.mark.parametrize("host", ["cpp", "python"])
def test_actual_flatten_menu(host):
    key = "IMAGEJXX_POINT_ANNOTATIONS_"+host.upper()+"_REPORT"
    path = os.environ.get(key)
    if not path:pytest.skip("Requires actual point-properties/flatten input capture")
    report = json.loads(Path(path).read_text(encoding="utf-8"))
    state = report["point_flatten"]
    record = state["selection"];attributes = {k:v for k,v in record.items() if k not in {"kind","points","width"}}
    selection = ij.Selection(record["kind"], record["points"], record.get("width", 1), attributes=attributes)
    options = ij.PointPresentationOptions();options.position = ij.PlanePosition(*state["pose"]);options.color = [255, 204, 0, 255]
    source = np.asarray(state["source_pixels"], dtype=np.uint8).reshape(state["source_shape"])
    if source.shape[2] == 1:source = np.repeat(source, 3, axis=2)
    style = ij.point_style(selection)
    assert style.labels
    expected = ij.render_point_style(source, selection, options, _desktop.load_point_raster_font(font_size=FONTS[style.size]))
    np.testing.assert_array_equal(np.asarray(state["pixels"], dtype=np.uint8).reshape(state["shape"]), expected)
    assert state["source_unchanged"] and state["documents_added"] == 1
    assert "menu.action.flatten" in [c["name"] for c in report["controls"]]


def test_real_font_internal_staging_refuses_with_bounded_allocator():
    import sys
    if os.environ.get("IMAGEJXX_FONT"):
        path = Path(os.environ["IMAGEJXX_FONT"])
    elif os.name == "nt":path = Path(os.environ.get("WINDIR", "C:/Windows"))/"Fonts/segoeui.ttf"
    elif sys.platform == "darwin":path = Path("/System/Library/Fonts/Supplemental/Arial.ttf")
    else:path = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    assert path.is_file()
    # The file plus fixed owner fits, but glyph coverage/internal stb work does
    # not. This exercises allocator refusal after resource IO, not a missing file.
    with pytest.raises((ValueError, RuntimeError), match="staging"):
        _desktop.load_point_raster_font(str(path), 12, path.stat().st_size+65536+512)
    valid = _desktop.load_point_raster_font(str(path), 12)
    assert valid.font_size == 12 and all(len(g.coverage) for g in valid.digits)


def test_global_show_all_preserves_parent_overlay_eligibility_before_font_io(tmp_path):
    editor, owner, source, selected = fixture(tmp_path, ij.PointMarker.HYBRID, 6)
    overlays = owner.snapshot().overlays;overlays[0]["slice_index"] = 1
    overlays[0]["font_path"] = str(tmp_path/"missing.ttf")
    tx = owner.begin_edit();tx.set_overlays(overlays);owner.commit(tx)
    editor.update_settings(json.dumps({"point_show_all": True}), False)
    before = owner.snapshot();action(editor, "flatten")
    result = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    np.testing.assert_array_equal(result, source[0])
    assert owner.snapshot().shares_data(before) and owner.snapshot().overlays == overlays


@pytest.mark.parametrize("opaque", [True, "retained", {"future": [1, None]}])
def test_visibility_setting_does_not_interpret_opaque_document_metadata(opaque, tmp_path):
    pixels = np.zeros((16, 16, 3), np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="YXS"));tx = owner.begin_edit()
    tx.set_property("point_show_all", opaque)
    tx.set_overlays([{"kind": "rectangle", "points": [[2, 2], [10, 10]], "width": 1}])
    owner.commit(tx);before = owner.snapshot()
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    report = json.loads(editor.render_headless());assert not report["window_error"]
    action(editor, "flatten")
    result = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    assert result.any() and owner.snapshot().metadata == before.metadata
    assert owner.snapshot().metadata["properties"]["point_show_all"] == opaque
