"""Installed SelectMin API assertions for P06c3's logical-state policy.

The N07 product expectation deliberately differs from the unchanged Java
reference reader. No processor cache is inferred from rendering history.
"""
import copy
import ctypes
import gc
import threading

import imagejxx as ij
import numpy as np
import pytest
import test_channel_display_projection as shared


CASE_IDS = {
    "native_min_inputs": tuple(f"N{i:02d}" for i in range(1,9)),
    "native_min_repeats": ("N01", "N03", "N07"),
    "typed_min_planes": ("u8", "u16", "f32"),
    "contributor_edges": ("empty", "singleton", "c8", "c9"),
    "quiet_nan_capture": ("captured-min",),
    "strict_min_records": ("v1", "state.v1", "state.sum", "state.future", "extra",
        "dependency.none", "dependency.bool", "dependency.negative", "dependency.extra",
        "navigation.c1", "navigation.bool", "navigation.extra", "max.without-c0",
        "reset.channels", "reset.flags", "source.bool", "position.bool", "action.case",
        "set.effect", "select-max.min-without-c0"),
    "source_guards": ("display", "pixels", "metadata", "foreign"),
    "complete_min_rebind": ("select_min", "set", "disable"),
    "min_controls": ("pre-cancel", "mid-cancel", "exception", "immutable-gil", "cap", "pixel-one"),
    "gray_persistence": ("logical-state-equivalence",),
    "invalid_min_requests": ("state", "channels", "missing-state", "future-state", "float64", "rgb", "rgba"),
}
EXPECTED_COLLECTION_COUNT = 60
EXPECTED_COLLECTION_IDS = tuple(f"test_{name}[{case}]" for name,cases in CASE_IDS.items() for case in cases)
installed_native_origin = shared.installed_native_origin
ERRORS = (ValueError, TypeError, OverflowError)
_data, _navigation = shared._data, shared._navigation


def _input(case):
    # Exact N01--N08 input decisions and native (not Java cache) outcomes.
    names,mode,projection,c = {
        "N01":(("red","yellow","cyan"),"composite",None,1),
        "N02":(("inverted_red","inverted_yellow","inverted_cyan"),"composite","max",2),
        "N03":(("inverted_red","yellow","custom_c"),"composite","max",1),
        "N04":(("red","constant","custom_b"),"composite",None,1),
        "N05":(("red","last_step_only","custom_a"),"composite",None,1),
        "N06":(("red","yellow","cyan"),"color","max",1),
        "N07":(("red","inverted_yellow","custom_c"),"grayscale","max",1),
        "N08":(("red","inverted_yellow","custom_c"),"color","max",1),
    }[case]
    state = {"schema":shared.SCHEMA,"version":1 if projection is None else 2,"mode":mode,
        "channels":[{"visible":v,"range":r,"lut":{"table":shared._table(n)}} for n,r,v in zip(
            names,([1,201],[7,219],[11,241]),(False,True,False) if mode == "color" else (True,False,True))]}
    if projection is not None: state["projection"] = projection
    expected = copy.deepcopy(state)
    expected.update(version=2,mode="composite",projection="max" if case == "N03" else "min")
    transform = case in {"N01","N03","N06","N07"}
    if transform:
        for channel in expected["channels"]: channel["lut"]["table"] = shared._translated(channel["lut"]["table"])
    if mode != "composite":
        for channel in expected["channels"]: channel["visible"] = True
    return state,expected,0 if transform else None,c


def _owner(state, c=1):
    _,owner = shared._owner(state,transition=True)
    view = owner.snapshot().display
    view.c = c
    owner.set_display(view)
    return owner


def _record(owner,state,navigation):
    record = shared._record(owner,state,"select_min",dependency={"revision":owner.snapshot().revisions.display},
                            navigation=None if navigation is None else {"c":0})
    if owner.snapshot().dataset.sample_policy == ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32:
        record["source"]["sample_policy"] = "preserve_quiet_nan_float32"
    return record


def _assert_command(owner,expected,navigation):
    before,snapshot = _data(owner),owner.snapshot()
    command = ij.resolve_channel_display_command(snapshot,action="select_min")
    record = _record(owner,expected,navigation)
    assert command.action == "select_min" and command.version == 2 and command.source.id == owner.id
    assert command.state == expected and command.record() == record
    assert command.navigation_channel == navigation and command.requires_captured_display is True
    assert command.display_dependency_revision == snapshot.revisions.display
    ij.validate_channel_display_command_record(record)
    replay = ij.channel_display_command_from_record(snapshot,record)
    assert replay.record() == record and replay.requires_captured_display is False
    view = ij.command_display_state(command,snapshot)
    assert view.properties["channel_display"] == expected and view.c == (snapshot.display.c if navigation is None else 0)
    view.c = snapshot.display.c
    assert _navigation(view) == _navigation(snapshot.display)
    assert {k:v for k,v in view.properties.items() if k != "channel_display"} == {
        k:v for k,v in snapshot.display.properties.items() if k != "channel_display"}
    for name in ("action","state","version","source","navigation_channel","display_dependency_revision","requires_captured_display"):
        with pytest.raises(AttributeError): setattr(command,name,None)
    detached_state,detached_record = command.state,command.record()
    detached_state["channels"].clear(); detached_record["source"]["id"] = "foreign"
    assert command.state == expected and command.record() == record and _data(owner) == before
    assert owner.snapshot().shares_data(snapshot)
    return command


@pytest.mark.parametrize("case_id",CASE_IDS["native_min_inputs"])
def test_native_min_inputs(case_id):
    state,expected,navigation,c = _input(case_id)
    owner = _owner(state,c)
    _assert_command(owner,expected,navigation)
    assert [channel["range"] for channel in expected["channels"]] == [[1,201],[7,219],[11,241]]


@pytest.mark.parametrize("case_id",CASE_IDS["native_min_repeats"])
def test_native_min_repeats(case_id):
    state,expected,navigation,c = _input(case_id)
    owner = _owner(state,c)
    first = _assert_command(owner,expected,navigation)
    owner.set_display(ij.command_display_state(first,owner.snapshot()))
    repeated = copy.deepcopy(expected)
    repeated["projection"] = "min"
    if case_id == "N03":
        for channel in repeated["channels"]: channel["lut"]["table"] = shared._translated(channel["lut"]["table"])
    # N07 is the declared native Min/Min correction, not Java Max/Max.
    _assert_command(owner,repeated,0 if case_id == "N03" else None)


def _constant_state(count=3):
    rgb = ((200,30,90),(80,150,60),(120,70,220))
    return {"schema":shared.SCHEMA,"version":1,"mode":"composite","channels":[
        {"visible":True,"range":[0,255],"lut":{"table":[list(rgb[c % 3]) for _ in range(256)]}}
        for c in range(count)]}


@pytest.mark.parametrize("case_id",CASE_IDS["typed_min_planes"])
def test_typed_min_planes(case_id):
    pixels,dataset = shared._source(shared.DTYPES[case_id])
    owner = ij.Document(dataset,"Min typed literal")
    state = _constant_state()
    view = owner.snapshot().display
    view.t,view.c,view.crosshair = 1,1,[1,1,1]
    view.properties = {"channel_display":state}
    owner.set_display(view)
    expected = copy.deepcopy(state);expected.update(version=2,projection="min")
    command = _assert_command(owner,expected,None)
    for plane,shape in (("xy",(2,3)),("xz",(2,3)),("yz",(2,2))):
        actual = ij.compose_channel_display(dataset,command.state,plane,1,1,1)
        np.testing.assert_array_equal(actual,np.tile(np.array([80,30,60],np.uint8),(*shape,1)))
    assert dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()


@pytest.mark.parametrize("case_id",CASE_IDS["contributor_edges"])
def test_contributor_edges(case_id):
    count = {"empty":3,"singleton":1,"c8":8,"c9":9}[case_id]
    _,dataset = shared._source(count=count)
    state = _constant_state(count)
    if count > 8: state["mode"] = "color"
    if case_id == "empty":
        for channel in state["channels"]: channel["visible"] = False
    owner = ij.Document(dataset,"Min count edges")
    view = owner.snapshot().display;view.c = count-1;view.properties = {"channel_display":state};owner.set_display(view)
    expected = copy.deepcopy(state);expected.update(version=2,projection="min")
    if count == 1:
        expected["channels"][0]["lut"]["table"] = [[225,55,115] for _ in range(256)]
    if count > 8:
        for channel in expected["channels"]: channel["visible"] = True
    command = _assert_command(owner,expected,0 if count == 1 else None)
    if case_id == "singleton":
        wanted = np.repeat(np.array([[[20],[21],[22]],[[23],[24],[25]]],np.uint8),3,axis=2)
    else:
        rgb = {"empty":[0,0,0],"c8":[80,30,60],"c9":[120,70,220]}[case_id]
        wanted = np.tile(np.array(rgb,np.uint8),(2,3,1))
    np.testing.assert_array_equal(ij.compose_channel_display(dataset,command.state,active_c=count-1),wanted)


@pytest.mark.parametrize("case_id",CASE_IDS["quiet_nan_capture"])
def test_quiet_nan_capture(case_id):
    bits = np.array([0x7fc12345,0x80000000,0x3f800000,0x3f000000],np.uint32)
    raw = np.tile(bits,(1,2,1,1,1,1)).reshape(1,2,1,1,4,1)
    dataset = ij.Dataset(raw.view(np.float32),axes="TCZYXS",sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    state = _constant_state(2)
    state["channels"][0].update(range=[0,1],lut={"table":[[3,11,9]]*255+[[17,2,23]]})
    state["channels"][1].update(range=[0,1],lut={"table":[[7,21,13]]*255+[[31,19,29]]})
    owner = ij.Document(dataset,"Min quiet bits")
    view = owner.snapshot().display;view.c=1;view.properties={"channel_display":state};owner.set_display(view)
    expected=copy.deepcopy(state);expected.update(version=2,projection="min")
    command=_assert_command(owner,expected,None)
    calls=[];captured=command.state
    def checkpoint():
        if not calls: captured["channels"].clear()
        calls.append(True)
    actual=ij.compose_channel_display(dataset,captured,checkpoint=checkpoint)
    assert calls and not captured["channels"] and command.state==expected
    np.testing.assert_array_equal(actual,np.array([[[3,11,9],[3,11,9],[17,2,23],[3,11,9]]],np.uint8))
    assert dataset.numpy("TCZYXS").view(np.uint32).ravel().tolist() == np.tile(bits,2).tolist()


@pytest.mark.parametrize("case_id",CASE_IDS["strict_min_records"])
def test_strict_min_records(case_id):
    state,expected,_,c=_input("N03");owner=_owner(state,c)
    record=_record(owner,expected,0)
    if case_id == "v1": record["version"]=1
    elif case_id == "state.v1": record["state"]["version"]=1;del record["state"]["projection"]
    elif case_id == "state.sum": record["state"]["projection"]="sum"
    elif case_id == "state.future": record["state"]["version"]=37
    elif case_id == "extra": record["future"]=None
    elif case_id.startswith("dependency."):
        record["display_dependency"]={"none":None,"bool":{"revision":True},"negative":{"revision":-1},"extra":{"revision":0,"x":0}}[case_id[11:]]
    elif case_id.startswith("navigation."):
        record["navigation"]={"c1":{"c":1},"bool":{"c":False},"extra":{"c":0,"t":0}}[case_id[11:]]
    elif case_id == "max.without-c0": record["navigation"]=None
    elif case_id == "reset.channels": record["reset"]["channels"]=[0]
    elif case_id == "reset.flags": record["reset"]["empty_finite"]=[False]
    elif case_id == "source.bool": record["source"]["metadata_revision"]=False
    elif case_id == "position.bool": record["position"]["c"]=True
    elif case_id == "action.case": record["action"]="SelectMin"
    elif case_id == "set.effect": record["action"]="set"
    else:
        assert case_id == "select-max.min-without-c0"
        record["action"]="select_max";record["state"]["projection"]="min";record["navigation"]=None
    before,original=_data(owner),copy.deepcopy(record)
    with pytest.raises(ERRORS) as error: ij.validate_channel_display_command_record(record)
    with pytest.raises(ERRORS): ij.channel_display_command_from_record(owner.snapshot(),record)
    callbacks=[]
    with pytest.raises(type(error.value)) as rebound:
        ij.rebind_channel_display_command(owner.snapshot(),record,action="select_min",checkpoint=lambda:callbacks.append(True))
    assert str(rebound.value)==str(error.value) and not callbacks and record==original and _data(owner)==before


@pytest.mark.parametrize("case_id",CASE_IDS["source_guards"])
def test_source_guards(case_id):
    state,expected,navigation,c=_input("N01");owner=_owner(state,c)
    command=ij.resolve_channel_display_command(owner.snapshot(),action="select_min");record=command.record()
    current=owner
    if case_id == "foreign": current=_owner(state,c)
    elif case_id in {"pixels","metadata"}:
        edit=owner.begin_edit()
        if case_id == "pixels": edit.replace_plane(np.full((3,5),7,np.uint8),t=1,c=1,z=2)
        else: edit.set_property("changed",True)
        owner.commit(edit)
    else:
        view=owner.snapshot().display;view.t,view.c,view.crosshair,view.zoom=0,2,[4,2,3],3.75
        view.properties={**view.properties,"channel_display":_input("N02")[0]};owner.set_display(view)
        owner.select(ij.Selection("point",[(3,2)]))
    before,view=_data(current),current.snapshot().display
    with pytest.raises((ij.RevisionConflict,ValueError)): ij.command_display_state(command,current.snapshot())
    if case_id == "display":
        replay=ij.channel_display_command_from_record(current.snapshot(),record)
        result=ij.command_display_state(replay,current.snapshot())
        assert result.properties["channel_display"]==expected and result.c==0 and not replay.requires_captured_display
        result.c=view.c
        assert _navigation(result)==_navigation(view) and replay.record()==record
    else:
        with pytest.raises((ij.RevisionConflict,ValueError)): ij.channel_display_command_from_record(current.snapshot(),record)
    assert _data(current)==before and _navigation(current.snapshot().display)==_navigation(view)


@pytest.mark.parametrize("case_id",CASE_IDS["complete_min_rebind"])
def test_complete_min_rebind(case_id):
    state,_,_,c=_input("N01");source=_owner(state,c)
    record=ij.resolve_channel_display_command(source.snapshot(),action="select_min").record()
    state,expected,nav,c=_input("N08");destination=_owner(state,c)
    before=_data(destination)
    if case_id == "select_min": command=ij.rebind_channel_display_command(destination.snapshot(),record,action=case_id)
    elif case_id == "disable": command=ij.rebind_channel_display_command(destination.snapshot(),record,action=case_id);expected=None;nav=None
    else:
        expected=_constant_state();expected.update(version=2,projection="sum")
        command=ij.rebind_channel_display_command(destination.snapshot(),record,expected,action="set");nav=None
    assert command.state==expected and command.navigation_channel==nav and command.source.id==destination.id
    assert command.record()["source"]["id"]==destination.id and command.action==case_id
    assert record["source"]["id"]==source.id and _data(destination)==before
    malformed=copy.deepcopy(record);malformed["navigation"]={"c":True}
    with pytest.raises(ERRORS): ij.rebind_channel_display_command(destination.snapshot(),malformed,action=case_id)
    assert _data(destination)==before


@pytest.mark.parametrize("case_id",CASE_IDS["min_controls"])
def test_min_controls(case_id):
    state,expected,_,c=_input("N01");owner=_owner(state,c)
    snapshot,before=owner.snapshot(),_data(owner)
    calls,errors,holders=[],[],{"owner":owner,"snapshot":snapshot,"state":state}
    token=ij.CancellationToken();limits=ij.ChannelDisplayLimits()
    class CallbackFailure(RuntimeError): pass
    def checkpoint():
        assert ctypes.pythonapi.PyGILState_Check()==1
        calls.append(True)
        if case_id == "exception": raise CallbackFailure("Min checkpoint")
        if case_id == "mid-cancel": token.cancel()
        if case_id == "immutable-gil" and len(calls)==1:
            state["channels"].clear()
            view=owner.snapshot().display;view.zoom=4;owner.set_display(view)
            holders.clear();gc.collect()
            worker=threading.Thread(target=lambda:calls.append("thread"));worker.start();worker.join(timeout=2)
            if worker.is_alive(): errors.append("GIL retained")
    if case_id == "pre-cancel": token.cancel()
    if case_id == "cap": limits.max_working_bytes=1
    if case_id == "pixel-one": limits.max_pixels=1
    error={"pre-cancel":ij.OperationCancelled,"mid-cancel":ij.OperationCancelled,"exception":CallbackFailure,"cap":ValueError}.get(case_id)
    if error:
        with pytest.raises(error): ij.resolve_channel_display_command(snapshot,action="select_min",limits=limits,cancellation=token,checkpoint=checkpoint)
        assert bool(calls)==(case_id in {"mid-cancel","exception"})
    else:
        command=ij.resolve_channel_display_command(snapshot,action="select_min",limits=limits,checkpoint=checkpoint)
        assert command.state==expected and calls and not errors
        if case_id == "immutable-gil":
            assert not holders and "thread" in calls
            with pytest.raises(ij.RevisionConflict): ij.command_display_state(command,owner.snapshot())
    assert _data(owner)==before


@pytest.mark.parametrize("case_id",CASE_IDS["gray_persistence"])
def test_gray_persistence(case_id):
    state,expected,_,c=_input("N07");owner=_owner(state,c)
    command=_assert_command(owner,expected,0);owner.set_display(ij.command_display_state(command,owner.snapshot()))
    record=command.record();before=_data(owner)
    session=ij.decode_session(ij.encode_session(ij.SessionCapture([owner],workflow=[record])))
    reopened=session.documents[0]
    assert reopened.id!=owner.id and session.workflow==[record]
    assert reopened.snapshot().display.properties["channel_display"]==expected
    assert reopened.snapshot().dataset.numpy("TCZYXS").tobytes()==owner.snapshot().dataset.numpy("TCZYXS").tobytes()
    assert reopened.snapshot().metadata==owner.snapshot().metadata
    repeated=_assert_command(owner,expected,None)
    reopened_repeat=_assert_command(reopened,expected,None)
    fresh=_owner(expected,0)
    fresh_repeat=_assert_command(fresh,expected,None)
    assert repeated.state==reopened_repeat.state==fresh_repeat.state and repeated.navigation_channel is None
    with pytest.raises(ValueError): ij.channel_display_command_from_record(reopened.snapshot(),record)
    rebound=ij.rebind_channel_display_command(reopened.snapshot(),record,action="select_min")
    assert rebound.state==expected and rebound.navigation_channel is None and _data(owner)==before


@pytest.mark.parametrize("case_id",CASE_IDS["invalid_min_requests"])
def test_invalid_min_requests(case_id):
    state,_,_,c=_input("N01");owner=_owner(state,c)
    kwargs={"action":"select_min"}
    if case_id == "state": kwargs["state"]=state
    elif case_id == "channels": kwargs["channels"]=[0]
    else:
        if case_id in {"float64","rgb","rgba"}:
            dtype,samples={"float64":(np.float64,1),"rgb":(np.uint8,3),"rgba":(np.float32,4)}[case_id]
            owner=ij.Document(ij.Dataset(np.zeros((2,3,4,3,5,samples),dtype),axes="TCZYXS"))
        view=owner.snapshot().display
        properties=view.properties
        if case_id == "future-state": properties["channel_display"]={"schema":shared.SCHEMA,"version":37,"opaque":[3,1,4]}
        else: properties.pop("channel_display",None)
        view.properties=properties;owner.set_display(view)
    before,view=_data(owner),owner.snapshot().display
    calls=[]
    with pytest.raises(ERRORS): ij.resolve_channel_display_command(owner.snapshot(),checkpoint=lambda:calls.append(True),**kwargs)
    assert not calls and _data(owner)==before and owner.snapshot().display.properties==view.properties
