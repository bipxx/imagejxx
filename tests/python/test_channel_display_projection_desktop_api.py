"""Installed shared desktop controller APIs for P06c2 projection commands.

Both entry points call the same C++ owner. These are API/publication checks,
not physical-input acceptance. Expectations do not import another test or use
the product's compositor, LUT lookup or resolver as an oracle.
"""

import copy
import importlib
from pathlib import Path
import sysconfig

import numpy as np
import pytest


LAYERS = ("binding", "wrapper")
TEST_NAMES = (
    "test_explicit_projection_addresses_inactive_owner",
    "test_select_max_resolves_min_and_ordinary_edits_retain_v2",
    "test_strict_replay_uses_recorded_effect_and_fresh_navigation",
    "test_complete_rebind_uses_destination_state",
    "test_native_save_open_keeps_state_and_historical_binding",
    "test_future_state_is_opaque_and_disable_remains_v1",
    "test_stale_pixel_and_metadata_refusals_are_atomic",
    "test_malformed_original_precedes_replacement_and_closed_owner",
    "test_unsupported_sources_and_exact_request_refusals",
)
EXPECTED_COLLECTION_COUNT = 18
EXPECTED_COLLECTION_IDS = tuple(f"{name}[{layer}]" for name in TEST_NAMES for layer in LAYERS)
SCHEMA = "imagejxx.channel-display"
COMMAND = "imagejxx.channel-display-command"


@pytest.fixture
def editor(tmp_path):
    global ij
    ij = importlib.import_module("imagejxx")
    # This explicit installed gate has no optional-module fallback or skip.
    desktop = importlib.import_module("imagejxx._desktop")
    roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("purelib", "platlib")}
    for module in (desktop,importlib.import_module("imagejxx._native")):
        path = Path(module.__file__).resolve(strict=True)
        assert path.suffix in {".pyd", ".so"} and any(path.is_relative_to(root) for root in roots)
    assert any(Path(ij.__file__).resolve(strict=True).is_relative_to(root) for root in roots)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def _state(projection="max",mode="composite",count=3):
    # The active C1 is inverted, while C0 Red becomes inverted after the
    # all-channel value inversion. This owned intent therefore resolves Min.
    tables = ([[i,0,0] for i in range(256)],
              [[255-i,255-i,255] for i in range(256)],
              [[(i*i+41)%256,(17*i+5)%256,255-i] for i in range(256)])
    return {"schema":SCHEMA,"version":2,"mode":mode,"projection":projection,
            "channels":[{"visible":c != 1,"range":[0.,255.],
                         "lut":{"table":copy.deepcopy(tables[c % 3])}} for c in range(count)]}


def _max_effect(state):
    # Frozen V01.002 integer effect on these independently selected inputs.
    expected = copy.deepcopy(state)
    expected.update(version=2,mode="composite",projection="min")
    for channel in expected["channels"]:
        table = channel["lut"]["table"]
        channel["lut"] = {"table":[[v+255-min(rgb)-max(rgb) for v in rgb] for rgb in table]}
    if state["mode"] != "composite":
        for channel in expected["channels"]: channel["visible"] = True
    return expected


def _request(action="set",state=None,channels=()):
    return {"action":action,"state":copy.deepcopy(state),"channels":list(channels)}


def _owner(editor,state=None,*,dtype=np.uint16,samples=1,attach=True):
    t,c,z,y,x,s = np.indices((2,3,4,3,5,samples),dtype=np.int64)
    pixels = (40*t+30*c+5*z+2*y+x+2*s).astype(dtype)
    kwargs = {}
    if dtype == np.float32 and samples == 1:
        pixels.view(np.uint32)[1,1,2,1,2,0] = 0x7fc12345
        pixels.view(np.uint32)[1,1,2,2,3,0] = 0x80000000
        kwargs["sample_policy"] = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    cal = ij.SpatialCalibration(spacing=(.5,1.25,2.5),origin=(11,-7,19),
        direction=(-1,.25,.5,0,1,.5,.5,0,1),unit="um",frame_interval=.75,time_origin=2.25,time_unit="s")
    owner = ij.Document(ij.Dataset(pixels,axes="TCZYXS",calibration=cal,**kwargs),"Desktop projection")
    edit = owner.begin_edit()
    edit.set_property("source_only",{"nested":[True,7,"retained"]})
    edit.set_extension("future_annotation",{"version":37,"opaque":[3,1,4]})
    edit.set_record_layout({"source_presence":True})
    edit.select(ij.Selection("rectangle",[(1,0),(4,3)],width=2,attributes={"keep":True}))
    edit.set_previous_selection(ij.Selection("line",[(0,0),(2,2)]))
    edit.set_rois([ij.ROI("stored",ij.Selection("rectangle",[(1,0),(3,2)]),t=0,c=0,z=0)])
    edit.set_overlays([{"future_annotation":{"version":37,"opaque":[3,1,4]}}])
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("redo_probe",True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.t,view.c,view.crosshair = 1,1,[1,1,2]
    view.zoom,view.orthogonal,view.linked_zoom = 1.25,True,False
    view.plane_zoom,view.pan = [1.25,2.,3.],[[3.,-2.],[1.,4.],[-5.,2.]]
    view.properties = {"display_range":[7.,19.],"lut":"Ice","custom_lut":[[i,255-i,0] for i in range(256)]}
    if state is not None:
        properties = view.properties
        properties["channel_display"] = copy.deepcopy(state)
        view.properties = properties
    owner.set_display(view)
    if attach:
        editor.add_native_document(owner)
    assert owner.snapshot().dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()
    return owner


def _selection(value):
    return None if value is None else (value.kind,tuple(map(tuple,value.points)),value.width,value.attributes)


def _data(owner):
    snap,hist = owner.snapshot(),owner.history()
    cal = snap.dataset.calibration
    return (snap.data_identity,snap.dirty,snap.revisions.pixels,snap.revisions.metadata,snap.revisions.selection,
        snap.dataset.numpy("TCZYXS").tobytes(),tuple(snap.dataset.shape),snap.dataset.dtype.str,snap.dataset.sample_policy,
        snap.metadata,snap.record_layout,_selection(snap.selection),_selection(snap.previous_selection),
        [(r.name,_selection(r.selection),r.t,r.c,r.z,r.attributes) for r in snap.rois],snap.overlays,
        None if snap.spectrum is None else snap.spectrum.numpy().tobytes(),
        (tuple(cal.spacing),tuple(cal.origin),tuple(cal.direction),cal.unit,cal.frame_interval,cal.time_origin,cal.time_unit),
        (hist.undo_steps,hist.redo_steps,hist.step_limit,hist.pixel_budget))


def _navigation(view):
    return (view.t,view.c,view.crosshair,view.orthogonal,view.linked_zoom,view.zoom,view.plane_zoom,view.pan)


def _full(owner):
    snap = owner.snapshot()
    return (_data(owner),snap.revisions.display,snap.display.revision,_navigation(snap.display),snap.display.properties)


def _collection(editor):
    return (editor.active_index,editor.recording,editor.busy,editor.results,editor.workflow,
            [(info["id"],_full(editor._editor.native_document(info["id"]))) for info in editor.document_infos])


def _stored(owner):
    return owner.snapshot().display.properties.get("channel_display")


def _call(editor,layer,name,*args,**kwargs):
    return getattr(editor if layer == "wrapper" else editor._editor,name)(*args,**kwargs)


def _record(owner,state,action="set",*,dependency=None,navigation=None,version=2):
    snap = owner.snapshot()
    policy = "preserve_quiet_nan_float32" if snap.dataset.dtype == np.dtype(np.float32) else "finite_only"
    value = {"schema":COMMAND,"version":version,"action":action,
        "source":{"id":owner.id,"pixels_revision":snap.revisions.pixels,"metadata_revision":snap.revisions.metadata,
                  "shape":list(snap.dataset.shape),"type":snap.dataset.dtype.name,"sample_policy":policy},
        "position":{"t":snap.display.t,"c":snap.display.c,"z":snap.display.crosshair[2]},
        "state":copy.deepcopy(state),"reset":{"channels":[],"empty_finite":[]}}
    if version == 2: value.update(display_dependency=dependency,navigation=navigation)
    return value


def _same_owner(editor,owner):
    assert editor.live_document(owner.id).native_owner.id == owner.id
    assert _full(editor.live_document(owner.id).native_owner) == _full(owner)
    assert _full(editor._editor.native_document(owner.id)) == _full(owner)


@pytest.mark.parametrize("layer",LAYERS)
def test_explicit_projection_addresses_inactive_owner(editor,layer):
    owner,other = _owner(editor,dtype=np.float32),_owner(editor)
    editor.recording,editor.results = True,[{"retained":7}]
    before,untouched,snapshot = _data(owner),_full(other),owner.snapshot()
    view,legacy = snapshot.display,copy.deepcopy(snapshot.display.properties)
    for projection in ("max","min"):
        state = _state(projection)
        request = _request(state=state)
        original,revision = copy.deepcopy(request),owner.snapshot().revisions.display
        expected = _record(owner,state)
        assert _call(editor,layer,"apply_channel_display",request,document_id=owner.id) is None
        assert request == original and _stored(owner) == state and editor.workflow[-1] == expected
        assert owner.snapshot().revisions.display == revision+1
        assert _navigation(owner.snapshot().display) == _navigation(view)
        assert {k:v for k,v in owner.snapshot().display.properties.items() if k != "channel_display"} == legacy
        assert owner.snapshot().shares_data(snapshot) and _data(owner) == before and _full(other) == untouched
    assert len(editor.workflow) == 2 and editor.active_index == 1 and editor.results == [{"retained":7}]
    _same_owner(editor,owner)


@pytest.mark.parametrize("layer",LAYERS)
def test_select_max_resolves_min_and_ordinary_edits_retain_v2(editor,layer):
    state = _state()
    owner = _owner(editor,state)
    editor.recording = True
    before,view,revision = _data(owner),owner.snapshot().display,owner.snapshot().revisions.display
    expected = _max_effect(state)
    record = _record(owner,expected,"select_max",dependency={"revision":revision},navigation={"c":0})
    _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=owner.id)
    assert _stored(owner) == expected and owner.snapshot().display.c == 0 and editor.workflow == [record]
    view.c = 0
    assert _navigation(owner.snapshot().display) == _navigation(view)
    # Existing edit actions retain the resolved V2/Min identity.
    editor.action("display_range",minimum=11.,maximum=91.,document_id=owner.id)
    expected["channels"][0]["range"] = [11.,91.]
    editor.action("lut",name="Blue",document_id=owner.id)
    expected["channels"][0]["lut"] = {"name":"Blue"}
    assert _stored(owner) == expected and _data(owner) == before
    # Ordinary Composite is a distinct explicit Sum selection, still V2.
    expected["projection"] = "sum"
    _call(editor,layer,"apply_channel_display",_request(state=expected),document_id=owner.id)
    assert _stored(owner) == expected and editor.workflow[-1]["version"] == 2
    assert editor.workflow[-1]["display_dependency"] is None and editor.workflow[-1]["navigation"] is None
    _same_owner(editor,owner)


@pytest.mark.parametrize("layer",LAYERS)
def test_strict_replay_uses_recorded_effect_and_fresh_navigation(editor,layer):
    owner = _owner(editor,_state())
    editor.recording = True
    _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=owner.id)
    record = copy.deepcopy(editor.workflow[-1])
    expected = copy.deepcopy(record["state"])
    view = owner.snapshot().display
    view.t,view.c,view.crosshair,view.zoom = 0,2,[4,2,3],3.75
    properties = view.properties
    properties["channel_display"] = _state("sum","color")
    view.properties = properties
    owner.set_display(view)
    owner.select(ij.Selection("point",[(3,2)]))
    before,count = _data(owner),len(editor.workflow)
    editor.recording = False
    _call(editor,layer,"replay_channel_display",record,document_id=owner.id)
    assert _stored(owner) == expected and owner.snapshot().display.c == 0
    view.c = 0
    assert _navigation(owner.snapshot().display) == _navigation(view) and _data(owner) == before
    assert len(editor.workflow) == count and editor.workflow[0] == record
    # Another strict replay must not invert tables or reinterpret fresh mode.
    _call(editor,layer,"replay_channel_display",record,document_id=owner.id)
    assert _stored(owner) == expected and len(editor.workflow) == count and _data(owner) == before
    _same_owner(editor,owner)


@pytest.mark.parametrize("layer",LAYERS)
def test_complete_rebind_uses_destination_state(editor,layer):
    source,destination = _owner(editor,_state()),_owner(editor,_state("sum","color"))
    editor.recording = True
    _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=source.id)
    original = copy.deepcopy(editor.workflow[0])
    before = _collection(editor)
    with pytest.raises(ValueError): _call(editor,layer,"replay_channel_display",original,document_id=destination.id)
    assert _collection(editor) == before
    # A full Set rebind has no inherited transform, Min or C0 navigation.
    replacement = _state("sum","color")
    request = _request(state=replacement)
    request_before,data,view = copy.deepcopy(request),_data(destination),destination.snapshot().display
    expected = _record(destination,replacement)
    _call(editor,layer,"replay_channel_display",original,rebind_request=request,document_id=destination.id)
    assert request == request_before and _stored(destination) == replacement and editor.workflow[-1] == expected
    assert _navigation(destination.snapshot().display) == _navigation(view) and _data(destination) == data
    # A full SelectMax rebind consumes destination's current Color/active LUT.
    expected_state = _max_effect(replacement)
    revision = destination.snapshot().revisions.display
    expected_record = _record(destination,expected_state,"select_max",dependency={"revision":revision},navigation={"c":0})
    _call(editor,layer,"replay_channel_display",original,rebind_request=_request("select_max"),document_id=destination.id)
    assert _stored(destination) == expected_state and editor.workflow[-1] == expected_record
    assert editor.workflow[0] == original and original["source"]["id"] == source.id
    assert all(channel["visible"] for channel in expected_state["channels"]) and _data(destination) == data
    _same_owner(editor,destination)


@pytest.mark.parametrize("layer",LAYERS)
def test_native_save_open_keeps_state_and_historical_binding(editor,layer,tmp_path):
    owner = _owner(editor,_state(),dtype=np.float32)
    editor.recording,editor.results = True,[{"retained":13}]
    _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=owner.id)
    records,expected = copy.deepcopy(editor.workflow),copy.deepcopy(_stored(owner))
    before = _data(owner)
    path = tmp_path / "projection.ijx"
    editor.action("save_document",path=str(path),document_id=owner.id)
    assert path.read_bytes()[:10] == b"IMAGEJXX3\n" and _data(owner) == before
    _call(editor,layer,"open_file",str(path))
    ids = [info["id"] for info in editor.document_infos if info["id"] != owner.id]
    assert len(ids) == 1
    reopened = editor._editor.native_document(ids[0])
    assert reopened.id != owner.id and _stored(reopened) == expected and editor.workflow == records
    old,new = owner.snapshot(),reopened.snapshot()
    assert new.dataset.numpy("TCZYXS").tobytes() == old.dataset.numpy("TCZYXS").tobytes()
    assert new.metadata == old.metadata and new.record_layout == old.record_layout
    assert _selection(new.selection) == _selection(old.selection) and _selection(new.previous_selection) == _selection(old.previous_selection)
    assert new.overlays == old.overlays and not new.dirty
    assert (reopened.history().undo_steps,reopened.history().redo_steps) == (0,0)
    before_collection = _collection(editor)
    with pytest.raises(ValueError): _call(editor,layer,"replay_channel_display",records[0],document_id=reopened.id)
    assert _collection(editor) == before_collection
    editor.recording = False
    _call(editor,layer,"replay_channel_display",records[0],rebind_request=_request(state=expected),document_id=reopened.id)
    assert _stored(reopened) == expected and editor.workflow == records
    assert records[0]["source"]["id"] == owner.id
    _same_owner(editor,reopened)


@pytest.mark.parametrize("layer",LAYERS)
def test_future_state_is_opaque_and_disable_remains_v1(editor,layer):
    future = {"schema":SCHEMA,"version":37,"opaque":{"unsigned":2**64-1,"values":[3,1,4]}}
    owner = _owner(editor,future,attach=False)
    editor.recording = True
    before = _collection(editor)
    original = _full(owner)
    # A future owner is native transport data, not an admitted desktop source.
    # Admission must refuse it before the frontend installs its validator.
    with pytest.raises(ValueError): _call(editor,layer,"add_native_document",owner)
    assert _collection(editor) == before and _full(owner) == original
    with pytest.raises(ValueError): ij.resolve_channel_display_command(owner.snapshot(),action="select_max")
    assert _collection(editor) == before and _stored(owner) == future
    # Explicit native removal does not interpret the opaque property. Only the
    # cleared owner can be attached; never bypass a live facade constraint.
    data,view = _data(owner),owner.snapshot().display
    native_expected = _record(owner,None,"disable",version=1)
    clearing = ij.resolve_channel_display_command(owner.snapshot(),action="disable")
    assert clearing.record() == native_expected
    owner.set_display(ij.command_display_state(clearing,owner.snapshot()))
    assert _data(owner) == data and _stored(owner) is None and _collection(editor) == before
    assert _navigation(owner.snapshot().display) == _navigation(view)
    assert owner.snapshot().display.properties == {k:v for k,v in view.properties.items() if k != "channel_display"}
    _call(editor,layer,"add_native_document",owner)
    # Stage a supported V2 property through the constrained native owner, then
    # exercise the real public desktop Disable through each API surface.
    supported = owner.snapshot().display
    properties = supported.properties
    properties["channel_display"] = _state()
    supported.properties = properties
    owner.set_display(supported)
    data,view = _data(owner),owner.snapshot().display
    expected = _record(owner,None,"disable",version=1)
    _call(editor,layer,"apply_channel_display",_request("disable"),document_id=owner.id)
    assert _stored(owner) is None and editor.workflow == [expected] and _data(owner) == data
    assert _navigation(owner.snapshot().display) == _navigation(view)
    assert owner.snapshot().display.properties == {k:v for k,v in view.properties.items() if k != "channel_display"}
    _same_owner(editor,owner)


@pytest.mark.parametrize("layer",LAYERS)
def test_stale_pixel_and_metadata_refusals_are_atomic(editor,layer):
    for change in ("pixels","metadata"):
        owner = _owner(editor,_state())
        editor.recording = True
        _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=owner.id)
        record = copy.deepcopy(editor.workflow[-1])
        edit = owner.begin_edit()
        if change == "pixels": edit.replace_plane(np.full((3,5),7,np.uint16),t=1,c=1,z=2)
        else: edit.set_property("new metadata",True)
        owner.commit(edit)
        before = _collection(editor)
        with pytest.raises(ij.RevisionConflict): _call(editor,layer,"replay_channel_display",record,document_id=owner.id)
        assert _collection(editor) == before


@pytest.mark.parametrize("layer",LAYERS)
def test_malformed_original_precedes_replacement_and_closed_owner(editor,layer):
    owner = _owner(editor,_state())
    editor.recording = True
    _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=owner.id)
    record = copy.deepcopy(editor.workflow[0])
    for replacement in (True,{},_request("unknown",{})):
        before = _collection(editor)
        with pytest.raises(ValueError,match="[Ss]chema|[Uu]nsupported|[Mm]issing"):
            _call(editor,layer,"replay_channel_display",{},rebind_request=replacement,document_id=owner.id)
        assert _collection(editor) == before
    bad = copy.deepcopy(record)
    bad["navigation"] = {"c":1}
    before = _collection(editor)
    with pytest.raises(ValueError): _call(editor,layer,"replay_channel_display",bad,rebind_request=_request("select_max"),document_id=owner.id)
    assert _collection(editor) == before
    editor.close_document(owner.id,discard=True)
    before = _collection(editor)
    with pytest.raises(IndexError): _call(editor,layer,"replay_channel_display",record,document_id=owner.id)
    assert _collection(editor) == before


@pytest.mark.parametrize("layer",LAYERS)
def test_unsupported_sources_and_exact_request_refusals(editor,layer):
    owner = _owner(editor,_state())
    invalid = (_request("select_max",_state()),_request("select_max",channels=[0]),
               {"action":"select_max","state":None,"channels":[],"projection":"max"},
               {"action":"select_max","state":None},_request("select_min_unknown"))
    for request in invalid:
        before = _collection(editor)
        with pytest.raises((ValueError,TypeError)): _call(editor,layer,"apply_channel_display",request,document_id=owner.id)
        assert _collection(editor) == before
    # No implicit enable from absent state, and no scalar interpretation of
    # legacy Float64 or RGB/RGBA source storage.
    for dtype,samples in ((np.uint16,1),(np.float64,1),(np.uint8,3),(np.float32,4)):
        unsupported = _owner(editor,dtype=dtype,samples=samples)
        before = _collection(editor)
        with pytest.raises(ValueError): _call(editor,layer,"apply_channel_display",_request("select_max"),document_id=unsupported.id)
        assert _collection(editor) == before
    _same_owner(editor,owner)
