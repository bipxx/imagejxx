"""Independent binary_iterations consumers with retained runtime observations."""

import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

import imagejxx as ij
import numpy as np
from numpy.testing import assert_array_equal
import pytest

from imagejxx.ui.model import Document
from imagejxx.ui.persistence import Preferences


FIXTURE_SHA256 = "f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd"
OUTPUT_SHA256 = {
    1: "6a270e5458c86befd340cdd0a693d125898ebbf823d306aa04f4b3a85bea9d2f",
    2: "b949d8e5cbc8dfbc092ce13c77e382935339fd0e76f94d7a4ca6f371cef7d377",
    3: "303431bc1ca0a0e2d91b51b6446f99bdf5098814945a594008abdab4f9a05b86",
}
SCENARIOS = {
    "default": (1, 1, 1),
    "changed": (2, 2, 2),
    "frozen": (2, 3, 2),
    "override": (2, 3, 1),
    "persisted": (2, 2, 2),
}


def _fixtures():
    path = Path(__file__).resolve().parents[1] / "fixtures/binary-imagej154r.json"
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == FIXTURE_SHA256
    cases = json.loads(raw)["cases"]
    selected = {iterations: cases[index] for iterations, index in ((1, 7), (2, 103), (3, 199))}
    for iterations, case in selected.items():
        assert case["operation"] == "erode" and case["iterations"] == iterations
        assert case["shape"] == [7, 9] and case["neighbors"] == 1
        assert case["black_background"] is True and case["pad_edges"] is False
        assert case["source"] == [255] * 63
        expected = np.asarray(case["pixels"], dtype=np.uint8)
        assert hashlib.sha256(expected.tobytes()).hexdigest() == OUTPUT_SHA256[iterations]
        assert np.count_nonzero(expected) == {1: 35, 2: 15, 3: 3}[iterations]
    return selected


def _directory(tmp_path, context, *, create=True):
    selected = os.environ.get("IMAGEJXX_CONSUMER_REPORT_DIR")
    base = Path(selected).resolve() if selected else tmp_path.resolve()
    base.mkdir(parents=True, exist_ok=True)
    directory = base / context.replace("_", "-")
    if create:
        directory.mkdir()  # A repeated gate must choose a fresh report destination.
    else:
        assert not directory.exists(), "Standalone consumer directory must be fresh"
    return directory


def _source(fixtures):
    return np.asarray(fixtures[1]["source"], dtype=np.uint8).reshape((7, 9)).copy()


def _pixels(result, iterations, fixtures):
    assert result.dtype == np.uint8 and result.shape == (7, 9)
    assert_array_equal(result.ravel(), fixtures[iterations]["pixels"])
    assert hashlib.sha256(result.tobytes()).hexdigest() == OUTPUT_SHA256[iterations]
    return result.ravel().tolist()


def _options(options):
    return (
        options.iterations,
        options.neighbors,
        options.black_background,
        options.pad_edges,
    )


def _persistence(path, reopened_value, *, displayed_value=None):
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    envelope = json.loads(text)
    assert envelope["format"] == "imagejxx-preferences" and envelope["version"] == 1
    assert envelope["values"]["binary_iterations"] == reopened_value
    result = {
        "path": str(path.resolve()),
        "bytes_utf8": text,
        "envelope": envelope,
        "reopened_setting_value": reopened_value,
    }
    if displayed_value is not None:
        result["displayed_setting_value"] = displayed_value
    return result


def _report(context, rows):
    return {
        "schema_version": 1,
        "context": context,
        "requirement_id": "G052.binary_erode.iterations",
        "scenarios": rows,
    }


def _write_report(directory, context, rows):
    report = _report(context, rows)
    with (directory / f"{context.replace('_', '-')}.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, allow_nan=False, indent=2)
        stream.write("\n")
    return report


def _assert_report(report, context, fixtures, *, ui=False):
    assert report["schema_version"] == 1 and report["context"] == context
    assert report["requirement_id"] == "G052.binary_erode.iterations"
    rows = report["scenarios"]
    assert [row["id"] for row in rows] == list(SCENARIOS)
    for row in rows:
        wanted = SCENARIOS[row["id"]]
        observed = tuple(row[name] for name in (
            "setting_value", "current_setting_value", "effective_parameter_value"
        ))
        assert all(type(value) is int for value in observed) and observed == wanted
        assert row["source_preserved"] is True
        assert all(type(pixel) is int and pixel in (0, 255) for pixel in row["pixels"])
        _pixels(np.asarray(row["pixels"], dtype=np.uint8).reshape((7, 9)), wanted[2], fixtures)
        if row["id"] == "override":
            assert row["override_value"] == 1
        if row["id"] == "frozen":
            assert row["captured_scenario"] == "changed"
        if ui:
            assert row["history_steps"] == 1 and row["workflow_iterations"] == wanted[2]
            assert row["undo_restored_source"] is True
            assert row["input_events"] and row["api_inputs"]
            for event in row["input_events"]:
                if event["type"] == "pointer":
                    assert set(event) == {"type", "position", "button", "down"}
                    assert len(event["position"]) == 2 and all(map(math.isfinite, event["position"]))
                    assert event["button"] in ("left", "middle", "right")
                    assert type(event["down"]) is bool
                elif event["type"] == "key":
                    assert set(event) == {"type", "key", "down"}
                    assert event["key"] and type(event["down"]) is bool
                else:
                    assert event["type"] == "text" and set(event) == {"type", "text"}
                    assert event["text"].strip()
        if row["id"] == "persisted":
            persisted = row["persistence_file"]
            path = Path(persisted["path"])
            assert path.is_absolute() and path.read_bytes() == persisted["bytes_utf8"].encode("utf-8")
            assert json.loads(persisted["bytes_utf8"]) == persisted["envelope"]
            assert persisted["envelope"]["format"] == "imagejxx-preferences"
            assert persisted["envelope"]["version"] == 1
            assert persisted["envelope"]["values"]["binary_iterations"] == 2
            assert persisted["reopened_setting_value"] == 2
            if ui:
                assert persisted["displayed_setting_value"] == 2


def test_binary_iterations_python_api_consumer(tmp_path):
    fixtures = _fixtures()
    directory = _directory(tmp_path, "python_api")
    settings = ij.Settings()
    captured = settings.binary_options()
    rows = []

    def compute(name, *, override=None, persistence=None):
        source = _source(fixtures)
        original = source.copy()
        before = _options(captured)
        effective = captured.iterations if override is None else override
        arguments = {} if override is None else {"iterations": override}
        result = ij.binary_erode(source, options=captured, **arguments)
        assert_array_equal(source, original)
        assert _options(captured) == before
        row = {
            "id": name,
            "setting_value": captured.iterations,
            "current_setting_value": settings["binary_iterations"],
            "effective_parameter_value": effective,
            "pixels": _pixels(result, effective, fixtures),
            "source_preserved": np.array_equal(source, original),
            "options_preserved": _options(captured) == before,
        }
        if override is not None:
            row["override_value"] = override
        if name == "frozen":
            row["captured_scenario"] = "changed"
        if persistence is not None:
            row["persistence_file"] = persistence
        rows.append(row)

    compute("default")
    settings["binary_iterations"] = 2
    captured = settings.binary_options()
    compute("changed")
    settings["binary_iterations"] = 3
    compute("frozen")
    settings["binary_iterations"] = 2  # Independent override captures two again.
    captured = settings.binary_options()
    settings["binary_iterations"] = 3
    compute("override", override=1)
    path = directory / "preferences.json"
    preferences = ij.PreferencesDocument()
    preferences.settings["binary_iterations"] = 2
    assert preferences.save(path) is True
    reopened = ij.PreferencesDocument.load(path, recover=False)
    settings = reopened.settings
    captured = settings.binary_options()
    compute("persisted", persistence=_persistence(path, settings["binary_iterations"]))
    _assert_report(_write_report(directory, "python_api", rows), "python_api", fixtures)


def _click(point):
    assert len(point) == 2 and all(map(math.isfinite, point))
    # Widget centers can be half pixels; ImGui's observed MousePos is in pixels.
    screen_point = [math.floor(coordinate) for coordinate in point]
    return [
        {"type": "position", "point": screen_point, "space": "screen"},
        {"type": "button", "name": "left", "down": True},
        {"type": "button", "name": "left", "down": False},
    ]


def _typing(point, value):
    return [
        *_click(point),
        {"type": "key", "name": "Ctrl", "down": True},
        {"type": "key", "name": "A", "down": True},
        {"type": "key", "name": "A", "down": False},
        {"type": "key", "name": "Ctrl", "down": False},
        {"type": "text", "value": str(value)},
        {"type": "key", "name": "Enter", "down": True},
        {"type": "key", "name": "Enter", "down": False},
    ]


def _deliver(editor, events, recorded):
    rendered = editor.render_headless(events=events)
    trace = rendered["input_trace"]
    assert len(trace) == len(events)
    for request, observation in zip(events, trace, strict=True):
        kind = request["type"]
        assert observation["type"] == kind
        if kind in ("position", "button"):
            point = observation["mouse_screen"]
            assert len(point) == 2 and all(map(math.isfinite, point))
            if kind == "position":
                assert point == request["point"]
            recorded.append({
                "type": "pointer", "position": point,
                "button": request.get("name", "left"),
                "down": request.get("down", False),
            })
        elif kind == "key":
            recorded.append({"type": "key", "key": request["name"], "down": request["down"]})
        else:
            assert kind == "text"
            recorded.append({"type": "text", "text": request["value"]})
    assert not editor.error
    return rendered


def _field(rendered):
    panel = rendered["preferences_panel"]
    assert panel["open"] and not panel["error"]
    field = panel["fields"]["binary_iterations"]
    assert field["name"] == "binary_iterations" and field["kind"] == "integer"
    assert type(field["draft"]) is int
    return field


def _attach_source(editor, source, inputs):
    owner = ij.Document(ij.Dataset(source, axes="YX"))
    attached = editor.add_native_document(owner)
    assert attached.id == owner.id
    assert attached.info["undo_steps"] == attached.info["redo_steps"] == 0
    assert_array_equal(attached.native_snapshot().dataset.plane(), source)
    inputs.append({
        "method": "add_native_document",
        "parameters": {
            "document_id": owner.id, "axes": "YX",
            "shape": list(source.shape), "dtype": source.dtype.name,
            "source_pixels": source.ravel().tolist(),
        },
    })


def _ui_scenario(name, directory, fixtures):
    from imagejxx.ui.app import Editor

    path = directory / f"{name}-preferences.json"
    source = _source(fixtures)
    original = source.copy()
    editor = Editor(preferences=Preferences(path))
    inputs = [
        {"method": "Editor", "parameters": {"preferences_path": str(path)}},
    ]
    _attach_source(editor, source, inputs)
    events = []
    persistence = None
    observations = {}
    assert editor.settings["binary_iterations"] == 1
    if name != "default":
        inputs.append({"method": "action", "name": "show_preferences", "parameters": {"search": "binary_iterations"}})
        editor.action("show_preferences", search="binary_iterations")
        rendered = editor.render_headless()
        observations["preferences_before"] = rendered["preferences_panel"]
        field = _field(rendered)
        assert field["draft"] == 1
        rendered = _deliver(editor, _typing(field["point"], 2), events)
        observations["preferences_typed"] = rendered["preferences_panel"]
        assert _field(rendered)["draft"] == 2
        assert editor.settings["binary_iterations"] == 1  # Draft alone has no effect.
        rendered = _deliver(editor, _click(rendered["preferences_panel"]["apply_point"]), events)
        observations["preferences_applied"] = rendered["preferences_panel"]
        assert _field(rendered)["draft"] == 2
        assert editor.settings["binary_iterations"] == 2
        persistence = _persistence(path, editor.settings["binary_iterations"])
        assert persistence["envelope"]["values"] == editor.settings.to_dict()
        if name == "persisted":
            saved_bytes = path.read_bytes()  # Retain immediately after actual Apply.
            editor = Editor(preferences=Preferences(path))
            inputs.append({"method": "Editor", "parameters": {"preferences_path": str(path), "fresh_owner": True}})
            _attach_source(editor, source, inputs)
            assert editor.settings["binary_iterations"] == 2
            editor.action("show_preferences", search="binary_iterations")
            inputs.append({"method": "action", "name": "show_preferences", "parameters": {"search": "binary_iterations"}})
            rendered = editor.render_headless()
            observations["preferences_reopened"] = rendered["preferences_panel"]
            displayed = _field(rendered)["draft"]
            assert displayed == 2 and path.read_bytes() == saved_bytes
            persistence = _persistence(path, editor.settings["binary_iterations"], displayed_value=displayed)
    capture_value = editor.settings["binary_iterations"]
    inputs.append({"method": "action", "name": "show_operation", "parameters": {"operation": "binary_erode"}})
    editor.action("show_operation", operation="binary_erode")
    dialog = editor.render_headless()["operation_dialog"]
    captured_parameters = dict(dialog["parameters"])
    observations["captured_parameters"] = captured_parameters
    assert dialog["open"] and captured_parameters == {
        "iterations": capture_value, "neighbors": 1,
        "black_background": True, "pad_edges": False,
    }
    if name in ("frozen", "override"):
        assert capture_value == 2  # The independent override must recapture two.
        editor.update_settings(binary_iterations=3, persist=False)
        inputs.append({"method": "update_settings", "values": {"binary_iterations": 3}, "persist": False})
        dialog = editor.render_headless()["operation_dialog"]
        assert dialog["parameters"] == captured_parameters and dialog["open"]
        observations["retained_parameters"] = dialog["parameters"]
    if name == "override":
        rendered = _deliver(editor, _typing(dialog["controls"]["iterations"]["point"], 1), events)
        dialog = rendered["operation_dialog"]
        assert dialog["parameters"]["iterations"] == 1
        observations["overridden_parameters"] = dialog["parameters"]
    effective = dialog["parameters"]["iterations"]
    assert_array_equal(editor.document.data, original)
    original_snapshot = editor.live_document().native_snapshot()
    editor.recording = True
    inputs.append({"method": "recording", "value": True})
    _deliver(editor, _click(dialog["apply_point"]), events)
    editor.wait()
    result = editor.document.data
    pixels = _pixels(result, effective, fixtures)
    history = editor.live_document().info["undo_steps"]
    steps = editor.workflow
    assert history == 1 and len(steps) == 1
    workflow_iterations = steps[0]["parameters"]["iterations"]
    assert steps[0]["operation"] == "binary_erode" and workflow_iterations == effective
    current_value = editor.settings["binary_iterations"]
    assert_array_equal(source, original)
    assert_array_equal(original_snapshot.dataset.plane(), original)
    editor.action("undo")
    inputs.append({"method": "action", "name": "undo", "parameters": {}})
    restored = np.array_equal(editor.document.data, original)
    assert restored
    row = {
        "id": name, "setting_value": capture_value,
        "current_setting_value": current_value,
        "effective_parameter_value": workflow_iterations,
        "pixels": pixels,
        "source_preserved": (
            np.array_equal(source, original)
            and np.array_equal(original_snapshot.dataset.plane(), original)
        ),
        "input_events": events, "api_inputs": inputs,
        "history_steps": history, "workflow_iterations": workflow_iterations,
        "workflow": steps,
        "undo_restored_source": restored,
        **observations,
    }
    if name == "frozen":
        row["captured_scenario"] = "changed"
    if name == "override":
        row["override_value"] = dialog["parameters"]["iterations"]
    if name in ("changed", "persisted"):
        assert path.read_bytes() == persistence["bytes_utf8"].encode("utf-8")
        row["persistence_file"] = persistence
    return row


def test_binary_iterations_python_ui_consumer(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    fixtures = _fixtures()
    directory = _directory(tmp_path, "imgui_python")
    rows = [_ui_scenario(name, directory, fixtures) for name in SCENARIOS]
    _assert_report(_write_report(directory, "imgui_python", rows), "imgui_python", fixtures, ui=True)


@pytest.mark.skipif(not os.environ.get("IMAGEJXX_CPP_EXE"), reason="Set IMAGEJXX_CPP_EXE to the installed C++ desktop")
def test_binary_iterations_cpp_ui_consumer(tmp_path):
    fixtures = _fixtures()
    directory = _directory(tmp_path, "imgui_cpp", create=False)
    executable = Path(os.environ["IMAGEJXX_CPP_EXE"]).resolve(strict=True)
    result = subprocess.run(
        [str(executable), "--binary-iterations-consumer-test", "--consumer-dir", str(directory)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
    )
    if directory.is_dir():
        (directory / "stdout.txt").write_text(result.stdout, encoding="utf-8")
        (directory / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + "\n" + result.stderr
    report = json.loads((directory / "imgui-cpp.json").read_text(encoding="utf-8"))
    _assert_report(report, "imgui_cpp", fixtures, ui=True)


def test_preferences_reject_image_input_without_mutation(tmp_path):
    pytest.importorskip("imagejxx._desktop")
    from imagejxx.ui.app import Editor

    fixtures = _fixtures()
    source = _source(fixtures)
    path = tmp_path / "guard-preferences.json"
    editor = Editor(preferences=Preferences(path))
    editor.add_document(Document(source))
    editor.action("show_preferences", search="binary_iterations")
    assert _field(editor.render_headless())["draft"] == 1
    settings = editor.settings.to_dict()
    before_pixels = editor.document.data.copy()
    before = editor.live_document().info
    with pytest.raises(ValueError, match="Preferences input requires screen-space coordinates"):
        editor.render_headless(events=[{"type": "position", "point": [3, 2], "space": "image"}])
    assert editor.settings.to_dict() == settings and not path.exists()
    assert editor.live_document().info == before
    assert_array_equal(editor.document.data, before_pixels)
    assert_array_equal(editor.document.data, source)
    assert _field(editor.render_headless())["draft"] == 1
