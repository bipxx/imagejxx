"""Pinned pixel moments, sparse point masks and actual native ellipse workflows."""
import json
import math
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/fit-ellipse-imagej154r.json").read_bytes())


def parameters(result):
    p = np.asarray(result.points); u, v = p[1] - p[0], p[2] - p[0]
    angle = math.degrees(math.atan2(-u[1], u[0])) % 180
    return p[0], 2 * np.linalg.norm(u), 2 * np.linalg.norm(v), angle


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_pinned_ellipse_moments_and_params(case):
    source = ij.Selection(case["kind"], case["points"]); before = source.to_dict()
    if case["error"]:
        with pytest.raises(ValueError, match="line selections"):
            ij.fit_ellipse_selection(source, 64, 64)
    else:
        result = ij.fit_ellipse_selection(source, 64, 64)
        assert result.kind == "rotated_ellipse"
        center, major, minor, angle = parameters(result)
        expected = case["moments"]
        np.testing.assert_allclose([*center, major, minor], expected[:4], atol=1e-7, rtol=1e-7)
        difference = abs(angle - expected[4]) % 180
        assert min(difference, 180 - difference) < 2e-5
        endpoints = np.array([center - (np.asarray(result.points[1]) - center), result.points[1]]).ravel()
        np.testing.assert_allclose(endpoints, case["params"][:4], atol=1e-5, rtol=0)
        assert minor / major == pytest.approx(case["params"][4], abs=1e-7, rel=1e-7)
    assert source.to_dict() == before


def points():
    return ij.Selection("multipoint", [(3, 8), (8, 3), (13, 8), (8, 13)])


@pytest.mark.parametrize("area", [False, True])
def test_resource_style_frozen_limits_and_cancellation(area):
    source = ij.Selection("rectangle", [(3, 3), (13, 13)]) if area else points()
    before = source.to_dict(); result = ij.fit_ellipse_selection(source, 16, 16)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = ij.estimate_fit_ellipse_selection(source, 16, 16)
    assert ij.fit_ellipse_selection(source, 16, 16, limits=limits) == result
    limits.max_working_bytes -= 1; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.fit_ellipse_selection(source, 16, 16, limits=limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    assert ij.fit_ellipse_selection(source, 16, 16, limits=limits, checkpoint=lambda: setattr(limits, "max_vertices", 1)) == result
    live = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.fit_ellipse_selection(source, 16, 16, cancellation=live, checkpoint=live.cancel)
    def fail():
        raise RuntimeError("owned ellipse callback")
    with pytest.raises(RuntimeError, match="owned ellipse callback"):
        ij.fit_ellipse_selection(source, 16, 16, checkpoint=fail)
    styled = ij.Selection(source.kind, source.points, 7, attributes={"opaque": {"future": "retained"}})
    output = ij.fit_ellipse_selection(styled, 16, 16)
    assert output.width == 7 and output.attributes == styled.attributes and output.points == result.points
    assert ij.SelectionEditor(result, (16, 16)).moved((1, 2)).points[0] == [result.points[0][0] + 1, result.points[0][1] + 2]
    assert source.to_dict() == before


def test_metadata_boolean_output_caps_and_native_holes():
    source = ij.with_point_annotations(points(), [ij.PointAnnotation(2)] * 4)
    with pytest.raises(ValueError, match="explicit discard"):
        ij.fit_ellipse_selection(source, 16, 16)
    assert not ij.fit_ellipse_selection(source, 16, 16, discard_point_metadata=True).attributes
    for options in [{"width": True}, {"height": False}, {"discard_point_metadata": 1}]:
        with pytest.raises(TypeError):
            ij.fit_ellipse_selection(points(), **{"width": 16, "height": 16, **options})
    limits = ij.SelectionEditLimits(); limits.max_vertices = 2
    with pytest.raises((ValueError, RuntimeError)):
        ij.fit_ellipse_selection(ij.Selection("rectangle", [(3, 3), (13, 13)]), 16, 16, limits=limits)
    path = ij.Selection.from_path(ij.ImageJCompositePath([0,3,3,1,13,3,1,13,13,1,3,13,4,0,5,5,1,11,5,1,11,11,1,5,11,4]))
    output = ij.fit_ellipse_selection(path, 16, 16)
    np.testing.assert_allclose(parameters(output)[0], [8, 8])
    _, major, minor, _ = parameters(output)
    assert math.pi * major * minor / 4 == pytest.approx(64)
    empty = ij.Selection.from_mask(np.zeros((2, 2), dtype=np.uint8))
    with pytest.raises(ValueError, match="nonempty area"):
        ij.fit_ellipse_selection(empty, 2, 2)


def test_rotated_ellipse_candidate_is_explicit_v4_and_validated():
    owner = ij.Document(ij.Dataset(np.zeros((16, 16), np.uint8), axes="YX"))
    ellipse = ij.fit_ellipse_selection(ij.Selection("rectangle", [(3, 3), (13, 13)]), 16, 16)
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(owner.snapshot(), ij.SelectionCreationRequest(ellipse))
    request = ij.SelectionCreationRequest(ellipse, ij.SelectionCreationOperation.REPLACE, ij.SelectionCreationGeometry.NATIVE_PATH_V4)
    command = ij.resolve_selection_creation_command(owner.snapshot(), request)
    record = command.record()
    ij.validate_selection_creation_command_record(record)
    malformed = json.loads(json.dumps(record)); malformed["candidate"]["points"] = [[8, 8], [8, 8], [8, 9]]
    with pytest.raises(ValueError):
        ij.validate_selection_creation_command_record(malformed)
    assert owner.undo_steps == 0 and owner.snapshot().selection is None


def test_large_canvas_fast_rectangle_and_sparse_points_fit_declared_budget():
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = 192 * 1024**2
    rectangle = ij.Selection("rectangle", [(3, 3), (13, 13)])
    assert ij.estimate_fit_ellipse_selection(rectangle, 4096, 4096, limits) < 1024**2
    assert ij.fit_ellipse_selection(rectangle, 4096, 4096, limits=limits).points[0] == [8, 8]
    cloud = ij.Selection("multipoint", [(x, y) for y in range(3, 33) for x in range(3, 33)])
    assert ij.estimate_fit_ellipse_selection(cloud, 4096, 4096, limits) <= limits.max_working_bytes
    output = ij.fit_ellipse_selection(cloud, 4096, 4096, limits=limits)
    center, major, minor, _ = parameters(output)
    np.testing.assert_allclose(center, [18, 18])
    assert math.pi * major * minor / 4 == pytest.approx(900)


@pytest.mark.parametrize("area", [False, True])
def test_history_workspace_and_explicit_rebind(tmp_path, area):
    from imagejxx import _desktop
    source = ij.Selection("rectangle", [(3, 3), (13, 13)]) if area else points()
    image = np.arange(256, dtype=np.uint8).reshape(16, 16)
    owner = ij.Document(ij.Dataset(image, axes="YX")); edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner); editor.recording = True
    previous = owner.undo_steps; editor.action("fit_ellipse_selection", "{}"); editor.wait()
    result = owner.snapshot().selection
    assert result.kind == "rotated_ellipse" and owner.undo_steps == previous + 1
    record = json.loads(editor.workflow)[0]
    editor.action("undo", "{}"); assert owner.snapshot().selection == source
    editor.action("redo", "{}"); assert owner.snapshot().selection == result
    path = tmp_path / "ellipse.ijx"; editor.action("save_workspace", json.dumps({"path": str(path)}))
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
def test_actual_menu_both_ellipse_branches(tmp_path, area):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); capture = tmp_path / "ui"
    method = editor._fit_ellipse_area_ui_case if area else editor._fit_ellipse_ui_case
    report = json.loads(method(str(capture)))
    assert json.loads((capture / "case.json").read_bytes()) == report
    assert report["context"] == "imgui_python" and report["state"] == "observed"
    assert report["ellipse"][0] == ([8, 8] if area else [8.5, 8.5])
    menu = ["menu.Edit", "menu.Edit.Selection", "menu.action.show_fit_ellipse"]
    choice = ["dialog.discard_point_metadata", "dialog.apply"]
    names = menu if area else menu + ["dialog.cancel"] + menu + choice + ["error.ok", "dialog.cancel"] + menu + choice
    assert [r["name"] for r in report["controls"]] == names
    assert all(r["control"]["enabled"] for r in report["controls"])
    assert report["record"]["schema"] == "imagejxx.selection-creation-command" and report["record"]["version"] == 4
