"""Immutable intensity calibration interface and both actual ImGui consumers."""
import json
import os
from pathlib import Path
import numpy as np
import pytest
import imagejxx as ij


def test_signed16_metadata_history_and_projection():
    owner=ij.Document(ij.Dataset(np.array([[[0,32768,65535]],[[0,32768,65535]]],np.uint16),axes="ZYX"),"calibrated")
    before=owner.snapshot()
    calibration=ij.ImageJIntensityCalibration.signed16("HU")
    ij.set_imagej_intensity_calibration(owner,calibration)
    assert owner.undo_steps==1 and ij.read_imagej_intensity_calibration(owner.snapshot()).record==calibration.record
    assert owner.snapshot().revisions.pixels==before.revisions.pixels
    request=ij.imagej_projection_defaults();request.update(last=1,method="sum")
    result=ij.prepare_imagej_projection(owner.snapshot(),request).prepared_new_document().snapshot()
    np.testing.assert_array_equal(result.dataset.numpy().ravel(),[-65536,0,65534])
    assert not ij.read_imagej_intensity_calibration(result).has_density
    assert ij.read_imagej_intensity_calibration(result).value_unit=="HU"
    owner.undo();assert not ij.read_imagej_intensity_calibration(owner.snapshot()).has_density
    owner.redo();assert ij.read_imagej_intensity_calibration(owner.snapshot()).signed16_density
    ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.uncalibrated())
    assert not ij.read_imagej_intensity_calibration(owner.snapshot()).has_density


def test_lookup_record_roundtrip_keeps_all_ieee_words():
    words=[0x7fc12345,0xff800000,0x80000000,0x3f800000]*64
    record={"schema":"imagejxx.intensity-calibration","version":1,"mode":"lookup","value_unit":"OD",
        "bit_depth":8,"table_f32_hex":"".join(f"{v:08x}" for v in words),"source_parameters":{"producer":"test"},"signed16":False}
    value=ij.ImageJIntensityCalibration.from_record(record)
    assert value.record==record and value.table_words()==words
    owner=ij.Document(ij.Dataset(np.zeros((2,3),np.uint8),axes="YX"),"lookup")
    ij.set_imagej_intensity_calibration(owner,value)
    assert ij.read_imagej_intensity_calibration(owner.snapshot()).table_words()==words
    for bad in (dict(record,version=True),dict(record,extra=1),dict(record,value_unit="bad\0unit")):
        with pytest.raises((ValueError,RuntimeError)):
            ij.ImageJIntensityCalibration.from_record(bad)


@pytest.mark.parametrize("pixels,axes",[(np.zeros((2,3),np.uint8),"YX"),(np.zeros((2,3),np.float32),"YX"),(np.zeros((2,3,3),np.uint8),"YXS")])
def test_incompatible_signed_calibration_is_atomic(pixels,axes):
    owner=ij.Document(ij.Dataset(pixels,axes=axes),"unsupported");before=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError)):
        ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.signed16())
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_intensity_calibration_dialog(context,tmp_path):
    if context=="imgui_cpp":
        path=Path(os.environ["IMAGEJXX_INTENSITY_CPP_REPORT"])
        assert 0<path.stat().st_size<16*1024*1024;report=json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._intensity_calibration_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="intensity-calibration-ui.001" and report["context"]==context
    assert report["state"]=="observed" and not report["error"] and not report["acceptance_claim"]
    for frame in report["frames"]:
        assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    key="imagejxx.intensity_calibration"
    expected={"schema":"imagejxx.intensity-calibration","version":1,"mode":"signed16","value_unit":"HU"}
    assert key not in report["before"]["properties"] and report["before"]["undo"]==0
    for name in ("applied","redone","cancelled","restored","reopened"):
        row=report[name];assert row["properties"][key]==expected
        assert row["pixels"]==[[0.,32768.,65535.],[0.,32768.,65535.]]
    for name in ("undone","cleared"):
        assert key not in report[name]["properties"]
    assert report["applied"]["undo"]==1 and report["cancelled"]["undo"]==1
    assert report["reopened_dialog"]["values"]=={"mode":"signed16","value_unit":"HU"}
    assert report["source_pixels_unchanged"] and report["projection_source_unchanged"]
    projected=report["projected"]
    assert projected["shape"]==[1,1,1,1,3,1] and projected["pixels"]==[[-65536.,0.,65534.]]
    assert projected["properties"][key]["mode"]=="none" and projected["properties"][key]["value_unit"]=="HU"
    names={r["name"] for r in report["controls"]}
    assert {"show_intensity_calibration","mode.choice.signed16","calibration_apply","Undo","Redo","calibration_cancel","calibration_clear","projection_apply"}<=names
    assert report["lifecycle"]["open"] is False

    for name in ("function_applied","function_reopened"):
        record=report[name]["properties"][key]
        assert record["mode"]=="lookup" and record["bit_depth"]==16 and not record["signed16"]
        assert record["value_unit"]=="HU"
        words=np.array([int(record["table_f32_hex"][i:i+8],16) for i in range(0,len(record["table_f32_hex"]),8)],dtype=np.uint32).view(np.float32)
        np.testing.assert_array_equal(words,5+2*np.arange(65536,dtype=np.float32))
        assert record["source_parameters"]["function"]=="straight_line"
        assert record["source_parameters"]["coefficients"]==[5.,2.]
        assert record["source_parameters"]["zero_clip"] is True
    draft=report["function_dialog"]["values"]
    assert draft["function"]=="straight_line" and json.loads(draft["coefficients"])==[5.,2.]
    assert draft["zero_clip"] is True and draft["value_unit"]=="HU"

    invalid=report["invalid_fit"]
    assert not invalid["busy"] and invalid["error"]["open"]
    assert "matching point counts" in invalid["error"]["message"]
    assert invalid["state"]["properties"][key]==report["function_applied"]["properties"][key]
    record=report["fitted"]["properties"][key]
    assert record==report["fit_reopened"]["properties"][key]
    assert record["source_parameters"]["function"]=="poly2"
    np.testing.assert_allclose(record["source_parameters"]["coefficients"],[1,2,1],rtol=0,atol=1e-12)
    fit=record["source_parameters"]["fit"]
    assert fit["measured"]==[0,1,2] and fit["known"]==[1,4,9]
    assert fit["rmse"]<1e-12 and fit["r_squared"]>1-1e-12
    assert report["fit_dialog"]["values"]["function"]=="poly2"
    assert json.loads(report["fit_dialog"]["values"]["measured"])==[0,1,2]
    assert json.loads(report["fit_dialog"]["values"]["known"])==[1,4,9]
    assert "RMSE" in report["fit_status"] and "R-squared" in report["fit_status"]


import gzip
import hashlib
_FUNCTION_FIXTURE=Path(__file__).resolve().parents[2]/"docs/parity/reviews/G034-intensity-functions-reference001.jsonl.gz"
_FUNCTION_CASES=[json.loads(line) for line in gzip.decompress(_FUNCTION_FIXTURE.read_bytes()).splitlines()]


@pytest.mark.parametrize("case",_FUNCTION_CASES)
def test_generated_full_table_against_imagej(case):
    value=ij.ImageJIntensityCalibration.function("uint"+str(case["bit_depth"]),case["function"],case["coefficients"],"unit",
        zero_clip=case["zero_clip"],inverted_lut=case["inverted_lut"])
    words=value.table_words()
    assert len(words)==2**case["bit_depth"]
    assert hashlib.sha256(np.asarray(words,dtype=">u4").tobytes()).hexdigest()==case["canonical_sha256"]
    for index,raw in case["samples"]:
        expected=0x7fc00000 if raw&0x7f800000==0x7f800000 and raw&0x7fffff else raw
        assert words[index]==expected
    assert value.signed16_density==case["signed16"] and value.value_unit=="unit"


@pytest.mark.parametrize("kind,name,coefficients",[
    ("float32","straight_line",[0,1]),("uint16","uncalibrated_od",[]),
    ("uint8","poly2",[0,1]),("uint8","unknown",[]),
    ("uint8","straight_line",[0,float("nan")]),("uint8","straight_line",[float("inf"),1]),
])
def test_function_invalid_configuration(kind,name,coefficients):
    with pytest.raises((ValueError,RuntimeError)):
        ij.ImageJIntensityCalibration.function(kind,name,coefficients)


_FIT_FIXTURE=Path(__file__).resolve().parents[2]/"docs/parity/reviews/G034-intensity-polynomial-fits-reference001.jsonl.gz"
_FIT_CASES=[json.loads(line) for line in gzip.decompress(_FIT_FIXTURE.read_bytes()).splitlines()]


@pytest.mark.parametrize("case",_FIT_CASES)
@pytest.mark.parametrize("kind",["uint8","uint16"])
def test_polynomial_fit_against_imagej_and_independent_lstsq(case,kind):
    value=ij.ImageJIntensityCalibration.fit_polynomial(kind,case["function"],case["measured"],case["known"],"unit")
    params=value.record["source_parameters"];coeff=np.asarray(params["coefficients"])
    np.testing.assert_allclose(coeff,case["coefficients"],rtol=5e-4,atol=1e-5)
    oracle=np.polynomial.polynomial.polyfit(case["measured"],case["known"],len(coeff)-1)
    np.testing.assert_allclose(coeff,oracle,rtol=1e-10,atol=1e-10)
    x=np.arange(256 if kind=="uint8" else 65536,dtype=np.float64)
    actual=np.asarray(value.table_words(),dtype=np.uint32).view(np.float32)
    np.testing.assert_allclose(actual,np.polynomial.polynomial.polyval(x,oracle).astype(np.float32),rtol=2e-6,atol=2e-5)
    assert abs(params["fit"]["r_squared"]-case["r_squared"])<1e-8


@pytest.mark.parametrize("name,x,y",[
    ("straight_line",[],[]),("straight_line",[0,1],[1]),("poly2",[1,1,1],[1,2,3]),
    ("poly4",[0,1,2,3],[0,1,2,3]),("straight_line",[-1,1],[0,1]),
    ("straight_line",[0,256],[0,1]),("straight_line",[0,1],[float("nan"),0]),
    ("exponential",[0,1],[1,2]),("straight_line",list(range(4097)),[1]*4097),
])
def test_polynomial_fit_rejects_invalid_points(name,x,y):
    with pytest.raises((ValueError,RuntimeError)):
        ij.ImageJIntensityCalibration.fit_polynomial("uint8",name,x,y)


def test_signed_linear_fit_and_constant_output_diagnostic():
    value=ij.ImageJIntensityCalibration.fit_polynomial("uint16","straight_line",[0,65535],[-32768,32767])
    assert value.signed16_density and value.record["source_parameters"]["coefficients"]==[-32768.,1.]
    constant=ij.ImageJIntensityCalibration.fit_polynomial("uint8","straight_line",[0,1,2],[5,5,5])
    assert constant.record["source_parameters"]["fit"]["r_squared"] is None


_SIGNED_FIT_FIXTURE=Path(__file__).resolve().parents[2]/"docs/parity/reviews/G034-intensity-signed-linear-fits-reference001.jsonl.gz"
@pytest.mark.parametrize("case",[json.loads(line) for line in gzip.decompress(_SIGNED_FIT_FIXTURE.read_bytes()).splitlines()])
def test_signed_fit_exact_imagej_coefficients_and_classification(case):
    value=ij.ImageJIntensityCalibration.fit_polynomial("uint16","straight_line",case["measured"],case["known"])
    assert value.record["source_parameters"]["coefficients"]==case["coefficients"]
    assert value.signed16_density==case["signed16"]
