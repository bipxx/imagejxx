"""Original processor ROI rectangle, independent of tight/density bounds."""
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
from test_center_of_mass import same
from test_shape_descriptors import source

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/rectangle-measurements-imagej154r.json").read_bytes())
FIELDS=[("bounding_x","BX"),("bounding_y","BY"),("bounding_width","Width"),("bounding_height","Height")]


def spatial_for(case):
    sx,sy=case["spacing"];ox,oy=case["origins"]
    y_origin=((oy if oy else 39) if case["invert_y"] else -oy)*sy
    return ij.SpatialCalibration(spacing=[sx,sy,1],origin=[-ox*sx,y_origin,0],direction=[1,0,0,0,-1 if case["invert_y"] else 1,0,0,0,1],unit="um")


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_four_columns_and_frozen_request(case):
    spatial=spatial_for(case)
    pixels=(np.arange(40)[None,:]+2*np.arange(40)[:,None]).astype([np.uint8,np.uint16,np.float32][case["type"]])
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="YX",calibration=spatial).observe_reads(reads))
    selection=source(case)
    if selection is not None:owner.select(selection)
    app=ij.Application();app.adopt(owner)
    if case["density"]=="all_nan":
        from test_calibrated_analysis import density
        app.set_global_calibration(ij.GlobalCalibration(spatial,density("all_nan",["uint8","uint16"][case["type"]]),["uint8","uint16"][case["type"]]))
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.bounding_rectangle=True
    before=reads.counts().plane_reads;command=ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before and command.record["version"]==14
    assert command.record["options"]["rectangle_policy"]=="processor-roi/local-axes/full-affine-v1"
    exposed=command.options;exposed.bounding_rectangle=False
    owner.select(ij.Selection("rectangle",[[0,0],[1,1]]));snapshot,undo=owner.snapshot(),owner.undo_steps
    row=ij.execute_target_analysis(command).planes[0]
    for field,column in FIELDS:
        same(getattr(row,field),case["rows"][0][column])
        with pytest.raises(AttributeError):setattr(row,field,0)
    assert row.statistics["area"]==case["rows"][0]["Area"]
    bx,by,w,h=case["processor_bounds"]
    assert row.bounding_origin_world==pytest.approx(spatial.voxel_to_world([bx,by,0]))
    assert np.linalg.norm(row.bounding_horizontal_world)==pytest.approx(row.bounding_width)
    assert np.linalg.norm(row.bounding_vertical_world)==pytest.approx(row.bounding_height)
    for field in ["bounding_origin_world","bounding_horizontal_world","bounding_vertical_world"]:
        with pytest.raises(AttributeError):setattr(row,field,[0,0,0])
    if "shape12-" in case["id"]:assert row.bounds is None and row.bounding_width>0 and row.bounding_height==0
    assert row.feret is None and row.circularity is None and command.options.bounding_rectangle
    assert reads.counts().plane_reads==before+1 and owner.snapshot().shares_data(snapshot) and owner.undo_steps==undo
    calls=[]
    with pytest.raises(ValueError):ij.execute_target_analysis(command,runtime_max_working_bytes=command.estimate.working_bytes-1,checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads==before+1


@pytest.mark.parametrize("kind,points",[("line",[[3,3],[13,13]]),("point",[[3,3]]),("rotated_rectangle",[[10,10],[15,10],[10,13]])])
def test_unqualified_rows_refuse_atomically_but_scalar_defaults_stay_usable(kind,points):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads));owner.select(ij.Selection(kind,points));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0];assert all(getattr(row,k) is None for k,_ in FIELDS)
    before=reads.counts().plane_reads;options.bounding_rectangle=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before


def test_polygon_float32_refuses_before_reads():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads));owner.select(ij.Selection("polygon",[[0,0],[1e100,0],[0,10]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.bounding_rectangle=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0


@pytest.mark.parametrize("bypass",[False,True])
def test_manager_grouping_global_bypass_and_option_composition(bypass):
    from test_target_measurements import fixture
    from test_global_calibration_execution import global_value
    app,owner,reads,_=fixture();app.set_global_calibration(global_value());app.set_ignore_global_calibration(bypass,owner.id)
    target=app.resolve_target(ij.roi_measurement_target_request(owner.snapshot(),[2,0,3]))
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER;options.roi_indices=[2,0,3];options.bounding_rectangle=True;options.feret=True;options.shape_descriptors=True
    before=reads.counts().plane_reads;limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    with pytest.raises(ValueError):ij.resolve_target_analysis(target,options,limits)
    assert reads.counts().plane_reads==before
    command=ij.resolve_target_analysis(target,options);rows=ij.execute_target_analysis(command).planes
    assert [r.roi_index for r in rows]==[2,0,3] and reads.counts().plane_reads==before+2
    for row,(x,y,w,h) in zip(rows,[(2,0,2,1),(0,0,2,2),(0,0,1,1)]):
        expected=command.spatial.voxel_to_world([x,y,row.position.z]);assert row.bounding_origin_world==pytest.approx(expected)
        assert row.bounding_x==expected[0] and row.bounding_y==expected[1]
        assert row.bounding_width==pytest.approx(w*command.spatial.spacing[0]) and row.bounding_height==pytest.approx(h*command.spatial.spacing[1])
        assert row.feret is not None and row.circularity is not None


def test_full_affine_rectangle_vectors_and_captured_plane():
    spatial=ij.SpatialCalibration(spacing=[2,.5,3],origin=[7,-4,9],direction=[1,1,0,0,1,0,.5,0,1])
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((2,40,40),np.uint8),axes="ZYX",calibration=spatial).observe_reads(reads));owner.select(ij.Selection("rectangle",[[3,3],[13,11]]))
    view=owner.snapshot().display;view.crosshair=[0,0,1];owner.set_display(view);app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.bounding_rectangle=True
    command=ij.resolve_target_analysis(app.resolve_target(),options);view.crosshair=[0,0,0];owner.set_display(view)
    row=ij.execute_target_analysis(command).planes[0]
    assert row.bounding_origin_world==[14.5,-2.5,15] and row.bounding_horizontal_world==[20,0,10] and row.bounding_vertical_world==[4,4,0]
    assert row.bounding_width==pytest.approx(np.sqrt(500)) and row.bounding_height==pytest.approx(np.sqrt(32)) and row.position.z==1
    detached=row.bounding_horizontal_world;detached[0]=1000;assert row.bounding_horizontal_world==[20,0,10]
    assert reads.counts().plane_reads==1


def test_wide_native_box_clips_before_integer_conversion():
    owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX"));owner.select(ij.Selection("rectangle",[[0,0],[1e100,10]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.bounding_rectangle=True
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert (row.bounding_x,row.bounding_y,row.bounding_width,row.bounding_height)==(0,0,40,10)


def test_old_scalar_and_generic_area_contracts():
    owner=ij.Document(ij.Dataset(np.zeros((4,6),np.uint8),axes="YX"));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    command=ij.resolve_target_analysis(app.resolve_target(),options,limits);assert not command.options.bounding_rectangle and command.record["version"]==11
    assert all(getattr(ij.execute_target_analysis(command).planes[0],name) is None for name,_ in FIELDS)
    options.bounding_rectangle=True;options.measurement=ij.TargetMeasurementMode.AREA
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)


@pytest.mark.parametrize("profile,shape,cal,coords",[(51,1,2,1),(52,6,2,0),(53,7,0,0),(54,13,0,0),(55,0,2,3),(56,9,2,1),(57,12,0,0),(58,3,2,2)])
def test_physical_rectangle_columns_csv_workspace(tmp_path,profile,shape,cal,coords):
    import csv
    from imagejxx import _desktop as desktop
    case=next(c for c in REFERENCE["cases"] if c["id"]==f"shape{shape}-type0-cal{cal}-coords{coords}-raw")
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"rectangle"
    report=json.loads(editor._subtype_measurement_ui_case(str(directory),profile))
    for _,name in FIELDS:same(report["rows"][0][name],case["rows"][0][name]);assert name in report["columns"]
    assert report["additional_measure_reads"]==1 and report["cancel_preserved"] and report["workspace_preserved"] and report["source_unchanged_by_measure"]
    if profile==51:assert report["stale_constructor_checked"] and "Circ." in report["columns"] and "Feret" in report["columns"]
    if profile==54:assert report["fitted"] and report["fit_undo_steps"]==1
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==1
    for _,name in FIELDS:assert float(rows[0][name])==pytest.approx(case["rows"][0][name],abs=1e-6)
