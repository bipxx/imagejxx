"""One explicit original Shape descriptors option, using existing native owners."""
import csv
import json
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
from test_center_of_mass import same

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/shape-descriptors-imagej154r.json").read_bytes())
FIELDS=[("circularity","Circ."),("aspect_ratio","AR"),("roundness","Round"),("solidity","Solidity")]


def source(case):
    if case["kind"]=="whole_image":return None
    s=ij.Selection(case["kind"],case["points"])
    if case["traced"]:s=ij.with_selection_polygon_type(s,ij.SelectionPolygonType.TRACED)
    if case["fitted"]:s=ij.fit_spline_selection(s,40,40)
    return s


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_four_descriptor_columns_and_frozen_options(case):
    assert case["selection_retained"] or case["kind"]=="whole_image"
    spatial=ij.SpatialCalibration(spacing=[*case["spacing"],1],unit="um")
    pixels=(np.arange(40)[None,:]+2*np.arange(40)[:,None]).astype([np.uint8,np.uint16,np.float32][case["type"]])
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="YX",calibration=spatial).observe_reads(reads))
    selection=source(case)
    if selection is not None:owner.select(selection)
    app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.shape_descriptors=True
    before=reads.counts().plane_reads;command=ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before and command.record["version"]==15
    assert command.record["options"]["shape_descriptor_policy"]=="rounded-hull/legacy-ellipse/area-options-v1"
    exposed=command.options;exposed.shape_descriptors=False
    owner.select(ij.Selection("rectangle",[[0,0],[1,1]]));snapshot,undo=owner.snapshot(),owner.undo_steps
    row=ij.execute_target_analysis(command).planes[0]
    for field,column in FIELDS:
        same(getattr(row,field),case["rows"][0][column])
        with pytest.raises(AttributeError):setattr(row,field,0)
    assert row.statistics["area"]==case["rows"][0]["Area"]
    assert reads.counts().plane_reads==before+1 and owner.snapshot().shares_data(snapshot) and owner.undo_steps==undo
    assert command.options.shape_descriptors
    calls=[]
    with pytest.raises(ValueError):ij.execute_target_analysis(command,runtime_max_working_bytes=command.estimate.working_bytes-1,checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads==before+1


@pytest.mark.parametrize("kind,points",[("line",[[3,3],[13,13]]),("point",[[3,3]]),("rotated_rectangle",[[10,10],[15,10],[10,13]])])
def test_unsupported_descriptor_rows_refuse_before_reads_but_scalar_defaults_stay_usable(kind,points):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads))
    owner.select(ij.Selection(kind,points));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert all(getattr(row,name) is None for name,_ in FIELDS)
    before=reads.counts().plane_reads;options.shape_descriptors=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==before


@pytest.mark.parametrize("kind",["rectangle","ellipse"])
def test_descriptor_float32_narrowing_refuses_before_geometry_or_reads(kind):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((40,40),np.uint8),axes="YX").observe_reads(reads))
    owner.select(ij.Selection(kind,[[0,0],[1e100,10]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.shape_descriptors=True
    with pytest.raises(ValueError,match="Float32"):
        ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0


def test_descriptor_request_is_not_a_generic_area_profile_option():
    owner=ij.Document(ij.Dataset(np.zeros((4,4),np.uint8),axes="YX"));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.shape_descriptors=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)


@pytest.mark.parametrize("bypass",[False,True])
def test_manager_order_global_bypass_and_aggregate_work_admission(bypass):
    from test_target_measurements import fixture
    from test_global_calibration_execution import global_value
    app,owner,reads,_=fixture();app.set_global_calibration(global_value());app.set_ignore_global_calibration(bypass,owner.id)
    request=ij.roi_measurement_target_request(owner.snapshot(),[2,0,3])
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER;options.roi_indices=[2,0,3];options.shape_descriptors=True
    target=app.resolve_target(request);before=reads.counts().plane_reads
    limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    with pytest.raises(ValueError):ij.resolve_target_analysis(target,options,limits)
    assert reads.counts().plane_reads==before
    command=ij.resolve_target_analysis(target,options);rows=ij.execute_target_analysis(command).planes
    assert [row.roi_index for row in rows]==[2,0,3]
    assert reads.counts().plane_reads==before+2
    for row in rows:
        assert row.solidity==1 and np.isnan(row.aspect_ratio) and np.isposinf(row.roundness)
        assert row.circularity==pytest.approx(min(1,4*np.pi*row.statistics["area"]/row.perimeter**2))


def test_old_scalar_default_has_no_descriptor_geometry_prerequisite():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((4,6),np.uint8),axes="YX").observe_reads(reads))
    owner.select(ij.Selection("rectangle",[[1,1],[5,3]]));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    limits=ij.TargetAnalysisLimits();limits.max_measurement_work=1
    command=ij.resolve_target_analysis(app.resolve_target(),options,limits)
    assert not command.options.shape_descriptors and command.record["version"]==11
    row=ij.execute_target_analysis(command).planes[0]
    assert all(getattr(row,name) is None for name,_ in FIELDS)
    assert reads.counts().plane_reads==1


def test_descriptor_axes_are_geometric_while_density_population_stays_separate():
    from test_calibrated_analysis import density
    owner=ij.Document(ij.Dataset(np.zeros((4,6),np.uint8),axes="YX"));app=ij.Application();app.adopt(owner)
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(),density("all_nan","uint8"),"uint8"))
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.shape_descriptors=True
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert row.statistics["pixel_count"]==0 and row.circularity==row.roundness==0
    assert row.aspect_ratio==1.5 and np.isnan(row.solidity)
    assert row.centroid_world==[3,2,0]


@pytest.mark.parametrize("profile,shape,cal",[(37,1,0),(38,2,0),(39,4,0),(40,1,2),(41,0,0),(42,9,0),(43,13,0)])
def test_physical_shape_option_columns_csv_workspace(tmp_path,profile,shape,cal):
    from imagejxx import _desktop as desktop
    case=next(c for c in REFERENCE["cases"] if c["id"]==f"shape{shape}-type0-cal{cal}")
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"shape-descriptors"
    report=json.loads(editor._subtype_measurement_ui_case(str(directory),profile))
    for _,name in FIELDS:
        actual=report["rows"][0][name];expected=case["rows"][0][name]
        if isinstance(expected,str):assert actual==expected
        else:same(actual,expected)
        assert name in report["columns"]
    assert report["cancel_preserved"] and report["workspace_preserved"] and report["source_unchanged_by_measure"]
    assert report["additional_measure_reads"]==1
    if profile==37:assert report["stale_constructor_checked"]
    if profile==43:assert report["fitted"] and report["fit_undo_steps"]==1
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==1
    for _,name in FIELDS:
        expected=case["rows"][0][name]
        if isinstance(expected,str):assert rows[0][name]==expected
        else:assert float(rows[0][name])==pytest.approx(expected,abs=1e-6)
