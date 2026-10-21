"""Independent headless V3-to-detached-model compatibility assertions.

Literal native records are admitted, then written through native save_session.
The compatibility reader must either retain their complete representable state
or reject the archive before publishing its caller-owned extension destination.
No GUI import, product serializer golden, or test-module helper is required.
"""
import base64
from copy import deepcopy
import json
import struct
import zlib

import imagejxx as ij
from imagejxx.ui import persistence
from imagejxx.ui.model import Document
import numpy as np
import pytest


_MAX_BYTES = 4 * 1024 * 1024
_RESULTS = [{"row": 7, "negative_zero": -0.0, "flags": [None, True, 2**64-1]}]
_WORKFLOW = [{"operation": "add", "parameters": {"value": 2.0}, "all_slices": False}]
_ROOT_EXTENSIONS = {"opaque_future_state": {"version": 37, "negative_zero": -0.0,
                                          "values": [None, False, 2**64-1]}}


def _typed(value):
    if type(value) is float:
        return (float, struct.pack("<d", value))
    if isinstance(value, dict):
        return (dict, tuple((key, _typed(item)) for key, item in sorted(value.items())))
    if isinstance(value, (list, tuple)):
        return (list, tuple(_typed(item) for item in value))
    return (type(value), value)


def _base64_doubles(values):
    return base64.b64encode(b"".join(struct.pack("<d", value) for value in values)).decode("ascii")


def _values(dtype, depth):
    offset = .125 if dtype == "float32" else 0
    return [100*z+10*y+x+offset for z in range(depth) for y in range(3) for x in range(5)]


def _record(dtype="uint16", depth=2):
    mask = {"kind": "mask", "points": [], "width": 2.0,
            "attributes": {"vendor": {"negative_zero": -0.0, "tag": "kept"}},
            "shape": [3, 5], "mask": base64.b64encode(bytes([0xA4, 0x5B])).decode("ascii")}
    rectangle = {"kind": "rectangle", "points": [[1.0, 1.0], [4.0, 3.0]], "width": 1.0,
                 "attributes": {"imagejxx.roi_style": {"schema_version": 1,
                     "stroke_color": [255, 0, 0, 255], "fill_color": None,
                     "stroke_width": 2.5, "vendor": -0.0}}}
    values = _values(dtype, depth)
    return {
        "axes": "TCZYXS", "shape": [1, 1, depth, 3, 5, 1], "pixel_type": dtype,
        "sample_policy": "finite_only", "storage": "Float64LE",
        "planes": [_base64_doubles(values[z*15:(z+1)*15]) for z in range(depth)],
        "calibration": {"spacing": [.5, 1.25, 2.5], "origin": [11.0, -7.0, 19.0],
            "direction": [0.0, -1.0, 0.0, 1.0, 0.0, -0.0, 0.0, 0.0, 1.0],
            "unit": "um", "time_unit": "ms", "frame_interval": .75, "time_origin": 2.25},
        "metadata": {"title": "compatibility literal", "properties": {
                "integer": 1, "double": 1.0, "negative_zero": -0.0,
                "unsigned_max": 2**64-1, "nested": {"flags": [None, True, 2.5]}},
            "extensions": {"vendor": {"label": "preserved", "negative_zero": -0.0},
                           "native_document_state": {"opaque": True}}, "record_layout": {}},
        "display": {"t": 0, "c": 0, "crosshair": [4, 2, depth-1],
            "orthogonal": True, "linked_zoom": False, "zoom": 2.5,
            "plane_zoom": [1.25, 0.0, 4.0], "pan": [[3.0, -4.0], [-.5, .25], [0.0, -0.0]],
            "properties": {"lut": "Fire", "display_range": [-0.0, 255.0],
                "manual_threshold": {"version": 1, "lower": 12.0, "upper": 24.0,
                                     "t": 0, "c": 0, "z": depth-1}}},
        "selection": mask,
        "previous_selection": {"kind": "rectangle", "points": [[0.0, 0.0], [99.0, 77.0]],
                               "width": 1.0, "attributes": {"shape": "opaque", "mask": 123}},
        "rois": [{"name": "unpositioned", "selection": deepcopy(mask), "t": None, "c": None, "z": None,
                  "attributes": {"vendor": "mask ROI"}},
                 {"name": "last", "selection": rectangle, "t": None, "c": None, "z": depth-1,
                  "attributes": {"vendor": {"negative_zero": -0.0}}}],
        "overlays": [{"kind": "point", "points": [[2.0, 1.0]], "slice_index": depth-1,
                      "overlay_vendor": {"negative_zero": -0.0}}],
        "fourier": {"shape": [2, 3], "original_shape": [1, 2], "storage": "Complex128LE",
            "pixels": _base64_doubles([1.0, -0.0, 2.0, .5, -3.0, 1.25, 0.0, -2.0, .25, 4.0, 5.0, 0.0])},
    }


def _write_native(path, records, *, extensions=None):
    root = {"format": "imagejxx-session", "version": 3, "documents": records,
            "results": deepcopy(_RESULTS), "workflow": deepcopy(_WORKFLOW),
            "extensions": {} if extensions is None else deepcopy(extensions)}
    raw = json.dumps(root, allow_nan=False).encode("utf-8")
    assert len(raw) <= _MAX_BYTES and len(records) <= 4
    literal = b"IMAGEJXX3\n" + struct.pack("<Q", len(raw)) + zlib.compress(raw)
    limits = ij.SessionLimits()
    limits.max_documents = 4
    limits.max_json_bytes = limits.max_encoded_bytes = _MAX_BYTES
    limits.max_working_bytes = 32 * 1024 * 1024
    owners = ij.decode_session(literal, limits).documents
    snapshots = [owner.snapshot() for owner in owners]
    capture = ij.SessionCapture(snapshots, results=_RESULTS, workflow=_WORKFLOW,
                                extensions={} if extensions is None else extensions)
    ij.save_session(str(path), capture, limits)
    assert path.stat().st_size <= _MAX_BYTES and path.read_bytes().startswith(b"IMAGEJXX3\n")
    for owner, snapshot in zip(owners, snapshots):
        assert owner.snapshot().shares_data(snapshot) and not owner.snapshot().dirty
        assert owner.undo_steps == owner.redo_steps == 0


def _selection(actual, expected):
    assert actual.kind == expected["kind"]
    assert _typed(actual.points) == _typed(expected["points"])
    assert _typed(actual.width) == _typed(expected["width"])
    assert _typed(actual.attributes) == _typed(expected["attributes"])
    wire = actual.to_dict()
    if expected["kind"] == "mask":
        bits = np.unpackbits(np.array([0xA4, 0x5B], np.uint8))[:15].reshape(3, 5) * 255
        np.testing.assert_array_equal(actual.bitmap, bits)
        assert wire["shape"] == [3, 5] and wire["mask"] == base64.b64encode(bytes([0xA4, 0x5B])).decode("ascii")


def _assert_detached(actual, expected):
    assert isinstance(actual, Document)
    depth, dtype = expected["shape"][2], expected["pixel_type"]
    shape = (depth, 3, 5) if depth > 1 else (3, 5)
    pixels = np.array(_values(dtype, depth), dtype=dtype).reshape(shape)
    assert actual.is_stack is (depth > 1) and actual.data.dtype == np.dtype(dtype)
    assert actual.data.shape == shape and actual.data.tobytes() == pixels.tobytes()
    assert not actual.data.flags.writeable and not actual.dirty
    assert actual._undo == actual._redo == []
    assert actual.title == "compatibility literal" and actual.slice_index == depth-1
    cal = actual.calibration
    assert _typed([cal.pixel_width, cal.pixel_height, cal.pixel_depth]) == _typed([.5, 1.25, 2.5])
    for key in ("origin", "direction", "unit", "time_unit", "frame_interval", "time_origin"):
        assert _typed(getattr(cal, key)) == _typed(expected["calibration"][key])
    state = actual._session_native_state()
    assert state is not None and _typed(state["metadata"]) == _typed(expected["metadata"])
    assert _typed(actual.view_state) == _typed({key: expected["display"][key] for key in (
        "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan")})
    assert _typed(actual.display_range) == _typed([-0.0, 255.0]) and actual.lut == "Fire"
    assert _typed(actual.metadata()["manual_threshold"]) == _typed(expected["display"]["properties"]["manual_threshold"])
    _selection(actual.selection, expected["selection"])
    _selection(actual.previous_selection, expected["previous_selection"])
    assert len(actual.rois) == 2
    for roi, literal in zip(actual.rois, expected["rois"]):
        assert roi.name == literal["name"] and roi.slice_index == literal["z"]
        assert _typed(roi.attributes) == _typed(literal["attributes"])
        _selection(roi.selection, literal["selection"])
    assert _typed([overlay.to_dict() for overlay in actual.overlays]) == _typed(expected["overlays"])
    assert actual.spectrum.dtype == np.dtype(np.complex128) and actual.spectrum.shape == (2, 3)
    assert actual.spectrum.tobytes() == b"".join(struct.pack("<dd", *pair) for pair in (
        (1.0, -0.0), (2.0, .5), (-3.0, 1.25), (0.0, -2.0), (.25, 4.0), (5.0, 0.0)))
    assert actual.spectrum_shape == (1, 2)


@pytest.mark.parametrize("dtype,depth", [("uint16", 2), ("float32", 1)])
def test_native_v3_representable_state_survives_legacy_reader_and_v2_resave(dtype, depth, tmp_path):
    expected = _record(dtype, depth)
    path = tmp_path / "native.ijx"
    _write_native(path, [expected])
    documents, results, workflow = persistence.load_session(path)
    assert len(documents) == 1 and _typed(results) == _typed(_RESULTS) and _typed(workflow) == _typed(_WORKFLOW)
    _assert_detached(documents[0], expected)
    # Re-saving through the unchanged v2 facade must retain the threshold
    # template, opaque metadata, packed tail bits and Fourier samples too.
    legacy = tmp_path / "resaved-v2.ijx"
    persistence.save_session(legacy, documents, results=results, workflow=workflow)
    assert legacy.read_bytes().startswith(b"IMAGEJXX2\n")
    reopened, reopened_results, reopened_workflow = persistence.load_session(legacy)
    _assert_detached(reopened[0], expected)
    assert _typed(reopened_results) == _typed(_RESULTS) and _typed(reopened_workflow) == _typed(_WORKFLOW)


def test_native_v3_root_extensions_require_destination_and_are_owned(tmp_path):
    path = tmp_path / "extensions.ijx"
    _write_native(path, [_record()], extensions=_ROOT_EXTENSIONS)
    before = path.read_bytes()
    with pytest.raises(ValueError):
        persistence.load_session(path)
    destination = {"old": {"negative_zero": -0.0}}
    documents, results, workflow = persistence.load_session(path, extensions=destination)
    assert len(documents) == 1 and _typed(destination) == _typed(_ROOT_EXTENSIONS)
    _assert_detached(documents[0], _record())
    destination["opaque_future_state"]["values"].append("caller mutation")
    fresh = {}
    persistence.load_session(path, extensions=fresh)
    assert _typed(fresh) == _typed(_ROOT_EXTENSIONS) and path.read_bytes() == before
    assert _typed(results) == _typed(_RESULTS) and _typed(workflow) == _typed(_WORKFLOW)


@pytest.mark.parametrize("unsupported", ["time", "channel", "uint8_backing", "float32_backing",
                                         "unknown_display", "channel_display", "roi_t", "roi_c"])
def test_later_unrepresentable_document_rejects_without_publishing_extensions(unsupported, tmp_path):
    valid, bad = _record(), _record(depth=1)
    if unsupported in ("time", "channel"):
        bad["shape"][0 if unsupported == "time" else 1] = 2
        bad["planes"] *= 2
        bad["fourier"] = None  # Isolate the legacy-axis refusal from native Fourier admission.
    elif unsupported == "uint8_backing":
        bad["pixel_type"] = "uint8"
        values = _values("uint8", 1)
        values[:3] = [-1.0, .5, 300.0]
        bad["planes"] = [_base64_doubles(values)]
    elif unsupported == "float32_backing":
        bad["pixel_type"] = "float32"
        values = _values("float32", 1)
        values[0] = 1 + 2**-40
        bad["planes"] = [_base64_doubles(values)]
    elif unsupported in ("unknown_display", "channel_display"):
        key = "unsupported_display_state" if unsupported == "unknown_display" else "channel_display"
        bad["display"]["properties"][key] = {"version": 37, "opaque": [None, -0.0]}
    else:
        bad["rois"][0]["t" if unsupported == "roi_t" else "c"] = 0
    path = tmp_path / "mixed.ijx"
    _write_native(path, [valid, bad], extensions=_ROOT_EXTENSIONS)
    before = path.read_bytes()
    destination = {"existing": {"typed": [True, 7, 7.0, -0.0]}}
    before_destination = _typed(destination)
    with pytest.raises(ValueError):
        persistence.load_session(path, extensions=destination)
    assert _typed(destination) == before_destination and path.read_bytes() == before
