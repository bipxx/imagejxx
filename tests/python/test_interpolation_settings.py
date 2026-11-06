"""Remembered UI options share the native registry and atomic preferences owner."""
import json

import imagejxx as ij
import numpy as np
import pytest
from imagejxx.ui.model import Document, Selection
from imagejxx.ui.persistence import Preferences


INTERVAL = "selection_interpolation_interval"
SMOOTH = "selection_interpolation_smooth"
ADJUST = "selection_interpolation_adjust_to_match"
DEFAULTS = {INTERVAL: 1.0, SMOOTH: False, ADJUST: False}


@pytest.fixture
def editor(tmp_path):
    from imagejxx.ui.app import Editor
    result = Editor(preferences=Preferences(tmp_path / "preferences.json"))
    result.add_document(Document(np.zeros((40, 40), np.uint8)))
    result.select(Selection("freehand", [[3, 3], [13, 3], [13, 13], [3, 13]]))
    return result


@pytest.mark.parametrize("interval", [-3, -0.01, 0.01, 2.7])
@pytest.mark.parametrize("adjust", [False, True])
def test_registry_signed_values_persistence_reset_and_unknown_extras(tmp_path, interval, adjust):
    path = tmp_path / "preferences.json"
    path.write_text(json.dumps({"format": "imagejxx-preferences", "version": 1,
        "values": {**DEFAULTS, "future_setting": [1, "keep"]},
        "extras": {"plugin": {"keep": True}}, "future_metadata": [2, 3]}))
    preferences = Preferences(path)
    preferences.values.update({INTERVAL: interval, SMOOTH: True, ADJUST: adjust})
    preferences.save()
    reloaded = Preferences(path)
    assert {k: reloaded.values[k] for k in DEFAULTS} == {INTERVAL: interval, SMOOTH: True, ADJUST: adjust}
    saved = json.loads(path.read_bytes())
    assert saved["values"]["future_setting"] == [1, "keep"]
    assert saved["extras"]["plugin"] == {"keep": True}
    assert saved["future_metadata"] == [2, 3]
    settings = ij.Settings()
    settings.update({INTERVAL: interval, SMOOTH: True, ADJUST: adjust})
    settings.reset("Selection")
    assert {k: settings[k] for k in DEFAULTS} == DEFAULTS


@pytest.mark.parametrize("key,value", [(INTERVAL, value) for value in
    [0, -0.009, 0.009, True, "2.7", None, float("nan"), float("inf")]] +
    [(SMOOTH, 1), (SMOOTH, "true"), (ADJUST, 0), (ADJUST, None)])
def test_registry_rejects_invalid_values_atomically(key, value):
    settings = ij.Settings()
    before = settings.to_dict()
    with pytest.raises((ValueError, TypeError)):
        settings.update({SMOOTH: True, key: value})
    assert settings.to_dict() == before


def test_legacy_migration_retains_signed_value_and_recovers_invalid_switch(tmp_path):
    path = tmp_path / "legacy.json"
    path.write_text(json.dumps({INTERVAL: -2.7, SMOOTH: 1, "opaque": [True]}))
    preferences = Preferences(path)
    assert preferences.values[INTERVAL] == -2.7 and preferences.values[SMOOTH] is False
    assert preferences.warnings
    preferences.save()
    assert json.loads(path.read_bytes())["extras"]["opaque"] == [True]


@pytest.mark.parametrize("interval", [-2.7, 2.7])
@pytest.mark.parametrize("adjust", [False, True])
def test_action_consumes_remembered_options_and_explicit_override(editor, tmp_path, interval, adjust):
    editor.update_settings({INTERVAL: interval, SMOOTH: True, ADJUST: adjust})
    source = ij.Selection("freehand", [[3, 3], [13, 3], [13, 13], [3, 13]])
    options = ij.SelectionInterpolationOptions()
    options.interval = interval; options.smooth = True; options.adjust_to_match = adjust
    expected = ij.interpolate_selection(source, 40, 40, options)
    editor.action("interpolate_selection"); editor.wait()
    assert not editor.error
    np.testing.assert_allclose(editor.document.selection.points, expected.points)
    editor.action("undo")
    editor.action("interpolate_selection", interval=1, smooth=False, adjust_to_match=False)
    editor.wait()
    assert not editor.error
    assert {k: editor.settings[k] for k in DEFAULTS} == DEFAULTS
    assert {k: Preferences(tmp_path / "preferences.json").values[k] for k in DEFAULTS} == DEFAULTS
    # Scientific kernel defaults remain independent of last-used editor options.
    assert ij.SelectionInterpolationOptions().interval == 1
    assert not ij.SelectionInterpolationOptions().smooth


@pytest.mark.parametrize("options", [{"interval": v} for v in [0, True, "2.7", None]] +
    [{"smooth": 1}, {"adjust_to_match": "true"}, {"unknown": 1}])
def test_invalid_action_never_persists_or_launches(editor, tmp_path, options):
    editor.update_settings(DEFAULTS)
    path = tmp_path / "preferences.json"; before = path.read_bytes()
    points = editor.document.selection.points
    with pytest.raises((ValueError, TypeError)):
        editor.action("interpolate_selection", **options)
    assert not editor.busy
    assert editor.document.selection.points == points
    assert path.read_bytes() == before
    assert {k: editor.settings[k] for k in DEFAULTS} == DEFAULTS


def test_save_failure_precedes_launch_and_does_not_change_last_used(editor, tmp_path):
    editor.update_settings(DEFAULTS)
    path = tmp_path / "preferences.json"; path.unlink(); path.mkdir()
    points = editor.document.selection.points
    with pytest.raises((OSError, RuntimeError)):
        editor.action("interpolate_selection", interval=-2.7, smooth=True, adjust_to_match=True)
    assert not editor.busy and not editor.error
    assert editor.document.selection.points == points
    assert {k: editor.settings[k] for k in DEFAULTS} == DEFAULTS
    assert path.is_dir() and not list(path.iterdir())


@pytest.mark.parametrize("kind", ["point", "multipoint"])
def test_point_noop_never_writes_last_used(editor, tmp_path, kind):
    editor.update_settings({INTERVAL: -2.7, SMOOTH: True, ADJUST: True})
    path = tmp_path / "preferences.json"; before = path.read_bytes()
    editor.select(Selection(kind, [[3, 3], [13, 13]] if kind == "multipoint" else [[3, 3]]))
    editor.action("interpolate_selection", interval=0, smooth=False, adjust_to_match=False)
    editor.action("show_interpolate_selection")
    assert not editor.busy
    assert path.read_bytes() == before
    assert editor.settings[INTERVAL] == -2.7 and editor.settings[SMOOTH]
