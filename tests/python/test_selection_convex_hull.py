"""Pinned ImageJ geometry plus native editor conversion/history/replay."""
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/convex-hull-imagej154r.json").read_bytes())


@pytest.mark.parametrize("kind", ["multipoint", "polygon", "polyline", "freehand"])
@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda case: case["id"])
def test_pinned_float_polygon_hull(kind, case):
    source = ij.Selection(kind, case["input"])
    before = source.to_dict()
    if len(case.get("hull", [])) < 3:
        with pytest.raises(ValueError):
            ij.convex_hull_selection(source, 128, 128)
    else:
        result = ij.convex_hull_selection(source, 128, 128)
        assert result.kind == "polygon" and result.points == case["hull"]
    assert source.to_dict() == before


def test_rectangle_and_generic_style_provenance():
    source = ij.Selection("rectangle", [(2, 3), (12, 13)], 2,
                          attributes={"future": {"note": "preserved"}})
    result = ij.convex_hull_selection(source, 16, 16)
    assert result.points == [[2, 3], [2, 13], [12, 13], [12, 3]]
    assert result.width == 2 and result.attributes == source.attributes


def fixture():
    return ij.Selection("multipoint", [(2, 2), (12, 2), (8, 8), (12, 12), (2, 12)])


@pytest.mark.parametrize("annotation", ["positions", "presentation"])
def test_point_metadata_requires_explicit_discard(annotation):
    source = fixture()
    if annotation == "positions":
        source = ij.with_point_annotations(source, [ij.PointAnnotation(2)] * 5)
    else:
        source = ij.with_point_style(source, ij.PointStyle())
    before = source.to_dict()
    with pytest.raises(ValueError, match="explicit discard"):
        ij.convex_hull_selection(source, 16, 16)
    result = ij.convex_hull_selection(source, 16, 16, discard_point_metadata=True)
    assert result.points == [[2, 2], [2, 12], [12, 12], [12, 2]]
    assert "imagejxx.point_annotations" not in result.attributes
    assert "imagejxx.point_style" not in result.attributes
    assert source.to_dict() == before


def test_exact_short_cap_and_frozen_limits():
    source = fixture(); limits = ij.SelectionEditLimits()
    exact = ij.estimate_convex_hull_selection(source, 16, 16, limits)
    limits.max_working_bytes = exact
    assert len(ij.convex_hull_selection(source, 16, 16, limits=limits).points) == 4
    limits.max_working_bytes = exact - 1
    callbacks = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.convex_hull_selection(source, 16, 16, limits=limits,
            progress=lambda *x: callbacks.append(x), checkpoint=lambda: callbacks.append("checkpoint"))
    assert callbacks == []
    limits.max_working_bytes = exact
    result = ij.convex_hull_selection(source, 16, 16, limits=limits,
        progress=lambda *x: setattr(limits, "max_vertices", 1))
    assert result.points == [[2, 2], [2, 12], [12, 12], [12, 2]]


@pytest.mark.parametrize("kind", ["line", "ellipse"])
def test_unsupported_geometry_is_explicit(kind):
    source = ij.Selection(kind, [(0, 0), (4, 4)])
    with pytest.raises(ValueError, match="requires vertex geometry"):
        ij.convex_hull_selection(source, 16, 16)


def test_cancellation_and_callback_failure_leave_input_unchanged():
    source = fixture(); before = source.to_dict(); token = ij.CancellationToken(); token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.convex_hull_selection(source, 16, 16, cancellation=token)
    def fail(*args):
        raise RuntimeError("owned callback failure")
    with pytest.raises(RuntimeError, match="owned callback failure"):
        ij.convex_hull_selection(source, 16, 16, checkpoint=fail)
    assert source.to_dict() == before


@pytest.mark.parametrize("options", [{"width": True}, {"height": False}, {"discard_point_metadata": 1}])
def test_boolean_contract(options):
    values = {"width": 16, "height": 16, **options}
    with pytest.raises(TypeError):
        ij.convex_hull_selection(fixture(), **values)


@pytest.mark.parametrize("point", [[float("nan"), 1], [1e300, 1]])
def test_nonfinite_or_float32_overflow_refuses_before_callbacks(point):
    source = ij.Selection("multipoint", [point, [2, 3], [4, 2]])
    events = []
    with pytest.raises(ValueError):
        ij.convex_hull_selection(source, 16, 16, checkpoint=lambda: events.append(1))
    assert not events


def test_editor_conversion_history_session_and_explicit_rebind(tmp_path):
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.zeros((16, 16), np.uint8), axes="YX"))
    source = ij.with_point_annotations(fixture(), [ij.PointAnnotation(2)] * 5)
    edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    before = owner.snapshot(); history = owner.undo_steps; editor.recording = True
    with pytest.raises(ValueError, match="explicit discard"):
        editor.action("convex_hull_selection", "{}")
    assert owner.snapshot().shares_data(before) and not json.loads(editor.workflow)
    editor.action("convex_hull_selection", json.dumps({"discard_point_metadata": True}))
    assert owner.snapshot().selection.points == [[2, 2], [2, 12], [12, 12], [12, 2]]
    assert owner.undo_steps == history + 1
    recorded = json.loads(editor.workflow)[-1]
    assert recorded["schema"] == "imagejxx.selection-creation-command" and recorded["version"] == 4
    editor.action("undo", "{}"); assert owner.snapshot().selection == source
    editor.action("redo", "{}"); hull = owner.snapshot().selection
    geometry = ij.SelectionEditor(hull, (16, 16))
    assert geometry.moved_vertex(0, (3, 3)).points == [[3, 3], [2, 12], [12, 12], [12, 2]]
    assert hull.points == [[2, 2], [2, 12], [12, 12], [12, 2]]
    path = tmp_path / "hull.ijx"
    editor.action("save_workspace", json.dumps({"path": str(path)}))
    reopened = _desktop.Editor(str(tmp_path / "reopened.json")); reopened.open_file(str(path))
    assert reopened.native_snapshot().selection == hull
    assert json.loads(reopened.workflow)[-1] == recorded
    with pytest.raises((ValueError, RuntimeError)):
        reopened.replay_selection_creation(recorded)
    reopened.select(json.dumps(source.to_dict()))
    previous = reopened.native_document().undo_steps
    request = ij.SelectionCreationRequest(hull, ij.SelectionCreationOperation.REPLACE,
                                         ij.SelectionCreationGeometry.NATIVE_PATH_V4)
    reopened.replay_selection_creation(recorded, rebind_request=request)
    assert reopened.native_snapshot().selection == hull
    assert reopened.native_document().undo_steps == previous + 1
    reopened.action("undo", "{}"); assert reopened.native_snapshot().selection == source
    np.testing.assert_array_equal(reopened.native_snapshot().dataset.numpy("YX"), np.zeros((16, 16), np.uint8))


def test_actual_imgui_menu_discard_cancel_and_apply(tmp_path):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json"))
    capture = tmp_path / "ui"
    result = json.loads(editor._convex_hull_ui_case(str(capture)))
    # Read the persisted report independently of the binding's returned value.
    assert json.loads((capture / "case.json").read_bytes()) == result
    assert result["context"] == "imgui_python" and result["state"] == "observed"
    assert result["hull"] == [[2, 2], [2, 12], [12, 12], [12, 2]]
    names = [item["name"] for item in result["controls"]]
    menu = ["menu.Edit", "menu.Edit.Selection", "menu.action.show_convex_hull"]
    assert names == menu + ["dialog.cancel"] + menu + [
        "dialog.discard_point_metadata", "dialog.apply"]
    assert all(item["control"]["enabled"] for item in result["controls"])
    assert result["record"]["schema"] == "imagejxx.selection-creation-command"
    assert result["record"]["version"] == 4
    assert (capture / "hull.ijx").is_file()
