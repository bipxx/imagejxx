"""Shared threshold defaults, durable round trips and actual host restart input."""
import json
import os
from pathlib import Path
import subprocess
import imagejxx as ij
import numpy as np
import pytest

KEYS = ("threshold_method", "threshold_bright", "threshold_ignore_black", "threshold_ignore_white", "threshold_stack_histogram", "threshold_histogram_roi", "threshold_apply_roi")
METHODS = ("default", "ij_isodata", "huang", "huang2", "renyi_entropy", "shanbhag")

def options(method="default", bright=True, black=False, white=False, pooled=False, histogram_roi=False, apply_roi="active_selection"):
    return dict(zip(KEYS, (method, bright, black, white, pooled, histogram_roi, apply_roi)))

def test_schema_matches_registered_methods_and_defaults():
    row = next(d for d in ij.Settings.schema() if d["name"] == "threshold_method")
    assert row["choices"] == list(ij.THRESHOLD_METHODS)
    assert {k: ij.Settings()[k] for k in KEYS} == options()

@pytest.mark.parametrize("method", ij.THRESHOLD_METHODS)
@pytest.mark.parametrize("bright", [True, False])
@pytest.mark.parametrize("black,white", [(False,False),(True,False),(False,True),(True,True)])
def test_all_method_options_roundtrip_and_group_reset(method, bright, black, white, tmp_path):
    document = ij.PreferencesDocument()
    document.settings.update(options(method, bright, black, white))
    document.settings["theme"] = "light"
    path = tmp_path / "preferences.json"
    assert document.save(path)
    restored = ij.PreferencesDocument.load(path, recover=False)
    assert {k: restored.settings[k] for k in KEYS} == options(method, bright, black, white)
    restored.settings.reset("Threshold")
    assert {k: restored.settings[k] for k in KEYS} == options()
    assert restored.settings["theme"] == "light"

@pytest.mark.parametrize("key,value", [("threshold_method","missing"),("threshold_method",1),("threshold_bright",1),("threshold_ignore_black",0),("threshold_ignore_white","false")])
def test_invalid_options_refuse_atomically(key, value):
    settings = ij.Settings(); before = settings.to_dict()
    with pytest.raises(ValueError): settings.update({"theme":"light",key:value})
    assert settings.to_dict() == before

def seed(path):
    document = {"format":"imagejxx-preferences","version":1,"values":{"future_plugin":{"keep":[1,True,None]}},"extras":{"recent":[],"plugin":{"opaque":"keep"}},"extension":{"version":7}}
    path.write_text(json.dumps(document),encoding="utf-8")
    return document

@pytest.mark.parametrize("context", ["imgui_cpp","imgui_python"])
@pytest.mark.parametrize("method,mode", [("default","dark"),("ij_isodata","both"),("huang","bright"),("huang2","dark"),("renyi_entropy","both"),("shanbhag","bright")])
def test_actual_auto_cancel_and_restart_restore_options(context,method,mode,tmp_path):
    path = tmp_path / "preferences.json"; original = seed(path)
    expected = options(method,mode!="dark",mode=="both",mode=="both")
    def observe(number):
        capture = tmp_path / ("capture"+str(number))
        if context == "imgui_cpp":
            executable = os.environ["IMAGEJXX_CPP_EXE"]
            subprocess.run([executable,"--animation-ui-case","auto-display-"+method+"-"+mode,str(capture)],check=True,timeout=45)
            return json.loads((capture/"case.json").read_bytes())
        from imagejxx import _desktop
        editor = _desktop.Editor(str(path))
        return json.loads(editor._animation_ui_case("auto-display-"+method+"-"+mode,str(capture)))
    first = observe(1)
    assert {k:first["settings_before"]["values"][k] for k in KEYS} == options()
    for phase in ("settings_after_auto","settings_after_cancel"):
        assert {k:first[phase]["values"][k] for k in KEYS} == expected
    saved = json.loads(path.read_bytes())
    assert {k:saved["values"][k] for k in KEYS} == expected
    assert saved["values"]["future_plugin"] == original["values"]["future_plugin"]
    assert saved["extras"] == original["extras"] and saved["extension"] == original["extension"]
    second = observe(2)
    assert {k:second["settings_before"]["values"][k] for k in KEYS} == expected
    opened = next(f["rendered"]["threshold_panel"]["automatic"] for f in second["frames"] if f["rendered"]["threshold_panel"]["visible"])
    assert opened == dict(method=method,bright=mode!="dark",ignore_black=mode=="both",ignore_white=mode=="both",stack_histogram=False)
    assert second["states"]["automatic"]["pixels"] == second["states"]["before"]["pixels"]
    assert not second["states"]["automatic"]["dirty"]
    assert second["states"]["automatic"]["undo"] == 0


def editor_with_plane(path):
    from imagejxx.ui.app import Editor
    from imagejxx.ui.model import Document
    editor = Editor(preferences=path)
    editor.add_document(Document(np.arange(256,dtype=np.uint8).reshape(16,16)))
    editor.action("show_manual_threshold")
    return editor

def test_settings_defaults_feed_automation_and_failed_compute_does_not_save(tmp_path):
    path = tmp_path / "preferences.json"; editor = editor_with_plane(path)
    editor.update_settings(options("huang2",False,True,False))
    before = path.read_bytes(); baseline = editor._editor.native_snapshot(0)
    with pytest.raises(ValueError):editor.action("auto_manual_threshold",method="unknown")
    assert path.read_bytes() == before
    editor.action("auto_manual_threshold")
    snapshot = editor._editor.native_snapshot(0)
    assert snapshot.display.properties["manual_threshold"]["lower"] == 0
    assert {k:editor.settings[k] for k in KEYS} == options("huang2",False,True,False)
    assert snapshot.dirty == baseline.dirty and snapshot.revisions.pixels == baseline.revisions.pixels

@pytest.mark.parametrize("payload", [{"format":"imagejxx-preferences","version":99,"values":{}}, {"format":"imagejxx-preferences","version":1,"values":{"threshold_bright":1}}])
def test_implicit_auto_save_preserves_write_blocked_files(payload,tmp_path):
    path = tmp_path / "preferences.json";path.write_text(json.dumps(payload),encoding="utf-8");before = path.read_bytes()
    editor = editor_with_plane(path); baseline = editor._editor.native_snapshot(0)
    editor.action("auto_manual_threshold",method="huang2",bright=False)
    assert path.read_bytes() == before
    assert {k:editor.settings[k] for k in KEYS} == options("huang2",False)
    assert list(tmp_path.glob("preferences.json.bak*")) == []
    assert editor._editor.native_snapshot(0).dirty == baseline.dirty

def test_save_failure_does_not_publish_display_or_settings(tmp_path):
    path = tmp_path / "preferences.json";editor = editor_with_plane(path)
    # Deterministic cross-platform replace failure: destination is a directory.
    path.mkdir();before = editor._editor.native_snapshot(0);settings = editor.settings.to_dict()
    with pytest.raises((ValueError,RuntimeError,OSError)):
        editor.action("auto_manual_threshold",method="huang2",bright=False)
    after = editor._editor.native_snapshot(0)
    assert after.display.properties == before.display.properties
    assert editor.settings.to_dict() == settings
    assert after.dirty == before.dirty and after.revisions.pixels == before.revisions.pixels
    assert list(tmp_path.glob("preferences.json.tmp.*")) == []


def test_threshold_preferences_are_visible_in_shared_settings_panel(tmp_path):
    editor = editor_with_plane(tmp_path / "preferences.json")
    editor.action("show_preferences", search="threshold")
    rendered = editor.render_headless()
    assert set(rendered["preferences_controls"]) == set(KEYS)
    editor.update_settings(options("shanbhag",False,True,True))
    editor.action("show_manual_threshold")
    editor.action("auto_manual_threshold")
    assert editor.settings["threshold_method"] == "shanbhag"
    assert editor._editor.native_snapshot(0).display.properties["manual_threshold"]["upper"] == 127
