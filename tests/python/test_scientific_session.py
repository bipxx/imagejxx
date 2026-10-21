"""Headless P06a assertions with independently authored session framing/payloads.

No UI adapter, native encoder or product JSON parser supplies the expected data.
All fixture archives are bounded to 32 MiB and all captures to 16 documents.
"""
import base64
from copy import deepcopy
import json
from pathlib import Path
import struct
import zlib

import imagejxx as ij
import numpy as np
import pytest


_CAP = 32 * 1024 * 1024
_FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "marked_session"
_OLD = json.loads((_FIXTURES / "literal-cases-001.json").read_text(encoding="utf-8"))


def _limits(**changes):
    result = ij.SessionLimits()
    result.max_documents = 16
    result.max_json_bytes = result.max_encoded_bytes = _CAP
    result.max_working_bytes = 128 * 1024 * 1024
    for name, value in changes.items():
        setattr(result, name, value)
    return result


def _typed(value):
    if type(value) is float:
        return (float, struct.pack("<d", value))
    if type(value) is dict:
        return (dict, tuple((k, _typed(v)) for k, v in sorted(value.items())))
    if type(value) in (list, tuple):
        return (list, tuple(_typed(v) for v in value))
    return (type(value), value)


def _frame(root=None, *, text=None, version=3):
    payload = (json.dumps(root, allow_nan=False, ensure_ascii=False)
               if text is None else text).encode("utf-8")
    assert 0 < len(payload) <= _CAP
    header = (b"IMAGEJXX3\n" + struct.pack("<Q", len(payload)) if version == 3
              else b"IMAGEJXX2\n" + struct.pack(">I", len(payload)))
    result = header + zlib.compress(payload)
    assert len(result) <= _CAP
    return result


def _unframe(data):
    assert type(data) is bytes and data[:10] == b"IMAGEJXX3\n"
    size, = struct.unpack("<Q", data[10:18])
    assert 0 < size <= _CAP
    decoder = zlib.decompressobj()
    text = decoder.decompress(data[18:], size + 1)
    assert len(text) == size and decoder.eof and not decoder.unused_data
    return json.loads(text)


def _raw(values):
    return b"".join(struct.pack("<d", value) for value in values)


def _document(values=(0, 1, 2, 3, 4, 5), *, dtype="uint8", policy="finite_only"):
    return {
        "axes": "TCZYXS", "shape": [1, 1, 1, 2, 3, 1],
        "pixel_type": dtype, "sample_policy": policy, "storage": "Float64LE",
        "planes": [base64.b64encode(_raw(values)).decode("ascii")],
        "calibration": {
            "spacing": [1.0, 1.0, 1.0], "origin": [0.0, 0.0, 0.0],
            "direction": [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
            "unit": "px", "time_unit": "s", "frame_interval": 1.0, "time_origin": 0.0,
        },
        "metadata": {"title": "literal", "properties": {}, "extensions": {}, "record_layout": {}},
        "display": {
            "t": 0, "c": 0, "crosshair": [2, 1, 0], "orthogonal": False,
            "linked_zoom": True, "zoom": 0.0, "plane_zoom": [0.0, 0.0, 0.0],
            "pan": [[0.0, 0.0], [0.0, 0.0], [0.0, 0.0]], "properties": {},
        },
        "selection": None, "previous_selection": None, "rois": [], "overlays": [], "fourier": None,
    }


def _root(document):
    return {"format": "imagejxx-session", "version": 3, "documents": [document],
            "results": [{"value": 7, "zero": -0.0}], "workflow": [{"operation": "retained"}],
            "extensions": {"opaque_future_state": {"version": 1, "future": [None, True, 2]}}}


def _fresh(owner):
    snapshot = owner.snapshot()
    assert not snapshot.dirty and owner.undo_steps == owner.redo_steps == 0
    assert [getattr(snapshot.revisions, key) for key in ("pixels", "metadata", "selection", "display")] == [0] * 4
    assert snapshot.display.revision == 0


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16, np.float32, np.float64])
def test_scientific_tczyxs_values_and_fresh_identity(dtype):
    t, c, z, y, x = np.indices((2, 3, 4, 5, 7))
    values = ((73*t + 41*c + 17*z + 7*y + x) % 251 if dtype == np.uint8
              else 10000*t + 1000*c + 100*z + 10*y + x)
    if dtype == np.float32:
        values = values + .125
    pixels = values.astype(dtype)[..., None]
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"), "scientific")
    before = owner.snapshot()
    capture = ij.SessionCapture([before])
    estimate = ij.estimate_session(capture, _limits())
    assert (estimate.documents, estimate.planes, estimate.samples) == (1, 24, 840)
    assert estimate.source_bytes >= 6720 and estimate.working_bytes <= 128 * 1024 * 1024
    encoded = ij.encode_session(capture, _limits())
    literal = _unframe(encoded)["documents"][0]
    assert literal["axes"] == "TCZYXS" and literal["shape"] == [2, 3, 4, 5, 7, 1]
    assert literal["storage"] == "Float64LE" and len(literal["planes"]) == 24
    # Every plane is independently matched, not just an endpoint or round-trip.
    for index, plane in enumerate(literal["planes"]):
        ti, rest = divmod(index, 12)
        ci, zi = divmod(rest, 4)
        assert base64.b64decode(plane) == _raw(pixels[ti, ci, zi].ravel())
    restored = ij.decode_session(encoded, _limits())
    actual = restored.documents[0].snapshot()
    array = actual.dataset.numpy("TCZYXS")
    assert array.dtype == np.dtype(dtype) and array.shape == (2, 3, 4, 5, 7, 1)
    assert array.tobytes() == pixels.tobytes() and actual.id != before.id
    assert owner.snapshot().shares_data(before) and owner.undo_steps == 0
    _fresh(restored.documents[0])


@pytest.mark.parametrize("samples", [3, 4])
def test_scientific_channel_is_independent_of_rgb_samples(samples):
    t, c, z, y, x, s = np.indices((2, 3, 1, 2, 3, samples))
    pixels = ((73*t + 41*c + 17*z + 7*y + 3*x + s) % 251).astype(np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
    decoded = ij.decode_session(ij.encode_session(ij.SessionCapture([owner]), _limits()), _limits())
    actual = decoded.documents[0].snapshot().dataset.numpy("TCZYXS")
    assert actual.shape == (2, 3, 1, 2, 3, samples) and actual.tobytes() == pixels.tobytes()


@pytest.mark.parametrize("dtype,values", [
    ("uint8", [-1, .5, 300, 0, -0.0, 255]),
    ("float32", [1 + 2**-40, -0.0, .125, -3, 2, 0]),
])
def test_literal_noncanonical_native_backing_survives_primitive_numpy_export(dtype, values):
    # Version1 and an unrecognized version remain document-scoped opaque data.
    # These literals define preservation, not P06c rendering/channel semantics.
    channel_display = {"version": 1 if dtype == "uint8" else 37,
                       "opaque_payload": {"negative_zero": -0.0, "unsigned_max": 2**64-1,
                                          "unknown": [None, True, {"vendor": "retained"}]}}
    document = _document(values, dtype=dtype)
    document["display"]["properties"]["channel_display"] = deepcopy(channel_display)
    decoded = ij.decode_session(_frame(_root(document)), _limits())
    snapshot = decoded.documents[0].snapshot()
    assert snapshot.dataset.numpy("YX").dtype == np.dtype(dtype)
    assert _typed(snapshot.display.properties["channel_display"]) == _typed(channel_display)
    # NumPy is a declared-type export; exact native backing is checked on wire.
    record = _unframe(ij.encode_session(ij.SessionCapture([snapshot]), _limits()))["documents"][0]
    assert record["shape"] == [1, 1, 1, 2, 3, 1]
    assert base64.b64decode(record["planes"][0]) == _raw(values)
    assert _typed(record["display"]["properties"]["channel_display"]) == _typed(channel_display)
    _fresh(decoded.documents[0])


def test_literal_v2_quiet_nan_bits_and_finite_fourier_migrate_to_v3():
    words = [0, 0x8000000000000000, 0x36A0000000000000,
             0x7FF8000020000000, 0xFFF8000020000000, 0x3FF0000000000000]
    values = struct.unpack("<6d", struct.pack("<6Q", *words))
    pairs = [(1, -0.0), (2, .5), (-3, 1.25), (0, -2), (.25, 4), (5, 0)]
    raw_spectrum = b"".join(struct.pack("<dd", *pair) for pair in pairs)
    document = {"title": "legacy quiet NaN and Fourier", "is_stack": False,
                "dtype": "float32", "shape": [2, 3], "pixels": base64.b64encode(_raw(values)).decode("ascii"),
                "pixel_sample_policy": "preserve_quiet_nan_float32", "slice_index": 0,
                "spectrum": base64.b64encode(raw_spectrum).decode("ascii"),
                "fft_shape": [2, 3], "spectrum_shape": [1, 2]}
    root = {"format": "imagejxx-session", "version": 2, "documents": [document], "results": [], "workflow": []}
    decoded = ij.decode_session(_frame(root, version=2), _limits())
    assert decoded.source_version == 2
    snapshot = decoded.documents[0].snapshot()
    assert snapshot.dataset.sample_policy == ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32
    assert snapshot.dataset.numpy("YX").view(np.uint32).ravel().tolist() == [
        0, 0x80000000, 1, 0x7FC00001, 0xFFC00001, 0x3F800000]
    assert snapshot.spectrum.shape == [2, 3] and snapshot.spectrum.original_shape == [1, 2]
    assert snapshot.spectrum.numpy().tobytes() == raw_spectrum
    record = _unframe(ij.encode_session(ij.SessionCapture([snapshot]), _limits()))["documents"][0]
    assert base64.b64decode(record["planes"][0]) == struct.pack("<6Q", *words)
    assert base64.b64decode(record["fourier"]["pixels"]) == raw_spectrum


def test_literal_scientific_annotations_calibration_and_immutable_capture():
    document = _document(dtype="uint16")
    document["shape"] = [2, 3, 4, 5, 7, 1]
    document["planes"] = [base64.b64encode(_raw([
        10000*t+1000*c+100*z+10*y+x for y in range(5) for x in range(7)
    ])).decode("ascii") for t in range(2) for c in range(3) for z in range(4)]
    document["calibration"].update(spacing=[.25, 1.5, 3.0], origin=[-7.0, 11.0, -13.0],
        direction=[-1.0, .5, -0.0, 0.0, 1.0, .25, 0.0, 0.0, 1.0],
        unit="um", frame_interval=.75, time_origin=2.5)
    document["metadata"] = {"title": "captured", "properties": {
        "integer": 1, "double": 1.0, "negative_zero": -0.0, "boolean": True, "null": None,
        "signed_min": -(2**63), "unsigned_max": 2**64-1},
        "extensions": {"native_document_state": {"opaque": True}}, "record_layout": {"opaque": [None, -0.0]}}
    mask = {"kind": "mask", "points": [], "width": 1.0, "attributes": {"vendor": -0.0},
            "shape": [5, 7], "mask": base64.b64encode(bytes([0, 0xE1, 0x43, 0x80, 0x3F])).decode("ascii")}
    document["selection"] = mask
    document["previous_selection"] = {"kind": "rectangle", "points": [[0.0, 0.0], [99.0, 77.0]],
                                      "width": 1.0, "attributes": {"shape": "opaque", "mask": 123}}
    document["rois"] = [{"name": name, "selection": deepcopy(mask), "t": t, "c": c, "z": z,
                         "attributes": {"opaque": [False, -0.0]}} for name, t, c, z in (
                             ("unpositioned", None, None, None), ("zero", 0, 0, 0), ("last", 1, 2, 3))]
    document["overlays"] = [{"kind": "text", "text": "retained", "vendor": -0.0}]
    document["display"].update(t=1, c=2, crosshair=[6, 4, 3], orthogonal=True, linked_zoom=False,
        zoom=2.5, plane_zoom=[1.25, 0.0, 4.0], pan=[[3.0, -4.0], [-.5, .25], [0.0, -0.0]],
        properties={"lut": "Fire", "display_range": [-0.0, 12346.0],
                    "channel_display": {"version": 1, "opaque_payload": {
                        "scientific_channel": 2, "negative_zero": -0.0, "future": [None, False]}}})
    root = _root(document)
    decoded = ij.decode_session(_frame(root), _limits())
    owner = decoded.documents[0]
    captured = owner.snapshot()
    assert captured.dataset.calibration.voxel_to_world([6, 4, 3]) == [-5.5, 19.25, -4.0]
    assert captured.dataset.calibration.time(1) == 3.25
    assert [(roi.t, roi.c, roi.z) for roi in captured.rois] == [(None, None, None), (0, 0, 0), (1, 2, 3)]
    assert captured.previous_selection.points == [[0.0, 0.0], [99.0, 77.0]]
    expected_mask = np.unpackbits(np.array([0, 0xE1, 0x43, 0x80, 0x3F], np.uint8))[:35].reshape(5, 7) * 255
    np.testing.assert_array_equal(captured.selection.mask((5, 7)), expected_mask)
    capture = ij.SessionCapture([captured], results=decoded.results, workflow=decoded.workflow, extensions=decoded.extensions)
    edit = owner.begin_edit()
    edit.set_title("new live title")
    owner.commit(edit)
    encoded = _unframe(ij.encode_session(capture, _limits()))
    assert _typed(encoded) == _typed(root)
    assert owner.snapshot().metadata["title"] == "new live title" and owner.snapshot().dirty
    assert owner.undo_steps == 1 and captured.metadata["title"] == "captured"
    detached = decoded.extensions
    detached["opaque_future_state"]["future"] = "mutated copy"
    assert _typed(decoded.extensions) == _typed(root["extensions"])


@pytest.mark.parametrize("case", _OLD["native_fixtures"], ids=lambda case: case["id"])
def test_existing_independent_marked_v2_case_migrates_exactly(case):
    marker = {"version": 1, "documents": [{"display_properties": case["display_keys"],
                                           "metadata": case["native_metadata"]}]}
    root = {"format": "imagejxx-session", "version": 2, "documents": [case["wire"]],
            "results": _OLD["results"], "workflow": _OLD["workflow"], "native_document_state": marker}
    decoded = ij.decode_session(_frame(root, version=2), _limits())
    assert decoded.source_version == 2
    owner = decoded.documents[0]
    snapshot = owner.snapshot()
    metadata = snapshot.metadata
    metadata["record_layout"] = snapshot.record_layout
    assert _typed(metadata) == _typed(case["native_metadata"])
    assert _typed(snapshot.display.properties) == _typed(case["display_properties"])
    assert _typed(decoded.results) == _typed(_OLD["results"])
    assert _typed(decoded.workflow) == _typed(_OLD["workflow"])
    actual = snapshot.dataset.numpy("ZYX")
    assert actual.dtype == np.dtype(case["pixel_dtype"]) and list(actual.shape) == case["pixel_shape"]
    assert actual.tobytes() == base64.b64decode(case["pixel_storage_base64"])
    assert [(r.t, r.c, r.z) for r in snapshot.rois] == [(None, None, r.get("slice_index")) for r in case["rois"]]
    _fresh(owner)
    output = _unframe(ij.encode_session(ij.SessionCapture([snapshot]), _limits()))
    assert output["version"] == 3 and output["documents"][0]["shape"] == [1, 1, *case["pixel_shape"], 1]


def test_malformed_wire_limits_cancellation_and_callback_failure():
    root = _root(_document())
    valid = _frame(root)
    variants = [valid[:-1], valid + b"trailing", valid + zlib.compress(b"second stream"),
                valid[:10] + struct.pack("<Q", 2**63) + valid[18:]]
    text = json.dumps(root, allow_nan=False)
    variants.append(_frame(text=text.replace('"version": 3', '"version": 3, "version": 3', 1)))
    variants.append(_frame(text=text.replace('"extensions": {"opaque_future_state":',
                                             '"extensions": {"deep":' + '['*200 + '0' + ']'*200 + ', "opaque_future_state":', 1)))
    for path, value in [(('documents', 0, 'axes'), "ZYX"), (('documents', 0, 'shape'), [2**31, 2, 3, 5, 7, 1]),
                        (('documents', 0, 'planes'), ["AAAA"]), (('documents', 0, 'storage'), "Float64BE"),
                        (('documents', 0, 'display', 't'), 1), (('version',), 4)]:
        bad = deepcopy(root)
        target = bad
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        variants.append(_frame(bad))
    selection = {"kind": "rectangle", "points": [[0.0, 0.0], [2.0, 1.0]], "width": 1.0, "attributes": {}}
    structural = deepcopy(root)
    structural["documents"][0].update(
        selection=deepcopy(selection), previous_selection=deepcopy(selection),
        rois=[{"name": "literal", "selection": deepcopy(selection), "t": None, "c": None, "z": 0, "attributes": {}}],
        fourier={"shape": [2, 3], "original_shape": None, "storage": "Complex128LE",
                 "pixels": base64.b64encode(bytes(96)).decode("ascii")})
    for path in [(), ("documents", 0), ("documents", 0, "calibration"),
                 ("documents", 0, "metadata"), ("documents", 0, "display"),
                 ("documents", 0, "selection"), ("documents", 0, "previous_selection"),
                 ("documents", 0, "rois", 0), ("documents", 0, "rois", 0, "selection"),
                 ("documents", 0, "fourier")]:
        bad = deepcopy(structural)
        target = bad
        for key in path:
            target = target[key]
        target["future_structural_key"] = {"version": 37}
        variants.append(_frame(bad))
    for data in variants:
        with pytest.raises((ValueError, OverflowError)):
            ij.decode_session(data, _limits())
    for cap, value in [("max_documents", 0), ("max_planes", 0), ("max_samples", 5),
                       ("max_nodes", 5), ("max_depth", 2), ("max_json_bytes", 1),
                       ("max_encoded_bytes", len(valid)-1), ("max_working_bytes", 1)]:
        with pytest.raises((ValueError, OverflowError)):
            ij.decode_session(valid, _limits(**{cap: value}))
    token = ij.CancellationToken()
    token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.decode_session(valid, _limits(), cancellation=token)
    class CallbackFailure(RuntimeError):
        pass
    def fail():
        raise CallbackFailure("literal checkpoint")
    with pytest.raises(CallbackFailure, match="literal checkpoint"):
        ij.decode_session(valid, _limits(), checkpoint=fail)


def test_nonfinite_policy_and_fourier_rejections():
    for word, dtype, policy in [
        (0x7FF0000000000000, "float32", "preserve_quiet_nan_float32"),
        (0x7FF0000020000000, "float32", "preserve_quiet_nan_float32"),
        (0x7FF8000020000000, "float32", "finite_only"),
        (0x7FF8000020000000, "float64", "preserve_quiet_nan_float32"),
    ]:
        value, = struct.unpack("<d", struct.pack("<Q", word))
        with pytest.raises(ValueError):
            ij.decode_session(_frame(_root(_document([0, 1, 2, 3, 4, value], dtype=dtype, policy=policy))), _limits())
    document = _document()
    document["fourier"] = {"shape": [2, 3], "original_shape": [3, 4], "storage": "Complex128LE",
                           "pixels": base64.b64encode(bytes(96)).decode("ascii")}
    with pytest.raises(ValueError):
        ij.decode_session(_frame(_root(document)), _limits())


def test_atomic_native_file_failure_cancel_and_capture_does_not_mark_newer_saved(tmp_path):
    owner = ij.Document(ij.Dataset(np.arange(6, dtype=np.uint16).reshape(2, 3), axes="YX"), "captured")
    capture = ij.SessionCapture([owner.snapshot()])
    edit = owner.begin_edit()
    edit.set_title("newer")
    owner.commit(edit)
    before = owner.snapshot()
    path = tmp_path / "session-µ.ijx"
    path.write_bytes(b"old destination\x00bytes")
    sentinel = tmp_path / "session-µ.ijx.tmp.unrelated"
    sentinel.write_bytes(b"unrelated")
    expected_files = set(tmp_path.iterdir())
    class SaveFailure(RuntimeError):
        pass
    for failing_stage in ("Atomic file write", "Atomic file replace"):
        reached = []
        def fail(stage, done, total):
            if stage == failing_stage and (stage == "Atomic file replace" or done > 0):
                reached.append((stage, done, total))
                raise SaveFailure("pre-publication failure")
        with pytest.raises(SaveFailure):
            ij.save_session(str(path), capture, _limits(), stage_progress=fail)
        assert reached and path.read_bytes() == b"old destination\x00bytes"
        assert sentinel.read_bytes() == b"unrelated" and set(tmp_path.iterdir()) == expected_files
    token = ij.CancellationToken()
    def cancel(stage, done, total):
        if stage == "Atomic file replace":
            token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.save_session(str(path), capture, _limits(), cancellation=token, stage_progress=cancel)
    assert path.read_bytes() == b"old destination\x00bytes" and set(tmp_path.iterdir()) == expected_files
    ij.save_session(str(path), capture, _limits())
    decoded = ij.load_session(str(path), _limits())
    assert decoded.documents[0].snapshot().metadata["title"] == "captured"
    assert owner.snapshot().shares_data(before) and owner.snapshot().dirty and owner.undo_steps == 1
    assert set(tmp_path.iterdir()) == expected_files and sentinel.read_bytes() == b"unrelated"
    _fresh(decoded.documents[0])
    with pytest.raises(ValueError):
        ij.load_session(str(path), _limits(max_encoded_bytes=1))
