"""Original Analyzer per-point rows, captured batch admission and ownership."""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from test_path_measurements import pixels_and_density, compare


ROWS = [json.loads(x) for x in gzip.decompress((Path(__file__).parents[1] /
    "fixtures/point-measurements-imagej154r.jsonl.gz").read_bytes()).splitlines()]


@pytest.mark.parametrize("row", ROWS)
def test_original_point_and_multipoint_rows(row):
    pixels, density = pixels_and_density(row)
    spatial = ij.SpatialCalibration(spacing=[*row["spacing"], 1], origin=[*row["origin"], 0], unit="um")
    owner = ij.Document(ij.Dataset(pixels, axes="YX", calibration=spatial))
    owner.select(ij.Selection(row["kind"], np.array(row["points"]).reshape(-1, 2).tolist(), width=7))
    ij.set_imagej_intensity_calibration(owner, density)
    app = ij.Application();app.adopt(owner)
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert command.record["version"] == 5 and command.estimate.row_count == len(row["rows"])
    result = ij.execute_target_analysis(command)
    assert len(result.planes) == len(row["rows"])
    for index, (actual, expected) in enumerate(zip(result.planes, row["rows"])):
        assert actual.point_index == index and actual.roi_index is None
        assert actual.length is actual.midpoint_world is actual.angle_degrees is None
        for axis, key in enumerate(["X", "Y"]):
            compare(actual.point_world[axis], expected[key])
        if expected["Width"] == 0:
            assert actual.bounds == [0, 0, 0, 0]
        else:
            assert actual.bounds == [round((expected["BX"]-row["origin"][0])/row["spacing"][0]),
                round((expected["BY"]-row["origin"][1])/row["spacing"][1]), 1, 1]
        for key, heading in [("area", "Area"), ("mean", "Mean"), ("stddev", "StdDev"),
            ("minimum", "Min"), ("maximum", "Max"), ("median", "Median"), ("integrated_density", "IntDen")]:
            compare(actual.statistics[key], expected[heading])
        compare(actual.statistics["raw_mean"]*actual.statistics["pixel_count"], expected["RawIntDen"])


def point_fixture():
    reads = ij.DatasetReadObserver()
    pixels = np.arange(70, dtype=np.uint8).reshape(2, 5, 7)
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX").observe_reads(reads))
    points = ij.Selection("multipoint", [(.5, .5), (2.25, 1.75), (.5, .5)])
    owner.select(points)
    edit = owner.begin_edit();edit.set_rois([
        ij.ROI("points", points, z=1),
        ij.ROI("area", ij.Selection("rectangle", [(0, 0), (2, 1)]), z=0),
        ij.ROI("again", ij.Selection("point", [(1, 1)]), z=1)])
    owner.commit(edit)
    app = ij.Application();app.adopt(owner)
    return app, owner, reads


def test_manager_expansion_order_grouping_caps_and_cancellation():
    app, owner, reads = point_fixture()
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.ROI_MANAGER
    options.roi_indices = [1, 0, 2]
    target = app.resolve_target(ij.roi_measurement_target_request(owner.snapshot(), options.roi_indices))
    n = reads.counts().plane_reads
    limits = ij.TargetAnalysisLimits();limits.max_measurement_rows = 4
    with pytest.raises(ValueError, match="expanded row limit"):
        ij.resolve_target_analysis(target, options, limits)
    assert reads.counts().plane_reads == n
    command = ij.resolve_target_analysis(target, options)
    assert command.estimate.row_count == 5 and command.estimate.plane_count == 2
    limits.max_working_bytes = command.estimate.working_bytes - 1
    with pytest.raises(ValueError):
        ij.resolve_target_analysis(target, options, limits)
    assert reads.counts().plane_reads == n
    before, undo = owner.snapshot(), owner.undo_steps
    rows = ij.execute_target_analysis(command).planes
    assert reads.counts().plane_reads == n + 2
    assert [r.roi_index for r in rows] == [1, 0, 0, 0, 2]
    assert [r.point_index for r in rows] == [None, 0, 1, 2, 0]
    assert [r.statistics["mean"] for r in rows] == [.5, 43, 51, 43, 43]
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo
    token = ij.CancellationToken()
    def progress(done, total):
        if done == 1:token.cancel()
    n = reads.counts().plane_reads
    with pytest.raises(ij.OperationCancelled):
        ij.execute_target_analysis(command, cancellation=token, progress=progress)
    assert reads.counts().plane_reads == n + 1


def test_point_expansion_all_z_uses_affine_and_preserves_repeated_points():
    app, owner, reads = point_fixture()
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    command = ij.resolve_target_analysis(app.resolve_target(ij.TargetRequest(ij.TargetScope.ALL_Z)), options)
    assert command.estimate.row_count == 6
    rows = ij.execute_target_analysis(command).planes
    assert [r.position.z for r in rows] == [0, 0, 0, 1, 1, 1]
    assert [r.point_index for r in rows] == [0, 1, 2, 0, 1, 2]
    assert rows[0].point_world == [.5, .5, 0] and rows[3].point_world == [.5, .5, 1]


@pytest.mark.parametrize("points", [[], [[float("nan"), 1]], [[1e100, 1]]])
def test_invalid_point_geometry_never_reads(points):
    app, owner, reads = point_fixture()
    n = reads.counts().plane_reads
    options = ij.TargetAnalysisOptions();options.measurement = ij.TargetMeasurementMode.SELECTION
    with pytest.raises(ValueError):
        owner.select(ij.Selection("point", points))
        ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == n


def test_scripted_selected_rows_expand_immediately_and_validate_guards(tmp_path):
    from imagejxx import _desktop
    _, owner, reads = point_fixture()
    editor = _desktop.Editor(str(tmp_path / "preferences.json"));editor.add_native_document(owner)
    before, undo = owner.snapshot(), owner.undo_steps
    n = reads.counts().plane_reads
    editor.action("measure_rois", json.dumps({"indices": [2, 0]}))
    rows = json.loads(editor.results)
    assert not editor.busy and len(rows) == 4 and reads.counts().plane_reads == n + 1
    assert [r["ROI index"] for r in rows] == [2, 0, 0, 0]
    assert [r["Point index"] for r in rows] == [0, 0, 1, 2]
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo
    n = reads.counts().plane_reads
    for invalid in [{"indices": []}, {"indices": [0, 0]}, {"indices": [-1]},
                    {"indices": [0.5]}, {"indices": [99]}, {"unexpected": 0}]:
        with pytest.raises(ValueError):editor.action("measure_rois", json.dumps(invalid))
    with pytest.raises(ij.RevisionConflict):
        editor.action("measure_rois", '{"indices":[0],"metadata_revision":999}')
    assert reads.counts().plane_reads == n and json.loads(editor.results) == rows


def test_selected_manager_rgb_refuses_instead_of_legacy_measure_all(tmp_path):
    from imagejxx import _desktop
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((5, 7, 3), np.uint8), axes="YXS").observe_reads(reads))
    edit = owner.begin_edit();edit.set_rois([ij.ROI("rgb", ij.Selection("point", [(1, 1)]))]);owner.commit(edit)
    editor = _desktop.Editor(str(tmp_path / "preferences.json"));editor.add_native_document(owner)
    editor.results = '[{"previous":1}]'
    n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="finite scalar storage"):
        editor.action("measure_rois", '{"indices":[0]}')
    assert reads.counts().plane_reads == n and json.loads(editor.results) == [{"previous": 1}]
    rendered = json.loads(editor.render_headless())
    assert rendered["roi_manager"]["controls"]["measure_selected"]["enabled"] is False
