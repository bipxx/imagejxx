"""Calibration standards exchange uses native parsing and guarded fit drafts."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij
from imagejxx.ui.app import Editor
from imagejxx.ui.persistence import Preferences


def fixture(tmp_path,kind="polynomial"):
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(np.zeros((2,3),np.uint8),axes="YX").observe_reads(reads),"standards")
    editor=Editor(preferences=Preferences(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.action("show_intensity_"+kind+"_fit")
    return owner,editor,reads


@pytest.mark.parametrize("kind",["polynomial","nonlinear"])
def test_point_import_updates_only_captured_fit_draft(kind,tmp_path):
    owner,editor,reads=fixture(tmp_path,kind);before=owner.snapshot();count=reads.counts().plane_reads
    path=tmp_path/"standards-碌.txt";path.write_text("Measured,Known\r\n16777217,0.1\r\n+0x1.8p1f,-0\n")
    editor.action("import_intensity_points",path=str(path))
    values=editor.render_headless(4)["dialog"]["values"]
    assert json.loads(values["measured"])==[16777216,3]
    known=json.loads(values["known"]);assert known[0]==float(np.float32(.1)) and np.signbit(known[1])
    # Rendering may acquire the displayed plane; import itself must not.
    current=reads.counts().plane_reads
    path.write_text("Known\n5\n7\n")
    editor.action("import_intensity_points",path=str(path));assert reads.counts().plane_reads==current
    single=editor.render_headless(4)["dialog"]["values"]
    assert json.loads(single["measured"])==[16777216,3] and json.loads(single["known"])==[5,7]
    export=tmp_path/"saved-碌.txt";editor.action("export_intensity_points",path=str(export))
    assert export.read_text()=="16777216\t5\n3\t7\n"
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0 and not editor.busy


@pytest.mark.parametrize("text",["1\n","1 2 3\n4 5 6","1 2\n3","NaN\n1\n2","1e9999\n1\n2","1\ninvalid\n2","1\n"*4097," "*(1024*1024+1)],ids=["short","wide","ragged","nan","overflow","invalid","too-many","too-large"])
def test_invalid_point_file_does_not_replace_draft(text,tmp_path):
    owner,editor,reads=fixture(tmp_path);before=owner.snapshot();draft=editor.render_headless(4)["dialog"]["values"];count=reads.counts().plane_reads
    path=tmp_path/"bad.txt";path.write_text(text)
    with pytest.raises((ValueError,RuntimeError)):editor.action("import_intensity_points",path=str(path))
    assert reads.counts().plane_reads==count
    assert editor.render_headless(4)["dialog"]["values"]==draft
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0


def test_stale_draft_refuses_import_and_export_before_files_change(tmp_path):
    owner,editor,_=fixture(tmp_path);path=tmp_path/"points.txt";path.write_text("1\t2\n3\t4\n")
    edit=owner.begin_edit();edit.set_title("changed");owner.commit(edit)
    for action in ["import_intensity_points","export_intensity_points"]:
        with pytest.raises((ValueError,RuntimeError)):editor.action(action,path=str(path))
    assert path.read_text()=="1\t2\n3\t4\n"



@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_physical_point_exchange_and_fit(context,tmp_path):
    if context=="imgui_cpp":
        path=Path(os.environ["IMAGEJXX_CALIBRATION_POINTS_CPP_REPORT"]);report=json.loads(path.read_text());directory=path.parent
    else:
        editor=Editor(preferences=Preferences(tmp_path/"prefs.json"));directory=tmp_path/"case"
        report=json.loads(editor._editor._calibration_points_ui_case(str(directory)))
    assert report["context"]==context and report["error"]=="" and report["frame_count"]<160
    assert report["initial_draft"]==report["after_cancel"]
    assert json.loads(report["pairs_draft"]["measured"])==[0,1,2]
    assert json.loads(report["pairs_draft"]["known"])==[1,3,5]
    assert json.loads(report["known_draft"]["measured"])==[0,1,2]
    assert json.loads(report["known_draft"]["known"])==[2,4,6]
    assert (directory/"saved-µ.txt").read_text()=="0\t2\n1\t4\n2\t6\n"
    assert report["before"]==report["before_fit"]
    assert report["fitted"]["undo"]==report["before"]["undo"]+1
    record=report["fitted"]["properties"]["imagejxx.intensity_calibration"]
    np.testing.assert_allclose(record["source_parameters"]["coefficients"],[2,2],rtol=1e-12,atol=1e-12)
    assert report["fitted"]["pixels"]==report["before"]["pixels"]
    assert report["fresh_before"]==report["fresh_after"]
    for name in ["remembered_nonlinear","remembered_polynomial"]:
        assert json.loads(report[name]["measured"])==[0,1,2]
        assert json.loads(report[name]["known"])==[2,4,6]
    # Editable text is recorded separately from the initialized values object.
    edited=[json.loads(frame["dialog"]["text"]["known"]) for frame in report["frames"]
            if frame["dialog"]["open"] and frame["dialog"]["title"]=="Fit Nonlinear Calibration"]
    assert [9,9,9] in edited
    assert all(frame["error"]=="" for frame in report["frames"])


def reference_cases():
    return json.loads((Path(__file__).parents[2]/"docs/parity/reviews/G034-calibration-points-reference001.json").read_text())["cases"]


@pytest.mark.parametrize("case",reference_cases(),ids=lambda case:case["id"])
def test_point_import_matches_pinned_textreader_values(case,tmp_path):
    owner,editor,_=fixture(tmp_path);path=tmp_path/"reference.txt";path.write_text(case["text"])
    image=case["image"]
    valid=image is not None and image["rows"]>=2 and np.isfinite(np.array(image["words"],np.uint32).view(np.float32)).all()
    if not valid:
        with pytest.raises((ValueError,RuntimeError)):editor.action("import_intensity_points",path=str(path))
        return
    editor.action("import_intensity_points",path=str(path))
    draft=editor.render_headless(4)["dialog"]["values"];columns=image["columns"]
    expected=np.array(image["words"],np.uint32).reshape(-1,columns)
    actual=np.array(json.loads(draft["known"]),np.float32).view(np.uint32)
    np.testing.assert_array_equal(actual,expected[:,-1])
    if columns==2:np.testing.assert_array_equal(np.array(json.loads(draft["measured"]),np.float32).view(np.uint32),expected[:,0])
    assert owner.undo_steps==0


def admit_standards(editor,known=(2,4,6),unit="OD"):
    editor.action("fit_intensity_calibration",function="straight_line",measured=[0,1,2],known=list(known),value_unit=unit,show_plot=False)
    editor.wait()


def fresh_source(editor,title="fresh"):
    editor.close_document(discard=True)
    reads=ij.DatasetReadObserver()
    owner=ij.Document(ij.Dataset(np.zeros((2,3),np.uint8),axes="YX").observe_reads(reads),title)
    editor.add_native_document(owner)
    return owner,reads


@pytest.mark.parametrize("kind",["polynomial","nonlinear"])
def test_accepted_standards_survive_source_closure_and_seed_both_dialogs(kind,tmp_path):
    _,editor,_=fixture(tmp_path);admit_standards(editor)
    owner,reads=fresh_source(editor);before=owner.snapshot();count=reads.counts().plane_reads
    editor.action("show_intensity_"+kind+"_fit");assert reads.counts().plane_reads==count
    values=editor.render_headless(4)["dialog"]["values"]
    assert json.loads(values["measured"])==[0,1,2] and json.loads(values["known"])==[2,4,6]
    assert values["value_unit"]=="OD" and owner.snapshot().shares_data(before) and owner.undo_steps==0


def test_rejected_fit_and_unsubmitted_import_preserve_last_standards(tmp_path):
    _,editor,_=fixture(tmp_path);admit_standards(editor)
    owner,_=fresh_source(editor);before=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("fit_intensity_calibration",function="straight_line",measured=[0,1],known=[99],value_unit="bad",show_plot=False)
    editor.action("show_intensity_polynomial_fit")
    path=tmp_path/"unsubmitted.txt";path.write_text("0 9\n1 9\n2 9\n")
    editor.action("import_intensity_points",path=str(path))
    imported=editor.render_headless(4)["dialog"]["values"];assert json.loads(imported["known"])==[9,9,9]
    editor.action("show_intensity_nonlinear_fit")
    values=editor.render_headless(4)["dialog"]["values"]
    assert json.loads(values["known"])==[2,4,6] and values["value_unit"]=="OD"
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0


def test_explicit_document_unit_takes_precedence_over_remembered_default(tmp_path):
    _,editor,_=fixture(tmp_path);admit_standards(editor)
    owner,_=fresh_source(editor)
    ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.uncalibrated("HU"))
    editor.action("show_intensity_polynomial_fit")
    assert editor.render_headless(4)["dialog"]["values"]["value_unit"]=="HU"


def test_standards_do_not_leak_between_editor_sessions(tmp_path):
    _,first,_=fixture(tmp_path);admit_standards(first)
    _,second,_=fixture(tmp_path/"second")
    values=second.render_headless(4)["dialog"]["values"]
    assert json.loads(values["measured"])==[0,1] and json.loads(values["known"])==[0,1]
    assert values["value_unit"]=="Gray Value"


def test_manual_calibration_remembers_unit_for_fresh_default_source(tmp_path):
    _,editor,_=fixture(tmp_path)
    editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.uncalibrated("unit-a").record,show_plot=False)
    owner,_=fresh_source(editor)
    editor.action("show_intensity_function")
    assert editor.render_headless(4)["dialog"]["values"]["value_unit"]=="unit-a"
    assert owner.undo_steps==0 and not ij.read_imagej_intensity_calibration(owner.snapshot()).has_density
