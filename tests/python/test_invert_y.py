"""Captured report coordinates, independent physical geometry and actual ImGui controls."""
import csv
import json
import math
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/invert-y-imagej154r.json").read_bytes())


def spatial(calibration,inverted,height=6):
    sx,sy,ox,oy=[(1,1,0,0),(2,.5,3.25,-1.5),(.75,2,0,4.25),(1,2,0,1.25)][calibration]
    return ij.SpatialCalibration(spacing=[sx,sy,1],origin=[-ox*sx,((oy if oy else height-1) if inverted else -oy)*sy,0],
        direction=[1,0,0,0,-1 if inverted else 1,0,0,0,1],unit="um")


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_report_coordinates_preserve_geometry_and_statistics(case):
    reads=ij.DatasetReadObserver();y,x=np.indices((6,8));pixels=(x+2*y+1).astype(np.uint8)
    owner=ij.Document(ij.Dataset(pixels,axes="YX",calibration=spatial(case["calibration"],case["local_invert"])).observe_reads(reads))
    if case["points"]:owner.select(ij.Selection(case["kind"],case["points"]))
    h=case["target_height"];ty,tx=np.indices((h,8))
    other=ij.Document(ij.Dataset((tx+2*ty+21).astype(np.uint8),axes="YX",calibration=spatial((case["calibration"]+1)%3,not case["local_invert"],h)))
    app=ij.Application();app.adopt(owner);app.adopt(other);app.activate(owner.id)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.invert_y=case["invert"]
    area=case["kind"] in ["whole_image","rectangle","ellipse","polygon"]
    options.bounding_rectangle=options.feret=area;options.ellipse=area and case["fitted"]
    capture=ij.TargetRequest();capture.roi=ij.RoiPolicy.WHOLE_PLANE
    if case["mode"]:options.redirect=app.resolve_target(capture,owner.id if case["mode"]==1 else other.id)
    target=app.resolve_target();before=owner.snapshot();history=owner.undo_steps
    if case["mode"]==3:
        # Existing native redirect contract requires equal XY extents. Original
        # point/line bypass variants remain an explicit M3 residual, not parity.
        with pytest.raises(ValueError):ij.resolve_target_analysis(target,options)
        assert reads.counts().plane_reads==0
        return
    command=ij.resolve_target_analysis(target,options);assert reads.counts().plane_reads==0
    if case["invert"]:assert command.record["version"]==21 and command.record["options"]["invert_y"]
    exposed=command.options;exposed.invert_y=not case["invert"];assert command.options.invert_y==case["invert"]
    result=ij.execute_target_analysis(command);assert len(result.planes)==len(case["rows"])
    options.invert_y=False;baseline=ij.execute_target_analysis(ij.resolve_target_analysis(target,options))
    for row,old,expected in zip(result.planes,baseline.planes,case["rows"]):
        raw=row.point_world or row.midpoint_world or row.centroid_world
        if raw is not None:
            assert (row.reported_y if case["invert"] else raw[1])==pytest.approx(expected["Y"],abs=1e-10)
            if expected["Y"]==0:assert math.copysign(1,row.reported_y if case["invert"] else raw[1])==math.copysign(1,expected["Y"])
        else:assert row.reported_y is None
        if area:
            assert (row.reported_center_of_mass_y if case["invert"] else row.center_of_mass_world[1])==pytest.approx(expected["YM"],abs=1e-10)
            assert (row.reported_bounding_y if case["invert"] else row.bounding_y)==pytest.approx(expected["BY"],abs=1e-10)
        for name in ["point_world","midpoint_world","centroid_world","center_of_mass_world","bounding_origin_world","feret_y","feret_angle"]:
            assert getattr(row,name)==getattr(old,name)
        for name in ["reported_y","reported_center_of_mass_y","reported_bounding_y"]:
            assert getattr(old,name) is None
            with pytest.raises(AttributeError):setattr(row,name,123)
        assert row.statistics["mean"]==old.statistics["mean"] and row.statistics["area"]==old.statistics["area"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history


def test_native_affine_reporting_guard_admission_and_defaults():
    reads=ij.DatasetReadObserver();cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[7,11,13],direction=[0,-1,0,1,0,0,0,0,1])
    owner=ij.Document(ij.Dataset(np.ones((3,4,6),np.uint8),axes="ZYX",calibration=cal).observe_reads(reads));owner.select(ij.Selection("rectangle",[[1,1],[5,3]]))
    app=ij.Application();app.adopt(owner);options=ij.TargetAnalysisOptions();assert not options.invert_y
    options.invert_y=True
    with pytest.raises(ValueError,match="Invert Y"):ij.resolve_target_analysis(app.resolve_target(),options)
    options.measurement=ij.TargetMeasurementMode.SELECTION;command=ij.resolve_target_analysis(app.resolve_target(),options)
    limit=ij.TargetAnalysisLimits();limit.max_working_bytes=command.estimate.working_bytes-1
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options,limit)
    assert reads.counts().plane_reads==0
    row=ij.execute_target_analysis(command).planes[0]
    assert row.centroid_world==[1,17,13] and row.reported_y==-17
    assert ij.Settings()["measurement_invert_y"] is False


def test_nonfinite_report_summary_and_runtime_admission_preserve_geometry_contract():
    cal=ij.SpatialCalibration(spacing=[2,3,5],origin=[7,11,13],direction=[0,-1,0,1,0,0,0,0,1])
    with pytest.raises(ValueError):cal.voxel_to_world([float("inf"),0,0])
    pixels=np.tile([-1,1],(2,2)).astype(np.float32);reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(np.stack([pixels,pixels,pixels]),axes="ZYX",calibration=cal).observe_reads(reads))
    view=owner.snapshot().display;view.crosshair=[0,0,2];owner.set_display(view)
    app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.invert_y=True
    command=ij.resolve_target_analysis(app.resolve_target(),options)
    calls=[]
    with pytest.raises(ValueError):ij.execute_target_analysis(command,runtime_max_working_bytes=command.estimate.working_bytes-1,checkpoint=lambda:calls.append(1))
    assert not calls and reads.counts().plane_reads==0
    row=ij.execute_target_analysis(command).planes[0]
    assert np.isnan(row.center_of_mass_world[0]) and np.isposinf(row.center_of_mass_world[1])
    assert np.isneginf(row.reported_center_of_mass_y)
    assert row.centroid_world==[4,15,23] and row.reported_y==-15


def test_ordered_mixed_manager_capture_groups_reads_without_history():
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(np.ones((6,8),np.uint8),axes="YX").observe_reads(reads))
    edit=owner.begin_edit();edit.set_rois([
        ij.ROI("area",ij.Selection("rectangle",[[1,1],[7,5]])),
        ij.ROI("point",ij.Selection("point",[[2.5,1.25]]))]);owner.commit(edit)
    before=owner.snapshot();history=owner.undo_steps
    app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER;options.invert_y=True
    target=app.resolve_target(ij.roi_measurement_target_request(before))
    command=ij.resolve_target_analysis(target,options);assert reads.counts().plane_reads==0
    result=ij.execute_target_analysis(command)
    assert [r.roi_index for r in result.planes]==[0,1]
    assert [r.reported_y for r in result.planes]==[2,3.75]
    assert reads.counts().plane_reads==1
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history


def test_original_positive_zero_reaches_visible_table_and_csv(tmp_path):
    from imagejxx import _desktop
    cal=ij.SpatialCalibration(spacing=[1,2,1],origin=[0,-2.5,0])
    owner=ij.Document(ij.Dataset(np.ones((6,8),np.uint8),axes="YX",calibration=cal))
    owner.select(ij.Selection("point",[[2.5,1.25]]))
    editor=_desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"measurement_invert_y":True,"measurement_precision":3,"measurements":["Y"]}),False)
    editor.action("measure","{}");editor.wait()
    value=json.loads(editor.results)[0]["Y"]
    assert value==0 and math.copysign(1,value)==1
    report=json.loads(editor.render_headless())
    assert next(x["text"] for x in report["results_table"]["cells"] if x["name"]=="Y" and x["row"]==0)=="0.000"
    output=tmp_path/"zero.csv";editor.action("save_results",json.dumps({"path":str(output)}))
    with output.open(newline="",encoding="utf-8") as stream:assert list(csv.reader(stream))==[["Y"],["0.000"]]


def test_actual_invert_y_preferences_measurement_outputs_and_workspace(tmp_path):
    from imagejxx import _desktop
    editor=_desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"actual"
    report=json.loads(editor._invert_y_ui_case(str(directory)))
    assert all(report[x] for x in ["workspace_preserved","cancel_preserved","sampling_close_preserved","stale_setting_refused","source_unchanged"])
    assert [x["Y"] for x in report["rows"]]==[.5,1.5,1.5,-8]
    assert report["rows"][1]["YM"]==1.5 and report["rows"][1]["BY"]==2
    assert report["rows"][3]["YM"]==-8 and report["rows"][3]["BY"]==-7
    assert report["csv"]==report["copied_csv"]
    assert (directory/"measure.csv").read_bytes()==(directory/"reopened.csv").read_bytes()
