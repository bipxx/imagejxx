"""Pinned spline modes, editable controls and both-host workflow integration."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/selection-spline-imagej154r.json").read_bytes())


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_pinned_spline_modes(case):
    source = ij.Selection(case["kind"], case["input"]); before = source.to_dict()
    options = ij.SplineFitOptions(); options.evaluation_points = case["evaluation_points"]
    if case["mode"] == "straighten":
        options.mode = ij.SplineFitMode.STRAIGHTEN
    if case["error"]:
        with pytest.raises((ValueError, RuntimeError)):
            ij.fit_spline_selection(source, 128, 128, options)
    else:
        result = ij.fit_spline_selection(source, 128, 128, options)
        if case["mode"] == "refit":
            result = ij.fit_spline_selection(result, 128, 128, options)
        state = ij.selection_spline_fit(result)
        assert state is not None
        if case["mode"] != "remove":
            np.testing.assert_allclose(state.controls.points, case["controls"], atol=1e-5, rtol=1e-7)
        if case["mode"] == "remove":
            result = ij.remove_spline_fit(result, 128, 128)
            assert ij.selection_spline_fit(result) is None
        np.testing.assert_allclose(result.points, case["samples"], atol=1e-5, rtol=1e-7)
    assert source.to_dict() == before


@pytest.mark.parametrize("straighten", [False, True])
def test_budget_frozen_controls_and_live_cancel(straighten):
    source = ij.Selection("polygon", [(3, 3), (13, 3), (13, 13), (3, 13)])
    options = ij.SplineFitOptions()
    if straighten:
        options.mode = ij.SplineFitMode.STRAIGHTEN
    output = ij.fit_spline_selection(source, 16, 16, options)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = ij.estimate_spline_selection(source, 16, 16, options)
    assert ij.fit_spline_selection(source, 16, 16, options, limits) == output
    limits.max_working_bytes -= 1; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.fit_spline_selection(source, 16, 16, options, limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    assert ij.fit_spline_selection(source, 16, 16, options, limits,
        checkpoint=lambda: setattr(options, "evaluation_points", 7)) == output
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.fit_spline_selection(source, 16, 16, cancellation=token, checkpoint=token.cancel)


def test_open_closed_geometry_controls_and_corrupt_state():
    vertices = [(3, 3), (13, 3), (13, 13), (3, 13)]
    closed = ij.fit_spline_selection(ij.Selection("polygon", vertices), 16, 16)
    opened = ij.fit_spline_selection(ij.Selection("polyline", vertices), 16, 16)
    assert closed.mask((16, 16))[8, 8] != 0 and opened.mask((16, 16))[8, 8] == 0
    state = ij.selection_spline_fit(opened)
    controls = ij.SelectionEditor(state.controls, (16, 16)).moved_vertex(1, (14, 4))
    refitted = ij.fit_spline_selection(controls, 16, 16, state.options)
    assert ij.selection_spline_fit(refitted).controls.points[1] == [14, 4]
    assert ij.remove_spline_fit(refitted, 16, 16).points == controls.points
    ij.validate_selection_spline_fit(refitted)
    corrupted = refitted.to_dict(); corrupted["points"][4][0] += 3
    with pytest.raises(ValueError, match="differ"):
        ij.validate_selection_spline_fit(ij.Selection.from_dict(corrupted))
    assert ij.Selection.from_dict(refitted.to_dict()) == refitted


def test_zoom_explicit_count_and_option_validation():
    source = ij.Selection("polyline", [(0, 0), (200, 0), (400, 200)])
    options = ij.SplineFitOptions(); options.magnification = .25
    assert len(ij.fit_spline_selection(source, 512, 512, options).points) == 100
    options.magnification = 1; options.evaluation_points = 7
    assert len(ij.fit_spline_selection(source, 512, 512, options).points) == 7
    options.magnification = .5
    with pytest.raises(ValueError, match="mutually exclusive"):
        ij.fit_spline_selection(source, 512, 512, options)
    for field, value in [("magnification", True), ("evaluation_points", False)]:
        with pytest.raises(TypeError):
            setattr(options, field, value)


def test_editor_exposes_knots_and_refits_vertex_mutations():
    source = ij.Selection("polyline", [(3, 3), (13, 3), (13, 13), (3, 13)])
    fitted = ij.fit_spline_selection(source, 16, 16)
    editor = ij.SelectionEditor(fitted, (16, 16))
    handles = editor.handles()
    assert len(handles) == 4
    assert [list(h.position) for h in handles] == source.points
    changed = editor.moved_vertex(1, (14, 4))
    assert ij.selection_spline_fit(changed).controls.points[1] == [14, 4]
    assert len(changed.points) == len(fitted.points)
    inserted = ij.SelectionEditor(changed, (16, 16)).inserted_vertex(2, (14, 8))
    assert len(ij.selection_spline_fit(inserted).controls.points) == 5
    removed = ij.SelectionEditor(inserted, (16, 16)).removed_vertex(2)
    assert ij.selection_spline_fit(removed).controls == ij.selection_spline_fit(changed).controls
    moved = editor.moved((1, 2))
    np.testing.assert_allclose(ij.selection_spline_fit(moved).controls.points, np.asarray(source.points) + [1, 2])
    ij.validate_selection_spline_fit(moved)
    assert editor.moved((1, 2), limits=ij.SelectionEditLimits()) == moved
    rotated = editor.rotated(30, limits=ij.SelectionEditLimits())
    ij.validate_selection_spline_fit(rotated)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = 20000
    with pytest.raises((ValueError, RuntimeError)):
        editor.moved_vertex(1, (14, 4), limits=limits)
    box = ij.bounding_rectangle_selection(fitted, 16, 16)
    assert ij.selection_spline_fit(box) is None


@pytest.mark.parametrize("kind,vertices", [("polyline", [(3, 8), (8, 8), (13, 8)]), ("polygon", [(3, 8), (8, 8), (13, 8)])])
def test_spline_and_removed_open_or_zero_area_geometry_are_explicit_v4(kind, vertices):
    owner = ij.Document(ij.Dataset(np.zeros((16, 16), np.uint8), axes="YX"))
    fitted = ij.fit_spline_selection(ij.Selection(kind, vertices), 16, 16)
    for result in [fitted, ij.remove_spline_fit(fitted, 16, 16)]:
        if ij.selection_spline_fit(result) is not None or kind == "polyline":
            with pytest.raises(ValueError):
                ij.resolve_selection_creation_command(owner.snapshot(), ij.SelectionCreationRequest(result))
        request = ij.SelectionCreationRequest(result, ij.SelectionCreationOperation.REPLACE, ij.SelectionCreationGeometry.NATIVE_PATH_V4)
        command = ij.resolve_selection_creation_command(owner.snapshot(), request)
        ij.validate_selection_creation_command_record(command.record())
        owner.commit(ij.prepare_selection_creation(command))
        assert owner.snapshot().selection == result
        owner.undo()


@pytest.mark.parametrize("opened", [False, True])
@pytest.mark.parametrize("straighten", [False, True])
def test_actual_spline_workflow_in_python_host(tmp_path, opened, straighten):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); directory = tmp_path / "spline-ui"
    report = json.loads(editor._spline_ui_case(str(directory), opened, straighten))
    assert json.loads((directory / "case.json").read_bytes()) == report
    assert report["context"] == "imgui_python" and report["state"] == "observed"
    assert report["sample_count"] == ((33 if opened else 45) if straighten else 100)
    np.testing.assert_allclose(report["edited_controls"][1], report["drag_target"], atol=1e-5)
    np.testing.assert_allclose(report["drag_target"], [14, 4], atol=.05)
    assert report["record"]["version"] == 4
    names = [x["name"] for x in report["controls"]]
    assert "menu.action.show_fit_spline" in names and "menu.action.remove_spline_fit" in names
    assert "error.ok" in names and "dialog.cancel" in names
    if straighten:
        assert "dialog.mode" in names
    assert all(x["control"]["enabled"] for x in report["controls"])


@pytest.mark.parametrize("opened", [False, True])
def test_desktop_spline_api_history_workspace_and_settings(tmp_path, opened):
    from imagejxx import _desktop
    source = ij.Selection("polyline" if opened else "polygon", [(3, 3), (13, 3), (13, 13), (3, 13)])
    image = np.arange(256, dtype=np.uint8).reshape(16, 16)
    owner = ij.Document(ij.Dataset(image, axes="YX")); edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner); editor.recording = True
    before = owner.undo_steps
    editor.action("fit_spline_selection", json.dumps({"evaluation_points": 47})); editor.wait()
    fitted = owner.snapshot().selection; assert len(fitted.points) == 47
    assert ij.selection_spline_fit(fitted).controls == source
    editor.action("show_selection_properties", "{}")
    changed_points = [[3, 3], [14, 4], [13, 13], [3, 13]]
    editor.action("selection_properties_draft", json.dumps({"points": changed_points}))
    editor.action("apply_selection_properties", "{}")
    fitted = owner.snapshot().selection
    expected_controls = ij.Selection(source.kind, changed_points)
    assert ij.selection_spline_fit(fitted).controls == expected_controls
    assert len(fitted.points) == 47
    editor.action("remove_spline_fit", "{}"); editor.wait(); assert owner.snapshot().selection == expected_controls
    editor.action("undo", "{}"); assert owner.snapshot().selection == fitted
    editor.action("redo", "{}"); assert owner.snapshot().selection == expected_controls
    assert owner.undo_steps == before + 3
    editor.action("undo", "{}"); path = tmp_path / "spline.ijx"
    editor.action("save_workspace", json.dumps({"path": str(path)}))
    reopened = _desktop.Editor(str(tmp_path / "reopened.json")); reopened.open_file(str(path))
    assert reopened.native_snapshot().selection == fitted
    np.testing.assert_array_equal(reopened.native_snapshot().dataset.numpy("YX"), image)
    for values in [{"mode": "unknown"}, {"evaluation_points": True}, {"magnification": False}, {"unknown": 1}]:
        old = reopened.native_snapshot()
        with pytest.raises((ValueError, RuntimeError)):
            reopened.action("fit_spline_selection", json.dumps(values)); reopened.wait()
        assert reopened.native_snapshot().selection == old.selection
        reopened.clear_error()


def test_freehand_knot_edits_do_not_repeat_command_trimming():
    source = ij.Selection("freehand", [(2.2, 3.7), (12.8, 3.1), (5.5, 13.2)])
    fitted = ij.fit_spline_selection(source, 16, 16)
    added = ij.SelectionEditor(fitted, (16, 16)).inserted_vertex(1, (7, 2))
    controls = ij.selection_spline_fit(added).controls
    assert controls.kind == "freehand" and controls.points == [source.points[0], [7, 2], *source.points[1:]]
    changed = ij.SelectionEditor(added, (16, 16)).moved_vertex(1, (7, 3))
    assert ij.selection_spline_fit(changed).controls.points[1] == [7, 3]
    ij.validate_selection_spline_fit(changed)


def test_freehand_numeric_properties_keep_all_edited_controls(tmp_path):
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.zeros((16, 16), dtype=np.uint8), axes="YX"))
    source = ij.Selection("freehand", [(2.2, 3.7), (12.8, 3.1), (5.5, 13.2)])
    edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    editor.action("fit_spline_selection", "{}"); editor.wait()
    fitted = owner.snapshot().selection
    controls = [source.points[0], [7, 2], *source.points[1:]]
    editor.action("show_selection_properties", "{}")
    editor.action("selection_properties_draft", json.dumps({"points": controls}))
    editor.action("apply_selection_properties", "{}")
    assert ij.selection_spline_fit(owner.snapshot().selection).controls.points == controls
    editor.action("undo", "{}"); assert owner.snapshot().selection == fitted
