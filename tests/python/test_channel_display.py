"""Installed native channel-display API checks; no physical UI acceptance.

Expected pixels, normalized records and V3 framing are authored independently.
The finite processor rows include literal unequal fractional U8 widths and
nonbinary F32 endpoint discriminators from the separate targeted entry capture.
"""

import copy
import ctypes
import gc
import json
import struct
import threading
import zlib

import imagejxx as ij
import numpy as np
import pytest


PALETTE = ("Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow", "White")
NAMED = ("Grays", "Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow")
DTYPES = (np.uint8, np.uint16, np.float32)
PLANES = ("xy", "xz", "yz")
MODES = ("color", "grayscale", "composite")
ERRORS = (ValueError, TypeError, OverflowError, IndexError)
# Frozen source membership; root checks actual installed pytest collection
# against this number before accepting any run. No GUI cases are included.
EXPECTED_COLLECTION_COUNT = 227
VALUES = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 126, 127, 128, 129, 254, 255)
PROCESSOR_ROWS = (
    ("u8.full", np.uint8, [0, 255], VALUES),
    ("u8.ten", np.uint8, [0, 10], (0,25,51,76,102,128,153,179,204,230,255,255,255,255,255,255,255,255)),
    ("u8.half", np.uint8, [.5, 10.5], (0,0,25,51,76,102,128,153,179,204,230,255,255,255,255,255,255,255)),
    ("u8.equal", np.uint8, [7, 7], (0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,255)),
    ("u8.zero", np.uint8, [0, 0], (0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255)),
    ("u16.full", np.uint16, [0, 255], VALUES),
    ("u16.ten", np.uint16, [0, 10], (0,23,47,70,93,116,140,163,186,209,233,255,255,255,255,255,255,255)),
    ("u16.half", np.uint16, [.5, 10.5], (0,0,23,47,70,93,116,140,163,186,209,233,255,255,255,255,255,255)),
    ("u16.equal", np.uint16, [7, 7], (0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,255)),
    ("u16.zero", np.uint16, [0, 0], (0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255)),
    ("f32.full", np.float32, [0, 255], VALUES),
    ("f32.ten", np.float32, [0, 10], (0,26,51,77,102,128,153,179,204,230,255,255,255,255,255,255,255,255)),
    ("f32.half", np.float32, [.5, 10.5], (0,13,38,64,89,115,140,166,191,217,242,255,255,255,255,255,255,255)),
    ("f32.equal", np.float32, [7, 7], (0,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,255)),
    ("f32.zero", np.float32, [0, 0], (0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255)),
)


def _state(count=3, mode="composite"):
    return {"schema": "imagejxx.channel-display", "version": 1, "mode": mode,
            "channels": [{"visible": True, "range": [0., 255.],
                          "lut": {"name": PALETTE[min(c, 7)]}} for c in range(count)]}


def _dataset(dtype=np.uint16, count=3, samples=1):
    t, c, z, y, x, s = np.indices((2, count, 3, 2, 4, samples), dtype=np.int64)
    pixels = (16*t + 8*c + 4*z + 2*y + x + s).astype(dtype)
    calibration = ij.SpatialCalibration(spacing=(.5, 1.25, 2.5), origin=(11, -7, 19),
        direction=(-1, .25, .5, 0, 1, .5, .5, 0, 1), unit="um",
        frame_interval=.75, time_origin=2.25, time_unit="s")
    return pixels, ij.Dataset(pixels, axes="TCZYXS", calibration=calibration)


def _owner(dtype=np.uint16, count=3):
    pixels, source = _dataset(dtype, count)
    owner = ij.Document(source, "Channel display literal")
    edit = owner.begin_edit()
    edit.set_property("retained", {"flags": [None, True, 2**64-1]})
    edit.select(ij.Selection("rectangle", [(0, 0), (2, 1)]))
    edit.set_previous_selection(ij.Selection("point", [(1, 1)]))
    edit.set_rois([ij.ROI("retained", ij.Selection("point", [(2, 1)]), t=1, c=0, z=1)])
    edit.set_overlays([{"name": "retained overlay", "points": [[1, 0]]}])
    owner.commit(edit)
    owner.mark_saved()
    edit = owner.begin_edit()
    edit.set_property("redo_probe", True)
    owner.commit(edit)
    owner.undo()
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, min(2, count-1), [2, 1, 2]
    view.orthogonal, view.linked_zoom, view.zoom = True, False, 1.25
    view.plane_zoom, view.pan = [1.25, 2., 3.], [[3., -2.], [1., 4.], [-5., 2.]]
    view.properties = {"display_range": [7., 19.], "lut": "Fire",
                       "custom_lut": [[1, 2, 3]], "unrelated": {"exact": 2**64-1}}
    owner.set_display(view)
    return pixels, owner


def _data(owner):
    snap, history = owner.snapshot(), owner.history()
    return {"identity": snap.data_identity, "dirty": snap.dirty,
            "revisions": (snap.revisions.pixels, snap.revisions.metadata),
            "selection_revision": snap.revisions.selection,
            "metadata": snap.metadata, "selection": snap.selection,
            "previous": snap.previous_selection,
            "rois": [(r.name, r.selection, r.t, r.c, r.z, r.attributes) for r in snap.rois],
            "overlays": snap.overlays, "layout": snap.record_layout,
            "spectrum": None if snap.spectrum is None else snap.spectrum.numpy().tobytes(),
            "pixels": snap.dataset.numpy("TCZYXS").tobytes(),
            "shape": tuple(snap.dataset.shape), "dtype": snap.dataset.dtype,
            "policy": snap.dataset.sample_policy,
            "calibration": {k: getattr(snap.dataset.calibration, k) for k in (
                "spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit")},
            "history": (history.undo_steps, history.redo_steps, history.step_limit, history.pixel_budget)}


def _navigation(view):
    return (view.t, view.c, view.crosshair, view.orthogonal, view.linked_zoom,
            view.zoom, view.plane_zoom, view.pan)


def _channel_property(owner):
    return owner.snapshot().display.properties.get("channel_display")


def _typed(value):
    if type(value) is float: return (float, struct.pack("<d", value))
    if isinstance(value, dict): return (dict, tuple((k, _typed(v)) for k, v in sorted(value.items())))
    if isinstance(value, (list, tuple)): return (list, tuple(_typed(v) for v in value))
    return (type(value), value)


@pytest.mark.parametrize("name", NAMED)
def test_named_lut_bytes_and_detachment(name):
    masks = {"Grays": (1,1,1), "Red": (1,0,0), "Green": (0,1,0), "Blue": (0,0,1),
             "White": (1,1,1), "Cyan": (0,1,1), "Magenta": (1,0,1), "Yellow": (1,1,0)}
    expected = np.array([[i*r, i*g, i*b] for i in range(256)
                         for r, g, b in [masks[name]]], dtype=np.uint8)
    actual = ij.channel_display_lut(name)
    assert actual.shape == (256, 3) and actual.dtype == np.uint8
    np.testing.assert_array_equal(actual, expected)
    actual[:] = 123
    np.testing.assert_array_equal(ij.channel_display_lut(name), expected)


@pytest.mark.parametrize("case_id,dtype,limits,expected", PROCESSOR_ROWS,
                         ids=[row[0] for row in PROCESSOR_ROWS])
def test_literal_processor_rows(case_id, dtype, limits, expected):
    pixels = np.array([VALUES], dtype=dtype)
    source = ij.Dataset(pixels, axes="YX")
    state = _state(1, "grayscale")
    state["channels"][0]["range"] = limits
    original = copy.deepcopy(state)
    actual = ij.compose_channel_display(source, state)
    expected_rgb = np.repeat(np.array([[expected]], np.uint8).reshape(1, -1, 1), 3, axis=2)
    assert actual.dtype == np.uint8 and actual.shape == (1, len(VALUES), 3), case_id
    np.testing.assert_array_equal(actual, expected_rgb)
    assert state == original and source.numpy("YX").tobytes() == pixels.tobytes()


def test_float32_literal_half_byte_boundaries():
    bits = np.array([0x3b008080, 0x3b008081, 0x3b008082,
                     0x3effffff, 0x3f000000, 0x3f000001], np.uint32)
    source = ij.Dataset(bits.view(np.float32).reshape(1, -1), axes="YX")
    state = _state(1, "grayscale")
    state["channels"][0]["range"] = [0, 1]
    expected = np.repeat(np.array([[[0], [1], [1], [127], [128], [128]]], np.uint8), 3, 2)
    np.testing.assert_array_equal(ij.compose_channel_display(source, state), expected)
    assert source.numpy("YX").view(np.uint32).ravel().tolist() == bits.tolist()


@pytest.mark.parametrize("low,high,expected", (
    (.2, 10.8, (0,24,48,72,96,120,144,169,193,217,241,255,255)),
    (.6, 10.4, (0,0,26,52,78,104,130,156,182,208,235,255,255)),
))
def test_targeted_u8_requested_width_with_rounded_low(low, high, expected):
    pixels = np.arange(13, dtype=np.uint8).reshape(1,13)
    source, state = ij.Dataset(pixels, axes="YX"), _state(1, "grayscale")
    state["channels"][0]["range"] = [low, high]
    np.testing.assert_array_equal(ij.compose_channel_display(source, state),
        np.repeat(np.array(expected, np.uint8).reshape(1,13,1), 3, axis=2))
    assert ij.normalize_channel_display(state, source)["channels"][0]["range"] == [low, high]
    assert source.numpy("YX").tobytes() == pixels.tobytes()


def test_targeted_f32_binary32_endpoint_threshold_neighbors():
    bits = np.array([0x3ddcdcdd,0x3dfcfcfd,0x3e1b6835,0x3e553ba2,0x3e654bb2,0x3e755bc2,
                     0x3e82b5e9,0x3e9c6936,0x3ea4713e,0x3eac7946,0x3eb4814e,0x3ebc8956], np.uint32)
    source, state = ij.Dataset(bits.view(np.float32).reshape(1,12), axes="YX"), _state(1,"grayscale")
    state["channels"][0]["range"] = [.1,.9]
    expected = np.repeat(np.array([2,7,17,35,40,45,50,66,71,76,81,86], np.uint8).reshape(1,12,1), 3, 2)
    np.testing.assert_array_equal(ij.compose_channel_display(source, state), expected)
    assert ij.normalize_channel_display(state, source)["channels"][0]["range"] == [.1,.9]
    assert source.numpy("YX").view(np.uint32).ravel().tolist() == bits.tolist()


@pytest.mark.parametrize("dtype", DTYPES)
@pytest.mark.parametrize("plane", PLANES)
@pytest.mark.parametrize("mode", MODES)
def test_scientific_xyz_modes_literal_source(dtype, plane, mode):
    pixels, owner = _owner(dtype)
    before = _data(owner)
    state = _state(mode=mode)
    state["channels"][1]["visible"] = False
    selected = pixels[1, :, :, :, :, 0]
    values = selected[:, 1] if plane == "xy" else selected[:, :, 1, :] if plane == "xz" else selected[:, :, :, 1]
    if mode == "composite":
        expected = np.stack((values[0], np.zeros_like(values[0]), values[2]), axis=-1)
    elif mode == "color":
        expected = np.stack((np.zeros_like(values[1]), values[1], np.zeros_like(values[1])), axis=-1)
    else:
        expected = np.repeat(values[1][..., None], 3, axis=-1)
    actual = ij.compose_channel_display(owner.snapshot().dataset, state, plane, 1, 1, 1)
    np.testing.assert_array_equal(actual, expected.astype(np.uint8))
    actual[:] = 251
    assert _data(owner) == before


@pytest.mark.parametrize("dtype", DTYPES)
@pytest.mark.parametrize("count", (1, 9))
def test_defaults_inherit_c0_live_xy_and_palette(dtype, count):
    pixels, source = _dataset(dtype, count)
    actual = ij.default_channel_display(source, 1, 2)
    expected = _state(count, "color" if count > 8 else "composite")
    for channel in expected["channels"]:
        channel["range"] = [0, 255] if dtype == np.uint8 else [24, 29]
    assert actual == expected
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()
    if count == 1:
        image = ij.compose_channel_display(source, actual, "xy", 2, 1, 0)
        assert np.array_equal(image[..., 0], image[..., 1]) and np.array_equal(image[..., 1], image[..., 2])


@pytest.mark.parametrize("dtype", DTYPES)
def test_reset_ordered_per_channel_live_xy(dtype):
    pixels, source = _dataset(dtype)
    requested = _state()
    original = copy.deepcopy(requested)
    actual, observations = ij.reset_channel_display_ranges(source, requested, 1, 2, [2, 0])
    expected = copy.deepcopy(original)
    expected["channels"][2]["range"] = [0, 255] if dtype == np.uint8 else [40, 45]
    expected["channels"][0]["range"] = [0, 255] if dtype == np.uint8 else [24, 29]
    assert actual == expected
    assert observations == [{"range": expected["channels"][c]["range"], "empty_finite": False} for c in (2, 0)]
    assert requested == original and source.numpy("TCZYXS").tobytes() == pixels.tobytes()


@pytest.mark.parametrize("dtype", DTYPES)
def test_effective_auto_uses_requested_live_channel(dtype):
    pixels, source = _dataset(dtype)
    calls = []
    actual = ij.effective_channel_display_range(source, t=1, c=2, z=2, checkpoint=lambda: calls.append(True))
    assert actual == {"range": [0,255] if dtype == np.uint8 else [40,45], "empty_finite":False}
    assert calls and source.numpy("TCZYXS").tobytes() == pixels.tobytes()


@pytest.mark.parametrize("channels", ((-1,), (3,), (0,0), (0,1,2,0), (True,)),
                         ids=("negative","outside","duplicate","excess","bool"))
def test_reset_subset_refusal_precedes_callbacks(channels):
    _, source = _dataset()
    calls = []
    with pytest.raises(ERRORS):
        ij.reset_channel_display_ranges(source, _state(), channels=channels, checkpoint=lambda: calls.append(True))
    assert not calls


@pytest.mark.parametrize("mode", MODES)
def test_quiet_nan_custom_zero_and_signed_zero(mode):
    bits = np.array([0x7fc12345, 0x80000000, 0x3f800000, 0xffc23456], np.uint32)
    pixels = np.tile(bits, 2).view(np.float32).reshape(1, 2, 1, 1, 4, 1)
    source = ij.Dataset(pixels, axes="TCZYXS", sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    table = [[0, 0, 0] for _ in range(256)]
    table[0], table[255] = [7, 11, 13], [17, 19, 23]
    state = _state(2, mode)
    for channel in state["channels"]:
        channel.update(range=[0, 1], lut={"table": table})
    expected = ([[0,0,0], [0,0,0], [255,255,255], [0,0,0]] if mode == "grayscale"
                else [[7,11,13], [7,11,13], [17,19,23], [7,11,13]])
    if mode == "composite":
        expected = [[2*v for v in rgb] for rgb in expected]
    np.testing.assert_array_equal(ij.compose_channel_display(source, state), np.array([expected], np.uint8))
    assert source.numpy("TCZYXS").view(np.uint32).ravel().tolist() == np.tile(bits, 2).tolist()


def test_all_nan_reset_persists_owned_empty_observation():
    bits = np.array([0x7fc12345, 0xffc23456, 0x7fc54321, 0xffc11111], np.uint32)
    source = ij.Dataset(bits.view(np.float32).reshape(1, 4), axes="YX",
                        sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    owner = ij.Document(source)
    state, observations = ij.reset_channel_display_ranges(source, _state(1), 0, 0)
    assert state["channels"][0]["range"] == [0, 1]
    assert observations == [{"range": [0, 1], "empty_finite": True}]
    command = ij.resolve_channel_display_command(owner.snapshot(), _state(1), action="reset_range")
    assert command.state == state
    assert command.record()["reset"] == {"channels": [0], "empty_finite": [True]}
    assert source.numpy("YX").view(np.uint32).ravel().tolist() == bits.tolist()


STATE_BAD_IDS = ("schema", "version_bool", "version_float", "version_future", "extra", "missing",
    "mode", "count", "visible_int", "visible_float", "channel_extra", "channel_missing",
    "range_bool", "range_lossy", "range_uint_lossy", "range_reversed", "range_nan", "range_inf",
    "lut_unknown", "lut_mixed", "table_length", "row_length", "byte_float", "byte_bool", "byte_overflow")


def _bad_state(case):
    state = _state()
    channel = state["channels"][0]
    if case == "schema": state["schema"] = "other"
    elif case == "version_bool": state["version"] = True
    elif case == "version_float": state["version"] = 1.
    elif case == "version_future": state["version"] = 3
    elif case == "extra": state["extra"] = None
    elif case == "missing": del state["mode"]
    elif case == "mode": state["mode"] = "Composite"
    elif case == "count": state["channels"].pop()
    elif case == "visible_int": channel["visible"] = 1
    elif case == "visible_float": channel["visible"] = 1.
    elif case == "channel_extra": channel["extra"] = False
    elif case == "channel_missing": del channel["visible"]
    elif case == "range_bool": channel["range"] = [False, 255]
    elif case == "range_lossy": channel["range"] = [0, 2**53+1]
    elif case == "range_uint_lossy": channel["range"] = [0, 2**64-1]
    elif case == "range_reversed": channel["range"] = [2, 1]
    elif case == "range_nan": channel["range"] = [0, float("nan")]
    elif case == "range_inf": channel["range"] = [0, float("inf")]
    elif case == "lut_unknown": channel["lut"] = {"name": "Fire"}
    else:
        table = [[0, 0, 0] for _ in range(256)]
        channel["lut"] = {"table": table}
        if case == "lut_mixed": channel["lut"]["name"] = "Red"
        elif case == "table_length": table.pop()
        elif case == "row_length": table[0] = [0, 0]
        elif case == "byte_float": table[0][0] = 1.
        elif case == "byte_bool": table[0][0] = True
        elif case == "byte_overflow": table[0][0] = 256
        else: raise AssertionError(case)
    return state


@pytest.mark.parametrize("case", STATE_BAD_IDS)
def test_strict_state_refuses_without_owner_mutation(case):
    _, owner = _owner()
    before = _data(owner)
    state = _bad_state(case)
    for operation in (lambda: ij.validate_channel_display(state, owner.snapshot().dataset),
                      lambda: ij.normalize_channel_display(state, owner.snapshot().dataset),
                      lambda: ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset, state),
                      lambda: ij.compose_channel_display(owner.snapshot().dataset, state)):
        with pytest.raises(ERRORS): operation()
    if case not in {"schema", "version_bool", "version_float", "version_future"}:
        def raw_install(commit):
            view = owner.snapshot().display
            view.properties = {"channel_display": state}
            return owner.set_display(view) if commit else owner.snapshot().with_display(view)
        for commit in (False, True):
            with pytest.raises(ERRORS): raw_install(commit)
    assert _data(owner) == before and _channel_property(owner) is None


INDEX_TARGETS = ("default_t", "default_z", "reset_t", "reset_z", "reset_channels",
                 "compose_coordinate", "compose_t", "compose_active_c", "max_pixels", "max_working_bytes")


def _index_call(target, value, source, state):
    if target.startswith("max_"):
        limits = ij.ChannelDisplayLimits()
        setattr(limits, target, value)
        return ij.compose_channel_display(source, state, limits=limits)
    operation, field = target.split("_", 1)
    if operation == "default": return ij.default_channel_display(source, **{field: value})
    if operation == "reset":
        return ij.reset_channel_display_ranges(source, state, **{field: [value] if field == "channels" else value})
    assert operation == "compose"
    return ij.compose_channel_display(source, state, **{field: value})


@pytest.mark.parametrize("target", INDEX_TARGETS)
@pytest.mark.parametrize("value", (True, np.bool_(False), 1.), ids=("bool", "numpy_bool", "float"))
def test_indices_and_limits_reject_boolean_and_float(target, value):
    _, source = _dataset()
    with pytest.raises(TypeError): _index_call(target, value, source, _state())


@pytest.mark.parametrize("target", ("compose_coordinate", "compose_t", "compose_active_c", "max_pixels", "max_working_bytes"))
@pytest.mark.parametrize("value", (-1, 2**100), ids=("negative", "overflow"))
def test_indices_and_limits_refuse_native_overflow(target, value):
    _, source = _dataset()
    with pytest.raises(ERRORS): _index_call(target, value, source, _state())


OPAQUE = (
    {"schema": "imagejxx.channel-display", "version": 37, "future": [None, True, 2**64-1]},
    {"schema": "other", "version": 1, "future": {"": -0.}},
    {"version": 1, "opaque": [1, 2, 3]},
    {"schema": "imagejxx.channel-display", "version": 1., "opaque": "float version"},
    {"schema": "imagejxx.channel-display", "version": True, "opaque": "bool version"},
)


@pytest.mark.parametrize("opaque", OPAQUE)
def test_unknown_state_is_transportable_but_not_interpreted(opaque):
    _, owner = _owner()
    before = _data(owner)
    view = owner.snapshot().display
    view.properties = {**view.properties, "channel_display": opaque}
    owner.set_display(view)
    assert _typed(_channel_property(owner)) == _typed(opaque) and _data(owner) == before
    with pytest.raises(ValueError): ij.channel_display_state(owner.snapshot().display, owner.snapshot().dataset)
    cleared = ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset)
    assert "channel_display" not in cleared.properties
    assert cleared.properties == {k: v for k, v in view.properties.items() if k != "channel_display"}


def _record(owner, state, action="set", channels=()):
    snap = owner.snapshot()
    return {"schema": "imagejxx.channel-display-command", "version": 1, "action": action,
            "source": {"id": snap.id, "pixels_revision": snap.revisions.pixels,
                       "metadata_revision": snap.revisions.metadata, "shape": [2,3,3,2,4,1],
                       "type": "uint16", "sample_policy": "finite_only"},
            "position": {"t": snap.display.t, "c": snap.display.c, "z": snap.display.crosshair[2]},
            "state": state, "reset": {"channels": list(channels), "empty_finite": [False for _ in channels]}}


@pytest.mark.parametrize("action", ("set", "reset_range", "disable"))
def test_command_exact_record_and_display_only_publication(action):
    _, owner = _owner()
    before, previous = _data(owner), owner.snapshot().display
    state, selected = _state(), ()
    if action == "disable":
        owner.set_display(ij.with_channel_display(previous, owner.snapshot().dataset, state))
        previous = owner.snapshot().display
        state = None
    elif action == "reset_range": selected = (2, 0)
    calls = []
    def forbidden(): calls.append(True); raise AssertionError("metadata-only action invoked callback")
    kwargs = {} if action == "reset_range" else {"checkpoint": forbidden}
    if action != "reset_range":
        token = ij.CancellationToken()
        token.cancel()
        kwargs["cancellation"] = token
    command = ij.resolve_channel_display_command(owner.snapshot(), state, action, selected, **kwargs)
    expected_state = copy.deepcopy(state)
    if action == "reset_range":
        expected_state["channels"][2]["range"] = [40, 45]
        expected_state["channels"][0]["range"] = [24, 29]
    expected = _record(owner, expected_state, action, selected)
    assert command.action == action and command.state == expected_state
    assert command.record() == expected and command.source.id == owner.id
    ij.validate_channel_display_command_record(expected)
    replay = ij.channel_display_command_from_record(owner.snapshot(), expected)
    assert replay.record() == expected
    next_view = ij.command_display_state(command, owner.snapshot())
    assert _navigation(next_view) == _navigation(previous)
    assert {k: v for k, v in next_view.properties.items() if k != "channel_display"} == {
        k: v for k, v in previous.properties.items() if k != "channel_display"}
    owner.set_display(next_view)
    assert owner.snapshot().display.revision == previous.revision + 1
    assert _data(owner) == before and not calls
    assert _channel_property(owner) == expected_state


def test_replay_resolved_reset_retains_current_navigation_and_selection():
    _, owner = _owner()
    command = ij.resolve_channel_display_command(owner.snapshot(), _state(), action="reset_range", channels=[2,0])
    record = copy.deepcopy(command.record())
    view = owner.snapshot().display
    view.t, view.c, view.crosshair, view.zoom = 0, 0, [1, 0, 0], 4.
    owner.set_display(view)
    owner.select(ij.Selection("point", [(1, 0)]))
    current, before = owner.snapshot(), _data(owner)
    replay = ij.channel_display_command_from_record(current, record)
    assert replay.record() == record and replay.state["channels"][2]["range"] == [40,45]
    owner.set_display(ij.command_display_state(replay, current))
    assert _navigation(owner.snapshot().display) == _navigation(view) and _data(owner) == before


RECORD_BAD_IDS = ("schema", "version_bool", "version_float", "extra", "missing", "action",
    "id_bool", "id_float", "id_zero", "revision_negative", "revision_float", "shape_bool", "shape_overflow",
    "type", "policy", "position_bool", "position_float", "position_overflow", "position_extra",
    "state_missing", "state_lossy", "reset_duplicate", "reset_flag_int", "reset_flag_true", "reset_extra",
    "reset_string_channels", "reset_string_flags")


def _bad_record(record, case):
    result = copy.deepcopy(record)
    if case == "schema": result["schema"] = "other"
    elif case == "version_bool": result["version"] = True
    elif case == "version_float": result["version"] = 1.
    elif case == "extra": result["extra"] = None
    elif case == "missing": del result["position"]
    elif case == "action": result["action"] = "auto"
    elif case == "id_bool": result["source"]["id"] = True
    elif case == "id_float": result["source"]["id"] = 1.
    elif case == "id_zero": result["source"]["id"] = 0
    elif case == "revision_negative": result["source"]["pixels_revision"] = -1
    elif case == "revision_float": result["source"]["metadata_revision"] = 0.
    elif case == "shape_bool": result["source"]["shape"][0] = True
    elif case == "shape_overflow": result["source"]["shape"][0] = 2**63
    elif case == "type": result["source"]["type"] = "rgb"
    elif case == "policy": result["source"]["sample_policy"] = "anything"
    elif case == "position_bool": result["position"]["t"] = False
    elif case == "position_float": result["position"]["c"] = 0.
    elif case == "position_overflow": result["position"]["z"] = 2**64-1
    elif case == "position_extra": result["position"]["extra"] = 0
    elif case == "state_missing": result["state"] = None
    elif case == "state_lossy": result["state"]["channels"][0]["range"] = [0,2**53+1]
    elif case == "reset_duplicate": result["reset"]["channels"] = [2,2]
    elif case == "reset_flag_int": result["reset"]["empty_finite"][0] = 0
    elif case == "reset_flag_true": result["reset"]["empty_finite"][0] = True
    elif case == "reset_extra": result["reset"]["extra"] = None
    elif case == "reset_string_channels": result["reset"]["channels"] = ["2","0"]
    elif case == "reset_string_flags": result["reset"]["empty_finite"] = ["false","false"]
    else: raise AssertionError(case)
    return result


@pytest.mark.parametrize("case", RECORD_BAD_IDS)
def test_complete_original_validation_before_replay_and_rebind(case):
    _, owner = _owner()
    _, destination = _owner()
    original = ij.resolve_channel_display_command(owner.snapshot(), _state(), action="reset_range", channels=[2,0]).record()
    malformed = _bad_record(original, case)
    before, calls = _data(destination), []
    def callback(): calls.append(True)
    for operation in (lambda: ij.validate_channel_display_command_record(malformed),
                      lambda: ij.channel_display_command_from_record(destination.snapshot(), malformed),
                      lambda: ij.rebind_channel_display_command(destination.snapshot(), malformed, _state(), action="reset_range", checkpoint=callback)):
        with pytest.raises(ERRORS): operation()
    assert _data(destination) == before and not calls


@pytest.mark.parametrize("case", ("set_missing","set_channels","disable_state","disable_channels","reset_missing","reset_duplicate"))
def test_malformed_action_request_is_atomic(case):
    _, owner = _owner()
    before, calls = _data(owner), []
    action, state, channels = "set", _state(), ()
    if case == "set_missing": state = None
    elif case == "set_channels": channels = [0]
    elif case == "disable_state": action = "disable"
    elif case == "disable_channels": action, state, channels = "disable", None, [0]
    elif case == "reset_missing": action, state = "reset_range", None
    elif case == "reset_duplicate": action, channels = "reset_range", [0,0]
    with pytest.raises(ERRORS):
        ij.resolve_channel_display_command(owner.snapshot(), state, action, channels, checkpoint=lambda: calls.append(True))
    assert not calls and _data(owner) == before and _channel_property(owner) is None


@pytest.mark.parametrize("action", ("set", "reset_range"))
def test_explicit_rebind_uses_new_owner_and_captured_live_position(action):
    _, owner = _owner()
    _, destination = _owner()
    original = ij.resolve_channel_display_command(owner.snapshot(), _state()).record()
    frozen = copy.deepcopy(original)
    view = destination.snapshot().display
    view.t, view.c, view.crosshair = 0, 1, [1, 0, 1]
    destination.set_display(view)
    state = _state()
    state["channels"][0]["range"] = [7, 11]
    before = _data(destination)
    with pytest.raises(ValueError): ij.channel_display_command_from_record(destination.snapshot(), original)
    rebound = ij.rebind_channel_display_command(destination.snapshot(), original, state, action)
    expected = copy.deepcopy(state)
    if action == "reset_range":
        for c in range(3): expected["channels"][c]["range"] = [8*c+4,8*c+9]
    assert rebound.state == expected and rebound.record()["source"]["id"] == destination.id
    assert rebound.record()["position"] == {"t":0,"c":1,"z":1}
    assert original == frozen and _data(destination) == before


@pytest.mark.parametrize("change", ("pixels", "metadata"))
def test_stale_commands_refuse_with_revision_conflict(change):
    _, owner = _owner()
    command = ij.resolve_channel_display_command(owner.snapshot(), _state())
    edit = owner.begin_edit()
    if change == "pixels": edit.replace_plane(np.full((2,4), 123, np.uint16))
    else: edit.set_property("changed", True)
    owner.commit(edit)
    before = _data(owner)
    with pytest.raises(ij.RevisionConflict): ij.channel_display_command_from_record(owner.snapshot(), command.record())
    with pytest.raises(ij.RevisionConflict): ij.command_display_state(command, owner.snapshot())
    assert _data(owner) == before


@pytest.mark.parametrize("operation", ("defaults", "reset", "compose", "resolve_reset"))
@pytest.mark.parametrize("failure", ("cancel", "exception"))
def test_cancellation_and_python_exception_are_atomic(operation, failure):
    _, owner = _owner()
    state, before, calls = _state(), _data(owner), []
    token = ij.CancellationToken()
    class CallbackFailure(RuntimeError): pass
    def checkpoint():
        calls.append(True)
        if failure == "exception": raise CallbackFailure("owned display callback")
        token.cancel()
    kwargs = {"checkpoint": checkpoint, "cancellation": token}
    source = owner.snapshot().dataset
    work = {"defaults": lambda: ij.default_channel_display(source, 1, 2, **kwargs),
            "reset": lambda: ij.reset_channel_display_ranges(source, state, 1, 2, **kwargs),
            "compose": lambda: ij.compose_channel_display(source, state, **kwargs),
            "resolve_reset": lambda: ij.resolve_channel_display_command(owner.snapshot(), state, action="reset_range", **kwargs)}
    with pytest.raises(CallbackFailure if failure == "exception" else ij.OperationCancelled): work[operation]()
    assert calls and state == _state() and _data(owner) == before and _channel_property(owner) is None


def test_callbacks_hold_gil_and_binding_owns_mutable_inputs():
    pixels, source = _dataset(np.uint16)
    state, original = _state(), _state()
    state["mode"] = original["mode"] = "grayscale"
    holders = {"source": source, "state": state}
    calls, errors = [], []
    def checkpoint():
        assert ctypes.pythonapi.PyGILState_Check() == 1
        if not calls:
            holders.clear()
            state["channels"].clear()
            gc.collect()
            thread = threading.Thread(target=lambda: calls.append("python worker"))
            thread.start()
            thread.join(timeout=2)
            if thread.is_alive(): errors.append("callback did not allow Python thread progress")
        calls.append("checkpoint")
    actual = ij.compose_channel_display(source, state, "xy", 1, 1, 1, checkpoint=checkpoint)
    expected = np.repeat(pixels[1,1,1,...,0,None], 3, axis=-1).astype(np.uint8)
    np.testing.assert_array_equal(actual, expected)
    assert calls and "python worker" in calls and not errors
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()
    assert original["channels"] and not state["channels"]


def test_presentation_keeps_fourier_buffers_and_history():
    _, owner = _owner()
    edit = owner.begin_edit()
    spectrum = np.arange(8, dtype=np.float64).reshape(2,4).astype(np.complex128) + 2j
    edit.set_spectrum(ij.FourierState(spectrum, original_shape=(2,4)))
    owner.commit(edit)
    before = _data(owner)
    command = ij.resolve_channel_display_command(owner.snapshot(), _state())
    ij.compose_channel_display(owner.snapshot().dataset, command.state, "yz", 1, 1, 2)
    owner.set_display(ij.command_display_state(command, owner.snapshot()))
    assert _data(owner) == before and owner.snapshot().spectrum.numpy().tobytes() == spectrum.tobytes()


@pytest.mark.parametrize("case", ("pixels_zero", "bytes_zero", "pixels_below", "bytes_below", "exact"))
def test_metadata_preflight_limits_before_callbacks(case):
    _, source = _dataset()
    state, calls = _state(), []
    estimate = ij.estimate_channel_display(source, state, "xz", 1, 1, 2)
    assert (estimate.width, estimate.height, estimate.pixels) == (4,3,12)
    assert estimate.working_bytes == 4*12*8 + 4*8 + 6272
    assert estimate.contributing_channels == [0,1,2]
    limits = ij.ChannelDisplayLimits()
    limits.max_pixels, limits.max_working_bytes = estimate.pixels, estimate.working_bytes
    if case == "pixels_zero": limits.max_pixels = 0
    elif case == "bytes_zero": limits.max_working_bytes = 0
    elif case == "pixels_below": limits.max_pixels -= 1
    elif case == "bytes_below": limits.max_working_bytes -= 1
    if case == "exact":
        assert ij.compose_channel_display(source, state, "xz", 1, 1, 2, limits).shape == (3,4,3)
    else:
        with pytest.raises(ValueError): ij.compose_channel_display(source, state, "xz", 1, 1, 2, limits, checkpoint=lambda: calls.append(True))
        assert not calls


@pytest.mark.parametrize("change", ("channels", "type", "samples"))
def test_known_channel_count_edit_refuses_atomically_and_clear_allows_edit(change):
    _, owner = _owner()
    owner.set_display(ij.with_channel_display(owner.snapshot().display, owner.snapshot().dataset, _state()))
    before, display = _data(owner), owner.snapshot().display
    _, replacement = _dataset(np.float64 if change == "type" else np.uint16,
                              1 if change == "channels" else 3,
                              3 if change == "samples" else 1)
    edit = owner.begin_edit()
    edit.replace_dataset(replacement)
    with pytest.raises(ValueError): owner.commit(edit)
    assert edit.active and _data(owner) == before
    assert owner.snapshot().display.properties == display.properties
    # The concrete repair clears only the known state and adjusts active C for
    # the new extent; product publication must validate the final merged view.
    cleared = ij.with_channel_display(display, owner.snapshot().dataset)
    cleared.c = 0
    owner.commit(edit, display=cleared)
    assert tuple(owner.snapshot().dataset.shape) == (2,1 if change == "channels" else 3,
                                                   3,2,4,3 if change == "samples" else 1)
    assert "channel_display" not in owner.snapshot().display.properties


UNSUPPORTED = ((np.float64,1), (np.uint8,3), (np.uint8,4), (np.uint16,3),
               (np.uint16,4), (np.float32,3), (np.float32,4))


@pytest.mark.parametrize("dtype,samples", UNSUPPORTED)
def test_recognized_state_refuses_legacy_type_or_sample_axis(dtype, samples):
    pixels, source = _dataset(dtype, samples=samples)
    owner = ij.Document(source)
    state, view = _state(), owner.snapshot().display
    view.properties = {"channel_display": state}
    with pytest.raises(ValueError): owner.set_display(view)
    with pytest.raises(ValueError): ij.with_channel_display(owner.snapshot().display, source, state)
    assert owner.snapshot().display.properties == {} and not owner.snapshot().dirty
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()


@pytest.mark.parametrize("dtype,samples", UNSUPPORTED)
def test_legacy_explicit_range_passthrough_preserves_all_native_sources(dtype, samples):
    pixels, source = _dataset(dtype, samples=samples)
    calls = []
    actual = ij.effective_channel_display_range(source, None, 1, 2, 2,
        legacy_range=[-2., 19.], checkpoint=lambda: calls.append(True))
    assert actual == {"range": [-2.,19.], "empty_finite": False} and not calls
    with pytest.raises(ValueError): ij.effective_channel_display_range(source)
    assert source.numpy("TCZYXS").tobytes() == pixels.tobytes()


def _unframe(data):
    assert data[:10] == b"IMAGEJXX3\n"
    size, = struct.unpack("<Q", data[10:18])
    assert 0 < size < 4*1024*1024
    decoder = zlib.decompressobj()
    payload = decoder.decompress(data[18:], size+1)
    assert len(payload) == size and decoder.eof and not decoder.unused_data
    return json.loads(payload)


@pytest.mark.parametrize("value", (_state(), *OPAQUE), ids=("known", "future", "other", "missing", "float_version", "bool_version"))
def test_native_v3_roundtrip_preserves_known_and_opaque_values(value, tmp_path):
    pixels, owner = _owner()
    view = owner.snapshot().display
    view.properties = {**view.properties, "channel_display": value}
    owner.set_display(view)
    before = _data(owner)
    capture = ij.SessionCapture([owner], results=[{"row":7}], extensions={"exact":2**64-1})
    encoded = ij.encode_session(capture)
    literal = _unframe(encoded)
    assert literal["version"] == 3
    assert _typed(literal["documents"][0]["display"]["properties"]["channel_display"]) == _typed(value)
    reopened = ij.decode_session(encoded).documents[0]
    assert reopened.id != owner.id and not reopened.snapshot().dirty
    assert reopened.undo_steps == reopened.redo_steps == 0
    assert _typed(_channel_property(reopened)) == _typed(value)
    assert reopened.snapshot().dataset.numpy("TCZYXS").tobytes() == pixels.tobytes()
    path = tmp_path / "channel-display-µ.ijx"
    ij.save_session(str(path), capture)
    assert _typed(_channel_property(ij.load_session(str(path)).documents[0])) == _typed(value)
    assert _data(owner) == before


@pytest.mark.parametrize("value", (_state(1), OPAQUE[0]), ids=("known", "future"))
def test_detached_v2_guard_refuses_channel_state_without_publication(value, tmp_path):
    from imagejxx.ui import persistence
    from imagejxx.ui.model import Document
    detached = Document(np.arange(8, dtype=np.uint16).reshape(2,4))
    metadata = detached.metadata()
    before = copy.deepcopy(metadata)
    metadata["channel_display"] = value
    with pytest.raises(ValueError, match="[Cc]hannel display"): detached.load_metadata(metadata)
    assert detached.metadata() == before
    wire = persistence.serialize_document(detached)
    wire["channel_display"] = value
    with pytest.raises(ValueError, match="[Cc]hannel display"): persistence.deserialize_document(wire)
    source = ij.Dataset(np.arange(8, dtype=np.uint16).reshape(2,4), axes="YX")
    owner = ij.Document(source)
    view = owner.snapshot().display
    view.properties = {"channel_display": value}
    owner.set_display(view)
    path = tmp_path / "native-v3.ijx"
    ij.save_session(str(path), ij.SessionCapture([owner], extensions={"future":37}))
    destination = {"retained": [None, True, 7]}
    with pytest.raises(ValueError): persistence.load_session(path, extensions=destination)
    assert destination == {"retained": [None, True, 7]}


def test_requested_normalization_defensive_copies_and_effective_precedence():
    _, source = _dataset(np.uint16, 9)
    requested = _state(9)
    with pytest.raises(ValueError): ij.validate_channel_display(requested, source)
    normalized = ij.normalize_channel_display(requested, source)
    assert normalized["mode"] == "color" and requested["mode"] == "composite"
    view = ij.with_channel_display(ij.DisplayState(), source, requested)
    assert ij.channel_display_state(view, source) == normalized
    returned = ij.channel_display_state(view, source)
    returned["channels"][0]["range"][0] = 99
    props = view.properties
    props["channel_display"]["channels"].clear()
    assert ij.channel_display_state(view, source) == normalized
    observation = ij.effective_channel_display_range(source, normalized, np.int64(1), np.int32(2), np.int64(2), legacy_range=[7,19])
    assert observation == {"range":[0,255], "empty_finite":False}
    _, owner = _owner()
    command = ij.resolve_channel_display_command(owner.snapshot(), _state())
    frozen = command.record()
    record, returned_state = command.record(), command.state
    record["state"]["channels"].clear()
    returned_state["channels"].clear()
    assert command.record() == frozen and command.state == _state()
    for field, value in (("source", owner.snapshot()), ("state", _state()), ("action", "disable")):
        with pytest.raises(AttributeError): setattr(command, field, value)

