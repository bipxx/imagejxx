"""A01–A08: headless native shortcut API and preferences adapter contracts.

These assertions supply events/context as data. They establish no OS input,
ImGui delivery, editor integration or complete keyboard-family acceptance.
"""

import inspect
import json
import os
from pathlib import Path
import subprocess
import sys

import imagejxx as ij
import pytest


PLATFORMS = (
    ij.ShortcutPlatform.Windows,
    ij.ShortcutPlatform.Linux,
    ij.ShortcutPlatform.MacOS,
)
COMMANDS = (
    "file.open", "file.save", "file.close", "edit.undo", "edit.redo",
    "edit.copy", "edit.cut", "edit.paste", "edit.select_all", "analysis.measure",
    "app.preferences", "app.jobs", "view.channel_display", "image.auto_contrast",
    "image.extract_plane", "view.orthogonal", "filter.gaussian_imagej",
    "filter.rank_imagej",
)
DEFAULTS = {
    "file.open": "Primary+O", "file.save": "Primary+S", "file.close": "Primary+W",
    "edit.undo": "Primary+Z", "edit.redo": "Primary+Y", "edit.copy": "Primary+C",
    "edit.cut": "Primary+X", "edit.paste": "Primary+V",
    "edit.select_all": "Primary+A", "analysis.measure": "M",
}


def envelope(values=None, extras=None, **unknown):
    return {
        "format": "imagejxx-preferences", "version": 1,
        "values": {} if values is None else values,
        "extras": {"recent": []} if extras is None else extras,
        **unknown,
    }


def chord_text(chord):
    return None if chord is None else ij.format_shortcut_chord(chord)


def _a01_installed_origins(envelope_name, source_package, *, require_loaded):
    """Stdlib-only guard also carried into the isolated installed consumer."""
    import hashlib
    from importlib.machinery import EXTENSION_SUFFIXES

    def canonical_path(value):
        assert type(value) is str and value, "Origin path must be a string"
        path = Path(value)
        assert path.is_absolute() and not path.is_symlink()
        resolved = path.resolve(strict=True)
        assert str(resolved) == value, "Origin paths must be canonical and absolute"
        return resolved

    def read_bounded(path, maximum):
        assert path.is_file() and not path.is_symlink()
        size = path.stat().st_size
        assert 0 < size <= maximum, "Origin file exceeds its bound"
        with path.open("rb") as stream:
            data = stream.read(size + 1)
        assert len(data) == size, "Origin file changed while reading"
        return data

    def checked_pin(pin):
        assert type(pin) is dict and set(pin) == {"path", "bytes", "sha256"}
        assert type(pin["bytes"]) is int and 0 < pin["bytes"] <= 64 * 1048576
        digest = pin["sha256"]
        assert type(digest) is str and len(digest) == 64
        assert all(value in "0123456789abcdef" for value in digest)
        path = canonical_path(pin["path"])
        data = read_bounded(path, 64 * 1048576)
        actual = {"path": str(path), "bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest()}
        assert actual == pin, "Installed origin bytes differ from approved pin"
        return actual

    def unique_object(pairs):
        value = {}
        for key, item in pairs:
            assert key not in value, "Duplicate origin JSON key"
            value[key] = item
        return value

    def nonfinite(token):
        raise ValueError(f"Nonfinite origin token: {token}")

    envelope_path = canonical_path(envelope_name)
    raw = read_bounded(envelope_path, 1048576)
    origin = json.loads(raw, object_pairs_hook=unique_object, parse_constant=nonfinite)
    assert type(origin) is dict and set(origin) == {
        "schema_version", "kind", "package_root", "modules", "numpy",
    }
    assert type(origin["schema_version"]) is int and origin["schema_version"] == 1
    assert origin["kind"] == "p06h_shortcuts_python_runtime_origins_v1"
    package = canonical_path(origin["package_root"])
    forbidden_source = Path(source_package).resolve(strict=True)
    assert package.is_dir() and package.name == "imagejxx"
    assert not package.is_relative_to(forbidden_source.parent), "Source bootstrap refused"
    assert type(origin["modules"]) is dict and set(origin["modules"]) == {"imagejxx", "_native"}
    modules = {role: checked_pin(pin) for role, pin in origin["modules"].items()}
    assert Path(modules["imagejxx"]["path"]) == package / "__init__.py"
    native = Path(modules["_native"]["path"])
    assert native.parent == package and any(
        native.name == "_native" + suffix for suffix in EXTENSION_SUFFIXES)
    numpy = origin["numpy"]
    assert type(numpy) is dict and set(numpy) == {"site_packages", "version", "module"}
    assert numpy["version"] == "2.2.6"
    numpy_site = canonical_path(numpy["site_packages"])
    assert numpy_site.is_dir() and not numpy_site.is_relative_to(forbidden_source.parent)
    numpy_pin = checked_pin(numpy["module"])
    assert Path(numpy_pin["path"]) == numpy_site / "numpy" / "__init__.py"
    assert "imagejxx._desktop" not in sys.modules, "Headless carrier imported desktop"
    assert not any(name == "imagejxx.ui" or name.startswith("imagejxx.ui.") for name in sys.modules)
    assert not any(name == "imgui_bundle" or name.startswith("imgui_bundle.") for name in sys.modules)
    if require_loaded:
        for role, name in (("imagejxx", "imagejxx"), ("_native", "imagejxx._native")):
            module_path = Path(sys.modules[name].__file__).resolve(strict=True)
            assert str(module_path) == modules[role]["path"], "Loaded package origin differs"
        assert str(Path(sys.modules["numpy"].__file__).resolve(strict=True)) == numpy_pin["path"]
        assert sys.modules["numpy"].__version__ == numpy["version"]
    return {"package_root": str(package), "modules": modules,
            "numpy": {"site_packages": str(numpy_site), "version": numpy["version"], "module": numpy_pin},
            "origin_envelope": {"path": str(envelope_path), "bytes": len(raw),
                                "sha256": hashlib.sha256(raw).hexdigest()}}


def _configuration_capture():
    """Optional detached transport; absence supplies no configuration proof."""
    import hashlib

    name = os.environ.get("IMAGEJXX_SHORTCUTS_PYTHON_OBSERVATIONS")
    if name is None:
        return None
    directory = Path(name)
    assert directory.is_absolute() and not directory.is_symlink() and directory.is_dir()
    assert str(directory.resolve(strict=True)) == name
    origin_name = os.environ.get("IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS")
    assert origin_name, "Configuration transport requires installed-origin pins"
    source = Path(__file__).resolve(strict=True)
    source_size = source.stat().st_size
    assert 0 < source_size <= 1048576
    with source.open("rb") as stream:
        source_bytes = stream.read(source_size + 1)
    assert len(source_bytes) == source_size
    source_pin = {"path": str(source), "bytes": len(source_bytes),
                  "sha256": hashlib.sha256(source_bytes).hexdigest()}
    installed = _a01_installed_origins(
        origin_name, str(source.parents[2] / "src" / "imagejxx"), require_loaded=True)
    runtime = {"python": {"executable": str(Path(sys.executable).resolve(strict=True)),
                          "version": sys.version, "process_id": os.getpid()},
               "installed_origins": installed}
    return directory, source_pin, runtime


def _write_configuration_capture(capture, case_id, values):
    if capture is None:
        return
    assert case_id in {"test_A05_settings_inherit_clear_reset", "test_A07_recovery_import_export_atomic"}
    after = _configuration_capture()
    assert after == capture, "Configuration source/runtime origins changed"
    directory, source, runtime = capture
    rows = []
    for name, value in values.items():
        assert type(value) in (list, str)
        if type(value) is list:
            assert all(type(item) is str for item in value)
        rows.append({"id": name, "encoding": "canonical_json" if type(value) is list else "scalar",
                     "value": value})
    raw = {"schema_version": 1, "producer_id": "p06h.shortcuts.configuration.python.raw.001",
           "context": "python_api", "case_id": case_id, "complete": True,
           "source": source, "runtime_before": runtime, "runtime_after": after[2],
           "observations": rows}
    data = json.dumps(raw, ensure_ascii=False, indent=2, allow_nan=False).encode("utf-8") + b"\n"
    assert 0 < len(data) <= 1048576
    with (directory / (case_id + ".json")).open("xb") as stream:
        stream.write(data)


def test_A01_base_registry_no_desktop():
    origin_name = os.environ.get("IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS")
    assert origin_name, "Required installed-origin envelope: IMAGEJXX_SHORTCUTS_PYTHON_ORIGINS"
    source_package = str((Path(__file__).resolve().parents[2] / "src" / "imagejxx").resolve(strict=True))
    before = _a01_installed_origins(origin_name, source_package, require_loaded=True)
    child = "import json, sys, sysconfig\nfrom pathlib import Path\n" + inspect.getsource(_a01_installed_origins) + """
assert 'imagejxx' not in sys.modules and 'imagejxx._native' not in sys.modules
approved = _a01_installed_origins(sys.argv[1], sys.argv[2], require_loaded=False)
site = str(Path(approved['package_root']).parent)
numpy_site = approved['numpy']['site_packages']
base = Path(sys.base_prefix).resolve(strict=True)
stdlib = Path(sysconfig.get_path('stdlib')).resolve(strict=True)
platstdlib = Path(sysconfig.get_path('platstdlib')).resolve(strict=True)
zip_name = 'python%d%d.zip' % sys.version_info[:2]
stdlib_paths = {base, base / 'DLLs', base / zip_name, stdlib, platstdlib,
                stdlib / 'lib-dynload', platstdlib / 'lib-dynload', stdlib.parent / zip_name}
sys.path[:] = list(dict.fromkeys([site, numpy_site] + [value for value in sys.path
    if value and Path(value).resolve() in stdlib_paths]))
import imagejxx as ij
before = _a01_installed_origins(sys.argv[1], sys.argv[2], require_loaded=True)
assert not any(name == 'imagejxx.ui' or name.startswith('imagejxx.ui.') for name in sys.modules)
assert not any(name == 'imgui_bundle' or name.startswith('imgui_bundle.') for name in sys.modules)
assert 'imagejxx._desktop' not in sys.modules
assert 'shortcut_registry_version' in ij.__all__
assert 'ShortcutProfileError' in ij.__all__
registry = [ij.shortcut_registry_version, [c.id for c in ij.shortcut_commands()]]
after = _a01_installed_origins(sys.argv[1], sys.argv[2], require_loaded=True)
assert before == after == approved
print(json.dumps({'registry': registry, 'origins': after}, allow_nan=False))
"""
    probe = subprocess.run(
        [sys.executable, "-I", "-B", "-c", child, origin_name, source_package],
        check=True, capture_output=True, text=True, timeout=30,
    )
    child_actual = json.loads(probe.stdout)
    assert child_actual["registry"] == [1, list(COMMANDS)]
    assert child_actual["origins"] == before
    registry = ij.shortcut_commands()
    assert [c.id for c in registry] == list(COMMANDS)
    assert all(c.label for c in registry)
    assert {c.id: chord_text(c.default_chord) for c in registry if c.default_chord} == DEFAULTS
    assert {c.id for c in registry if c.scope == ij.ShortcutScope.Shell} == {
        "file.open", "app.preferences", "app.jobs",
    }
    assert _a01_installed_origins(origin_name, source_package, require_loaded=True) == before


def test_A02_chord_platform_literals():
    chord = ij.parse_shortcut_chord("  CtrlOrCmd+shift+LeftBracket\t")
    assert chord.key == ij.ShortcutKey.LeftBracket
    assert chord.modifiers == ij.ShortcutModifiers.Primary | ij.ShortcutModifiers.Shift
    assert chord_text(chord) == "Primary+Shift+LeftBracket"
    assert [chord_text(ij.resolve_shortcut_chord(chord, p)) for p in PLATFORMS] == [
        "Ctrl+Shift+LeftBracket", "Ctrl+Shift+LeftBracket", "Shift+Super+LeftBracket",
    ]
    assert chord_text(ij.parse_shortcut_chord("Control+K")) == "Ctrl+K"
    for alias in ("Cmd+K", "Command+K", "Meta+K"):
        assert chord_text(ij.parse_shortcut_chord(alias)) == "Super+K"
    for invalid in ("", "Ctrl+", "Ctrl+Ctrl+K", "Primary+Ctrl+K", "A+B", "Ctrl+F25"):
        with pytest.raises(ValueError):
            ij.parse_shortcut_chord(invalid)
    profile = ij.ShortcutProfile()
    assert profile.override_lines() == []
    for p, primary in zip(PLATFORMS, ("Ctrl", "Ctrl", "Super")):
        table = {v.command_id: chord_text(v.chord) for v in profile.bindings(p)}
        assert table == {c: DEFAULTS.get(c, None).replace("Primary", primary)
                         if c in DEFAULTS else None for c in COMMANDS}


def test_A03_strict_types_atomic_bad_profile():
    profile = ij.compile_shortcuts(["analysis.measure=Primary+K"])
    before = profile.override_lines()
    for invalid in ("analysis.measure=K", ("analysis.measure=K",), [b"analysis.measure=K"], [True], [1]):
        with pytest.raises(TypeError):
            ij.compile_shortcuts(invalid)
    for invalid in (True, 1, b"M", None):
        with pytest.raises(TypeError):
            ij.parse_shortcut_chord(invalid)
    for invalid in (True, 0, "Windows", ij.ShortcutFocus.Canvas):
        with pytest.raises(TypeError):
            profile.bindings(invalid)
    with pytest.raises(TypeError):
        ij.ShortcutChord(key=1)
    with pytest.raises(TypeError):
        ij.ShortcutChord(modifiers=1)
    with pytest.raises(ValueError):
        ij.ShortcutChord(modifiers=ij.ShortcutModifiers.Primary | ij.ShortcutModifiers.Ctrl)
    for invalid in (1, "true", None):
        with pytest.raises(TypeError):
            ij.ShortcutEvent(ctrl=invalid)
        with pytest.raises(TypeError):
            ij.ShortcutContext(window_focused=invalid)
    for invalid in (True, 1.0, "1"):
        with pytest.raises(TypeError):
            ij.ShortcutLimits(max_override_entries=invalid)
    for invalid in (0, -1, 1025, 1 << 100):
        with pytest.raises(ValueError):
            ij.ShortcutLimits(max_override_entries=invalid)
    for invalid in ({"file.open": 1}, {True: True}, {"future.tool": True}, []):
        with pytest.raises((TypeError, ValueError)):
            ij.ShortcutContext(availability=invalid)
    with pytest.raises(ij.ShortcutProfileError) as caught:
        ij.assign_shortcut(profile, "analysis.measure", "Primary+S")
    conflict = caught.value.diagnostics
    assert len(conflict) == 1
    assert conflict[0].kind == ij.ShortcutDiagnosticKind.Conflict
    assert {conflict[0].command_id, conflict[0].other_command_id} == {"file.save", "analysis.measure"}
    assert conflict[0].platforms == list(PLATFORMS)
    assert {conflict[0].chord, conflict[0].other_chord} == {"Primary+S"}
    with pytest.raises(ij.ShortcutProfileError) as caught:
        ij.compile_shortcuts(["app.preferences=Ctrl+O"])
    assert caught.value.diagnostics[0].platforms == list(PLATFORMS[:2])
    with pytest.raises(ij.ShortcutProfileError) as caught:
        ij.compile_shortcuts(["app.preferences=Alt+F4"])
    assert any(d.kind == ij.ShortcutDiagnosticKind.Reserved and PLATFORMS[0] in d.platforms
               for d in caught.value.diagnostics)
    with pytest.raises(ValueError):
        ij.assign_shortcut(profile, "analysis.measure", "K", ij.ShortcutLimits(max_native_working_bytes=1))
    assert profile.override_lines() == before
    assert chord_text(profile.chord("analysis.measure", PLATFORMS[0])) == "Ctrl+K"


def test_A04_detached_values():
    incoming = ["analysis.measure=Primary+K", "future.tool=opaque future v1"]
    profile = ij.compile_shortcuts(incoming)
    incoming.clear()
    lines = profile.override_lines()
    lines.clear()
    inactive = profile.inactive_rows()
    inactive.append("future.other=anything")
    diagnostics = profile.diagnostics()
    diagnostics[0].platforms.append(PLATFORMS[0])
    diagnostics.clear()
    table = profile.bindings(PLATFORMS[0])
    table[0].chord.key = ij.ShortcutKey.K
    table.clear()
    value = profile.chord("analysis.measure", PLATFORMS[0])
    value.key = ij.ShortcutKey.S
    commands = ij.shortcut_commands()
    commands[0].default_chord.key = ij.ShortcutKey.K
    commands.clear()
    assert profile.override_lines() == ["analysis.measure=Primary+K", "future.tool=opaque future v1"]
    assert profile.inactive_rows() == ["future.tool=opaque future v1"]
    assert profile.diagnostics()[0].platforms == []
    assert chord_text(profile.chord("file.open", PLATFORMS[0])) == "Ctrl+O"
    assert chord_text(profile.chord("analysis.measure", PLATFORMS[0])) == "Ctrl+K"
    assert chord_text(ij.shortcut_commands()[0].default_chord) == "Primary+O"
    context = ij.ShortcutContext(availability={"file.open": True})
    detached = context.availability
    detached["file.open"] = False
    assert context.availability == {"file.open": True}
    result = ij.assign_shortcut(profile, "analysis.measure", "Primary+K")
    assert result.status == ij.ShortcutEditStatus.NoOp and result.bindings_changed is False
    assert result.profile.override_lines() == profile.override_lines()
    assert profile.working_bytes() > 0


def test_A05_settings_inherit_clear_reset():
    capture = _configuration_capture()
    settings = ij.Settings()
    default_profile = ij.compile_shortcuts(settings["shortcut_bindings"])
    configuration_values = {
        "default.setting": settings["shortcut_bindings"],
        "default.current": settings["shortcut_bindings"],
        "default.effective": default_profile.override_lines(),
        "default.effect": chord_text(default_profile.chord("analysis.measure", PLATFORMS[0])),
    }
    assert settings["shortcut_bindings"] == []
    schema = next(s for s in settings.schema() if s["name"] == "shortcut_bindings")
    assert schema["group"] == "Shortcuts" and schema["default"] == []
    settings["shortcut_bindings"] = ["future.tool=opaque future v1", "analysis.measure= control+shift+k "]
    assert settings["shortcut_bindings"] == ["analysis.measure=Ctrl+Shift+K", "future.tool=opaque future v1"]
    before = settings.to_dict()
    with pytest.raises(ij.ShortcutProfileError) as caught:
        settings.update({"theme": "light", "shortcut_bindings": ["analysis.measure=Primary+S"]})
    assert caught.value.diagnostics[0].kind == ij.ShortcutDiagnosticKind.Conflict
    assert settings.to_dict() == before
    for invalid in (True, "analysis.measure=K", [7], ["analysis.measure=K", "analysis.measure=M"]):
        with pytest.raises(ValueError):
            settings["shortcut_bindings"] = invalid
        assert settings.to_dict() == before
    detached = settings["shortcut_bindings"]
    detached.clear()
    assert settings.to_dict() == before
    profile = ij.ShortcutProfile()
    explicit = ij.assign_shortcut(profile, "analysis.measure", "M")
    assert explicit.status == ij.ShortcutEditStatus.Changed and explicit.bindings_changed is False
    assert explicit.profile.override_lines() == ["analysis.measure=M"]
    cleared = ij.clear_shortcut(explicit.profile, "analysis.measure")
    assert cleared.profile.override_lines() == ["analysis.measure="]
    assert cleared.profile.chord("analysis.measure", PLATFORMS[0]) is None
    settings["shortcut_bindings"] = cleared.profile.override_lines()
    assert ij.compile_shortcuts(settings["shortcut_bindings"]).chord("analysis.measure", PLATFORMS[0]) is None
    reset = ij.reset_shortcut(cleared.profile, "analysis.measure")
    assert reset.profile.override_lines() == [] and reset.bindings_changed is True
    assert chord_text(reset.profile.chord("analysis.measure", PLATFORMS[0])) == "M"
    assert ij.reset_shortcuts(ij.compile_shortcuts(before["shortcut_bindings"])).profile.override_lines() == []
    settings["theme"] = "light"
    settings.reset("Shortcuts")
    assert settings["shortcut_bindings"] == [] and settings["theme"] == "light"
    before_reset = settings.to_dict()
    with pytest.raises(ValueError):
        settings.reset("Not a settings group")
    assert settings.to_dict() == before_reset
    settings.reset()
    assert settings["shortcut_bindings"] == [] and settings["theme"] == "dark"
    migrated, warnings = ij.Settings.migrate({"shortcut_bindings": ["analysis.measure= ctrl+K "]})
    assert warnings == [] and migrated["shortcut_bindings"] == ["analysis.measure=Ctrl+K"]
    migrated, warnings = ij.Settings.migrate({"shortcut_bindings": ["analysis.measure=Primary+S"], "theme": "light"})
    assert len(warnings) == 1 and migrated["shortcut_bindings"] == [] and migrated["theme"] == "light"
    _write_configuration_capture(capture, "test_A05_settings_inherit_clear_reset", configuration_values)


def test_A06_preferences_future_roundtrip(tmp_path):
    future = ["future.second=opaque v2", "future.first=Ctrl+?future"]
    payload = envelope(
        {"shortcut_bindings": [future[0], "analysis.measure= ctrl+shift+k ", future[1]],
         "future_setting": {"nested": [True, None]}},
        {"recent": [], "shortcut_bindings": {"inactive": ["analysis.measure=Primary+S"]}},
        future_root={"keep": "opaque"},
    )
    document = ij.PreferencesDocument.parse(json.dumps(payload), recover=False)
    assert document.settings["shortcut_bindings"] == ["analysis.measure=Ctrl+Shift+K", *future]
    assert document.warnings == [] and document.write_blocked is False
    assert json.loads(document.unknown_values_json) == {"future_setting": {"nested": [True, None]}}
    assert json.loads(document.extras_json) == payload["extras"]
    assert json.loads(document.unknown_fields_json) == {"future_root": {"keep": "opaque"}}
    profile = ij.compile_shortcuts(document.settings["shortcut_bindings"])
    assert profile.inactive_rows() == future
    assert [d.command_id for d in profile.diagnostics()] == ["future.second", "future.first"]
    assert all(d.kind == ij.ShortcutDiagnosticKind.InactiveUnknown for d in profile.diagnostics())
    path = tmp_path / "preferences.json"
    assert document.save(path) is True
    reopened = ij.PreferencesDocument.load(path, recover=False)
    assert json.loads(reopened.json()) == json.loads(document.json())
    missing = ij.PreferencesDocument.parse(json.dumps(envelope()), recover=False)
    assert missing.settings["shortcut_bindings"] == []
    legacy = ij.PreferencesDocument.parse(
        json.dumps({"shortcut_bindings": ["analysis.measure= ctrl+k "], "plugin": {"keep": True}}),
        recover=False,
    )
    assert legacy.settings["shortcut_bindings"] == ["analysis.measure=Ctrl+K"]
    assert json.loads(legacy.extras_json)["plugin"] == {"keep": True}
    assert json.loads(legacy.unknown_values_json) == {}


def test_A07_recovery_import_export_atomic(tmp_path):
    capture = _configuration_capture()
    path = tmp_path / "preferences.json"
    original = json.dumps(envelope({"shortcut_bindings": ["analysis.measure=Primary+S"], "theme": "light"})).encode()
    path.write_bytes(original)
    active = ij.PreferencesDocument.parse(json.dumps(envelope({"shortcut_bindings": ["analysis.measure=Primary+K"]})), recover=False)
    changed_profile = ij.compile_shortcuts(active.settings["shortcut_bindings"])
    configuration_values = {
        "changed.setting": active.settings["shortcut_bindings"],
        "changed.current": active.settings["shortcut_bindings"],
        "changed.effective": changed_profile.override_lines(),
        "changed.effect": chord_text(changed_profile.chord("analysis.measure", PLATFORMS[0])),
    }
    active_bytes = active.json()
    for invalid in (["analysis.measure=Primary+S"], ["analysis.measure=Ctrl+"], ["analysis.measure=K", "analysis.measure=M"]):
        with pytest.raises(ValueError):
            ij.PreferencesDocument.parse(json.dumps(envelope({"shortcut_bindings": invalid})), recover=False)
        assert active.json() == active_bytes
    document = ij.PreferencesDocument.load(path, recover=True)
    assert len(document.warnings) == 1 and document.write_blocked is True
    assert document.settings["shortcut_bindings"] == [] and document.settings["theme"] == "light"
    assert document.save(path) is False and path.read_bytes() == original
    assert not Path(str(path) + ".bak").exists()
    document.settings["shortcut_bindings"] = ["analysis.measure=Primary+K"]
    assert document.save(path, explicit_save=True) is True
    assert Path(str(path) + ".bak").read_bytes() == original
    reopened = ij.PreferencesDocument.load(path, recover=False)
    persisted_profile = ij.compile_shortcuts(reopened.settings["shortcut_bindings"])
    configuration_values.update({
        "persisted.setting": document.settings["shortcut_bindings"],
        "persisted.current": reopened.settings["shortcut_bindings"],
        "persisted.effective": persisted_profile.override_lines(),
        "persisted.effect": chord_text(persisted_profile.chord("analysis.measure", PLATFORMS[0])),
        "persisted.reopened_setting": reopened.settings["shortcut_bindings"],
        "persisted.file_utf8": path.read_bytes().decode("utf-8", errors="strict"),
    })
    assert reopened.settings["shortcut_bindings"] == ["analysis.measure=Primary+K"]
    assert reopened.warnings == [] and reopened.write_blocked is False
    assert document.write_blocked is True  # Saving a detached document is const.
    invalid_export = ij.PreferencesDocument.parse(active_bytes, recover=False)
    invalid_export.unknown_values_json = "[]"
    destination_before = path.read_bytes()
    with pytest.raises(ValueError):
        invalid_export.save(path)
    assert path.read_bytes() == destination_before
    not_directory = tmp_path / "file"
    not_directory.write_bytes(b"keep")
    with pytest.raises(OSError):
        active.save(not_directory / "preferences.json")
    assert not_directory.read_bytes() == b"keep" and active.json() == active_bytes
    assert list(tmp_path.glob("*.tmp.*")) == []
    _write_configuration_capture(capture, "test_A07_recovery_import_export_atomic", configuration_values)


def test_A08_focus_admission_literals():
    profile = ij.ShortcutProfile()
    context = ij.ShortcutContext(ij.ShortcutFocus.Canvas, availability={"analysis.measure": True, "file.open": True})
    measure = ij.ShortcutEvent(ij.ShortcutKey.M, down_edge=True)
    evaluate = lambda event=measure: ij.evaluate_shortcut(profile, PLATFORMS[0], event, context)
    decision = evaluate()
    assert (decision.command_id, decision.reason, decision.disposition, decision.consumed()) == (
        "analysis.measure", ij.ShortcutDecisionReason.Dispatch, ij.ShortcutDisposition.GlobalCommand, True,
    )
    context.availability = {}
    assert evaluate().reason == ij.ShortcutDecisionReason.Unavailable
    context.availability = {"analysis.measure": True, "file.open": True}
    for focus, reason in (
        (ij.ShortcutFocus.None_, ij.ShortcutDecisionReason.NoWorkspace),
        (ij.ShortcutFocus.Text, ij.ShortcutDecisionReason.Control),
        (ij.ShortcutFocus.Control, ij.ShortcutDecisionReason.Control),
        (ij.ShortcutFocus.Modal, ij.ShortcutDecisionReason.Modal),
        (ij.ShortcutFocus.Capture, ij.ShortcutDecisionReason.Capture),
    ):
        context.focus = focus
        assert evaluate().reason == reason and evaluate().command_id is None
    context.focus = ij.ShortcutFocus.Canvas
    for attribute, reason in (
        ("window_focused", ij.ShortcutDecisionReason.Unfocused),
        ("rearmed", ij.ShortcutDecisionReason.Rearming),
    ):
        setattr(context, attribute, False)
        assert evaluate().reason == reason
        setattr(context, attribute, True)
    for attribute, reason in (
        ("gesture_active", ij.ShortcutDecisionReason.Gesture),
        ("local_key_claimed", ij.ShortcutDecisionReason.LocalClaim),
    ):
        setattr(context, attribute, True)
        assert evaluate().reason == reason and evaluate().consumed() is True
        setattr(context, attribute, False)
    assert evaluate(ij.ShortcutEvent(ij.ShortcutKey.M)).reason == ij.ShortcutDecisionReason.NoEdge
    assert evaluate(ij.ShortcutEvent(ij.ShortcutKey.M, down_edge=True, repeat=True)).reason == ij.ShortcutDecisionReason.Repeat
    assert evaluate(ij.ShortcutEvent(ij.ShortcutKey.M, shift=True, down_edge=True)).reason == ij.ShortcutDecisionReason.Unbound
    context.focus = ij.ShortcutFocus.ROIManager
    local = evaluate(ij.ShortcutEvent(ij.ShortcutKey.A, ctrl=True, down_edge=True))
    assert local.reason == ij.ShortcutDecisionReason.ROIManagerSelectAll and local.consumed() is True
    assert local.command_id is None
    context.focus = ij.ShortcutFocus.Canvas
    ambiguous = ij.evaluate_shortcuts(profile, PLATFORMS[0], [
        measure, ij.ShortcutEvent(ij.ShortcutKey.O, ctrl=True, down_edge=True),
    ], context)
    assert ambiguous.reason == ij.ShortcutDecisionReason.Ambiguous and ambiguous.command_id is None
    assert ij.evaluate_shortcuts(profile, PLATFORMS[0], [measure, measure], context).command_id == "analysis.measure"
    for invalid in ((measure,), ["M"], [True]):
        with pytest.raises(TypeError):
            ij.evaluate_shortcuts(profile, PLATFORMS[0], invalid, context)
    for platform, flags in ((PLATFORMS[0], {"ctrl": True}), (PLATFORMS[1], {"ctrl": True}), (PLATFORMS[2], {"super": True})):
        event = ij.ShortcutEvent(ij.ShortcutKey.O, down_edge=True, **flags)
        assert ij.evaluate_shortcut(profile, platform, event, context).command_id == "file.open"
