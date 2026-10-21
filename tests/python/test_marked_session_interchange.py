"""Held P04c assertions: independent literals, existing APIs, no fixture API.

SOURCE ONLY. Root must adopt the contract, qualify matching installed modules,
freeze this exact case inventory, and supply process/GUI/memory/time guards
before collection or execution. Current unmodified Python sources lack this
behavior; these assertions are proposed requirements, not a passed receipt.
"""

import base64
from copy import deepcopy
from dataclasses import fields, is_dataclass
import json
from pathlib import Path
import struct
import zlib

import imagejxx as ij
from imagejxx import _native
import numpy as np
import pytest

from imagejxx.ui.app import Editor
from imagejxx.ui.model import Document
from imagejxx.ui.persistence import (
    Preferences, load_session, save_session, serialize_document,
)


_STAGE = Path(__file__).resolve().parent.parent / "fixtures" / "marked_session"
_LITERALS = json.loads((_STAGE / "literal-cases-001.json").read_text(encoding="utf-8"))
_NEGATIVES = json.loads((_STAGE / "malformed-cases-001.json").read_text(encoding="utf-8"))
_BY_ID = {case["id"]: case for case in _LITERALS["native_fixtures"]}
_DISPLAY_KEYS = ("display_range", "lut", "custom_lut")
_TRANSPORT = {"pixels", "dtype", "shape", "spectrum", "fft_shape", "spectrum_shape", "pixel_sample_policy"}
_MAX_FILE = 256 * 1024


def _typed(value, depth=0):
    """JSON type + finite Double bits; bool/int and signed zero stay distinct."""
    assert depth <= 256
    if value is None:
        return ("null",)
    if type(value) is bool:
        return ("boolean", value)
    if type(value) is int:
        return ("integer", value)
    if type(value) is float:
        assert np.isfinite(value)
        return ("double", struct.pack("<d", value))
    if type(value) is str:
        return ("string", value)
    if type(value) in (tuple, list):
        return ("array", tuple(_typed(v, depth + 1) for v in value))
    assert type(value) is dict
    assert all(type(key) is str for key in value)
    return ("object", tuple((key, _typed(value[key], depth + 1)) for key in sorted(value)))


def _equal(actual, expected):
    assert _typed(actual) == _typed(expected)


def _entry(case):
    return {"display_properties": deepcopy(case["display_keys"]), "metadata": deepcopy(case["native_metadata"])}


def _root(cases=None):
    cases = _LITERALS["native_fixtures"] if cases is None else cases
    return {
        "format": "imagejxx-session", "version": 2,
        "documents": [deepcopy(case["wire"]) for case in cases],
        "results": deepcopy(_LITERALS["results"]), "workflow": deepcopy(_LITERALS["workflow"]),
        "native_document_state": {"version": 1, "documents": [_entry(case) for case in cases]},
    }


def _bytes(root=None, *, raw_json=None):
    content = json.dumps(root, allow_nan=False).encode("utf-8") if raw_json is None else raw_json.encode("utf-8")
    assert len(content) <= _MAX_FILE
    data = b"IMAGEJXX2\n" + struct.pack(">I", len(content)) + zlib.compress(content)
    assert len(data) <= _MAX_FILE
    return data


def _write(path, root=None, *, raw_json=None):
    path.write_bytes(_bytes(root, raw_json=raw_json))
    return path


def _read(path):
    data = path.read_bytes()
    assert 14 <= len(data) <= _MAX_FILE and data[:10] == b"IMAGEJXX2\n"
    size = struct.unpack(">I", data[10:14])[0]
    assert size <= _MAX_FILE
    decoder = zlib.decompressobj()
    payload = decoder.decompress(data[14:], size + 1)
    assert len(payload) == size and decoder.eof and not decoder.unused_data
    return json.loads(payload)


def _read_native_v3(path):
    """Decode only the new native-save frame; legacy _read stays strictly v2."""
    data = path.read_bytes()
    assert 18 <= len(data) <= _MAX_FILE and data[:10] == b"IMAGEJXX3\n"
    size, = struct.unpack("<Q", data[10:18])
    assert 0 < size <= _MAX_FILE
    decoder = zlib.decompressobj()
    payload = decoder.decompress(data[18:], size + 1)
    assert len(payload) == size and decoder.eof
    assert not decoder.unused_data and not decoder.unconsumed_tail

    def unique(pairs):
        result = {}
        for key, value in pairs:
            assert key not in result, f"Duplicate V3 field {key}"
            result[key] = value
        return result

    def nonfinite(value):
        raise AssertionError(f"Nonfinite V3 metadata constant {value}")

    return json.loads(payload, object_pairs_hook=unique, parse_constant=nonfinite)


def _v3_selection_literal(expected):
    if expected is None:
        return None
    reserved = {"kind", "points", "width"}
    if expected["kind"] == "mask":
        reserved |= {"shape", "mask"}
    result = {"kind": expected["kind"], "points": deepcopy(expected["points"]),
              "width": expected["width"],
              "attributes": {key: deepcopy(value) for key, value in expected.items() if key not in reserved}}
    if expected["kind"] == "mask":
        result.update(shape=deepcopy(expected["shape"]), mask=expected["mask"])
    return result


def _assert_native_v3(root, case):
    # The independent P04c literal supplies every expected value; neither the
    # producer serializer nor a product decoder supplies the V3 expectation.
    assert set(root) == {"format", "version", "documents", "results", "workflow", "extensions"}
    assert root["format"] == "imagejxx-session" and type(root["version"]) is int and root["version"] == 3
    assert type(root["documents"]) is list and len(root["documents"]) == 1
    _equal(root["results"], _LITERALS["results"])
    _equal(root["workflow"], _LITERALS["workflow"])
    # Workspace saves now capture the active document explicitly. This is a
    # test-owned literal for the single-document fixture, not producer output.
    _equal(root["extensions"], {"imagejxx_workspace": {"version": 1, "active_index": 0}})
    actual = root["documents"][0]
    assert set(actual) == {"axes", "shape", "pixel_type", "sample_policy", "storage", "planes",
                           "calibration", "metadata", "display", "selection", "previous_selection",
                           "rois", "overlays", "fourier"}
    depth, height, width = case["pixel_shape"]
    _equal(actual["shape"], [1, 1, depth, height, width, 1])
    assert actual["axes"] == "TCZYXS" and actual["pixel_type"] == case["pixel_dtype"]
    assert actual["sample_policy"] == "finite_only" and actual["storage"] == "Float64LE"
    # Expand the literal primitive payload into the documented double backing
    # using stdlib IEEE packing, then compare every byte of every ordered plane.
    codes = {"uint8": "B", "uint16": "H", "float32": "f", "float64": "d"}
    expected_pixels = struct.unpack("<" + codes[case["pixel_dtype"]] * (depth*height*width),
                                    base64.b64decode(case["pixel_storage_base64"], validate=True))
    assert type(actual["planes"]) is list and len(actual["planes"]) == depth
    for z, plane in enumerate(actual["planes"]):
        expected = b"".join(struct.pack("<d", sample)
                            for sample in expected_pixels[z*height*width:(z+1)*height*width])
        assert base64.b64decode(plane, validate=True) == expected
    _equal(actual["metadata"], case["native_metadata"])
    cal = case["calibration"]
    _equal(actual["calibration"], {
        "spacing": [cal[key] for key in ("pixel_width", "pixel_height", "pixel_depth")],
        **{key: cal[key] for key in ("origin", "direction", "unit", "time_unit", "frame_interval", "time_origin")},
    })
    _equal(actual["display"], {"t": 0, "c": 0, **case["camera"], "properties": case["display_properties"]})
    _equal(actual["selection"], _v3_selection_literal(case["selection"]))
    _equal(actual["previous_selection"], _v3_selection_literal(case["previous_selection"]))
    _equal(actual["rois"], [{"name": roi["name"], "selection": _v3_selection_literal(roi["selection"]),
        "t": None, "c": None, "z": roi.get("slice_index"),
        "attributes": {key: value for key, value in roi.items() if key not in {"name", "selection", "slice_index"}}}
        for roi in case["rois"]])
    _equal(actual["overlays"], case["overlays"])
    assert actual["fourier"] is None


def _display(item):
    return {key: deepcopy(item[key]) for key in _DISPLAY_KEYS if key in item}


def _assert_detached(document, case):
    assert document.data.dtype == np.dtype(case["pixel_dtype"])
    assert list(document.data.shape) == case["pixel_shape"]
    assert document.data.tobytes(order="C") == base64.b64decode(case["pixel_storage_base64"])
    assert not document.data.flags.writeable
    assert document.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
    item = serialize_document(document)
    _equal(_display(item), case["display_properties"])
    _equal(item["view_state"], case["camera"])
    for field in ("calibration", "selection", "previous_selection", "rois", "overlays", "native_properties", "vendor", "native_document_state"):
        _equal(item[field], case["wire"][field])


def _assert_marker(root, cases):
    assert type(root["version"]) is int and root["version"] == 2
    assert set(root["native_document_state"]) == {"version", "documents"}
    assert type(root["native_document_state"]["version"]) is int
    assert root["native_document_state"]["version"] == 1
    assert len(root["documents"]) == len(cases)
    _equal(root["native_document_state"]["documents"], [_entry(case) for case in cases])
    for wire, case in zip(root["documents"], cases, strict=True):
        assert [key for key in _DISPLAY_KEYS if key in wire] == case["display_keys"]
        _equal(_display(wire), case["display_properties"])


def _editor(tmp_path, name):
    # Missing desktop support is a qualification failure; never importorskip.
    return Editor(preferences=Preferences(tmp_path / (name + ".json")))


def _close(editor):
    for info in list(editor.document_infos):
        editor.close_document(info["id"], discard=True)
    assert not editor.document_infos and not editor.busy and not editor.jobs


def _assert_selection(actual, expected, shape):
    if expected is None:
        assert actual is None
        return
    assert actual.kind == expected["kind"]
    _equal(actual.points, expected["points"])
    _equal(actual.width, expected["width"])
    reserved = {"kind", "points", "width", "shape", "mask"}
    _equal(actual.attributes, {key: value for key, value in expected.items() if key not in reserved})
    if expected["kind"] == "mask":
        bits = np.unpackbits(np.frombuffer(base64.b64decode(expected["mask"]), dtype=np.uint8))
        np.testing.assert_array_equal(actual.mask(shape), bits[:np.prod(shape)].reshape(shape) * 255)


def _assert_native(snapshot, case):
    actual_metadata = snapshot.metadata
    actual_metadata["record_layout"] = snapshot.record_layout
    _equal(actual_metadata, case["native_metadata"])
    _equal(snapshot.display.properties, case["display_properties"])
    display = snapshot.display
    _equal({
        "orthogonal": display.orthogonal, "linked_zoom": display.linked_zoom,
        "crosshair": display.crosshair, "zoom": display.zoom,
        "plane_zoom": display.plane_zoom, "pan": display.pan,
    }, case["camera"])
    assert display.t == 0 and display.c == 0
    pixels = snapshot.dataset.numpy("ZYX")
    assert pixels.dtype == np.dtype(case["pixel_dtype"])
    assert list(pixels.shape) == case["pixel_shape"]
    assert pixels.tobytes(order="C") == base64.b64decode(case["pixel_storage_base64"])
    cal = snapshot.dataset.calibration
    expected = case["calibration"]
    _equal(list(cal.spacing), [expected[key] for key in ("pixel_width", "pixel_height", "pixel_depth")])
    for key in ("origin", "direction", "unit", "frame_interval", "time_origin", "time_unit"):
        _equal(getattr(cal, key), expected[key])
    _assert_selection(snapshot.selection, case["selection"], (2, 3))
    _assert_selection(snapshot.previous_selection, case["previous_selection"], (2, 3))
    assert len(snapshot.rois) == len(case["rois"])
    for roi, expected_roi in zip(snapshot.rois, case["rois"], strict=True):
        assert roi.name == expected_roi["name"]
        assert roi.t is None and roi.c is None and roi.z == expected_roi.get("slice_index")
        _assert_selection(roi.selection, expected_roi["selection"], (2, 3))
        _equal(roi.attributes, {key: value for key, value in expected_roi.items() if key not in {"name", "selection", "slice_index"}})
    _equal(snapshot.overlays, case["overlays"])


def _native_owner(case):
    cal = case["calibration"]
    calibration = ij.SpatialCalibration(
        spacing=[cal[key] for key in ("pixel_width", "pixel_height", "pixel_depth")],
        origin=cal["origin"], direction=cal["direction"], unit=cal["unit"],
        frame_interval=cal["frame_interval"], time_origin=cal["time_origin"], time_unit=cal["time_unit"],
    )
    pixels = np.frombuffer(base64.b64decode(case["pixel_storage_base64"]), dtype=np.dtype(case["pixel_dtype"]).newbyteorder("<")).reshape(case["pixel_shape"])
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX", calibration=calibration), case["native_metadata"]["title"])
    tx = owner.begin_edit()
    for key, value in case["native_metadata"]["properties"].items():
        tx.set_property(key, deepcopy(value))
    for key, value in case["native_metadata"]["extensions"].items():
        tx.set_extension(key, deepcopy(value))
    tx.set_record_layout(deepcopy(case["native_metadata"]["record_layout"]))
    selection = case["selection"]
    tx.select(ij.Selection(selection["kind"], selection["points"], selection["width"], attributes={"shape_unknown": "kept"}))
    previous = case["previous_selection"]
    tx.set_previous_selection(ij.Selection(previous["kind"], previous["points"], previous["width"], attributes={"previous_unknown": deepcopy(previous["previous_unknown"])}))
    tx.set_rois([ij.ROI("retained", ij.Selection(selection["kind"], selection["points"], selection["width"], attributes={"shape_unknown": "kept"}), z=1, attributes={"roi_unknown": "kept"})])
    tx.set_overlays(deepcopy(case["overlays"]))
    owner.commit(tx)
    display = ij.DisplayState()
    for key, value in case["camera"].items():
        setattr(display, key, deepcopy(value))
    display.properties = deepcopy(case["display_properties"])
    owner.set_display(display)
    # Existing snapshot constructor makes a clean owner without synthetic load edits.
    clean = ij.Document(owner.snapshot())
    _assert_native(clean.snapshot(), case)
    assert not clean.snapshot().dirty and clean.undo_steps == clean.redo_steps == 0
    return clean


def _state(document):
    # Entire detached state equality guards opaque sidecar/history aliases too.
    return _freeze(document.__dict__)


def _freeze(value):
    # Broader than the finite JSON oracle: invalid preflight values and every
    # history DATA field still need exact unchanged-state comparison.
    if isinstance(value, np.ndarray):
        return ("ndarray", value.dtype.str, value.shape, value.tobytes())
    if isinstance(value, ij.FloatingSamplePolicy):
        return ("sample-policy", str(value))
    if isinstance(value, Path):
        return ("path", str(value))
    if is_dataclass(value):
        return ("dataclass", type(value).__name__, tuple((f.name, _freeze(getattr(value, f.name))) for f in fields(value)))
    if type(value) is dict:
        return ("dict", tuple((key, _freeze(value[key])) for key in sorted(value)))
    if type(value) in (list, tuple):
        return (type(value).__name__, tuple(_freeze(v) for v in value))
    if type(value) is float:
        return ("double", struct.pack("<d", value))
    assert value is None or type(value) in (str, int, bool), type(value)
    return (type(value).__name__, value)


def _state_equal(document, before):
    assert _freeze(document.__dict__) == before


@pytest.mark.parametrize("dtype", ["uint8", "uint16", "float32", "float64"])
def test_C01_fresh_legacy_bytes_and_native_defaults(dtype, tmp_path):
    document = Document(np.array([[0, 1], [2, 3]], dtype=dtype), "Owned NaN adapter")
    record = _LITERALS["legacy_records"][dtype]
    root = {"format": "imagejxx-session", "version": 2, "documents": [record], "results": [], "workflow": []}
    path = tmp_path / "legacy.ijx"
    save_session(path, [document])
    assert path.read_bytes() == _bytes(root)
    assert "native_document_state" not in _read(path)
    editor = _editor(tmp_path, "legacy-preferences")
    try:
        editor.open_file(path)
        snapshot = editor.live_document().native_snapshot()
        assert snapshot.display.properties["lut"] == "Grays"
        assert not snapshot.dirty and editor.live_document().info["undo_steps"] == 0
    finally:
        _close(editor)


@pytest.mark.parametrize("case_id", ["empty", "range", "nulls", "named", "custom"])
def test_C02_native_python_native_exact(case_id, tmp_path):
    case = _BY_ID[case_id]
    producer = _editor(tmp_path, "producer")
    observer = _editor(tmp_path, "observer")
    original = _native_owner(case)
    before = original.snapshot()
    try:
        producer.add_native_document(original)
        producer.results, producer.workflow = _LITERALS["results"], _LITERALS["workflow"]
        source = tmp_path / "native.ijx"
        producer.action("save_workspace", path=str(source))
        _assert_native_v3(_read_native_v3(source), case)
        extensions = {}
        detached, results, workflow = load_session(source, extensions=extensions)
        _equal(extensions, {"imagejxx_workspace": {"version": 1, "active_index": 0}})
        assert len(detached) == 1 and not detached[0].dirty
        assert len(detached[0]._undo) == len(detached[0]._redo) == 0
        _assert_detached(detached[0], case)
        _equal(results, _LITERALS["results"])
        _equal(workflow, _LITERALS["workflow"])
        destination = tmp_path / "python.ijx"
        save_session(destination, detached, results=results, workflow=workflow)
        _assert_marker(_read(destination), [case])
        observer.open_file(destination)
        restored = observer.live_document()
        _assert_native(restored.native_snapshot(), case)
        assert not restored.info["dirty"] and restored.info["undo_steps"] == restored.info["redo_steps"] == 0
        _equal(observer.results, _LITERALS["results"])
        _equal(observer.workflow, _LITERALS["workflow"])
        assert before.shares_data(original.snapshot()) and before.revisions.matches(original.snapshot().revisions)
        _assert_native(original.snapshot(), case)
        assert original.undo_steps == original.redo_steps == 0
    finally:
        _close(observer)
        _close(producer)


@pytest.mark.parametrize("case_id", ["empty", "range", "nulls", "named", "custom"])
def test_C03_pure_projection_has_independent_legacy_oracle(case_id):
    case = _BY_ID[case_id]
    scalar = {key: value for key, value in case["wire"].items() if key not in _TRANSPORT}
    actual = json.loads(_native._legacy_session_state_json(json.dumps(scalar, allow_nan=False), 2))
    _equal(actual, case["legacy_projection"])
    assert _typed(actual["metadata"]) != _typed(case["native_metadata"])
    opaque = case["native_metadata"]["properties"]["unknown_property"]
    assert type(opaque["unsigned"]) is int and opaque["unsigned"] == 2**64 - 1
    assert type(opaque["signed"]) is int and opaque["signed"] == 17
    assert struct.pack("<d", opaque["negative_zero"]) == bytes.fromhex("0000000000000080")


@pytest.mark.parametrize("order", ["native-legacy-opted", "opted-native-legacy", "legacy-opted-native"])
def test_C04_mixed_ordinals_and_complete_marker(order, tmp_path):
    native_case = _BY_ID["empty"]
    path = _write(tmp_path / "marked.ijx", _root([native_case]))
    native = load_session(path)[0][0]
    legacy = Document(np.array([[0, 1], [2, 3]], dtype=np.float32), "Owned NaN adapter")
    bits = np.array(_LITERALS["opted_bits32"], dtype=np.uint32).view(np.float32).reshape(2, 3)
    opted = Document(bits, "Owned NaN adapter", sample_policy=ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32)
    owners = {"native": native, "legacy": legacy, "opted": opted}
    keys = order.split("-")
    documents = [owners[key] for key in keys]
    states = [_state(document) for document in documents]
    destination = tmp_path / "mixed.ijx"
    save_session(destination, documents, results=_LITERALS["results"], workflow=_LITERALS["workflow"])
    root = _read(destination)
    expected_entries = {
        "native": _entry(native_case),
        "legacy": {"display_properties": list(_DISPLAY_KEYS), "metadata": _LITERALS["legacy_native_metadata"]},
        "opted": {"display_properties": list(_DISPLAY_KEYS), "metadata": _LITERALS["legacy_native_metadata"]},
    }
    _equal(root["native_document_state"]["documents"], [expected_entries[key] for key in keys])
    assert len(root["documents"]) == 3
    for document, state in zip(documents, states, strict=True):
        _state_equal(document, state)
    observer = _editor(tmp_path, "mixed-observer")
    try:
        observer.open_file(destination)
        assert len(observer.document_infos) == 3
        for info, key in zip(observer.document_infos, keys, strict=True):
            snapshot = observer.live_document(info["id"]).native_snapshot()
            if key == "native":
                _assert_native(snapshot, native_case)
            else:
                metadata = snapshot.metadata
                metadata["record_layout"] = snapshot.record_layout
                _equal(metadata, _LITERALS["legacy_native_metadata"])
                pixels = snapshot.dataset.numpy("YX")
                expected = bits if key == "opted" else legacy.data
                assert pixels.dtype == expected.dtype and pixels.tobytes() == expected.tobytes()
                assert snapshot.dataset.sample_policy == owners[key].sample_policy
                assert snapshot.display.crosshair == [1, 1, 0]
                assert snapshot.display.zoom == 0 and snapshot.display.plane_zoom == [0, 0, 0]
            assert not snapshot.dirty and info["undo_steps"] == info["redo_steps"] == 0
        _equal(observer.results, _LITERALS["results"])
        _equal(observer.workflow, _LITERALS["workflow"])
    finally:
        _close(observer)


@pytest.mark.parametrize("assignment", ["read-lut", "lut-grays", "lut-null", "range-null", "custom-null"])
def test_C05_explicit_display_presence(assignment, tmp_path):
    case = deepcopy(_BY_ID["empty"])
    document = load_session(_write(tmp_path / "adopt.ijx", _root([case])))[0][0]
    before = _state(document)
    expected = {}
    if assignment == "read-lut":
        assert document.lut == "Grays"
        _state_equal(document, before)
    else:
        key, value = {"lut-grays": ("lut", "Grays"), "lut-null": ("lut", None), "range-null": ("display_range", None), "custom-null": ("custom_lut", None)}[assignment]
        setattr(document, key, value)
        expected[key] = value
    _equal(_display(serialize_document(document)), expected)
    assert len(document._undo) == len(document._redo) == 0 and not document.dirty
    case["display_properties"] = expected
    case["display_keys"] = [key for key in _DISPLAY_KEYS if key in expected]
    destination = tmp_path / "assigned.ijx"
    save_session(destination, [document])
    _assert_marker(_read(destination), [case])


@pytest.mark.parametrize("case_id", ["empty", "range", "nulls", "named", "custom"])
def test_C06_bulk_display_adoption_is_exact(case_id, tmp_path):
    original = _BY_ID["named"]
    document = load_session(_write(tmp_path / "adopt.ijx", _root([original])))[0][0]
    supplied = deepcopy(original["wire"])
    for key in _DISPLAY_KEYS:
        supplied.pop(key, None)
    supplied.update(deepcopy(_BY_ID[case_id]["display_properties"]))
    before_pixels = document.data.tobytes()
    document.load_metadata(supplied)
    _equal(_display(serialize_document(document)), _BY_ID[case_id]["display_properties"])
    assert document.data.tobytes() == before_pixels
    expected = deepcopy(original)
    expected["display_properties"] = deepcopy(_BY_ID[case_id]["display_properties"])
    expected["display_keys"] = deepcopy(_BY_ID[case_id]["display_keys"])
    path = tmp_path / "bulk.ijx"
    save_session(path, [document])
    _assert_marker(_read(path), [expected])
    assert not document.dirty and not document._undo and not document._redo


@pytest.mark.parametrize("invalid", ["range-reversed", "lut-bool", "custom-short", "custom-overflow", "calibration-nonpositive"])
def test_C07_bulk_invalid_leaves_all_state(invalid, tmp_path):
    case = _BY_ID["named"]
    document = load_session(_write(tmp_path / "adopt.ijx", _root([case])))[0][0]
    document.commit(document.data.copy() + np.float32(1))
    document.undo()
    before = _state(document)
    value = deepcopy(case["wire"])
    if invalid == "range-reversed":
        value["display_range"] = [2, 1]
    elif invalid == "lut-bool":
        value["lut"] = True
    elif invalid == "custom-short":
        value["custom_lut"] = [[0, 0, 0]] * 255
    elif invalid == "custom-overflow":
        value["custom_lut"] = [[0, 0, 0]] * 255 + [[256, 0, 0]]
    else:
        value["calibration"]["pixel_width"] = 0
    with pytest.raises(ValueError):
        document.load_metadata(value)
    _state_equal(document, before)


@pytest.mark.parametrize("mutation", ["display", "annotation", "returned-tree"])
def test_C08_deepcopy_isolates_every_owned_compartment(mutation, tmp_path):
    case = _BY_ID["custom"]
    original = load_session(_write(tmp_path / "adopt.ijx", _root([case])))[0][0]
    original.commit(original.data.copy() + np.float32(1))
    before = _state(original)
    copied = deepcopy(original)
    assert not np.shares_memory(copied.data, original.data)
    assert not np.shares_memory(copied._undo[-1].pixels, original._undo[-1].pixels)
    if mutation == "display":
        copied.custom_lut[0, 0] = 99
        copied.lut = "Fire"
    elif mutation == "annotation":
        copied.selection.attributes["shape_unknown"] = "copied"
        copied.rois[0].attributes["roi_unknown"] = "copied"
        copied.overlays[0].attributes["overlay_unknown"] = "copied"
    else:
        value = copied.metadata()
        value["native_properties"]["unknown_property"]["values"].clear()
        value["vendor"]["acquisition"].clear()
        value["calibration"]["intensity"]["coefficients"].clear()
        _equal(copied.metadata(), original.metadata())
    copied.undo()
    copied.redo()
    _state_equal(original, before)


@pytest.mark.parametrize("case_id", ["empty", "range", "nulls", "named", "custom"])
def test_C09_marked_edit_undo_redo_preserves_current_codec_state(case_id, tmp_path):
    case = _BY_ID[case_id]
    document = load_session(_write(tmp_path / "adopt.ijx", _root([case])))[0][0]
    original = document.data.tobytes()
    replacement = (document.data.copy() + 1).astype(document.data.dtype)
    document.commit(replacement)
    assert len(document._undo) == 1 and not document._redo and document.dirty
    # A subsequent camera/display edit must remain current across pixel history.
    document.view_state["zoom"] = 2.5
    document.lut = "Ice"
    expected_display = deepcopy(case["display_properties"])
    expected_display["lut"] = "Ice"
    for operation, pixel_bytes, undo_steps, redo_steps in (("undo", original, 0, 1), ("redo", replacement.tobytes(), 1, 0)):
        getattr(document, operation)()
        assert document.data.tobytes() == pixel_bytes
        assert len(document._undo) == undo_steps and len(document._redo) == redo_steps
        item = serialize_document(document)
        _equal(_display(item), expected_display)
        assert item["view_state"]["zoom"] == 2.5
        for field in ("calibration", "selection", "previous_selection", "rois", "overlays", "native_properties", "vendor", "native_document_state"):
            _equal(item[field], case["wire"][field])
        expected_case = deepcopy(case)
        expected_case["display_properties"] = deepcopy(expected_display)
        expected_case["display_keys"] = [key for key in _DISPLAY_KEYS if key in expected_display]
        path = tmp_path / (operation + ".ijx")
        save_session(path, [document])
        _assert_marker(_read(path), [expected_case])


def test_C10_unmarked_history_keeps_legacy_range_reset():
    document = Document(np.zeros((4, 5), dtype=np.float32))
    document.display_range = (0, 10)
    document.commit(np.ones((4, 5), dtype=np.float32))
    assert document.display_range is None
    document.display_range = (0, 20)
    document.undo()
    assert document.display_range is None
    document.display_range = (0, 30)
    document.redo()
    assert document.display_range is None


@pytest.mark.parametrize("annotation", ["mask", "roi-position-absent", "overlay-defaults-absent"])
def test_C11_supported_annotation_layout_survives(annotation, tmp_path):
    case = deepcopy(_BY_ID["named"])
    if annotation == "mask":
        mask = np.eye(2, 3, dtype=np.uint8)
        selected = {"kind": "mask", "points": [], "width": 1.0, "shape": [2, 3], "mask": base64.b64encode(np.packbits(mask != 0)).decode("ascii"), "vendor_mask": {"negative_zero": -0.0}}
        case["wire"]["selection"] = deepcopy(selected)
        case["selection"] = selected
    elif annotation == "roi-position-absent":
        case["native_metadata"]["record_layout"]["roi_slice_fields"] = []
        case["wire"]["rois"][0].pop("slice_index")
        case["rois"][0].pop("slice_index")
    # Third input already deliberately omits overlay color/width/font defaults.
    source = _write(tmp_path / "annotation.ijx", _root([case]))
    document = load_session(source)[0][0]
    _assert_detached(document, case)
    output = tmp_path / "annotation-python.ijx"
    save_session(output, [document])
    _assert_marker(_read(output), [case])
    observer = _editor(tmp_path, "annotation-observer")
    try:
        observer.open_file(output)
        _assert_native(observer.live_document().native_snapshot(), case)
    finally:
        _close(observer)


def _apply(root, edit):
    path = edit["path"]
    parent = root
    for key in path[:-1]:
        parent = parent[key]
    key, operation = path[-1], edit["operation"]
    if operation == "set":
        parent[key] = deepcopy(edit["value"])
    elif operation == "delete":
        del parent[key]
    elif operation == "pop":
        parent[key].pop()
    else:
        assert operation == "duplicate-last"
        parent[key].append(deepcopy(parent[key][-1]))


@pytest.mark.parametrize("case_id", [
    "marker-null", "marker-array", "version-zero", "version-two", "version-bool", "version-null", "version-text", "version-u64max",
    "marker-extra", "marker-missing-version", "marker-missing-documents", "documents-null", "documents-object", "count-short", "count-long",
    "entry-null", "entry-array", "entry-empty", "entry-extra", "entry-missing-display", "entry-missing-metadata",
    "keys-null", "keys-object", "keys-bool", "keys-unknown", "keys-duplicate", "keys-presence", "keys-order",
    "metadata-null", "metadata-array", "metadata-empty", "title-type", "properties-type", "extensions-type", "layout-type",
    "metadata-missing-title", "metadata-extra", "property-empty-name", "layout-root-bool", "extension-reserved",
    "title-contradiction", "property-contradiction", "extension-contradiction", "layout-contradiction",
    "marker-uint-float", "wire-uint-float", "marker-int-float", "wire-int-float", "marker-zero-sign", "wire-zero-sign",
    "display-contradiction", "range-missing", "slice-camera-contradiction", "root-version-bool", "root-format-type",
])
def test_C12_malformed_all_owner_load_is_atomic(case_id, tmp_path):
    case = next(value for value in _NEGATIVES["cases"] if value["id"] == case_id)
    root = _root()
    _apply(root, case)
    for edit in case.get("additional_edits", []):
        _apply(root, edit)
    path = _write(tmp_path / "malformed.ijx", root)
    with pytest.raises(ValueError):
        load_session(path)
    if case["native_reject"]:
        editor = _editor(tmp_path, "malformed-observer")
        try:
            owner = _native_owner(_BY_ID["named"])
            editor.add_native_document(owner)
            editor.results, editor.workflow = ["results-before"], ["workflow-before"]
            infos = deepcopy(editor.document_infos)
            snapshot = owner.snapshot()
            with pytest.raises(ValueError):
                editor.open_file(path)
            _equal(editor.document_infos, infos)
            assert editor.results == ["results-before"] and editor.workflow == ["workflow-before"]
            assert snapshot.shares_data(owner.snapshot()) and snapshot.revisions.matches(owner.snapshot().revisions)
            _assert_native(owner.snapshot(), _BY_ID["named"])
        finally:
            _close(editor)


@pytest.mark.parametrize("case_id", ["duplicate-root-marker", "duplicate-marker-version", "duplicate-property-key", "integer-above-u64", "integer-below-i64", "unpaired-surrogate"])
def test_C13_strict_json_domain_rejects_before_adoption(case_id, tmp_path):
    raw = json.dumps(_root(), allow_nan=False, separators=(",", ":"))
    if case_id == "duplicate-root-marker":
        marker = json.dumps(_root()["native_document_state"], allow_nan=False, separators=(",", ":"))
        raw = raw[:-1] + ',"native_document_state":' + marker + '}'
    elif case_id == "duplicate-marker-version":
        raw = raw.replace('"native_document_state":{"version":1,"documents":[', '"native_document_state":{"version":1,"version":1,"documents":[', 1)
    elif case_id == "duplicate-property-key":
        raw = raw.replace('"signed":17,', '"signed":17,"signed":17,', 1)
    elif case_id in ("integer-above-u64", "integer-below-i64"):
        token = "18446744073709551616" if case_id == "integer-above-u64" else "-9223372036854775809"
        raw = raw.replace('"signed":17,', '"signed":' + token + ',', 1)
    else:
        raw = raw.replace('"property-kept"', '"\\ud800"', 1)
    assert raw != json.dumps(_root(), allow_nan=False, separators=(",", ":"))
    with pytest.raises(ValueError):
        load_session(_write(tmp_path / "strict-json.ijx", raw_json=raw))


@pytest.mark.parametrize("unsupported", ["selection-defaults-omitted", "reserved-root-extension", "opaque-calibration-shadow", "uint8-fraction", "uint16-overflow", "float32-unrepresented"])
def test_C14_unrepresentable_input_rejects_without_partial_owners(unsupported, tmp_path):
    root = _root()
    if unsupported == "selection-defaults-omitted":
        root["documents"][0]["selection"].pop("width")
    elif unsupported == "reserved-root-extension":
        root["native_document_state"]["documents"][0]["metadata"]["extensions"]["view_state"] = {}
    elif unsupported == "opaque-calibration-shadow":
        root["native_document_state"]["documents"][0]["metadata"]["extensions"]["calibration"]["pixel_width"] = .4
    else:
        item = root["documents"][0]
        item["dtype"] = {"uint8-fraction": "uint8", "uint16-overflow": "uint16", "float32-unrepresented": "float32"}[unsupported]
        values = [float(i) for i in range(12)]
        values[0] = {"uint8-fraction": .5, "uint16-overflow": 65536.0, "float32-unrepresented": .1}[unsupported]
        item["pixels"] = base64.b64encode(struct.pack("<12d", *values)).decode("ascii")
    with pytest.raises(ValueError):
        load_session(_write(tmp_path / "unrepresentable.ijx", root))


def _canonical_legacy_record(opted):
    item = deepcopy(_LITERALS["opted_record"] if opted else _LITERALS["legacy_finite_bits_record"])
    # Literal native representation is required only inside a marked envelope;
    # unchanged fresh/unmarked Python bytes are checked separately by C01/V18/V26.
    for key in ("pixel_width", "pixel_height", "pixel_depth", "frame_interval", "time_origin"):
        item["calibration"][key] = float(item["calibration"][key])
    for key in ("origin", "direction"):
        item["calibration"][key] = [float(v) for v in item["calibration"][key]]
    item["view_state"] = {"orthogonal": False, "linked_zoom": True, "crosshair": [1, 1, 0], "zoom": 0.0, "plane_zoom": [0.0, 0.0, 0.0], "pan": [[0.0, 0.0], [0.0, 0.0], [0.0, 0.0]]}
    return item


def _typed_root(opted):
    return {
        "format": "imagejxx-session", "version": 2, "documents": [_canonical_legacy_record(opted)], "results": [], "workflow": [],
        "native_document_state": {"version": 1, "documents": [{"display_properties": list(_DISPLAY_KEYS), "metadata": deepcopy(_LITERALS["legacy_native_metadata"])}]},
    }


@pytest.mark.parametrize("opted", [False, True], ids=["finite", "quiet-nan"])
def test_C15_marked_typed_pixels_have_exact_source_bits(opted, tmp_path):
    root = _typed_root(opted)
    document = load_session(_write(tmp_path / "typed.ijx", root))[0][0]
    bits = _LITERALS["opted_bits32"] if opted else _LITERALS["finite_bits32"]
    expected = b"".join(struct.pack("<I", bit) for bit in bits)
    assert document.data.dtype == np.dtype(np.float32) and document.data.shape == (2, 3)
    assert document.data.tobytes() == expected
    assert document.sample_policy == (ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if opted else ij.FloatingSamplePolicy.FINITE_ONLY)
    with pytest.raises(AttributeError):
        document.sample_policy = ij.FloatingSamplePolicy.FINITE_ONLY
    output = tmp_path / "typed-python.ijx"
    save_session(output, [document])
    observer = _editor(tmp_path, "typed-observer")
    try:
        observer.open_file(output)
        snapshot = observer.live_document().native_snapshot()
        assert snapshot.dataset.numpy("YX").tobytes() == expected
        assert snapshot.dataset.sample_policy == document.sample_policy
        assert not snapshot.dirty
    finally:
        _close(observer)


def test_C16_marked_finite_spectrum_remains_exact_and_detached(tmp_path):
    root = _typed_root(False)
    components = _LITERALS["finite_spectrum"]["components"]
    raw = b"".join(struct.pack("<dd", real, imag) for real, imag in components)
    root["documents"][0].update(spectrum=base64.b64encode(raw).decode("ascii"), fft_shape=[2, 3], spectrum_shape=[2, 3])
    document = load_session(_write(tmp_path / "spectrum.ijx", root))[0][0]
    assert document.spectrum.dtype == np.dtype(np.complex128) and document.spectrum.shape == (2, 3)
    assert document.spectrum.tobytes() == raw and document.spectrum_shape == (2, 3)
    before = _state(document)
    output = tmp_path / "spectrum-python.ijx"
    save_session(output, [document])
    _state_equal(document, before)
    observer = _editor(tmp_path, "spectrum-observer")
    try:
        observer.open_file(output)
        spectrum = observer.live_document().native_snapshot().spectrum
        assert spectrum.numpy().tobytes() == raw and spectrum.original_shape == [2, 3]
        detached = spectrum.numpy()
        detached[:] = 0
        assert spectrum.numpy().tobytes() == raw
    finally:
        _close(observer)


@pytest.mark.parametrize("invalid", ["pixel-base64", "pixel-short", "policy-bool", "policy-unknown", "opted-infinity", "spectrum-base64", "spectrum-short", "spectrum-nonfinite"])
def test_C17_marked_payload_rejection_is_atomic(invalid, tmp_path):
    root = _typed_root(True)
    item = root["documents"][0]
    if invalid == "pixel-base64":
        item["pixels"] = "!"
    elif invalid == "pixel-short":
        item["pixels"] = base64.b64encode(b"\0" * 47).decode("ascii")
    elif invalid in ("policy-bool", "policy-unknown"):
        item["pixel_sample_policy"] = True if invalid == "policy-bool" else "preserve-all"
    elif invalid == "opted-infinity":
        item["pixels"] = base64.b64encode(struct.pack("<6Q", 0, 0, 0, 0, 0, 0x7FF0000000000000)).decode("ascii")
    else:
        raw = b"\0" * 96
        if invalid == "spectrum-short":
            raw = raw[:-1]
        elif invalid == "spectrum-nonfinite":
            raw = struct.pack("<Q", 0x7FF0000000000000) + raw[8:]
        item.update(spectrum="!" if invalid == "spectrum-base64" else base64.b64encode(raw).decode("ascii"), fft_shape=[2, 3], spectrum_shape=[2, 3])
    with pytest.raises(ValueError):
        load_session(_write(tmp_path / "payload-invalid.ijx", root))


@pytest.mark.parametrize("invalid", ["results-nonfinite", "workflow-nonfinite", "later-document-nonfinite"])
def test_C18_preflight_preserves_destination_documents_and_tables(invalid, tmp_path):
    case = _BY_ID["named"]
    documents, results, workflow = load_session(_write(tmp_path / "adopt.ijx", _root([case])))
    if invalid.startswith("later-document"):
        documents.append(deepcopy(documents[0]))
        documents[-1].view_state["vendor"] = float("nan")
    elif invalid == "results-nonfinite":
        results.append({"invalid": float("nan")})
    else:
        workflow.append({"invalid": float("inf")})
    destination = tmp_path / "retained.ijx"
    sentinel = b"preexisting destination bytes\x00\xff"
    destination.write_bytes(sentinel)
    unrelated = tmp_path / "retained.ijx.tmp"
    unrelated.write_bytes(b"unrelated temporary")
    states = [_state(document) for document in documents]
    table_before = _freeze((results, workflow))
    with pytest.raises(ValueError):
        save_session(destination, documents, results=results, workflow=workflow)
    assert destination.read_bytes() == sentinel and unrelated.read_bytes() == b"unrelated temporary"
    assert not list(tmp_path.glob(".ijxs-*.tmp"))
    for document, before in zip(documents, states, strict=True):
        _state_equal(document, before)
    assert _freeze((results, workflow)) == table_before


def test_C19_explicit_opaque_replacement_and_rejection_are_atomic(tmp_path):
    document = load_session(_write(tmp_path / "adopt.ijx", _root([_BY_ID["named"]])))[0][0]
    value = document.metadata()
    value["native_properties"] = {"replacement": {"id": 2**64 - 1, "negative_zero": -0.0}}
    value["vendor"] = {"replacement": True}
    value["calibration"]["intensity"] = {"unit": "counts", "coefficients": [1.0, 2.0]}
    document.load_metadata(value)
    item = serialize_document(document)
    for key in ("native_properties", "vendor", "calibration"):
        _equal(item[key], value[key])
    before = _state(document)
    invalid = deepcopy(value)
    invalid["native_properties"] = {"": "invalid-name"}
    with pytest.raises(ValueError):
        document.load_metadata(invalid)
    _state_equal(document, before)


@pytest.mark.parametrize("case_id", ["custom-null", "custom-fractional"])
def test_C20_custom_lut_raw_values_survive_without_uint8_coercion(case_id, tmp_path):
    case = next(value for value in _LITERALS["custom_numeric_fixtures"] if value["id"] == case_id)
    source = _write(tmp_path / "raw-custom.ijx", _root([case]))
    document = load_session(source)[0][0]
    _equal(_display(serialize_document(document)), case["display_properties"])
    first = document.custom_lut.tolist()[0]
    expected_first = case["display_properties"]["custom_lut"][0]
    if case_id == "custom-null":
        assert first[0] is None and first[1:] == expected_first[1:]
    else:
        for actual, expected in zip(first, expected_first, strict=True):
            assert struct.pack("<d", float(actual)) == struct.pack("<d", expected)
    output = tmp_path / "raw-custom-python.ijx"
    save_session(output, [document])
    _assert_marker(_read(output), [case])
    observer = _editor(tmp_path, "raw-custom-observer")
    try:
        observer.open_file(output)
        _assert_native(observer.live_document().native_snapshot(), case)
    finally:
        _close(observer)


@pytest.mark.parametrize("route", ["documents", "document-snapshot", "add-detached-document"])
def test_C21_existing_editor_adapters_preserve_marked_state(route, tmp_path):
    case = _BY_ID["named"]
    editor = _editor(tmp_path, "adapter-producer")
    observer = _editor(tmp_path, "adapter-observer")
    try:
        if route == "add-detached-document":
            document = load_session(_write(tmp_path / "adopt.ijx", _root([case])))[0][0]
            editor.add_document(document)
            _assert_native(editor.live_document().native_snapshot(), case)
            return
        live = editor.add_native_document(_native_owner(case))
        document = editor.documents[0] if route == "documents" else editor.document_snapshot(live.id)
        _assert_detached(document, case)
        assert not document.dirty and not document._undo and not document._redo
        path = tmp_path / "adapter.ijx"
        save_session(path, [document])
        _assert_marker(_read(path), [case])
        observer.open_file(path)
        _assert_native(observer.live_document().native_snapshot(), case)
    finally:
        _close(observer)
        _close(editor)


def test_C22_history_restores_data_title_previous_geometry_and_layout(tmp_path):
    case = _BY_ID["named"]
    document = load_session(_write(tmp_path / "adopt.ijx", _root([case])))[0][0]
    data_before = document.metadata()
    data_before["title"] = "Before pixel edit"
    data_before["vendor"] = {"stage": "before", "negative_zero": -0.0}
    data_before["previous_selection"] = {"kind": "point", "points": [[1.0, 0.0]], "width": 2.0, "previous_unknown": {"id": 2**64 - 1}}
    document.load_metadata(data_before)
    document.commit((document.data.copy() + 1).astype(document.data.dtype))
    data_after = document.metadata()
    data_after["title"] = "After pixel edit"
    data_after["vendor"] = {"stage": "after"}
    data_after["previous_selection"] = None
    document.load_metadata(data_after)
    document.view_state["zoom"] = 3.5
    document.lut = "Ice"
    document.undo()
    undo_item = serialize_document(document)
    for field in ("title", "vendor", "previous_selection", "calibration", "rois", "overlays", "native_properties"):
        _equal(undo_item[field], data_before[field])
    assert undo_item["view_state"]["zoom"] == 3.5 and undo_item["lut"] == "Ice"
    document.redo()
    redo_item = serialize_document(document)
    for field in ("title", "vendor", "previous_selection", "calibration", "rois", "overlays", "native_properties"):
        _equal(redo_item[field], data_after[field])
    assert redo_item["view_state"]["zoom"] == 3.5 and redo_item["lut"] == "Ice"
    assert len(document._undo) == 1 and not document._redo


@pytest.mark.parametrize("case_id", ["empty", "range", "nulls", "named", "custom"])
def test_C23_private_inverse_has_independent_wire_oracle(case_id):
    case = _BY_ID[case_id]
    scalar = {key: value for key, value in case["wire"].items() if key not in _TRANSPORT}
    actual = json.loads(_native._session_wire_metadata_json(
        json.dumps(case["native_metadata"], allow_nan=False), json.dumps(scalar, allow_nan=False), 3, 2, 2,
    ))
    _equal(actual, scalar)


@pytest.mark.parametrize("opted", [False, True], ids=["finite", "quiet-nan"])
def test_C24_stable_marked_state_omits_marker_after_exact_projection(opted, tmp_path):
    root = _typed_root(opted)
    document = load_session(_write(tmp_path / "stable-marked.ijx", root))[0][0]
    before = _state(document)
    output = tmp_path / "stable-unmarked.ijx"
    save_session(output, [document], results=_LITERALS["results"], workflow=_LITERALS["workflow"])
    saved = _read(output)
    assert "native_document_state" not in saved
    scalar = {key: value for key, value in saved["documents"][0].items() if key not in _TRANSPORT}
    projection = json.loads(_native._legacy_session_state_json(json.dumps(scalar, allow_nan=False), 1))
    _equal(projection, {"metadata": _LITERALS["legacy_native_metadata"], "display_properties": {"display_range": None, "lut": "Grays", "custom_lut": None}})
    _equal(saved["results"], _LITERALS["results"])
    _equal(saved["workflow"], _LITERALS["workflow"])
    _state_equal(document, before)
    observer = _editor(tmp_path, "stable-observer")
    try:
        observer.open_file(output)
        snapshot = observer.live_document().native_snapshot()
        metadata = snapshot.metadata
        metadata["record_layout"] = snapshot.record_layout
        _equal(metadata, _LITERALS["legacy_native_metadata"])
        assert not snapshot.dirty and observer.live_document().info["undo_steps"] == 0
    finally:
        _close(observer)
