"""Native even-odd interpolation profile, separately observed from dispatch bugs."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/interpolation-components-imagej154r.json").read_bytes())


def rect(x, y, right, bottom):
    return [0, x, y, 1, right, y, 1, right, bottom, 1, x, bottom, 4]


def path(commands):
    return ij.Selection.from_path(ij.ImageJCompositePath(commands))


def contours(value):
    result = []; current = []
    commands = value.path.commands; at = 0
    while at < len(commands):
        op = commands[at]; at += 1
        if op == 4:
            result.append(current); current = []
        else:
            assert op in (0, 1)
            current.append(commands[at:at+2]); at += 2
    assert not current
    return result


def source(case):
    if case["kind"] == "ellipse":
        return ij.Selection("ellipse", [[3.2, 4.7], [26.8, 19.9]])
    if case["kind"] == "rounded_rectangle":
        return ij.Selection("rounded_rectangle", [[3, 4], [27, 20]]).with_attributes({"corner_diameter": 6})
    return path(case["commands"])


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_observed_component_arrays_and_even_odd_mask(case):
    original = source(case); before = original.with_attributes(original.attributes)
    options = ij.SelectionInterpolationOptions()
    options.interval = case["interval"]; options.smooth = case["smooth"]; options.adjust_to_match = case["adjust"]
    result = ij.interpolate_selection(original, 40, 40, options)
    actual = contours(result)
    assert len(actual) == len(case["components"])
    direct = case
    if case["scenario"] == "curve":
        direct = next(c for c in REFERENCE["cases"] if c["id"] == "single-" + case["id"])
    expected_components = ([{"samples": direct["command_samples"]}]
                           if case["scenario"] in ("curve", "single-curve") else case["components"])
    for points, expected in zip(actual, expected_components):
        np.testing.assert_allclose(points, expected["samples"], atol=1e-5, rtol=1e-7)
    expected_mask = direct["command_mask"] if case["scenario"] in ("curve", "single-curve") else case["profile_mask"]
    mask = np.asarray([[v == "1" for v in row] for row in expected_mask])
    np.testing.assert_array_equal(result.mask((40, 40)) != 0, mask)
    assert original == before
    assert "corner_diameter" not in result.attributes


def pixels():
    a = np.zeros((32, 32), np.uint8); a[2:22, 2:22] = 255; a[8:16, 8:16] = 0
    a[26:30, 26:30] = 255; a[24, 2] = a[25, 3] = 255
    return a


def test_mask_boundaries_keep_holes_islands_and_diagonal_contacts():
    a = pixels(); original = ij.Selection.from_mask(a)
    result = ij.interpolate_selection(original, 32, 32)
    assert len(contours(result)) == 5
    np.testing.assert_array_equal(result.mask((32, 32)) != 0, a != 0)
    assert not result.path.contains([12, 12]) and result.path.contains([28, 28])
    assert len(ij.SelectionEditor(result, (32, 32)).handles()) == sum(map(len, contours(result)))


@pytest.mark.parametrize("seed", range(8))
def test_unit_spacing_preserves_independent_random_pixel_masks(seed):
    a = (np.random.default_rng(seed).random((12, 12)) < .35).astype(np.uint8)
    result = ij.interpolate_selection(ij.Selection.from_mask(a), 12, 12)
    np.testing.assert_array_equal(result.mask((12, 12)) != 0, a != 0)


def test_intrinsic_offcanvas_region_is_not_clipped_before_interpolation():
    original = ij.SelectionRegion(ij.Selection.from_mask(pixels()), (32, 32), (-10, -8)).as_selection()
    result = ij.interpolate_selection(original, 40, 40)
    assert len(contours(result)) == 5
    assert min(p[0] for ring in contours(result) for p in ring) == -8
    assert min(p[1] for ring in contours(result) for p in ring) == -6
    np.testing.assert_array_equal(result.mask((40, 40)) != 0, original.mask((40, 40)) != 0)


@pytest.mark.parametrize("mask", [False, True])
def test_aggregate_budget_frozen_options_and_live_cancel(mask):
    a = np.zeros((32, 32), np.uint8); a[2:30, 2:30] = 255; a[10:22, 10:22] = 0
    original = ij.Selection.from_mask(a) if mask else path(rect(2, 2, 30, 30) + rect(10, 10, 22, 22))
    size = 32 if mask else 40
    options = ij.SelectionInterpolationOptions(); options.interval = 2.7; options.adjust_to_match = True
    expected = ij.interpolate_selection(original, size, size, options)
    limits = ij.SelectionEditLimits(); limits.max_working_bytes = ij.estimate_interpolated_selection(original, size, size, options)
    assert ij.interpolate_selection(original, size, size, options, limits) == expected
    limits.max_working_bytes -= 1; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.interpolate_selection(original, size, size, options, limits, checkpoint=lambda: events.append(1))
    assert not events
    limits.max_working_bytes += 1
    assert ij.interpolate_selection(original, size, size, options, limits,
        checkpoint=lambda: setattr(options, "interval", 99)) == expected
    token = ij.CancellationToken()
    with pytest.raises(ij.OperationCancelled):
        ij.interpolate_selection(original, size, size, cancellation=token, checkpoint=token.cancel)


def test_aggregate_vertex_limit_and_empty_mask_refuse_before_callbacks():
    original = path(rect(2, 2, 12, 12) + rect(20, 20, 30, 30))
    limits = ij.SelectionEditLimits(); limits.max_vertices = 80; events = []
    with pytest.raises((ValueError, RuntimeError)):
        ij.interpolate_selection(original, 40, 40, limits=limits, checkpoint=lambda: events.append(1))
    assert not events
    with pytest.raises(ValueError, match="nonempty"):
        ij.interpolate_selection(ij.Selection.from_mask(np.zeros((32, 32), np.uint8)), 32, 32)


@pytest.mark.parametrize("kind", ["rotated_rectangle", "rotated_ellipse", "rotated_rounded_rectangle"])
def test_rotated_native_frame_profiles(kind):
    original = ij.Selection(kind, [[20, 20], [30, 24], [18, 25]])
    attributes = {"opaque": [True, "retained"]}
    if "rounded" in kind:
        attributes["corner_diameter"] = 6
    original = original.with_attributes(attributes)
    result = ij.interpolate_selection(original, 40, 40)
    assert result.is_path and len(contours(result)) == 1 and result.path.contains([20, 20])
    assert result.attributes == {"opaque": [True, "retained"]}


@pytest.mark.parametrize("profile", [0, 1, 2, 3], ids=["path", "mask", "ellipse", "curve"])
@pytest.mark.parametrize("configured", [False, True])
def test_actual_components_workflow_python_host(tmp_path, profile, configured):
    from imagejxx import _desktop
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); directory = tmp_path / "components-ui"
    report = json.loads(editor._interpolation_components_ui_case(str(directory), profile, configured))
    assert report == json.loads((directory / "case.json").read_bytes()) and report["context"] == "imgui_python"
    scenario = "hole" if profile < 2 else "ellipse" if profile == 2 else "curve"
    suffix = "2.7-true-true" if configured else "1.0-false-false"
    case = next(c for c in REFERENCE["cases"] if c["id"] == scenario + "-" + suffix)
    actual = contours(path(report["commands"]))
    expected_components = ([{"samples": next(c for c in REFERENCE["cases"] if c["id"] == "single-" + case["id"])["command_samples"]}]
                           if profile == 3 else case["components"])
    for points, expected in zip(actual, expected_components):
        np.testing.assert_allclose(points, expected["samples"], atol=1e-5, rtol=1e-7)
    assert len(actual) == len(expected_components) and report["record"]["version"] == 4


def test_actual_measure_and_v5_workspace_of_compound_result(tmp_path):
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.arange(1600, dtype=np.uint16).reshape(40, 40), axes="YX"))
    original = path(rect(2, 2, 30, 30) + rect(10, 10, 22, 22)); owner.select(original)
    editor = _desktop.Editor(str(tmp_path / "preferences.json")); editor.add_native_document(owner)
    editor.action("interpolate_selection", "{}"); editor.wait(); result = owner.snapshot().selection
    editor.update_settings(json.dumps({"measurements": ["Area", "count"]}), False)
    editor.action("measure", "{}"); editor.wait()
    assert json.loads(editor.results)[-1]["Area"] == np.count_nonzero(result.mask((40, 40)))
    target = tmp_path / "compound.ijx"; editor.action("save_workspace", json.dumps({"path": str(target)}))
    reopened = _desktop.Editor(str(tmp_path / "reopened.json")); reopened.open_file(str(target))
    assert reopened.native_snapshot().selection == result
    np.testing.assert_array_equal(reopened.native_snapshot().dataset.numpy("YX"), np.arange(1600, dtype=np.uint16).reshape(40, 40))


def test_native_path_histogram_freeze_admission_and_single_provider_read():
    a = np.arange(1600, dtype=np.uint16).reshape(40, 40)
    reads = ij.DatasetReadObserver(); dataset = ij.Dataset(a, axes="YX").observe_reads(reads)
    selection = path(rect(2, 2, 30, 30) + rect(10, 10, 22, 22))
    request = ij.DatasetHistogramRequest(); request.area_roi = selection
    estimate = ij.estimate_dataset_histogram(dataset, request)
    assert estimate.selection_bytes > len(selection.path.commands) * 8 and reads.counts().plane_reads == 0
    limits = ij.DatasetHistogramLimits(); limits.max_working_bytes = estimate.working_bytes - 1
    events = []
    with pytest.raises(ValueError):
        ij.accumulate_dataset_histogram(dataset, request, limits, checkpoint=lambda: events.append(1))
    assert not events and reads.counts().plane_reads == 0
    limits.max_working_bytes += 1
    result = ij.accumulate_dataset_histogram(dataset, request, limits,
        checkpoint=lambda: setattr(request, "area_roi", ij.Selection("rectangle", [[0, 0], [1, 1]])))
    members = a[selection.mask((40, 40)) != 0]
    assert result.visited_count == len(members) and result.sum == float(members.astype(np.float64).sum())
    assert result.request.area_roi == selection and reads.counts().plane_reads == 1


@pytest.mark.parametrize("positioned", [False, True])
def test_native_paths_in_active_and_positioned_target_histograms(positioned):
    a = np.arange(1600, dtype=np.uint16).reshape(40, 40)
    reads = ij.DatasetReadObserver(); owner = ij.Document(ij.Dataset(a, axes="YX").observe_reads(reads))
    selection = path(rect(2, 2, 30, 30) + rect(10, 10, 22, 22))
    edit = owner.begin_edit(); edit.select(selection); edit.set_rois([ij.ROI("area", selection)]); owner.commit(edit)
    request = ij.TargetRequest(roi=ij.RoiPolicy.POSITIONED_ROIS if positioned else ij.RoiPolicy.ACTIVE_SELECTION)
    target = ij.resolve_target(owner.snapshot(), request)
    estimate = ij.estimate_target_histogram(target)
    assert reads.counts().plane_reads == 0
    limits = ij.DatasetHistogramLimits(); limits.max_working_bytes = estimate.working_bytes - 1; events = []
    with pytest.raises(ValueError):
        ij.accumulate_target_histogram(target, limits=limits, checkpoint=lambda: events.append(1))
    assert not events and reads.counts().plane_reads == 0
    limits.max_working_bytes += 1; result = ij.accumulate_target_histogram(target, limits=limits)
    # Observed-range UInt16 targets use the established extrema/count passes;
    # the standalone one-plane service above captures its plane only once.
    assert result.visited_count == np.count_nonzero(selection.mask((40, 40))) and reads.counts().plane_reads == 2
