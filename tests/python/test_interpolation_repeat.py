"""Repeated-command reference carrier and point no-op consumer contracts."""
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest


REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/interpolation-repeat-imagej154r.json").read_bytes())
THRESHOLD_REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/interpolation-threshold-imagej154r.json").read_bytes())
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


TYPES = {2: ij.SelectionPolygonType.POLYGON, 3: ij.SelectionPolygonType.FREEHAND,
         4: ij.SelectionPolygonType.TRACED, 6: ij.SelectionPolygonType.POLYLINE,
         7: ij.SelectionPolygonType.FREELINE}
DIRECT_CASES = [case for case in REFERENCE["cases"] + THRESHOLD_REFERENCE["cases"]
                if case["kind"] not in ("point", "multipoint", "compound")
                and not any(step["error"] for step in case["steps"])]


def repeat_source(case):
    kind = case["kind"]
    native_kind = "polyline" if kind in ("polyline", "freeline") else "freehand" if kind in ("freehand", "traced") else "line" if kind == "line" else "polygon"
    points = case["source"]["samples"]
    source = ij.Selection(native_kind, points)
    if kind == "shape":
        commands = [value for i, p in enumerate(points) for value in [0 if i == 0 else 1, *p]] + [4]
        return ij.Selection.from_path(ij.ImageJCompositePath(commands))
    if kind in ("freeline", "traced"):
        source = ij.with_selection_polygon_type(source, TYPES[case["source"]["type"]])
    return source


@pytest.mark.parametrize("case", DIRECT_CASES, ids=lambda case: case["id"])
def test_actual_repeated_arrays_subtypes_and_area_membership(case):
    source = repeat_source(case)
    options = ij.SelectionInterpolationOptions()
    options.interval = case["interval"]; options.smooth = case["smooth"]; options.adjust_to_match = case["adjust"]
    options.magnification = case["magnification"] or 1
    for step in case["steps"]:
        result = ij.interpolate_selection(source, 40, 40, options)
        expected = step["result"]
        np.testing.assert_allclose(result.points, expected["samples"], atol=1e-5, rtol=1e-7)
        assert ij.selection_polygon_type(result) == TYPES[expected["type"]]
        if expected["type"] in (2, 3, 4):
            mask = np.asarray([[v == "1" for v in row] for row in expected["mask"]])
            np.testing.assert_array_equal(result.mask((40, 40)) != 0, mask)
        source = result


@pytest.mark.parametrize("kind,subtype", [("polyline", "FREELINE"), ("freehand", "TRACED")])
def test_subtype_edit_wire_session_and_imagej_roi_roundtrips(kind, subtype):
    from test_path_selection import pack, unpack
    source = ij.with_selection_polygon_type(ij.Selection(kind, [[3, 3], [13, 3], [13, 13], [3, 13]]), getattr(ij.SelectionPolygonType, subtype))
    edited = ij.SelectionEditor(source, (40, 40)).moved_vertex(1, [14, 4])
    assert ij.selection_polygon_type(edited) == ij.selection_polygon_type(source)
    assert ij.Selection.from_dict(edited.to_dict()) == edited
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), np.uint8), axes="YX"))
    with pytest.raises(ValueError):
        ij.resolve_selection_creation_command(owner.snapshot(), ij.SelectionCreationRequest(edited))
    command = ij.resolve_selection_creation_command(owner.snapshot(), ij.SelectionCreationRequest(
        edited, ij.SelectionCreationOperation.REPLACE, ij.SelectionCreationGeometry.NATIVE_PATH_V4))
    ij.validate_selection_creation_command_record(command.record())
    owner.commit(ij.prepare_selection_creation(command))
    blob = ij.encode_session(ij.SessionCapture([owner.snapshot()]))
    assert blob.startswith(b"IMAGEJXX5\n")
    assert ij.decode_session(blob).documents[0].snapshot().selection == edited
    for version in (3, 4):
        root = unpack(blob); root["version"] = version
        with pytest.raises(ValueError):
            ij.decode_session(pack(root, version))
    wire = ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI("subtype", edited)))
    restored = ij.decode_imagej_roi(wire).roi.selection
    assert ij.selection_polygon_type(restored) == ij.selection_polygon_type(edited)
    np.testing.assert_allclose(restored.points, edited.points, atol=1e-5)
    assert ij.decode_imagej_roi(wire).source_type == (4 if kind == "polyline" else 8)


@pytest.mark.parametrize("carrier", ["active", "previous", "saved"])
@pytest.mark.parametrize("opened", [False, True])
def test_v5_subtype_detection_covers_each_native_selection_carrier(carrier, opened):
    source = ij.with_selection_polygon_type(
        ij.Selection("polyline" if opened else "freehand", [[3, 3], [13, 3], [13, 13]]),
        ij.SelectionPolygonType.FREELINE if opened else ij.SelectionPolygonType.TRACED)
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), np.uint8), axes="YX")); edit = owner.begin_edit()
    if carrier == "active":
        edit.select(source)
    elif carrier == "previous":
        edit.set_previous_selection(source)
    else:
        edit.set_rois([ij.ROI("subtype", source)])
    owner.commit(edit)
    blob = ij.encode_session(ij.SessionCapture([owner.snapshot()]))
    assert blob.startswith(b"IMAGEJXX5\n")
    restored = ij.decode_session(blob).documents[0].snapshot()
    result = restored.selection if carrier == "active" else restored.previous_selection if carrier == "previous" else restored.rois[0].selection
    assert result == source


@pytest.mark.parametrize("value", [True, 3, "unknown", ["freeline"], "traced"])
def test_invalid_or_mismatched_subtype_is_not_opaque_metadata(value):
    source = ij.Selection("polyline", [[2, 2], [12, 12]], attributes={"imagejxx.polygon_type": value})
    with pytest.raises(ValueError):
        ij.selection_polygon_type(source)
    with pytest.raises(ValueError):
        ij.interpolate_selection(source, 40, 40)


@pytest.mark.parametrize("magnification", [0, -1, float("inf"), float("nan")])
def test_invalid_zoom_refuses_before_callbacks(magnification):
    source = ij.Selection("freehand", [[3, 3], [13, 3], [13, 13]])
    options = ij.SelectionInterpolationOptions(); options.magnification = magnification
    events = []
    with pytest.raises(ValueError):
        ij.interpolate_selection(source, 40, 40, options, checkpoint=lambda: events.append(1))
    assert not events


def test_degenerate_area_retains_geometry_empty_membership_and_repeated_behavior():
    source = ij.Selection("polygon", [[2, 2], [8, 2], [14, 2]])
    options = ij.SelectionInterpolationOptions(); options.interval = 2.7; options.adjust_to_match = True
    result = ij.interpolate_selection(source, 40, 40, options)
    # A large interval yields an explicit one-vertex area, not a PointRoi.
    options.interval = 100
    tiny = ij.interpolate_selection(source, 40, 40, options)
    assert tiny.kind == "polygon" and len(tiny.points) == 1
    assert not np.any(tiny.mask((40, 40)))
    assert ij.interpolate_selection(tiny, 40, 40, options) == tiny
    assert len(ij.SelectionEditor(tiny, (40, 40)).handles()) == 1
    moved = ij.SelectionEditor(tiny, (40, 40)).moved_vertex(0, [5, 7])
    assert moved.points == [[5, 7]] and not np.any(moved.mask((40, 40)))
    dataset = ij.Dataset(np.zeros((40, 40), np.uint8), axes="YX")
    request = ij.DatasetHistogramRequest(); request.area_roi = moved
    assert ij.accumulate_dataset_histogram(dataset, request).visited_count == 0
    wire = ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI("degenerate", moved)))
    restored = ij.decode_imagej_roi(wire).roi.selection
    assert restored.kind == "polygon" and restored.points == moved.points
    assert not np.any(restored.mask((40, 40)))
    assert len(result.points) >= 1


def test_one_vertex_polyline_is_explicit_editable_and_roundtrips():
    options = ij.SelectionInterpolationOptions(); options.interval = 8
    first = ij.interpolate_selection(ij.Selection("line", [[3.2, 4.7], [13.8, 3.1]]), 40, 40, options)
    result = ij.interpolate_selection(first, 40, 40, options)
    assert result.kind == "polyline" and len(result.points) == 1
    assert ij.selection_polygon_type(result) == ij.SelectionPolygonType.POLYLINE
    assert ij.interpolate_selection(result, 40, 40, options) == result
    assert len(ij.SelectionEditor(result, (40, 40)).handles()) == 1
    assert not np.any(result.mask((40, 40)))
    restored = ij.decode_imagej_roi(ij.encode_imagej_roi(ij.ImageJRoi(ij.ROI("one vertex", result)))).roi.selection
    assert restored.points == result.points and ij.selection_polygon_type(restored) == ij.SelectionPolygonType.POLYLINE
    owner = ij.Document(ij.Dataset(np.zeros((40, 40), np.uint8), axes="YX"))
    command = ij.resolve_selection_creation_command(owner.snapshot(), ij.SelectionCreationRequest(
        result, ij.SelectionCreationOperation.REPLACE, ij.SelectionCreationGeometry.NATIVE_PATH_V4))
    ij.validate_selection_creation_command_record(command.record())
    owner.commit(ij.prepare_selection_creation(command))
    assert ij.decode_session(ij.encode_session(ij.SessionCapture([owner.snapshot()]))).documents[0].snapshot().selection == result


@pytest.mark.parametrize("case", [c for c in REFERENCE["cases"] if c["kind"] == "tiny"], ids=lambda c: c["id"])
def test_native_empty_area_policy_for_upstream_null_outputs(case):
    assert all(step["error"] for step in case["steps"])
    options = ij.SelectionInterpolationOptions()
    options.interval = case["interval"]; options.smooth = case["smooth"]; options.adjust_to_match = case["adjust"]
    options.magnification = case["magnification"]
    source = ij.Selection("polygon", case["source"]["samples"])
    result = ij.interpolate_selection(source, 40, 40, options)
    assert result.kind == "polygon" and result.points == [[2, 2]]
    assert not np.any(result.mask((40, 40)))
    assert ij.interpolate_selection(result, 40, 40, options) == result


def test_repeated_native_compound_profile_preserves_all_membership():
    from test_interpolation_compound import contours, path, rect
    source = path(rect(2, 2, 30, 30) + rect(10, 10, 22, 22) + rect(34, 34, 38, 38))
    expected = source.mask((40, 40))
    for _ in range(3):
        source = ij.interpolate_selection(source, 40, 40)
        assert source.is_path and len(contours(source)) == 3
        np.testing.assert_array_equal(source.mask((40, 40)), expected)


def test_actual_measure_preserves_degenerate_area_interpretation(tmp_path):
    from imagejxx import _desktop
    source = ij.with_selection_polygon_type(ij.Selection("polygon", [[2, 2]]), ij.SelectionPolygonType.POLYGON)
    pixels = np.arange(1600, dtype=np.uint16).reshape(40, 40)
    owner = ij.Document(ij.Dataset(pixels, axes="YX")); owner.select(source)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    editor.update_settings(json.dumps({"measurements": ["Area"]}), False)
    before = owner.snapshot()
    editor.action("measure", "{}"); editor.wait()
    assert json.loads(editor.results)[-1]["Area"] == 0
    assert owner.snapshot().selection == source and owner.snapshot().shares_data(before)
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy("YX"), pixels)


@pytest.mark.parametrize("opened", [False, True])
@pytest.mark.parametrize("high_zoom", [False, True])
def test_physical_repeat_after_edit_zoom_workspace_and_rebind(tmp_path, opened, high_zoom):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); directory = tmp_path / "repeat-ui"
    report = json.loads(editor._interpolation_repeat_ui_case(str(directory), opened, high_zoom))
    reloaded = _desktop.Editor(str(tmp_path / "preferences.json"))
    assert json.loads(reloaded.settings())["selection_interpolation_smooth"] is True
    assert report == json.loads((directory / "case.json").read_bytes())
    kind = "polyline" if opened else "freehand"
    case = next(c for c in REFERENCE["cases"] if c["kind"] == kind and c["magnification"] == (12 if high_zoom else 0)
                and c["interval"] == 1 and c["smooth"] and not c["adjust"])
    np.testing.assert_allclose(report["first"]["points"], case["steps"][0]["result"]["samples"], atol=1e-5, rtol=1e-7)
    # An integer translation is a separate metamorphic edit check; pinned
    # two-command arrays remain unchanged and independently supply the shape.
    np.testing.assert_allclose(report["second"]["points"], np.asarray(case["steps"][1]["result"]["samples"]) + [1, 2], atol=1e-5, rtol=1e-7)
    assert report["record"]["version"] == 4 and report["magnification"] == (12 if high_zoom else 1)
    assert "dialog.apply.stale_zoom" in [c["name"] for c in report["controls"]]
