"""Repeated-command reference carrier and point no-op consumer contracts."""
import json
from pathlib import Path

import imagejxx as ij
import pytest


REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/interpolation-repeat-imagej154r.json").read_bytes())
POINT_CASES = [case for case in REFERENCE["cases"] if case["kind"] in ("point", "multipoint")]


def annotated(multiple):
    points = ij.Selection("multipoint" if multiple else "point", [[4.5, 6.25], [13, 17]] if multiple else [[4.5, 6.25]])
    points = ij.with_point_annotations(points, [ij.PointAnnotation(7, ij.PlanePosition(0, 0, 1)) for _ in points.points])
    style = ij.PointStyle(); style.size = 5
    return ij.with_point_style(points.with_attributes({**points.attributes, "opaque": [True, "retained"]}), style)


@pytest.mark.parametrize("case", POINT_CASES, ids=lambda case: case["id"])
def test_actual_two_command_point_noop(case):
    source = ij.Selection(case["kind"], case["source"]["samples"])
    options = ij.SelectionInterpolationOptions()
    options.interval = case["interval"]; options.smooth = case["smooth"]; options.adjust_to_match = case["adjust"]
    for step in case["steps"]:
        assert not step["error"] and step["same_source"]
        result = ij.interpolate_selection(source, 40, 40, options)
        assert result == source and result.points == step["result"]["samples"]
        source = result


@pytest.mark.parametrize("multiple", [False, True])
@pytest.mark.parametrize("interval", [0, float("nan"), float("inf")])
def test_point_noop_precedes_interval_validation_and_preserves_all_metadata(multiple, interval):
    source = annotated(multiple)
    options = ij.SelectionInterpolationOptions(); options.interval = interval; options.smooth = True
    assert ij.interpolate_selection(source, 40, 40, options) == source


@pytest.mark.parametrize("multiple", [False, True])
def test_point_noop_admission_freeze_and_cancellation(multiple):
    source = annotated(multiple); expected = source.with_attributes(source.attributes)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = ij.estimate_interpolated_selection(source, 40, 40)
    assert ij.interpolate_selection(source, 40, 40, limits=limits) == expected
    limits.max_working_bytes -= 1; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.interpolate_selection(source, 40, 40, limits=limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    detached = source.points
    assert ij.interpolate_selection(source, 40, 40, limits=limits,
        checkpoint=detached.clear) == expected
    assert source == expected  # The Python getter returns a detached vector.
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.interpolate_selection(expected, 40, 40, cancellation=token, checkpoint=token.cancel)


@pytest.mark.parametrize("multiple", [False, True])
def test_physical_point_noop_python_host(tmp_path, multiple):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); directory = tmp_path / "point-noop"
    report = json.loads(editor._interpolation_point_noop_ui_case(str(directory), multiple))
    assert report == json.loads((directory / "case.json").read_bytes())
    assert report["context"] == "imgui_python" and report["same_data"]
    assert report["undo_steps"] == report["record_count"] == report["additional_plane_reads"] == 0
    assert len(report["controls"]) == 6
