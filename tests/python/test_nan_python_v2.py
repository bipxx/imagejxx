"""Independent typed source-policy/v2 adapters; no NaN numerical parity claim."""

import base64
from copy import copy, deepcopy
import json
import os
import struct
import zlib

import imagejxx as ij
import numpy as np
import pytest

from imagejxx.ui.model import Calibration, Document, NamedROI, Overlay, Selection
from imagejxx.ui.persistence import (
    Preferences, deserialize_document, load_session, save_session, serialize_document,
)
from imagejxx.ui import workflows


_TOKEN = "preserve_quiet_nan_float32"
_BITS32 = (0x00000000, 0x80000000, 0x3F800000, 0xC0200000, 0x00000001, 0x7FC00001)
_BITS64 = (
    0x0000000000000000, 0x8000000000000000, 0x3FF0000000000000,
    0xC004000000000000, 0x36A0000000000000, 0x7FF8000020000000,
)
_FINITE32 = _BITS32[:-1] + (0x40400000,)
_FINITE64 = _BITS64[:-1] + (0x4008000000000000,)
_BAD64 = (
    0x7FF0000000000000, 0xFFF0000000000000, 0x7FF0000020000000,
    0xFFF0000020000000, 0x7FF8000020000001,
    0x3FB999999999999A, 0x7FEFFFFFFFFFFFFF,
)
_BAD64_IDS = (
    "positive-inf", "negative-inf", "positive-snan", "negative-snan",
    "foreign-tail", "finite-not-represented", "finite-overflow",
)


def _preserve():
    return ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32


def _raw32(bits):
    return b"".join(struct.pack("=I", value) for value in bits)


def _raw64(bits):
    return b"".join(struct.pack("<Q", value) for value in bits)


def _pixels(*, nan=True, stack=False):
    values = _BITS32 if nan else _FINITE32
    array = np.array(values, dtype=np.uint32).view(np.float32).reshape(2, 3)
    return np.stack([array, array]) if stack else array


def _exact(array, bits, *, stack=False):
    assert array.dtype == np.dtype(np.float32) and array.dtype.isnative
    assert array.shape == ((2, 2, 3) if stack else (2, 3))
    assert array.tobytes(order="C") == _raw32(bits * (2 if stack else 1))


def _metadata(*, stack=False):
    # Reviewed literal legacy metadata layout; no adapter output is an oracle.
    return {
        "title": "Owned NaN adapter",
        "is_stack": stack,
        "slice_index": 0,
        "calibration": {
            "pixel_width": 1, "pixel_height": 1, "pixel_depth": 1, "unit": "px",
            "frame_interval": 1, "time_unit": "s", "origin": (0, 0, 0),
            "direction": (1, 0, 0, 0, 1, 0, 0, 0, 1), "time_origin": 0,
        },
        "selection": None,
        "previous_selection": None,
        "rois": [],
        "overlays": [],
        "display_range": None,
        "lut": "Grays",
        "labels": False,
        "custom_lut": None,
        "spectrum_shape": None,
        "view_state": {},
    }


def _record(*, opted=True, nan=True, stack=False):
    record = _metadata(stack=stack)
    if opted:
        record["pixel_sample_policy"] = _TOKEN
    record.update(
        dtype="float32", shape=[2, 2, 3] if stack else [2, 3],
        pixels=base64.b64encode(_raw64(
            (_BITS64 if nan else _FINITE64) * (2 if stack else 1)
        )).decode("ascii"),
    )
    return record


def _session_bytes(records, *, results=(), workflow=()):
    manifest = {
        "format": "imagejxx-session", "version": 2, "documents": records,
        "results": list(results), "workflow": list(workflow),
    }
    data = json.dumps(manifest, allow_nan=False).encode("utf-8")
    return b"IMAGEJXX2\n" + struct.pack(">I", len(data)) + zlib.compress(data)


def _state(document):
    return (
        document.data.tobytes(), document.data.dtype, document.data.shape,
        document.sample_policy, deepcopy(document.metadata()),
        document._identity, document._saved_identity, document.revision,
        len(document._undo), len(document._redo), document.dirty,
    )


@pytest.mark.parametrize("nan", [False, True], ids=["finite-opted", "payload-opted"])
@pytest.mark.parametrize("stack", [False, True], ids=["plane", "stack"])
def test_V01_explicit_admission_owned_and_readonly(nan, stack):
    source = _pixels(nan=nan, stack=stack)
    before = source.tobytes()
    document = Document(source, sample_policy=_preserve(), is_stack=stack)
    assert document.sample_policy == _preserve()
    with pytest.raises(AttributeError):
        document.sample_policy = ij.FloatingSamplePolicy.FINITE_ONLY
    _exact(document.data, _BITS32 if nan else _FINITE32, stack=stack)
    assert not np.shares_memory(document.data, source)
    assert not document.data.flags.writeable
    assert source.tobytes() == before


@pytest.mark.parametrize("stack", [False, True], ids=["plane", "stack"])
def test_V02_default_stays_finite_only(stack):
    with pytest.raises(ValueError):
        Document(_pixels(stack=stack), is_stack=stack)
    finite = Document(_pixels(nan=False, stack=stack), is_stack=stack)
    assert finite.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
    assert "pixel_sample_policy" not in finite.metadata()
    _exact(finite.data, _FINITE32, stack=stack)


@pytest.mark.parametrize("policy", [None, True, False, 0, 1, _TOKEN],
                         ids=["null", "true", "false", "zero", "one", "wire-token"])
def test_V03_constructor_rejects_untyped_policies(policy):
    with pytest.raises(ValueError):
        Document(_pixels(nan=False), sample_policy=policy)


@pytest.mark.parametrize("kind", ["uint8", "uint16", "float64", "rgb", "rgba"])
def test_V04_opted_storage_is_grayscale_float32(kind):
    if kind in ("rgb", "rgba"):
        pixels = np.zeros((2, 3, 3 if kind == "rgb" else 4), dtype=np.float32)
    else:
        pixels = np.zeros((2, 3), dtype=kind)
    with pytest.raises(ValueError):
        Document(pixels, sample_policy=_preserve())


@pytest.mark.parametrize("copier", ["shallow", "deep"], ids=["copy", "deepcopy"])
def test_V05_copy_and_history_keep_policy(copier):
    document = Document(_pixels(), sample_policy=_preserve())
    replacement = _pixels(nan=False)
    document.commit(replacement)
    cloned = copy(document) if copier == "shallow" else deepcopy(document)
    assert cloned is not document and cloned.sample_policy == _preserve()
    _exact(cloned.data, _FINITE32)
    assert cloned._undo[-1].sample_policy == _preserve()
    _exact(cloned._undo[-1].pixels, _BITS32)
    if copier == "deep":
        assert not np.shares_memory(cloned.data, document.data)
        assert not np.shares_memory(cloned._undo[-1].pixels, document._undo[-1].pixels)
        cloned.undo()
        _exact(cloned.data, _BITS32)
        cloned.redo()
        _exact(cloned.data, _FINITE32)
        _exact(document.data, _FINITE32)
    document.undo()
    assert document.sample_policy == _preserve()
    _exact(document.data, _BITS32)
    document.redo()
    _exact(document.data, _FINITE32)


@pytest.mark.parametrize("bits", [0x7F800000, 0x7F800001], ids=["inf", "snan"])
def test_V06_bad_replacement_is_atomic(bits):
    document = Document(_pixels(), sample_policy=_preserve())
    document.mark_saved()
    before = _state(document)
    replacement = _pixels(nan=False)
    replacement.view(np.uint32)[0, 0] = bits
    with pytest.raises(ValueError):
        document.commit(replacement)
    assert _state(document) == before


def test_V07_finite_selected_z_edit_preserves_nan_z_and_policy():
    source = np.stack([_pixels(), _pixels(nan=False)])
    document = Document(source, is_stack=True, sample_policy=_preserve())
    document.slice_index = 1
    document.mark_saved()
    document.apply(ij.math, params={"operation": "add", "value": 1})
    _exact(document.data[0], _BITS32)
    expected = np.array([[1, 1, 2], [-1.5, 1, 4]], dtype=np.float32)
    assert document.data[1].tobytes() == expected.tobytes()
    assert document.sample_policy == _preserve() and document.dirty
    assert len(document._undo) == 1 and len(document._redo) == 0
    document.undo()
    assert document.data.tobytes() == source.tobytes() and not document.dirty
    document.redo()
    assert document.data[1].tobytes() == expected.tobytes()
    assert source[0].tobytes() == _raw32(_BITS32)


@pytest.mark.parametrize("operation", ["add", "generic-gaussian", "imagej-zero"])
def test_V08_generic_processing_rejects_nan_atomically(operation):
    document = Document(_pixels(), sample_policy=_preserve())
    document.mark_saved()
    before = _state(document)
    if operation == "add":
        call = lambda frame: ij.math(frame, "add", 1)
    elif operation == "generic-gaussian":
        call = ij.gaussian_blur
    else:
        call = lambda frame: ij.gaussian_blur_imagej(frame, 0)
    with pytest.raises(ValueError):
        document.apply(call)
    assert _state(document) == before


@pytest.mark.parametrize("token", [None, True, 0, "finite_only", "unknown"],
                         ids=["null", "bool", "number", "finite-token", "unknown"])
def test_V09_metadata_cannot_change_admission(token):
    document = Document(_pixels(), sample_policy=_preserve())
    before = _state(document)
    metadata = document.metadata()
    metadata["pixel_sample_policy"] = token
    with pytest.raises(ValueError):
        document.load_metadata(metadata)
    assert _state(document) == before


def test_V10_metadata_is_derived_and_absence_never_downgrades():
    document = Document(_pixels(), sample_policy=_preserve())
    metadata = document.metadata()
    metadata.pop("pixel_sample_policy")
    document.load_metadata(metadata)
    assert document.sample_policy == _preserve()
    assert document.metadata()["pixel_sample_policy"] == _TOKEN
    finite = Document(_pixels(nan=False))
    before = _state(finite)
    metadata = finite.metadata()
    metadata["pixel_sample_policy"] = _TOKEN
    with pytest.raises(ValueError):
        finite.load_metadata(metadata)
    assert _state(finite) == before


@pytest.mark.parametrize("nan", [False, True], ids=["finite-opted", "payload-opted"])
@pytest.mark.parametrize("stack", [False, True], ids=["plane", "stack"])
def test_V11_exact_binary64_wire_and_payload_roundtrip(nan, stack):
    document = Document(_pixels(nan=nan, stack=stack), "Owned NaN adapter",
                        sample_policy=_preserve(), is_stack=stack)
    before = _state(document)
    encoded = serialize_document(document)
    expected = _record(nan=nan, stack=stack)
    assert encoded == expected
    assert base64.b64decode(encoded["pixels"]) == _raw64(
        (_BITS64 if nan else _FINITE64) * (2 if stack else 1)
    )
    restored = deserialize_document(expected)
    assert restored.sample_policy == _preserve()
    _exact(restored.data, _BITS32 if nan else _FINITE32, stack=stack)
    assert not np.shares_memory(restored.data, document.data)
    assert serialize_document(restored) == encoded
    assert _state(document) == before


@pytest.mark.parametrize("bits", _BAD64, ids=_BAD64_IDS)
def test_V12_opted_wire_rejects_nonquiet_or_nonrepresented_samples(bits):
    record = _record()
    record["pixels"] = base64.b64encode(_raw64(_BITS64[:-1] + (bits,))).decode("ascii")
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("token", [None, True, False, 0, "finite_only", "unknown", {}, []],
                         ids=["null", "true", "false", "number", "finite-token", "unknown", "object", "array"])
def test_V13_wire_policy_requires_one_exact_token(token):
    record = _record()
    record["pixel_sample_policy"] = token
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("kind", ["uint8", "uint16", "float64", "rgb", "rgba"])
def test_V14_wire_policy_rejects_other_declared_storage(kind):
    record = _record()
    if kind in ("rgb", "rgba"):
        channels = 3 if kind == "rgb" else 4
        record["shape"] = [2, 3, channels]
        record["pixels"] = base64.b64encode(_raw64(_BITS64 * channels)).decode("ascii")
    else:
        record["dtype"] = kind
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("shape", [[True, 3], [2.5, 3], [0, 3], [], [2**31, 3]],
                         ids=["boolean", "float", "zero", "empty", "int32-overflow"])
def test_V15_opted_shape_never_coerces_invalid_geometry(shape):
    record = _record()
    record["shape"] = shape
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("kind", ["bad-base64", "truncated", "extra-sample"])
def test_V16_bad_pixel_transport_rejects(kind):
    record = _record()
    if kind == "bad-base64":
        record["pixels"] = "@not-base64@"
    else:
        raw = _raw64(_BITS64)
        raw = raw[:-1] if kind == "truncated" else raw + struct.pack("<Q", 0)
        record["pixels"] = base64.b64encode(raw).decode("ascii")
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("bits", [0x7FF0000000000000, 0x7FF0000020000000, 0x7FF8000020000000],
                         ids=["inf", "snan", "qnan"])
def test_V17_absent_policy_rejects_nonfinite_wire(bits):
    record = _record(opted=False, nan=False)
    record["pixels"] = base64.b64encode(_raw64(_FINITE64[:-1] + (bits,))).decode("ascii")
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("dtype", ["uint8", "uint16", "float32", "float64"])
def test_V18_legacy_finite_session_bytes_unchanged(dtype, tmp_path):
    document = Document(np.array([[0, 1], [2, 3]], dtype=dtype), "Owned NaN adapter")
    expected = _metadata()
    expected.update(dtype=dtype, shape=[2, 2],
                    pixels=base64.b64encode(struct.pack("<4d", 0, 1, 2, 3)).decode("ascii"))
    assert serialize_document(document) == expected
    path = tmp_path / "finite.ijx"
    save_session(path, [document])
    assert path.read_bytes() == _session_bytes([expected])
    restored = load_session(path)[0][0]
    assert restored.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY
    assert "pixel_sample_policy" not in serialize_document(restored)
    assert restored.data.tobytes() == document.data.tobytes()


def _decorated(*, stack=False):
    document = Document(_pixels(stack=stack), "Owned NaN adapter", is_stack=stack,
                        sample_policy=_preserve())
    document.calibration = Calibration(
        pixel_width=.5, pixel_height=2, pixel_depth=3, unit="um", frame_interval=4,
        time_unit="ms", origin=(11, -7, 19),
        direction=(0, -1, 0, 1, 0, 0, 0, 0, 1), time_origin=-2,
    ).validate()
    document.selection = Selection("rectangle", [(0, 0), (2, 2)],
                                   attributes={"project": "retained"})
    document.previous_selection = Selection("point", [(1, 1)])
    document.rois = [NamedROI("retained", document.selection.clone(), None,
                             attributes={"group": 2, "owned": True})]
    document.overlays = [Overlay("text", points=[[1, 1]], text="retained",
                                attributes={"project": 7})]
    document.display_range = (-32, 64)
    document.view_state = {"crosshair": [1, 1, 0], "orthogonal": False}
    return document


@pytest.mark.parametrize("stack", [False, True], ids=["plane", "stack"])
def test_V19_session_reopen_resave_preserves_policy_bits_and_metadata(stack, tmp_path):
    document = _decorated(stack=stack)
    expected_metadata = document.metadata()
    expected_raw = _raw64(_BITS64 * (2 if stack else 1))
    results, workflow = [{"value": 7}], [{"operation": "add", "parameters": {"value": 1}}]
    first, second = tmp_path / "a.ijx", tmp_path / "b.ijx"
    save_session(first, [document], results=results, workflow=workflow)
    documents, actual_results, actual_workflow = load_session(first)
    restored = documents[0]
    assert restored.sample_policy == _preserve()
    _exact(restored.data, _BITS32, stack=stack)
    assert restored.metadata() == expected_metadata
    assert base64.b64decode(serialize_document(restored)["pixels"]) == expected_raw
    assert actual_results == results and actual_workflow == workflow
    save_session(second, documents, results=actual_results, workflow=actual_workflow)
    assert second.read_bytes() == first.read_bytes()


@pytest.mark.parametrize("kind", ["metadata", "results", "workflow", "spectrum"])
def test_V20_nonpixel_nonfinite_save_is_atomic(kind, tmp_path):
    document = Document(_pixels(), sample_policy=_preserve())
    results, workflow = [], []
    if kind == "metadata":
        document.view_state = {"bad": float("nan")}
    elif kind == "results":
        results = [{"bad": float("inf")}]
    elif kind == "workflow":
        workflow = [{"bad": float("nan")}]
    else:
        document.spectrum = np.array([[complex(float("nan"), 0)]], dtype=np.complex128)
    path = tmp_path / "unchanged.ijx"
    path.write_bytes(b"original destination")
    before = document.data.tobytes()
    with pytest.raises(ValueError):
        save_session(path, [document], results=results, workflow=workflow)
    assert path.read_bytes() == b"original destination"
    assert not (tmp_path / "unchanged.ijx.tmp").exists()
    assert document.data.tobytes() == before and document.sample_policy == _preserve()


@pytest.mark.parametrize("number", ["NaN", "Infinity", "1e999"],
                         ids=["nan", "inf", "exponent-overflow"])
def test_V21_nonfinite_json_never_loads_as_metadata(number, tmp_path):
    payload = json.dumps({
        "format": "imagejxx-session", "version": 2, "documents": [_record()],
        "results": [{"bad": "REPLACE"}], "workflow": [],
    }).replace('"REPLACE"', number).encode("utf-8")
    path = tmp_path / "bad.ijx"
    original = b"IMAGEJXX2\n" + struct.pack(">I", len(payload)) + zlib.compress(payload)
    path.write_bytes(original)
    with pytest.raises(ValueError):
        load_session(path)
    assert path.read_bytes() == original


def test_V23_metadata_history_preserves_source_and_policy():
    document = Document(_pixels(), sample_policy=_preserve())
    document.mark_saved()
    original = _state(document)
    calibration = Calibration(pixel_width=.5, pixel_height=2, unit="um")
    document.set_calibration(calibration)
    document.select(Selection("rectangle", [(0, 0), (2, 2)]))
    document.add_roi("kept", all_slices=True)
    assert document.sample_policy == _preserve()
    _exact(document.data, _BITS32)
    assert len(document._undo) == 2
    assert all(state.sample_policy == _preserve() for state in document._undo)
    document.undo()
    assert document.rois == [] and document.calibration.unit == "um"
    document.undo()
    assert document.calibration.unit == "px" and not document.dirty
    _exact(document.data, _BITS32)
    document.redo()
    document.redo()
    assert len(document.rois) == 1 and document.rois[0].name == "kept"
    assert document.calibration.unit == "um" and document.dirty
    _exact(document.data, _BITS32)
    assert original[0] == document.data.tobytes()


@pytest.mark.parametrize("kind", ["uint8", "float64", "rgb"])
def test_V24_wrong_storage_edit_is_atomic(kind):
    document = Document(_pixels(), sample_policy=_preserve())
    before = _state(document)
    pixels = np.zeros((2, 3, 3), np.float32) if kind == "rgb" else np.zeros((2, 3), kind)
    with pytest.raises(ValueError):
        document.commit(pixels)
    assert _state(document) == before


def test_V25_commit_has_no_policy_override():
    document = Document(_pixels(), sample_policy=_preserve())
    before = _state(document)
    with pytest.raises(TypeError):
        document.commit(_pixels(nan=False), sample_policy=ij.FloatingSamplePolicy.FINITE_ONLY)
    assert _state(document) == before


def test_V26_legacy_float32_signed_zero_and_subnormal_wire_unchanged(tmp_path):
    document = Document(_pixels(nan=False), "Owned NaN adapter")
    expected = _record(opted=False, nan=False)
    assert serialize_document(document) == expected
    path = tmp_path / "finite.ijx"
    save_session(path, [document])
    assert path.read_bytes() == _session_bytes([expected])
    restored = load_session(path)[0][0]
    _exact(restored.data, _FINITE32)
    assert restored.sample_policy == ij.FloatingSamplePolicy.FINITE_ONLY


@pytest.mark.parametrize("is_stack", [None, 1, "false"], ids=["null", "number", "string"])
def test_V27_opted_wire_stack_flag_is_boolean(is_stack):
    record = _record()
    record["is_stack"] = is_stack
    with pytest.raises(ValueError):
        deserialize_document(record)


@pytest.mark.parametrize("nan", [False, True], ids=["finite-opted", "payload-opted"])
def test_V28_empty_workflow_preserves_immutable_source_policy(nan):
    document = Document(_pixels(nan=nan), sample_policy=_preserve())
    before = _state(document)
    result = workflows.run(document, [])
    _exact(result, _BITS32 if nan else _FINITE32)
    assert not np.shares_memory(result, document.data)
    assert _state(document) == before


def test_V29_numeric_workflow_current_z_keeps_unselected_nan_plane():
    document = Document(np.stack([_pixels(), _pixels(nan=False)]),
                        is_stack=True, sample_policy=_preserve())
    document.slice_index = 1
    before = _state(document)
    steps = [{"operation": "add", "parameters": {"value": 1}}]
    frozen = deepcopy(steps)
    progress = []
    result = workflows.run(document, steps,
                           progress=lambda done, total: progress.append((done, total)))
    _exact(result[0], _BITS32)
    expected = np.array([[1, 1, 2], [-1.5, 1, 4]], dtype=np.float32)
    assert result[1].tobytes() == expected.tobytes()
    assert progress == [(1, 1)] and steps == frozen
    assert _state(document) == before


@pytest.mark.parametrize("z", [0, 1], ids=["nan-target-rejects", "finite-target-keeps-policy"])
def test_V30_typed_workflow_transports_policy_without_weakening_kernel(z):
    recorded_owner = ij.Document(ij.Dataset(_pixels(nan=False), axes="YX"))
    steps = [ij.resolve_gaussian_command(
        recorded_owner.snapshot(), options=ij.GaussianBlurCommandOptions(0, False),
    ).record()]
    frozen = deepcopy(steps)
    document = Document(np.stack([_pixels(), _pixels(nan=False)]),
                        is_stack=True, sample_policy=_preserve())
    document.slice_index = z
    before = _state(document)
    request = ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE)
    if z == 0:
        with pytest.raises((ValueError, RuntimeError)):
            workflows.run(document, steps, gaussian_rebind=request)
    else:
        result = workflows.run(document, steps, gaussian_rebind=request)
        assert result.dtype == np.dtype(np.float32)
        assert result.tobytes() == document.data.tobytes()
        assert not np.shares_memory(result, document.data)
    assert steps == frozen and _state(document) == before


@pytest.mark.skipif(os.environ.get("IMAGEJXX_RENDER_TESTS") != "1",
                    reason="Set IMAGEJXX_RENDER_TESTS=1 for required installed native UI adapters")
@pytest.mark.parametrize("route", ["detached", "native-owner"])
@pytest.mark.parametrize("stack", [False, True], ids=["plane", "stack"])
def test_V22_installed_editor_roundtrip_preserves_policy(route, stack, tmp_path):
    from imagejxx.ui.app import Editor

    editor = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    original = _decorated(stack=stack)
    expected = original.data.tobytes()
    if route == "detached":
        editor.add_document(original)
    else:
        dataset = ij.Dataset(original.data, axes="ZYX" if stack else "YX",
                             sample_policy=_preserve())
        owner = ij.Document(dataset)
        editor.add_native_document(owner)
    try:
        active = editor.live_document()
        actual_native = active.native_snapshot().dataset
        assert actual_native.sample_policy == _preserve()
        assert actual_native.numpy("ZYX" if stack else "YX").tobytes() == expected
        detached = editor.document_snapshot()
        assert detached.sample_policy == _preserve()
        assert detached.data.tobytes() == expected
        assert len(editor.documents) == 1
        assert editor.documents[0].sample_policy == _preserve()
        assert editor.documents[0].data.tobytes() == expected
        if route == "detached":
            assert detached.metadata()["rois"] == original.metadata()["rois"]
            assert detached.metadata()["overlays"] == original.metadata()["overlays"]
            assert detached.metadata()["calibration"] == original.metadata()["calibration"]
        native_session, python_session = tmp_path / "n.ijx", tmp_path / "p.ijx"
        editor.action("save_workspace", path=str(native_session))
        extensions = {}
        documents, results, workflow = load_session(native_session, extensions=extensions)
        assert extensions == {"imagejxx_workspace": {"version": 1, "active_index": 0}}
        assert extensions == ij.load_session(str(native_session)).extensions
        assert documents[0].sample_policy == _preserve()
        assert documents[0].data.tobytes() == expected
        save_session(python_session, documents, results=results, workflow=workflow)
        editor.close_document(active.id, discard=True)
        editor.open_file(python_session)
        restored = editor.document_snapshot()
        assert restored.sample_policy == _preserve() and restored.data.tobytes() == expected
        assert editor.live_document().native_snapshot().dataset.sample_policy == _preserve()
        assert original.data.tobytes() == expected
    finally:
        for info in list(editor.document_infos):
            editor.close_document(info["id"], discard=True)
