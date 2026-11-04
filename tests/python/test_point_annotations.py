"""Original PointRoi counters/positions and versioned native metadata."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from test_path_measurements import compare


ROWS = [json.loads(x) for x in gzip.decompress((Path(__file__).parents[1] /
    "fixtures/point-annotations-imagej154r.jsonl.gz").read_bytes()).splitlines()]
EXTENT = [2, 2, 2, 5, 7, 1]


def fixture(row):
    pixels = np.zeros((2, 2, 2, 5, 7), np.uint8)
    for t in range(2):
        for c in range(2):
            for z in range(2):pixels[t, c, z] = 80*t + 30*c + 10*z + np.arange(7) + np.arange(5)[:, None]*10
    if row["type"] == "uint16":pixels = pixels.astype(np.uint16)*257
    elif row["type"] == "float32":pixels = pixels.astype(np.float32)*.25-8
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX", calibration=ij.SpatialCalibration(
        spacing=[*row["spacing"], 3], origin=[11, -7, 19], unit="um")).observe_reads(reads))
    selected = ij.with_imagej_point_counters(ij.Selection("multipoint", row["points"]), row["words"], EXTENT)
    owner.select(selected)
    if row["density"] != "raw":
        ij.set_imagej_intensity_calibration(owner, ij.ImageJIntensityCalibration.function(row["type"],
            "straight_line" if row["density"] == "linear" else "poly2",
            [10, 2] if row["density"] == "linear" else [0, 0, 1], "value"))
    view = owner.snapshot().display;view.t = view.c = 1;view.crosshair = [0, 0, 0];owner.set_display(view)
    app = ij.Application();app.adopt(owner)
    return app, owner, reads, selected


@pytest.mark.parametrize("row", ROWS)
def test_original_annotated_point_rows(row):
    app, owner, reads, selected = fixture(row)
    assert ij.imagej_point_counters(selected, EXTENT) == row["words"]
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    n = reads.counts().plane_reads;before, undo = owner.snapshot(), owner.undo_steps
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert command.record["version"] == 6 and reads.counts().plane_reads == n
    result = ij.execute_target_analysis(command)
    assert reads.counts().plane_reads == n + command.estimate.plane_count
    assert len(result.planes) == len(row["rows"])
    positions = set()
    for index, (actual, original) in enumerate(zip(result.planes, row["rows"])):
        assert actual.point_index == index and actual.point_counter == (row["words"][index]&255)
        assert actual.point_counter_count == original.get("Count")
        assert (actual.position.t+1, actual.position.c+1, actual.position.z+1) == (
            original["Frame"], original["Ch"], original["Slice"])
        positions.add((actual.position.t, actual.position.c, actual.position.z))
        for axis, key in enumerate(["X", "Y"]):compare(actual.point_world[axis], original[key])
        for key, heading in [("area", "Area"), ("mean", "Mean"), ("stddev", "StdDev"),
            ("minimum", "Min"), ("maximum", "Max"), ("median", "Median"), ("integrated_density", "IntDen")]:
            compare(actual.statistics[key], original[heading])
    assert len(positions) == command.estimate.plane_count
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo


def test_manager_factory_includes_actual_point_positions_not_unused_parent():
    row = dict(ROWS[1]);row["words"] = [256, 1026, 1026, 2147]
    app, owner, reads, selected = fixture(row)
    edit = owner.begin_edit();edit.set_rois([ij.ROI("annotated", selected, t=0, c=1, z=0)]);owner.commit(edit)
    request = ij.roi_measurement_target_request(owner.snapshot())
    assert [(p.t, p.c, p.z) for p in request.positions] == [(0, 0, 0), (0, 1, 1), (1, 1, 1)]
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.ROI_MANAGER
    n = reads.counts().plane_reads
    command = ij.resolve_target_analysis(app.resolve_target(request), options)
    result = ij.execute_target_analysis(command)
    assert len(result.planes) == 4 and reads.counts().plane_reads == n + 3
    assert [r.point_counter_count for r in result.planes] == [1, 2, 2, 1]


def test_wire_projection_requires_explicit_adapter_and_preserves_words():
    selected = ij.with_imagej_point_counters(ij.Selection("multipoint", [(0, 0), (1, 1)]), [2050, 0], EXTENT)
    raw = ij.ImageJRoi(ij.ROI("annotated", selected))
    with pytest.raises(ValueError, match="attributes"):
        ij.encode_imagej_roi(raw)
    raw.roi = ij.ROI("annotated", ij.without_point_annotations(selected))
    raw.point_counters = ij.imagej_point_counters(selected, EXTENT)
    decoded = ij.decode_imagej_roi(ij.encode_imagej_roi(raw))
    restored = ij.with_imagej_point_counters(decoded.roi.selection, decoded.point_counters, EXTENT)
    assert ij.imagej_point_counters(restored, EXTENT) == [2050, 0]


def test_annotated_groups_admit_actual_planes_and_cancel_without_partial_result():
    app, owner, reads, _ = fixture(ROWS[1])
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    target = app.resolve_target();command = ij.resolve_target_analysis(target, options)
    n = reads.counts().plane_reads
    assert command.estimate.plane_count == 4
    for key, value in [("max_pixels", command.estimate.total_pixels-1),
                       ("max_working_bytes", command.estimate.working_bytes-1),
                       ("max_measurement_rows", 3)]:
        limits = ij.TargetAnalysisLimits();setattr(limits, key, value)
        with pytest.raises(ValueError):ij.resolve_target_analysis(target, options, limits)
    assert reads.counts().plane_reads == n
    token = ij.CancellationToken()
    def progress(done, total):
        if done == 1:token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.execute_target_analysis(command, cancellation=token, progress=progress)
    assert reads.counts().plane_reads == n + 1
    invalid = ij.with_point_annotations(ij.Selection("point", [(0, 0)]),
        [ij.PointAnnotation(0, ij.PlanePosition(9, 0, 0))])
    owner.select(invalid);n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="outside dataset extent"):
        ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == n


def test_native_annotations_detach_preserve_unknown_metadata_and_roundtrip_session(tmp_path):
    selected = ij.Selection("multipoint", [(0, 0), (1, 1)], attributes={"opaque": {"keep": [1, "μ"]}})
    entries = [ij.PointAnnotation(2, ij.PlanePosition(1, 1, 1)), ij.PointAnnotation()]
    annotated = ij.with_point_annotations(selected, entries)
    entries[0].counter = 9
    assert ij.point_annotations(annotated)[0].counter == 2 and ij.point_annotations(selected) == []
    assert ij.without_point_annotations(annotated).attributes == selected.attributes
    assert ij.imagej_point_counters(annotated, EXTENT) == [2050, 0]
    # Exercise the existing session owner; this is not a recorder/replay claim.
    from imagejxx import _desktop
    owner = ij.Document(ij.Dataset(np.zeros((2, 2, 2, 5, 7), np.uint8), axes="TCZYX"));owner.select(annotated)
    editor = _desktop.Editor(str(tmp_path / "prefs.json"));editor.add_native_document(owner)
    path = tmp_path / "annotations.ijx";editor.action("save_document", json.dumps({"path": str(path)}))
    reopened = _desktop.Editor(str(tmp_path / "other-prefs.json"));reopened.open_file(str(path))
    restored = reopened.native_document(json.loads(reopened.document_infos())[0]["id"]).snapshot().selection
    assert restored.attributes == annotated.attributes


@pytest.mark.parametrize("counter", [-1, 100, True, 1.5])
def test_counter_constructor_refuses_invalid_values(counter):
    with pytest.raises((ValueError, TypeError, OverflowError)):ij.PointAnnotation(counter)


@pytest.mark.parametrize("words", [[0], [255, 0], [2304, 0], [0x80000000, 0]])
def test_wire_adapter_refuses_count_counter_extent_and_signed_overflow(words):
    with pytest.raises(ValueError):ij.with_imagej_point_counters(ij.Selection("multipoint", [(0, 0), (1, 1)]), words, EXTENT)


@pytest.mark.parametrize("record", [
    {"version": 2, "points": []},
    {"version": True, "points": []},
    {"version": 1, "points": []},
    {"version": 1, "points": [{"counter": 0, "position": None, "unknown": 1}]},
    {"version": 1, "points": [{"counter": True, "position": None}]},
    {"version": 1, "points": [{"counter": 0, "position": [-1, 0, 0]}]},
])
def test_malformed_metadata_refuses_before_plane_reads(record):
    reads = ij.DatasetReadObserver();owner = ij.Document(ij.Dataset(np.zeros((5, 7), np.uint8), axes="YX").observe_reads(reads))
    selected = ij.Selection("point", [(0, 0)], attributes={"imagejxx.point_annotations": record});owner.select(selected)
    app = ij.Application();app.adopt(owner);options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    n = reads.counts().plane_reads
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == n
