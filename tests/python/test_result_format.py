"""Scoped original scientific presentation and actual both-host consumers."""
import csv
import json
import struct
from pathlib import Path
import pytest
import imagejxx as ij
from imagejxx import _desktop as desktop

REFERENCE_PATH=Path(__file__).parents[1]/"fixtures/result-format-imagej154r.json"
REFERENCE=json.loads(REFERENCE_PATH.read_bytes())


def read(path):
    with path.open(newline="",encoding="utf-8") as stream:return list(csv.reader(stream))


def test_actual_preferences_presentation_and_measurement_outputs(tmp_path):
    editor=desktop.Editor(str(tmp_path/"preferences.json"));directory=tmp_path/"actual"
    report=json.loads(editor._result_format_ui_case(str(directory),str(REFERENCE_PATH)))
    assert report["original_cases_passed"]==1950
    assert report["copy_matches_csv"] and report["copied_csv"]==(directory/"scientific.csv").read_text()
    assert report["workspace_preserved"] and report["presentation_only_while_running"] and report["source_unchanged"]
    assert len(report["rows"])==5 and len(report["manager_rows"])==3
    assert report["current_rows"][0]["mean"]==1.5
    assert [x["mean"] for x in report["manager_rows"]]==[1.5,.5,2.5]
    assert [x["mean"] for x in report["rows"][-2:]]==[30,30]
    assert read(directory/"ordinary.csv")==[["mean","count"],["1.500","4"]]
    assert read(directory/"scientific.csv")==[["mean","count"],["1.50E0","4"]]
    assert read(directory/"all.csv")==[["mean","count"],["1.50E0","4"],["5.00E-1","2"],["2.50E0","2"],["3.00E1","4"],["3.00E1","4"]]
    assert (directory/"all.csv").read_bytes()==(directory/"reopened.csv").read_bytes()
    assert any(c["name"]=="mean" and c["text"]=="1.50E0" for c in report["results_table"]["cells"])


@pytest.mark.parametrize("precision",[-1,0,1,3,9,10,15])
def test_scientific_precision_mapping_literal_kinds_and_default_compatibility(tmp_path,precision):
    editor=desktop.Editor(str(tmp_path/"preferences.json"))
    values=[1.25,-0.0,1e100,struct.unpack(">d",bytes.fromhex("47efffffe0000000"))[0]]
    rows=[{"value":v} for v in values]+[{"value":v} for v in [42,18446744073709551615,-9223372036854775808,"nan","inf","-inf",None,True,[1,2],{"k":1}]]
    editor.results=json.dumps(rows);baseline=editor.results
    editor.update_settings(json.dumps({"measurement_precision":precision,"measurements":["value"]}),False)
    ordinary=tmp_path/"ordinary.csv";editor.action("save_results",json.dumps({"path":str(ordinary)}))
    editor.update_settings(json.dumps({"measurement_scientific":True}),False)
    scientific=tmp_path/"scientific.csv";editor.action("save_results",json.dumps({"path":str(scientific)}))
    actual=read(scientific)[1:];mapped=3 if precision<0 else min(9,precision)
    for n,value in enumerate(values):
        bits=struct.pack(">d",value).hex().lstrip("0") or "0"
        expected=next(c["scientific"] for c in REFERENCE["cases"] if c["precision"]==mapped and c["bits"]==bits)
        assert actual[n]==[expected]
    assert actual[4:]==[["42"],["18446744073709551615"],["-9223372036854775808"],["nan"],["inf"],["-inf"],[],["true"],["[1,2]"],['{"k":1}']]
    assert editor.results==baseline
    editor.update_settings(json.dumps({"measurement_scientific":False}),False)
    restored=tmp_path/"restored.csv";editor.action("save_results",json.dumps({"path":str(restored)}))
    assert restored.read_bytes()==ordinary.read_bytes() and editor.results==baseline


def test_native_settings_defaults_validation_and_saved_compatibility():
    settings=ij.Settings();assert not settings["measurement_scientific"]
    schema=next(x for x in ij.Settings.schema() if x["name"]=="measurement_scientific")
    assert schema["default"] is False
    settings["measurement_scientific"]=True
    with pytest.raises(ValueError):settings["measurement_scientific"]=1
    assert settings["measurement_scientific"]
