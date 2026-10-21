"""Explicit rank API qualification: public callbacks, GIL and scientific V3.

No processing oracle, patched kernel, private provider or optional skip. This
file is intentionally outside default tests/python collection. Its five cases
require the raw-observation output environment and exact native product pins
from the supervising qualification run. All expected pixels are owned literals.
"""

from copy import deepcopy
import gc
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import sysconfig
import threading
import time
import weakref
import zlib

import imagejxx as ij
import numpy as np
import pytest


_helper_path = Path(__file__).resolve().parents[1] / "python" / "rank_observations.py"
_spec = importlib.util.spec_from_file_location("_rank_api_raw_observations", _helper_path)
_helper = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_helper)
RawCase = _helper.RawCase

SPIKE = ((0,0,0,0,0), (0,0,0,0,0), (0,0,80,0,0),
         (0,0,0,0,0), (0,0,0,0,0))
MEAN = ((0,0,0,0,0), (0,0,16,0,0), (0,16,16,16,0),
        (0,0,16,0,0), (0,0,0,0,0))


@pytest.fixture
def raw(request):
    assert os.environ.get(_helper.ENVIRONMENT), "Qualification requires a fresh raw-observation directory"
    return RawCase(request.node.name)


@pytest.fixture
def native():
    app = ij.Application()
    owner = ij.Document(ij.Dataset(np.array(SPIKE, np.uint16), axes="YX"), "Owned Python rank callback")
    app.adopt(owner)
    scheduler = app.job_scheduler
    scheduler.set_limits(ij.JobLimits(workers=1, memory_budget=0, capacity=8))
    try:
        yield app, owner, scheduler
    finally:
        scheduler.shutdown()


def command(owner, method="mean", radius=0):
    return ij.resolve_rank_command(owner.snapshot(),
        ij.TargetRequest(roi=ij.RoiPolicy.WHOLE_PLANE),
        ij.RankFilterCommandOptions(method, radius))


def unchanged(owner, before, history):
    after = owner.snapshot()
    assert after.shares_data(before) and after.revisions.matches(before.revisions, ij.Change.ALL)
    assert after.dirty == before.dirty
    assert (owner.undo_steps, owner.redo_steps) == history
    assert after.dataset.numpy("TCZYXS").tobytes() == before.dataset.numpy("TCZYXS").tobytes()


def zero_usage(scheduler):
    usage = _helper.usage_value(scheduler)
    assert usage == dict.fromkeys(_helper.USAGE_FIELDS, 0)
    return usage


def test_rao01_rank_progress_exception_releases_callback_and_preserves_source(native, raw):
    app, owner, scheduler = native
    before, history = owner.snapshot(), (owner.undo_steps, owner.redo_steps)
    resolved = command(owner)
    calls, holder = [], []

    class OwnedFailure(RuntimeError):
        pass

    class Progress:
        def __call__(self, completed, total):
            calls.append([completed, total])
            assert app.snapshot(owner.id).shares_data(before)
            raise OwnedFailure("owned rank progress failure")

    holder.append(Progress())
    reference = weakref.ref(holder[0])
    raw.owner("source", owner)

    def invoke():
        try:
            app.prepare("rank_filter_imagej", resolved.target, resolved.parameters(), progress=holder.pop())
        except OwnedFailure as error:
            return {"type": type(error).__name__, "message": str(error)}
        pytest.fail("Actual rank preparation swallowed its Python progress failure")

    failure = invoke()
    gc.collect()
    assert calls == [[0, 1]] and reference() is None
    unchanged(owner, before, history)
    usage = zero_usage(scheduler)
    raw.json("callback.calls", calls)
    raw.json("callback.error", failure)
    raw.scalar("callback.alive_after_return", reference() is not None)
    # Synchronous prepare does not admit a scheduler job. This actual zero
    # reading is not a claim that a charged async callback lease was released.
    raw.json("synchronous_prepare.scheduler_usage", usage)
    raw.scalar("python.callback.reserved_bytes", usage["reserved_bytes"])
    raw.owner("after_failure", owner)
    raw.finish()


def test_rao02_rank_progress_captures_parameters_and_releases_accepted_callback(native, raw):
    app, owner, scheduler = native
    before, history = owner.snapshot(), (owner.undo_steps, owner.redo_steps)
    resolved = command(owner)
    parameters = resolved.parameters()
    calls, alive, holder = [], [], []

    class Progress:
        def __call__(self, completed, total):
            calls.append([completed, total])
            assert app.snapshot(owner.id).shares_data(before)
            assert owner.snapshot().revisions.matches(before.revisions, ij.Change.ALL)
            if completed == 0:
                parameters["method"], parameters["radius"] = "maximum", 1000.0
                holder.clear()
            else:
                alive.append(reference() is self)

    holder.append(Progress())
    reference = weakref.ref(holder[0])
    raw.owner("source", owner)
    prepared = app.prepare("rank_filter_imagej", resolved.target, parameters, progress=holder[0])
    gc.collect()
    assert calls[0] == [0, 1] and calls[-1] == [1, 1]
    assert alive and all(alive) and reference() is None
    np.testing.assert_array_equal(prepared.preview.dataset.plane(), np.array(MEAN, np.uint16))
    unchanged(owner, before, history)
    result = app.publish(prepared)
    assert result.document is owner and owner.undo_steps == history[0] + 1
    np.testing.assert_array_equal(owner.snapshot().dataset.plane(), np.array(MEAN, np.uint16))
    raw.json("callback.calls", calls)
    raw.json("callback.alive_at_completion", alive)
    raw.scalar("callback.alive_after_return", reference() is not None)
    raw.json("mutated.caller_parameters", parameters)
    raw.json("captured.command_record", resolved.record())
    raw.owner("published", owner)
    prepared = result = None
    gc.collect()
    raw.json("terminal.scheduler_usage", zero_usage(scheduler))
    raw.finish()


def test_rao03_rank_releases_gil_for_cross_thread_cancel_after_initial_progress(native, raw):
    assert sys.implementation.name == "cpython", "This controlled handoff qualifies CPython only"
    runtime_check = getattr(sys, "_is_gil_enabled", None)
    runtime_enabled = runtime_check() if callable(runtime_check) else None
    build_disabled = sysconfig.get_config_var("Py_GIL_DISABLED")
    suffix = sysconfig.get_config_var("EXT_SUFFIX")
    profile = {
        "implementation": sys.implementation.name, "version": sys.version,
        "version_major": sys.version_info.major, "version_minor": sys.version_info.minor,
        "version_micro": sys.version_info.micro, "api_version": sys.api_version,
        "cache_tag": sys.implementation.cache_tag, "abiflags": getattr(sys, "abiflags", None),
        "soabi": sysconfig.get_config_var("SOABI"), "extension_suffix": suffix,
        "py_gil_disabled": build_disabled, "flags_gil": getattr(sys.flags, "gil", None),
        "runtime_check_available": callable(runtime_check), "runtime_enabled": runtime_enabled,
        "executable": sys.executable,
    }
    assert build_disabled in (None, 0, "0"), "Free-threaded builds are outside this GIL qualification"
    assert isinstance(suffix, str) and suffix
    assert str(Path(ij._native.__file__).resolve()).endswith(suffix)
    if callable(runtime_check):
        assert type(runtime_enabled) is bool and runtime_enabled, "Qualification requires an enabled GIL"
    else:
        # CPython3.12 predates optional free-threaded execution. Only this exact
        # version/cache ABI may use build provenance in place of the newer
        # runtime hook; an arbitrary CPython name is insufficient.
        assert runtime_check is None
        assert sys.version_info[:2] == (3, 12)
        assert sys.implementation.cache_tag == "cpython-312" and "312" in suffix
    app, _, scheduler = native
    pixels = np.zeros((7, 9), np.uint16)
    pixels[3, 4] = 80
    owner = ij.Document(ij.Dataset(pixels, axes="YX"), "Owned radius1000 cancellation")
    app.adopt(owner)
    before, history = owner.snapshot(), (owner.undo_steps, owner.redo_steps)
    resolved = command(owner, radius=1000)
    token = ij.CancellationToken()
    ready, enter = threading.Event(), threading.Event()
    calls, handoffs, failures = [], [], []
    flags = {"callback_active": False, "native_returned": False}

    def cancel_from_python():
        ready.set()
        if not enter.wait(5):
            failures.append("Native rank progress did not open the cancellation gate")
            return
        handoffs.append({"callback_active": flags["callback_active"],
                         "native_returned": flags["native_returned"],
                         "thread_id": threading.get_ident()})
        token.cancel()

    def progress(completed, total):
        flags["callback_active"] = True
        calls.append([completed, total])
        if completed == 0:
            enter.set()
        flags["callback_active"] = False

    worker = threading.Thread(target=cancel_from_python, name="owned rank GIL canceller", daemon=True)
    worker.start()
    assert ready.wait(5)
    previous = sys.getswitchinterval()
    canceled = False
    started = time.monotonic()
    raw.owner("source", owner)
    try:
        # No sleep/wait inside the callback: those would release the GIL
        # themselves and would not establish this native binding's handoff.
        # Suppress periodic Python preemption across the tiny callback return;
        # reject a handoff inside that callback or after native return. Initial
        # progress precedes the plane-loop checkpoint and kernel. This proves
        # neither positive rank work nor entry into the first neighborhood;
        # those cancellation gates remain separate, unproven obligations.
        sys.setswitchinterval(10)
        try:
            app.prepare("rank_filter_imagej", resolved.target, resolved.parameters(),
                        cancellation=token, progress=progress)
        except ij.OperationCancelled:
            canceled = True
        finally:
            flags["native_returned"] = True
    finally:
        sys.setswitchinterval(previous)
        enter.set()
        worker.join(5)
    assert not worker.is_alive() and not failures
    assert time.monotonic() - started < 10
    assert canceled and token.canceled
    assert len(handoffs) == 1 and handoffs[0]["callback_active"] is False
    assert handoffs[0]["native_returned"] is False
    assert handoffs[0]["thread_id"] != threading.get_ident()
    assert calls == [[0, 1]]
    unchanged(owner, before, history)
    raw.json("rank.progress_calls", calls)
    for name, value in profile.items():
        raw.scalar("runtime.gil_profile." + name, value)
    raw.json("gil.actual_handoffs", handoffs)
    raw.scalar("python.gil.thread_progress", len(handoffs))
    raw.scalar("rank.cancellation_observed", canceled)
    raw.scalar("rank.token_canceled", token.canceled)
    raw.json("terminal.scheduler_usage", zero_usage(scheduler))
    raw.owner("after_cancel", owner)
    raw.finish()


def test_rao04_scientific_native_v3_save_open_preserves_bits_and_requires_full_rebind(tmp_path, raw):
    calibration = ij.SpatialCalibration(spacing=[.5, 1.25, 2.5], origin=[11, -7, 19],
        unit="um", frame_interval=.75, time_origin=2.25, time_unit="s")
    pixels = np.empty((2, 2, 3, 5, 5), np.uint16)
    for t in range(2):
        for c in range(2):
            for z in range(3):
                pixels[t, c, z] = np.array(SPIKE, np.uint16) + 10*t + 20*c + 30*z
    owner = ij.Document(ij.Dataset(pixels, axes="TCZYX", calibration=calibration), "Owned scientific rank V3")
    edit = owner.begin_edit()
    edit.set_property("owned_rank_source", {"retained": True, "values": [3, 1, 4]})
    edit.select(ij.Selection("rectangle", [[1, 1], [4, 4]]))
    edit.set_previous_selection(ij.Selection("line", [[0, 0], [1, 1]]))
    edit.set_rois([ij.ROI("positioned retained", ij.Selection("rectangle", [[1, 1], [4, 4]]),
                          t=1, c=None, z=2, attributes={"retained": True})])
    edit.set_overlays([{"kind": "rectangle", "points": [[0, 0], [1, 1]],
                        "t": 0, "c": 0, "z": 2, "version": 37, "opaque": [3, 1, 4]}])
    owner.commit(edit)
    view = owner.snapshot().display
    view.t, view.c, view.crosshair = 1, 1, [2, 2, 1]
    view.orthogonal, view.zoom = True, 1.5
    owner.set_display(view)
    owner.mark_saved()
    app = ij.Application()
    app.adopt(owner)
    resolved = command(owner, "median", 0)
    record = resolved.record()
    frozen = deepcopy(record)
    raw.owner("source", owner)
    prepared = app.prepare("rank_filter_imagej", resolved.target, resolved.parameters())
    app.publish(prepared)
    wanted = pixels.copy()
    wanted[1, 1, 1] = np.full((5, 5), 60, np.uint16)
    saved = owner.snapshot()
    np.testing.assert_array_equal(saved.dataset.numpy("TCZYX"), wanted)
    limits = ij.SessionLimits()
    limits.max_documents, limits.max_planes, limits.max_samples = 2, 16, 1024
    limits.max_json_bytes = limits.max_encoded_bytes = 2 * 1024 * 1024
    limits.max_working_bytes = 16 * 1024 * 1024
    capture = ij.SessionCapture([saved], workflow=[record], extensions={"owned_rank": "retained"})
    path = tmp_path / "rank-scientific.ijx"
    ij.save_session(str(path), capture, limits)
    file_bytes = path.read_bytes()
    assert 18 < len(file_bytes) <= limits.max_encoded_bytes and file_bytes[:10] == b"IMAGEJXX3\n"
    declared, = struct.unpack("<Q", file_bytes[10:18])
    assert 0 < declared <= limits.max_json_bytes
    inflater = zlib.decompressobj()
    document_bytes = inflater.decompress(file_bytes[18:], declared + 1)
    assert inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail
    assert len(document_bytes) == declared
    persisted = json.loads(document_bytes)
    assert persisted["version"] == 3 and persisted["workflow"] == [frozen]
    decoded = ij.load_session(str(path), limits)
    restored, = decoded.documents
    opened = restored.snapshot()
    assert decoded.source_version == 3 and decoded.workflow == [record]
    assert decoded.extensions == {"owned_rank": "retained"} and record == frozen
    assert restored.id != owner.id and not opened.dirty
    assert restored.undo_steps == restored.redo_steps == 0
    assert tuple(opened.dataset.shape) == (2, 2, 3, 5, 5, 1)
    np.testing.assert_array_equal(opened.dataset.numpy("TCZYX"), wanted)
    assert _helper.calibration_value(opened.dataset.calibration) == _helper.calibration_value(saved.dataset.calibration)
    assert opened.metadata == saved.metadata and opened.record_layout == saved.record_layout
    assert opened.selection == saved.selection and opened.previous_selection == saved.previous_selection
    assert opened.overlays == saved.overlays
    assert [(r.name, r.t, r.c, r.z, r.attributes, r.selection) for r in opened.rois] == [
        (r.name, r.t, r.c, r.z, r.attributes, r.selection) for r in saved.rois]
    assert {k: v for k, v in _helper.display_value(opened.display).items() if k != "revision"} == {
        k: v for k, v in _helper.display_value(saved.display).items() if k != "revision"}
    assert owner.snapshot().shares_data(saved) and owner.snapshot().dirty
    with pytest.raises(ValueError) as old_record:
        ij.rank_command_from_record(opened, record)
    request = ij.TargetRequest(ij.TargetScope.POSITIONS, positions=[ij.PlanePosition(0, 1, 2)],
        roi=ij.RoiPolicy.WHOLE_PLANE, destination=ij.OutputDestination.IN_PLACE)
    rebound = ij.rebind_rank_command(opened, record, request)
    assert rebound.target.document_id == restored.id and rebound.target.position_count == 1
    assert rebound.record()["options"] == record["options"] and record == frozen
    raw.scalar("python.session.version", decoded.source_version)
    opened_shape = tuple(opened.dataset.shape)
    raw.scalar("python.scientific.planes", opened_shape[0] * opened_shape[1] * opened_shape[2])
    raw.scalar("python.scientific.unit", opened.dataset.calibration.unit)
    raw.array("python.scientific.reopened_bytes", opened.dataset.numpy("TCZYXS"))
    raw.json("native_session.file", {"byte_count": len(file_bytes), "hex": file_bytes.hex()})
    raw.json("native_session.persisted", persisted)
    raw.json("original.record", record)
    raw.scalar("reopened.old_record_accepted", old_record.type is None)
    raw.json("rebound.actual_public_command", rebound.record())
    raw.owner("reopened", restored)
    raw.owner("source_after_save", owner)
    app.adopt(restored)
    rebound_prepared = app.prepare("rank_filter_imagej", rebound.target, rebound.parameters())
    assert restored.snapshot().shares_data(opened)
    app.publish(rebound_prepared)
    wanted_rebound = wanted.copy()
    wanted_rebound[0, 1, 2] = np.full((5, 5), 80, np.uint16)
    np.testing.assert_array_equal(restored.snapshot().dataset.numpy("TCZYX"), wanted_rebound)
    assert restored.undo_steps == 1 and restored.redo_steps == 0
    assert owner.snapshot().shares_data(saved) and record == frozen
    raw.owner("rebound", restored)
    raw.finish()


def test_rao05_rank_enabled_gil_thread_progress_during_successful_native_preparation(native, raw):
    started = time.monotonic()
    assert sys.implementation.name == "cpython", "This controlled handoff qualifies CPython only"
    runtime_check = getattr(sys, "_is_gil_enabled", None)
    runtime_enabled = runtime_check() if callable(runtime_check) else None
    build_disabled = sysconfig.get_config_var("Py_GIL_DISABLED")
    suffix = sysconfig.get_config_var("EXT_SUFFIX")
    profile = {
        "implementation": sys.implementation.name, "version": sys.version,
        "version_major": sys.version_info.major, "version_minor": sys.version_info.minor,
        "version_micro": sys.version_info.micro, "api_version": sys.api_version,
        "cache_tag": sys.implementation.cache_tag, "abiflags": getattr(sys, "abiflags", None),
        "soabi": sysconfig.get_config_var("SOABI"), "extension_suffix": suffix,
        "py_gil_disabled": build_disabled, "flags_gil": getattr(sys.flags, "gil", None),
        "runtime_check_available": callable(runtime_check), "runtime_enabled": runtime_enabled,
        "executable": sys.executable,
    }
    assert build_disabled in (None, 0, "0"), "Free-threaded builds are outside this GIL qualification"
    assert isinstance(suffix, str) and suffix
    assert str(Path(ij._native.__file__).resolve()).endswith(suffix)
    if callable(runtime_check):
        assert type(runtime_enabled) is bool and runtime_enabled, "Qualification requires an enabled GIL"
    else:
        assert runtime_check is None
        assert sys.version_info[:2] == (3, 12)
        assert sys.implementation.cache_tag == "cpython-312" and "312" in suffix
    assert sys.gettrace() is None and sys.getprofile() is None
    assert threading.gettrace() is None and threading.getprofile() is None

    app, _, scheduler = native
    # All ndarray creation, ingestion, expected values, parameter/target binding
    # and raw input capture precede the companion's gate. The ndarray wrapper
    # rank_filter_imagej also releases the GIL in conversion, so it cannot
    # isolate this handoff. Use the supported Application.prepare native route.
    pixels = np.zeros((512, 512), np.uint16)
    pixels[256, 256] = 80
    expected = np.zeros((512, 512), np.uint16)
    assert pixels.nbytes < 16 * 1024 * 1024
    owner = ij.Document(ij.Dataset(pixels, axes="YX"), "Owned positive rank GIL work")
    app.adopt(owner)
    before, history = owner.snapshot(), (owner.undo_steps, owner.redo_steps)
    resolved = command(owner, "median", 10)
    target, parameters = resolved.target, resolved.parameters()
    assert set(parameters) == {"method", "radius"}
    assert type(parameters["method"]) is str and type(parameters["radius"]) is float
    prepare_call = app.prepare
    operation = "rank_filter_imagej"
    actual_input = before.dataset.plane()
    np.testing.assert_array_equal(actual_input, pixels)
    input_bytes = actual_input.tobytes()
    raw.array("positive_rank.input", actual_input)
    raw.json("positive_rank.command_record", resolved.record())
    raw.json("positive_rank.parameters", parameters)
    raw.json("positive_rank.binding_route", {
        "application_class_module": type(app).__module__,
        "application_class_name": type(app).__qualname__,
        "callable_module": getattr(prepare_call, "__module__", None),
        "callable_name": getattr(prepare_call, "__name__", None),
        "callable_qualname": getattr(prepare_call, "__qualname__", None),
        "native_module": str(Path(ij._native.__file__).resolve()),
    })

    ready, enter = threading.Event(), threading.Event()
    handoffs, failures = [], []
    flags = {"callback_active": False, "native_call_armed": False, "native_returned": False}
    main_thread = threading.get_ident()

    def progress_from_python():
        ready.set()
        if not enter.wait(5):
            failures.append("Positive native rank call did not open the companion gate")
            return
        # Exactly one finite handoff. No loop, cancellation or worker wait is
        # used to inflate workload or delay native completion.
        handoffs.append({"callback_active": flags["callback_active"],
                         "native_call_armed": flags["native_call_armed"],
                         "native_returned": flags["native_returned"],
                         "thread_id": threading.get_ident()})

    worker = threading.Thread(target=progress_from_python,
                              name="owned positive rank GIL companion", daemon=True)
    previous = sys.getswitchinterval()
    prepared = None
    worker.start()
    try:
        assert ready.wait(5)
        sys.setswitchinterval(10)
        # Event.set may itself hand off while acquiring its condition lock.
        # Arm only after it returns, so any such early progress is rejected.
        enter.set()
        flags["native_call_armed"] = True
        try:
            # No Python callback, wait, sleep, ndarray conversion or parameter
            # acquisition occurs here. Builtin string/float parameters are
            # converted under the GIL; prepare_python releases only around
            # native Application::prepare. This includes completed rank work,
            # without locating the handoff in a particular neighborhood.
            prepared = prepare_call(operation, target, parameters,
                                    cancellation=None, progress=None)
        finally:
            # First Python action after native return: later thread progress
            # cannot receive credit even if cleanup/join releases the GIL.
            flags["native_returned"] = True
    finally:
        sys.setswitchinterval(previous)
        enter.set()
        worker.join(5)
    elapsed = time.monotonic() - started
    assert not worker.is_alive() and not failures
    assert elapsed < 10
    assert len(handoffs) == 1
    assert handoffs[0]["callback_active"] is False
    assert handoffs[0]["native_call_armed"] is True
    assert handoffs[0]["native_returned"] is False
    assert handoffs[0]["thread_id"] != main_thread
    assert sys.getswitchinterval() == previous
    assert prepared is not None and prepared.active

    # Conversion happens after native_returned and companion completion, so
    # its own GIL release cannot supply the observed handoff. The full literal
    # output independently proves successful non-noop native median filtering.
    actual = prepared.preview.dataset.plane()
    assert actual.shape == (512, 512) and actual.dtype == np.dtype(np.uint16)
    np.testing.assert_array_equal(actual, expected)
    assert actual.tobytes() != input_bytes
    unchanged(owner, before, history)
    raw.array("positive_rank.output", actual)
    for name, value in profile.items():
        raw.scalar("runtime.gil_profile." + name, value)
    raw.json("positive_rank.actual_handoffs", handoffs)
    raw.json("positive_rank.return_flags", flags)
    raw.scalar("positive_rank.main_thread_id", main_thread)
    raw.scalar("positive_rank.thread_progress", len(handoffs))
    raw.scalar("positive_rank.input_byte_count", actual_input.nbytes)
    raw.scalar("positive_rank.output_byte_count", actual.nbytes)
    raw.scalar("positive_rank.elapsed_seconds", elapsed)
    raw.scalar("positive_rank.previous_switch_interval", previous)
    raw.scalar("positive_rank.restored_switch_interval", sys.getswitchinterval())
    raw.scalar("positive_rank.progress_callback", None)
    raw.scalar("positive_rank.cancellation_token", None)
    raw.json("positive_rank.source_revisions", {
        name: getattr(owner.snapshot().revisions, name)
        for name in ("pixels", "metadata", "selection", "display")})
    prepared = None
    gc.collect()
    raw.json("terminal.scheduler_usage", zero_usage(scheduler))
    raw.finish()


EXPECTED_COLLECTION_IDS = (
    "test_rao01_rank_progress_exception_releases_callback_and_preserves_source",
    "test_rao02_rank_progress_captures_parameters_and_releases_accepted_callback",
    "test_rao03_rank_releases_gil_for_cross_thread_cancel_after_initial_progress",
    "test_rao04_scientific_native_v3_save_open_preserves_bits_and_requires_full_rebind",
    "test_rao05_rank_enabled_gil_thread_progress_during_successful_native_preparation",
)
