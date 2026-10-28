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
