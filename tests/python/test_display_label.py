"""Original captured Analyzer labels; UTF8 projection, metadata and both host consumers."""
import json
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=json.loads((Path(__file__).parents[1]/"fixtures/display-label-imagej154r.json").read_bytes())
NAME="imagej_roi_name"


def prepare(case,provider="planes_property",manager=False):
    t,c,z=case["shape"];position=ij.PlanePosition(*case["position"])
    pixels=np.fromfunction(lambda a,b,d,y,x:10*a+3*d+b+1,(t,c,z,3,4),dtype=int).astype(np.uint8)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,axes="TCZYX").observe_reads(reads),case["source_title"])
    edit=owner.begin_edit();planes=provider.startswith("planes");key="plane_labels" if planes else "slice_labels"
    labels=[f"other-{a}-{b}-{d}.tif" for a in range(t) for b in range(c) for d in range(z)] if planes else ["unselected.tif"]*z
    pt,pc,pz=case["position"];labels[(pt*c+pc)*z+pz if planes else pz]=case["slice_label"]
    if provider.endswith("extension"):edit.set_extension(key,labels)
    else:edit.set_property(key,labels)
    owner.commit(edit);view=owner.snapshot().display;view.t=pt;view.c=pc;view.crosshair=[0,0,pz];owner.set_display(view)
    kind=case["kind"];selection=None
    if kind!=5:
        name,points={0:("rectangle",[(0,0),(3,2)]),1:("line",[(0,0),(2,1)]),2:("angle",[(0,0),(1,1),(2,0)]),3:("point",[(1,1)]),4:("multipoint",[(1,1),(2,1)])}[kind]
        selection=ij.Selection(name,points)
        if kind==4:selection=ij.with_point_annotations(selection,[ij.PointAnnotation(0,ij.PlanePosition(0,0,0)),ij.PointAnnotation(0,ij.PlanePosition(t-1,c-1,z-1))])
        if case["roi_name"] is not None:selection=selection.with_attributes({**selection.attributes,NAME:case["roi_name"]})
        owner.select(selection)
    if manager:
        edit=owner.begin_edit();edit.set_rois([ij.ROI(case["roi_name"],selection,t=pt,c=pc,z=pz)]);owner.commit(edit)
    other_reads=ij.DatasetReadObserver();other_shape=(1,1,1) if case["mode"]==3 else (t,c,z)
    other=ij.Document(ij.Dataset(np.ones((*other_shape,3,4),np.uint8),axes="TCZYX").observe_reads(other_reads),case["target_title"])
    other_view=other.snapshot().display;other_view.t=other_shape[0]-1;other_view.c=other_shape[1]-1;other_view.crosshair=[0,0,other_shape[2]-1];other.set_display(other_view)
    app=ij.Application();app.adopt(owner);app.adopt(other);app.activate(owner.id)
    request=ij.roi_measurement_target_request(owner.snapshot()) if manager else ij.TargetRequest()
    options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.ROI_MANAGER if manager else ij.TargetMeasurementMode.SELECTION;options.display_label=True
    sampled=owner if case["mode"]==1 else other
    capture=ij.TargetRequest();capture.roi=ij.RoiPolicy.WHOLE_PLANE
    if case["mode"]:options.redirect=app.resolve_target(capture,sampled.id)
    command=ij.resolve_target_analysis(app.resolve_target(request,owner.id),options)
    return app,owner,sampled,reads,other_reads,command


@pytest.mark.parametrize("case",REFERENCE["cases"],ids=lambda c:c["id"])
def test_original_captured_labels_and_unchanged_source(case):
    app,owner,sampled,reads,other_reads,command=prepare(case)
    assert reads.counts().plane_reads==other_reads.counts().plane_reads==0
    before=owner.snapshot();history=owner.undo_steps
    exposed=command.options;exposed.display_label=False;assert command.options.display_label and command.record["version"]==20
    result=ij.execute_target_analysis(command);assert len(result.planes)==len(case["rows"])
    for row,expected in zip(result.planes,case["rows"]):
        assert row.label==expected["label_utf8"]
        with pytest.raises(AttributeError):row.label=None
    assert owner.snapshot().shares_data(before) and owner.undo_steps==history


TAIL=[c for c in REFERENCE["cases"] if c["id"] in ["label-layout3-mode3-name2-slice2-kind4-title0","label-layout2-mode3-name7-slice10-kind0-title0","label-layout1-mode0-name0-slice2-kind4-title0"]]
@pytest.mark.parametrize("case",TAIL,ids=lambda c:c["id"])
@pytest.mark.parametrize("provider",["planes_extension","slices_property","slices_extension"])
def test_metadata_providers_and_parent_point_label(case,provider):
    *_,command=prepare(case,provider)
    assert [x.label for x in ij.execute_target_analysis(command).planes]==[x["label_utf8"] for x in case["rows"]]


@pytest.mark.parametrize("mode",[0,1,2,3])
def test_manager_label_uses_row_name_and_captured_pose(mode):
    case=next(c for c in REFERENCE["cases"] if c["id"]==f"label-layout3-mode{mode}-name2-slice2-kind0-title0")
    *_,command=prepare(case,manager=True)
    assert ij.execute_target_analysis(command).planes[0].label==case["rows"][0]["label_utf8"]


def test_default_generic_guards_and_consumed_metadata_stale():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((2,3,4),np.uint8),axes="ZYX").observe_reads(reads),"image")
    app=ij.Application();app.adopt(owner);options=ij.TargetAnalysisOptions();assert not options.display_label
    options.measurement=ij.TargetMeasurementMode.SELECTION;assert ij.execute_target_analysis(ij.resolve_target_analysis(app.resolve_target(),options)).planes[0].label is None
    options.display_label=True;command=ij.resolve_target_analysis(app.resolve_target(),options)
    before_reads=reads.counts().plane_reads;edit=owner.begin_edit();edit.set_title("renamed");owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):command.validate_source(owner.snapshot())
    assert reads.counts().plane_reads==before_reads
    options.measurement=ij.TargetMeasurementMode.AREA
    with pytest.raises(ValueError,match="Display label"):ij.resolve_target_analysis(app.resolve_target(),options)


@pytest.mark.parametrize("bad",[["bad"],[None,7],[None,False],None,"not an array"])
def test_bad_consumed_label_metadata_refuses_before_reads(bad):
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((2,3,4),np.uint8),axes="ZYX").observe_reads(reads))
    edit=owner.begin_edit();edit.set_property("slice_labels",bad);owner.commit(edit)
    app=ij.Application();app.adopt(owner);options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.display_label=True
    # Select the malformed second entry where this is an extent-correct array.
    if isinstance(bad,list) and len(bad)==2:
        view=owner.snapshot().display;view.crosshair=[0,0,1];owner.set_display(view)
    with pytest.raises(ValueError):ij.resolve_target_analysis(app.resolve_target(),options)
    assert reads.counts().plane_reads==0


def test_label_storage_work_admission_precedence_and_defaults():
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(np.ones((2,3,4),np.uint8),axes="ZYX").observe_reads(reads),"image")
    edit=owner.begin_edit();edit.set_property("plane_labels",["property.tif",None]);edit.set_extension("plane_labels",["extension.tif",None]);edit.set_property("slice_labels",["slice.tif","unused"]);owner.commit(edit)
    app=ij.Application();app.adopt(owner);options=ij.TargetAnalysisOptions();options.measurement=ij.TargetMeasurementMode.SELECTION;options.display_label=True
    target=app.resolve_target();command=ij.resolve_target_analysis(target,options);assert reads.counts().plane_reads==0
    limited=ij.TargetAnalysisLimits();limited.max_working_bytes=command.estimate.working_bytes-1
    with pytest.raises(ValueError):ij.resolve_target_analysis(target,options,limited)
    limited=ij.TargetAnalysisLimits();limited.max_measurement_work=1
    with pytest.raises(ValueError):ij.resolve_target_analysis(target,options,limited)
    assert reads.counts().plane_reads==0
    assert ij.execute_target_analysis(command).planes[0].label=="image:property"
    choices=next(x["choices"] for x in ij.Settings.schema() if x["name"]=="measurements");assert "Label" in choices


def test_actual_label_preferences_measurement_copy_save_and_workspace(tmp_path):
    import csv
    from imagejxx import _desktop as desktop
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"actual"
    report=json.loads(editor._display_label_ui_case(str(directory)))
    assert report["workspace_preserved"] and report["cancel_preserved"] and report["sampling_close_preserved"] and report["stale_setting_refused"] and report["source_unchanged"]
    rows=report["rows"];assert len(rows)==4 and "Label" not in rows[0]
    assert rows[1]["Label"]==rows[2]["Label"]=='source,"title\nnext:roi,name:slice1'
    assert rows[3]["Label"]=='target,title:roi,name:slice1'
    assert report["csv"]==report["copied_csv"]
    with (directory/"measure.csv").open(newline="",encoding="utf-8") as stream:emitted=list(csv.DictReader(stream))
    assert [x["Label"] for x in emitted]==["",rows[1]["Label"],rows[2]["Label"],rows[3]["Label"]]
    assert (directory/"measure.csv").read_bytes()==(directory/"reopened.csv").read_bytes()
