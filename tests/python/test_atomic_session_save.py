"""Independent owned-temporary publication checks; preparation has no runtime credit."""
import base64
from dataclasses import fields, is_dataclass
import json
from pathlib import Path
import struct
import tempfile
import threading
import zlib

import imagejxx as ij
from imagejxx.ui.model import Document
from imagejxx.ui import persistence
import numpy as np
import pytest

_REAL_TEMP = tempfile.NamedTemporaryFile
_REAL_REPLACE, _REAL_UNLINK = Path.replace, Path.unlink


class SaveFailure(OSError):
    pass


def _freeze(value):
    if isinstance(value, np.ndarray):
        return (value.dtype.str, value.shape, value.tobytes())
    if type(value) is float:
        return ("float64", struct.pack("<d", value))
    if isinstance(value, Path):
        return ("path", str(value))
    if isinstance(value, dict):
        return tuple((key, _freeze(item)) for key, item in value.items())
    if isinstance(value, (list, tuple)):
        return (type(value).__name__, tuple(_freeze(item) for item in value))
    if is_dataclass(value):
        return (type(value).__name__, tuple((item.name, _freeze(getattr(value, item.name))) for item in fields(value)))
    if type(value) in (str, int, bool, type(None)):
        return (type(value).__name__, value)
    return (type(value).__name__, repr(value))


def _state(document, results, workflow):
    return _freeze((document.__dict__, document.metadata(), results, workflow))


def _fixture(tmp_path):
    pixels = np.array([[0, 1], [2, 3]], dtype=np.uint8)
    document = Document(pixels, "Original owner")
    document.mark_saved(tmp_path / "original.tif")
    document.commit(pixels + 1)
    document.commit(pixels + 2)
    document.undo()
    document.display_range = (-1, 9)
    document.zoom = 1.75
    document.view_state = {"crosshair": [1, 1, 0], "owned": {"pan": [3.0, -4.0]}}
    results = [{"row": 7, "opaque": {"flags": [True, 0, -0.0]}}]
    workflow = [{"operation": "add", "parameters": {"value": 1}, "opaque": "retained"}]
    destination = tmp_path / "target.ijx"
    destination.write_bytes(b"original destination\x00bytes")
    sentinel = Path(str(destination) + ".tmp")
    sentinel.write_bytes(b"unrelated existing .tmp\x00sentinel")
    return document, results, workflow, destination, sentinel


def _assert_unchanged(fixture, before):
    document, results, workflow, destination, sentinel = fixture
    assert _state(document, results, workflow) == before
    assert destination.read_bytes() == b"original destination\x00bytes"
    assert sentinel.read_bytes() == b"unrelated existing .tmp\x00sentinel"


class _Stream:
    def __init__(self, raw, phase=None, error=None, close_error=None):
        self.raw, self.phase, self.error, self.close_error = raw, phase, error, close_error
        self.close_calls = 0
        self.events = []

    @property
    def name(self):
        return self.raw.name

    @property
    def closed(self):
        return self.raw.closed

    def write(self, data):
        self.events.append("write")
        if self.phase == "write":
            self.raw.write(data[:11])
            raise self.error
        if self.phase == "short":
            return self.raw.write(data[:-1])
        return self.raw.write(data)

    def flush(self):
        self.events.append("flush")
        self.raw.flush()
        if self.phase == "flush":
            raise self.error

    def close(self):
        self.events.append("close")
        self.close_calls += 1
        self.raw.close()
        if self.close_error is not None:
            raise self.close_error
        if self.phase == "close" and self.close_calls == 1:
            raise self.error


def _factory(captured, phase=None, error=None, close_error=None):
    def create(*args, **kwargs):
        assert not args and kwargs["mode"] == "wb" and kwargs["delete"] is False
        assert kwargs["prefix"] == ".ijxs-"
        raw = _REAL_TEMP(*args, **kwargs)
        wrapped = _Stream(raw, phase, error, close_error)
        captured.append(wrapped)
        return wrapped
    return create


def _assert_owned(captured, directory, *, removed=True):
    for stream in captured:
        file = Path(stream.name)
        assert file.parent == directory and file.name.startswith(".ijxs-")
        assert stream.closed
        assert file.exists() is (not removed)


@pytest.mark.parametrize("phase", ["serialize", "json", "compression", "create"])
def test_P04a_failure_before_or_at_temp_creation(phase, tmp_path, monkeypatch):
    fixture = _fixture(tmp_path)
    document, results, workflow, destination, _ = fixture
    before, captured, calls = _state(document, results, workflow), [], []
    primary = SaveFailure(phase)

    def fail(*args, **kwargs):
        raise primary

    def create(*args, **kwargs):
        calls.append(kwargs)
        if phase == "create":
            raise primary
        return _factory(captured)(*args, **kwargs)

    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", create)
        if phase == "serialize":
            patch.setattr(persistence, "serialize_document", fail)
        elif phase == "json":
            patch.setattr(persistence.json, "dumps", fail)
        elif phase == "compression":
            patch.setattr(persistence.zlib, "compress", fail)
        with pytest.raises(SaveFailure) as caught:
            persistence.save_session(destination, [document], results=results, workflow=workflow)
    assert caught.value is primary
    assert len(calls) == (1 if phase == "create" else 0) and not captured
    _assert_unchanged(fixture, before)
    assert not list(tmp_path.glob(".ijxs-*"))


@pytest.mark.parametrize("phase", ["write", "flush", "close", "replace"])
def test_P04a_owned_temp_failures_are_atomic(phase, tmp_path, monkeypatch):
    fixture = _fixture(tmp_path)
    document, results, workflow, destination, _ = fixture
    before, captured = _state(document, results, workflow), []
    primary = SaveFailure(phase)

    def fail_replace(file, target):
        assert file == Path(captured[0].name) and Path(target) == destination
        assert captured[0].closed
        assert phase == "replace", "Publication attempted before the failing stage completed"
        raise primary

    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", _factory(captured, phase, primary))
        patch.setattr(Path, "replace", fail_replace)
        with pytest.raises(SaveFailure) as caught:
            persistence.save_session(destination, [document], results=results, workflow=workflow)
    assert caught.value is primary and len(captured) == 1
    _assert_owned(captured, tmp_path)
    _assert_unchanged(fixture, before)
    assert not list(tmp_path.glob(".ijxs-*"))


def test_P04a_short_write_rejects_partial_payload(tmp_path, monkeypatch):
    fixture = _fixture(tmp_path)
    document, results, workflow, destination, _ = fixture
    before, captured = _state(document, results, workflow), []

    def forbid_replace(file, target):
        raise AssertionError("Short write must never reach publication")

    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", _factory(captured, "short"))
        patch.setattr(Path, "replace", forbid_replace)
        with pytest.raises(OSError):
            persistence.save_session(destination, [document], results=results, workflow=workflow)
    assert len(captured) == 1
    _assert_owned(captured, tmp_path)
    _assert_unchanged(fixture, before)


@pytest.mark.parametrize("phase", ["write", "replace"])
def test_P04a_cleanup_error_keeps_primary(phase, tmp_path, monkeypatch):
    fixture = _fixture(tmp_path)
    document, results, workflow, destination, _ = fixture
    before, captured = _state(document, results, workflow), []
    primary, cleanup = SaveFailure(phase), SaveFailure("owned unlink cleanup")

    def fail_replace(file, target):
        assert file == Path(captured[0].name) and Path(target) == destination
        raise primary

    def fail_unlink(file, *args, **kwargs):
        assert file == Path(captured[0].name)
        raise cleanup

    try:
        with monkeypatch.context() as patch:
            patch.setattr(persistence.tempfile, "NamedTemporaryFile", _factory(captured, phase, primary))
            patch.setattr(Path, "unlink", fail_unlink)
            if phase == "replace":
                patch.setattr(Path, "replace", fail_replace)
            with pytest.raises(SaveFailure) as caught:
                persistence.save_session(destination, [document], results=results, workflow=workflow)
        assert caught.value is primary and caught.value.__cause__ is cleanup
        assert len(captured) == 1
        _assert_owned(captured, tmp_path, removed=False)
        _assert_unchanged(fixture, before)
    finally:
        for stream in captured:
            stream.raw.close()
            _REAL_UNLINK(Path(stream.name), missing_ok=True)


def test_P04a_close_cleanup_error_keeps_write_primary(tmp_path, monkeypatch):
    fixture = _fixture(tmp_path)
    document, results, workflow, destination, _ = fixture
    before, captured = _state(document, results, workflow), []
    primary, cleanup = SaveFailure("partial write"), SaveFailure("owned close cleanup")
    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", _factory(captured, "write", primary, cleanup))
        with pytest.raises(SaveFailure) as caught:
            persistence.save_session(destination, [document], results=results, workflow=workflow)
    assert caught.value is primary and caught.value.__cause__ is cleanup
    assert len(captured) == 1
    _assert_owned(captured, tmp_path)
    _assert_unchanged(fixture, before)


def test_P04a_no_implicit_parent_creation(tmp_path):
    fixture = _fixture(tmp_path)
    document, results, workflow, _, _ = fixture
    before = _state(document, results, workflow)
    missing = tmp_path / "absent-parent"
    with pytest.raises(FileNotFoundError):
        persistence.save_session(missing / "new.ijx", [document], results=results, workflow=workflow)
    assert not missing.exists()
    _assert_unchanged(fixture, before)


def test_P04a_size_limit_preflights_before_temp_creation(tmp_path, monkeypatch):
    fixture = _fixture(tmp_path)
    document, results, workflow, destination, _ = fixture
    before = _state(document, results, workflow)

    class OversizedBytes(bytes):
        def __len__(self):
            return 1024**3 + 1

    class EncodedManifest:
        def encode(self, encoding):
            assert encoding == "utf-8"
            return OversizedBytes(b"{}")

    def forbid_temp(*args, **kwargs):
        raise AssertionError("Oversized session must fail before temporary acquisition")

    with monkeypatch.context() as patch:
        patch.setattr(persistence.json, "dumps", lambda *args, **kwargs: EncodedManifest())
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", forbid_temp)
        with pytest.raises(ValueError, match="1 GiB interchange limit"):
            persistence.save_session(destination, [document], results=results, workflow=workflow)
    _assert_unchanged(fixture, before)
    assert not list(tmp_path.glob(".ijxs-*"))


def _record(dtype, shape, payload, *, opted=False, title="Owned NaN adapter"):
    # Literal legacy layout; no serializer/model output supplies an oracle.
    result = {
        "title": title, "is_stack": False, "slice_index": 0,
        "calibration": {
            "pixel_width": 1, "pixel_height": 1, "pixel_depth": 1, "unit": "px",
            "frame_interval": 1, "time_unit": "s", "origin": (0, 0, 0),
            "direction": (1, 0, 0, 0, 1, 0, 0, 0, 1), "time_origin": 0,
        },
        "selection": None, "previous_selection": None, "rois": [], "overlays": [],
        "display_range": None, "lut": "Grays", "labels": False,
        "custom_lut": None, "spectrum_shape": None, "view_state": {},
    }
    if opted:
        result["pixel_sample_policy"] = "preserve_quiet_nan_float32"
    result.update(dtype=dtype, shape=shape, pixels=base64.b64encode(payload).decode("ascii"))
    return result


def _wire(records, results=(), workflow=()):
    content = json.dumps({"format": "imagejxx-session", "version": 2, "documents": records,
                          "results": list(results), "workflow": list(workflow)}, allow_nan=False).encode("utf8")
    return b"IMAGEJXX2\n" + struct.pack(">I", len(content)) + zlib.compress(content)


def _successful_save(tmp_path, monkeypatch, document, expected):
    destination = tmp_path / "success.ijx"
    destination.write_bytes(b"previous destination")
    sentinel = Path(str(destination) + ".tmp")
    sentinel.write_bytes(b"unrelated successful-save sentinel")
    results, workflow, captured, replacements = [], [], [], []
    before = _state(document, results, workflow)

    def replace(file, target):
        assert len(captured) == 1 and file == Path(captured[0].name)
        assert captured[0].closed and file.read_bytes() == expected
        assert captured[0].events == ["write", "flush", "close"]
        replacements.append((file, Path(target)))
        return _REAL_REPLACE(file, target)

    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", _factory(captured))
        patch.setattr(Path, "replace", replace)
        persistence.save_session(destination, [document])
    assert replacements == [(Path(captured[0].name), destination)]
    assert destination.read_bytes() == expected
    assert sentinel.read_bytes() == b"unrelated successful-save sentinel"
    assert _state(document, results, workflow) == before
    _assert_owned(captured, tmp_path)
    restored, actual_results, actual_workflow = persistence.load_session(destination)
    assert len(restored) == 1 and actual_results == actual_workflow == []
    assert restored[0].data.tobytes() == document.data.tobytes()
    assert restored[0].sample_policy == document.sample_policy


@pytest.mark.parametrize("dtype", ["uint8", "uint16", "float32", "float64"])
def test_P04a_V18_exact_legacy_bytes(dtype, tmp_path, monkeypatch):
    document = Document(np.array([[0, 1], [2, 3]], dtype=dtype), "Owned NaN adapter")
    expected = _wire([_record(dtype, [2, 2], struct.pack("<4d", 0, 1, 2, 3))])
    _successful_save(tmp_path, monkeypatch, document, expected)


@pytest.mark.parametrize("opted", [False, True], ids=["finite-default", "quiet-nan-opted"])
def test_P04a_V26_exact_float32_wire(opted, tmp_path, monkeypatch):
    bits32 = (0, 0x80000000, 0x3F800000, 0xC0200000, 1, 0x7FC00001 if opted else 0x40400000)
    bits64 = (0, 0x8000000000000000, 0x3FF0000000000000, 0xC004000000000000,
              0x36A0000000000000, 0x7FF8000020000000 if opted else 0x4008000000000000)
    pixels = np.array(bits32, dtype=np.uint32).view(np.float32).reshape(2, 3)
    policy = ij.FloatingSamplePolicy.PRESERVE_QUIET_NAN_FLOAT32 if opted else ij.FloatingSamplePolicy.FINITE_ONLY
    document = Document(pixels, "Owned NaN adapter", sample_policy=policy)
    payload = b"".join(struct.pack("<Q", bits) for bits in bits64)
    expected = _wire([_record("float32", [2, 3], payload, opted=opted)])
    _successful_save(tmp_path, monkeypatch, document, expected)


def test_P04a_reentrant_saves_own_distinct_temps(tmp_path, monkeypatch):
    destination = tmp_path / "shared.ijx"
    destination.write_bytes(b"original complete file")
    sentinel = Path(str(destination) + ".tmp")
    sentinel.write_bytes(b"external reentrant sentinel")
    outer = Document(np.array([[0, 1], [2, 3]], dtype=np.uint8), "Outer")
    inner = Document(np.array([[4, 5], [6, 7]], dtype=np.uint8), "Inner")
    expected_outer = _wire([_record("uint8", [2, 2], struct.pack("<4d", 0, 1, 2, 3), title="Outer")])
    expected_inner = _wire([_record("uint8", [2, 2], struct.pack("<4d", 4, 5, 6, 7), title="Inner")])
    before = _state(outer, [], []), _state(inner, [], [])
    captured, entered = [], False

    def create(*args, **kwargs):
        nonlocal entered
        stream = _factory(captured)(*args, **kwargs)
        if not entered:
            entered = True
            persistence.save_session(destination, [inner])
            assert destination.read_bytes() == expected_inner
            assert Path(stream.name).read_bytes() == b"" and not stream.closed
        return stream

    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", create)
        persistence.save_session(destination, [outer])
    assert len(captured) == len({stream.name for stream in captured}) == 2
    _assert_owned(captured, tmp_path)
    assert destination.read_bytes() == expected_outer
    assert sentinel.read_bytes() == b"external reentrant sentinel"
    assert (_state(outer, [], []), _state(inner, [], [])) == before


def test_P04a_concurrent_saves_publish_complete_payloads(tmp_path, monkeypatch):
    destination = tmp_path / "shared.ijx"
    destination.write_bytes(b"original complete file")
    sentinel = Path(str(destination) + ".tmp")
    sentinel.write_bytes(b"external concurrent sentinel")
    documents = [Document(np.array([[k, k + 1]], dtype=np.uint8), "Owner" + str(k)) for k in (0, 7)]
    expected = [_wire([_record("uint8", [1, 2], struct.pack("<2d", k, k + 1), title="Owner" + str(k))]) for k in (0, 7)]
    before = [_state(document, [], []) for document in documents]
    captured, errors, lock = [], [], threading.Lock()
    barrier = threading.Barrier(2, timeout=3)

    def create(*args, **kwargs):
        with lock:
            stream = _factory(captured)(*args, **kwargs)
        try:
            barrier.wait()
        except BaseException:
            stream.raw.close(); _REAL_UNLINK(Path(stream.name), missing_ok=True)
            raise
        return stream

    def save(document):
        try:
            persistence.save_session(destination, [document])
        except BaseException as error:
            with lock:
                errors.append(error)

    threads = [threading.Thread(target=save, args=(document,)) for document in documents]
    with monkeypatch.context() as patch:
        patch.setattr(persistence.tempfile, "NamedTemporaryFile", create)
        try:
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=5)
        finally:
            barrier.abort()
            for thread in threads:
                if thread.ident is not None:
                    thread.join(timeout=5)
    assert not any(thread.is_alive() for thread in threads) and not errors
    assert len(captured) == len({stream.name for stream in captured}) == 2
    _assert_owned(captured, tmp_path)
    assert destination.read_bytes() in expected
    assert sentinel.read_bytes() == b"external concurrent sentinel"
    assert [_state(document, [], []) for document in documents] == before
