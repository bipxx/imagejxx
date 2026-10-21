"""A09/A10: finite required Editor/_desktop/_native shortcut API assertions.

These cases create no renderer and deliver no ImGui/OS input. They do not
complete host H01-H08 or paired K01-K10, and never skip a missing desktop module.
"""

import json

import imagejxx as ij
from imagejxx import _native, _desktop  # Required real modules, no skip/fallback.
from imagejxx.ui.app import Editor
from imagejxx.ui.persistence import Preferences
import pytest


PLATFORMS = (ij.ShortcutPlatform.Windows, ij.ShortcutPlatform.Linux, ij.ShortcutPlatform.MacOS)
FUTURE = "future.tool=opaque future v1"
CANONICAL = ["analysis.measure=Primary+K", FUTURE]


def payload(lines):
    return {
        "format": "imagejxx-preferences", "version": 1,
        "values": {"shortcut_bindings": lines, "future_setting": {"keep": [True, None]}},
        "extras": {"recent": [], "vendor": {"keep": "opaque"},
                   "shortcut_bindings": {"inactive": ["analysis.measure=Primary+S"]}},
        "future_root": {"keep": 17},
    }


def profile_value(profile):
    """Read detached native DTOs; all chord interpretation stays native."""
    return {
        "overrides": profile.override_lines(),
        "inactive": profile.inactive_rows(),
        "tables": [[(b.command_id, None if b.chord is None else ij.format_shortcut_chord(b.chord))
                    for b in profile.bindings(p)] for p in PLATFORMS],
        "diagnostics": [(d.kind, d.command_id, d.other_command_id, d.chord,
                         d.other_chord, d.platforms) for d in profile.diagnostics()],
    }


def test_A09_editor_persist_false_reopen(tmp_path):
    path = tmp_path / "preferences.json"
    original = payload([FUTURE])
    path.write_text(json.dumps(original), encoding="utf-8")
    source_bytes = path.read_bytes()
    supplied = Preferences(path)
    supplied.values["shortcut_bindings"] = ["analysis.measure=Primary+K"]
    editor = Editor(preferences=supplied)
    assert editor.shortcut_profile.override_lines() == [FUTURE]  # Path, not unsaved facade values.
    prior = editor.shortcut_profile
    editor.update_settings(shortcut_bindings=[FUTURE, "analysis.measure= ctrlOrCmd+k "], persist=False)
    assert editor.settings["shortcut_bindings"] == CANONICAL
    assert editor.shortcut_profile.override_lines() == CANONICAL
    assert path.read_bytes() == source_bytes
    assert prior.override_lines() == [FUTURE]
    fresh_before_save = Editor(preferences=Preferences(path))
    assert fresh_before_save.shortcut_profile.override_lines() == [FUTURE]
    assert [ij.format_shortcut_chord(fresh_before_save.shortcut_profile.chord("analysis.measure", p))
            for p in PLATFORMS] == ["M", "M", "M"]
    editor.update_settings(shortcut_bindings=CANONICAL, persist=True)
    persisted = json.loads(path.read_text(encoding="utf-8"))
    assert persisted["values"]["shortcut_bindings"] == CANONICAL
    assert persisted["values"]["future_setting"] == original["values"]["future_setting"]
    assert persisted["extras"] == original["extras"]
    assert persisted["future_root"] == original["future_root"]
    fresh_after_save = _desktop.Editor(str(path))
    assert profile_value(fresh_after_save.shortcut_profile()) == profile_value(editor.shortcut_profile)
    assert json.loads(fresh_after_save.settings()) == editor.settings.to_dict()
    assert json.loads(fresh_after_save.preferences()) == editor.preferences
    assert fresh_before_save.shortcut_profile.override_lines() == [FUTURE]  # No watcher/cross-window merge.
    assert not list(tmp_path.glob("preferences.json.tmp.*"))


def test_A10_binding_wrapper_same_profile(tmp_path):
    path = tmp_path / "preferences.json"
    original = payload(CANONICAL)
    path.write_text(json.dumps(original), encoding="utf-8")
    wrapper = Editor(preferences=Preferences(path))
    raw = _desktop.Editor(str(path))
    compiled = _native.compile_shortcuts(CANONICAL)
    assert _native.ShortcutProfileError is ij.ShortcutProfileError
    assert type(wrapper.shortcut_profile) is type(raw.shortcut_profile()) is type(compiled) is ij.ShortcutProfile
    expected = profile_value(compiled)
    assert profile_value(wrapper.shortcut_profile) == profile_value(raw.shortcut_profile()) == expected
    assert expected["overrides"] == CANONICAL and expected["inactive"] == [FUTURE]
    assert [ij.format_shortcut_chord(compiled.chord("analysis.measure", p)) for p in PLATFORMS] == [
        "Ctrl+K", "Ctrl+K", "Super+K",
    ]
    prior_settings = wrapper.settings.to_dict()
    prior_preferences = wrapper.preferences
    prior_bytes = path.read_bytes()
    detached = wrapper.shortcut_profile
    detached.override_lines().clear()
    detached.inactive_rows().clear()
    detached.bindings(PLATFORMS[0])[0].chord.key = ij.ShortcutKey.M
    detached.diagnostics()[0].platforms.append(PLATFORMS[0])
    settings_copy = wrapper.settings
    settings_copy["shortcut_bindings"] = []
    preferences_copy = wrapper.preferences
    preferences_copy["values"]["shortcut_bindings"] = []
    edited = ij.clear_shortcut(detached, "analysis.measure").profile
    assert edited.override_lines() == ["analysis.measure=", FUTURE]
    assert profile_value(detached) == expected
    assert profile_value(wrapper.shortcut_profile) == profile_value(raw.shortcut_profile()) == expected
    assert wrapper.settings.to_dict() == prior_settings and wrapper.preferences == prior_preferences
    assert path.read_bytes() == prior_bytes

    conflicting = {"brush_width": 21, "history_limit": 2,
                   "shortcut_bindings": ["analysis.measure=Primary+S"]}
    failing_calls = (
        lambda: _native.compile_shortcuts(conflicting["shortcut_bindings"]),
        lambda: raw.update_settings(json.dumps(conflicting), True),
        lambda: wrapper.update_settings(conflicting, persist=True),
    )
    for call in failing_calls:
        with pytest.raises(ij.ShortcutProfileError) as caught:
            call()
        assert type(caught.value) is ij.ShortcutProfileError
        diagnostic = caught.value.diagnostics
        assert len(diagnostic) == 1 and diagnostic[0].kind == ij.ShortcutDiagnosticKind.Conflict
        assert {diagnostic[0].command_id, diagnostic[0].other_command_id} == {"file.save", "analysis.measure"}
        assert {diagnostic[0].chord, diagnostic[0].other_chord} == {"Primary+S"}
        assert diagnostic[0].platforms == list(PLATFORMS)
        assert wrapper.settings.to_dict() == json.loads(raw.settings()) == prior_settings
        assert wrapper.preferences == json.loads(raw.preferences()) == prior_preferences
        assert profile_value(wrapper.shortcut_profile) == profile_value(raw.shortcut_profile()) == expected
        assert path.read_bytes() == prior_bytes
    invalid_patches = (
        {"shortcut_bindings": True}, {"shortcut_bindings": "analysis.measure=K"},
        {"shortcut_bindings": [True]}, {"shortcut_bindings": ["analysis.measure=Ctrl+"]},
        {"shortcut_bindings": ["analysis.measure=K", "analysis.measure=M"]},
    )
    for patch in invalid_patches:
        for apply in (lambda: raw.update_settings(json.dumps(patch), True),
                      lambda: wrapper.update_settings(patch, persist=True)):
            with pytest.raises(ValueError):
                apply()
            assert wrapper.settings.to_dict() == json.loads(raw.settings()) == prior_settings
            assert wrapper.preferences == json.loads(raw.preferences()) == prior_preferences
            assert profile_value(wrapper.shortcut_profile) == profile_value(raw.shortcut_profile()) == expected
            assert path.read_bytes() == prior_bytes
