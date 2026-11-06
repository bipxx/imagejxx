"""Original Analyzer redirect columns, plus explicit immutable two-owner guards."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_center_of_mass import same

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/redirect-measurements-imagej154r.json").read_bytes())
POINTS={3:[[1,1],[7,1],[3,5]],4:[[1,1],[6,1],[6,5],[1,5]],5:[[20,20],[26,20],[22,24]],6:[[2,2],[3,2],[4,2]],7:[[1,1],[6,4]],8:[[1,1],[6,1],[6,4]],9:[[1,1],[4,4],[6,1]],10:[[2,2]],11:[[2,2],[6,4]]}


def document(case,destination):
    depth=case["target_depth" if destination else "source_depth"]
    kind=case["type"] if destination else 0
    y,x=np.indices((6,8));values=np.stack([(30+3*x+y if destination else x+2*y)+z*10 for z in range(depth)])
    if kind==1:values=values*257
    if kind==2:values=values*.25-2
    if not destination and case["mode"]==2:values[:]=np.where(x>=3,255,0)
    values=values.astype([np.uint8,np.uint16,np.float32][kind]);axes="ZYX"
    if case["mode"]==3:
        values=values.reshape(2,2,6,8);axes="TZYX" if destination else "ZCYX"
    spatial=ij.SpatialCalibration()
    bit=2 if destination else 1
    if case["spatial"]&bit:
        spatial=ij.SpatialCalibration(spacing=[.5,1.25,1] if destination else [2,3,1],origin=[-1,.625,0] if destination else [-1,-3,0],unit="target_um" if destination else "source_um")
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(values,axes=axes,calibration=spatial).observe_reads(reads),title="target" if destination else "mask" if case["mode"]==2 else "source")
    if case["density"]&bit and kind!=2:
        ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.function(["uint8","uint16"][kind],"straight_line",[5,.25] if destination else [-7,2],"target_density" if destination else "source_density"))
    view=owner.snapshot().display
    if case["mode"]==3:view.c=0;view.t=0;view.crosshair=[4,3,0 if destination else 1]
    else:view.crosshair=[4,3,depth-1 if destination else min(1,depth-1)]
    owner.set_display(view)
    if case["threshold_case"]&bit:
        low,high=(40.4,46.4) if destination else (6.4,10.4)
        if destination and kind==1:low*=257;high*=257
        if destination and kind==2:low=low*.25-2;high=high*.25-2
        owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(low,high)))
    return owner,reads


def prepare(case):
    source,source_reads=document(case,False);redirect,redirect_reads=document(case,True)
    shape=case["shape"]
    if shape:
        kind="rectangle" if shape==1 else "ellipse" if shape==2 else "polygon" if shape<7 else "line" if shape==7 else "polyline" if shape==8 else "angle" if shape==9 else "point" if shape==10 else "multipoint"
        selection=ij.Selection(kind,[[1,1],[6,5]] if shape<3 else POINTS[shape])
        if shape==4:selection=ij.fit_spline_selection(selection,8,6)
        source.select(selection)
    app=ij.Application();app.adopt(source);app.adopt(redirect)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    assert options.redirect is None
    options.limit_to_threshold=case["limit"];options.ellipse=case["variant"]==1;options.shape_descriptors=case["variant"]==2
    options.bounding_rectangle=options.feret=shape<7
    request=ij.TargetRequest();request.roi=ij.RoiPolicy.WHOLE_PLANE
    sampled=source if case["mode"]==1 else redirect
    options.redirect=app.resolve_target(request,sampled.id)
    command=ij.resolve_target_analysis(app.resolve_target(ij.TargetRequest(),source.id),options)
    return app,source,redirect,source_reads,redirect_reads,command


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_redirect_columns_and_populations(case):
    app,source,redirect,source_reads,redirect_reads,command=prepare(case)
    assert source_reads.counts().plane_reads==redirect_reads.counts().plane_reads==0
    sampled=source if case["mode"]==1 else redirect
    before,other=source.snapshot(),redirect.snapshot();undo=(source.undo_steps,redirect.undo_steps)
    command.validate_source(before);command.validate_redirect(sampled.snapshot())
    app.validate_target(command.target);app.validate_target(command.options.redirect)
    detached=command.options;detached.redirect=None
    result=ij.execute_target_analysis(command);assert command.options.redirect is not None
    assert result.spatial.unit==sampled.snapshot().dataset.calibration.unit
    assert result.record["spatial"]["unit"]==source.snapshot().dataset.calibration.unit
    assert len(result.planes)==len(case["rows"]) and command.record["version"]==17
    for row,expected in zip(result.planes,case["rows"]):
        for field,column in [("area","Area"),("mean","Mean"),("stddev","StdDev"),("minimum","Min"),("maximum","Max"),("median","Median"),("mode_value","Mode"),("integrated_density","IntDen")]:same(row.statistics[field],expected[column])
        if row.point_world is not None:coordinates=row.point_world
        elif row.centroid_world is not None:coordinates=row.centroid_world
        elif row.midpoint_world is not None:coordinates=row.midpoint_world
        else:coordinates=[0,0,0]
        same(coordinates[0],expected["X"]);same(coordinates[1],expected["Y"])
        if row.center_of_mass_world is not None:
            same(row.center_of_mass_world[0],expected["XM"]);same(row.center_of_mass_world[1],expected["YM"])
        if row.perimeter is not None:same(row.perimeter,expected["Perim."])
        if row.length is not None:same(row.length,expected["Length"])
        if case["shape"]<7:
            for field,column in [("bounding_x","BX"),("bounding_y","BY"),("bounding_width","Width"),("bounding_height","Height"),("feret","Feret"),("min_feret","MinFeret"),("feret_angle","FeretAngle"),("feret_x","FeretX"),("feret_y","FeretY")]:same(getattr(row,field),expected[column])
        if case["variant"]==1:
            for field,column in [("ellipse_major","Major"),("ellipse_minor","Minor"),("ellipse_angle_degrees","Angle")]:same(getattr(row,field),expected[column])
        if case["variant"]==2:
            for field,column in [("circularity","Circ."),("aspect_ratio","AR"),("roundness","Round"),("solidity","Solidity")]:same(getattr(row,field),expected[column])
        if case["limit"]:same(row.threshold_lower,expected["MinThr"]);same(row.threshold_upper,expected["MaxThr"])
        else:assert row.threshold_lower is row.threshold_upper is None
        expected_slice=case["source_slice"] if case["mode"]==1 else case["target_slice_after"]
        shape=sampled.snapshot().dataset.shape;p=row.sampling_position
        assert (p.t*shape[2]+p.z)*shape[1]+p.c+1==expected_slice
        with pytest.raises(AttributeError):row.sampling_position=None
    assert source.snapshot().shares_data(before) and redirect.snapshot().shares_data(other)
    assert (source.undo_steps,redirect.undo_steps)==undo
    assert source.snapshot().display.crosshair==before.display.crosshair and redirect.snapshot().display.crosshair==other.display.crosshair
    for maximum,token in [(command.estimate.working_bytes-1,None),(command.estimate.working_bytes,ij.CancellationToken())]:
        if token is not None:token.cancel()
        reads=(source_reads.counts().plane_reads,redirect_reads.counts().plane_reads)
        with pytest.raises(ValueError if token is None else ij.OperationCancelled):ij.execute_target_analysis(command,runtime_max_working_bytes=maximum,cancellation=token)
        assert (source_reads.counts().plane_reads,redirect_reads.counts().plane_reads)==reads


def test_redirect_guard_ignores_unused_source_threshold_and_unconsumed_view():
    case=next(c for c in REFERENCE["cases"] if "-populations-" in c["id"] and c["shape"]==1 and c["type"]==0 and c["threshold_case"]==3 and c["variant"]==0)
    app,source,redirect,sr,rr,command=prepare(case)
    source.set_display(ij.set_manual_threshold_state(source.snapshot().display,source.snapshot().dataset,ij.ManualThresholdRange(1,2)))
    command.validate_source(source.snapshot())
    view=redirect.snapshot().display;view.zoom=2;view.pan=[[3,4],[0,0],[0,0]];redirect.set_display(view)
    command.validate_redirect(redirect.snapshot())
    redirect.select(ij.Selection("rectangle",[[0,0],[1,1]]));command.validate_redirect(redirect.snapshot())
    redirect.set_display(ij.set_manual_threshold_state(redirect.snapshot().display,redirect.snapshot().dataset,ij.ManualThresholdRange(1,2)))
    with pytest.raises(ij.RevisionConflict):command.validate_redirect(redirect.snapshot())
    assert sr.counts().plane_reads==rr.counts().plane_reads==0
    same(ij.execute_target_analysis(command).planes[0].statistics["mean"],case["rows"][0]["Mean"])


@pytest.mark.parametrize("bypass",[False,True])
def test_redirect_captures_independent_global_and_bypass(bypass):
    case=next(c for c in REFERENCE["cases"] if "-populations-" in c["id"] and c["shape"]==1 and c["type"]==0 and c["threshold_case"]==3 and c["variant"]==0)
    app,source,redirect,sr,rr,old=prepare(case)
    app.set_global_calibration(ij.GlobalCalibration(ij.SpatialCalibration(spacing=[1.5,2,1],unit="global_um"),ij.ImageJIntensityCalibration.function("uint8","straight_line",[10,2],"global_density"),"uint8"))
    app.set_ignore_global_calibration(True,source.id);app.set_ignore_global_calibration(bypass,redirect.id)
    request=ij.TargetRequest();request.roi=ij.RoiPolicy.WHOLE_PLANE
    options=old.options;options.redirect=app.resolve_target(request,redirect.id)
    command=ij.resolve_target_analysis(app.resolve_target(ij.TargetRequest(),source.id),options)
    assert sr.counts().plane_reads==rr.counts().plane_reads==0
    row=ij.execute_target_analysis(command).planes[0]
    assert row.statistics["pixel_count"]==10
    same(row.statistics["mean"],15.75 if bypass else 96)
    same(row.statistics["area"],6.25 if bypass else 30)
    same(row.perimeter,44)
    assert command.spatial.unit==("target_um" if bypass else "global_um")
    app.validate_target(command.target);app.validate_target(command.options.redirect)
    app.set_ignore_global_calibration(False,source.id)
    with pytest.raises(ij.RevisionConflict):app.validate_target(command.target)
    same(ij.execute_target_analysis(command).planes[0].statistics["mean"],15.75 if bypass else 96)


@pytest.mark.parametrize("change",["metadata","close"])
def test_redirect_owner_guard_and_frozen_execution(change):
    case=next(c for c in REFERENCE["cases"] if "-populations-" in c["id"] and c["shape"]==1 and c["type"]==0 and c["threshold_case"]==3 and c["variant"]==0)
    app,source,redirect,sr,rr,command=prepare(case)
    if change=="close":app.close(redirect.id,discard=True)
    else:
        edit=redirect.begin_edit();edit.set_property("external_redirect_change",True);redirect.commit(edit)
        with pytest.raises(ij.RevisionConflict):command.validate_redirect(redirect.snapshot())
    with pytest.raises(ij.DocumentUnavailable if change=="close" else ij.RevisionConflict):app.validate_target(command.options.redirect)
    same(ij.execute_target_analysis(command).planes[0].statistics["mean"],case["rows"][0]["Mean"])
    assert sr.counts().plane_reads==0 and rr.counts().plane_reads==1


@pytest.mark.parametrize("depth",[1,3])
def test_redirect_line_validates_actual_sample_pose_before_reads(depth):
    source=ij.Document(ij.Dataset(np.zeros((3,6,8),np.uint8),axes="ZYX"))
    source.select(ij.Selection("line",[[1,1],[6,4]]));view=source.snapshot().display;view.crosshair=[4,3,2];source.set_display(view)
    reads=ij.DatasetReadObserver()
    sampled=ij.Document(ij.Dataset(np.zeros((depth,6,8),np.uint8),axes="ZYX",calibration=ij.SpatialCalibration(spacing=[1,1,1e308])).observe_reads(reads))
    app=ij.Application();app.adopt(source);app.adopt(sampled)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    request=ij.TargetRequest();request.roi=ij.RoiPolicy.WHOLE_PLANE;options.redirect=app.resolve_target(request,sampled.id)
    if depth==3:
        with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(ij.TargetRequest(),source.id),options)
        assert reads.counts().plane_reads==0
    else:
        command=ij.resolve_target_analysis(app.resolve_target(ij.TargetRequest(),source.id),options)
        assert reads.counts().plane_reads==0
        row=ij.execute_target_analysis(command).planes[0]
        assert row.sampling_position.z==0 and row.position.z==2 and row.midpoint_world==[3.5,2.5,2]
        assert row.statistics["mean"]==0 and reads.counts().plane_reads==1


@pytest.mark.parametrize("profile",range(10))
def test_physical_redirect_selection_measure_csv_workspace(tmp_path,profile):
    import csv
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"redirect-measurement"
    report=json.loads(editor._redirect_measurement_ui_case(str(directory),profile))
    case=next(c for c in REFERENCE["cases"] if c["id"]==report["reference"])
    expected_rows=case["rows"]*3 if profile==7 else case["rows"]
    assert len(report["rows"])==len(expected_rows)
    aliases={"mean":"Mean","minimum":"Min","maximum":"Max","stddev":"StdDev","median":"Median","integrated_density":"IntDen"}
    for row,expected in zip(report["rows"],expected_rows):
        for column in report["columns"]:
            key=aliases.get(column,column)
            if key in expected:
                if isinstance(expected[key],str):assert row[column]==expected[key]
                else:same(row[column],expected[key])
        assert row["Redirect image"]==("source" if profile==9 else "target")
        assert row["Redirect Z"]==case["source_slice" if profile==9 else "target_slice_after"]
    assert report["actual_selector"] and report["cancel_preserved"] and report["workspace_preserved"] and report["source_unchanged"]
    assert report["additional_measure_reads"]==(2 if profile==6 else 1)
    if profile==0:assert all(report[k] for k in ["none_checked","unsupported_source_refused","stale_redirect_setting","stale_redirect_threshold","stale_global","sampling_close_checked"])
    if profile==7:assert report["source_close_checked"]
    if profile==8:assert report["stale_redirect_position"]
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:rows=list(csv.DictReader(stream))
    assert len(rows)==len(expected_rows)
    for row,expected in zip(rows,expected_rows):
        for column in report["columns"]:
            key=aliases.get(column,column)
            if key not in expected:continue
            value=expected[key]
            if isinstance(value,str):assert row[column]==value
            else:assert float(row[column])==pytest.approx(value,rel=1e-6,abs=1e-6)
