"""Point properties/default consumers and actual shared ImGui glyph output."""
import json
import os
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from test_point_presentation import MARKERS, KEY


def fixture(tmp_path, styled=False):
    from imagejxx import _desktop
    selected = ij.Selection("multipoint", [(5, 5), (12, 12)], width=17,
        attributes={"opaque": [None, True, "μ"]})
    selected = ij.with_point_annotations(selected, [ij.PointAnnotation(0, ij.PlanePosition(0, 0, 1)), ij.PointAnnotation(2)])
    if styled:selected = ij.with_point_style(selected, ij.PointStyle())
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.zeros((2, 32, 32), np.uint8), axes="ZYX").observe_reads(reads));doc.select(selected)
    editor = _desktop.Editor(str(tmp_path / "prefs.json"));editor.add_native_document(doc)
    return editor, doc, reads


def action(editor, command, **values):editor.action(command, json.dumps(values))


def style_record(marker=ij.PointMarker.CIRCLE, size=6, labels=False, show_all=True):
    s = ij.PointStyle();s.marker = marker;s.size = size;s.labels = labels;s.show_all = show_all
    return ij.with_point_style(ij.Selection("point", [(1, 1)]), s).attributes[KEY]


def test_properties_style_stage_cancel_apply_clear_and_unknown_recovery(tmp_path):
    editor, doc, reads = fixture(tmp_path);before = doc.snapshot();n = reads.counts().plane_reads
    original = before.selection
    action(editor, "show_selection_properties");action(editor, "selection_properties_draft", point_style=style_record())
    assert doc.snapshot().shares_data(before) and reads.counts().plane_reads == n
    action(editor, "cancel_selection_properties");assert doc.snapshot().selection == original
    action(editor, "show_selection_properties");action(editor, "selection_properties_draft", point_style=style_record())
    action(editor, "apply_selection_properties")
    tagged = doc.snapshot().selection
    assert ij.point_style(tagged).marker == ij.PointMarker.CIRCLE and tagged.width == 17
    assert tagged.points == original.points and ij.imagej_point_counters(tagged, [1, 1, 2, 32, 32, 1]) == [512, 2]
    assert tagged.attributes["opaque"] == original.attributes["opaque"] and doc.undo_steps == 0
    action(editor, "add_roi", name="styled");action(editor, "save_document", path=str(tmp_path / "styled.ijx"))
    action(editor, "show_selection_properties");action(editor, "selection_properties_draft", point_style=None)
    action(editor, "apply_selection_properties")
    assert ij.point_style(doc.snapshot().selection) is None
    assert ij.point_style(doc.snapshot().rois[0].selection).size == 6
    attrs = original.attributes;attrs[KEY] = {"version": 9, "future": [1, 2]}
    unknown = ij.Selection("multipoint", original.points, attributes=attrs);doc.select(unknown)
    action(editor, "show_selection_properties")
    rendered = json.loads(editor.render_headless())
    assert rendered["selection_properties"]["point_style_error"] and not rendered["window_error"]
    assert next(l["point_visual"] for l in rendered["ordinary_canvas"]["style_layers"] if l.get("role") == "selection")["error"]
    with pytest.raises(ValueError):action(editor, "selection_properties_draft", point_style=style_record())
    assert doc.snapshot().selection == unknown
    action(editor, "selection_properties_draft", point_style=None)
    action(editor, "selection_properties_draft", point_style=style_record(size=2))
    action(editor, "apply_selection_properties")
    assert ij.point_style(doc.snapshot().selection).size == 2


@pytest.mark.parametrize("marker", MARKERS)
@pytest.mark.parametrize("size", range(7))
def test_native_markers_are_consumed_by_actual_draw_lists(marker, size, tmp_path):
    editor, doc, _ = fixture(tmp_path)
    selected = ij.with_point_style(doc.snapshot().selection, ij.PointStyle())
    s = ij.point_style(selected);s.marker = marker;s.size = size;s.show_all = True
    doc.select(ij.with_point_style(selected, s))
    report = json.loads(editor.render_headless())
    layers = report["ordinary_canvas"]["style_layers"]
    point = next(l["point_visual"] for l in layers if l.get("role") == "selection")
    assert point["marker"] == int(marker) and point["size"] == size
    assert point["visible_indices"] == [0, 1] and point["emitted_primitives"] > 0 and point["template_builds"] == 1
    assert report["vertices"] > 0 and not report["window_error"]


def test_view_filtering_and_cached_static_rendering_do_not_retag_points(tmp_path):
    editor, doc, _ = fixture(tmp_path, True);before = doc.snapshot();undo = doc.undo_steps
    first = json.loads(editor.render_headless());second = json.loads(editor.render_headless())
    def point(r):return next(l["point_visual"] for l in r["ordinary_canvas"]["style_layers"] if l.get("role") == "selection")
    assert point(first)["visible_indices"] == point(second)["visible_indices"] == [1]
    assert second["mask_outline_builds"] == first["mask_outline_builds"]
    assert doc.snapshot().shares_data(before) and doc.undo_steps == undo
    view = doc.snapshot().display;view.crosshair = [0, 0, 1];doc.set_display(view)
    third = json.loads(editor.render_headless())
    assert point(third)["visible_indices"] == [0, 1]
    assert third["mask_outline_builds"] == first["mask_outline_builds"]
    assert ij.point_annotations(doc.snapshot().selection)[0].position.z == 1


def test_consumed_settings_validate_persist_and_reset(tmp_path):
    s = ij.Settings();assert s["point_marker"] == "Hybrid" and s["point_marker_size"] == "Small"
    s.update({"point_marker": "Circle", "point_marker_size": "XXXL", "point_labels": False, "point_show_all": True})
    before = s.to_dict()
    with pytest.raises(ValueError):s.update({"point_marker": "unknown", "point_marker_size": "Tiny"})
    assert s.to_dict() == before
    p = ij.PreferencesDocument();p.settings = s;path = tmp_path / "prefs.json";p.save(path)
    loaded = ij.PreferencesDocument.load(path, False);assert loaded.settings.to_dict() == before
    loaded.settings.reset("Drawing")
    assert loaded.settings["point_marker"] == "Hybrid" and loaded.settings["point_marker_size"] == "Small"
    assert loaded.settings["point_labels"] and not loaded.settings["point_show_all"]


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
@pytest.mark.parametrize("stack", [False, True])
def test_actual_point_preferences_and_creation_style(stack, context, tmp_path):
    from actual_observation_transport import decode_actual_observations
    key = "IMAGEJXX_POINT_STACK_DEFAULTS" if stack else "IMAGEJXX_POINT_DEFAULTS"
    if context == "imgui_cpp" or os.environ.get(key+"_PYTHON_REPORT"):
        path = Path(os.environ[key+("_CPP_REPORT" if context == "imgui_cpp" else "_PYTHON_REPORT")]);raw = json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "preferences.json"))
        raw = json.loads(editor._settings_drawing_ui_case("point-stack-defaults" if stack else "point-defaults", str(tmp_path / "capture")))
    report = decode_actual_observations(raw);assert report["state"] == "observed"
    states = report["states"];multi = states["multipoint_created"]["owners"][0]["selection"][KEY]
    single = states["single_created"]["owners"][0]["selection"][KEY]
    assert (multi["marker"], multi["size"], multi["labels"], multi["show_all"]) == (3, 5, False, False)
    assert (single["marker"], single["size"], single["labels"], single["show_all"]) == (1, 0, False, False)
    assert states["multipoint_created"]["owners"][0]["selection"]["imagejxx.roi_style"]["stroke_color"] == [224, 31, 64, 255]
    assert states["single_created"]["owners"][0]["selection"]["imagejxx.roi_style"]["stroke_color"] == [16, 32, 224, 128]
    assert states["final"]["settings"]["point_marker"] == "Cross"
    saved = json.loads(states["final"]["preferences_file"]["utf8"])
    assert saved["values"]["point_marker"] == "Cross" and saved["values"]["point_marker_size"] == "Tiny"


@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_point_properties_style_and_all_view_roles(context, tmp_path):
    if context == "imgui_cpp":report = json.loads(Path(os.environ["IMAGEJXX_POINT_ANNOTATIONS_CPP_REPORT"]).read_bytes())
    elif os.environ.get("IMAGEJXX_POINT_ANNOTATIONS_PYTHON_REPORT"):
        report = json.loads(Path(os.environ["IMAGEJXX_POINT_ANNOTATIONS_PYTHON_REPORT"]).read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "prefs.json"));report = json.loads(editor._point_annotations_ui_case(str(tmp_path / "capture")))
    assert report["state"] == "observed"
    style = report["applied"][KEY]
    assert (style["marker"], style["size"], style["labels"], style["show_all"]) == (3, 3, False, True)
    assert report["stored"][KEY] == style
    layers = report["point_style_render"]["style_layers"]
    assert {p.get("role") for p in layers if p.get("point_visual")} >= {"selection", "stored_roi"}
    for p in layers:
        if p.get("point_visual"):assert p["point_visual"]["visible_indices"] == [0, 1, 2]
    assert report["point_style_render"]["style_layers"] == report["point_style_repeat"]["style_layers"]


def test_global_show_all_changes_drawing_without_retagging_selection(tmp_path):
    editor, doc, _ = fixture(tmp_path, True);before = doc.snapshot()
    editor.update_settings(json.dumps({"point_show_all": True}), False)
    report = json.loads(editor.render_headless())
    point = next(l["point_visual"] for l in report["ordinary_canvas"]["style_layers"] if l.get("role") == "selection")
    assert point["visible_indices"] == [0, 1] and point["show_all_override"]
    assert not ij.point_style(doc.snapshot().selection).show_all and doc.snapshot().selection == before.selection
    editor.update_settings(json.dumps({"point_show_all": False}), False)
    report = json.loads(editor.render_headless())
    point = next(l["point_visual"] for l in report["ordinary_canvas"]["style_layers"] if l.get("role") == "selection")
    assert point["visible_indices"] == [1] and not point["show_all_override"]


@pytest.mark.parametrize("scale", [0.5, 1.0, 2.0, 3.0])
def test_marker_dpi_uses_host_scale_without_changing_raster_diameter(scale, tmp_path):
    editor, doc, _ = fixture(tmp_path, True);before = doc.snapshot().selection
    editor.update_settings(json.dumps({"ui_scale": scale, "auto_dpi": False}), False)
    report = json.loads(editor.render_headless())
    point = next(l["point_visual"] for l in report["ordinary_canvas"]["style_layers"] if l.get("role") == "selection")
    assert point["marker_scale"] == report["gui_scale"] == scale
    assert doc.snapshot().selection == before and doc.snapshot().selection.width == 17


@pytest.mark.parametrize("plane", ["XY", "XZ", "YZ"])
@pytest.mark.parametrize("context", ["imgui_cpp", "imgui_python"])
def test_actual_scientific_point_marker_properties(plane, context, tmp_path):
    case = plane+".point_style"
    if context == "imgui_cpp":report = json.loads((Path(os.environ["IMAGEJXX_PLANAR_MATRIX_CPP_ROOT"])/case/"case.json").read_bytes())
    else:
        from imagejxx import _desktop
        editor = _desktop.Editor(str(tmp_path / "prefs.json"))
        report = json.loads(editor._planar_annotation_matrix_ui_case(case, str(tmp_path / "capture")))
    assert report["state"] == "observed" and report["context"] == context and not report["error"]
    states = report["states"]
    def rows(state):return state["metadata"]["imagejxx.planar_annotations"]["rows"]
    before = rows(states["before"])[0]
    assert rows(states["staged"])[0] == before and rows(states["reverted"])[0] == before
    applied = rows(states["applied"])[0]
    style = applied["selection"]["attributes"][KEY]
    assert (style["marker"], style["size"], style["labels"], style["show_all"]) == (3, 3, False, True)
    assert applied["selection"]["points"] == before["selection"]["points"] and applied["plane"] == plane
    assert applied["selection"]["attributes"]["imagejxx.point_annotations"] == before["selection"]["attributes"]["imagejxx.point_annotations"]
    assert rows(states["undone"])[0] == before and rows(states["redone"])[0] == applied
    assert rows(report["reopened"])[0] == applied
    for state in states.values():assert state["pixels"] == states["before"]["pixels"] and state["selection"] is None
    layers = report["point_panels"]["applied"]["layers"]
    point = next(l["point_visual"] for l in layers if l.get("row") == 0)
    assert point["marker"] == 3 and point["visible_indices"] == [0, 1] and point["emitted_primitives"] == 2
    ij.validate_planar_annotation_command_record(states["applied"]["workflow"][0])
