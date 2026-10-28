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
    assert report["reopened_dialog"]["values"]=={"mode":"signed16","value_unit":"HU","show_plot":True}
    assert report["source_pixels_unchanged"] and report["projection_source_unchanged"]
    projected=report["projected"]
    assert projected["shape"]==[1,1,1,1,3,1] and projected["pixels"]==[[-65536.,0.,65534.]]
    assert projected["properties"][key]["mode"]=="none" and projected["properties"][key]["value_unit"]=="HU"
    names={r["name"] for r in report["controls"]}
    assert {"show_intensity_calibration","mode.choice.signed16","calibration_apply","Undo","Redo","calibration_cancel","calibration_clear","projection_apply"}<=names
    assert report["lifecycle"]["open"] is False
    assert report["fit_ready"]["state"]==2 and report["fit_ready"]["source_unchanged"]
    assert report["fit_ready"]["resource_bytes"]==report["fit_ready"]["retained_bytes"]>0
    assert report["fit_reserved_after_publish"]==0
    _assert_physical_fit_running_cancel(report)
    _assert_fit_plot_render(report)
    _assert_automatic_fit_plot(report)
    _assert_calibration_table_plot_and_csv(report)
    for name in ("manual_automatic_plot","function_automatic_plot"):
        plot=report[name]
        assert plot["kind"]=="calibration_table" and plot["visible"] and plot["finite_geometry"]
        assert len(plot["curve_x"])==len(plot["curve_y"])==256
    np.testing.assert_array_equal(report["manual_automatic_plot"]["curve_y"],257*np.arange(256)-32768)
    np.testing.assert_array_equal(report["function_automatic_plot"]["curve_y"],5+2*257*np.arange(256))
    _assert_fit_csv(report)
    _assert_plot_interaction(report)
    _assert_plot_axes(report)
    _assert_plot_log(report)

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


_NONLINEAR_FIXTURE=Path(__file__).resolve().parents[2]/"docs/parity/reviews/G034-intensity-nonlinear-fits-reference001.jsonl.gz"
_NONLINEAR_CASES=[json.loads(line) for line in gzip.decompress(_NONLINEAR_FIXTURE.read_bytes()).splitlines()]


def _nonlinear_value(name,p,x):
    a,b=p[:2]
    if name=="exponential":return a*np.exp(b*x)
    if name=="power":return a*np.power(x,b)
    if name=="log":return a*np.log(b*x)
    if name=="rodbard":return p[3]+(a-p[3])/(1+np.power(x/p[2],b))
    if name=="gamma_variate":
        d=np.maximum(x-a,0);return b*np.power(d,p[2])*np.exp(-d/p[3])
    if name=="log2":return a+b*np.log(x-p[2])
    if name=="exp_with_offset":return a*np.exp(-b*x)+p[2]
    if name=="gaussian":return a+(b-a)*np.exp(-(x-p[2])**2/(2*p[3]**2))
    if name=="exp_recovery":return a*(1-np.exp(-b*x))+p[2]
    raise AssertionError(name)


@pytest.mark.parametrize("case",_NONLINEAR_CASES)
@pytest.mark.parametrize("kind",["uint8","uint16"])
@pytest.mark.parametrize("starting",["automatic","explicit"])
def test_nonlinear_models_against_pinned_imagej(case,kind,starting):
    initial=[] if starting=="automatic" else [v*f for v,f in zip(case["true_coefficients"],[1.1,.9,1.05,.95])]
    value=ij.ImageJIntensityCalibration.fit_nonlinear(kind,case["function"],case["measured"],case["known"],"unit",initial_coefficients=initial)
    parameters=value.record["source_parameters"];p=np.asarray(parameters["coefficients"])
    x=np.asarray(case["measured"]);y=np.asarray(case["known"])
    if case["function"]=="rodbard2":
        prediction=_nonlinear_value("rodbard",p,y);target=x
        assert parameters["fit"]["objective"]=="measured_as_function_of_known"
    else:prediction=_nonlinear_value(case["function"],p,x);target=y
    sse=np.sum((prediction-target)**2);scale=max(1,np.max(np.abs(target)))
    assert np.isfinite(p).all() and case["status"]==0
    assert sse<=case["sse"]*1.02+1e-10*scale**2
    if not case["noisy"]:
        compare=p.copy()
        if case["function"]=="gaussian":compare[3]=abs(compare[3])
        np.testing.assert_allclose(compare,case["true_coefficients"],rtol=1e-3,atol=1e-4)
    fit=parameters["fit"]
    assert fit["initial_coefficients"]==initial and len(fit["resolved_initial_coefficients"])==len(p)
    assert fit["iterations"]<fit["max_iterations"]==256
    assert fit["method"]=="scaled_damped_least_squares"
    np.testing.assert_allclose(fit["rmse"],np.sqrt(sse/len(target)),rtol=1e-5,atol=1e-12)


@pytest.mark.parametrize("configuration",[
    {"name":"unknown"},{"name":"poly2"},{"measured":[]},{"known":[1]},
    {"measured":[1,1,1]},{"initial_coefficients":[1]},
    {"initial_coefficients":[0,0]},{"initial_coefficients":[float("inf"),1]},
    {"max_iterations":0},{"max_iterations":1025},{"relative_tolerance":0},
    {"relative_tolerance":float("nan")},{"max_iterations":1},
])
def test_nonlinear_invalid_or_nonconvergent_configuration(configuration):
    args=dict(pixel_type="uint8",name="exponential",measured=[0,1,2],known=[5,10,20],initial_coefficients=[4,.5])
    args.update(configuration)
    with pytest.raises((ValueError,RuntimeError)):
        ij.ImageJIntensityCalibration.fit_nonlinear(**args)


@pytest.mark.parametrize("context",["imgui_cpp","imgui_python"])
def test_actual_nonlinear_calibration_dialog(context,tmp_path):
    if context=="imgui_cpp":
        path=Path(os.environ["IMAGEJXX_INTENSITY_NONLINEAR_CPP_REPORT"])
        assert 0<path.stat().st_size<16*1024*1024;report=json.loads(path.read_bytes())
    else:
        from imagejxx import _desktop
        editor=_desktop.Editor(str(tmp_path/"preferences.json"))
        report=json.loads(editor._intensity_nonlinear_ui_case(str(tmp_path/"capture")))
    assert report["contract"]=="intensity-nonlinear-ui.001" and report["context"]==context
    assert report["state"]=="observed" and not report["error"] and not report["acceptance_claim"]
    for frame in report["frames"]:assert frame["events"]==frame["rendered"]["input_frame"]["raw_inputs"]
    key="imagejxx.intensity_calibration"
    invalid=report["invalid_fit"]
    assert not invalid["busy"] and invalid["error"]["open"]
    assert invalid["state"]["properties"][key]==report["function_applied"]["properties"][key]
    record=report["fitted"]["properties"][key]
    assert record==report["fit_reopened"]["properties"][key]
    assert record["source_parameters"]["function"]=="exponential" and record["value_unit"]=="HU"
    fit=record["source_parameters"]["fit"]
    assert fit["known"]==[5,10,20] and fit["measured"]==[0,1,2]
    assert fit["initial_coefficients"]==[4,.5] and fit["iterations"]>0
    assert fit["rmse"]<1e-8 and fit["r_squared"]>1-1e-12
    np.testing.assert_allclose(record["source_parameters"]["coefficients"],[5,np.log(2)],rtol=1e-8)
    values=report["fit_dialog"]["values"]
    assert values["function"]=="exponential" and json.loads(values["initial_coefficients"])==[4,.5]
    assert json.loads(values["known"])==[5,10,20] and values["max_iterations"]==256
    assert "iterations:" in report["fit_status"] and not report["lifecycle"]["open"]
    assert report["fit_ready"]["state"]==2 and report["fit_ready"]["source_unchanged"]
    assert report["fit_ready"]["resource_bytes"]==report["fit_ready"]["retained_bytes"]>0
    assert report["fit_ready"]["stage"]=="Calibration table"
    assert report["fit_ready"]["stage_completed"]==report["fit_ready"]["stage_total"]==65536
    assert report["fit_reserved_after_publish"]==0
    _assert_physical_fit_running_cancel(report)
    _assert_fit_plot_render(report)
    _assert_automatic_fit_plot(report)
    _assert_calibration_table_plot_and_csv(report)
    for name in ("manual_automatic_plot","function_automatic_plot"):
        plot=report[name]
        assert plot["kind"]=="calibration_table" and plot["visible"] and plot["finite_geometry"]
        assert len(plot["curve_x"])==len(plot["curve_y"])==256
    np.testing.assert_array_equal(report["manual_automatic_plot"]["curve_y"],257*np.arange(256)-32768)
    np.testing.assert_array_equal(report["function_automatic_plot"]["curve_y"],5+2*257*np.arange(256))
    _assert_fit_csv(report)
    _assert_plot_interaction(report)
    _assert_plot_axes(report)
    _assert_plot_log(report)
    assert {"show_intensity_nonlinear_fit","initial_coefficients","invalid_fit_apply","fit_error_ok","fit_apply","fit_cancel"}<={r["name"] for r in report["controls"]}



def _fit_request(function="exponential"):
    request=ij.ImageJIntensityFitRequest();request.function=function
    request.measured=[0,1,2];request.known=[5,10,20] if function=="exponential" else [1,4,9]
    request.initial_coefficients=[4,.5] if function=="exponential" else []
    return request


def _fit_owner():
    return ij.Document(ij.Dataset(np.array([[[0,1,2]],[[3,4,5]]],np.uint16),axes="ZYX"),"fit")


def test_prepared_fit_freezes_request_and_publishes_one_metadata_edit():
    owner=_fit_owner();before=owner.snapshot();app=ij.Application();app.adopt(owner)
    request=_fit_request("poly2");bytes_=ij.estimate_imagej_intensity_fit(before,request)
    request.max_working_bytes=bytes_-1;rejected_callbacks=[]
    with pytest.raises((ValueError,RuntimeError)):
        ij.prepare_imagej_intensity_fit(before,request,checkpoint=lambda:rejected_callbacks.append(True))
    assert not rejected_callbacks
    request.max_working_bytes=bytes_;events=[];changed=False
    def checkpoint():
        nonlocal changed
        if not changed:request.known=[100,100,100];changed=True
    prepared=ij.prepare_imagej_intensity_fit(before,request,checkpoint=checkpoint,stage_progress=lambda name,done,total:events.append((name,done,total)))
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    assert prepared.prepared_new_document() is None and any(e[0]=="Calibration polynomial fit" for e in events)
    value=ij.read_imagej_intensity_calibration(prepared.preview)
    np.testing.assert_allclose(value.record["source_parameters"]["coefficients"],[1,2,1],rtol=0,atol=1e-12)
    app.publish(prepared);assert owner.undo_steps==1 and not prepared.active
    np.testing.assert_array_equal(owner.snapshot().dataset.numpy(),before.dataset.numpy())
    owner.undo();assert not ij.read_imagej_intensity_calibration(owner.snapshot()).has_density
    owner.redo();assert ij.read_imagej_intensity_calibration(owner.snapshot()).has_density


@pytest.mark.parametrize("stage",["Calibration nonlinear fit","Calibration table"])
def test_fit_cancel_at_real_numerical_stage(stage):
    owner=_fit_owner();before=owner.snapshot();token=ij.CancellationToken();seen=[]
    def progress(name,done,total):
        seen.append(name)
        if name==stage:token.cancel()
    with pytest.raises(RuntimeError,match="(?i)cancel"):
        ij.prepare_imagej_intensity_fit(before,_fit_request(),cancellation=token,stage_progress=progress)
    assert stage in seen and owner.snapshot().shares_data(before) and owner.undo_steps==0


@pytest.mark.parametrize("change",["display","metadata","close","cancel_ready"])
def test_prepared_fit_refuses_changed_closed_or_cancelled_source(change):
    owner=_fit_owner();before=owner.snapshot();app=ij.Application();app.adopt(owner);token=ij.CancellationToken()
    prepared=ij.prepare_imagej_intensity_fit(before,_fit_request(),cancellation=token)
    if change=="display":view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    elif change=="metadata":edit=owner.begin_edit();edit.set_property("changed",True);owner.commit(edit)
    elif change=="close":app.close(owner.id,discard=True)
    else:token.cancel()
    current=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError)):
        app.publish(prepared)
    assert owner.snapshot().shares_data(current) and prepared.active
    assert not ij.read_imagej_intensity_calibration(owner.snapshot()).has_density


def test_editor_fit_worker_budget_cancel_and_success(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));owner=_fit_owner();editor.add_native_document(owner)
    before=owner.snapshot();arguments=dict(function="exponential",measured=[0,1,2],known=[5,10,20],initial_coefficients=[4,.5])
    editor.update_settings(processing_memory_mb=1,persist=False)
    with pytest.raises((ValueError,RuntimeError),match="(?i)budget|memory"):
        editor.action("fit_intensity_calibration",**arguments)
    assert not editor.busy and owner.snapshot().shares_data(before)
    editor.update_settings(processing_memory_mb=64,persist=False)
    editor.action("fit_intensity_calibration",**arguments);assert editor.busy
    editor.cancel();editor.wait();assert owner.snapshot().shares_data(before) and owner.undo_steps==0
    editor.action("fit_intensity_calibration",**arguments);editor.wait()
    assert not editor.busy and owner.undo_steps==1
    np.testing.assert_allclose(ij.read_imagej_intensity_calibration(owner.snapshot()).record["source_parameters"]["coefficients"],[5,np.log(2)],rtol=1e-8)



def test_fit_python_stage_exception_isolated():
    owner=_fit_owner();before=owner.snapshot()
    def stop(name,done,total):raise ValueError("intentional stage failure")
    with pytest.raises(ValueError,match="intentional stage failure"):
        ij.prepare_imagej_intensity_fit(before,_fit_request(),stage_progress=stop)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0


def test_editor_fit_rejects_view_changes_before_publication(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));owner=_fit_owner();editor.add_native_document(owner)
    editor.action("fit_intensity_calibration",function="exponential",measured=[0,1,2],known=[5,10,20],initial_coefficients=[4,.5])
    view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    with pytest.raises(RuntimeError,match="(?i)changed"):
        editor.wait()
    assert not editor.busy and owner.undo_steps==0 and not ij.read_imagej_intensity_calibration(owner.snapshot()).has_density


def _assert_physical_fit_running_cancel(report):
    case=report["fit_running_cancel"]
    assert case["running_state"]==1 and case["terminal_state"]==6
    assert case["resource_bytes"]>0 and case["reserved_before"]>=case["resource_bytes"]
    assert case["cancel_requested"] and case["reserved_after"]==0
    assert case["source_data_unchanged"] and case["source_revisions_unchanged"]
    assert case["source_before"]==case["source_after"]
    assert not case["busy_after"] and not case["error_after"]
    control=case["control"]
    assert control["enabled"] and control["id"]>0
    assert {"fit_cancel_apply","fit_running_cancel"}<={r["name"] for r in report["controls"]}
    assert len(report["frames"])<=608


def _assert_fit_plot_render(report):
    plot=report["fit_plot"];fit=report["fitted"]["properties"]["imagejxx.intensity_calibration"]["source_parameters"]
    assert plot["function"]==fit["function"] and plot["value_unit"]=="HU"
    assert plot["measured"]==fit["fit"]["measured"] and plot["known"]==fit["fit"]["known"]
    assert len(plot["curve_x"])==len(plot["curve_y"])==100
    assert plot["curve_x"][0]==0 and plot["curve_x"][-1]==2
    assert plot["objective"]=="known_as_function_of_measured"
    origin=np.asarray(plot["canvas_origin"]);size=np.asarray(plot["canvas_size"])
    limits=plot["limits"]
    expected_x=origin[0]+(np.asarray(plot["measured"])-limits[0])/(limits[1]-limits[0])*size[0]
    np.testing.assert_allclose(np.asarray(plot["data_screen_points"])[:,0],expected_x,rtol=0,atol=1e-4)
    curve=np.asarray(plot["curve_x"])
    expected=5*np.power(2.,curve) if plot["function"]=="exponential" else (curve+1)**2
    np.testing.assert_allclose(plot["curve_y"],expected,rtol=3e-6,atol=1e-5)
    assert "show_intensity_fit_plot" in {c["name"] for c in report["controls"]}


def _plot_from_reference(case,pixel_type):
    value=ij.ImageJIntensityCalibration.function(pixel_type,case["function"],case["coefficients"],"unit")
    record=value.record
    record["source_parameters"]["fit"]={"measured":case["measured"],"known":case["known"]}
    return ij.ImageJIntensityCalibration.from_record(record)


def _fit_plot_cases():
    import gzip
    path=Path(__file__).parents[2]/"docs/parity/reviews/G034-intensity-fit-plots-reference001.jsonl.gz"
    return [json.loads(line) for line in gzip.decompress(path.read_bytes()).splitlines()]


@pytest.mark.parametrize("case",_fit_plot_cases())
@pytest.mark.parametrize("pixel_type",["uint8","uint16"])
def test_fit_plot_matches_imagej_samples_and_limits(case,pixel_type):
    value=_plot_from_reference(case,pixel_type)
    plot=ij.imagej_intensity_fit_plot(value,case["points"])
    np.testing.assert_array_equal(plot.curve_x,case["curve_x"])
    expected=np.asarray([np.nan if v is None else v for v in case["curve_y"]])
    np.testing.assert_allclose(plot.curve_y,expected,rtol=3e-6,atol=1e-6,equal_nan=True)
    np.testing.assert_allclose([plot.x_min,plot.x_max,plot.y_min,plot.y_max],case["limits"],rtol=3e-6,atol=1e-6)
    assert plot.measured==case["measured"] and plot.known==case["known"]
    assert plot.objective==("measured_as_function_of_known" if case["function"]=="rodbard2" else "known_as_function_of_measured")
    assert plot.value_unit=="unit" and plot.function==case["function"]


@pytest.mark.parametrize("points",[0,1,1001])
def test_fit_plot_refuses_invalid_sample_bounds(points):
    value=ij.ImageJIntensityCalibration.fit_polynomial("uint16","straight_line",[0,1],[1,3])
    with pytest.raises(ValueError):ij.imagej_intensity_fit_plot(value,points)


def test_fit_plot_budget_cancellation_and_callback_failure():
    value=ij.ImageJIntensityCalibration.fit_polynomial("uint16","straight_line",[0,1],[1,3])
    calls=[]
    with pytest.raises(ValueError,match="budget"):
        ij.imagej_intensity_fit_plot(value,max_working_bytes=1,checkpoint=lambda:calls.append(True))
    assert not calls
    token=ij.CancellationToken()
    with pytest.raises(RuntimeError,match="(?i)cancel"):
        ij.imagej_intensity_fit_plot(value,cancellation=token,checkpoint=token.cancel)
    def fail():raise LookupError("plot callback failure")
    with pytest.raises(LookupError,match="plot callback failure"):ij.imagej_intensity_fit_plot(value,checkpoint=fail)


@pytest.mark.parametrize("change",["missing_fit","mismatch","out_of_range","bad_coefficients","unknown_model","constant_x"])
def test_fit_plot_refuses_malformed_provenance(change):
    value=ij.ImageJIntensityCalibration.fit_polynomial("uint8","straight_line",[0,1],[1,3]);record=value.record
    p=record["source_parameters"]
    if change=="missing_fit":p.pop("fit")
    elif change=="mismatch":p["fit"]["known"]=[1]
    elif change=="out_of_range":p["fit"]["measured"]=[0,256]
    elif change=="bad_coefficients":p["coefficients"]=[1]
    elif change=="unknown_model":p["function"]="unknown"
    else:p["fit"]["measured"]=[1,1]
    with pytest.raises(ValueError):ij.imagej_intensity_fit_plot(ij.ImageJIntensityCalibration.from_record(record))


def test_fit_plot_rejects_uncalibrated_and_arbitrary_lookup():
    for value in [ij.ImageJIntensityCalibration.uncalibrated(),ij.ImageJIntensityCalibration.signed16(),
                  ij.ImageJIntensityCalibration.function("uint8","straight_line",[1,2])]:
        with pytest.raises(ValueError):ij.imagej_intensity_fit_plot(value)


@pytest.mark.parametrize("case",["extreme","nonfinite","irregular"])
def test_fit_plot_headless_geometry_and_stale_source(case,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    coefficients=[1,2] if case=="irregular" else [1e38,1e38]
    measured=[0,1,4] if case=="irregular" else [0,1] if case=="extreme" else [0,5]
    known=[1,3,9] if case=="irregular" else [0,1e-300] if case=="extreme" else [0,1]
    value=ij.ImageJIntensityCalibration.function("uint16","straight_line",coefficients)
    record=value.record;record["source_parameters"]["fit"]={"measured":measured,"known":known}
    owner=_fit_owner();ij.set_imagej_intensity_calibration(owner,ij.ImageJIntensityCalibration.from_record(record));before=owner.snapshot();steps=owner.undo_steps
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    editor.action("show_intensity_fit_plot");render=editor.render_headless(4);plot=render["fit_plot"]
    assert plot is not None and plot["finite_geometry"] and plot["drawn_segments"]>0
    assert np.isfinite(plot["data_screen_points"]).all()
    if case=="nonfinite":assert any(v is None for v in plot["curve_y"])
    if case=="irregular":
        x=np.asarray(plot["data_screen_points"])[:,0]
        np.testing.assert_allclose((x-x[0])/(x[-1]-x[0]),[0,.25,1],atol=1e-6)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    assert editor.render_headless(4)["fit_plot"] is None


def test_fit_plot_editor_honors_memory_preference(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();value=ij.ImageJIntensityCalibration.fit_polynomial("uint16","straight_line",[0,1],[1,3],"u"*1048576)
    ij.set_imagej_intensity_calibration(owner,value);before=owner.snapshot()
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner);editor.update_settings(processing_memory_mb=1,persist=False)
    with pytest.raises((ValueError,RuntimeError),match="(?i)budget|memory"):editor.action("show_intensity_fit_plot")
    assert owner.snapshot().shares_data(before)


def test_fit_plot_uses_model_curve_before_uint8_zero_clip():
    value=ij.ImageJIntensityCalibration.fit_polynomial("uint8","straight_line",[0,20],[-10,10],zero_clip=True)
    assert np.array(value.table_words(),np.uint32).view(np.float32)[0]==0
    assert ij.imagej_intensity_fit_plot(value,256).curve_y[0]==-10


def _assert_automatic_fit_plot(report):
    assert report["fit_initial_show_plot"] is True
    assert report["auto_plot_disabled"] is None
    assert report["fit_dialog"]["values"]["show_plot"] is False
    assert report["auto_plot_draft"]["show_plot"] is False and report["auto_plot_saved"] is True
    assert report["plot_after_running_cancel"]==report["fit_plot"]
    assert report["plot_after_close"] is None
    close=next(c["control"] for c in report["controls"] if c["name"]=="fit_plot_close")
    assert close["visible"] and close["window_name"]=="Plot"
    ready=report["auto_plot_ready"]
    assert ready["state"]==2 and ready["source_unchanged"]
    assert ready["stage"]=="Calibration fit plot" and ready["stage_completed"]==ready["stage_total"]==100
    assert ready["resource_bytes"]==ready["retained_bytes"]>report["fit_ready"]["resource_bytes"]
    assert report["auto_plot_reserved_after"]==0
    plot=report["auto_plot_enabled"]
    assert plot and plot["value_unit"]=="HUauto" and plot["function"]==report["fit_plot"]["function"]
    assert plot["visible"] and plot["finite_geometry"] and plot["drawn_segments"]==99
    assert report["auto_fit_applied"]["undo"]==report["fitted"]["undo"]+1
    assert "show_plot" not in report["auto_fit_applied"]["properties"]["imagejxx.intensity_calibration"]["source_parameters"]
    assert {"fit_show_plot_false","fit_show_plot_true","auto_fit_apply","fit_plot_close"}<={c["name"] for c in report["controls"]}


@pytest.mark.parametrize("show",[False,True])
def test_editor_automatic_fit_plot_publication_and_noop(show,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    before=owner.snapshot()
    arguments=dict(function="poly2",measured=[0,1,3],known=[1,4,16],show_plot=show)
    editor.action("fit_intensity_calibration",**arguments);editor.wait()
    plot=editor.render_headless(4)["fit_plot"]
    assert (plot is not None)==show
    if show:assert plot["function"]=="poly2" and plot["measured"]==[0,1,3]
    assert owner.undo_steps==1 and owner.snapshot().revisions.pixels==before.revisions.pixels
    assert "show_plot" not in ij.read_imagej_intensity_calibration(owner.snapshot()).record["source_parameters"]
    # A metadata-identical fit must bind to the unchanged revision too.
    editor.action("fit_intensity_calibration",**arguments);editor.wait()
    assert (editor.render_headless(4)["fit_plot"] is not None)==show
    assert owner.undo_steps==1


@pytest.mark.parametrize("change",["failure","cancel","stale"])
def test_automatic_fit_plot_refusal_and_previous_plot(change,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    editor.action("fit_intensity_calibration",function="straight_line",measured=[0,1],known=[1,3],show_plot=True);editor.wait()
    previous=editor.render_headless(4)["fit_plot"];before=owner.snapshot();steps=owner.undo_steps
    args=dict(function="exponential",measured=[0,1,2],known=[5,10,20],initial_coefficients=[4,.5],show_plot=True)
    if change=="failure":args["max_iterations"]=1
    editor.action("fit_intensity_calibration",**args)
    if change=="cancel":editor.cancel()
    elif change=="stale":view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    if change=="cancel":editor.wait()
    else:
        with pytest.raises(RuntimeError):editor.wait()
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    after=editor.render_headless(4)["fit_plot"]
    if change=="stale":assert after is None
    else:assert after==previous


@pytest.mark.parametrize("value",[0,1,"true",None])
def test_fit_show_plot_requires_boolean(value,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner);before=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError),match="Boolean"):
        editor.action("fit_intensity_calibration",function="straight_line",measured=[0,1],known=[1,3],show_plot=value)
    assert not editor.busy and owner.snapshot().shares_data(before)


def _assert_calibration_table_plot_and_csv(report):
    import csv,io
    plot=report["calibration_table_plot"]
    assert plot["kind"]=="calibration_table" and plot["function"]=="signed16" and plot["value_unit"]=="HU"
    assert plot["visible"] and plot["finite_geometry"] and plot["drawn_segments"]==255
    assert plot["measured"]==plot["known"]==[]
    x=257*np.arange(256)
    np.testing.assert_array_equal(plot["curve_x"],x);np.testing.assert_array_equal(plot["curve_y"],x-32768)
    assert report["table_csv_browser"]["open"] and report["table_csv_browser"]["save"]
    assert report["table_csv_browser_closed"] and report["table_plot_closed"] and report["table_plot_source_unchanged"]
    path=Path(report["table_csv_path"]);assert "µ" in path.name and path.read_text()==report["table_csv_text"]
    rows=list(csv.DictReader(io.StringIO(report["table_csv_text"])))
    assert len(rows)==256 and all(r["series"]=="table" for r in rows)
    np.testing.assert_array_equal([float(r["x"]) for r in rows],x)
    np.testing.assert_array_equal([float(r["y"]) for r in rows],x-32768)
    assert {"show_intensity_calibration_plot","table_plot_export","table_csv_path","table_csv_save","table_plot_close"}<={c["name"] for c in report["controls"]}


def _calibration_table_plot_cases():
    import gzip
    path=Path(__file__).parents[2]/"docs/parity/reviews/G034-calibration-table-plots-reference001.jsonl.gz"
    return [json.loads(line) for line in gzip.decompress(path.read_bytes()).splitlines()]


def _table_plot_calibration(case):
    if case["kind"]=="function":
        return ij.ImageJIntensityCalibration.function("uint"+str(case["bit_depth"]),case["function"],case["coefficients"],"unit",zero_clip=case["zero_clip"],inverted_lut=case["inverted_lut"])
    if case["kind"]=="signed16":return ij.ImageJIntensityCalibration.signed16("unit")
    pattern=case["pattern_words"];count=1<<case["bit_depth"]
    return ij.ImageJIntensityCalibration.from_record({"schema":"imagejxx.intensity-calibration","version":1,"mode":"lookup","value_unit":"unit","bit_depth":case["bit_depth"],
        "table_f32_hex":"".join(f"{pattern[i%len(pattern)]:08x}" for i in range(count)),"source_parameters":{},"signed16":False})


@pytest.mark.parametrize("case",_calibration_table_plot_cases())
def test_calibration_table_plot_matches_imagej_tables(case):
    value=_table_plot_calibration(case);before=value.record
    plot=ij.imagej_intensity_calibration_plot(value)
    np.testing.assert_array_equal(plot.curve_x,case["curve_x"])
    np.testing.assert_array_equal(plot.curve_words,case["curve_words"])
    values=np.asarray(case["curve_words"],np.uint32).view(np.float32)
    assert plot.has_finite_samples==bool(np.isfinite(values).any())
    if all(v is not None for v in case["limits"]):np.testing.assert_array_equal([plot.y_min,plot.y_max],case["limits"])
    assert plot.value_unit=="unit" and plot.x_min==0 and plot.x_max==(1<<case["bit_depth"])-1
    assert value.record==before


def test_calibration_table_plot_admission_cancel_and_callback():
    value=ij.ImageJIntensityCalibration.signed16();calls=[]
    with pytest.raises(ValueError,match="budget"):ij.imagej_intensity_calibration_plot(value,max_working_bytes=1,checkpoint=lambda:calls.append(True))
    assert not calls
    token=ij.CancellationToken()
    with pytest.raises(RuntimeError,match="(?i)cancel"):ij.imagej_intensity_calibration_plot(value,cancellation=token,checkpoint=token.cancel)
    def fail():raise LookupError("table plot callback")
    with pytest.raises(LookupError,match="table plot callback"):ij.imagej_intensity_calibration_plot(value,checkpoint=fail)
    with pytest.raises(ValueError):ij.imagej_intensity_calibration_plot(ij.ImageJIntensityCalibration.uncalibrated())


@pytest.mark.parametrize("kind",["signed16","lookup","empty"])
def test_editor_calibration_table_plot_and_stale_source(kind,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    if kind=="signed16":value=ij.ImageJIntensityCalibration.signed16()
    else:
        bits=8 if kind=="empty" else 16
        case=next(c for c in _calibration_table_plot_cases() if c["kind"]=="lookup" and c["bit_depth"]==bits and (len(c["pattern_words"])==1)==(kind=="empty"))
        value=_table_plot_calibration(case)
    dtype=np.uint8 if value.bit_depth==8 else np.uint16
    owner=ij.Document(ij.Dataset(np.zeros((2,3),dtype),axes="YX"),"table")
    ij.set_imagej_intensity_calibration(owner,value);before=owner.snapshot();steps=owner.undo_steps
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner);editor.action("show_intensity_calibration_plot")
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["visible"] and plot["kind"]=="calibration_table" and plot["finite_geometry"]
    if kind=="empty":assert plot["drawn_segments"]==0 and plot["limits"][2:]==[0,1]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    assert editor.render_headless(4)["fit_plot"] is None


def _assert_fit_csv(report):
    import csv,io
    path=Path(report["fit_csv_path"]);assert "µ" in path.name and path.read_text()==report["fit_csv_text"]
    rows=list(csv.DictReader(io.StringIO(report["fit_csv_text"])))
    data=[r for r in rows if r["series"]=="data"];fit=[r for r in rows if r["series"]=="fit"]
    assert len(data)==3 and len(fit)==100 and len(rows)==103 and report["fit_csv_source_unchanged"]
    plot=report["fit_plot"]
    for group,x,y in [(data,plot["measured"],plot["known"]),(fit,plot["curve_x"],plot["curve_y"])]:
        np.testing.assert_array_equal([float(r["x"]) for r in group],x)
        np.testing.assert_array_equal([float(r["y"]) for r in group],y)
    assert {"fit_plot_export","fit_csv_path","fit_csv_save"}<={c["name"] for c in report["controls"]}


@pytest.mark.parametrize("show",[False,True])
def test_manual_calibration_automatic_plot_and_noop(show,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=ij.Document(ij.Dataset(np.zeros((2,3),np.uint16),axes="YX"),"manual")
    editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    before=owner.snapshot();record=ij.ImageJIntensityCalibration.signed16("HU").record
    for _ in range(2):
        editor.action("set_intensity_calibration",record=record,show_plot=show)
        plot=editor.render_headless(4)["fit_plot"]
        assert (plot is not None)==show
        if show:
            assert plot["kind"]=="calibration_table" and plot["visible"]
            np.testing.assert_array_equal(plot["curve_y"],257*np.arange(256)-32768)
        assert owner.undo_steps==1 and owner.snapshot().revisions.pixels==before.revisions.pixels
    editor.action("show_intensity_function")
    assert editor.render_headless(4)["dialog"]["values"]["show_plot"]==show


@pytest.mark.parametrize("value",[0,1,"true",None])
def test_manual_show_plot_requires_boolean(value,tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    before=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError),match="Boolean"):
        editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.uncalibrated().record,show_plot=value)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==0


def test_manual_plot_budget_failure_preserves_previous_plot_and_preference(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    record=ij.ImageJIntensityCalibration.function("uint16","straight_line",[1,2],"HU").record
    editor.action("set_intensity_calibration",record=record,show_plot=True)
    previous=editor.render_headless(4)["fit_plot"];before=owner.snapshot();steps=owner.undo_steps
    editor.update_settings(processing_memory_mb=1,persist=False)
    # A large, valid unit exceeds the plot producer's declared budget.
    oversized=dict(record,value_unit="u"*(1024*1024))
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("set_intensity_calibration",record=oversized,show_plot=True)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    assert editor.render_headless(4)["fit_plot"]==previous
    editor.action("show_intensity_calibration")
    assert editor.render_headless(4)["dialog"]["values"]["show_plot"] is True


def _assert_plot_interaction(report):
    case=report["plot_interaction"];base=np.asarray(case["hover"]["limits"],dtype=float)
    cursor=case["hover"]["cursor"];assert cursor is not None
    ax=(cursor["x"]-base[0])/(base[1]-base[0]);ay=(cursor["y"]-base[2])/(base[3]-base[2])
    origin=np.asarray(case["hover"]["canvas_origin"]);size=np.asarray(case["hover"]["canvas_size"])
    # ImGui AddMousePosEvent floors screen positions to whole pixels.
    mouse=np.floor(np.asarray(origin+size*.25,dtype=np.float32)).astype(float);normalized=(mouse-origin)/size
    np.testing.assert_allclose([ax,ay],[normalized[0],1-normalized[1]],rtol=0,atol=2e-6)
    factor=2**(-.2*2)
    expected=np.array([ax*(1-factor),ax+(1-ax)*factor,ay*(1-factor),ay+(1-ay)*factor])
    np.testing.assert_allclose(case["zoom"]["viewport"],expected,rtol=0,atol=2e-6)
    delta=(np.floor(np.asarray(origin+size*.35,dtype=np.float32))-mouse)/size
    pan=expected+np.array([-delta[0]*factor,-delta[0]*factor,delta[1]*factor,delta[1]*factor])
    np.testing.assert_allclose(case["pan"]["viewport"],pan,rtol=0,atol=2e-6)
    assert case["reset"]["viewport"]==[0,1,0,1]
    assert case["reset"]["view_limits"]==case["reset"]["limits"]
    for state in case.values():
        if not isinstance(state,dict):continue
        assert state["finite_geometry"] and state["visible"]
        assert state["curve_x"]==case["hover"]["curve_x"]
        assert state["curve_y"]==case["hover"]["curve_y"]
    assert case["source_unchanged"]
    assert "plot_reset_axes" in {c["name"] for c in report["controls"]}


def _assert_plot_axes(report):
    case=report["plot_axes"]
    assert case["initial"]["title"]=="Plot Axis Limits"
    assert case["initial"]["values"]=={"x_min":0,"x_max":65535,"y_min":-32768,"y_max":32767,"log_x":False,"log_y":False,"grid":True,"ticks":True}
    assert case["error"]["open"] and "distinct" in case["error"]["message"]
    assert case["invalid_plot"]["view_limits"]==case["invalid_plot"]["limits"]
    for name in ("applied","cancelled"):
        plot=case[name]
        assert plot["view_limits"]==[200,100,1000,-1000]
        assert plot["finite_geometry"] and plot["visible"]
        assert plot["curve_x"]==report["calibration_table_plot"]["curve_x"]
        assert plot["curve_y"]==report["calibration_table_plot"]["curve_y"]
    assert case["reopened"]["values"]=={"x_min":200,"x_max":100,"y_min":1000,"y_max":-1000,"log_x":False,"log_y":False,"grid":True,"ticks":True}
    assert case["replacement_fixture_action"]=="show_intensity_calibration_plot"
    assert case["stale_error"]["open"] and "plotted data changed" in case["stale_error"]["message"]
    assert case["replacement"]["view_limits"]==case["replacement"]["limits"]
    assert case["source_unchanged"]


def _axis_editor(tmp_path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    owner=_fit_owner();editor=Editor(preferences=Preferences(tmp_path/"preferences.json"));editor.add_native_document(owner)
    editor.action("set_intensity_calibration",record=ij.ImageJIntensityCalibration.signed16("HU").record,show_plot=True)
    editor.render_headless(4)
    return owner,editor


@pytest.mark.parametrize("limits",[[100,200,-1000,1000],[200,100,1000,-1000],[-1e308,1e308,1e308,-1e308]])
def test_editor_numeric_plot_limits_are_view_only(limits,tmp_path):
    owner,editor=_axis_editor(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    editor.action("set_intensity_plot_limits",limits=limits)
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["view_limits"]==limits and plot["finite_geometry"]
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    assert all(getattr(owner.snapshot().revisions,k)==getattr(before.revisions,k) for k in ("pixels","metadata","selection","display"))
    editor.action("show_intensity_calibration_plot")
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["view_limits"]==plot["limits"]


@pytest.mark.parametrize("limits",[[0,0,0,1],[0,1,2,2],[True,1,0,1],["0",1,0,1],[0,1,0],[0,1,0,1,2],None])
def test_invalid_plot_limits_preserve_view_and_source(limits,tmp_path):
    owner,editor=_axis_editor(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    editor.action("set_intensity_plot_limits",limits=[200,100,1000,-1000])
    previous=editor.render_headless(4)["fit_plot"]
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("set_intensity_plot_limits",limits=limits)
    assert editor.render_headless(4)["fit_plot"]==previous
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps


def test_plot_axis_limits_refuse_stale_source(tmp_path):
    owner,editor=_axis_editor(tmp_path)
    view=owner.snapshot().display;view.zoom=2;owner.set_display(view)
    before=owner.snapshot()
    with pytest.raises((ValueError,RuntimeError)):
        editor.action("set_intensity_plot_limits",limits=[0,1,0,1])
    assert owner.snapshot().shares_data(before)
    assert editor.render_headless(4)["fit_plot"] is None


def _assert_plot_log(report):
    case=report["plot_log"]
    assert case["error"]["open"] and "positive" in case["error"]["message"]
    assert not case["invalid_plot"]["log_x"] and not case["invalid_plot"]["log_y"]
    for name in ("hidden","visible"):
        plot=case[name]
        assert plot["log_x"] and plot["log_y"] and plot["view_limits"]==[1000,1,1000,1]
        assert plot["log_notice"]=="Log X/Y: nonpositive values are omitted."
        assert plot["finite_geometry"] and plot["masked_segments"]==128 and plot["drawn_segments"]==127
        assert plot["curve_x"]==report["calibration_table_plot"]["curve_x"]
        assert plot["curve_y"]==report["calibration_table_plot"]["curve_y"]
    assert not case["hidden"]["grid"] and not case["hidden"]["ticks"]
    assert case["hidden"]["grid_lines"]==0 and case["hidden"]["x_ticks"]==case["hidden"]["y_ticks"]==[]
    shown=case["visible"];assert shown["grid"] and shown["ticks"] and shown["grid_lines"]==6
    for axis in ("x_ticks","y_ticks"):
        np.testing.assert_allclose([v["value"] for v in shown[axis]],10.**np.linspace(3,0,5),rtol=1e-13)
        assert all(v["label"] for v in shown[axis])
    assert not report["plot_axes"]["replacement"]["log_x"] and not report["plot_axes"]["replacement"]["log_y"]


@pytest.mark.parametrize("grid,ticks",[(True,True),(True,False),(False,True),(False,False)])
def test_log_plot_grid_ticks_and_data_are_independent(grid,ticks,tmp_path):
    owner,editor=_axis_editor(tmp_path);before=owner.snapshot();steps=owner.undo_steps
    editor.action("set_intensity_plot_limits",limits=[1,1000,1000,1],log_x=True,log_y=True,grid=grid,ticks=ticks)
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["log_x"] and plot["log_y"] and plot["finite_geometry"]
    assert plot["grid"]==grid and plot["ticks"]==ticks and plot["grid_lines"]==(6 if grid else 0)
    assert len(plot["x_ticks"])==len(plot["y_ticks"])==(5 if ticks else 0)
    if ticks:
        np.testing.assert_allclose([v["value"] for v in plot["x_ticks"]],10.**np.linspace(0,3,5),rtol=1e-13)
        np.testing.assert_allclose([v["value"] for v in plot["y_ticks"]],10.**np.linspace(3,0,5),rtol=1e-13)
    assert plot["masked_segments"]==128 and plot["drawn_segments"]==127
    np.testing.assert_array_equal(plot["curve_y"],257*np.arange(256)-32768)
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
    # Omitted options preserve this plot's configuration.
    editor.action("set_intensity_plot_limits",limits=[1,100,100,1])
    updated=editor.render_headless(4)["fit_plot"]
    assert updated["log_x"] and updated["log_y"] and updated["grid"]==grid and updated["ticks"]==ticks


@pytest.mark.parametrize("key",["log_x","log_y","grid","ticks"])
@pytest.mark.parametrize("value",[0,1,"true",None])
def test_plot_axis_options_require_boolean(key,value,tmp_path):
    owner,editor=_axis_editor(tmp_path);before=owner.snapshot();previous=editor.render_headless(4)["fit_plot"]
    with pytest.raises((ValueError,RuntimeError),match="Boolean"):
        editor.action("set_intensity_plot_limits",limits=[1,1000,1,1000],**{key:value})
    assert editor.render_headless(4)["fit_plot"]==previous and owner.snapshot().shares_data(before)


@pytest.mark.parametrize("limits",[[0,10,1,10],[-1,10,1,10],[1,10,-1,10],[1e300,float(np.nextafter(1e300,np.inf)),1,10]])
def test_log_plot_refuses_nonpositive_or_indistinguishable_limits(limits,tmp_path):
    owner,editor=_axis_editor(tmp_path);before=owner.snapshot();previous=editor.render_headless(4)["fit_plot"]
    with pytest.raises((ValueError,RuntimeError),match="positive"):
        editor.action("set_intensity_plot_limits",limits=limits,log_x=True,log_y=True)
    assert editor.render_headless(4)["fit_plot"]==previous and owner.snapshot().shares_data(before)


def test_log_plot_ticks_stay_finite_near_double_maximum(tmp_path):
    owner,editor=_axis_editor(tmp_path);before=owner.snapshot();maximum=float(np.finfo(float).max)
    editor.action("set_intensity_plot_limits",limits=[maximum/2,maximum,maximum,maximum/2],log_x=True,log_y=True)
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["finite_geometry"] and owner.snapshot().shares_data(before)
    for axis in ("x_ticks","y_ticks"):
        assert all(np.isfinite(v["value"]) and maximum/2<=v["value"]<=maximum for v in plot[axis])


def test_log_fit_measurements_use_logarithmic_screen_positions(tmp_path):
    owner,editor=_axis_editor(tmp_path)
    editor.action("fit_intensity_calibration",function="poly2",measured=[0,1,3],known=[1,4,16],show_plot=True)
    editor.wait();before=owner.snapshot();steps=owner.undo_steps
    editor.action("set_intensity_plot_limits",limits=[1,3,1,16],log_x=True,log_y=True)
    plot=editor.render_headless(4)["fit_plot"]
    assert plot["data_screen_points"][0] is None
    origin=np.asarray(plot["canvas_origin"]);size=np.asarray(plot["canvas_size"])
    measured=np.asarray(plot["measured"])[1:];known=np.asarray(plot["known"])[1:]
    expected=np.column_stack((origin[0]+np.log(measured)/np.log(3)*size[0],origin[1]+(1-np.log(known)/np.log(16))*size[1]))
    np.testing.assert_allclose(plot["data_screen_points"][1:],expected,rtol=0,atol=1e-4)
    assert plot["masked_segments"]==1 and plot["drawn_segments"]==98
    assert owner.snapshot().shares_data(before) and owner.undo_steps==steps
