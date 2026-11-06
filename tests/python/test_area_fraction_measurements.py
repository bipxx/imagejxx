"""Original %Area populations, immutable capture and real editor outputs."""
import csv
import json
from pathlib import Path

import numpy as np
import pytest
import imagejxx as ij
from test_center_of_mass import same
from test_threshold_measurements import fixture as threshold_fixture
from test_redirect_measurements import prepare as redirect_fixture

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/area-fraction-measurements-imagej154r.json").read_bytes())


def prepare(case):
    if case["group"]=="redirect":
        app,owner,sampled,reads,other,base=redirect_fixture(case)
        options=base.options;options.area_fraction=True
        sampled=owner if case["mode"]==1 else sampled
        return app,owner,sampled,reads,other,ij.resolve_target_analysis(base.target,options)
    app,owner,reads=threshold_fixture(case)
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION
    options.limit_to_threshold=case["limit"];options.ellipse=case["variant"]==1
    options.shape_descriptors=case["variant"]==2;options.area_fraction=True
    if case["summary"] is not None:options.bounding_rectangle=options.feret=True
    return app,owner,owner,reads,None,ij.resolve_target_analysis(app.resolve_target(),options)


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_area_fraction_without_changing_other_columns(case):
    app,owner,sampled,reads,other,command=prepare(case)
    assert reads.counts().plane_reads==0 and (other is None or other.counts().plane_reads==0)
    before=owner.snapshot();undo=owner.undo_steps
    command.validate_source(before)
    if command.options.redirect is not None:command.validate_redirect(sampled.snapshot())
    exposed=command.options;exposed.area_fraction=False
    assert command.options.area_fraction and command.record["version"]==18
    result=ij.execute_target_analysis(command)
    assert len(result.planes)==len(case["rows"])
    for row,expected in zip(result.planes,case["rows"]):
        same(row.area_fraction,expected["%Area"])
        same(row.statistics["mean"],expected["Mean"])
        same(row.statistics["integrated_density"],expected["IntDen"])
        with pytest.raises(AttributeError):row.area_fraction=None
    assert owner.snapshot().shares_data(before) and owner.undo_steps==undo


@pytest.mark.parametrize("redirect",[False,True])
def test_fraction_consumes_threshold_with_limit_off(redirect):
    case=next(c for c in REFERENCE["cases"] if c["group"]==("redirect" if redirect else "threshold") and not c["limit"] and c["type"]==0 and (c.get("shape")==1 if redirect else c["kind"]=="rectangle") and not c.get("zero_source",False))
    app,owner,sampled,reads,other,command=prepare(case)
    view=sampled.snapshot().display;view.zoom=2;view.pan=[[3,4],[0,0],[0,0]];sampled.set_display(view)
    if redirect:command.validate_redirect(sampled.snapshot())
    else:command.validate_source(sampled.snapshot())
    if redirect:
        owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(1,2)))
        command.validate_source(owner.snapshot())
    sampled.set_display(ij.set_manual_threshold_state(sampled.snapshot().display,sampled.snapshot().dataset,ij.ManualThresholdRange(1,2)))
    with pytest.raises(ij.RevisionConflict):
        if redirect:command.validate_redirect(sampled.snapshot())
        else:command.validate_source(sampled.snapshot())
    assert reads.counts().plane_reads==0 and (other is None or other.counts().plane_reads==0)
    same(ij.execute_target_analysis(command).planes[0].area_fraction,case["rows"][0]["%Area"])


@pytest.mark.parametrize("kind",["line","polyline","angle"])
def test_nonarea_fraction_ignores_unused_threshold_and_flag_off_stays_absent(kind):
    case=next(c for c in REFERENCE["cases"] if c["group"]=="threshold" and c["kind"]==kind and not c["limit"] and not c["zero_source"])
    app,owner,sampled,reads,other,command=prepare(case)
    owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(1,2)))
    command.validate_source(owner.snapshot());assert reads.counts().plane_reads==0
    assert ij.execute_target_analysis(command).planes[0].area_fraction==0
    options=command.options;options.area_fraction=False
    assert ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0].area_fraction is None


def test_generic_mode_refuses_new_option_before_reads():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.zeros((3,4),np.uint8),axes="YX").observe_reads(reads));app=ij.Application();app.adopt(owner)
    options=ij.TargetAnalysisOptions();assert not options.area_fraction;options.area_fraction=True
    with pytest.raises(ValueError,match="Area fraction requires"):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0
    choices=next(x["choices"] for x in ij.Settings.schema() if x["name"]=="measurements")
    assert "%Area" in choices


@pytest.mark.parametrize("dtype",[np.uint8,np.uint16,np.float32])
def test_editor_fraction_independent_of_limit_and_default_columns(tmp_path,dtype):
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"))
    owner=ij.Document(ij.Dataset(np.array([[0,6,6,7,10]],dtype=dtype),axes="YX"))
    owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(6.6,10.6)))
    editor.add_native_document(owner)
    editor.update_settings(json.dumps({"measurements":["%Area","mean","Area"],"measurement_limit_to_threshold":False,"measurement_precision":3}),True)
    editor.action("measure","{}");rows=json.loads(editor.results)
    assert rows[0]["%Area"]==40 and rows[0]["mean"]==5.8 and rows[0]["Area"]==5
    path=tmp_path/"results.csv";editor.action("save_results",json.dumps({"path":str(path)}))
    with path.open(newline="") as stream:assert list(csv.reader(stream))==[["%Area","mean","Area"],["40.000","5.800","5.000"]]
    # Empty preference still enumerates all row keys; it does not implicitly
    # enable new optional computations on future measurements.
    editor.update_settings(json.dumps({"measurements":[]}),False)
    editor.action("measure","{}");assert "%Area" not in json.loads(editor.results)[-1]


@pytest.mark.parametrize("group,profile",[("redirect",p) for p in range(10)]+[("threshold",p) for p in range(68,86)])
def test_actual_fraction_controls_measure_csv_workspace(tmp_path,group,profile):
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"fraction"
    method=editor._redirect_measurement_ui_case if group=="redirect" else editor._subtype_measurement_ui_case
    report=json.loads(method(str(directory),profile))
    identity=report["reference"] if group=="redirect" else report["threshold_reference"]
    case=next(c for c in REFERENCE["cases"] if c["group"]==group and c["id"]=="area-fraction-"+identity)
    expected=case["rows"]*3 if group=="redirect" and profile==7 else case["rows"]
    assert "%Area" in report["columns"] and len(report["rows"])==len(expected)
    for row,reference in zip(report["rows"],expected):
        if isinstance(reference["%Area"],str):assert row["%Area"]==reference["%Area"]
        else:same(row["%Area"],reference["%Area"])
    if group=="redirect" and profile==0:assert report["area_fraction_setting_stale_checked"] and report["statistic_choices_checked"]
    assert report["workspace_preserved"] and report["cancel_preserved"]
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:emitted=list(csv.DictReader(stream))
    for row,reference in zip(emitted,expected):
        if isinstance(reference["%Area"],str):assert row["%Area"]==reference["%Area"]
        else:assert float(row["%Area"])==pytest.approx(reference["%Area"],abs=1e-6,rel=1e-6)


@pytest.mark.parametrize("case",REFERENCE["extreme_cases"],ids=lambda c:c["id"])
@pytest.mark.parametrize("consumer",["api","editor"])
def test_float_threshold_overflow_rounding(tmp_path,case,consumer):
    owner=ij.Document(ij.Dataset(np.array([case["pixels"]],dtype=np.float32),axes="YX"))
    owner.set_display(ij.set_manual_threshold_state(owner.snapshot().display,owner.snapshot().dataset,ij.ManualThresholdRange(*case["range"])))
    if consumer=="api":
        app=ij.Application();app.adopt(owner);options=ij.TargetAnalysisOptions()
        options.measurement=ij.TargetMeasurementMode.SELECTION;options.area_fraction=True
        observed=ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0].area_fraction
    else:
        from imagejxx import _desktop as desktop
        editor=desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(owner)
        editor.update_settings(json.dumps({"measurements":["%Area"],"measurement_limit_to_threshold":False}),False)
        editor.action("measure","{}");observed=json.loads(editor.results)[0]["%Area"]
    assert observed==case["expected"]
