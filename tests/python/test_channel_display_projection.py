"""Installed P06c2 core API contracts, independent of product/reference readers.

Adoption P06c2.V01.002 (032858e4...) owns deterministic first-contributor
Min and empty black. The transition fixtures are the qualified R12--R18
inputs; their expected effects use the frozen integer LUT translation, not
the implementation's mapper, extractor, classifier or command resolver.
"""

import copy
import ctypes
import gc
import importlib
import json
from pathlib import Path
import struct
import sysconfig
import threading
import zlib

import imagejxx as ij
import numpy as np
import pytest


CASE_IDS = {
    "literal_projected_planes": (
        "u8.xy.max", "u8.xy.min", "u8.xz.max", "u8.xz.min", "u8.yz.max", "u8.yz.min",
        "u16.xy.max", "u16.xy.min", "u16.xz.max", "u16.xz.min", "u16.yz.max", "u16.yz.min",
        "f32.xy.max", "f32.xy.min", "f32.xz.max", "f32.xz.min", "f32.yz.max", "f32.yz.min"),
    "v1_compatibility": ("default-and-exact-record",),
    "v2_retention": ("sum.color", "sum.grayscale", "sum.composite", "max.color", "max.grayscale",
                     "max.composite", "min.color", "min.grayscale", "min.composite"),
    "strict_state_fields": (
        "v1.sum", "v1.max", "v1.min", "v2.missing", "v2.none", "v2.bool", "v2.number",
        "v2.unknown", "v2.case", "version.bool", "version.float", "version.future", "extra",
        "schema", "channel.extra", "channel.missing", "channel.bool", "range.inverted",
        "range.nonfinite", "lut.extra", "lut.short", "lut.boolean", "count.short", "count.long"),
    "contributor_edges": ("empty.max", "empty.min", "singleton.max", "singleton.min",
                          "c8.max", "c8.min", "c9.max", "c9.min"),
    "quiet_nan_zero_entry": ("max", "min"),
    "inactive_projection": ("color.sum", "color.max", "color.min", "grayscale.sum",
                            "grayscale.max", "grayscale.min"),
    "captured_kernel_inputs": ("max", "min"),
    "qualified_select_max": ("R12", "R13", "R14", "R15", "R16", "R17", "R18", "R18.repeat"),
    "strict_command_fields": (
        "missing.dependency", "missing.navigation", "extra", "v1.with-v2-fields", "v2.with-v1-state",
        "version.bool", "version.float", "version.future", "dependency.none", "dependency.bool",
        "dependency.negative", "dependency.extra", "navigation.c1", "navigation.bool", "navigation.extra",
        "projection.sum", "mode.color", "reset.nonempty", "set.dependency", "set.navigation"),
    "command_versions": ("set", "reset", "disable", "decode-v2-disable"),
    "fresh_guard_and_replay": ("display", "metadata", "pixels"),
    "complete_rebind": ("set", "reset_range", "select_max"),
    "select_max_controls": ("pre-cancel", "exception", "immutable-gil-lifetime", "working-cap", "pixel-cap-one"),
    "native_v3_transport": ("sum", "max", "min", "future"),
    "detached_no_loss": ("sum", "max", "min"),
}
EXPECTED_COLLECTION_COUNT = 120
EXPECTED_COLLECTION_IDS = tuple(f"test_{name}[{case}]" for name, cases in CASE_IDS.items() for case in cases)
ERRORS = (ValueError, TypeError, OverflowError)
DTYPES = {"u8": np.uint8, "u16": np.uint16, "f32": np.float32}
SCHEMA = "imagejxx.channel-display"
COMMAND = "imagejxx.channel-display-command"
LITERAL_RGB = {
    "xy.max": [[[200,223,183], [200,222,182], [200,221,181]],
               [[200,220,180], [200,219,179], [200,218,178]]],
    "xy.min": [[[32,72,100], [33,73,100], [34,74,100]],
               [[35,75,100], [36,76,100], [37,77,100]]],
    "xz.max": [[[200,222,182], [200,221,181], [200,220,180]],
               [[200,220,180], [200,219,179], [200,218,178]]],
    "xz.min": [[[33,73,100], [34,74,100], [35,75,100]],
               [[35,75,100], [36,76,100], [37,77,100]]],
    "yz.max": [[[200,224,184], [200,221,181]], [[200,222,182], [200,219,179]]],
    "yz.min": [[[31,71,100], [34,74,100]], [[33,73,100], [36,76,100]]],
}


@pytest.fixture(autouse=True)
def installed_native_origin():
    roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("purelib", "platlib")}
    module = importlib.import_module("imagejxx._native")
    path = Path(module.__file__).resolve(strict=True)
    assert path.suffix in {".pyd", ".so"} and any(path.is_relative_to(root) for root in roots)
    assert any(Path(ij.__file__).resolve(strict=True).is_relative_to(root) for root in roots)


def _tables():
    return ([[i, 255-i, 100] for i in range(256)],
            [[200, i, 255-i] for i in range(256)],
            [[255-i, 80, i] for i in range(256)])


def _state(projection="max", mode="composite", count=3, version=2):
    value = {"schema": SCHEMA, "version": version, "mode": mode,
             "channels": [{"visible": True, "range": [0.,255.],
                           "lut": {"table": copy.deepcopy(_tables()[c % 3])}} for c in range(count)]}
    if version == 2:
        value["projection"] = projection
    return value


def _source(dtype=np.uint16, count=3):
    t, c, z, y, x, s = np.indices((2,count,2,2,3,1), dtype=np.int64)
    pixels = ((20 + 40*c) + 10*t + 2*z + 3*y + x + s).astype(dtype)
    cal = ij.SpatialCalibration(spacing=(.5,1.25,2.5), origin=(11,-7,19),
        direction=(-1,.25,.5,0,1,.5,.5,0,1), unit="um", frame_interval=.75,
        time_origin=2.25, time_unit="s")
    return pixels, ij.Dataset(pixels, axes="TCZYXS", calibration=cal)


def _owner(state=None, *, transition=False):
    if transition:
        t,c,z,y,x,s = np.indices((2,3,4,3,5,1), dtype=np.int64)
        pixels = (40*t+30*c+5*z+2*y+x+s).astype(np.uint8)
        source = ij.Dataset(pixels, axes="TCZYXS")
    else:
        pixels, source = _source()
    owner = ij.Document(source, "Projection independent fixture")
    edit = owner.begin_edit()
    edit.set_property("retained", {"nested": [True,7,"retained"]})
    edit.set_extension("future_annotation", {"version":37, "opaque":[3,1,4]})
    edit.set_record_layout({"source_presence":True})
    edit.select(ij.Selection("rectangle", [(0,0),(2,1)], width=2, attributes={"keep":True}))
    edit.set_previous_selection(ij.Selection("point", [(1,1)]))
    edit.set_rois([ij.ROI("stored", ij.Selection("point", [(2,1)]), t=0,c=0,z=1)])
    edit.set_overlays([{"future_annotation":{"version":37,"opaque":[3,1,4]}}])
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("redo_probe",True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1,1,[1,1,2 if transition else 1]
    view.zoom, view.orthogonal, view.linked_zoom = 1.25,True,False
    view.plane_zoom, view.pan = [1.25,2.,3.], [[3.,-2.],[1.,4.],[-5.,2.]]
    view.properties = {"display_range":[7.,19.], "lut":"Fire", "unrelated":{"exact":2**64-1}}
    if state is not None:
        properties = view.properties
        properties["channel_display"] = copy.deepcopy(state)
        view.properties = properties
    owner.set_display(view)
    return pixels,owner


def _selection(value):
    return None if value is None else (value.kind,tuple(map(tuple,value.points)),value.width,value.attributes)


def _data(owner):
    snap,hist = owner.snapshot(),owner.history()
    cal = snap.dataset.calibration
    return (snap.data_identity,snap.dirty,snap.revisions.pixels,snap.revisions.metadata,
            snap.revisions.selection,snap.dataset.numpy("TCZYXS").tobytes(),tuple(snap.dataset.shape),
            snap.dataset.dtype.str,snap.dataset.sample_policy,snap.metadata,snap.record_layout,
            _selection(snap.selection),_selection(snap.previous_selection),
            [(r.name,_selection(r.selection),r.t,r.c,r.z,r.attributes) for r in snap.rois],snap.overlays,
            None if snap.spectrum is None else snap.spectrum.numpy().tobytes(),
            (tuple(cal.spacing),tuple(cal.origin),tuple(cal.direction),cal.unit,
             cal.frame_interval,cal.time_origin,cal.time_unit),
            (hist.undo_steps,hist.redo_steps,hist.step_limit,hist.pixel_budget))


def _navigation(view):
    return (view.t,view.c,view.crosshair,view.orthogonal,view.linked_zoom,view.zoom,view.plane_zoom,view.pan)


def _record(owner,state,action="set",*,version=None,navigation=None,dependency=None,channels=()):
    snap = owner.snapshot()
    version = (state["version"] if state is not None else 1) if version is None else version
    value = {"schema":COMMAND,"version":version,"action":action,
        "source":{"id":owner.id,"pixels_revision":snap.revisions.pixels,
                  "metadata_revision":snap.revisions.metadata,"shape":list(snap.dataset.shape),
                  "type":snap.dataset.dtype.name,"sample_policy":"finite_only"},
        "position":{"t":snap.display.t,"c":snap.display.c,"z":snap.display.crosshair[2]},
        "state":copy.deepcopy(state),"reset":{"channels":list(channels),"empty_finite":[False]*len(channels)}}
    if version == 2:
        value.update(display_dependency=dependency,navigation=navigation)
    return value


@pytest.mark.parametrize("case_id", CASE_IDS["literal_projected_planes"])
def test_literal_projected_planes(case_id):
    dtype,plane,projection = case_id.split(".")
    pixels,source = _source(DTYPES[dtype])
    state = _state(projection)
    before = copy.deepcopy(state)
    actual = ij.compose_channel_display(source,state,plane,1,1,1)
    np.testing.assert_array_equal(actual,np.array(LITERAL_RGB[f"{plane}.{projection}"],np.uint8))
    assert actual.dtype == np.uint8 and actual.flags.writeable
    actual[:] = 0
    np.testing.assert_array_equal(ij.compose_channel_display(source,state,plane,1,1,1),
                                  np.array(LITERAL_RGB[f"{plane}.{projection}"],np.uint8))
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes() and state == before


@pytest.mark.parametrize("case_id", CASE_IDS["v1_compatibility"])
def test_v1_compatibility(case_id):
    _,owner = _owner()
    state = {"schema":SCHEMA,"version":1,"mode":"composite",
        "channels":[{"visible":True,"range":[32.,37.],"lut":{"name":name}}
                    for name in ("Red","Green","Blue")]}
    assert ij.default_channel_display(owner.snapshot().dataset,1,1) == state
    command = ij.resolve_channel_display_command(owner.snapshot(),state)
    expected = _record(owner,state)
    assert command.record() == expected and set(expected) == {"schema","version","action","source","position","state","reset"}
    assert command.version == 1 and command.navigation_channel is None
    assert command.display_dependency_revision is None and command.requires_captured_display is False
    assert "projection" not in command.state
    assert ij.channel_display_command_from_record(owner.snapshot(),expected).record() == expected


@pytest.mark.parametrize("case_id", CASE_IDS["v2_retention"])
def test_v2_retention(case_id):
    projection,mode = case_id.split(".")
    _,owner = _owner()
    state = _state(projection,mode)
    original = copy.deepcopy(state)
    source = owner.snapshot().dataset
    assert ij.validate_channel_display(state,source) is None
    assert ij.normalize_channel_display(state,source) == original
    view = ij.with_channel_display(owner.snapshot().display,source,state)
    assert ij.channel_display_state(view,source) == original
    reset,observed = ij.reset_channel_display_ranges(source,state,1,1,[2,0])
    expected = copy.deepcopy(state)
    expected["channels"][2]["range"],expected["channels"][0]["range"] = [112.,117.],[32.,37.]
    assert reset == expected and observed == [{"range":[112.,117.],"empty_finite":False},
                                             {"range":[32.,37.],"empty_finite":False}]
    command = ij.resolve_channel_display_command(owner.snapshot(),state)
    assert command.version == 2 and command.record() == _record(owner,original)
    assert state == original and command.state == original


@pytest.mark.parametrize("case_id", CASE_IDS["strict_state_fields"])
def test_strict_state_fields(case_id):
    _,source = _source()
    state = _state()
    if case_id.startswith("v1."):
        state.update(version=1,projection=case_id.split(".")[1])
    elif case_id == "v2.missing": del state["projection"]
    elif case_id.startswith("v2."):
        state["projection"] = {"none":None,"bool":True,"number":2,"unknown":"multiply","case":"Max"}[case_id[3:]]
    elif case_id.startswith("version."):
        state["version"] = {"bool":True,"float":2.,"future":3}[case_id[8:]]
    elif case_id == "extra": state["future"] = None
    elif case_id == "schema": state["schema"] = "foreign.channel-display"
    elif case_id == "channel.extra": state["channels"][0]["projection"] = "max"
    elif case_id == "channel.missing": del state["channels"][0]["visible"]
    elif case_id == "channel.bool": state["channels"][0]["visible"] = 1
    elif case_id == "range.inverted": state["channels"][0]["range"] = [7,3]
    elif case_id == "range.nonfinite": state["channels"][0]["range"] = [0,float("nan")]
    elif case_id == "lut.extra": state["channels"][0]["lut"]["name"] = "Red"
    elif case_id == "lut.short": state["channels"][0]["lut"]["table"].pop()
    elif case_id == "lut.boolean": state["channels"][0]["lut"]["table"][0][0] = True
    elif case_id == "count.short": state["channels"].pop()
    else:
        assert case_id == "count.long"
        state["channels"].append(copy.deepcopy(state["channels"][0]))
    before = copy.deepcopy(state)
    with pytest.raises(ERRORS): ij.validate_channel_display(state,source)
    with pytest.raises(ERRORS): ij.normalize_channel_display(state,source)
    # NaN is the only deliberately non-reflexive input.
    if case_id != "range.nonfinite": assert state == before


@pytest.mark.parametrize("case_id", CASE_IDS["contributor_edges"])
def test_contributor_edges(case_id):
    name,projection = case_id.split(".")
    count = {"empty":3,"singleton":1,"c8":8,"c9":9}[name]
    pixels = np.full((1,count,1,1,2,1),17,np.uint16)
    source = ij.Dataset(pixels,axes="TCZYXS")
    state = _state(projection,count=count)
    for c,channel in enumerate(state["channels"]):
        channel["lut"] = {"table":[[c+1,100-c,20+2*c]]*256}
        channel["visible"] = name not in {"empty","c9"}
    if name == "c9":
        with pytest.raises(ValueError): ij.validate_channel_display(state,source)
        state = ij.normalize_channel_display(state,source)
        assert state["mode"] == "color" and state["projection"] == projection and state["version"] == 2
        assert len(state["channels"]) == 9 and not any(c["visible"] for c in state["channels"])
        expected = [9,92,36]
    elif name == "empty": expected = [0,0,0]
    elif name == "singleton": expected = [17,17,17]
    else: expected = [8,100,34] if projection == "max" else [1,93,20]
    image = ij.compose_channel_display(source,state,active_c=count-1)
    np.testing.assert_array_equal(image,np.array([[expected,expected]],np.uint8))
    estimate = ij.estimate_channel_display(source,state,active_c=count-1)
    assert estimate.contributing_channels == ([] if name == "empty" else [count-1] if name in {"singleton","c9"} else list(range(8)))
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()
    if name == "singleton":
        state["channels"][0]["visible"] = False
        np.testing.assert_array_equal(ij.compose_channel_display(source,state),np.zeros((1,2,3),np.uint8))
        assert ij.estimate_channel_display(source,state).contributing_channels == []
    if name == "empty":
        # A preceding populated result must not leave pixel0 or an empty cache.
        state["channels"][0]["visible"] = True
        populated = ij.compose_channel_display(source,state)
        np.testing.assert_array_equal(populated,np.array([[[1,100,20],[1,100,20]]],np.uint8))
        state["channels"][0]["visible"] = False
        for _ in range(2):
            np.testing.assert_array_equal(ij.compose_channel_display(source,state),np.zeros((1,2,3),np.uint8))
    if name in {"c8","c9"}:
        owner = ij.Document(source)
        view = owner.snapshot().display
        view.c = count-1
        properties = view.properties
        properties["channel_display"] = copy.deepcopy(state)
        view.properties = properties
        owner.set_display(view)
        command = ij.resolve_channel_display_command(owner.snapshot(),action="select_max")
        # All input tables are constant: active inversion, then still-inverted
        # C0 constant yields Min. Above eight is the owned Color extension.
        assert command.state["mode"] == ("color" if count == 9 else "composite")
        assert command.state["version"] == 2 and command.state["projection"] == "min"
        assert command.navigation_channel == 0 and len(command.state["channels"]) == count
        assert all(c["visible"] for c in command.state["channels"])
        for c,channel in enumerate(command.state["channels"]):
            original_rgb = [c+1,100-c,20+2*c]
            translated = [v+255-min(original_rgb)-max(original_rgb) for v in original_rgb]
            assert channel["lut"] == {"table":[translated]*256} and channel["range"] == [0.,255.]


@pytest.mark.parametrize("case_id", CASE_IDS["quiet_nan_zero_entry"])
def test_quiet_nan_zero_entry(case_id):
    bits = np.array([0x7fc12345,0x80000000,0x3f800000,0xffc23456],np.uint32)
    pixels = np.tile(bits,2).view(np.float32).reshape(1,2,1,1,4,1)
    source = ij.Dataset(pixels,axes="TCZYXS",sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    state = _state(case_id,count=2)
    for c,channel in enumerate(state["channels"]):
        table = [[0,0,0] for _ in range(256)]
        table[0],table[255] = ([7,11,13],[17,19,23]) if c == 0 else ([3,21,9],[31,2,29])
        channel.update(range=[0,1],lut={"table":table})
    low,high = ([7,21,13],[31,19,29]) if case_id == "max" else ([3,11,9],[17,2,23])
    np.testing.assert_array_equal(ij.compose_channel_display(source,state),np.array([[low,low,high,low]],np.uint8))
    assert source.numpy("TCZYXS").view(np.uint32).ravel().tolist() == np.tile(bits,2).tolist()


@pytest.mark.parametrize("case_id", CASE_IDS["inactive_projection"])
def test_inactive_projection(case_id):
    mode,projection = case_id.split(".")
    _,source = _source()
    state = _state(projection,mode)
    for channel in state["channels"]: channel["visible"] = False
    expected = ([[[200,72,183],[200,73,182],[200,74,181]],
                 [[200,75,180],[200,76,179],[200,77,178]]] if mode == "color" else
                [[[72,72,72],[73,73,73],[74,74,74]],[[75,75,75],[76,76,76],[77,77,77]]])
    np.testing.assert_array_equal(ij.compose_channel_display(source,state,"xy",1,1,1),np.array(expected,np.uint8))


@pytest.mark.parametrize("case_id", CASE_IDS["captured_kernel_inputs"])
def test_captured_kernel_inputs(case_id):
    pixels,source = _source()
    state,called = _state(case_id),[]
    def mutate():
        if not called:
            state["projection"] = "sum"
            state["mode"] = "grayscale"
            state["channels"].clear()
        called.append(True)
    image = ij.compose_channel_display(source,state,"xy",1,1,1,checkpoint=mutate)
    assert called and not state["channels"]
    np.testing.assert_array_equal(image,np.array(LITERAL_RGB[f"xy.{case_id}"],np.uint8))
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()


def _table(name):
    colors = {"red":(1,0,0),"yellow":(1,1,0),"cyan":(0,1,1)}
    if name in colors: return [[i*r,i*g,i*b] for i in range(256) for r,g,b in [colors[name]]]
    if name.startswith("inverted_"):
        return [[255-i*r,255-i*g,255-i*b] for i in range(256) for r,g,b in [colors[name[9:]]]]
    if name == "custom_a": return [[(3*i+7)%256,(5*i+11)%256,(7*i+13)%256] for i in range(256)]
    if name == "custom_b": return [[(11*i+23)%256,(2*i+101)%256,(13*i+3)%256] for i in range(256)]
    if name == "custom_c": return [[(i*i+41)%256,(17*i+5)%256,255-i] for i in range(256)]
    if name == "constant": return [[37,91,143] for _ in range(256)]
    assert name == "last_step_only"
    return [[200-i//2,160-i//3,120-i//4] for i in range(255)]+[[250,240,230]]


def _translated(table):
    # Pinned InvertAllLuts integer-equivalent value inversion; no HSV shortcut
    # or product helper is used to obtain the expected owned table bytes.
    return [[component+255-min(rgb)-max(rgb) for component in rgb] for rgb in table]


def _transition(case_id):
    row = case_id.split(".")[0]
    names = {"R12":("red","yellow","cyan"),"R13":("red","yellow","cyan"),
             "R14":("inverted_red","inverted_yellow","inverted_cyan"),
             "R15":("inverted_red","inverted_yellow","inverted_cyan"),
             "R16":("red","inverted_yellow","custom_c"),"R17":("red","constant","custom_b"),
             "R18":("red","last_step_only","custom_a")}[row]
    mode = "color" if row == "R13" else "grayscale" if row == "R14" else "composite"
    state = _state("sum" if row == "R12" else "max",mode,version=1 if row == "R12" else 2)
    for c,(name,interval,visible) in enumerate(zip(names,([1,201],[7,219],[11,241]),
                                                (False,True,False) if row == "R13" else (True,False,True))):
        state["channels"][c].update(range=interval,visible=visible,lut={"table":_table(name)})
    expected = copy.deepcopy(state)
    expected.update(version=2,mode="composite",projection="min" if row in {"R16","R17","R18"} else "max")
    transform = row in {"R15","R16","R17","R18"}
    if transform:
        for channel in expected["channels"]: channel["lut"]["table"] = _translated(channel["lut"]["table"])
    if mode != "composite":
        for channel in expected["channels"]: channel["visible"] = True
    return state,expected,0 if transform else None,2 if row == "R15" else 1


@pytest.mark.parametrize("case_id", CASE_IDS["qualified_select_max"])
def test_qualified_select_max(case_id):
    state,expected,navigation,c = _transition(case_id)
    _,owner = _owner(state,transition=True)
    view = owner.snapshot().display
    view.c = c
    owner.set_display(view)
    if case_id == "R18.repeat":
        first = ij.resolve_channel_display_command(owner.snapshot(),action="select_max")
        assert first.state == expected and first.navigation_channel == 0
        owner.set_display(ij.command_display_state(first,owner.snapshot()))
        expected = copy.deepcopy(expected)
        expected["projection"] = "max"
        # C0 is now inverted Red, so the real repeated intent transforms every
        # table again and deliberately records C0 even when already selected.
        for channel in expected["channels"]:
            channel["lut"]["table"] = _translated(channel["lut"]["table"])
        navigation = 0
    before,previous,revision = _data(owner),owner.snapshot().display,owner.snapshot().revisions.display
    command = ij.resolve_channel_display_command(owner.snapshot(),action="select_max")
    assert command.action == "select_max" and command.version == 2 and command.source.id == owner.id
    assert command.state == expected and command.navigation_channel == navigation
    assert command.display_dependency_revision == revision and command.requires_captured_display is True
    record = _record(owner,expected,"select_max",dependency={"revision":revision},
                     navigation=None if navigation is None else {"c":0})
    assert command.record() == record
    ij.validate_channel_display_command_record(record)
    replay = ij.channel_display_command_from_record(owner.snapshot(),record)
    assert replay.record() == record and replay.requires_captured_display is False
    assert replay.display_dependency_revision == revision
    next_view = ij.command_display_state(command,owner.snapshot())
    assert next_view.c == (previous.c if navigation is None else 0)
    next_view.c = previous.c
    assert _navigation(next_view) == _navigation(previous)
    assert {k:v for k,v in next_view.properties.items() if k != "channel_display"} == {
        k:v for k,v in previous.properties.items() if k != "channel_display"}
    # Readonly provenance and recursively detached state/record copies.
    for name in ("version","navigation_channel","display_dependency_revision","requires_captured_display","state","source","action"):
        with pytest.raises(AttributeError): setattr(command,name,None)
    detached_state,detached_record = command.state,command.record()
    detached_state["channels"].clear()
    detached_record["source"]["id"] = "foreign"
    assert command.state == expected and command.record() == record and _data(owner) == before


@pytest.mark.parametrize("case_id", CASE_IDS["strict_command_fields"])
def test_strict_command_fields(case_id):
    state,expected,navigation,_ = _transition("R16")
    _,owner = _owner(state,transition=True)
    record = _record(owner,expected,"select_max",dependency={"revision":owner.snapshot().revisions.display},navigation={"c":0})
    if case_id.startswith("missing."): del record[case_id[8:].replace("dependency","display_dependency")]
    elif case_id == "extra": record["future"] = None
    elif case_id == "v1.with-v2-fields": record["version"] = 1
    elif case_id == "v2.with-v1-state":
        record["state"]["version"] = 1
        del record["state"]["projection"]
    elif case_id.startswith("version."):
        record["version"] = {"bool":True,"float":2.,"future":3}[case_id[8:]]
    elif case_id.startswith("dependency."):
        record["display_dependency"] = {"none":None,"bool":{"revision":True},
            "negative":{"revision":-1},"extra":{"revision":0,"future":None}}[case_id[11:]]
    elif case_id.startswith("navigation."):
        record["navigation"] = {"c1":{"c":1},"bool":{"c":False},"extra":{"c":0,"t":0}}[case_id[11:]]
    elif case_id == "projection.sum": record["state"]["projection"] = "sum"
    elif case_id == "mode.color": record["state"]["mode"] = "color"
    elif case_id == "reset.nonempty": record["reset"] = {"channels":[0],"empty_finite":[False]}
    else:
        record["action"] = "set"
        record["display_dependency"],record["navigation"] = None,None
        if case_id == "set.dependency": record["display_dependency"] = {"revision":0}
        else:
            assert case_id == "set.navigation"
            record["navigation"] = {"c":0}
    before = copy.deepcopy(record)
    with pytest.raises(ERRORS) as validation: ij.validate_channel_display_command_record(record)
    with pytest.raises(ERRORS): ij.channel_display_command_from_record(owner.snapshot(),record)
    calls = []
    # Original malformed record must win even over a malformed new request.
    with pytest.raises(type(validation.value)) as rebind:
        ij.rebind_channel_display_command(owner.snapshot(),record,{},action="unknown",checkpoint=lambda:calls.append(True))
    assert str(rebind.value) == str(validation.value)
    assert record == before and not calls


@pytest.mark.parametrize("case_id", CASE_IDS["command_versions"])
def test_command_versions(case_id):
    state = _state("min")
    _,owner = _owner(state)
    before = _data(owner)
    if case_id in {"disable","decode-v2-disable"}:
        command = ij.resolve_channel_display_command(owner.snapshot(),action="disable")
        expected = _record(owner,None,"disable")
        if case_id == "decode-v2-disable":
            expected.update(version=2,display_dependency=None,navigation=None)
            command = ij.channel_display_command_from_record(owner.snapshot(),expected)
        assert command.state is None
    else:
        action = "reset_range" if case_id == "reset" else "set"
        command = ij.resolve_channel_display_command(owner.snapshot(),state,action,channels=[1] if case_id == "reset" else [])
        expected_state = copy.deepcopy(state)
        if case_id == "reset": expected_state["channels"][1]["range"] = [72.,77.]
        expected = _record(owner,expected_state,action,channels=[1] if case_id == "reset" else [])
        assert command.state == expected_state
    assert command.record() == expected and command.version == expected["version"]
    assert command.navigation_channel is None and command.display_dependency_revision is None
    assert command.requires_captured_display is False and _data(owner) == before


@pytest.mark.parametrize("case_id", CASE_IDS["fresh_guard_and_replay"])
def test_fresh_guard_and_replay(case_id):
    state,expected,_,_ = _transition("R16")
    _,owner = _owner(state,transition=True)
    command = ij.resolve_channel_display_command(owner.snapshot(),action="select_max")
    record = command.record()
    view = owner.snapshot().display
    view.t,view.c,view.crosshair,view.zoom = 0,2,[4,2,3],3.75
    owner.set_display(view)
    owner.select(ij.Selection("point",[(3,2)]))
    if case_id != "display":
        edit = owner.begin_edit()
        if case_id == "metadata": edit.set_property("new metadata",True)
        else: edit.replace_plane(np.full((3,5),17,np.uint8),t=1,c=1,z=2)
        owner.commit(edit)
    before,view = _data(owner),owner.snapshot().display
    with pytest.raises(ij.RevisionConflict): ij.command_display_state(command,owner.snapshot())
    if case_id == "display":
        replay = ij.channel_display_command_from_record(owner.snapshot(),record)
        next_view = ij.command_display_state(replay,owner.snapshot())
        assert next_view.properties["channel_display"] == expected and next_view.c == 0
        next_view.c = view.c
        assert _navigation(next_view) == _navigation(view) and replay.record() == record
    else:
        with pytest.raises(ij.RevisionConflict): ij.channel_display_command_from_record(owner.snapshot(),record)
    assert _data(owner) == before and _navigation(owner.snapshot().display) == _navigation(view)


@pytest.mark.parametrize("case_id", CASE_IDS["complete_rebind"])
def test_complete_rebind(case_id):
    source_state,_,_,_ = _transition("R16")
    _,source = _owner(source_state,transition=True)
    original = ij.resolve_channel_display_command(source.snapshot(),action="select_max").record()
    destination_state,select_expected,_,_ = _transition("R12")
    _,destination = _owner(destination_state,transition=True)
    with pytest.raises(ValueError): ij.channel_display_command_from_record(destination.snapshot(),original)
    before = _data(destination)
    replacement = _state("sum")
    if case_id == "reset_range": replacement["channels"][2]["range"] = [7.,19.]
    if case_id == "select_max":
        rebound = ij.rebind_channel_display_command(destination.snapshot(),original,action=case_id)
        expected = select_expected
        assert rebound.requires_captured_display and rebound.display_dependency_revision == destination.snapshot().revisions.display
    else:
        rebound = ij.rebind_channel_display_command(destination.snapshot(),original,replacement,action=case_id,
                                                    channels=[2] if case_id == "reset_range" else [])
        expected = copy.deepcopy(replacement)
        # Qualified ByteProcessor Reset is the full byte interval, irrespective
        # of this live plane's finite sample extrema (110 through 118).
        if case_id == "reset_range": expected["channels"][2]["range"] = [0.,255.]
        assert rebound.navigation_channel is None and rebound.display_dependency_revision is None
    assert rebound.source.id == destination.id and rebound.record()["source"]["id"] == destination.id
    assert rebound.state == expected and rebound.record()["position"] == {"t":1,"c":1,"z":2}
    assert original["source"]["id"] == source.id and original["state"]["projection"] == "min"
    assert _data(destination) == before


@pytest.mark.parametrize("case_id", CASE_IDS["select_max_controls"])
def test_select_max_controls(case_id):
    state,expected,_,_ = _transition("R16")
    _,owner = _owner(state,transition=True)
    before,view = _data(owner),owner.snapshot().display
    snapshot = owner.snapshot()
    calls,thread_errors,holders = [],[],{"source":snapshot,"state":state,"owner":owner}
    class CallbackFailure(RuntimeError): pass
    def checkpoint():
        assert ctypes.pythonapi.PyGILState_Check() == 1
        if case_id == "exception": raise CallbackFailure("checkpoint marker")
        if not calls and case_id == "immutable-gil-lifetime":
            state["channels"].clear()
            changed = owner.snapshot().display
            properties = changed.properties
            properties["channel_display"] = _state("sum")
            changed.properties = properties
            owner.set_display(changed)
            holders.clear()
            gc.collect()
            worker = threading.Thread(target=lambda:calls.append("python worker"))
            worker.start()
            worker.join(timeout=2)
            if worker.is_alive(): thread_errors.append("Python worker blocked")
        calls.append("checkpoint")
    limits = ij.ChannelDisplayLimits()
    token = ij.CancellationToken()
    if case_id == "pre-cancel": token.cancel()
    if case_id == "working-cap": limits.max_working_bytes = 1
    if case_id == "pixel-cap-one": limits.max_pixels = 1
    if case_id in {"pre-cancel","exception","working-cap"}:
        error = {"pre-cancel":ij.OperationCancelled,"exception":CallbackFailure,"working-cap":ValueError}[case_id]
        with pytest.raises(error):
            ij.resolve_channel_display_command(snapshot,action="select_max",limits=limits,cancellation=token,checkpoint=checkpoint)
        if case_id != "exception": assert not calls
        if case_id == "pre-cancel":
            mid_token,mid_calls = ij.CancellationToken(),[]
            def cancel_during_work():
                mid_calls.append(True)
                mid_token.cancel()
            with pytest.raises(ij.OperationCancelled):
                ij.resolve_channel_display_command(snapshot,action="select_max",cancellation=mid_token,checkpoint=cancel_during_work)
            assert mid_calls
        if case_id == "working-cap":
            original = _record(owner,expected,"select_max",dependency={"revision":snapshot.revisions.display},navigation={"c":0})
            with pytest.raises(ValueError):
                ij.rebind_channel_display_command(snapshot,original,action="select_max",limits=limits,checkpoint=checkpoint)
            assert not calls
            for field in ("max_pixels","max_working_bytes"):
                zero = ij.ChannelDisplayLimits()
                setattr(zero,field,0)
                with pytest.raises(ValueError):
                    ij.resolve_channel_display_command(snapshot,action="select_max",limits=zero,checkpoint=checkpoint)
                assert not calls
    else:
        command = ij.resolve_channel_display_command(snapshot,action="select_max",limits=limits,checkpoint=checkpoint)
        assert calls and command.state == expected and command.source.id == owner.id
        if case_id == "immutable-gil-lifetime":
            assert "python worker" in calls and not thread_errors and not holders
            with pytest.raises(ij.RevisionConflict): ij.command_display_state(command,owner.snapshot())
    assert _data(owner) == before
    if case_id != "immutable-gil-lifetime": assert owner.snapshot().display.properties == view.properties


@pytest.mark.parametrize("case_id", CASE_IDS["native_v3_transport"])
def test_native_v3_transport(case_id):
    state = (_state(case_id) if case_id != "future" else
             {"schema":SCHEMA,"version":37,"opaque":{"unsigned":2**64-1,"keep":[3,1,4]}})
    _,owner = _owner(state)
    before = _data(owner)
    record = (ij.resolve_channel_display_command(owner.snapshot(),state).record() if case_id != "future" else
              {"schema":"future.command","version":37,"owner":owner.id})
    encoded = bytes(ij.encode_session(ij.SessionCapture([owner],workflow=[record],results=[{"keep":7}],extensions={"future":37})))
    assert encoded[:10] == b"IMAGEJXX3\n"
    size = struct.unpack("<Q",encoded[10:18])[0]
    raw = zlib.decompress(encoded[18:])
    literal = json.loads(raw)
    assert len(raw) == size and literal["version"] == 3
    assert literal["documents"][0]["display"]["properties"]["channel_display"] == state
    assert literal["workflow"] == [record]
    decoded = ij.decode_session(encoded)
    reopened = decoded.documents[0]
    assert decoded.source_version == 3 and decoded.workflow == [record] and decoded.results == [{"keep":7}]
    assert decoded.extensions == {"future":37} and reopened.id != owner.id
    assert reopened.snapshot().display.properties["channel_display"] == state
    assert reopened.snapshot().dataset.numpy("TCZYXS").tobytes() == owner.snapshot().dataset.numpy("TCZYXS").tobytes()
    assert reopened.snapshot().metadata == owner.snapshot().metadata and not reopened.snapshot().dirty
    assert (reopened.history().undo_steps,reopened.history().redo_steps) == (0,0)
    if case_id == "future":
        with pytest.raises(ValueError): ij.channel_display_state(reopened.snapshot().display,reopened.snapshot().dataset)
    else:
        with pytest.raises(ValueError): ij.channel_display_command_from_record(reopened.snapshot(),record)
        rebound = ij.rebind_channel_display_command(reopened.snapshot(),record,state)
        assert rebound.state == state and rebound.record()["source"]["id"] == reopened.id
    assert _data(owner) == before
    # Native V3 transport is opaque; changing C requires an explicit mapping
    # or removal. Base V01 persistence_and_no_loss.axis_changes retains the
    # qualified future-state remap refusal, independently of Save/Open.
    view = owner.snapshot().display
    display_revision = owner.snapshot().revisions.display
    edit = owner.begin_edit()
    replacement = _source(count=4)[1]
    edit.replace_dataset(replacement)
    with pytest.raises(ValueError): owner.commit(edit)
    assert edit.active and _data(owner) == before
    assert owner.snapshot().display.properties == view.properties
    assert _navigation(owner.snapshot().display) == _navigation(view)
    assert owner.snapshot().revisions.display == display_revision
    if case_id == "future":
        cleared = owner.snapshot().display
        properties = cleared.properties
        del properties["channel_display"]
        cleared.properties = properties
        owner.commit(edit,display=cleared)
        assert not edit.active
        assert owner.snapshot().dataset.shape[1] == 4
        assert owner.snapshot().dataset.numpy("TCZYXS").tobytes() == replacement.numpy("TCZYXS").tobytes()
        assert owner.snapshot().display.properties == {k:v for k,v in view.properties.items() if k != "channel_display"}
        assert _navigation(owner.snapshot().display) == _navigation(view)


@pytest.mark.parametrize("case_id", CASE_IDS["detached_no_loss"])
def test_detached_no_loss(case_id,tmp_path):
    from imagejxx.ui import persistence
    from imagejxx.ui.model import Document
    state = _state(case_id,count=1)
    detached = Document(np.arange(8,dtype=np.uint16).reshape(2,4))
    metadata = detached.metadata()
    before = copy.deepcopy(metadata)
    metadata["channel_display"] = state
    with pytest.raises(ValueError,match="[Cc]hannel display"): detached.load_metadata(metadata)
    assert detached.metadata() == before
    wire = persistence.serialize_document(detached)
    wire["channel_display"] = state
    with pytest.raises(ValueError,match="[Cc]hannel display"): persistence.deserialize_document(wire)
    # Eligible T1/C1 scalar storage isolates the forbidden projection property
    # from the independent scientific-axis detached-model restriction.
    owner = ij.Document(ij.Dataset(np.arange(8,dtype=np.uint16).reshape(2,4),axes="YX"))
    view = owner.snapshot().display
    view.properties = {"channel_display":state}
    owner.set_display(view)
    path = tmp_path / "native.imagejxx"
    ij.save_session(str(path),ij.SessionCapture([owner]))
    destination = {"untouched":{"keep":7}}
    with pytest.raises(ValueError,match="display property"): persistence.load_session(path,extensions=destination)
    assert destination == {"untouched":{"keep":7}}
