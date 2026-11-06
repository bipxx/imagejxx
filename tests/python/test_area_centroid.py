"""Geometric cell-center centroids, original Analyzer and real Measure consumers."""
import csv
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/area-centroid-imagej154r.json").read_bytes())


@pytest.mark.parametrize("case", REFERENCE["cases"], ids=lambda c: c["id"])
def test_original_centroid_captured_without_another_read(case):
    dtype = [np.uint8, np.uint16, np.float32][case["type"]]
    y, x = np.indices((40, 40))
    pixels = (x-y*.5 if dtype == np.float32 else x+y).astype(dtype)
    sx, sy = case["spacing"]
    ox, oy = case["origins"]
    spatial = ij.SpatialCalibration(spacing=[sx, sy, 1], origin=[-ox*sx, oy*sy if case["invert_y"] else -oy*sy, 0],
        direction=[1,0,0,0,-1 if case["invert_y"] else 1,0,0,0,1], unit="um")
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="YX", calibration=spatial).observe_reads(reads))
    selection = None if case["kind"] == "whole_image" else ij.Selection(case["kind"], case["points"])
    if selection is not None:
        owner.select(selection)
    mask = np.ones((40,40), bool) if selection is None else selection.mask((40,40)) != 0
    assert np.flatnonzero(mask).tolist() == case["members"]
    assert case["native_membership_matches_original"]
    app = ij.Application(); app.adopt(owner)
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    before = reads.counts().plane_reads
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    assert reads.counts().plane_reads == before and command.record["version"] == 10
    assert command.record["options"]["area_centroid_policy"] == "geometric-pixel-centers/captured-affine-v1"
    owner.select(ij.Selection("rectangle", [[0,0],[1,1]]))
    snapshot, undo = owner.snapshot(), owner.undo_steps
    row = ij.execute_target_analysis(command).planes[0]
    expected = case["rows"][0]
    if expected["X"] == "nan":
        assert np.isnan(row.centroid_world).all()
    else:
        np.testing.assert_allclose(row.centroid_world, [expected["X"], expected["Y"], 0], atol=1e-12, rtol=1e-12)
    assert row.statistics["area"] == expected["Area"]
    with pytest.raises(AttributeError):
        row.centroid_world = [0,0,0]
    exposed = row.centroid_world
    exposed[0] = 123
    assert np.isnan(row.centroid_world[0]) or row.centroid_world[0] != 123
    assert reads.counts().plane_reads == before + 1
    assert owner.snapshot().shares_data(snapshot) and owner.undo_steps == undo
    calls = []
    with pytest.raises(ValueError):
        ij.execute_target_analysis(command, runtime_max_working_bytes=command.estimate.working_bytes-1,
            checkpoint=lambda: calls.append(1))
    assert not calls and reads.counts().plane_reads == before + 1


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32])
def test_native_affine_centroid_uses_captured_slice_and_source(dtype):
    pixels = np.ones((3,4,6), dtype=dtype)
    spatial = ij.SpatialCalibration(spacing=[2,3,5], origin=[7,11,13],
        direction=[0,-1,0,1,0,0,0,0,1], unit="world")
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX", calibration=spatial).observe_reads(reads))
    owner.select(ij.Selection("rectangle", [[1,1],[5,3]]))
    view = owner.snapshot().display; view.crosshair = [0,0,2]; owner.set_display(view)
    app = ij.Application(); app.adopt(owner)
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    command = ij.resolve_target_analysis(app.resolve_target(), options)
    before = reads.counts().plane_reads
    row = ij.execute_target_analysis(command).planes[0]
    # Local mean centers=(3,2), captured z=2; native affine extension.
    assert row.centroid_world == [1,17,23]
    assert reads.counts().plane_reads == before + 1


def test_density_nan_filter_does_not_change_geometric_centroid():
    from test_calibrated_analysis import density
    owner = ij.Document(ij.Dataset(np.zeros((4,6),dtype=np.uint8), axes="YX"))
    app = ij.Application(); app.adopt(owner)
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(),density("all_nan","uint8"),"uint8"))
    options = ij.TargetAnalysisOptions(); options.measurement = ij.TargetMeasurementMode.SELECTION
    row = ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(), options)).planes[0]
    assert row.statistics["pixel_count"] == 0
    assert row.centroid_world == [3,2,0]


@pytest.mark.parametrize("profile,x,y", [(28,10.1,3.55),(29,33.5,10.75),(30,"nan","nan"),(31,9.5,-4.25)])
def test_physical_centroid_columns_precision_csv_workspace(tmp_path, profile, x, y):
    from imagejxx import _desktop as desktop
    editor = desktop.Editor(str(tmp_path / "preferences.json"))
    directory = tmp_path / "area-centroid"
    report = json.loads(editor._subtype_measurement_ui_case(str(directory), profile))
    row = report["rows"][0]
    for name, expected in [("X",x),("Y",y)]:
        assert row[name] == expected if isinstance(expected,str) else row[name] == pytest.approx(expected)
        assert name in report["columns"]
    assert report["cancel_preserved"] and report["workspace_preserved"]
    assert report["source_unchanged_by_measure"] and report["additional_measure_reads"] == 1
    with (directory / "measure.csv").open(newline="",encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows)==1
    for name, expected in [("X",x),("Y",y)]:
        assert rows[0][name] == expected if isinstance(expected,str) else float(rows[0][name]) == pytest.approx(expected,abs=1e-6)
