"""Installed public Python regressions for binding-local native categories.

These tests use no desktop/UI imports. The parent qualifies installed origins and
enforces the process deadline; no native exception identity is inferred from text.
"""

import imagejxx as ij
import pytest


CAP_CALLS = ("compile", "assign", "clear", "reset", "reset_all")
EXPECTED_COLLECTION_IDS = tuple(
    "tests/python/test_shortcut_native_category.py::"
    "test_shortcut_working_cap_retains_value_error_and_native_category[" + name + "]"
    for name in CAP_CALLS
) + (
    "tests/python/test_shortcut_native_category.py::test_shortcut_ordinary_errors_and_parser_scope_unchanged",
    "tests/python/test_shortcut_native_category.py::test_shortcut_structured_conflict_remains_unchanged",
)


def chord(profile):
    value = profile.chord("analysis.measure", ij.ShortcutPlatform.Windows)
    return None if value is None else ij.format_shortcut_chord(value)


@pytest.mark.parametrize("call_name", CAP_CALLS, ids=CAP_CALLS)
def test_shortcut_working_cap_retains_value_error_and_native_category(call_name):
    source = ij.compile_shortcuts(["analysis.measure=Primary+K"])
    before_lines = source.override_lines()
    before_chord = chord(source)
    caps = ij.ShortcutLimits(max_native_working_bytes=1)
    calls = {
        "compile": lambda: ij.compile_shortcuts(["analysis.measure=Primary+K"], caps),
        "assign": lambda: ij.assign_shortcut(source, "analysis.measure", "Primary+J", caps),
        "clear": lambda: ij.clear_shortcut(source, "analysis.measure", caps),
        "reset": lambda: ij.reset_shortcut(source, "analysis.measure", caps),
        "reset_all": lambda: ij.reset_shortcuts(source, caps),
    }
    with pytest.raises(ValueError) as caught:
        calls[call_name]()
    error = caught.value
    assert type(error) is ValueError
    assert error.native_category == "length_error"
    assert str(error) == "Shortcut compilation exceeds its logical working-byte limit"
    assert error.args == ("Shortcut compilation exceeds its logical working-byte limit",)
    assert not hasattr(error, "diagnostics")
    assert source.override_lines() == before_lines and chord(source) == before_chord
    assert caps.max_native_working_bytes == 1


def test_shortcut_ordinary_errors_and_parser_scope_unchanged():
    with pytest.raises(TypeError) as caught:
        ij.compile_shortcuts(("analysis.measure=Primary+K",))
    assert type(caught.value) is TypeError
    assert str(caught.value) == "Shortcut override lines must be a list of strings"
    assert not hasattr(caught.value, "native_category")

    with pytest.raises(ValueError) as caught:
        ij.compile_shortcuts(["analysis.measure=Ctrl+"])
    assert type(caught.value) is ValueError
    assert not hasattr(caught.value, "native_category")
    with pytest.raises(ValueError) as caught:
        ij.ShortcutLimits(max_native_working_bytes=0)
    assert type(caught.value) is ValueError
    assert not hasattr(caught.value, "native_category")

    # The fixed-ceiling parser has no per-call limits argument and is outside
    # the five enriched bindings. Its actual native length_error still follows
    # the prior translator, proving there is no module-wide category hook.
    with pytest.raises(ValueError) as caught:
        ij.parse_shortcut_chord("A" * 97)
    assert type(caught.value) is ValueError
    assert str(caught.value) == "Shortcut chord exceeds its owned byte ceiling"
    assert not hasattr(caught.value, "native_category")


def test_shortcut_structured_conflict_remains_unchanged():
    source = ij.ShortcutProfile()
    with pytest.raises(ij.ShortcutProfileError) as caught:
        ij.assign_shortcut(source, "analysis.measure", "Primary+S")
    error = caught.value
    assert type(error) is ij.ShortcutProfileError and isinstance(error, ValueError)
    assert not hasattr(error, "native_category")
    assert str(error) == "Shortcut profile has reserved or conflicting bindings"
    diagnostic, = error.diagnostics
    assert diagnostic.kind == ij.ShortcutDiagnosticKind.Conflict
    assert {diagnostic.command_id, diagnostic.other_command_id} == {"analysis.measure", "file.save"}
    assert diagnostic.chord == diagnostic.other_chord == "Primary+S"
    assert diagnostic.platforms == [ij.ShortcutPlatform.Windows, ij.ShortcutPlatform.Linux, ij.ShortcutPlatform.MacOS]
    assert source.override_lines() == [] and chord(source) == "M"
