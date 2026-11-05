"""Pinned ROI integer bounds and both public/native editor conversion paths."""
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/bounding-rectangle-imagej154r.json").read_bytes())


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_pinned_bounds(case):
    source = ij.Selection(case["kind"], case["points"])
    before = source.to_dict()
    result = ij.bounding_rectangle_selection(source, 128, 128)
    x, y, right, bottom = case["bounds"]
    assert result.kind == "rectangle" and result.points == [[x, y], [right, bottom]]
    assert source.to_dict() == before


def points():
    return ij.Selection("multipoint", [(2, 2), (12, 2), (8, 8), (12, 12), (2, 12)])


def test_style_limits_freezing_and_metadata():
    source = ij.Selection("polygon", [(2.2, 3.7), (12.8, 3.1), (5.5, 13.2)], 3,
                          attributes={"opaque": {"future": "retained"}})
    result = ij.bounding_rectangle_selection(source, 16, 16)
    assert result.width == 3 and result.attributes == source.attributes
    limits = ij.SelectionEditLimits()
    limits.max_working_bytes = ij.estimate_bounding_rectangle_selection(source, 16, 16)
    assert ij.bounding_rectangle_selection(source, 16, 16, limits=limits) == result
    limits.max_working_bytes -= 1
    events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.bounding_rectangle_selection(source, 16, 16, limits=limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    assert ij.bounding_rectangle_selection(source, 16, 16, limits=limits,
        checkpoint=lambda: setattr(limits, "max_vertices", 1)) == result
    for annotated in [ij.with_point_annotations(points(), [ij.PointAnnotation(2)] * 5),
                      ij.with_point_style(points(), ij.PointStyle())]:
        with pytest.raises(ValueError, match="explicit discard"):
            ij.bounding_rectangle_selection(annotated, 16, 16)
        assert ij.bounding_rectangle_selection(annotated, 16, 16, discard_point_metadata=True).attributes == {}


def test_native_path_and_rotated_bounds():
    path = ij.Selection.from_path(ij.ImageJCompositePath([0, 2, 3, 1, 8, 3, 1, 8, 9, 4]))
    assert ij.bounding_rectangle_selection(path, 16, 16).points == [[2, 3], [8, 9]]
    rotated = ij.Selection("rotated_ellipse", [(10, 10), (14, 10), (10, 12)])
    assert ij.bounding_rectangle_selection(rotated, 16, 16).points == [[6, 8], [14, 12]]
    box = ij.bounding_rectangle_selection(points(), 16, 16)
    assert ij.SelectionEditor(box, (16, 16)).resized(4, (14, 14)).points == [[2, 2], [14, 14]]


def test_occupied_mask_region_bounds_and_empty_refusal():
    mask = ij.Selection.from_mask(np.array([[0, 1, 0], [0, 0, 1]], dtype=np.uint8))
    assert ij.bounding_rectangle_selection(mask, 3, 2).points == [[1, 0], [3, 2]]
    region = ij.SelectionRegion(mask, (2, 3), (-5, 7)).as_selection()
    assert ij.bounding_rectangle_selection(region, 16, 16).points == [[-4, 7], [-2, 9]]
    empty = ij.Selection.from_mask(np.zeros((2, 3), dtype=np.uint8))
    with pytest.raises(ValueError, match="nonempty geometry"):
        ij.bounding_rectangle_selection(empty, 3, 2)


def test_cancellation_callback_exception_and_type_refusals():
    source = points(); before = source.to_dict()
    token = ij.CancellationToken(); token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.bounding_rectangle_selection(source, 16, 16, cancellation=token)
    def fail():
        raise RuntimeError("owned bounds callback")
    with pytest.raises(RuntimeError, match="owned bounds callback"):
        ij.bounding_rectangle_selection(source, 16, 16, checkpoint=fail)
    for options in [{"width": True}, {"height": False}, {"discard_point_metadata": 1}]:
        with pytest.raises(TypeError):
            ij.bounding_rectangle_selection(source, **{"width": 16, "height": 16, **options})
    events = []
    with pytest.raises(ValueError):
        ij.bounding_rectangle_selection(ij.Selection("point", [(1e300, 1)]), 16, 16,
                                        checkpoint=lambda: events.append(1))
    assert not events and source.to_dict() == before


def test_editor_history_save_replay_and_unchanged_pixels(tmp_path):
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.arange(256, dtype=np.uint8).reshape(16, 16), axes="YX"))
    source = ij.with_point_annotations(points(), [ij.PointAnnotation(2)] * 5)
    edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    before = owner.snapshot(); undo = owner.undo_steps; editor.recording = True
    with pytest.raises(ValueError, match="explicit discard"):
        editor.action("bounding_rectangle_selection", "{}")
    assert owner.snapshot().shares_data(before) and not json.loads(editor.workflow)
    editor.action("bounding_rectangle_selection", '{"discard_point_metadata":true}')
    result = owner.snapshot().selection
    assert result.points == [[2, 2], [13, 13]] and owner.undo_steps == undo + 1
    record = json.loads(editor.workflow)[0]
    editor.action("undo", "{}"); assert owner.snapshot().selection == source
    editor.action("redo", "{}"); assert owner.snapshot().selection == result
    path = tmp_path / "bounds.ijx"; editor.action("save_workspace", json.dumps({"path": str(path)}))
    reopened = _desktop.Editor(str(tmp_path / "reopened.json")); reopened.open_file(str(path))
    assert reopened.native_snapshot().selection == result and json.loads(reopened.workflow)[0] == record
    with pytest.raises((ValueError, RuntimeError)):
        reopened.replay_selection_creation(record)
    reopened.select(json.dumps(source.to_dict())); history = reopened.native_document().undo_steps
    request = ij.SelectionCreationRequest(result, ij.SelectionCreationOperation.REPLACE,
                                         ij.SelectionCreationGeometry.NATIVE_PATH_V4)
    reopened.replay_selection_creation(record, rebind_request=request)
    assert reopened.native_snapshot().selection == result and reopened.native_document().undo_steps == history + 1
    reopened.action("undo", "{}"); assert reopened.native_snapshot().selection == source
    np.testing.assert_array_equal(reopened.native_snapshot().dataset.numpy("YX"), before.dataset.numpy("YX"))


def test_physical_imgui_menu_and_loss_controls(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); capture = tmp_path / "ui"
    report = json.loads(editor._bounding_rectangle_ui_case(str(capture)))
    assert json.loads((capture / "case.json").read_bytes()) == report
    assert report["state"] == "observed" and report["context"] == "imgui_python"
    assert report["rectangle"] == [[2, 2], [13, 13]]
    menu = ["menu.Edit", "menu.Edit.Selection", "menu.action.show_bounding_rectangle"]
    choice = ["dialog.discard_point_metadata", "dialog.apply"]
    assert [r["name"] for r in report["controls"]] == menu + ["dialog.cancel"] + menu + choice + [
        "error.ok", "dialog.cancel"] + menu + choice
    assert all(r["control"]["enabled"] for r in report["controls"])
    assert report["record"]["schema"] == "imagejxx.selection-creation-command" and report["record"]["version"] == 4
    assert (capture / "bounds.ijx").is_file()
