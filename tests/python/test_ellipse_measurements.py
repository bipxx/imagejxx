"""Original fitted area statistics through captured API and both real UI hosts."""
import csv
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
from test_center_of_mass import same
from test_shape_descriptors import source, REFERENCE as BASE
from test_rectangle_measurements import spatial_for

EXTRA=json.loads((Path(__file__).parents[1]/"fixtures/ellipse-measurements-extra-imagej154r.json").read_bytes())
FIELDS=[("ellipse_major","Major"),("ellipse_minor","Minor"),("ellipse_angle_degrees","Angle")]


@pytest.mark.parametrize("case",BASE["cases"]+EXTRA["cases"],ids=lambda c:c["id"])
def test_original_axes_angle_and_implicit_fitted_center(case):
    additional="summary" in case
    spatial=spatial_for(case) if additional else ij.SpatialCalibration(spacing=[*case["spacing"],1],unit="um")
    pixels=(np.arange(40)[None,:]+2*np.arange(40)[:,None]).astype([np.uint8,np.uint16,np.float32][case["type"]])
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="YX",calibration=spatial).observe_reads(reads))
    selection=source(case)
    if selection is not None:owner.select(selection)
    app=ij.Application();app.adopt(owner)
    if case.get("density")=="all_nan":
        from test_calibrated_analysis import density
        kind=["uint8","uint16","float32"][case["type"]]
        app.set_global_calibration(ij.GlobalCalibration(spatial,density("all_nan",kind),kind))
    implicit=case.get("implicit",False)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    assert not options.ellipse
    options.ellipse=not implicit;options.shape_descriptors=implicit
    before=reads.counts().plane_reads;command=ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before and command.record["version"]==15
    assert command.record["options"]["ellipse_policy"]=="processor-frame/legacy-moments/near-square-spacing/image-angle-v1"
    assert command.record["options"]["area_centroid_policy"]=="ellipse-fitted-center/captured-affine-v1"
    exposed=command.options;exposed.ellipse=False;exposed.shape_descriptors=False
    owner.select(ij.Selection("rectangle",[[0,0],[1,1]]));snapshot,undo=owner.snapshot(),owner.undo_steps
    row=ij.execute_target_analysis(command).planes[0]
    expected=case["summary"] if additional else case["rows"][0]
    for field,column in FIELDS:
        if implicit:assert getattr(row,field) is None
        else:same(getattr(row,field),expected[column])
        with pytest.raises(AttributeError):setattr(row,field,0)
    if additional:
        same(row.centroid_world[0],expected["X"]);same(row.centroid_world[1],expected["Y"])
    assert (row.circularity is not None)==implicit
    assert command.options.ellipse==(not implicit) and command.options.shape_descriptors==implicit
    assert reads.counts().plane_reads==before+1 and owner.snapshot().shares_data(snapshot) and owner.undo_steps==undo
    calls=[]
    with pytest.raises(ValueError):ij.execute_target_analysis(command,runtime_max_working_bytes=command.estimate.working_bytes-1,checkpoint=lambda:calls.append(1))
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.execute_target_analysis(command,cancellation=token,checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads==before+1


@pytest.mark.parametrize("kind,points",[("line",[[3,3],[13,13]]),("point",[[3,3]]),("rotated_rectangle",[[10,10],[15,10],[10,13]])])
def test_unqualified_rows_refuse_before_reads_and_keep_scalar_defaults(kind,points):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads));owner.select(ij.Selection(kind,points));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert all(getattr(row,k) is None for k,_ in FIELDS)
    if kind=="line":assert row.angle_degrees is not None
    before=reads.counts().plane_reads;options.ellipse=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before


@pytest.mark.parametrize("kind",["rectangle","ellipse","polygon"])
def test_float32_narrowing_and_work_refuse_before_reads(kind):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads));app=ij.Application();app.adopt(owner)
    points=[[0,0],[1e100,10]] if kind!="polygon" else [[0,0],[1e100,0],[0,10]]
    owner.select(ij.Selection(kind,points));options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.ellipse=True
    with pytest.raises(ValueError,match="constructor coordinates" if kind=="polygon" else "Float32"):ij.resolve_target_analysis(app.resolve_target(),options)
    owner.select(ij.Selection("polygon",[[3,3],[13,3],[9,11]]));limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options,limits)
    assert reads.counts().plane_reads==0


def test_generic_area_refuses_and_combination_shares_fit_work():
    owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX"));owner.select(ij.Selection("polygon",[[3,3],[13,3],[9,11]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.ellipse=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    options.measurement=ij.TargetMeasurementMode.SELECTION;options.shape_descriptors=True
    command=ij.resolve_target_analysis(app.resolve_target(),options)
    # One 1600-pixel borrowed-mask traversal; adding explicit columns to the
    # implicit fit does not add another geometry-work charge.
    implicit=ij.TargetAnalysisOptions();implicit.measurement=ij.TargetMeasurementMode.SELECTION;implicit.shape_descriptors=True
    for work in [1600,1700,2000]:
        limits=ij.TargetAnalysisLimits();limits.max_measurement_work=work
        outcomes=[]
        for request in [implicit,options]:
            try:ij.resolve_target_analysis(app.resolve_target(),request,limits);outcomes.append(True)
            except ValueError:outcomes.append(False)
        assert outcomes[0]==outcomes[1]
    row=ij.execute_target_analysis(command).planes[0]
    assert row.aspect_ratio==pytest.approx(row.ellipse_major/row.ellipse_minor)
    assert row.centroid_world==pytest.approx([8.3,5.6,0])


@pytest.mark.parametrize("bypass",[False,True])
def test_manager_group_order_captured_global_and_fit_composition(bypass):
    from test_target_measurements import fixture
    from test_global_calibration_execution import global_value
    app,owner,reads,_=fixture();app.set_global_calibration(global_value());app.set_ignore_global_calibration(bypass,owner.id)
    request=ij.roi_measurement_target_request(owner.snapshot(),[2,0,3]);options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER;options.roi_indices=[2,0,3];options.ellipse=True;options.shape_descriptors=True;options.feret=True;options.bounding_rectangle=True
    before=reads.counts().plane_reads;command=ij.resolve_target_analysis(app.resolve_target(request),options);assert reads.counts().plane_reads==before
    rows=ij.execute_target_analysis(command).planes
    assert [r.roi_index for r in rows]==[2,0,3] and reads.counts().plane_reads==before+2
    for row in rows:assert row.ellipse_major==row.ellipse_minor==0 and row.circularity is not None and row.feret is not None and row.bounding_x is not None


@pytest.mark.parametrize("profile,shape,cal,extra",[(59,1,0,False),(60,2,0,False),(61,3,0,False),(62,1,2,False),(63,0,0,False),(64,3,0,True),(65,4,0,True),(66,13,0,False)])
def test_physical_ellipse_columns_angle_only_csv_workspace(tmp_path,profile,shape,cal,extra):
    from imagejxx import _desktop as desktop
    case=next(c for c in (EXTRA if extra else BASE)["cases"] if c["id"]==(f"extra{shape}-type0-shiftfalse-nanfalse-implicitfalse" if extra else f"shape{shape}-type0-cal{cal}"))
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"ellipse"
    report=json.loads(editor._subtype_measurement_ui_case(str(directory),profile))
    for _,name in FIELDS:
        expected=case["rows"][0][name]
        if isinstance(expected,str):assert report["rows"][0][name]==expected
        else:same(report["rows"][0][name],expected)
    assert report["additional_measure_reads"]==1 and report["cancel_preserved"] and report["workspace_preserved"] and report["source_unchanged_by_measure"]
    if profile==59:assert report["stale_constructor_checked"] and "Circ." in report["columns"] and "BX" in report["columns"]
    if profile==66:assert report["fitted"] and report["fit_undo_steps"]==1
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==1
    for _,name in FIELDS:
        if profile==61 and name!="Angle":assert name not in rows[0];continue
        expected=case["rows"][0][name]
        if isinstance(expected,str):assert rows[0][name]==expected
        else:assert float(rows[0][name])==pytest.approx(expected,abs=1e-6)


def test_physical_angle_only_line_keeps_existing_orientation(tmp_path):
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"))
    report=json.loads(editor._subtype_measurement_ui_case(str(tmp_path/"line-angle"),67))
    assert report["rows"][0]["Angle"]==pytest.approx(-38.659808254090095)
    assert "Major" not in report["rows"][0] and report["columns"]==["Angle","X","Y"]
    assert report["additional_measure_reads"]==1 and report["cancel_preserved"] and report["workspace_preserved"]
