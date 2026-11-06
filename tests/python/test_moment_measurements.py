"""Original ordered Skew/Kurt populations and captured editor workflow."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from test_center_of_mass import same
from test_threshold_measurements import fixture as threshold_fixture
from test_redirect_measurements import prepare as redirect_fixture

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/moment-measurements-imagej154r.json").read_bytes())


def prepare_tail(case,moments=True):
    dtype=[np.uint8,np.uint16,np.float32][case["type"]]
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.array([case["pixels"]],dtype=dtype),axes="YX").observe_reads(reads))
    if case["density"]=="signed16":ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.signed16("Gray Value"))
    elif case["density"]!="raw":
        values=np.arange(256 if case["type"]==0 else 65536,dtype=np.float32)
        values[1]=dict(nan=np.nan,inf=np.inf,negative_inf=-np.inf)[case["density"]]
        density=ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration",version=1,mode="lookup",value_unit="tail",bit_depth=8 if case["type"]==0 else 16,table_f32_hex="".join(f"{int(x):08x}" for x in values.view(np.uint32)),source_parameters={},signed16=False))
        ij.set_imagej_intensity_calibration(owner,density)
    if case["range"] is not None:owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(*case["range"])))
    if case["point"]:owner.select(ij.Selection("point",[[0,0]]))
    app=ij.Application();app.adopt(owner);options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    options.limit_to_threshold=case["limit"];options.area_fraction=True
    if moments:options.moments=True
    command=ij.resolve_target_analysis(app.resolve_target(),options)
    return app,owner,reads,command


ZERO_CASES=[c for c in REFERENCE["tail_cases"] if c["id"] in ["moment-tail-type2-pattern0-range1-limittrue-raw-pointfalse","moment-tail-type2-pattern0-range3-limittrue-raw-pointfalse"]]
@pytest.mark.parametrize("case",ZERO_CASES,ids=lambda c:c["id"])
def test_float_zero_threshold_center_of_mass_matches_original(case):
    app,owner,reads,command=prepare_tail(case,moments=False)
    assert reads.counts().plane_reads==0
    row=ij.execute_target_analysis(command).planes[0]
    same(row.center_of_mass_world[0],case["rows"][0]["XM"])
    same(row.center_of_mass_world[1],case["rows"][0]["YM"])


def prepare(case):
    if case["group"]=="redirect":
        app,owner,other,reads,other_reads,base=redirect_fixture(case)
        options=base.options;options.moments=True;options.area_fraction=True
        sampled=owner if case["mode"]==1 else other
        return app,owner,sampled,reads,ij.resolve_target_analysis(base.target,options)
    app,owner,reads=threshold_fixture(case)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    options.moments=True;options.area_fraction=True;options.limit_to_threshold=case["limit"]
    options.ellipse=case["variant"]==1;options.shape_descriptors=case["variant"]==2
    if case["summary"] is not None:options.bounding_rectangle=options.feret=True
    return app,owner,owner,reads,ij.resolve_target_analysis(app.resolve_target(),options)


def check_row(row,expected):
    same(row.skewness,expected["Skew"]);same(row.kurtosis,expected["Kurt"])
    same(row.statistics["mean"],expected["Mean"]);same(row.statistics["integrated_density"],expected["IntDen"])
    same(row.area_fraction,expected["%Area"])
    for field in ["skewness","kurtosis"]:
        with pytest.raises(AttributeError):setattr(row,field,None)


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_moments_and_unchanged_existing_populations(case):
    app,owner,sampled,reads,command=prepare(case)
    assert reads.counts().plane_reads==0;before=owner.snapshot();undo=owner.undo_steps
    command.validate_source(before)
    if command.options.redirect is not None:command.validate_redirect(sampled.snapshot())
    exposed=command.options;exposed.moments=False;assert command.options.moments and command.record["version"]==19
    result=ij.execute_target_analysis(command);assert len(result.planes)==len(case["rows"])
    for row,expected in zip(result.planes,case["rows"]):check_row(row,expected)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==undo


@pytest.mark.parametrize("case",REFERENCE["tail_cases"],ids=lambda c:c["id"])
def test_original_moment_tails_and_shared_center_of_mass(case):
    app,owner,reads,command=prepare_tail(case)
    assert reads.counts().plane_reads==0;before=owner.snapshot();undo=owner.undo_steps
    row=ij.execute_target_analysis(command).planes[0];expected=case["rows"][0];check_row(row,expected)
    if not case["point"]:
        same(row.center_of_mass_world[0],expected["XM"]);same(row.center_of_mass_world[1],expected["YM"])
    assert reads.counts().plane_reads==1 and owner.snapshot().shares_data(before) and owner.undo_steps==undo


@pytest.mark.parametrize("limit",[False,True])
def test_moment_only_threshold_dependency_follows_limit(limit):
    case=next(c for c in REFERENCE["cases"] if c["group"]=="threshold" and c["kind"]=="rectangle" and c["type"]==0 and c["limit"]==limit and not c["zero_source"])
    app,owner,reads=threshold_fixture(case);options=ij.TargetAnalysisOptions();assert not options.moments
    options.measurement=ij.TargetMeasurementMode.SELECTION;options.moments=True;options.limit_to_threshold=limit
    command=ij.resolve_target_analysis(app.resolve_target(),options)
    owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(1,2)))
    if limit:
        with pytest.raises(ij.RevisionConflict):command.validate_source(owner.snapshot())
    else:command.validate_source(owner.snapshot())
    assert reads.counts().plane_reads==0
    assert ij.execute_target_analysis(command).planes[0].skewness is not None
    options.moments=False;row=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0]
    assert row.skewness is row.kurtosis is None


def test_generic_mode_refuses_moments_before_reads():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((3,4),np.uint8),axes="YX").observe_reads(reads));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();options.moments=True
    with pytest.raises(ValueError,match="Moments require"):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0
    choices=next(x["choices"] for x in ij.Settings.schema() if x["name"]=="measurements")
    assert {"Skew","Kurt"}<=set(choices)


@pytest.mark.parametrize("columns",[["Skew"],["Kurt"],["Skew","Kurt"]])
def test_editor_independent_moment_choices_and_csv(tmp_path,columns):
    import csv
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));owner=ij.Document(ij.Dataset(np.array([[0,1,1,4]],np.uint8),axes="YX"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"measurements":columns,"measurement_precision":6}),True)
    editor.action("measure","{}");row=json.loads(editor.results)[0]
    assert row["Skew"]==pytest.approx(0.8888888888888888) and row["Kurt"]==pytest.approx(-0.8148148148148149)
    path=tmp_path/"moments.csv";editor.action("save_results",json.dumps({"path":str(path)}))
    with path.open(newline="") as stream:emitted=list(csv.reader(stream))
    assert emitted==[columns,[("0.888889" if key=="Skew" else "-0.814815") for key in columns]]
    editor.update_settings(json.dumps({"measurements":[]}),False);editor.action("measure","{}")
    assert "Skew" not in json.loads(editor.results)[-1] and "Kurt" not in json.loads(editor.results)[-1]


@pytest.mark.parametrize("group,profile",[("redirect",p) for p in range(10)]+[("threshold",p) for p in range(68,86)])
def test_actual_moment_controls_measure_csv_workspace(tmp_path,group,profile):
    import csv
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"moments"
    method=editor._redirect_measurement_ui_case if group=="redirect" else editor._subtype_measurement_ui_case
    report=json.loads(method(str(directory),profile));identity=report["reference"] if group=="redirect" else report["threshold_reference"]
    case=next(c for c in REFERENCE["cases"] if c["group"]==group and c["id"]=="moments-"+identity)
    expected=case["rows"]*3 if group=="redirect" and profile==7 else case["rows"]
    assert {"Skew","Kurt"}<=set(report["columns"]) and len(report["rows"])==len(expected)
    for row,reference in zip(report["rows"],expected):
        for key in ["Skew","Kurt"]:
            if isinstance(reference[key],str):assert row[key]==reference[key]
            else:same(row[key],reference[key])
    if group=="redirect" and profile==0:assert report["moments_setting_stale_checked"] and report["statistic_choices_checked"]
    assert report["workspace_preserved"] and report["cancel_preserved"]
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:emitted=list(csv.DictReader(stream))
    for row,reference in zip(emitted,expected):
        for key in ["Skew","Kurt"]:
            if isinstance(reference[key],str):assert row[key]==reference[key]
            else:assert float(row[key])==pytest.approx(reference[key],abs=1e-6,rel=1e-6)
