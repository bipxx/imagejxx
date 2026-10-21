"""Prepared installed desktop API checks; no physical UI acceptance credit.

Requests and RGB expectations are authored here. C++ owns state interpretation,
mapping, source binding, range resolution, recording and publication.
"""

import copy
from concurrent.futures import ThreadPoolExecutor
import json

import imagejxx as ij
import numpy as np
import pytest


ERRORS = (ValueError, TypeError, OverflowError, IndexError)
PALETTE = ("Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow", "White")
# Static declared membership, not an observed pytest collection result.
EXPECTED_COLLECTION_COUNT = 58


def _state(count=3, mode="composite"):
    return {"schema": "imagejxx.channel-display", "version": 1, "mode": mode,
            "channels": [{"visible": True, "range": [0., 255.],
                          "lut": {"name": PALETTE[min(c, 7)]}} for c in range(count)]}


def _request(action="set", state=None, channels=()):
    return {"action": action,
            "state": None if action == "disable" else _state() if state is None else state,
            "channels": list(channels)}


def _table():
    return [[i, (3*i) % 256, 255-i] for i in range(256)]


def _owner(dtype=np.float32, count=3):
    t, c, z, y, x, s = np.indices((2, count, 4, 5, 7, 1), dtype=np.int64)
    pixels = (70*t + 50*c + 8*z + 2*y + x + s).astype(dtype)
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"), "Desktop channels")
    edit = owner.begin_edit()
    edit.set_extension("vendor", {"version": 37, "unsigned": 2**64-1})
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("history_fixture", True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, min(1, count-1), [4, 3, 2]
    view.properties = {"display_range": [5., 10.], "lut": "Ice"}
    owner.set_display(view)
    return pixels, owner


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences
    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def _data(owner):
    snapshot, history = owner.snapshot(), owner.history()
    return {"identity": snapshot.data_identity, "dirty": snapshot.dirty,
            "revisions": (snapshot.revisions.pixels, snapshot.revisions.metadata,
                          snapshot.revisions.selection),
            "pixels": snapshot.dataset.numpy("TCZYXS").tobytes(),
            "metadata": snapshot.metadata, "selection": snapshot.selection,
            "previous_selection": snapshot.previous_selection,
            "rois": [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snapshot.rois],
            "overlays": snapshot.overlays, "record_layout": snapshot.record_layout,
            "history": (history.undo_steps, history.redo_steps, history.step_limit, history.pixel_budget)}


def _navigation(view):
    return (view.t, view.c, view.crosshair, view.orthogonal, view.linked_zoom,
            view.zoom, view.plane_zoom, view.pan)


def _collection(editor):
    documents = []
    for info in editor.document_infos:
        owner = editor.live_document(info["id"]).native_owner
        snapshot = owner.snapshot()
        documents.append((owner.id, _data(owner), _navigation(snapshot.display),
                          snapshot.revisions.display, snapshot.display.revision,
                          snapshot.display.properties))
    return (editor.active_index, editor.recording, editor.results, editor.workflow,
            editor.preferences, documents)


def _stored(owner):
    return owner.snapshot().display.properties.get("channel_display")


def _call(editor, layer, name, *args, **kwargs):
    return getattr(editor if layer == "wrapper" else editor._editor, name)(*args, **kwargs)


@pytest.mark.parametrize("layer", ("wrapper", "binding"))
def test_full_set_reset_disable_requests_preserve_pixels_history_and_legacy(editor, layer):
    _, owner = _owner()
    editor.add_native_document(owner)
    editor.recording = True
    editor.results = [{"keep": 7}]
    before, snapshot = _data(owner), owner.snapshot()
    legacy = copy.deepcopy(snapshot.display.properties)
    state = _state()
    state["channels"][0]["range"] = [.1, .9]
    request = _request(state=state)
    original = copy.deepcopy(request)
    assert _call(editor, layer, "apply_channel_display", request, document_id=owner.id) is None
    assert request == original and _stored(owner) == state
    assert owner.snapshot().shares_data(snapshot) and _data(owner) == before
    assert editor.workflow[0]["schema"] == "imagejxx.channel-display-command"
    assert editor.workflow[0]["action"] == "set" and editor.workflow[0]["state"] == state
    assert editor.workflow[0]["position"] == {"t": 1, "c": 1, "z": 2}
    reset = _request("reset_range", state, [1])
    _call(editor, layer, "apply_channel_display", reset, document_id=owner.id)
    expected = copy.deepcopy(state)
    expected["channels"][1]["range"] = [136., 150.]
    assert _stored(owner) == expected and reset["state"] == state
    assert editor.workflow[-1]["reset"] == {"channels": [1], "empty_finite": [False]}
    assert editor.workflow[-1]["action"] == "reset_range"
    _call(editor, layer, "apply_channel_display", _request("disable"), document_id=owner.id)
    assert owner.snapshot().display.properties == legacy
    assert len(editor.workflow) == 3 and editor.workflow[-1]["action"] == "disable"
    assert editor.results == [{"keep": 7}] and _data(owner) == before


@pytest.mark.parametrize("dtype", (np.uint8, np.uint16, np.float32))
def test_enable_addresses_explicit_inactive_owner_and_inherits_first_c_range(editor, dtype):
    _, inactive = _owner(dtype)
    _, active = _owner(dtype)
    for owner in (inactive, active):
        view = owner.snapshot().display
        properties = view.properties
        properties.pop("display_range", None)
        properties.pop("lut", None)
        view.properties = properties
        owner.set_display(view)
    editor.add_native_document(inactive)
    editor.add_native_document(active)
    before, other = _data(inactive), _collection(editor)
    editor.enable_channel_display(document_id=inactive.id)
    expected = _state()
    if dtype != np.uint8:
        for channel in expected["channels"]:
            channel["range"] = [86., 100.]
    assert _stored(inactive) == expected and _data(inactive) == before
    assert editor.active_index == other[0] and _stored(active) is None
    assert editor.workflow == [] and editor.results == []


@pytest.mark.parametrize("legacy_lut", ("named", "custom"))
def test_first_enable_inherits_explicit_legacy_range_and_lut_then_disable_restores(editor, legacy_lut):
    _, owner = _owner()
    view = owner.snapshot().display
    legacy = {"display_range": [.2, 10.8], "lut": "Ice"}
    if legacy_lut == "custom":
        legacy["custom_lut"] = _table()
    view.properties = copy.deepcopy(legacy)
    owner.set_display(view)
    editor.add_native_document(owner)
    editor.recording = True
    before = _data(owner)
    editor.enable_channel_display(document_id=owner.id)
    state = _stored(owner)
    assert state["mode"] == "composite" and len(state["channels"]) == 3
    inherited = state["channels"][0]["lut"]
    assert set(inherited) == {"table"} and len(inherited["table"]) == 256
    if legacy_lut == "custom":
        assert inherited == {"table": _table()}
    else:
        # Literal samples of the existing owned Ice display palette. This is
        # migration preservation, without a new upstream palette claim.
        assert [inherited["table"][i] for i in (0,64,128,192,255)] == [
            [0,0,0], [0,40,120], [1,161,220], [161,240,255], [255,255,255]]
    for channel in state["channels"]:
        assert channel == {"visible": True, "range": [.2,10.8], "lut": inherited}
    assert editor.workflow[-1]["state"] == state and _data(owner) == before
    for key, value in legacy.items():
        assert owner.snapshot().display.properties[key] == value
    editor.apply_channel_display(_request("disable"), document_id=owner.id)
    assert owner.snapshot().display.properties == legacy and _data(owner) == before
    assert len(editor.workflow) == 2 and editor.workflow[-1]["action"] == "disable"


@pytest.mark.parametrize("mode", ("color", "grayscale", "composite"))
def test_custom_lut_transport_is_owned_and_rendered_bytes_are_literal(editor, mode):
    pixels = np.array([[0, 1, 127, 255]], np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="YX"))
    editor.add_native_document(owner)
    state = _state(1, mode)
    state["channels"][0]["lut"] = {"table": _table()}
    request, expected_state = _request(state=state), copy.deepcopy(state)
    editor.recording = True
    editor.apply_channel_display(request, document_id=owner.id)
    request["state"]["channels"][0]["lut"]["table"][0][:] = [99, 98, 97]
    assert _stored(owner) == expected_state and editor.workflow[0]["state"] == expected_state
    editor.action("show_channel_display")
    rendered = editor.render_headless(frames=3)["ordinary_pixels"]["readback"]["rendered_rgb"]
    expected = ([[0,0,255], [1,3,254], [127,125,128], [255,253,0]] if mode == "color"
                else [[0,0,0], [1,1,1], [127,127,127], [255,255,255]])
    assert rendered["shape"] == [1,4,3] and rendered["dtype"] == "uint8"
    assert rendered["pixels"] == [component for triplet in expected for component in triplet]
    assert owner.snapshot().dataset.numpy("YX").tobytes() == pixels.tobytes()


def test_quiet_nan_maps_custom_nonblack_zero_without_changing_raw_bits(editor):
    bits = np.array([0x7fc12345, 0x80000000, 0x3f800000, 0xffc23456], np.uint32)
    source = ij.Dataset(bits.view(np.float32).reshape(1,4), axes="YX",
                        sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    owner = ij.Document(source)
    editor.add_native_document(owner)
    state = _state(1, "color")
    state["channels"][0]["lut"] = {"table": _table()}
    editor.apply_channel_display(_request(state=state), document_id=owner.id)
    editor.action("show_channel_display")
    rendered = editor.render_headless(frames=3)["ordinary_pixels"]["readback"]["rendered_rgb"]
    assert rendered["pixels"] == [0,0,255, 0,0,255, 1,3,254, 0,0,255]
    assert source.numpy("YX").view(np.uint32).ravel().tolist() == bits.tolist()


def _bad_requests():
    good = _request()
    def field(name, value):
        result = copy.deepcopy(good)
        result[name] = value
        return result
    def state_field(name, value):
        state = _state()
        state[name] = value
        return _request(state=state)
    def channel_field(name, value):
        state = _state()
        state["channels"][1][name] = value
        return _request(state=state)
    bool_table, high_table = _table(), _table()
    bool_table[42][1], high_table[42][1] = True, 256
    return (
        ("01_none", None),
        ("02_boolean", True),
        ("03_array", []),
        ("04_json_string", json.dumps(good)),
        ("05_empty_object", {}),
        ("06_missing_action", {k:v for k,v in good.items() if k != "action"}),
        ("07_missing_state", {k:v for k,v in good.items() if k != "state"}),
        ("08_missing_channels", {k:v for k,v in good.items() if k != "channels"}),
        ("09_unknown_field", {**good, "extra": True}),
        ("10_boolean_action", field("action", True)),
        ("11_unknown_action", field("action", "max")),
        ("12_set_null_state", field("state", None)),
        ("13_disable_state", {"action":"disable", "state":_state(), "channels":[]}),
        ("14_disable_channels", {"action":"disable", "state":None, "channels":[0]}),
        ("15_set_channels", field("channels", [0])),
        ("16_reset_null_state", {"action":"reset_range", "state":None, "channels":[]}),
        ("17_channels_boolean", {"action":"reset_range", "state":_state(), "channels":True}),
        ("18_channel_boolean", {"action":"reset_range", "state":_state(), "channels":[True]}),
        ("19_channel_integer_float", {"action":"reset_range", "state":_state(), "channels":[1.0]}),
        ("20_channel_fraction", {"action":"reset_range", "state":_state(), "channels":[1.5]}),
        ("21_channel_overflow", {"action":"reset_range", "state":_state(), "channels":[2**31]}),
        ("22_channel_negative", {"action":"reset_range", "state":_state(), "channels":[-1]}),
        ("23_channel_duplicate", {"action":"reset_range", "state":_state(), "channels":[1,1]}),
        ("24_channel_outside", {"action":"reset_range", "state":_state(), "channels":[3]}),
        ("25_future_version", state_field("version", 37)),
        ("26_boolean_version", state_field("version", True)),
        ("27_unknown_mode", state_field("mode", "max")),
        ("28_wrong_count", _request(state=_state(2))),
        ("29_boolean_endpoint", channel_field("range", [True,255])),
        ("30_lossy_endpoint", channel_field("range", [2**53+1,2**53+2])),
        ("31_boolean_lut_byte", channel_field("lut", {"table":bool_table})),
        ("32_overflow_lut_byte", channel_field("lut", {"table":high_table})),
        ("33_unknown_state_field", state_field("extra", True)),
        ("34_mixed_lut_forms", channel_field("lut", {"name":"Red", "table":_table()})),
    )


BAD_REQUESTS = _bad_requests()


@pytest.mark.parametrize("case_id,channel_request", BAD_REQUESTS, ids=[row[0] for row in BAD_REQUESTS])
def test_malformed_full_requests_refuse_atomically(editor, case_id, channel_request):
    _, owner = _owner()
    editor.add_native_document(owner)
    editor.recording = True
    editor.results = [{"retained": 3}]
    before = _collection(editor)
    with pytest.raises(ERRORS):
        editor.apply_channel_display(copy.deepcopy(channel_request), document_id=owner.id)
    assert _collection(editor) == before, case_id


@pytest.mark.parametrize("operation", ("enable", "set", "replay"))
def test_missing_and_closed_ids_never_target_the_active_owner(editor, operation):
    _, source = _owner()
    _, active = _owner()
    editor.add_native_document(source)
    editor.add_native_document(active)
    editor.recording = True
    editor.apply_channel_display(_request(), document_id=source.id)
    record = copy.deepcopy(editor.workflow[-1])
    editor.close_document(source.id, discard=True)
    before = _collection(editor)
    for document_id in (source.id, 2**64-1):
        with pytest.raises(ERRORS):
            if operation == "enable":
                editor.enable_channel_display(document_id=document_id)
            elif operation == "set":
                editor.apply_channel_display(_request(), document_id=document_id)
            else:
                editor.replay_channel_display(record, document_id=document_id)
        assert _collection(editor) == before


def test_strict_and_explicit_rebind_distinguish_recorded_and_new_live_ranges(editor):
    _, source = _owner()
    _, destination = _owner()
    editor.add_native_document(source)
    editor.add_native_document(destination)
    editor.recording = True
    editor.apply_channel_display(_request("reset_range", _state(), [1]), document_id=source.id)
    record = copy.deepcopy(editor.workflow[-1])
    assert record["state"]["channels"][1]["range"] == [136.,150.]
    view = source.snapshot().display
    view.t, view.c, view.crosshair = 0, 0, [6,4,0]
    source.set_display(view)
    source.select(ij.Selection("point", [(3,2)]))
    source_data, navigation = _data(source), _navigation(source.snapshot().display)
    editor.replay_channel_display(record, document_id=source.id)
    assert _stored(source) == record["state"] and _data(source) == source_data
    assert _navigation(source.snapshot().display) == navigation
    before = _collection(editor)
    with pytest.raises(ERRORS):
        editor.replay_channel_display(record, document_id=destination.id)
    assert _collection(editor) == before
    view = destination.snapshot().display
    view.t, view.c, view.crosshair = 0, 2, [4,3,3]
    destination.set_display(view)
    request = _request("reset_range", _state(), [2])
    original, count, data = copy.deepcopy(request), len(editor.workflow), _data(destination)
    editor.replay_channel_display(record, rebind_request=request, document_id=destination.id)
    expected = _state()
    expected["channels"][2]["range"] = [124.,138.]
    assert request == original and _stored(destination) == expected and _data(destination) == data
    assert len(editor.workflow) == count+1
    assert editor.workflow[-1]["position"] == {"t":0,"c":2,"z":3}
    assert editor.workflow[-1]["reset"] == {"channels":[2],"empty_finite":[False]}
    assert record["state"]["channels"][1]["range"] == [136.,150.]


@pytest.mark.parametrize("rebind", (True, {}, {"action":"set", "state":None, "channels":[float("nan")]}))
def test_original_record_validation_precedes_rebind_transport(editor, rebind):
    _, owner = _owner()
    editor.add_native_document(owner)
    before = _collection(editor)
    with pytest.raises(ValueError, match="Missing channel display command field: schema"):
        editor.replay_channel_display({}, rebind_request=rebind, document_id=owner.id)
    assert _collection(editor) == before


@pytest.mark.parametrize("change", ("pixels", "metadata"))
def test_stale_strict_replay_refuses_atomically(editor, change):
    _, owner = _owner()
    editor.add_native_document(owner)
    editor.recording = True
    editor.apply_channel_display(_request(), document_id=owner.id)
    record = copy.deepcopy(editor.workflow[-1])
    edit = owner.begin_edit()
    if change == "pixels":
        edit.replace_plane(np.full((5,7), 7, np.float32), t=1, c=1, z=2)
    else:
        edit.set_property("new metadata", True)
    owner.commit(edit)
    before = _collection(editor)
    with pytest.raises(ERRORS + (RuntimeError,)):
        editor.replay_channel_display(record, document_id=owner.id)
    assert _collection(editor) == before


@pytest.mark.parametrize("operation", ("enable", "set", "replay"))
def test_new_native_methods_retain_creator_thread_rule(editor, operation):
    _, owner = _owner()
    editor.add_native_document(owner)
    editor.recording = True
    editor.apply_channel_display(_request(), document_id=owner.id)
    record, before = copy.deepcopy(editor.workflow[-1]), _collection(editor)
    def call():
        if operation == "enable":
            editor.enable_channel_display(document_id=owner.id)
        elif operation == "set":
            editor.apply_channel_display(_request(), document_id=owner.id)
        else:
            editor.replay_channel_display(record, document_id=owner.id)
    with ThreadPoolExecutor(max_workers=1) as executor:
        with pytest.raises(RuntimeError, match="thread"):
            executor.submit(call).result(timeout=5)
    assert _collection(editor) == before


def test_existing_range_lut_auto_and_lut_file_actions_route_to_active_c(editor, tmp_path):
    _, owner = _owner()
    editor.add_native_document(owner)
    legacy, before = copy.deepcopy(owner.snapshot().display.properties), _data(owner)
    editor.apply_channel_display(_request(), document_id=owner.id)
    state = _state()
    editor.action("display_range", minimum=11., maximum=19., document_id=owner.id)
    state["channels"][1]["range"] = [11.,19.]
    assert _stored(owner) == state
    editor.action("lut", name="Blue", document_id=owner.id)
    state["channels"][1]["lut"] = {"name":"Blue"}
    assert _stored(owner) == state
    editor.action("display_range", auto=True, document_id=owner.id)
    state["channels"][1]["range"] = [136.,150.]
    assert _stored(owner) == state
    path, exported = tmp_path / "custom-lut.json", tmp_path / "exported-lut.json"
    path.write_text(json.dumps({"format":"imagejxx-lut", "lut":_table()}), encoding="utf-8")
    editor.action("load_lut", path=str(path), document_id=owner.id)
    state["channels"][1]["lut"] = {"table":_table()}
    assert _stored(owner) == state
    editor.action("save_lut", path=str(exported), document_id=owner.id)
    assert json.loads(exported.read_text(encoding="utf-8"))["lut"] == _table()
    for key, value in legacy.items():
        assert owner.snapshot().display.properties[key] == value
    assert _data(owner) == before
    editor.apply_channel_display(_request("disable"), document_id=owner.id)
    assert owner.snapshot().display.properties == legacy and _data(owner) == before
