"""SU01/04/07/08/09/10 headless native policy; no editor/launch/UI credit."""

import json
from pathlib import Path

import imagejxx as ij
import pytest


def envelope(values=None, extras=None, **unknown):
    return {
        "format": "imagejxx-preferences", "version": 1,
        "values": {} if values is None else values,
        "extras": {"recent": []} if extras is None else extras,
        **unknown,
    }


def request(tmp_path, **changes):
    fields = {
        "launch_cwd": str(tmp_path / "launch"),
        "preferences_write_path": str(tmp_path / "config" / "preferences.json"),
    }
    fields.update(changes)
    return ij.StartupRequest(**fields)


def configured():
    preferences = ij.PreferencesDocument()
    preferences.settings.update({
        "startup_mode": "workspace", "startup_workspace": "saved.ijx",
    })
    return preferences


def test_su01_defaults_recent_and_empty_presentation(tmp_path):
    preferences = ij.PreferencesDocument.parse(json.dumps(envelope(
        extras={"recent": ["must-not-open.ijx"]},
    )), recover=False)
    plan = ij.plan_startup(preferences, request(tmp_path))
    assert plan.source == ij.StartupSource.Empty
    assert plan.paths == [] and plan.diagnostics == []
    assert preferences.settings["startup_mode"] == "empty"
    assert preferences.settings["startup_workspace"] == ""
    presentation = ij.with_workspace_presentation({}, None, 0)
    assert presentation.extensions == {
        "imagejxx_workspace": {"version": 1, "active_index": None},
    }
    assert presentation.diagnostics == []
    parsed = ij.workspace_presentation(presentation.extensions, 0)
    assert parsed.active_index is None and parsed.supported


def test_su04_pure_precedence_config_parent_and_cwd(tmp_path):
    preferences = configured()
    startup = request(tmp_path)
    plan = ij.plan_startup(preferences, startup)
    assert plan.source == ij.StartupSource.PersistedWorkspace
    assert plan.paths == [str(tmp_path / "config" / "saved.ijx")]
    startup.explicit_paths = ["first.pgm", "sub/../second.ijx"]
    plan = ij.plan_startup(preferences, startup)
    assert plan.source == ij.StartupSource.ExplicitPaths
    assert plan.paths == [str(tmp_path / "launch" / "first.pgm"),
                          str(tmp_path / "launch" / "second.ijx")]
    assert plan.ignored_persisted_reason == "explicit_paths"
    detached = plan.paths
    detached.append("not-a-live-plan-edit")
    assert len(plan.paths) == 2
    startup.restore_workspace = ij.StartupOverride.Suppress
    assert ij.plan_startup(preferences, startup).source == ij.StartupSource.ExplicitPaths
    startup.explicit_workspace = "chosen.ijx"
    with pytest.raises(ij.StartupError) as caught:
        ij.plan_startup(preferences, startup)
    assert caught.value.diagnostic.reason == ij.StartupReason.InvalidRequest
    startup.explicit_paths = []
    with pytest.raises(ij.StartupError):
        ij.plan_startup(preferences, startup)
    startup.restore_workspace = ij.StartupOverride.Inherit
    plan = ij.plan_startup(preferences, startup)
    assert plan.source == ij.StartupSource.ExplicitWorkspace
    assert plan.paths == [str(tmp_path / "launch" / "chosen.ijx")]
    startup.explicit_workspace = None
    startup.restore_workspace = ij.StartupOverride.Suppress
    startup.reset_layout = True
    plan = ij.plan_startup(preferences, startup)
    assert plan.source == ij.StartupSource.Empty and plan.paths == []
    assert plan.reset_layout and plan.ignored_persisted_reason == "suppressed"
    startup.restore_workspace = ij.StartupOverride.RequireConfigured
    with pytest.raises(ij.StartupError) as caught:
        ij.plan_startup(ij.PreferencesDocument(), startup)
    diagnostic = caught.value.diagnostic
    assert diagnostic.stage == ij.StartupStage.Planning
    assert diagnostic.reason == ij.StartupReason.WorkspaceNotConfigured
    assert diagnostic.recoverable and len(diagnostic.detail.encode()) <= 1024
    assert ij.startup_stage_name(diagnostic.stage) == "planning"
    assert ij.startup_reason_name(diagnostic.reason) == "workspace_not_configured"


@pytest.mark.parametrize("changes,reason", [
    ({"explicit_paths": ["same.pgm", "sub/../same.pgm"]}, "DuplicatePath"),
    ({"explicit_paths": ["a\x00b"]}, "InvalidPath"),
    ({"explicit_paths": ["a" * 4097]}, "PathLimitExceeded"),
    ({"explicit_paths": [f"{i}.pgm" for i in range(17)]}, "PathLimitExceeded"),
    ({"explicit_workspace": ""}, "InvalidPath"),
    ({"launch_cwd": "relative"}, "InvalidRequest"),
])
def test_su04_bounded_native_refusals(tmp_path, changes, reason):
    with pytest.raises(ij.StartupError) as caught:
        ij.plan_startup(configured(), request(tmp_path, **changes))
    assert caught.value.diagnostic.reason == getattr(ij.StartupReason, reason)
    assert len(caught.value.diagnostic.path.encode()) <= 4096
    assert len(caught.value.diagnostic.detail.encode()) <= 1024


@pytest.mark.parametrize("field,value", [
    ("explicit_paths", "file.pgm"), ("explicit_paths", ("file.pgm",)),
    ("explicit_paths", [Path("file.pgm")]), ("explicit_paths", [1]),
    ("explicit_workspace", Path("workspace.ijx")),
    ("explicit_workspace", False), ("restore_workspace", 0),
    ("restore_workspace", False), ("restore_workspace", "Inherit"),
    ("reset_layout", 1), ("reset_layout", None),
    ("launch_cwd", Path("launch")), ("preferences_write_path", b"path"),
])
def test_strict_native_request_constructor_and_property_boundaries(field, value):
    with pytest.raises(TypeError):
        ij.StartupRequest(**{field: value})
    startup = ij.StartupRequest()
    before = getattr(startup, field)
    with pytest.raises(TypeError):
        setattr(startup, field, value)
    assert getattr(startup, field) == before


@pytest.mark.parametrize("value", [True, False, 0, -1, 17, 1.0, "1", 2**100])
def test_strict_native_limits(value):
    limits = ij.StartupLimits()
    with pytest.raises((TypeError, ValueError)):
        limits.max_explicit_paths = value
    assert limits.max_explicit_paths == 16


def test_su07_presentation_missing_supported_and_future_opaque():
    missing = ij.workspace_presentation({}, 2)
    assert missing.active_index == 1 and not missing.supported
    assert missing.diagnostics[0].reason == ij.StartupReason.MissingPresentation
    current = {"imagejxx_workspace": {"version": 1, "active_index": 0,
                                       "future_hint": {"keep": [True, None]}},
               "unrelated": [1, "opaque"]}
    parsed = ij.workspace_presentation(current, 2)
    assert parsed.active_index == 0 and parsed.supported and not parsed.future_owned
    updated = ij.with_workspace_presentation(current, 1, 2)
    assert updated.extensions == {
        **current, "imagejxx_workspace": {**current["imagejxx_workspace"], "active_index": 1},
    }
    future = {"imagejxx_workspace": {"version": 2, "active_index": "future opaque"},
              "unrelated": {"v": [True, None, 2**63]}}
    parsed = ij.workspace_presentation(future, 2)
    assert parsed.active_index == 1 and parsed.future_owned and not parsed.supported
    assert parsed.diagnostics[0].reason == ij.StartupReason.FuturePresentation
    updated = ij.with_workspace_presentation(future, 0, 2)
    assert updated.extensions == future
    assert updated.diagnostics[0].reason == ij.StartupReason.FuturePresentation
    # Generic SessionV3 serialization never interprets this reserved child.
    opaque = {"imagejxx_workspace": "deliberately malformed for explicit consumer"}
    capture = ij.SessionCapture([], extensions=opaque)
    decoded = ij.decode_session(ij.encode_session(capture))
    assert decoded.extensions == opaque


@pytest.mark.parametrize("child", [
    None, "bad", {}, {"version": 1}, {"version": 0, "active_index": 0},
    {"version": 1.0, "active_index": 0}, {"version": True, "active_index": 0},
    {"version": 1, "active_index": 2}, {"version": 1, "active_index": -1},
    {"version": 1, "active_index": None}, {"version": 1, "active_index": True},
    {"version": 1, "active_index": 0.0},
])
def test_su07_malformed_recognized_presentation_refuses(child):
    with pytest.raises(ij.StartupError) as caught:
        ij.workspace_presentation({"imagejxx_workspace": child}, 2)
    assert caught.value.diagnostic.stage == ij.StartupStage.Presentation
    assert caught.value.diagnostic.reason == ij.StartupReason.InvalidPresentation


@pytest.mark.parametrize("count", [True, -1, 1.0, "2", 2**100])
def test_strict_presentation_counts(count):
    with pytest.raises((TypeError, ValueError)):
        ij.workspace_presentation({}, count)


def test_su08_su09_complete_pair_atomic_patch_reset_and_native_migration():
    schema = {item["name"]: item for item in ij.Settings.schema()}
    assert schema["startup_workspace"]["kind"] == "text"
    assert schema["startup_workspace"]["group"] == "Startup"
    settings = ij.Settings()
    before = settings.to_dict()
    with pytest.raises(ValueError):
        settings.update({"history_limit": 21, "startup_mode": "workspace"})
    assert settings.to_dict() == before
    settings.update({"startup_mode": "workspace", "startup_workspace": "valid.ijx",
                     "history_limit": 21, "shortcut_bindings": ["future.tool=opaque"]})
    before = settings.to_dict()
    with pytest.raises(ij.ShortcutProfileError) as caught:
        settings.update({"startup_workspace": "changed.ijx",
                         "shortcut_bindings": ["analysis.measure=Primary+S"]})
    assert caught.value.diagnostics and settings.to_dict() == before
    with pytest.raises(ValueError):
        settings.update({"startup_workspace": "", "shortcut_bindings": []})
    assert settings.to_dict() == before
    settings.reset("Startup")
    assert settings["startup_mode"] == "empty" and settings["startup_workspace"] == ""
    assert settings["history_limit"] == 21 and settings["shortcut_bindings"] == ["future.tool=opaque"]
    migrated, warnings = ij.Settings.migrate({"startup_mode": "workspace",
                                               "startup_workspace": "valid.ijx"})
    assert warnings == [] and migrated["startup_mode"] == "workspace"
    for patch in ({"startup_mode": "workspace"},
                  {"startup_mode": "bad", "startup_workspace": "otherwise-valid.ijx"},
                  {"startup_mode": "empty", "startup_workspace": 42}):
        migrated, warnings = ij.Settings.migrate({**patch, "theme": "light"})
        assert len(warnings) == 1
        assert migrated["startup_mode"] == "empty" and migrated["startup_workspace"] == ""
        assert migrated["theme"] == "light"


@pytest.mark.parametrize("value", [True, 12, [], "a\x00b", "x" * 4097, "é" * 2049])
def test_su08_strict_text_storage_and_byte_bound(value):
    settings = ij.Settings()
    before = settings.to_dict()
    with pytest.raises(ValueError):
        settings["startup_workspace"] = value
    assert settings.to_dict() == before


def test_su08_strict_unicode_and_exact_utf8_boundary():
    settings = ij.Settings()
    settings["startup_workspace"] = "é" * 2048
    assert len(settings["startup_workspace"].encode()) == 4096
    with pytest.raises(UnicodeEncodeError):
        settings["startup_workspace"] = "\ud800"
    with pytest.raises(UnicodeEncodeError):
        ij.StartupRequest(explicit_workspace="\ud800")


def test_su09_recovered_pair_blocks_writes_and_preserves_opaque(tmp_path):
    payload = envelope(
        values={"startup_mode": "workspace", "theme": "light", "unknown": {"v": 3}},
        extras={"recent": ["keep.pgm"], "layout": "keep layout", "opaque": [True, None]},
        future={"keep": 2**63},
    )
    text = json.dumps(payload)
    path = tmp_path / "preferences.json"
    path.write_text(text, encoding="utf-8")
    original = path.read_bytes()
    recovered = ij.PreferencesDocument.parse(text)
    assert recovered.write_blocked and recovered.warnings
    assert recovered.settings["startup_mode"] == "empty"
    assert recovered.settings["startup_workspace"] == ""
    assert recovered.settings["theme"] == "light"
    assert json.loads(recovered.extras_json) == payload["extras"]
    assert json.loads(recovered.unknown_values_json) == {"unknown": {"v": 3}}
    assert json.loads(recovered.unknown_fields_json) == {"future": {"keep": 2**63}}
    assert recovered.save(path) is False and path.read_bytes() == original
    with pytest.raises(ValueError):
        ij.PreferencesDocument.parse(text, recover=False)


@pytest.mark.parametrize("layout", [False, 42, {}, "bad\x00layout", "x" * (1024 * 1024 + 1)],
                         ids=["boolean", "integer", "object", "embedded_nul", "oversize_utf8"])
def test_su10_layout_recovery_erases_only_layout(layout):
    payload = envelope(values={"theme": "light"},
                       extras={"recent": ["keep.pgm"], "layout": layout, "opaque": {"v": 3}})
    recovered = ij.PreferencesDocument.parse(json.dumps(payload))
    assert recovered.write_blocked and len(recovered.warnings) == 1
    assert recovered.settings["theme"] == "light"
    assert json.loads(recovered.extras_json) == {"recent": ["keep.pgm"], "opaque": {"v": 3}}
    with pytest.raises(ValueError):
        ij.PreferencesDocument.parse(json.dumps(payload), recover=False)


def test_su10_native_layout_validator_exact_bound_and_strict_types():
    assert ij.validate_preferences_layout("x" * (1024 * 1024)) is None
    assert ij.validate_preferences_layout("é" * (512 * 1024)) is None
    for layout in ("x" * (1024 * 1024 + 1), "bad\x00layout"):
        with pytest.raises(ValueError):
            ij.validate_preferences_layout(layout)
    with pytest.raises(UnicodeEncodeError):
        ij.validate_preferences_layout("\ud800")
    for layout in (False, 12, b"text", []):
        with pytest.raises(TypeError):
            ij.validate_preferences_layout(layout)


def test_su10_requested_layout_limit_and_one_shot_ignore(tmp_path):
    preferences = ij.PreferencesDocument()
    preferences.extras_json = json.dumps({"recent": [], "layout": "12345"})
    limits = ij.StartupLimits()
    limits.max_layout_bytes = 4
    with pytest.raises(ij.StartupError) as caught:
        ij.plan_startup(preferences, request(tmp_path), limits)
    assert caught.value.diagnostic.stage == ij.StartupStage.Validation
    assert caught.value.diagnostic.reason == ij.StartupReason.InvalidRequest
    assert ij.plan_startup(preferences, request(tmp_path, reset_layout=True), limits).reset_layout
