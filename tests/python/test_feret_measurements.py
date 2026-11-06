"""Original five-column Feret option through captured public consumers."""
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
from test_center_of_mass import same
from test_shape_descriptors import source

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/feret-measurements-imagej154r.json").read_bytes())
FIELDS=[("feret","Feret"),("min_feret","MinFeret"),("feret_angle","FeretAngle"),("feret_x","FeretX"),("feret_y","FeretY")]


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_five_columns_and_frozen_request(case):
    sx,sy=case["spacing"]
    spatial=ij.SpatialCalibration(spacing=[sx,sy,1],origin=[-case["origins"][0]*sx,-case["origins"][1]*sy,0],
        direction=[1,0,0,0,-1 if case["invert_y"] else 1,0,0,0,1],unit="um")
    pixels=(np.arange(40)[None,:]+2*np.arange(40)[:,None]).astype([np.uint8,np.uint16,np.float32][case["type"]])
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="YX",calibration=spatial).observe_reads(reads))
    selection=source(case)
    if selection is not None:owner.select(selection)
    app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.feret=True
    before=reads.counts().plane_reads;command=ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before and command.record["version"]==13
    assert command.record["options"]["feret_policy"]=="float-hull/analytic-oval/pixel-fallback/local-plane-v1"
    exposed=command.options;exposed.feret=False
    owner.select(ij.Selection("rectangle",[[0,0],[1,1]]));snapshot,undo=owner.snapshot(),owner.undo_steps
    row=ij.execute_target_analysis(command).planes[0]
    for field,column in FIELDS:
        same(getattr(row,field),case["rows"][0][column])
        with pytest.raises(AttributeError):setattr(row,field,0)
    assert row.feret_in_pixels==(case["kind"]=="whole_image")
    assert row.feret_coordinates_scaled==(case["kind"]=="ellipse")
    assert row.circularity is None and command.options.feret
    assert reads.counts().plane_reads==before+1 and owner.snapshot().shares_data(snapshot) and owner.undo_steps==undo
    calls=[]
    with pytest.raises(ValueError):ij.execute_target_analysis(command,runtime_max_working_bytes=command.estimate.working_bytes-1,checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads==before+1


@pytest.mark.parametrize("kind,points",[("line",[[3,3],[13,13]]),("point",[[3,3]]),("rotated_rectangle",[[10,10],[15,10],[10,13]])])
def test_unsupported_rows_refuse_before_reads_without_changing_scalar_defaults(kind,points):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads))
    owner.select(ij.Selection(kind,points));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert all(getattr(row,name) is None for name,_ in FIELDS)
    before=reads.counts().plane_reads;options.feret=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before


@pytest.mark.parametrize("kind",["rectangle","ellipse"])
def test_float32_boundary_refuses_before_geometry_and_reads(kind):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads))
    owner.select(ij.Selection(kind,[[0,0],[1e100,10]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.feret=True
    with pytest.raises(ValueError,match="Float32"):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0


@pytest.mark.parametrize("bypass",[False,True])
def test_manager_grouping_global_bypass_and_shared_options(bypass):
    from test_target_measurements import fixture
    from test_global_calibration_execution import global_value
    app,owner,reads,_=fixture();app.set_global_calibration(global_value());app.set_ignore_global_calibration(bypass,owner.id)
    request=ij.roi_measurement_target_request(owner.snapshot(),[2,0,3]);target=app.resolve_target(request)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER;options.roi_indices=[2,0,3];options.feret=True;options.shape_descriptors=True
    before=reads.counts().plane_reads;limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    with pytest.raises(ValueError):ij.resolve_target_analysis(target,options,limits)
    assert reads.counts().plane_reads==before
    command=ij.resolve_target_analysis(target,options);rows=ij.execute_target_analysis(command).planes
    assert [row.roi_index for row in rows]==[2,0,3] and reads.counts().plane_reads==before+2
    sx,sy=command.spatial.spacing[:2]
    for row,(w,h),(x,y) in zip(rows,[(2,1),(2,2),(1,1)],[(2,0),(0,0),(0,0)]):
        assert row.feret==pytest.approx(np.hypot(w*sx,h*sy))
        assert row.min_feret==pytest.approx(min(w*sx,h*sy))
        assert row.feret_angle==pytest.approx(180-np.degrees(np.arctan2(h*sy,w*sx)))
        assert (row.feret_x,row.feret_y)==(x,y)
        assert not row.feret_in_pixels and not row.feret_coordinates_scaled and row.circularity is not None


def test_old_scalar_and_generic_area_contracts():
    owner=ij.Document(ij.Dataset(np.zeros((4,6),np.uint8),axes="YX"));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    command=ij.resolve_target_analysis(app.resolve_target(),options,limits)
    assert not command.options.feret and command.record["version"]==11
    assert all(getattr(ij.execute_target_analysis(command).planes[0],name) is None for name,_ in FIELDS)
    options.feret=True;options.measurement=ij.TargetMeasurementMode.AREA
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)


@pytest.mark.parametrize("kind",["rectangle","ellipse"])
def test_full_affine_plane_metric(kind):
    spatial=ij.SpatialCalibration(direction=[1,1,0,0,1,0,0,0,1])
    owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX",calibration=spatial))
    owner.select(ij.Selection(kind,[[3,3],[13,11]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.feret=True
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    if kind=="rectangle":
        assert row.feret==pytest.approx(np.sqrt(388)) and row.min_feret==pytest.approx(np.sqrt(50))
    else:
        assert row.feret*row.min_feret==pytest.approx(80) and row.feret>10 and row.min_feret>0


@pytest.mark.parametrize("profile,shape,cal",[(44,1,0),(45,2,2),(46,5,0),(47,0,2),(48,9,2),(49,13,0),(50,12,0)])
def test_physical_feret_columns_csv_workspace(tmp_path,profile,shape,cal):
    import csv
    from imagejxx import _desktop as desktop
    case=next(c for c in REFERENCE["cases"] if c["id"]==f"shape{shape}-type0-cal{cal}-coords0")
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"feret"
    report=json.loads(editor._subtype_measurement_ui_case(str(directory),profile))
    for _,name in FIELDS:
        same(report["rows"][0][name],case["rows"][0][name]);assert name in report["columns"]
    assert report["cancel_preserved"] and report["workspace_preserved"] and report["source_unchanged_by_measure"]
    assert report["additional_measure_reads"]==1
    assert report["rows"][0]["Feret unit"]==("pixel" if shape==0 else "um")
    assert report["rows"][0]["Feret coordinate unit"]==("um" if case["kind"]=="ellipse" else "pixel")
    if profile==44:assert report["stale_constructor_checked"] and "Circ." in report["columns"]
    if profile==49:assert report["fitted"] and report["fit_undo_steps"]==1
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==1
    for _,name in FIELDS:assert float(rows[0][name])==pytest.approx(case["rows"][0][name],abs=1e-6)
