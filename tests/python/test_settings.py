"""Shared native settings, migration, consumed defaults and ImGui preferences."""

import csv
import json
import os
from pathlib import Path
import re

import imagejxx as ij
import numpy as np
import pytest
from imagejxx.ui.model import Document, Selection
from imagejxx.ui.persistence import Preferences


@pytest.fixture
def editor(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor

    return Editor(preferences=Preferences(tmp_path / "preferences.json"))


def test_schema_defaults_and_atomic_updates():
    settings = ij.Settings()
    schema = ij.Settings.schema()
    assert ij.Settings.schema_version == 1
    assert len({d["name"] for d in schema}) == len(schema)
    assert settings.to_dict() == {d["name"]: d["default"] for d in schema}
    settings.update({"theme": "light", "brush_width": 14})
    before = settings.to_dict()
    with pytest.raises(ValueError):
        settings.update({"brush_width": 20, "history_limit": 0})
    assert settings.to_dict() == before
    settings["foreground"] = "#AA33CC"
    assert settings["foreground"] == "#aa33cc"
    settings.reset("Drawing")
    assert settings["brush_width"] == 8 and settings["theme"] == "light"
    with pytest.raises(ValueError):
        settings.reset("not a group")
    snapshot = settings.to_dict()
    snapshot["measurements"].append("mean")
    assert settings["measurements"] == []


@pytest.mark.parametrize(
    "key,value",
    [
        ("history_limit", True),
        ("history_limit", 2**80),
        ("history_limit", 3.0),
        ("ui_scale", 0.49),
        ("ui_scale", np.nan),
        ("ui_scale", np.inf),
        ("theme", "unknown"),
        ("show_rois", 1),
        ("brush_width", -1),
        ("foreground", "white"),
        ("foreground", "#zzzzzz"),
        ("wand_connectivity", 6),
        ("wand_tolerance", -1),
        ("profile_samples", 1),
        ("measurements", ["mean", "mean"]),
        ("measurements", ["  "]),
        ("measurement_precision", 16),
        ("unknown_key", 1),
    ],
)
def test_rejected_values(key, value):
    settings = ij.Settings()
    before = settings.to_dict()
    with pytest.raises(ValueError):
        settings[key] = value
    assert settings.to_dict() == before


def test_legacy_and_unknown_preferences_roundtrip(tmp_path):
    path = tmp_path / "legacy.json"
    path.write_text(
        json.dumps(
            {
                "brush_width": 14,
                "history_limit": 3,
                "foreground": "red",
                "background": "#03A",
                "plugin_data": {"keep": [1, 2]},
                "recent": ["a.npy"],
            }
        )
    )
    preferences = Preferences(path)
    assert preferences.values["point_size"] == 14
    assert preferences.values["history_limit"] == 3
    assert preferences.values["foreground"] == "#ff0000"
    assert preferences.values["background"] == "#0033aa"
    assert preferences.warnings == []
    preferences.save()
    document = json.loads(path.read_text())
    assert document["format"] == "imagejxx-preferences" and document["version"] == 1
    assert document["extras"]["plugin_data"] == {"keep": [1, 2]}
    document["values"]["future_setting"] = {"opaque": True}
    document["future_metadata"] = [1, "keep"]
    document["extras"]["theme"] = {"extension_value": True}
    path.write_text(json.dumps(document))
    reopened = Preferences(path)
    reopened.values["animation_fps"] = 12
    reopened.save()
    saved = json.loads(path.read_text())
    assert saved["values"]["future_setting"] == document["values"]["future_setting"]
    assert saved["future_metadata"] == document["future_metadata"]
    assert saved["extras"]["theme"] == document["extras"]["theme"]
    assert saved["values"]["animation_fps"] == 12


def test_preference_save_preserves_unrelated_temporary_file(tmp_path):
    preferences = Preferences(tmp_path / "preferences.json")
    unrelated = tmp_path / "preferences.tmp"
    unrelated.write_text("another application's data")
    preferences.save()
    assert unrelated.read_text() == "another application's data"
    assert list(tmp_path.glob("preferences.json.tmp.*")) == []
    assert Preferences(preferences.path).warnings == []


@pytest.mark.parametrize(
    "content",
    [
        '{"history_limit":0,"brush_width":12}',
        "not json",
        '{"format":"imagejxx-preferences","version":99,"values":{}}',
    ],
)
def test_recovery_preserves_original_and_backs_up_on_save(tmp_path, content):
    path = tmp_path / "bad.json"
    path.write_text(content)
    preferences = Preferences(path)
    assert preferences.warnings
    assert path.read_text() == content
    if content.startswith('{"history_limit"'):
        assert preferences.values["brush_width"] == 12
    preferences.save()
    assert Path(str(path) + ".bak").read_text() == content
    assert Preferences(path).warnings == []


def test_invalid_save_is_atomic(tmp_path):
    preferences = Preferences(tmp_path / "settings.json")
    preferences.save()
    before = preferences.path.read_bytes()
    preferences.values["ui_scale"] = -1
    with pytest.raises(ValueError):
        preferences.save()
    assert preferences.path.read_bytes() == before


def test_native_and_python_files_share_settings(editor, tmp_path):
    legacy = tmp_path / "legacy-colors.json"
    legacy.write_text('{"foreground":"red","background":"#03A"}')
    editor.action("import_preferences", path=str(legacy))
    assert editor.settings["foreground"] == "#ff0000"
    assert editor.settings["background"] == "#0033aa"
    editor.update_settings(
        theme="light",
        ui_scale=1.5,
        brush_width=12,
        point_size=5,
        foreground="#0033aa",
        history_limit=3,
    )
    preferences = Preferences(tmp_path / "preferences.json")
    assert preferences.values["theme"] == "light"
    assert preferences.values["brush_width"] == 12
    assert preferences.values["point_size"] == 5
    preferences.values["animation_fps"] = 11
    preferences.save()
    editor.action("import_preferences", path=str(preferences.path))
    assert editor.settings["animation_fps"] == 11
    assert editor.settings["brush_width"] == 12
    before = editor.settings.to_dict()
    data = preferences.path.read_bytes()
    with pytest.raises(ValueError):
        editor.update_settings(brush_width=20, history_limit=0)
    assert editor.settings.to_dict() == before
    assert preferences.path.read_bytes() == data
    snapshot = editor.settings
    snapshot["brush_width"] = 30
    assert editor.settings["brush_width"] == 12
    exported = tmp_path / "exported.json"
    editor.action("export_preferences", path=str(exported))
    assert Preferences(exported).values["animation_fps"] == 11
    editor.action("reset_preferences", group="Drawing")
    assert editor.settings["brush_width"] == 8
    assert editor.settings["theme"] == "light"


def test_native_consumption_of_colors_text_profiles_and_results(editor, tmp_path):
    editor.update_settings(
        foreground="#ff0000",
        background="#0000ff",
        text_size=23,
        profile_samples=5,
        measurements=["mean", "Image"],
        measurement_precision=2,
        persist=False,
    )
    editor.add_document(Document(np.zeros((4, 5, 3), np.uint8)))
    editor.action("fill")
    editor.wait()
    np.testing.assert_array_equal(editor.document.data, np.tile([255, 0, 0], (4, 5, 1)))
    editor.action("clear")
    editor.wait()
    np.testing.assert_array_equal(editor.document.data, np.tile([0, 0, 255], (4, 5, 1)))
    editor.action("text", x=1, y=1, text="label")
    assert editor.document.overlays[-1].font_size == 23
    editor.select(Selection("line", [(0, 0), (4, 0)]))
    editor.action("profile")
    assert editor.render_headless()["plot_samples"] == 5
    editor.results = [{"mean": 1 / 3, "Image": "sample", "maximum": 10}]
    assert editor.render_headless()["results_columns"] == ["mean", "Image"]
    path = tmp_path / "results.csv"
    editor.action("save_results", path=str(path))
    with path.open(newline="") as stream:
        assert list(csv.reader(stream)) == [["mean", "Image"], ["0.33", "sample"]]
    assert editor.results[0]["mean"] == 1 / 3


def test_wand_matches_native_settings(editor):
    source = np.full((3, 3), 20.0, np.float64)
    source[0, 0] = source[1, 1] = 10
    source[2, 2] = 11
    editor.add_document(Document(source))
    for connectivity, tolerance, count in [(4, 1, 1), (8, 0, 2), (8, 1, 3)]:
        expected = ij.wand_mask(source, 0, 0, tolerance, connectivity)
        assert np.count_nonzero(expected) == count
        editor.update_settings(
            wand_connectivity=connectivity, wand_tolerance=tolerance, persist=False
        )
        editor.action("wand", x=0, y=0)
        np.testing.assert_array_equal(editor.document.selection.mask((3, 3)), expected)
    with pytest.raises(IndexError):
        ij.wand_mask(source, -1, 0)


def test_pdf_consumes_preferences_and_accepts_explicit_overrides(editor, tmp_path):
    editor.add_document(Document(np.zeros((180, 240), np.uint8)))
    editor.update_settings(print_dpi=72, print_landscape=True, persist=False)

    def page(path, **overrides):
        editor.action("export_pdf", path=str(path), **overrides)
        content = path.read_bytes()
        box = re.search(rb"/MediaBox \[([^]]+)\]", content)
        image = re.search(rb"([\d.]+) 0 0 ([\d.]+) [\d.]+ [\d.]+ cm", content)
        assert box is not None and image is not None
        return [float(v) for v in box[1].split()], [float(v) for v in image.groups()]

    assert page(tmp_path / "defaults.pdf") == ([0, 0, 792, 612], [240, 180])
    assert page(tmp_path / "override.pdf", dpi=144, landscape=False) == (
        [0, 0, 612, 792],
        [120, 90],
    )


def test_undo_budget_and_runtime_step_limit(editor):
    editor.update_settings(history_limit=16, history_memory_mb=1, persist=False)
    editor.add_document(Document(np.zeros((128, 256), np.uint16)))
    live = editor.live_document()
    for _ in range(12):
        live.apply_operation("add", {"value": 1}, asynchronous=False)
    assert live.info["undo_steps"] == 4
    assert live.info["history_pixel_bytes"] == 1024 * 1024
    before = live.info
    editor.update_settings(history_limit=2, persist=False)
    assert live.info["undo_steps"] == 2
    assert live.info["history_pixel_bytes"] == 512 * 1024
    assert live.info["revision"] == before["revision"]
    np.testing.assert_array_equal(live.plane(), 12)
    editor.action("undo")
    np.testing.assert_array_equal(live.plane(), 11)


def test_real_preferences_controls_search_scaling_and_default_zoom(editor, tmp_path):
    editor.update_settings(theme="light", ui_scale=1.5, default_zoom=2, persist=False)
    editor.add_document(Document(np.zeros((10, 10), np.uint8)))
    editor.action("show_preferences", search="dpi")
    before = editor.settings["auto_dpi"]
    info = editor.render_headless(exercise=True)
    path = tmp_path / "preferences.json"
    saved = json.loads(path.read_text(encoding="utf-8"))
    assert saved["format"] == "imagejxx-preferences" and saved["version"] == 1
    assert saved["values"]["auto_dpi"] is not before
    assert saved["values"] == editor.settings.to_dict()
    assert info["preferences_controls"] == ["auto_dpi", "print_dpi"]
    assert editor.settings["auto_dpi"] is not before
    assert info["gui_scale"] == 1.5
    # ImGui's baked default font rounds the requested pixel size down.
    assert info["font_size"] == pytest.approx(19)
    assert info["ordinary_zoom"] == 2
    assert min(info["window_background"]) > 0.8

    from imagejxx.ui.app import Editor

    reopened = Editor(preferences=Preferences(path))
    assert reopened.settings.to_dict() == saved["values"]
    reopened.action("show_preferences", search="dpi")
    displayed = reopened.render_headless()
    assert displayed["preferences_controls"] == ["auto_dpi", "print_dpi"]
    assert displayed["settings"]["auto_dpi"] is saved["values"]["auto_dpi"]
    # Clicking the freshly displayed checkbox and Apply must invert the saved
    # value. A draft initialized from defaults would keep the loaded value.
    applied = reopened.render_headless(exercise=True)
    resaved = json.loads(path.read_text(encoding="utf-8"))
    assert applied["settings"]["auto_dpi"] is before
    assert reopened.settings["auto_dpi"] is before
    assert resaved["format"] == "imagejxx-preferences" and resaved["version"] == 1
    assert resaved["values"]["auto_dpi"] is before


def test_python_launch_preferences_gestures_persist_and_reopen(editor, tmp_path):
    from imagejxx.ui.app import Editor

    def verify_interaction(info, before):
        interaction = info["preferences_interaction"]
        for phase, expected in (
            ("before", before),
            ("changed_draft", not before),
            ("applied", not before),
        ):
            observation = interaction[phase]
            assert observation["visible"] is True
            assert observation["toggle_name"] == "auto_dpi"
            assert observation["toggle_value"] is expected
            assert observation["controls"] == ["auto_dpi", "print_dpi"]
        assert interaction["before"]["settings_auto_dpi"] is before
        assert interaction["changed_draft"]["settings_auto_dpi"] is before
        assert interaction["applied"]["settings_auto_dpi"] is not before
        events = interaction["input_events"]
        assert {event["type"] for event in events} == {
            "position",
            "button",
            "key",
            "text",
        }
        assert all(
            isinstance(event.get("target"), str) and event["target"]
            for event in events
        )
        assert [event["value"] for event in events if event["type"] == "text"] == [
            "dpi"
        ]

    path = tmp_path / "preferences.json"
    before = editor.settings["auto_dpi"]
    assert editor.render_headless()["preferences_controls"] == []
    first = editor.render_headless(exercise_preferences=True)
    saved = json.loads(path.read_text(encoding="utf-8"))
    assert saved["format"] == "imagejxx-preferences" and saved["version"] == 1
    assert saved["values"]["auto_dpi"] is not before
    assert saved["values"] == editor.settings.to_dict() == first["settings"]
    verify_interaction(first, before)

    reopened = Editor(preferences=Preferences(path))
    assert reopened.settings.to_dict() == saved["values"]
    assert reopened.render_headless()["preferences_controls"] == []
    second = reopened.render_headless(exercise_preferences=True)
    resaved = json.loads(path.read_text(encoding="utf-8"))
    assert resaved["format"] == "imagejxx-preferences" and resaved["version"] == 1
    assert resaved["values"]["auto_dpi"] is before
    assert resaved["values"] == reopened.settings.to_dict() == second["settings"]
    verify_interaction(second, saved["values"]["auto_dpi"])


def test_bad_native_file_is_not_overwritten_on_launch(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor

    path = tmp_path / "bad.json"
    content = '{"format":"imagejxx-preferences","version":99,"values":{}}'
    path.write_text(content)
    editor = Editor(preferences=Preferences(path))
    assert "version" in editor.error.lower()
    editor.render_headless()
    assert path.read_text() == content
    editor.update_settings(theme="classic")
    assert Path(str(path) + ".bak").read_text() == content
    assert Preferences(path).values["theme"] == "classic"


def test_portable_default_path_is_shared(tmp_path, monkeypatch):
    monkeypatch.setenv("IMAGEJXX_CONFIG_DIR", str(tmp_path))
    preferences = Preferences()
    assert preferences.path == tmp_path / "preferences.json"
    preferences.values["line_width"] = 3
    preferences.save()
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    from imagejxx.ui.app import Editor

    assert Editor().settings["line_width"] == 3


@pytest.mark.skipif(
    os.environ.get("IMAGEJXX_RENDER_TESTS") != "1",
    reason="Set IMAGEJXX_RENDER_TESTS=1 for hidden OpenGL",
)
def test_settings_in_hidden_opengl_and_recreated_context(editor, tmp_path):
    editor.update_settings(
        theme="classic",
        ui_scale=1.25,
        interpolation="linear",
        canvas_background="#103050",
        persist=False,
    )
    document = Document(np.arange(60, dtype=np.uint16).reshape(3, 4, 5), is_stack=True)
    editor.add_document(document)
    editor.live_document().set_view(orthogonal=True, crosshair=[2, 1, 1])
    editor.action("show_preferences", search="interpolation")
    screenshot = tmp_path / "settings.png"
    assert editor.run(hidden=True, frames=3, screenshot=screenshot) == 0
    assert screenshot.stat().st_size > 1000
    editor.update_settings(ui_scale=1.5, persist=False)
    assert editor.run(hidden=True, frames=3) == 0
