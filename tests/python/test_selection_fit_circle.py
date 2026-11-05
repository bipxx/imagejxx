"""Both ImageJ circle-fit branches and native history/physical UI integration."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/fit-circle-imagej154r.json").read_bytes())


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_pinned_circle_fit(case):
    source = ij.Selection(case["kind"], case["points"])
    before = source.to_dict()
    if case["error"]:
        with pytest.raises(ValueError):
            ij.fit_circle_selection(source, 64, 64)
    else:
        result = ij.fit_circle_selection(source, 64, 64)
        x, y, right, bottom = case["bounds"]
        assert result.kind == "ellipse" and result.points == [[x, y], [right, bottom]]
    assert source.to_dict() == before


def points():
    return ij.Selection("multipoint", [(3, 8), (8, 3), (13, 8), (8, 13)])


@pytest.mark.parametrize("area", [False, True])
def test_admission_cancel_frozen_limits_and_metadata(area):
    source = ij.Selection("rectangle", [(3, 3), (13, 13)]) if area else points()
    result = ij.fit_circle_selection(source, 16, 16)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = ij.estimate_fit_circle_selection(source, 16, 16)
    assert ij.fit_circle_selection(source, 16, 16, limits=limits) == result
    limits.max_working_bytes -= 1; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.fit_circle_selection(source, 16, 16, limits=limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    assert ij.fit_circle_selection(source, 16, 16, limits=limits,
        checkpoint=lambda: setattr(limits, "max_vertices", 1)) == result
    token = ij.CancellationToken(); token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.fit_circle_selection(source, 16, 16, cancellation=token)
    def fail():
        raise RuntimeError("owned fit callback")
    with pytest.raises(RuntimeError, match="owned fit callback"):
        ij.fit_circle_selection(source, 16, 16, checkpoint=fail)
    live = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.fit_circle_selection(source, 16, 16, cancellation=live, checkpoint=live.cancel)
    source = ij.Selection(source.kind, source.points, 3,
                          attributes={"opaque": {"note": "preserved"}})
    styled = ij.fit_circle_selection(source, 16, 16)
    assert styled.attributes == source.attributes and styled.width == 3
    assert ij.SelectionEditor(result, (16, 16)).resized(4, (14, 14)).kind == "ellipse"


def test_typed_point_metadata_and_boolean_contract():
    source = ij.with_point_annotations(points(), [ij.PointAnnotation(2)] * 4)
    with pytest.raises(ValueError, match="explicit discard"):
        ij.fit_circle_selection(source, 16, 16)
    assert not ij.fit_circle_selection(source, 16, 16, discard_point_metadata=True).attributes
    for options in [{"width": True}, {"height": False}, {"discard_point_metadata": 1}]:
        with pytest.raises(TypeError):
            ij.fit_circle_selection(points(), **{"width": 16, "height": 16, **options})


def test_path_hole_packed_region_and_empty_area():
    path = ij.Selection.from_path(ij.ImageJCompositePath([
        0,3,3,1,13,3,1,13,13,1,3,13,4, 0,5,5,1,11,5,1,11,11,1,5,11,4]))
    assert ij.fit_circle_selection(path, 16, 16).points == [[3, 3], [12, 12]]
    mask = ij.Selection.from_mask(np.array([[1, 0], [0, 1]], dtype=np.uint8))
    region = ij.SelectionRegion(mask, (2, 2), (3, 3)).as_selection()
    assert ij.fit_circle_selection(region, 16, 16).points == [[3, 3], [5, 5]]
    empty = ij.Selection.from_mask(np.zeros((2, 2), dtype=np.uint8))
    with pytest.raises(ValueError, match="nonempty area"):
        ij.fit_circle_selection(empty, 2, 2)


@pytest.mark.parametrize("area", [False, True])
def test_editor_history_reopen_rebind_and_pixels(tmp_path, area):
    from imagejxx import _desktop
    source = ij.Selection("rectangle", [(3, 3), (13, 13)]) if area else points()
    image = np.arange(256, dtype=np.uint8).reshape(16, 16)
    owner = ij.Document(ij.Dataset(image, axes="YX")); edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner); editor.recording = True
    history = owner.undo_steps; editor.action("fit_circle_selection", "{}"); editor.wait()
    result = owner.snapshot().selection
    assert result.points == ([[2, 2], [13, 13]] if area else [[3, 3], [13, 13]])
    assert owner.undo_steps == history + 1
    record = json.loads(editor.workflow)[0]
    editor.action("undo", "{}"); assert owner.snapshot().selection == source
    editor.action("redo", "{}"); assert owner.snapshot().selection == result
    path = tmp_path / "circle.ijx"; editor.action("save_workspace", json.dumps({"path": str(path)}))
    reopened = _desktop.Editor(str(tmp_path / "reopened.json")); reopened.open_file(str(path))
    assert reopened.native_snapshot().selection == result and json.loads(reopened.workflow)[0] == record
    with pytest.raises((ValueError, RuntimeError)):
        reopened.replay_selection_creation(record)
    reopened.select(json.dumps(source.to_dict())); old = reopened.native_document().undo_steps
    request = ij.SelectionCreationRequest(result, ij.SelectionCreationOperation.REPLACE, ij.SelectionCreationGeometry.NATIVE_PATH_V4)
    reopened.replay_selection_creation(record, rebind_request=request)
    assert reopened.native_snapshot().selection == result and reopened.native_document().undo_steps == old + 1
    reopened.action("undo", "{}"); assert reopened.native_snapshot().selection == source
    np.testing.assert_array_equal(reopened.native_snapshot().dataset.numpy("YX"), image)


@pytest.mark.parametrize("area", [False, True])
def test_actual_menu_both_circle_branches(tmp_path, area):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); capture = tmp_path / "ui"
    method = editor._fit_circle_area_ui_case if area else editor._fit_circle_ui_case
    report = json.loads(method(str(capture)))
    assert json.loads((capture / "case.json").read_bytes()) == report
    assert report["context"] == "imgui_python" and report["state"] == "observed"
    assert report["circle"] == ([[2, 2], [13, 13]] if area else [[3, 3], [13, 13]])
    menu = ["menu.Edit", "menu.Edit.Selection", "menu.action.show_fit_circle"]
    choice = ["dialog.discard_point_metadata", "dialog.apply"]
    names = menu if area else menu + ["dialog.cancel"] + menu + choice + ["error.ok", "dialog.cancel"] + menu + choice
    assert [r["name"] for r in report["controls"]] == names
    assert all(r["control"]["enabled"] for r in report["controls"])
    assert report["record"]["schema"] == "imagejxx.selection-creation-command" and report["record"]["version"] == 4
