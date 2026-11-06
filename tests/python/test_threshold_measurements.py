"""Original LIMIT populations through captured analysis, not a filtered ROI mask."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_center_of_mass import same

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/threshold-measurements-imagej154r.json").read_bytes())
STAT_FIELDS=["pixel_count","area","mean","raw_mean","stddev","minimum","maximum","median","mode_value","mode_index"]


def calibration(case):
    kind=["uint8","uint16","float32"][case["type"]]
    if case["density"]==0:return ij.ImageJIntensityCalibration.uncalibrated()
    if case["density"]==1:return ij.ImageJIntensityCalibration.function(kind,"straight_line",[-2,.5],"linear")
    values=np.arange(256 if case["type"]==0 else 65536,dtype=np.float32)
    if case["density"]==2:values[:]=np.nan
    else:values[7 if case["type"]==0 else 7*257]=np.nan
    return ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration",version=1,mode="lookup",value_unit="all_nan" if case["density"]==2 else "mixed_nan",bit_depth=8 if case["type"]==0 else 16,table_f32_hex="".join(f"{int(x):08x}" for x in values.view(np.uint32)),source_parameters={},signed16=False))


def fixture(case):
    y,x=np.indices((6,8));values=x+2*y
    if case["type"]==1:values=values*257
    if case["type"]==2:values=values*.25-2
    values=values.astype([np.uint8,np.uint16,np.float32][case["type"]])
    reads=ij.DatasetReadObserver();spatial=ij.SpatialCalibration(unit="um")
    owner=ij.Document(ij.Dataset(values,axes="YX",calibration=spatial).observe_reads(reads))
    if case["kind"]!="whole_image":
        selection=ij.Selection(case["kind"],case["points"])
        if case["fitted"]:selection=ij.fit_spline_selection(selection,8,6)
        owner.select(selection)
    if case["threshold_active"]:
        view=ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(*case["requested_range"]))
        owner.set_display(view)
    app=ij.Application();app.adopt(owner)
    if case["density"]:app.set_global_calibration(ij.GlobalCalibration(spatial,calibration(case),["uint8","uint16","float32"][case["type"]]))
    return app,owner,reads


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_limit_scalar_fit_histograms_and_bound_columns(case):
    app,owner,reads=fixture(case)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    assert not options.limit_to_threshold
    options.limit_to_threshold=case["limit"];options.ellipse=case["variant"]==1;options.shape_descriptors=case["variant"]==2
    if case["summary"] is not None:options.bounding_rectangle=True;options.feret=True
    before=reads.counts().plane_reads;command=ij.resolve_target_analysis(app.resolve_target(),options)
    command.validate_source(owner.snapshot());assert reads.counts().plane_reads==before
    if case["limit"]:
        assert command.record["version"]==16
        assert command.record["options"]["rows"][0]["threshold_range"]==(case["effective_range"] if case["threshold_active"] else None)
    exposed=command.options;exposed.limit_to_threshold=not case["limit"]
    snapshot,undo=owner.snapshot(),owner.undo_steps;result=ij.execute_target_analysis(command)
    assert len(result.planes)==len(case["rows"])
    for row,expected in zip(result.planes,case["rows"]):
        stats=row.statistics
        if case["summary"] is not None:
            summary=case["summary"]
            for field in STAT_FIELDS:
                value=summary[field]
                # The accepted flag-off API uses a full-image mask histogram,
                # including its existing empty-Short extrema. LIMIT now captures
                # the original processor ROI separately; do not change defaults.
                if not case["limit"] and case["type"]==1 and summary["pixel_count"]==0 and field=="minimum":value=4369
                same(stats[field],value)
            for field in ["histogram","histogram16"]:
                source=summary[field]
                if source is None:assert not len(stats[field]);continue
                expected_size=4370 if not case["limit"] and case["type"]==1 and field=="histogram16" else source["size"]
                assert len(stats[field])==expected_size
                assert [[int(i),int(stats[field][i])] for i in np.flatnonzero(stats[field])]==source["nonzero"]
            for axis,name in enumerate(["x","y"]):same(row.centroid_world[axis],summary[name])
            for axis,name in enumerate(["xm","ym"]):same(row.center_of_mass_world[axis],summary[name])
            for field,column in [("bounding_x","BX"),("bounding_y","BY"),("bounding_width","Width"),("bounding_height","Height"),("feret","Feret"),("min_feret","MinFeret"),("feret_angle","FeretAngle"),("feret_x","FeretX"),("feret_y","FeretY")]:same(getattr(row,field),expected[column])
            if case["variant"]==1:
                for field,key in [("ellipse_major","major"),("ellipse_minor","minor"),("ellipse_angle_degrees","angle")]:same(getattr(row,field),summary[key])
            else:assert row.ellipse_major is row.ellipse_minor is row.ellipse_angle_degrees is None
            if case["variant"]==2:
                for field,column in [("circularity","Circ."),("aspect_ratio","AR"),("roundness","Round"),("solidity","Solidity")]:same(getattr(row,field),expected[column])
        else:
            for field,column in [("area","Area"),("mean","Mean"),("stddev","StdDev"),("minimum","Min"),("maximum","Max"),("median","Median"),("mode_value","Mode")]:same(stats[field],expected[column])
            if row.point_world is not None:
                same(row.point_world[0],expected["X"]);same(row.point_world[1],expected["Y"])
            if row.length is not None:same(row.length,expected["Length"])
        same(stats["integrated_density"],expected["IntDen"])
        same(stats["pixel_count"]*stats["raw_mean"],expected["RawIntDen"])
        if case["limit"]:
            same(row.threshold_lower,expected["MinThr"]);same(row.threshold_upper,expected["MaxThr"])
        else:assert row.threshold_lower is row.threshold_upper is None
        for field in ["threshold_lower","threshold_upper"]:
            with pytest.raises(AttributeError):setattr(row,field,0)
    assert command.options.limit_to_threshold==case["limit"]
    assert owner.snapshot().shares_data(snapshot) and owner.undo_steps==undo
    after=reads.counts().plane_reads;assert after==before+(case["kind"]!="angle")
    calls=[]
    with pytest.raises(ValueError):ij.execute_target_analysis(command,runtime_max_working_bytes=command.estimate.working_bytes-1,checkpoint=lambda:calls.append(1))
    token=ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):ij.execute_target_analysis(command,cancellation=token,checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads==after


def test_threshold_context_guard_ignores_pan_and_preserves_frozen_range():
    case=next(c for c in REFERENCE["cases"] if c["id"]=="threshold-shape0-type0-range1-density0-variant0-limittrue")
    app,owner,reads=fixture(case);options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.limit_to_threshold=True
    command=ij.resolve_target_analysis(app.resolve_target(),options)
    view=owner.snapshot().display;view.zoom=2;view.pan=[[3,4],[0,0],[0,0]];owner.set_display(view);command.validate_source(owner.snapshot())
    view=ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(6,10));owner.set_display(view);command.validate_source(owner.snapshot())
    changed=ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(1,2));owner.set_display(changed)
    with pytest.raises(ij.RevisionConflict):command.validate_source(owner.snapshot())
    assert reads.counts().plane_reads==0
    row=ij.execute_target_analysis(command).planes[0];assert row.statistics["pixel_count"]==20 and row.threshold_lower==6
    options.limit_to_threshold=False;ordinary=ij.resolve_target_analysis(app.resolve_target(),options)
    owner.set_display(ij.reset_manual_threshold_state(owner.snapshot().display));ordinary.validate_source(owner.snapshot())
    assert ij.execute_target_analysis(ordinary).planes[0].statistics["pixel_count"]==48


@pytest.mark.parametrize("bypass",[False,True])
def test_manager_grouped_ranges_global_bypass_and_off_grid_fallback(bypass):
    y,x=np.indices((6,8));pixels=np.stack([x+2*y,x+2*y]).astype(np.uint8)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="ZYX").observe_reads(reads))
    edit=owner.begin_edit();selection=ij.Selection("rectangle",[[1,1],[6,5]])
    edit.set_rois([ij.ROI("z0",selection,z=0),ij.ROI("z1",selection,z=1),ij.ROI("z0again",selection,z=0)]);owner.commit(edit)
    ranges=[ij.AxisRange(0,1),ij.AxisRange(0,1),ij.AxisRange(0,2,2)]
    view=ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(6,10),ranges=ranges);owner.set_display(view)
    app=ij.Application();app.adopt(owner)
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(spacing=[2,2,1]),ij.ImageJIntensityCalibration.function("uint8","straight_line",[-2,.5],"linear"),"uint8"));app.set_ignore_global_calibration(bypass,owner.id)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER;options.roi_indices=[1,2,0];options.limit_to_threshold=True
    request=ij.roi_measurement_target_request(owner.snapshot(),[1,2,0]);command=ij.resolve_target_analysis(app.resolve_target(request),options);assert reads.counts().plane_reads==0
    before,undo=owner.snapshot(),owner.undo_steps;rows=ij.execute_target_analysis(command).planes
    assert [r.roi_index for r in rows]==[1,2,0] and [r.statistics["pixel_count"] for r in rows]==[20,12,12]
    assert [r.statistics["area"] for r in rows]==([20,12,12] if bypass else [80,48,48])
    assert [r.statistics["mean"] for r in rows]==([8,8,8] if bypass else [2,2,2])
    assert [r.threshold_lower for r in rows]==([0,6,6] if bypass else [-2,1,1])
    assert [r.threshold_upper for r in rows]==([255,10,10] if bypass else [125.5,3,3])
    assert reads.counts().plane_reads==2 and owner.snapshot().shares_data(before) and owner.undo_steps==undo


def test_generic_mode_refuses_before_reads_and_bbox_keeps_wide_native_domain():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((6,8),np.uint8),axes="YX").observe_reads(reads));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.limit_to_threshold=True
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0
    owner.select(ij.Selection("rectangle",[[0,0],[1e100,10]]));owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(0,0)))
    options.measurement=ij.TargetMeasurementMode.SELECTION;options.bounding_rectangle=True
    row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert row.bounding_width==8 and row.bounding_height==6 and row.statistics["pixel_count"]==48 and row.threshold_lower==0


@pytest.mark.parametrize("dtype",[np.uint8,np.uint16,np.float32])
def test_existing_finite_raw_range_domain_stays_safe_before_typed_narrowing(dtype):
    # Native ManualThresholdRange permits finite intervals outside processor
    # bounds. These are safe empty native results, not new Java parity claims.
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((6,8),dtype),axes="YX").observe_reads(reads));app=ij.Application();app.adopt(owner)
    owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(1e100,1e101)))
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.limit_to_threshold=True;options.ellipse=True
    command=ij.resolve_target_analysis(app.resolve_target(),options);assert reads.counts().plane_reads==0
    row=ij.execute_target_analysis(command).planes[0]
    assert row.statistics["pixel_count"]==0 and np.isnan(row.statistics["mean"])
    assert np.isnan(row.ellipse_major) and np.isnan(row.ellipse_minor) and row.ellipse_angle_degrees==0
    assert row.centroid_world==[.5,.5,0] and reads.counts().plane_reads==1


@pytest.mark.parametrize("profile",range(68,86))
def test_physical_threshold_preference_measure_csv_workspace(tmp_path,profile):
    import csv
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"threshold-measurement"
    report=json.loads(editor._subtype_measurement_ui_case(str(directory),profile))
    case=next(c for c in REFERENCE["cases"] if c["id"]==report["threshold_reference"])
    assert len(report["rows"])==len(case["rows"])
    aliases={"mean":"Mean","minimum":"Min","maximum":"Max","median":"Median"}
    for row,expected in zip(report["rows"],case["rows"]):
        for column in report["columns"]:
            if column in {"Unit","Value unit","T","C","Z"}:continue
            name=aliases.get(column,column)
            if name not in expected:assert column not in row;continue
            value=expected[name]
            if isinstance(value,str):assert row[column]==value
            else:same(row[column],value)
    for row in report["rows"]:
        assert row["T"]==row["C"]==1 and row["Z"]==(2 if profile==85 else 1)
        assert isinstance(row["Value unit"],str)
    assert report["additional_measure_reads"]==(0 if profile==81 else 1)
    assert report["cancel_preserved"] and report["workspace_preserved"] and report["source_unchanged_by_measure"]
    if profile==68:assert report["threshold_setting_checked"] and report["threshold_stale_checked"] and report["threshold_preference_stale_checked"]
    if profile==72:assert report["fitted"] and report["fit_undo_steps"]==1
    if profile==85:assert report["threshold_native_off_grid"] and report["rows"][0]["Z"]==2
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==len(case["rows"])
    for emitted,stored in zip(rows,report["rows"]):
        for key in ["Value unit","T","C","Z"]:assert emitted[key]==str(stored[key])
    for row,expected in zip(rows,case["rows"]):
        for column in report["columns"]:
            if column in {"Unit","Value unit","T","C","Z"}:continue
            name=aliases.get(column,column)
            if name not in expected:assert row[column]=="";continue
            value=expected[name]
            if isinstance(value,str):assert row[column]==value
            else:assert float(row[column])==pytest.approx(value,abs=1e-6,rel=1e-12)
