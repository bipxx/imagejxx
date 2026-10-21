"""SU01..SU10 independent native startup API assertions.

V3/V2 fixtures are owned bounded wire literals; expected samples never come
from the product encoder. Optional Editor calls are disclosed API setup, with
no OS relaunch, pointer input or visible ImGui layout credit.
"""
import base64
from copy import deepcopy
import json
from pathlib import Path
import struct
import sys
import zlib

import imagejxx as ij
import numpy as np
import pytest


MAX_BYTES = 4 * 1024 * 1024
CASES = tuple(f"SU{index:02d}" for index in range(1, 11))


def _frame(root, version=3):
    payload = json.dumps(root, allow_nan=False, ensure_ascii=False).encode("utf-8")
    assert 0 < len(payload) <= MAX_BYTES
    header = (b"IMAGEJXX3\n" + struct.pack("<Q", len(payload)) if version == 3
              else b"IMAGEJXX2\n" + struct.pack(">I", len(payload)))
    result = header + zlib.compress(payload)
    assert len(result) <= MAX_BYTES
    return result


def _selection(kind, points):
    return {"kind": kind, "points": points, "width": 1.0, "attributes": {}}


def _document(scientific):
    shape = [2, 3, 2, 6, 8, 1] if scientific else [1, 1, 2, 6, 8, 1]
    planes = []
    for t in range(shape[0]):
        for c in range(shape[1]):
            for z in range(2):
                samples = [40*t + 30*c + 5*z + 2*y + x if scientific else 7*z + 3*y + x
                           for y in range(6) for x in range(8)]
                planes.append(base64.b64encode(struct.pack("<48d", *samples)).decode("ascii"))
    return {
        "axes": "TCZYXS", "shape": shape,
        "pixel_type": "uint16" if scientific else "uint8",
        "sample_policy": "finite_only", "storage": "Float64LE", "planes": planes,
        "calibration": {"spacing": [1.25, .5, 2.5], "origin": [-2.0, 3.0, 4.0],
                        "direction": [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
                        "unit": "um", "time_unit": "s", "frame_interval": .75, "time_origin": 2.0},
        "metadata": {"title": "Owned U16 scientific" if scientific else "Owned U8 stack",
                     "properties": {"original_path": "deleted-owned-original.pgm"},
                     "extensions": {"vendor": {"opaque": [True, None, "retained"]}}, "record_layout": {}},
        "display": {"t": 1 if scientific else 0, "c": 2 if scientific else 0,
                    "crosshair": [3, 2, 1], "orthogonal": bool(scientific), "linked_zoom": False,
                    "zoom": 2.25, "plane_zoom": [1.5, 2.5, 3.5],
                    "pan": [[.25, -.5], [1.25, -1.5], [2.25, -2.5]], "properties": {}},
        "selection": _selection("rectangle", [[1, 1], [5, 4]]),
        "previous_selection": _selection("line", [[0, 0], [7, 5]]),
        "rois": [{"name": "positioned", "selection": _selection("rectangle", [[2, 1], [6, 5]]),
                  "t": 1 if scientific else 0, "c": 2 if scientific else 0, "z": 1,
                  "attributes": {"inert": True}}],
        "overlays": [{"type": "text", "text": "owned inert overlay"}], "fourier": None,
    }


def _workspace(*, changed=False):
    root = {"format": "imagejxx-session", "version": 3,
            "documents": [_document(False), _document(True)],
            "results": [{"Value": 17}],
            "workflow": [{"operation": "must_never_execute", "historical_id": 777}],
            "extensions": {"imagejxx_workspace": {"version": 1, "active_index": 0},
                           "vendor": {"retained": True}}}
    if changed:
        samples = list(struct.unpack("<48d", base64.b64decode(root["documents"][1]["planes"][11])))
        assert samples[2*8 + 3] == 112
        samples[2*8 + 3] = 113
        root["documents"][1]["planes"][11] = base64.b64encode(struct.pack("<48d", *samples)).decode("ascii")
    return root


def _preferences(path, workspace="", **changes):
    values = {"history_limit": 3, "history_memory_mb": 1,
              "shortcut_bindings": ["analysis.measure=Ctrl+K"], **changes}
    if workspace:
        values.update(startup_mode="workspace", startup_workspace=workspace)
    root = {"format": "imagejxx-preferences", "version": 1, "values": values,
            "extras": {"recent": ["missing-recent.pgm"], "layout": "[Window][Owned]\nPos=13,17\nSize=400,300\n",
                       "vendor": {"opaque": True}}, "vendor_root": [None, 17]}
    path.write_text(json.dumps(root, allow_nan=False), encoding="utf-8")
    return root


def _request(work, **changes):
    return ij.StartupRequest(launch_cwd=str(work), preferences_write_path=str(work / "preferences.json"), **changes)


def _editor(**kwargs):
    from imagejxx.ui import Editor
    return Editor(**kwargs)


def _record(work, case, **values):
    assert case in CASES
    data = {"schema_version": 1, "producer_id": "p06j.startup.python.raw.001",
            "case_id": case, "complete": True, "observations": values}
    raw = json.dumps(data, ensure_ascii=False, allow_nan=False, indent=2).encode("utf-8") + b"\n"
    assert len(raw) <= 16 * 1024 * 1024
    with (work / f"startup-api-{case}.json").open("xb") as stream:
        stream.write(raw)


def _empty(editor):
    assert editor.document_infos == [] and editor.startup_state["active_index"] is None
    assert editor.results == [] and editor.workflow == []


def _fresh(owner):
    snapshot = owner.snapshot()
    assert not snapshot.dirty and owner.undo_steps == owner.redo_steps == 0
    assert [getattr(snapshot.revisions, name) for name in ("pixels", "metadata", "selection", "display")] == [0, 0, 0, 0]


def _check_samples(snapshot, number, *, changed=False):
    actual = snapshot.dataset.numpy("TCZYXS")
    shape = (2, 3, 2, 6, 8, 1) if number else (1, 1, 2, 6, 8, 1)
    assert actual.shape == shape and actual.dtype == np.dtype(np.uint16 if number else np.uint8)
    for t in range(shape[0]):
        for c in range(shape[1]):
            for z in range(2):
                for y in range(6):
                    for x in range(8):
                        expected = 40*t + 30*c + 5*z + 2*y + x if number else 7*z + 3*y + x
                        if changed and number and (t, c, z, y, x) == (1, 2, 1, 2, 3):
                            expected = 113
                        assert int(actual[t, c, z, y, x, 0]) == expected
    assert list(snapshot.dataset.calibration.spacing) == [1.25, .5, 2.5]
    assert snapshot.selection is not None and snapshot.previous_selection is not None
    assert len(snapshot.rois) == 1 and snapshot.rois[0].z == 1
    assert snapshot.metadata["extensions"]["vendor"] == {"opaque": [True, None, "retained"]}
    assert snapshot.display.zoom == 2.25 and list(snapshot.display.plane_zoom) == [1.5, 2.5, 3.5]
    return actual.ravel().tolist()


def test_SU01_defaults_recent_and_strict_base(tmp_path):
    # This case exercises only the base module. Other API cases may explicitly
    # import the optional Editor after this assertion has run.
    preferences = ij.PreferencesDocument()
    preferences.extras_json = '{"recent":["missing.ijx"]}'
    plan = ij.plan_startup(preferences, _request(tmp_path))
    assert plan.source == ij.StartupSource.Empty and plan.paths == []
    empty = ij.with_workspace_presentation({}, None, 0)
    assert ij.workspace_presentation(empty.extensions, 0).active_index is None
    for kwargs in ({"explicit_paths": ("x",)}, {"explicit_paths": [Path("x")]},
                   {"reset_layout": 1}, {"restore_workspace": False}, {"launch_cwd": Path("x")}):
        with pytest.raises(TypeError):
            ij.StartupRequest(**kwargs)
    for field in ("max_explicit_paths", "max_path_bytes", "max_layout_bytes"):
        limits = ij.StartupLimits()
        for invalid in (True, 0, -1, 1.5, 2**200):
            with pytest.raises((TypeError, ValueError)):
                setattr(limits, field, invalid)
    assert "StartupRequest" in ij.__all__ and "StartupError" in ij.__all__
    _record(tmp_path, "SU01", source=str(plan.source), paths=plan.paths,
            active_index=ij.workspace_presentation(empty.extensions, 0).active_index,
            extras_json=preferences.extras_json)


def test_SU02_selected_preferences_and_path_only_constructor(tmp_path, monkeypatch):
    selected = tmp_path / "selected"
    selected.mkdir()
    (selected / "relative.ijx").write_bytes(_frame(_workspace()))
    prefs = selected / "preferences.json"
    _preferences(prefs, "relative.ijx")
    monkeypatch.chdir(tmp_path)
    editor = _editor(preferences=prefs)
    assert len(editor.document_infos) == 2 and editor.active_index == 0
    assert editor.startup_state["paths"] == [str(selected / "relative.ijx")]
    assert editor.startup_state["preferences_write_path"] == str(prefs)
    for invalid in (object(), {"path": str(prefs)}, 17, True):
        with pytest.raises(TypeError):
            _editor(preferences=invalid)
    explicit = _editor(preferences=prefs, workspace="selected/relative.ijx")
    assert explicit.startup_state["source"] == "explicit_workspace"
    _record(tmp_path, "SU02", selected_state=editor.startup_state, explicit_state=explicit.startup_state)


def test_SU03_owned_scientific_first_active_and_fresh(tmp_path):
    wire = _frame(_workspace())
    path = tmp_path / "scientific.ijx"
    path.write_bytes(wire)
    decoded = ij.load_session(str(path))
    presentation = ij.workspace_presentation(decoded.extensions, 2)
    application = ij.Application()
    identities = application.adopt_all_selected(decoded.documents, presentation.active_index)
    assert application.document_ids == identities and application.active_document_id == identities[0]
    actual = []
    for index, owner in enumerate(decoded.documents):
        _fresh(owner)
        actual.append(_check_samples(owner.snapshot(), index))
    scientific = decoded.documents[1].snapshot()
    assert (scientific.display.t, scientific.display.c, scientific.display.crosshair[2]) == (1, 2, 1)
    assert decoded.results == [{"Value": 17}] and decoded.workflow == [{"operation": "must_never_execute", "historical_id": 777}]
    second_decode = ij.load_session(str(path))
    assert all(a.id != b.id for a, b in zip(decoded.documents, second_decode.documents, strict=True))
    assert path.read_bytes() == wire
    _record(tmp_path, "SU03", document_ids=application.document_ids, active_id=application.active_document_id,
            samples=actual, source_hex=path.read_bytes().hex(), source_version=decoded.source_version,
            results=decoded.results, workflow=decoded.workflow)


def test_SU04_explicit_order_suppression_and_conflicts(tmp_path):
    prefs = tmp_path / "preferences.json"
    _preferences(prefs, "missing-saved.ijx")
    first, last, middle = tmp_path / "first.pgm", tmp_path / "last.pgm", tmp_path / "middle.ijx"
    first.write_bytes(b"P2\n2 2\n255\n1 2 3 4\n")
    last.write_bytes(b"P2\n2 2\n255\n5 6 7 8\n")
    middle.write_bytes(_frame(_workspace()))
    editor = _editor(preferences=prefs, images=[first, middle, last])
    assert len(editor.document_infos) == 4 and editor.active_index == 3
    assert editor.results == [{"Value": 17}] and editor.workflow[0]["operation"] == "must_never_execute"
    assert editor.startup_state["source"] == "explicit_paths"
    assert editor.live_document(editor.document_infos[0]["id"]).native_snapshot().dataset.plane()[0, 0] == 1
    assert editor.live_document(editor.document_infos[3]["id"]).native_snapshot().dataset.plane()[0, 0] == 5
    with pytest.raises(ij.StartupError):
        _editor(preferences=prefs, images=[first, first.parent / "." / first.name])
    with pytest.raises((ij.StartupError, ValueError)):
        _editor(preferences=prefs, images=[first], workspace=middle)
    with pytest.raises((ij.StartupError, ValueError)):
        _editor(preferences=prefs, workspace=middle, restore_workspace=False)
    _record(tmp_path, "SU04", state=editor.startup_state, order=editor.document_infos,
            results=editor.results, workflow=editor.workflow)


def test_SU05_zero_prefix_and_strict_selected_adoption(tmp_path):
    prefs = tmp_path / "preferences.json"
    _preferences(prefs, "good.ijx")
    good = tmp_path / "good.ijx"
    good.write_bytes(_frame(_workspace()))
    again = tmp_path / "again.ijx"
    again.write_bytes(good.read_bytes())
    config, source = prefs.read_bytes(), good.read_bytes()
    with pytest.raises(ij.StartupError) as failure:
        _editor(preferences=prefs, images=[good, tmp_path / "missing.ijx", again])
    assert failure.value.diagnostic.path == str(tmp_path / "missing.ijx")
    assert prefs.read_bytes() == config and good.read_bytes() == source
    owners = ij.load_session(str(good)).documents
    app = ij.Application()
    for documents, index, error in (([owners[0], object()], 0, TypeError),
                                    (owners, True, TypeError), (owners, 2, (ValueError, IndexError)),
                                    ([owners[0], owners[0]], 0, ValueError)):
        with pytest.raises(error):
            app.adopt_all_selected(documents, index)
        assert app.document_ids == [] and app.active_document_id == 0
    late = _workspace()
    late["documents"][1]["display"]["properties"]["channel_display"] = {"version": 1}
    unsupported = tmp_path / "late-facade.ijx"
    unsupported.write_bytes(_frame(late))
    with pytest.raises(ij.StartupError):
        _editor(preferences=prefs, workspace=unsupported)
    assert prefs.read_bytes() == config
    _record(tmp_path, "SU05", failure_path=failure.value.diagnostic.path,
            document_ids=app.document_ids, active_id=app.active_document_id,
            config_before_hex=config.hex(), config_after_hex=prefs.read_bytes().hex(),
            source_before_hex=source.hex(), source_after_hex=good.read_bytes().hex())


def test_SU06_automatic_recovery_reads_current_bytes(tmp_path):
    prefs = tmp_path / "preferences.json"
    path = tmp_path / "mutable.ijx"
    _preferences(prefs, str(path))
    config = prefs.read_bytes()
    editor = _editor(preferences=prefs)
    _empty(editor)
    assert editor.startup_state["status"] == "failed" and editor.startup_state["retry_allowed"] is True
    missing = deepcopy(editor.startup_state)
    path.write_bytes(b"IMAGEJXX3\ntruncated")
    editor.retry_startup()
    _empty(editor)
    truncated = deepcopy(editor.startup_state)
    automatic_refusals = []
    for kind in range(2):
        invalid = _workspace()
        if kind == 0:
            invalid["documents"][1]["display"]["properties"]["channel_display"] = {"version": 1}
        else:
            invalid["extensions"]["imagejxx_workspace"]["active_index"] = 9
        path.write_bytes(_frame(invalid))
        refused_bytes = path.read_bytes()
        refused_editor = _editor(preferences=prefs)
        _empty(refused_editor)
        assert refused_editor.startup_state["status"] == "failed"
        assert path.read_bytes() == refused_bytes and prefs.read_bytes() == config
        automatic_refusals.append(deepcopy(refused_editor.startup_state))
    original = tmp_path / "deleted-owned-original.pgm"
    original.write_bytes(b"P2\n1 1\n255\n9\n")
    path.write_bytes(_frame(_workspace()))
    original.unlink()
    path.write_bytes(_frame(_workspace(changed=True)))
    current = path.read_bytes()
    editor.retry_startup()
    assert editor.active_index == 0 and len(editor.document_infos) == 2
    actual = _check_samples(editor.live_document(editor.document_infos[1]["id"]).native_snapshot(), 1, changed=True)
    assert prefs.read_bytes() == config and path.read_bytes() == current
    assert editor.startup_state["retry_allowed"] is False
    with pytest.raises(ij.StartupError):
        editor.retry_startup()
    _record(tmp_path, "SU06", missing_state=missing, truncated_state=truncated,
            automatic_refusals=automatic_refusals, loaded_state=editor.startup_state, scientific_samples=actual,
            config_before_hex=config.hex(), config_after_hex=prefs.read_bytes().hex())


def test_SU07_v2_and_reserved_presentation_compatibility(tmp_path):
    documents = [{"title": f"Owned V2 {index}", "is_stack": False, "dtype": "uint8", "shape": [1, 1],
                  "pixels": base64.b64encode(struct.pack("<d", index + 1)).decode("ascii"), "slice_index": 0}
                 for index in range(2)]
    v2 = ij.decode_session(_frame({"format": "imagejxx-session", "version": 2,
                                  "documents": documents, "results": [], "workflow": []}, version=2))
    assert v2.source_version == 2 and ij.workspace_presentation(v2.extensions, 2).active_index == 1
    assert [int(owner.snapshot().dataset.plane()[0, 0]) for owner in v2.documents] == [1, 2]
    known = ij.with_workspace_presentation({"vendor": [1, None]}, 0, 2)
    assert ij.workspace_presentation(known.extensions, 2).active_index == 0
    future = {"imagejxx_workspace": {"version": 37, "opaque": [True, None]}, "vendor": "retained"}
    decoded = ij.workspace_presentation(future, 2)
    assert decoded.active_index == 1 and decoded.future_owned and len(decoded.diagnostics) == 1
    assert ij.with_workspace_presentation(future, 0, 2).extensions == future
    for child in ({"version": 1, "active_index": 9}, {"version": 1, "active_index": None},
                  {"version": True, "active_index": 0}):
        with pytest.raises(ij.StartupError):
            ij.workspace_presentation({"imagejxx_workspace": child}, 2)
    _record(tmp_path, "SU07", v2_source_version=v2.source_version,
            v2_active_index=ij.workspace_presentation(v2.extensions, 2).active_index,
            known_active_index=ij.workspace_presentation(known.extensions, 2).active_index,
            future_active_index=decoded.active_index, future_extensions=ij.with_workspace_presentation(future, 0, 2).extensions,
            future_reason=ij.startup_reason_name(decoded.diagnostics[0].reason))


def test_SU08_compound_settings_and_failed_destination(tmp_path):
    prefs = tmp_path / "preferences.json"
    _preferences(prefs)
    editor = _editor(preferences=prefs, restore_workspace=False)
    config, before = prefs.read_bytes(), deepcopy(editor.preferences)
    with pytest.raises((ValueError, ij.StartupError)):
        editor.update_settings(startup_mode="workspace", startup_workspace="", shortcut_bindings=["analysis.measure=Ctrl+L"])
    after_invalid = deepcopy(editor.preferences)
    assert after_invalid == before and prefs.read_bytes() == config
    editor.update_settings(startup_mode="workspace", startup_workspace="next.ijx", persist=False)
    assert editor.settings["startup_workspace"] == "next.ijx" and prefs.read_bytes() == config
    blocked = tmp_path / "blocked"
    blocked.write_bytes(b"file parent")
    failed = _editor(preferences=blocked / "preferences.json", restore_workspace=False)
    failed_before = deepcopy(failed.preferences)
    with pytest.raises((OSError, RuntimeError, ValueError)):
        failed.update_settings(startup_mode="workspace", startup_workspace="good.ijx")
    assert failed.preferences == failed_before and blocked.read_bytes() == b"file parent"
    exported = ij.PreferencesDocument.parse(json.dumps(editor.preferences), recover=False)
    assert json.loads(exported.unknown_fields_json) == {"vendor_root": [None, 17]}
    _record(tmp_path, "SU08", before=before, after_invalid=after_invalid,
            local_applied=editor.preferences, failed_before=failed_before, failed_after=failed.preferences,
            source_before_hex=config.hex(), source_after_hex=prefs.read_bytes().hex())


def test_SU09_recovery_backup_and_startup_reset(tmp_path):
    path = tmp_path / "malformed.json"
    path.write_bytes(b"{broken original\x00bytes")
    original = path.read_bytes()
    recovered = ij.PreferencesDocument.load(path)
    assert recovered.write_blocked and recovered.settings["startup_mode"] == "empty"
    assert recovered.save(path) is False and path.read_bytes() == original
    assert recovered.save(path, explicit_save=True) is True
    backup = path.with_name(path.name + ".bak")
    assert backup.read_bytes() == original
    prefs = tmp_path / "preferences.json"
    _preferences(prefs, "missing.ijx")
    retained = ij.PreferencesDocument.load(prefs, recover=False)
    before = json.loads(retained.json())
    retained.settings.reset("Startup")
    after = json.loads(retained.json())
    assert after["values"]["startup_mode"] == "empty" and after["values"]["startup_workspace"] == ""
    assert after["extras"] == before["extras"] and after["values"]["shortcut_bindings"] == before["values"]["shortcut_bindings"]
    assert after["vendor_root"] == [None, 17]
    for invalid in ({"startup_mode": "future"}, {"startup_workspace": "A\x00B"},
                    {"startup_mode": "workspace", "startup_workspace": ""}):
        envelope = {"format": "imagejxx-preferences", "version": 1, "values": invalid, "extras": {"recent": []}}
        safe = ij.PreferencesDocument.parse(json.dumps(envelope))
        assert safe.write_blocked and safe.settings["startup_mode"] == "empty"
    _record(tmp_path, "SU09", backup_hex=backup.read_bytes().hex(), original_hex=original.hex(),
            reset_before=before, reset_after=after, recovery_write_blocked=recovered.write_blocked)


def test_SU10_layout_validation_and_explicit_reset_api(tmp_path):
    prefs = tmp_path / "preferences.json"
    _preferences(prefs)
    original = prefs.read_bytes()
    editor = _editor(preferences=prefs, restore_workspace=False, reset_layout=True)
    assert editor.startup_state["reset_layout"] is True and prefs.read_bytes() == original
    editor.reset_layout(persist=False)
    assert prefs.read_bytes() == original
    editor.reset_layout(persist=True)
    after = json.loads(prefs.read_bytes())
    assert "layout" not in after["extras"] and after["extras"]["recent"] == ["missing-recent.pgm"]
    actual = []
    for layout in (17, "A\x00B", "x" * (1024*1024 + 1)):
        root = {"format": "imagejxx-preferences", "version": 1, "values": {}, "extras": {"recent": [], "layout": layout}}
        safe = ij.PreferencesDocument.parse(json.dumps(root))
        assert safe.write_blocked and "layout" not in json.loads(safe.extras_json)
        actual.append({"write_blocked": safe.write_blocked, "extras_json": safe.extras_json})
    blocked = tmp_path / "blocked-layout-parent"
    blocked.write_bytes(b"owned blocking file")
    failed = _editor(preferences=blocked / "preferences.json", restore_workspace=False)
    failed_layout_before = deepcopy(failed.startup_state["layout"])
    with pytest.raises((OSError, RuntimeError, ValueError)):
        failed.reset_layout(persist=True)
    assert failed.startup_state["layout"] == failed_layout_before and blocked.read_bytes() == b"owned blocking file"
    _record(tmp_path, "SU10", startup_state=editor.startup_state, before_json=json.loads(original), after_json=after,
            invalid_layout_recovery=actual,
            failed_reset_before=failed_layout_before, failed_reset_after=failed.startup_state["layout"],
            scope="API configuration/reset effects; visible ImGui layout and actual controls require paired UI evidence")
