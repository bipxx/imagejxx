"""Actual-command interpolation observations and usable editor workflows."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/selection-interpolation-imagej154r.json").read_bytes())
COMPOSITION = json.loads((Path(__file__).parents[1] / "fixtures/interpolated-measurement-imagej154r.json").read_bytes())["case"]


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_actual_command_samples(case):
    source = ij.Selection(case["kind"], case["input"])
    if case["fitted"]:
        fit = ij.SplineFitOptions(); fit.evaluation_points = 47
        source = ij.fit_spline_selection(source, 128, 128, fit)
    before = source.to_dict()
    options = ij.SelectionInterpolationOptions()
    options.interval = case["interval"]; options.smooth = case["smooth"]; options.adjust_to_match = case["adjust"]
    # Explicit subtype state now retains upstream one/two-vertex areas.
    if case["error"] or (case["kind"] in ("line", "polyline") and len(case["samples"]) < 2):
        with pytest.raises((ValueError, RuntimeError)):
            ij.interpolate_selection(source, 128, 128, options)
    else:
        result = ij.interpolate_selection(source, 128, 128, options)
        np.testing.assert_allclose(result.points, case["samples"], atol=1e-5, rtol=1e-7)
        assert result.kind == ("polyline" if case["kind"] in ("line", "polyline") else "polygon" if case["type"] == 2 else "freehand")
        assert ij.selection_polygon_type(result) == {
            2: ij.SelectionPolygonType.POLYGON, 3: ij.SelectionPolygonType.FREEHAND,
            6: ij.SelectionPolygonType.POLYLINE, 7: ij.SelectionPolygonType.FREELINE,
        }[case["type"]]
        assert ij.selection_spline_fit(result) is None
    assert source.to_dict() == before


@pytest.mark.parametrize("adjust", [False, True])
def test_limits_callbacks_topology_and_editable_output(adjust):
    source = ij.Selection("polygon", [(3, 3), (13, 3), (13, 13), (3, 13)])
    options = ij.SelectionInterpolationOptions(); options.interval = 2.7; options.adjust_to_match = adjust
    expected = ij.interpolate_selection(source, 16, 16, options)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = ij.estimate_interpolated_selection(source, 16, 16, options)
    assert ij.interpolate_selection(source, 16, 16, options, limits) == expected
    limits.max_working_bytes -= 1; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.interpolate_selection(source, 16, 16, options, limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    assert ij.interpolate_selection(source, 16, 16, options, limits,
        checkpoint=lambda: setattr(options, "interval", 99)) == expected
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.interpolate_selection(source, 16, 16, cancellation=token, checkpoint=token.cancel)
    assert expected.mask((16, 16))[8, 8]
    opened = ij.interpolate_selection(ij.Selection("polyline", source.points), 16, 16)
    assert not opened.mask((16, 16))[8, 8]
    assert len(ij.SelectionEditor(expected, (16, 16)).handles()) == len(expected.points)
    changed = ij.SelectionEditor(expected, (16, 16)).moved_vertex(1, (7, 2))
    assert changed.points[1] == [7, 2]


@pytest.mark.parametrize("value", [0, .005, float("inf"), float("nan")])
def test_invalid_interval_is_atomic(value):
    source = ij.Selection("polyline", [(3, 3), (13, 3)])
    options = ij.SelectionInterpolationOptions(); options.interval = value
    with pytest.raises((ValueError, RuntimeError)):
        ij.interpolate_selection(source, 16, 16, options)


def test_strict_options_and_bounded_work():
    options = ij.SelectionInterpolationOptions()
    for field, value in [("interval", True), ("smooth", 1), ("adjust_to_match", "yes")]:
        with pytest.raises(TypeError):
            setattr(options, field, value)
    options.interval = .01
    with pytest.raises((ValueError, RuntimeError)):
        ij.interpolate_selection(ij.Selection("polyline", [(0, 0), (1e9, 0)]), 16, 16, options)


@pytest.mark.parametrize("opened", [False, True])
@pytest.mark.parametrize("configured", [False, True])
def test_physical_workflow_in_python_host(tmp_path, opened, configured):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); directory = tmp_path / "interpolate-ui"
    report = json.loads(editor._interpolation_ui_case(str(directory), opened, configured))
    assert report == json.loads((directory / "case.json").read_bytes())
    kind = "polyline" if opened else "freehand"
    case = next(c for c in REFERENCE["cases"] if c["id"] == f"shape0-{kind}-{('2.7-true-false' if opened else '2.7-true-true') if configured else '1.0-false-false'}-false")
    np.testing.assert_allclose(report["samples"], case["samples"], atol=1e-5, rtol=1e-7)
    assert report["context"] == "imgui_python" and report["state"] == "observed"
    assert report["record"]["version"] == 4
    names = [x["name"] for x in report["controls"]]
    assert "menu.action.show_interpolate_selection" in names and "dialog.cancel" in names and "error.ok" in names
    if configured:
        assert all("dialog." + name in names for name in ("interval", "smooth", "adjust_to_match"))


def test_desktop_edit_history_and_invalid_options(tmp_path):
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.arange(256, dtype=np.uint8).reshape(16, 16), axes="YX"))
    source = ij.Selection("polyline", [(3, 3), (13, 3), (13, 13)])
    edit = owner.begin_edit(); edit.select(source); owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    editor.action("interpolate_selection", json.dumps({"interval": 2.7, "adjust_to_match": True})); editor.wait()
    result = owner.snapshot().selection
    editor.update_settings(json.dumps({"measurements": ["Length", "Bounds", "count"]}), False)
    editor.action("measure", "{}"); editor.wait()
    np.testing.assert_allclose(result.points, COMPOSITION["samples"], atol=1e-5, rtol=1e-7)
    assert ij.selection_polygon_type(result) == ij.SelectionPolygonType.FREELINE
    assert json.loads(editor.results)[-1]["Length"] == pytest.approx(COMPOSITION["columns"]["Length"], abs=1e-5)
    before = owner.undo_steps
    editor.action("move_selection_vertex", json.dumps({"index": 1, "x": 7, "y": 2}))
    assert owner.snapshot().selection.points[1] == [7, 2] and owner.undo_steps == before
    editor.action("undo", "{}"); assert owner.snapshot().selection == source
    editor.action("redo", "{}"); assert owner.snapshot().selection.points[1] == [7, 2]
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), np.arange(256, dtype=np.uint8).reshape(16, 16))
    for values in [{"interval": True}, {"smooth": 1}, {"adjust_to_match": "yes"}, {"unexpected": 1}, {"interval": 0}]:
        old = owner.snapshot().selection
        with pytest.raises((ValueError, RuntimeError)):
            editor.action("interpolate_selection", json.dumps(values)); editor.wait()
        assert owner.snapshot().selection == old
        editor.clear_error()
