"""Explicit native global/effective metadata and application capture ownership.

This foundation does not enable global processing in the desktop or substitute
for downstream consumer integration. Original ImageJ API results are independent.
"""
import json
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import pytest
import imagejxx as ij

REFERENCE=Path(__file__).parents[2]/"docs/parity/reviews/G034-global-calibration-reference002.json"
CASES=json.loads(REFERENCE.read_text(encoding="utf-8"))["cases"]


def spatial(record):
    return ij.SpatialCalibration(spacing=[record["pixel_width"],record["pixel_height"],record["pixel_depth"]],
        origin=[-record["x_origin"]*record["pixel_width"],0,0],unit=record["unit"])


def document(bits=8,local=None):
    dtype={8:np.uint8,16:np.uint16,24:np.uint8,32:np.float32,64:np.float64}[bits]
    pixels=np.zeros((1,2,3) if bits==24 else (1,2),dtype)
    options=dict(axes="YXS" if bits==24 else "YX")
    if local:options["calibration"]=spatial(local)
    reads=ij.DatasetReadObserver();owner=ij.Document(ij.Dataset(pixels,**options).observe_reads(reads))
    if local and local["signed16"]:ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.signed16(local["value_unit"]))
    elif local and local["value_unit"]!="Gray Value":ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.uncalibrated(local["value_unit"]))
    return owner,reads


def global_value(width=.5,unit="OD"):
    reference=next(c for c in CASES if c["id"]=="enabled_u8")["effective"]
    cal=ij.SpatialCalibration(spacing=[width,.75,2],origin=spatial(reference).origin,unit=reference["unit"])
    return ij.GlobalCalibration(cal,ij.ImageJIntensityCalibration.function("uint8","straight_line",[10,2],unit),"uint8")


def density_value(calibration,raw):
    mode=calibration.record["mode"]
    if mode=="none":return float(raw)
    if mode=="signed16":return float(raw-32768)
    return float(np.asarray(calibration.table_words(),np.uint32).view(np.float32)[raw])


@pytest.mark.parametrize("case",CASES,ids=lambda case:case["id"])
def test_pure_resolver_matches_original_global_local_api(case):
    owner,reads=document(case["bit_depth"],case["local"]);before=owner.snapshot();steps=owner.undo_steps;count=reads.counts().plane_reads
    effective=ij.resolve_effective_calibration(before,global_value() if case["global_present"] else None,ignore_global=case["id"]=="ignored_u8")
    expected=case["effective"];actual=effective.spatial
    assert actual.spacing==[expected["pixel_width"],expected["pixel_height"],expected["pixel_depth"]]
    assert actual.origin[0]==-expected["x_origin"]*expected["pixel_width"] and actual.unit==expected["unit"]
    density=effective.intensity
    assert density.value_unit==expected["value_unit"] and density.has_density==expected["calibrated"]
    assert density.signed16_density==expected["signed16"]
    assert density_value(density,0)==expected["at_0"] and density_value(density,1)==expected["at_1"]
    assert effective.uses_global==(case["global_present"] and case["id"]!="ignored_u8")
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps and reads.counts().plane_reads==count


def test_application_set_clear_new_images_and_local_state():
    app=ij.Application();owner,reads=document();source=app.adopt(owner);before=owner.snapshot();count=reads.counts().plane_reads
    original=app.effective_calibration(source);app.validate_effective_calibration(original)
    assert app.global_calibration is None and app.global_calibration_generation==0
    app.clear_global_calibration();assert app.global_calibration_generation==0
    app.set_global_calibration(global_value());assert app.global_calibration_generation==1
    with pytest.raises(ij.RevisionConflict):app.validate_effective_calibration(original)
    effective=app.effective_calibration(source);assert effective.spatial.spacing[0]==.5 and app.local_calibration(source).spatial.spacing[0]==1
    added=app.create(ij.Dataset(np.zeros((1,2),np.uint8),axes="YX"));assert app.effective_calibration(added).uses_global
    app.clear_global_calibration();assert app.global_calibration is None and app.global_calibration_generation==2
    with pytest.raises(ij.RevisionConflict):app.validate_effective_calibration(effective)
    assert app.effective_calibration(source).spatial.spacing[0]==1
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0 and reads.counts().plane_reads==count


def test_bypass_revisions_are_per_document_and_ignore_global_changes():
    app=ij.Application();first=app.create(ij.Dataset(np.zeros((1,2),np.uint8),axes="YX"));second=app.create(ij.Dataset(np.zeros((1,2),np.uint8),axes="YX"))
    app.set_global_calibration(global_value());capture=app.effective_calibration(first)
    app.set_ignore_global_calibration(True,second);app.validate_effective_calibration(capture)
    app.set_ignore_global_calibration(False,first);app.validate_effective_calibration(capture)
    app.set_ignore_global_calibration(True,first)
    with pytest.raises(ij.RevisionConflict):app.validate_effective_calibration(capture)
    ignored=app.effective_calibration(first);assert ignored.ignores_global and not ignored.uses_global
    app.set_global_calibration(global_value(.75));app.validate_effective_calibration(ignored)
    app.set_ignore_global_calibration(False,first);app.set_ignore_global_calibration(True,first)
    with pytest.raises(ij.RevisionConflict):app.validate_effective_calibration(ignored)


def test_application_identity_source_revisions_and_closed_captures():
    app=ij.Application();owner,_=document();key=app.adopt(owner);app.set_global_calibration(global_value());capture=app.effective_calibration(key)
    display=owner.snapshot().display;display.zoom=2;owner.set_display(display);app.validate_effective_calibration(capture)
    other=ij.Application();other.adopt(owner)
    with pytest.raises(ValueError):other.validate_effective_calibration(capture)
    with pytest.raises(ValueError):app.validate_effective_calibration(ij.resolve_effective_calibration(owner.snapshot()))
    edit=owner.begin_edit();edit.set_title("changed");owner.commit(edit)
    with pytest.raises(ij.RevisionConflict):app.validate_effective_calibration(capture)
    fresh=app.effective_calibration(key);app.close(key,discard=True)
    with pytest.raises(ij.DocumentUnavailable):app.validate_effective_calibration(fresh)
    with pytest.raises(ij.DocumentUnavailable):app.set_ignore_global_calibration(True,key)
    assert fresh.intensity.value_unit=="OD" and fresh.spatial.spacing[0]==.5


def test_immutable_python_values_and_independent_applications():
    cal=ij.SpatialCalibration(spacing=[.5,.75,2]);density=ij.ImageJIntensityCalibration.function("uint8","straight_line",[10,2],"OD")
    value=ij.GlobalCalibration(cal,density,"uint8");outside=cal.spacing;outside[0]=9
    app=ij.Application();owner,_=document();key=app.adopt(owner);app.set_global_calibration(value)
    returned=app.global_calibration;sp=returned.spatial.spacing;sp[0]=99
    effective=app.effective_calibration(key);e=effective.spatial.origin;e[0]=99
    with pytest.raises(AttributeError):effective.spatial.unit="changed"
    assert value.spatial.spacing==[.5,.75,2] and app.global_calibration.spatial.spacing==[.5,.75,2]
    assert app.effective_calibration(key).spatial.origin==[0,0,0]
    replacement=ij.GlobalCalibration(ij.SpatialCalibration(spacing=[9,9,9]),density,"uint8")
    app.set_global_calibration(replacement)
    assert returned.spatial.spacing==[.5,.75,2] and app.global_calibration.spatial.spacing==[9,9,9]
    other=ij.Application();other.adopt(owner);assert not other.effective_calibration(key).uses_global


@pytest.mark.parametrize("samples",[True,1.0,2,0,5])
def test_invalid_global_configuration_refuses(samples):
    with pytest.raises((TypeError,ValueError)):
        ij.GlobalCalibration(ij.SpatialCalibration(),ij.ImageJIntensityCalibration.uncalibrated(),"uint8",samples)


def test_incompatible_density_configuration_refuses():
    with pytest.raises(ValueError):ij.GlobalCalibration(ij.SpatialCalibration(),global_value().intensity,"uint16")
    with pytest.raises(ValueError):ij.GlobalCalibration(ij.SpatialCalibration(),global_value().intensity,"uint8",3)
    with pytest.raises(ValueError):ij.GlobalCalibration(ij.SpatialCalibration(),ij.ImageJIntensityCalibration.uncalibrated(),"int32")


@pytest.mark.parametrize("bits,samples",[(64,1),(8,4)])
def test_native_float64_rgba_extensions(bits,samples):
    pixels=np.zeros((1,2,samples) if samples>1 else (1,2),np.float64 if bits==64 else np.uint8)
    owner=ij.Document(ij.Dataset(pixels,axes="YXS" if samples>1 else "YX"))
    effective=ij.resolve_effective_calibration(owner.snapshot(),global_value())
    assert effective.spatial.spacing[0]==.5 and not effective.intensity.has_density
    assert effective.intensity.value_unit==("OD" if samples==1 else "Gray Value")


def test_lookup_words_and_compatible_record_identity_survive_capture():
    words=[0x7fc12345,0x80000000,0x7f800000,0x3f800000]*64
    density=ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration",version=1,mode="lookup",value_unit="raw",bit_depth=8,table_f32_hex="".join(f"{word:08x}" for word in words),source_parameters={},signed16=False))
    value=ij.GlobalCalibration(ij.SpatialCalibration(),density,"uint8");owner,_=document()
    effective=ij.resolve_effective_calibration(owner.snapshot(),value)
    assert effective.intensity.table_words()==words and effective.intensity.record==density.record


def test_concurrent_global_capture_values_are_coherent():
    app=ij.Application();key=app.create(ij.Dataset(np.zeros((1,2),np.uint8),axes="YX"))
    first=global_value(.5,"A");second=global_value(.75,"B");app.set_global_calibration(first)
    def write():
        for i in range(100):app.set_global_calibration(first if i%2 else second)
    def read():
        for _ in range(100):
            captured=app.effective_calibration(key)
            assert (captured.spatial.spacing[0],captured.intensity.value_unit) in [(.5,"A"),(.75,"B")]
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures=[pool.submit(write),pool.submit(read)]
        for future in futures:future.result(timeout=20)


VARIANTS=json.loads((REFERENCE.parent/"G034-global-calibration-reference003.json").read_text(encoding="utf-8"))["cases"]


@pytest.mark.parametrize("case",VARIANTS,ids=lambda case:case["id"])
def test_original_none_function_and_custom_table_adaptation(case):
    owner,reads=document(case["destination_bits"])
    if case["destination"]=="signed16":ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.signed16())
    before=owner.snapshot();count=reads.counts().plane_reads;unit="unit-"+case["kind"]
    pixel_type="uint16" if case["source_bits"]==16 else "uint8"
    if case["kind"].startswith("none"):density=ij.ImageJIntensityCalibration.uncalibrated(unit)
    elif case["kind"].startswith("line"):density=ij.ImageJIntensityCalibration.function(pixel_type,"straight_line",[10,2],unit)
    else:
        words=[0x3e800000,0x7fc12345,0x7f800000,0x80000000]*(65536//4 if case["source_bits"]==16 else 256//4)
        density=ij.ImageJIntensityCalibration.from_record(dict(schema="imagejxx.intensity-calibration",version=1,mode="lookup",value_unit=unit,bit_depth=case["source_bits"],table_f32_hex="".join(f"{word:08x}" for word in words),source_parameters={},signed16=False))
    value=ij.GlobalCalibration(global_value().spatial,density,pixel_type)
    actual=ij.resolve_effective_calibration(before,value);expected=case["effective"]
    assert actual.uses_global and actual.intensity.value_unit==expected["value_unit"]
    assert actual.intensity.has_density==expected["density"] and actual.intensity.signed16_density==expected["signed16"]
    assert actual.spatial.spacing==[expected["pixel_width"],expected["pixel_height"],expected["pixel_depth"]]
    assert actual.spatial.origin[0]==-expected["x_origin"]*expected["pixel_width"] and actual.spatial.unit==expected["unit"]
    if expected["first_words"] is None:assert not actual.intensity.has_density
    elif actual.intensity.record["mode"]=="signed16":
        np.testing.assert_array_equal(np.array([-32768,-32767,-32766,-32765],np.float32).view(np.uint32),expected["first_words"])
    else:assert actual.intensity.table_words()[:4]==expected["first_words"]
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads==count
