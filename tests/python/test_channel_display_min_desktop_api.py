"""Installed shared C++ controller APIs for the explicit SelectMin action.

These are real binding/wrapper publication checks, not physical UI evidence.
The generic owner/preservation helpers are test-owned; Min effects are fixed
here independently of the product's classifier and compositor.
"""
import copy

import numpy as np
import pytest
import test_channel_display_projection_desktop_api as shared
import test_channel_display_min as core


LAYERS = ("binding","wrapper")
TEST_NAMES = (
    "test_min_inactive_owner_and_exact_record",
    "test_min_gray_first_repeat_and_recording",
    "test_min_strict_replay_preserves_fresh_navigation",
    "test_min_complete_rebind_and_set_alternative",
    "test_min_native_save_open_logical_equivalence",
    "test_min_strict_request_parser_is_atomic",
    "test_min_malformed_original_and_closed_owner",
    "test_min_stale_pixels_metadata_and_foreign_binding",
    "test_min_unsupported_and_future_admission",
    "test_min_ranges_lut_edits_and_shared_controller",
)
EXPECTED_COLLECTION_COUNT = 20
EXPECTED_COLLECTION_IDS = tuple(f"{name}[{layer}]" for name in TEST_NAMES for layer in LAYERS)
editor = shared.editor


@pytest.fixture(autouse=True)
def native_module(editor):
    global ij
    ij = shared.ij


def _owner(editor,case="N01",**kwargs):
    state,expected,navigation,c=core._input(case)
    owner=shared._owner(editor,state,**kwargs)
    view=owner.snapshot().display;view.c=c;owner.set_display(view)
    return owner,expected,navigation


def _request(): return shared._request("select_min")
def _apply(editor,layer,owner): return shared._call(editor,layer,"apply_channel_display",_request(),document_id=owner.id)
def _replay(editor,layer,owner,record,**kwargs):
    return shared._call(editor,layer,"replay_channel_display",record,document_id=owner.id,**kwargs)
def _record(owner,state,navigation):
    return shared._record(owner,state,"select_min",dependency={"revision":owner.snapshot().revisions.display},
                          navigation=None if navigation is None else {"c":0})


@pytest.mark.parametrize("layer",LAYERS)
def test_min_inactive_owner_and_exact_record(editor,layer):
    owner,expected,navigation=_owner(editor,dtype=np.float32)
    other,_,_=_owner(editor,"N02")
    editor.recording,editor.results=True,[{"retained":13}]
    data,untouched,view=shared._data(owner),shared._full(other),owner.snapshot().display
    record=_record(owner,expected,navigation)
    assert _apply(editor,layer,owner) is None
    assert shared._stored(owner)==expected and editor.workflow==[record] and owner.snapshot().display.c==0
    view.c=0
    assert shared._navigation(owner.snapshot().display)==shared._navigation(view)
    assert shared._data(owner)==data and shared._full(other)==untouched
    assert editor.active_index==1 and editor.results==[{"retained":13}]
    shared._same_owner(editor,owner)


@pytest.mark.parametrize("layer",LAYERS)
def test_min_gray_first_repeat_and_recording(editor,layer):
    owner,expected,_=_owner(editor,"N07")
    editor.recording=True
    data=shared._data(owner)
    first=_record(owner,expected,0)
    _apply(editor,layer,owner)
    assert shared._stored(owner)==expected and editor.workflow==[first]
    # Unlike Java's cache-dependent Max/Max, logical-state Min remains Min.
    before_revision=owner.snapshot().revisions.display
    repeated=_record(owner,expected,None)
    _apply(editor,layer,owner)
    assert shared._stored(owner)==expected and owner.snapshot().display.c==0
    assert owner.snapshot().revisions.display==before_revision+1
    # Adoption002 retains existing publication/recording for equal effects.
    assert editor.workflow==[first,repeated] and shared._data(owner)==data
    editor.recording=False
    before_revision=owner.snapshot().revisions.display
    _replay(editor,layer,owner,first)
    assert owner.snapshot().revisions.display==before_revision+1
    assert shared._stored(owner)==expected and editor.workflow==[first,repeated] and shared._data(owner)==data


@pytest.mark.parametrize("layer",LAYERS)
def test_min_strict_replay_preserves_fresh_navigation(editor,layer):
    owner,expected,_=_owner(editor)
    editor.recording=True;_apply(editor,layer,owner)
    record=copy.deepcopy(editor.workflow[0])
    view=owner.snapshot().display;view.t,view.c,view.crosshair,view.zoom=0,2,[4,2,3],3.75
    view.properties={**view.properties,"channel_display":core._input("N02")[0]};owner.set_display(view)
    owner.select(ij.Selection("point",[(3,2)]))
    data=shared._data(owner);editor.recording=False
    _replay(editor,layer,owner,record)
    assert shared._stored(owner)==expected and owner.snapshot().display.c==0
    view.c=0
    assert shared._navigation(owner.snapshot().display)==shared._navigation(view)
    _replay(editor,layer,owner,record)
    assert shared._stored(owner)==expected and editor.workflow==[record] and shared._data(owner)==data


@pytest.mark.parametrize("layer",LAYERS)
def test_min_complete_rebind_and_set_alternative(editor,layer):
    source,_,_=_owner(editor)
    destination,expected,navigation=_owner(editor,"N08")
    editor.recording=True;_apply(editor,layer,source)
    original=copy.deepcopy(editor.workflow[0]);before=shared._collection(editor)
    with pytest.raises(ValueError): _replay(editor,layer,destination,original)
    assert shared._collection(editor)==before
    data=shared._data(destination);record=_record(destination,expected,navigation)
    _replay(editor,layer,destination,original,rebind_request=_request())
    assert shared._stored(destination)==expected and editor.workflow[-1]==record and shared._data(destination)==data
    replacement=core._input("N07")[0]
    expected_record=shared._record(destination,replacement)
    _replay(editor,layer,destination,original,rebind_request=shared._request(state=replacement))
    assert shared._stored(destination)==replacement and editor.workflow[-1]==expected_record
    assert editor.workflow[0]==original and shared._data(destination)==data


@pytest.mark.parametrize("layer",LAYERS)
def test_min_native_save_open_logical_equivalence(editor,layer,tmp_path):
    owner,expected,_=_owner(editor,"N07",dtype=np.float32)
    editor.recording=True;_apply(editor,layer,owner)
    record=copy.deepcopy(editor.workflow[0]);data=shared._data(owner)
    path=tmp_path/"min-gray.ijx"
    editor.action("save_document",path=str(path),document_id=owner.id)
    assert path.read_bytes()[:10]==b"IMAGEJXX3\n" and shared._data(owner)==data
    shared._call(editor,layer,"open_file",str(path))
    reopened=editor._editor.native_document(next(info["id"] for info in editor.document_infos if info["id"]!=owner.id))
    assert reopened.id!=owner.id and shared._stored(reopened)==expected and editor.workflow==[record]
    old,new=owner.snapshot(),reopened.snapshot()
    assert new.dataset.numpy("TCZYXS").tobytes()==old.dataset.numpy("TCZYXS").tobytes()
    assert new.metadata==old.metadata and new.record_layout==old.record_layout and new.overlays==old.overlays
    assert shared._selection(new.selection)==shared._selection(old.selection)
    assert shared._selection(new.previous_selection)==shared._selection(old.previous_selection)
    assert not new.dirty and (reopened.history().undo_steps,reopened.history().redo_steps)==(0,0)
    before=shared._collection(editor)
    with pytest.raises(ValueError): _replay(editor,layer,reopened,record)
    assert shared._collection(editor)==before
    editor.recording=False;_apply(editor,layer,reopened)
    assert shared._stored(reopened)==expected and editor.workflow==[record]
    # Rebind recomputes the same logical-state no-transform effect.
    _replay(editor,layer,reopened,record,rebind_request=_request())
    assert shared._stored(reopened)==expected and editor.workflow==[record]


@pytest.mark.parametrize("layer",LAYERS)
def test_min_strict_request_parser_is_atomic(editor,layer):
    owner,_,_=_owner(editor)
    invalid=(True,{}, {"action":"select_min","state":None},
        {**_request(),"projection":"min"}, shared._request("SelectMin"),
        shared._request("select_min",core._input("N01")[0]),shared._request("select_min",channels=[0]),
        {**_request(),"channels":[True]})
    for request in invalid:
        before=shared._collection(editor)
        with pytest.raises((ValueError,TypeError)): shared._call(editor,layer,"apply_channel_display",request,document_id=owner.id)
        assert shared._collection(editor)==before


@pytest.mark.parametrize("layer",LAYERS)
def test_min_malformed_original_and_closed_owner(editor,layer):
    owner,_,_=_owner(editor,"N03")
    editor.recording=True;_apply(editor,layer,owner)
    record=copy.deepcopy(editor.workflow[0])
    assert record["state"]["projection"]=="max" and record["navigation"]=={"c":0}
    for change in ("coupled","bool","extra","v1"):
        bad=copy.deepcopy(record)
        if change=="coupled": bad["navigation"]=None
        elif change=="bool": bad["source"]["pixels_revision"]=True
        elif change=="extra": bad["future"]=None
        else: bad["version"]=1
        before=shared._collection(editor)
        with pytest.raises((ValueError,TypeError)): _replay(editor,layer,owner,bad,rebind_request=_request())
        assert shared._collection(editor)==before
    editor.close_document(owner.id,discard=True);before=shared._collection(editor)
    with pytest.raises(IndexError): _replay(editor,layer,owner,record)
    assert shared._collection(editor)==before


@pytest.mark.parametrize("layer",LAYERS)
def test_min_stale_pixels_metadata_and_foreign_binding(editor,layer):
    for change in ("pixels","metadata","foreign"):
        owner,_,_=_owner(editor);editor.recording=True;_apply(editor,layer,owner)
        record=copy.deepcopy(editor.workflow[-1]);destination=owner
        if change=="foreign": destination,_,_=_owner(editor)
        else:
            edit=owner.begin_edit()
            if change=="pixels": edit.replace_plane(np.full((3,5),7,np.uint16),t=1,c=1,z=2)
            else: edit.set_property("changed",True)
            owner.commit(edit)
        before=shared._collection(editor)
        with pytest.raises(ValueError if change=="foreign" else ij.RevisionConflict): _replay(editor,layer,destination,record)
        assert shared._collection(editor)==before


@pytest.mark.parametrize("layer",LAYERS)
def test_min_unsupported_and_future_admission(editor,layer):
    for dtype,samples in ((np.uint16,1),(np.float64,1),(np.uint8,3),(np.float32,4)):
        owner=shared._owner(editor,dtype=dtype,samples=samples)
        before=shared._collection(editor)
        with pytest.raises(ValueError): _apply(editor,layer,owner)
        assert shared._collection(editor)==before
    future={"schema":shared.SCHEMA,"version":37,"opaque":{"keep":[3,1,4]}}
    owner=shared._owner(editor,future,attach=False);before=shared._collection(editor);original=shared._full(owner)
    with pytest.raises(ValueError): shared._call(editor,layer,"add_native_document",owner)
    assert shared._collection(editor)==before and shared._full(owner)==original
    with pytest.raises(ValueError): ij.resolve_channel_display_command(owner.snapshot(),action="select_min")
    assert shared._full(owner)==original


@pytest.mark.parametrize("layer",LAYERS)
def test_min_ranges_lut_edits_and_shared_controller(editor,layer):
    owner,expected,_=_owner(editor);editor.recording=True;_apply(editor,layer,owner)
    data=shared._data(owner)
    editor.action("display_range",minimum=11.,maximum=91.,document_id=owner.id)
    expected["channels"][0]["range"]=[11.,91.]
    editor.action("lut",name="Blue",document_id=owner.id)
    expected["channels"][0]["lut"]={"name":"Blue"}
    assert shared._stored(owner)==expected and shared._data(owner)==data
    assert all(record["version"]==2 for record in editor.workflow)
    assert editor.workflow[0]["action"]=="select_min" and editor.workflow[0]["navigation"]=={"c":0}
    shared._same_owner(editor,owner)
