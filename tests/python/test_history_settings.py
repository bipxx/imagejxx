"""Installed headless G010 policy conformance and actual-value transport.

The parent must supply exact installed-origin pins and a hard30s process bound.
No desktop/private hooks or Python image-processing implementation is used.
"""

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import sys
import time

import imagejxx as ij
import numpy as np
import pytest


MIB = 1048576
PLANE_BYTES = 262144
MAX_RAW_BYTES = 16 * MIB
STEP = "G010.history.step_limit"
BUDGET = "G010.history.pixel_budget"
EXPECTED_COLLECTION_IDS = (
    "tests/python/test_history_settings.py::test_history_step_policy_effects",
    "tests/python/test_history_settings.py::test_history_pixel_budget_effects",
    "tests/python/test_history_settings.py::test_history_persist_reopen_adopt",
    "tests/python/test_history_settings.py::test_history_detached_settings_no_owner_effects",
)
CASE_IDS = (
    "HISTORYPY01.step_policy_effects",
    "HISTORYPY02.pixel_budget_effects",
    "HISTORYPY03.persist_reopen_adopt",
    "HISTORYPY04.detached_settings_no_owner_effects",
)


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def _read(path, maximum):
    assert path.is_file() and not path.is_symlink()
    size = path.stat().st_size
    assert 0 < size <= maximum
    with path.open("rb") as stream:
        data = stream.read(size + 1)
    assert len(data) == size
    return data


def _pin(path, maximum):
    path = path.resolve(strict=True)
    data = _read(path, maximum)
    return {"path": str(path), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def _runtime():
    name = os.environ.get("IMAGEJXX_HISTORY_PYTHON_ORIGINS")
    assert name, "Required installed-origin envelope: IMAGEJXX_HISTORY_PYTHON_ORIGINS"
    path = Path(name)
    assert path.is_absolute()
    raw = _read(path, MIB)
    origin = json.loads(raw, parse_constant=lambda value: (_ for _ in ()).throw(
        ValueError(f"Nonfinite origin token: {value}")))
    assert origin["schema_version"] == 1
    assert origin["kind"] == "p06h_history_python_runtime_origins_v1"
    package = Path(origin["package_root"])
    assert package.is_absolute() and package.is_dir()
    package = package.resolve(strict=True)
    assert set(origin["modules"]) == {"imagejxx", "_native"}
    actual = {}
    for role, module in (("imagejxx", ij), ("_native", sys.modules["imagejxx._native"])):
        expected = origin["modules"][role]
        assert Path(expected["path"]).is_absolute()
        module_path = Path(module.__file__).resolve(strict=True)
        assert module_path.parent == package
        actual[role] = _pin(module_path, 64 * MIB)
        assert module_path == Path(expected["path"]).resolve(strict=True)
        assert actual[role]["bytes"] == expected["bytes"] and actual[role]["sha256"] == expected["sha256"], (
            f"Installed {role} origin/pin differs")
    assert "imagejxx._desktop" not in sys.modules, "Headless carrier imported desktop"
    return {
        "python": {"executable": str(Path(sys.executable).resolve(strict=True)),
                   "version": sys.version, "process_id": os.getpid()},
        "numpy": {"version": np.__version__}, "package_root": str(package),
        "modules": actual,
        "origin_envelope": {"path": str(path.resolve(strict=True)), "bytes": len(raw),
                            "sha256": hashlib.sha256(raw).hexdigest()},
        "desktop_loaded": "imagejxx._desktop" in sys.modules,
    }


class _Observations:
    def __init__(self, tmp_path, case_id, required_rows):
        self.work = tmp_path / "work"
        self.work.mkdir()
        self.case_id = case_id
        self.required_rows = required_rows
        self.rows = []
        self.auxiliary = []
        self.identities = set()
        self.runtime = None
        self.started = time.monotonic()

    def __enter__(self):
        try:
            self.runtime = _runtime()
        except BaseException as error:
            self._write(False, error)
            raise
        return self

    def _add(self, requirement, name, encoding, value):
        identity = requirement, name
        assert identity not in self.identities
        self.identities.add(identity)
        self.rows.append({"requirement_id": requirement, "id": name,
                          "encoding": encoding, "value": value})

    def scalar(self, requirement, name, actual, expected):
        assert actual is None or type(actual) in (bool, int, float, str)
        self._add(requirement, name, "scalar", actual)
        assert type(actual) is type(expected), f"Typed scalar mismatch: {requirement}/{name}"
        assert _canonical(actual) == _canonical(expected), f"Actual scalar differs: {requirement}/{name}"

    def pixels(self, requirement, name, owner, expected_sha256):
        value = _pixel_transport(owner)
        self._add(requirement, name, "bytes_le", value)
        # Only actual native-exported bytes enter the observed digest.
        actual_digest = hashlib.sha256(bytes.fromhex(value["hex"])).hexdigest()
        assert actual_digest == expected_sha256, f"Full declared pixels differ: {requirement}/{name}"

    def facts(self, name, value):
        self.auxiliary.append({"id": name, "value": deepcopy(value)})

    def _write(self, complete, error):
        raw = {"schema_version": 1, "producer_id": "p06h.history-settings.python.raw.001",
               "complete": complete, "case_id": self.case_id, "runtime": self.runtime,
               "context": "python_api", "scope": "explicit native Document/Application policy; no live Window",
               "observations": self.rows, "auxiliary": self.auxiliary,
               "source": _pin(Path(__file__), MIB),
               "error": None if error is None else {"type": type(error).__name__, "message": str(error)}}
        data = json.dumps(raw, ensure_ascii=False, indent=2, allow_nan=False).encode("utf-8") + b"\n"
        assert len(data) <= MAX_RAW_BYTES
        with (self.work / "history-settings-observations.json").open("xb") as stream:
            stream.write(data)

    def __exit__(self, error_type, error, traceback):
        if error is None:
            try:
                assert len(self.rows) == self.required_rows, "Exact declared assertion membership did not finish"
                assert time.monotonic() - self.started < 30, "Source boundary exceeded30 seconds; parent also enforces hard deadline"
                assert _runtime() == self.runtime, "Runtime origins changed during this case"
            except BaseException as failure:
                self._write(False, failure)
                raise
        self._write(error is None, error)
        return False


def _literal(large, iteration=0):
    # Independent deterministic input/expected fixture, never an output oracle.
    if large:
        return np.full((1, 128, 256), iteration, np.uint8)
    z, y, x = np.indices((2, 6, 8))
    return (7*z + 3*y + x + iteration).astype(np.uint8)


def _owner(large):
    calibration = ij.SpatialCalibration(spacing=[2, 3, 5], origin=[11, -7, 4], unit="um")
    seed = ij.Document(ij.Dataset(_literal(large), axes="ZYX", calibration=calibration),
                       "Owned history budget" if large else "Owned history steps")
    transaction = seed.begin_edit()
    transaction.set_property("fixture_recipe", "zero;fill1..10" if large else "7*z+3*y+x;add1x10")
    transaction.set_extension("future", [True, 17])
    transaction.select(ij.Selection("rectangle", [(1, 1), (4, 4)]))
    transaction.set_previous_selection(ij.Selection("line", [(0, 0), (2, 2)]))
    transaction.set_rois([ij.ROI("retained area", ij.Selection("rectangle", [(1, 1), (4, 4)]),
                                 z=0, attributes={"future.style": {"rgba": [1, 2, 3, 255]}})])
    seed.commit(transaction)
    # Public clone yields fresh/clean0-revision/empty-history owner while
    # retaining the initialized records. All seed aliases end on return.
    return ij.Document(seed.snapshot())


def _owned(large):
    application = ij.Application()
    owner = _owner(large)
    identity = application.adopt(owner)
    assert identity == owner.id
    return application, owner, identity


def _apply(application, identity, settings):
    units = settings["history_memory_mb"]
    assert type(units) is int and 0 <= units <= 1048576
    application.set_history_policy(settings["history_limit"], units * MIB, identity)


def _edits(application, identity, large, first=1, last=10):
    for iteration in range(first, last + 1):
        source = application.snapshot(identity)
        transaction = source.begin_edit()
        for z in range(source.dataset.shape[2]):
            plane = source.dataset.plane(coordinate=z)
            # Processing remains the public native core. Python only routes
            # native arrays into native plane transactions.
            replacement = ij.fill(plane, iteration) if large else ij.add(plane, 1)
            transaction.replace_plane(replacement, z=z)
        application.commit(transaction)
    # No source snapshot, Dataset, native plane or transaction escapes.


def _exact_pixels(owner, large, iteration):
    array = owner.snapshot().dataset.numpy("ZYX")
    assert array.dtype == np.dtype("uint8")
    np.testing.assert_array_equal(array, _literal(large, iteration))


def _pixel_transport(owner):
    source = owner.snapshot()
    array = source.dataset.numpy("TCZYXS")
    assert array.dtype == np.dtype("uint8") and tuple(array.shape) == tuple(source.dataset.shape)
    actual = array.tobytes(order="C")
    assert len(actual) <= MAX_RAW_BYTES
    return {"pixel_type": "uint8", "shape": list(array.shape), "order": "TCZYXS",
            "hex": actual.hex(), "source_samples": array.reshape(-1).tolist(),
            "storage_scope": "actual declared UInt8 export; no private native-double storage accessor"}


def _history(owner):
    value = owner.history()
    return {"undo_steps": value.undo_steps, "redo_steps": value.redo_steps,
            "step_limit": value.step_limit, "pixel_budget": value.pixel_budget,
            "pixels": {name: getattr(value.pixels, name) for name in (
                "logical_bytes", "current_bytes", "retained_bytes", "obsolete_bytes", "retained_planes", "exact")}}


def _selection(value):
    if value is None:
        return None
    assert value.kind in ("rectangle", "line")
    return {"kind": value.kind, "points": [list(point) for point in value.points],
            "width": value.width, "attributes": value.attributes}


def _state(owner):
    source = owner.snapshot()
    data = source.dataset
    calibration = data.calibration
    display = source.display
    assert source.spectrum is None
    return {"id": source.id, "data_identity": source.data_identity,
            "revisions": {name: getattr(source.revisions, name) for name in ("pixels", "metadata", "selection", "display")},
            "dirty": source.dirty, "shape": list(data.shape), "dtype": str(data.dtype),
            "pixels": data.numpy("TCZYXS").tobytes(order="C").hex(),
            "metadata": source.metadata, "record_layout": source.record_layout,
            "calibration": {name: list(getattr(calibration, name)) if name in ("spacing", "origin", "direction")
                            else getattr(calibration, name) for name in (
                                "spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit")},
            "selection": _selection(source.selection), "previous_selection": _selection(source.previous_selection),
            "rois": [{"name": row.name, "selection": _selection(row.selection), "t": row.t, "c": row.c,
                      "z": row.z, "attributes": row.attributes} for row in source.rois],
            "overlays": source.overlays,
            "display": {name: deepcopy(getattr(display, name)) for name in (
                "t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan", "properties", "revision")},
            "history": _history(owner)}


def _preserved(record, requirement, before, owner):
    after = _state(owner)
    assert {k: v for k, v in after.items() if k != "history"} == {
        k: v for k, v in before.items() if k != "history"}
    for kind in ("pixels", "metadata", "selection"):
        name = "pixel" if kind == "pixels" else kind
        record.scalar(requirement, f"policy.{name}_revision_delta",
                      after["revisions"][kind] - before["revisions"][kind], 0)
    record.scalar(requirement, "policy.dirty_changed", after["dirty"] != before["dirty"], False)


def _phase(record, requirement, phase, settings, owner, expected_setting, expected_effect, memory):
    history = owner.history()
    assert history.pixel_budget % MIB == 0
    name = "history_memory_mb" if memory else "history_limit"
    record.scalar(requirement, f"{phase}.setting", settings[name], expected_setting)
    record.scalar(requirement, f"{phase}.current", history.pixel_budget // MIB if memory else history.step_limit, expected_setting)
    record.scalar(requirement, f"{phase}.effective", history.pixel_budget if memory else history.step_limit,
                  expected_setting * MIB if memory else expected_setting)
    record.scalar(requirement, f"{phase}.effect", history.pixels.obsolete_bytes if memory else history.undo_steps, expected_effect)


def test_history_step_policy_effects(tmp_path):
    with _Observations(tmp_path, CASE_IDS[0], 16) as record:
        defaults = ij.Settings()
        application, owner, identity = _owned(False)
        assert owner.history().step_limit == 8 and owner.history().pixel_budget == 0
        _edits(application, identity, False)
        _exact_pixels(owner, False, 10)
        assert owner.redo_steps == 0
        _phase(record, STEP, "default", defaults, owner, 8, 8, False)
        del application, owner

        changed = ij.Settings({"history_limit": 3})
        application, owner, identity = _owned(False)
        _apply(application, identity, changed)
        _edits(application, identity, False)
        _phase(record, STEP, "changed", changed, owner, 3, 3, False)
        _exact_pixels(owner, False, 10)
        before = _state(owner)
        application.set_history_policy(2, 0, identity)
        _preserved(record, STEP, before, owner)
        record.scalar(STEP, "pruned_two.undo_steps", owner.history().undo_steps, 2)
        record.pixels(STEP, "policy.all_pixels", owner,
                      "d74cb605d08471733acb7961277bf819b3c3004727cd80686691ebe52bf2c671")
        application.undo(identity)
        _exact_pixels(owner, False, 9)
        first = owner.snapshot().dataset.plane()[0, 0]
        assert first.dtype == np.dtype("uint8")
        record.scalar(STEP, "pruned_two.undo_first_pixel", int(first), 9)
        record.pixels(STEP, "pruned_two.undo_all_pixels", owner,
                      "7362892ab71c2298f769847331c54fa7be8c950ec7f2c885d695b3c27cb58232")


def test_history_pixel_budget_effects(tmp_path):
    with _Observations(tmp_path, CASE_IDS[1], 17) as record:
        defaults = ij.Settings({"history_limit": 128})
        application, owner, identity = _owned(True)
        assert owner.history().pixel_budget == 0
        _apply(application, identity, defaults)
        _edits(application, identity, True)
        _exact_pixels(owner, True, 10)
        history = owner.history()
        assert history.undo_steps == 10 and history.pixels.exact and history.pixels.current_bytes == PLANE_BYTES
        _phase(record, BUDGET, "default", defaults, owner, 0, 2621440, True)
        del application, owner

        changed = ij.Settings({"history_limit": 128, "history_memory_mb": 1})
        application, owner, identity = _owned(True)
        _apply(application, identity, defaults)
        _edits(application, identity, True)
        before = _state(owner)
        _apply(application, identity, changed)
        _preserved(record, BUDGET, before, owner)
        _exact_pixels(owner, True, 10)
        history = owner.history()
        assert history.undo_steps == 4 and history.redo_steps == 0 and history.pixels.exact
        assert history.pixels.current_bytes == PLANE_BYTES and history.pixels.retained_planes == 5
        _phase(record, BUDGET, "changed", changed, owner, 1, MIB, True)
        record.scalar(BUDGET, "one_mib.obsolete_buffer_count", history.pixels.retained_planes - 1, 4)
        record.scalar(BUDGET, "one_mib.pixel_bytes", history.pixels.obsolete_bytes, MIB)
        record.pixels(BUDGET, "policy.all_pixels", owner,
                      "53b222bdde41d9adc3c2ac63cb06d59e51f427e4128694c7503c3fb05f795e6c")
        application.set_history_policy(2, MIB, identity)
        record.scalar(BUDGET, "step_two.pixel_bytes", owner.history().pixels.obsolete_bytes, 524288)
        assert owner.undo_steps == 2
        application.set_history_policy(2, 0, identity)
        _edits(application, identity, True, 11, 13)
        history = owner.history()
        zero_cap = (history.pixel_budget == 0 and history.step_limit == 2 and history.undo_steps == 2
                    and history.pixels.obsolete_bytes == 524288)
        record.scalar(BUDGET, "zero_budget.step_cap_applies", zero_cap, True)

        dedup_app, dedup_owner, dedup_id = _owned(True)
        _apply(dedup_app, dedup_id, changed)
        _edits(dedup_app, dedup_id, True)
        for value in (1, 2):
            transaction = dedup_app.begin_edit(dedup_id)
            transaction.set_property("metadata_only", value)
            dedup_app.commit(transaction)
        del transaction
        actual = _history(dedup_owner)
        assert actual["undo_steps"] == 6 and actual["pixels"]["retained_planes"] == 5
        assert actual["pixels"]["obsolete_bytes"] == MIB and actual["pixels"]["current_bytes"] == PLANE_BYTES
        assert actual["pixels"]["exact"]
        record.facts("deduplicated_metadata_history", actual)


def _limits():
    value = ij.SessionLimits()
    value.max_documents, value.max_planes, value.max_samples = 4, 8, MIB
    value.max_json_bytes = value.max_encoded_bytes = MAX_RAW_BYTES
    value.max_working_bytes = 128 * MIB
    return value


def _bounded_file(path):
    assert path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= MAX_RAW_BYTES


def _atomic_adopt(application, owners, settings, *, make_active=True):
    # Required genuine public seam. Absence fails visibly; no sequential or
    # private-desktop substitution can claim atomic policy adoption.
    return application.adopt_all_with_history_policy(
        documents=owners, limit=settings["history_limit"],
        pixel_budget=settings["history_memory_mb"] * MIB, make_active=make_active)


def _persisted(record, large):
    requirement = BUDGET if large else STEP
    prefix = "budget" if large else "step"
    preferences = ij.PreferencesDocument()
    preferences.settings.update({"history_limit": 128 if large else 3, "history_memory_mb": 1 if large else 0})
    path = record.work / f"{prefix}-preferences.json"
    assert preferences.save(path)
    _bounded_file(path)
    reopened = ij.PreferencesDocument.load(path, recover=False)
    assert not reopened.write_blocked
    application, owner, identity = _owned(large)
    _apply(application, identity, reopened.settings)
    _edits(application, identity, large)
    _exact_pixels(owner, large, 10)
    _phase(record, requirement, "persisted", reopened.settings, owner, 1 if large else 3, MIB if large else 3, large)
    record.scalar(requirement, "persisted.reopened_setting",
                  reopened.settings["history_memory_mb" if large else "history_limit"], 1 if large else 3)
    session_path = record.work / f"{prefix}-session.ijx"
    ij.save_session(str(session_path), ij.SessionCapture([owner]), _limits())
    _bounded_file(session_path)
    decoded = ij.load_session(str(session_path), _limits())
    assert decoded.source_version == 3 and len(decoded.documents) == 1
    incoming = decoded.documents[0]
    actual = _state(incoming)
    assert not actual["dirty"] and actual["history"]["undo_steps"] == actual["history"]["redo_steps"] == 0
    assert actual["history"]["step_limit"] == 8 and actual["history"]["pixel_budget"] == 0
    assert list(actual["revisions"].values()) == [0, 0, 0, 0]
    _exact_pixels(incoming, large, 10)
    reopened_app = ij.Application()
    ids = _atomic_adopt(reopened_app, [incoming], reopened.settings)
    assert ids == [incoming.id] and reopened_app.document_ids == ids
    history = reopened_app.history(incoming.id)
    assert history.step_limit == (128 if large else 3) and history.pixel_budget == (MIB if large else 0)
    record.scalar(requirement, "persisted.reopened_undo_steps", history.undo_steps, 0)
    record.scalar(requirement, "persisted.reopened_redo_steps", history.redo_steps, 0)
    record.facts(f"{prefix}_reopened_owner", {"document_id": incoming.id, "policy": _history(incoming),
                                             "dirty": incoming.snapshot().dirty})


def _adoption_refusals(record):
    application = ij.Application()
    anchor, incoming = _owner(False), _owner(False)
    # The incoming caller really has pruneable history. An empty-history
    # fixture would not detect a premature policy write before late refusal.
    _unattached_edits(incoming)
    assert incoming.undo_steps == 8 and incoming.snapshot().dirty
    anchor_id = application.adopt(anchor)
    before, anchor_before = _state(incoming), _state(anchor)
    membership, active = application.document_ids, application.active_document_id
    refused = []
    calls = (
        ("tuple", TypeError, lambda: application.adopt_all_with_history_policy((incoming,), 3, MIB)),
        ("bool_limit", TypeError, lambda: application.adopt_all_with_history_policy([incoming], True, MIB)),
        ("float_limit", TypeError, lambda: application.adopt_all_with_history_policy([incoming], 3.0, MIB)),
        ("zero_limit", ValueError, lambda: application.adopt_all_with_history_policy([incoming], 0, MIB)),
        ("bool_budget", TypeError, lambda: application.adopt_all_with_history_policy([incoming], 3, True)),
        ("negative_budget", ValueError, lambda: application.adopt_all_with_history_policy([incoming], 3, -1)),
        ("oversize_budget", ValueError, lambda: application.adopt_all_with_history_policy([incoming], 3, 2**256)),
        ("nonbool_active", TypeError, lambda: application.adopt_all_with_history_policy([incoming], 3, MIB, make_active=1)),
        ("positional_active", TypeError, lambda: application.adopt_all_with_history_policy([incoming], 3, MIB, False)),
        ("late_invalid", TypeError, lambda: application.adopt_all_with_history_policy([incoming, object()], 3, MIB)),
        ("duplicate", ValueError, lambda: application.adopt_all_with_history_policy([incoming, incoming], 3, MIB)),
        ("late_registered", ValueError, lambda: application.adopt_all_with_history_policy([incoming, anchor], 3, MIB)),
    )
    for name, error_type, call in calls:
        with pytest.raises(error_type) as caught:
            call()
        assert application.document_ids == membership and application.active_document_id == active
        assert _state(incoming) == before and _state(anchor) == anchor_before
        refused.append({"id": name, "exception_type": type(caught.value).__name__, "message": str(caught.value),
                        "membership": application.document_ids, "active_document_id": application.active_document_id})
    empty = application.adopt_all_with_history_policy([], 3, MIB)
    assert empty == []
    assert application.document_ids == membership and application.active_document_id == anchor_id
    assert _state(incoming) == before and _state(anchor) == anchor_before
    second = _owner(False)
    admitted = application.adopt_all_with_history_policy([incoming, second], 3, MIB, make_active=False)
    assert admitted == [incoming.id, second.id]
    assert application.document_ids == [anchor_id] + admitted and application.active_document_id == anchor_id
    assert incoming.history().step_limit == second.history().step_limit == 3
    assert incoming.history().pixel_budget == second.history().pixel_budget == MIB
    assert incoming.undo_steps == 3 and second.undo_steps == 0
    after_admission = _state(incoming)
    assert {k: v for k, v in after_admission.items() if k != "history"} == {
        k: v for k, v in before.items() if k != "history"}
    record.facts("atomic_adoption_signature_refusals", refused)
    record.facts("empty_and_nonactivating_adoption", {"empty_reply": empty, "admitted": admitted, "membership": application.document_ids,
                                                      "active_document_id": application.active_document_id})


def _unattached_edits(owner):
    for _ in range(10):
        source = owner.snapshot()
        transaction = source.begin_edit()
        for z in range(source.dataset.shape[2]):
            transaction.replace_plane(ij.add(source.dataset.plane(coordinate=z), 1), z=z)
        owner.commit(transaction)


def test_history_persist_reopen_adopt(tmp_path):
    with _Observations(tmp_path, CASE_IDS[2], 18) as record:
        _persisted(record, False)
        _persisted(record, True)
        preferences = ij.PreferencesDocument()
        preferences.settings.update({"history_limit": 3, "history_memory_mb": 1})
        path = record.work / "pair-preferences.json"
        assert preferences.save(path)
        _bounded_file(path)
        reopened = ij.PreferencesDocument.load(path, recover=False)
        left = ij.load_session(str(record.work / "step-session.ijx"), _limits()).documents[0]
        right = ij.load_session(str(record.work / "budget-session.ijx"), _limits()).documents[0]
        fresh = _owner(False)
        application = ij.Application()
        admitted = _atomic_adopt(application, [fresh, left, right], reopened.settings)
        assert admitted == [fresh.id, left.id, right.id] and application.document_ids == admitted
        assert application.active_document_id == right.id
        for owner in (fresh, left, right):
            history = owner.history()
            assert history.step_limit == 3 and history.pixel_budget == MIB
            assert history.undo_steps == history.redo_steps == 0 and not owner.snapshot().dirty
        for requirement, owner in ((STEP, left), (BUDGET, right)):
            record.scalar(requirement, "adopted_pair.limit", owner.history().step_limit, 3)
            record.scalar(requirement, "adopted_pair.pixel_budget_bytes", owner.history().pixel_budget, MIB)
        record.facts("actual_pair_adoption", {"document_ids": application.document_ids,
                                              "active_document_id": application.active_document_id})
        _adoption_refusals(record)


def test_history_detached_settings_no_owner_effects(tmp_path):
    with _Observations(tmp_path, CASE_IDS[3], 2) as record:
        left_app, left, left_id = _owned(False)
        right_app, right, right_id = _owned(True)
        _edits(left_app, left_id, False, 1, 3)
        _edits(right_app, right_id, True, 1, 3)
        before = [_state(left), _state(right)]
        detached = ij.Settings({"history_limit": 2, "history_memory_mb": 1})
        after = [_state(left), _state(right)]
        assert detached["history_limit"] == 2 and detached["history_memory_mb"] == 1
        actual_effects = sum(actual != original for actual, original in zip(after, before, strict=True))
        record.scalar(STEP, "detached_settings.owner_effect_count", actual_effects, 0)
        record.scalar(BUDGET, "detached_settings.owner_effect_count", actual_effects, 0)
        record.facts("detached_actual_policies", [value["history"] for value in after])
