"""Original typed moments through captured Measure, without geometry API changes."""
import csv
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

REFERENCE = json.loads((Path(__file__).parents[1] / "fixtures/center-of-mass-imagej154r.json").read_bytes())


def density(case):
    kind = ["uint8","uint16","float32"][case["type"]]
    mode = case["mode"]
    if mode == "raw":
        return ij.ImageJIntensityCalibration.uncalibrated()
    if mode == "signed16":
        return ij.ImageJIntensityCalibration.signed16()
    if mode in ("linear","cancel"):
        return ij.ImageJIntensityCalibration.function(kind,"straight_line",[10,2] if mode=="linear" else [-1,2],"OD")
    values = np.arange(256 if kind=="uint8" else 65536,dtype=np.float32)
    values[0] = np.nan if mode=="nan" else np.inf
    return ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration",version=1,
        mode="lookup",value_unit="table",bit_depth=8 if kind=="uint8" else 16,
        table_f32_hex="".join(f"{int(word):08x}" for word in values.view(np.uint32)),source_parameters={},signed16=False))


def same(actual, expected):
    if expected == "nan":
        assert np.isnan(actual)
    elif expected == "inf":
        assert np.isposinf(actual)
    elif expected == "-inf":
        assert np.isneginf(actual)
    else:
        assert actual == pytest.approx(expected,abs=1e-12,rel=1e-12)


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_weighted_positions_are_frozen_and_geometric_centroids_stay_independent(case):
    dtype = [np.uint8,np.uint16,np.float32][case["type"]]
    y,x = np.indices((6,8))
    pixels = (x+2*y+1 if case["pattern"]==0 else np.zeros((6,8)) if case["pattern"]==1 else
        np.where((x==5)&(y==3),4,0) if case["pattern"]==2 else 2*(x%2)-1 if dtype==np.float32 else x%2).astype(dtype)
    sx,sy=case["spacing"];ox,oy=case["origins"]
    spatial=ij.SpatialCalibration(spacing=[sx,sy,1],origin=[-ox*sx,oy*sy if case["invert_y"] else -oy*sy,0],
        direction=[1,0,0,0,-1 if case["invert_y"] else 1,0,0,0,1],unit="um")
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(pixels,axes="YX",calibration=spatial).observe_reads(reads))
    if case["kind"]!="whole_image":
        owner.select(ij.Selection(case["kind"],case["points"]))
    app=ij.Application();app.adopt(owner)
    app.set_global_calibration(ij.GlobalCalibration(spatial,density(case),["uint8","uint16","float32"][case["type"]]))
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    before=reads.counts().plane_reads
    command=ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before and command.record["version"]==11
    assert command.record["options"]["center_of_mass_policy"]=="typed-row-order/byte-float-minvalue/nonzero-affine-v1"
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(),ij.ImageJIntensityCalibration.uncalibrated(),"uint8"))
    owner.select(ij.Selection("rectangle",[[0,0],[1,1]]))
    snapshot,undo=owner.snapshot(),owner.undo_steps
    row=ij.execute_target_analysis(command).planes[0]
    expected=case["rows"][0]
    for actual,name in zip(row.center_of_mass_world[:2],["XM","YM"]):
        same(actual,expected[name])
    assert row.center_of_mass_world[2]==0
    for actual,name in zip(row.centroid_world[:2],["X","Y"]):
        same(actual,expected[name])
    assert row.statistics["area"]==expected["Area"]
    with pytest.raises(AttributeError):
        row.center_of_mass_world=[0,0,0]
    exposed=row.center_of_mass_world;exposed[0]=123
    assert np.isnan(row.center_of_mass_world[0]) or row.center_of_mass_world[0]!=123
    assert reads.counts().plane_reads==before+1 and owner.snapshot().shares_data(snapshot) and owner.undo_steps==undo


def test_nonfinite_weighted_summary_does_not_weaken_geometry_api():
    spatial=ij.SpatialCalibration()
    with pytest.raises(ValueError):
        spatial.voxel_to_world([float("inf"),0,0])
    pixels=np.tile([-1,1],(2,2)).astype(np.float32)
    spatial=ij.SpatialCalibration(spacing=[2,3,5],origin=[7,11,13],direction=[0,-1,0,1,0,0,0,0,1])
    owner=ij.Document(ij.Dataset(np.stack([pixels,pixels,pixels]),axes="ZYX",calibration=spatial))
    view=owner.snapshot().display;view.crosshair=[0,0,2];owner.set_display(view)
    app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert np.isnan(row.center_of_mass_world[0]) and np.isposinf(row.center_of_mass_world[1])
    assert row.center_of_mass_world[2]==23
    assert row.centroid_world==[4,15,23]


@pytest.mark.parametrize("bypass",[False,True])
def test_manager_grouping_global_bypass_and_capture(bypass):
    from test_target_measurements import fixture,prepare
    from test_global_calibration_execution import global_value
    app,owner,reads,pixels=fixture()
    app.set_global_calibration(global_value());app.set_ignore_global_calibration(bypass,owner.id)
    command=prepare(app,owner,[2,1,0]);before=reads.counts().plane_reads
    rows=ij.execute_target_analysis(command).planes
    assert reads.counts().plane_reads==before+2
    for row,index in zip(rows,[2,1,0]):
        if index==1:
            assert row.center_of_mass_world is None
            continue
        raw=pixels[1,0,1]
        ys,xs=np.indices(raw.shape)
        mask=(xs>=2)&(ys==0) if index==2 else (xs<2)&(ys<2)
        weights=raw[mask].astype(float)
        if not bypass:weights=10+2*weights
        x=(xs[mask]*weights).sum()/weights.sum()+.5
        y=(ys[mask]*weights).sum()/weights.sum()+.5
        np.testing.assert_allclose(row.center_of_mass_world,[x*(3 if bypass else .5),y*(4 if bypass else 2),5 if bypass else 3],atol=1e-12)


@pytest.mark.parametrize("profile,xm,ym",[(32,2.6052631578947363,2.557017543859649),(33,"nan","nan"),(34,1.5,2.25),(35,"inf","nan"),(36,"inf","nan")])
def test_physical_weighted_columns_csv_workspace(tmp_path,profile,xm,ym):
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"center-of-mass"
    report=json.loads(editor._subtype_measurement_ui_case(str(directory),profile))
    assert report["rows"][0]["X"]==1.5 and report["rows"][0]["Y"]==2.25
    for name,expected in [("XM",xm),("YM",ym)]:
        value=report["rows"][0][name]
        assert value==expected if isinstance(expected,str) else value==pytest.approx(expected)
        assert name in report["columns"]
    assert report["cancel_preserved"] and report["workspace_preserved"]
    assert report["source_unchanged_by_measure"] and report["additional_measure_reads"]==1
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==1
    for name,expected in [("XM",xm),("YM",ym)]:
        assert rows[0][name]==expected if isinstance(expected,str) else float(rows[0][name])==pytest.approx(expected,abs=1e-6)
