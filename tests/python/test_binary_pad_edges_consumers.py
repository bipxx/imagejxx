"""Independent pad-edges consumers and retained native publication observations."""

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

from imagejxx.ui.persistence import Preferences


FIXTURE_SHA256 = "f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd"
SOURCE_SHA256 = "d12449c8124182545ae91924286cc6af13528bcf62a5ddbd5e00b891fffc1b48"
OUTPUT_SHA256 = {
    False: "6a270e5458c86befd340cdd0a693d125898ebbf823d306aa04f4b3a85bea9d2f",
    True: SOURCE_SHA256,
}
HOLE_SOURCE_SHA256 = "1d14fb345445b5854c2e759880ac0ba7898c5c914008c4106d097149a7f4c887"
HOLE_OUTPUT_SHA256 = {
    False: "530aaf55fbf79d301a13d2d2cfe2a5e942c322d5023305b37f79563ef61dd50c",
    True: "cebee3743d548888392464245ace26f212ed847a13cbd36af52d188eeec9fe10",
}
SCENARIOS = {
    "default": (False, False, False),
    "changed": (True, True, True),
    "frozen": (False, True, False),
    "override": (False, False, True),
    "persisted": (True, True, True),
}


def _source(*, hole=False):
    # Owned literal geometry; no fixture/native output supplies expected data.
    values = [0 if hole and y == 3 and x == 4 else 255
              for y in range(7) for x in range(9)]
    raw = bytes(values)
    assert hashlib.sha256(raw).hexdigest() == (HOLE_SOURCE_SHA256 if hole else SOURCE_SHA256)
    return np.frombuffer(raw, dtype=np.uint8).reshape((7, 9)).copy()


def _oracle(pad_edges, *, hole=False):
    assert type(pad_edges) is bool
    # One erosion removes the unpadded exterior border and, for the second
    # fixture, the center hole's complete 3x3 neighborhood. This is a geometric
    # closed form, independent of the implementation's neighborhood loop.
    pixels = [0 if ((not pad_edges and (x in (0, 8) or y in (0, 6)))
                    or (hole and 2 <= y <= 4 and 3 <= x <= 5)) else 255
              for y in range(7) for x in range(9)]
    raw = bytes(pixels)
    expected_hash = (HOLE_OUTPUT_SHA256 if hole else OUTPUT_SHA256)[pad_edges]
    assert hashlib.sha256(raw).hexdigest() == expected_hash
    assert pixels.count(255) == ((54 if pad_edges else 26) if hole else (63 if pad_edges else 35))
    return np.frombuffer(raw, dtype=np.uint8).reshape((7, 9)).copy()


def _fixtures():
    path = Path(__file__).resolve().parents[1] / "fixtures/binary-imagej154r.json"
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == FIXTURE_SHA256
    corpus = json.loads(raw)
    assert corpus["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    selected = {False: corpus["cases"][7], True: corpus["cases"][10]}
    for flag, case in selected.items():
        assert case["operation"] == "erode" and case["shape"] == [7, 9]
        assert type(case["iterations"]) is int and case["iterations"] == 1
        assert type(case["neighbors"]) is int and case["neighbors"] == 1
        assert case["black_background"] is True and case["pad_edges"] is flag
        assert case["source"] == _source().ravel().tolist()
        assert case["pixels"] == _oracle(flag).ravel().tolist()
    assert np.count_nonzero(_oracle(False) != _oracle(True)) == 28
    return selected


def _options(options):
    observed = {"iterations": options.iterations, "neighbors": options.neighbors,
                "black_background": options.black_background, "pad_edges": options.pad_edges}
    assert type(observed["iterations"]) is int and observed["iterations"] == 1
    assert type(observed["neighbors"]) is int and observed["neighbors"] == 1
    assert observed["black_background"] is True and type(observed["pad_edges"]) is bool
    return observed


def _assert_parameters(values, flag):
    assert set(values) == {"iterations", "neighbors", "black_background", "pad_edges"}
    assert type(values["iterations"]) is int and values["iterations"] == 1
    assert type(values["neighbors"]) is int and values["neighbors"] == 1
    assert values["black_background"] is True and values["pad_edges"] is flag


def _assert_byte_pixels(values, expected):
    assert len(values) == 63 and all(type(value) is int and value in (0, 255) for value in values)
    assert values == expected


def _pixels(result, flag, *, hole=False):
    assert result.dtype == np.uint8 and result.shape == (7, 9)
    assert_array_equal(result, _oracle(flag, hole=hole))
    raw = result.tobytes()
    assert hashlib.sha256(raw).hexdigest() == (HOLE_OUTPUT_SHA256 if hole else OUTPUT_SHA256)[flag]
    return {"pixels": result.ravel().tolist(), "pixel_sha256": hashlib.sha256(raw).hexdigest(),
            "foreground": int(np.count_nonzero(result == 255)), "shape": list(result.shape),
            "dtype": result.dtype.name}


def _directory(tmp_path, context, *, create=True):
    base = Path(os.environ.get("IMAGEJXX_CONSUMER_REPORT_DIR", tmp_path)).resolve()
    namespace = base / "binary-pad-edges"
    namespace.mkdir(parents=True, exist_ok=True)
    directory = namespace / context.replace("_", "-")
    if create:
        directory.mkdir()  # Each invocation must select a fresh destination.
    else:
        assert not directory.exists(), "Installed standalone destination must be fresh"
    return directory


def _persistence(path, reopened_value, *, displayed_value=None):
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    envelope = json.loads(text)
    assert envelope["format"] == "imagejxx-preferences" and envelope["version"] == 1
    assert type(reopened_value) is bool
    assert envelope["values"]["binary_pad_edges"] is reopened_value
    result = {"path": str(path.resolve()), "bytes_utf8": text,
              "sha256": hashlib.sha256(raw).hexdigest(), "envelope": envelope,
              "reopened_setting_value": reopened_value}
    if displayed_value is not None:
        assert type(displayed_value) is bool and displayed_value is reopened_value
        result["displayed_setting_value"] = displayed_value
    return result


def _write_report(directory, context, rows, supplementary):
    from imagejxx import _native

    artifacts = {"installed_python_core": Path(_native.__file__)}
    if context == "imgui_python":
        from imagejxx import _desktop

        artifacts["installed_python_ui"] = Path(_desktop.__file__)
    identities = {role: {"path": str(path.resolve(strict=True)),
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                  for role, path in artifacts.items()}
    report = {"schema_version": 1, "context": context,
              "requirement_id": "G052.binary_erode.pad_edges", "scenarios": rows,
              "supplementary": supplementary, "runtime_identities": identities}
    with (directory / f"{context.replace('_', '-')}.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, allow_nan=False, indent=2)
        stream.write("\n")
    return report


def _assert_events(events):
    assert events
    for event in events:
        if event["type"] == "pointer":
            assert set(event) == {"type", "position", "button", "down"}
            assert len(event["position"]) == 2 and all(map(math.isfinite, event["position"]))
            assert event["button"] in ("left", "middle", "right") and type(event["down"]) is bool
        elif event["type"] == "key":
            assert set(event) == {"type", "key", "down"}
            assert event["key"] and type(event["down"]) is bool
        else:
            assert event["type"] == "text" and set(event) == {"type", "text"}
            assert event["text"].strip()


def _assert_row(row, expected, *, ui=False, hole=False):
    observed = tuple(row[key] for key in ("setting_value", "current_setting_value", "effective_parameter_value"))
    assert all(type(value) is bool for value in observed) and observed == expected
    effective = expected[2]
    assert row["source_preserved"] is True and row["options_preserved"] is True
    assert len(row["pixels"]) == 63
    assert all(type(value) is int and value in (0, 255) for value in row["pixels"])
    result = np.asarray(row["pixels"], dtype=np.uint8).reshape((7, 9))
    actual = _pixels(result, effective, hole=hole)
    if "pixel_sha256" in row:
        assert row["pixel_sha256"] == actual["pixel_sha256"]
    if "foreground" in row:
        assert type(row["foreground"]) is int and row["foreground"] == actual["foreground"]
    if "source_pixels" in row:
        _assert_byte_pixels(row["source_pixels"], _source(hole=hole).ravel().tolist())
    if "captured_options" in row:
        _assert_parameters(row["captured_options"], expected[0])
        _assert_parameters(row["options_after"], expected[0])
        assert row["captured_options"] == row["options_after"]
    if "captured_parameters" in row:
        _assert_parameters(row["captured_parameters"], expected[0])
        if "captured_options" not in row:
            # Standalone records options_after as the actual workflow execution
            # options; its retained capture has a separate, explicit identity.
            _assert_parameters(row["options_after"], effective)
    if "execution_parameters" in row:
        _assert_parameters(row["execution_parameters"], effective)
    assert type(row["history_steps"]) is int
    assert row["history_steps"] == (1 if ui or hole or not effective else 0)
    assert row["undo_restored_source"] is True
    expected_steps = row["history_steps"]
    if "undo_performed" in row:
        assert row["undo_performed"] is bool(expected_steps)
    if "identity_preserved_history" in row:
        assert row["identity_preserved_history"] is (not bool(expected_steps))
    if "restored_pixels" in row:
        _assert_byte_pixels(row["restored_pixels"], _source(hole=hole).ravel().tolist())
        assert type(row["undo_steps_after"]) is int and row["undo_steps_after"] == 0
        assert type(row["redo_steps_after"]) is int and row["redo_steps_after"] == bool(expected_steps)
    if "publication" in row:
        publication = row["publication"]
        assert type(publication["document_id"]) is int and publication["document_id"] > 0
        assert publication["in_place"] is True and publication["prepared_pad_edges"] is effective
        for field in ("preview_pixels", "published_pixels"):
            _assert_byte_pixels(publication[field], actual["pixels"])
        _assert_byte_pixels(publication["restored_pixels"], _source(hole=hole).ravel().tolist())
        assert type(publication["undo_steps_after"]) is int and publication["undo_steps_after"] == 0
        assert type(publication["redo_steps_after"]) is int
        assert publication["redo_steps_after"] == bool(expected_steps)
    if row["id"] == "frozen":
        assert row["captured_scenario"] == "default"
    if row["id"] in ("override", "reverse_override"):
        assert row["override_value"] is effective
    if ui:
        _assert_events(row["input_events"])
        assert row["api_inputs"] and row["workflow_pad_edges"] is effective
        assert len(row["workflow"]) == 1
        assert row["workflow"][0]["operation"] == "binary_erode"
        _assert_parameters(row["workflow"][0]["parameters"], effective)
    if row["id"] == "persisted" or (ui and row["id"] == "changed"):
        saved = row["persistence_file"]
        path = Path(saved["path"])
        assert path.is_absolute() and path.read_bytes() == saved["bytes_utf8"].encode("utf-8")
        assert json.loads(saved["bytes_utf8"]) == saved["envelope"]
        assert saved["envelope"]["format"] == "imagejxx-preferences" and saved["envelope"]["version"] == 1
        assert saved["envelope"]["values"]["binary_pad_edges"] is True
        assert saved["reopened_setting_value"] is True
        if ui and row["id"] == "persisted":
            assert saved["displayed_setting_value"] is True


def _assert_report(report, context, *, ui=False):
    assert report["schema_version"] == 1 and report["context"] == context
    assert report["requirement_id"] == "G052.binary_erode.pad_edges"
    assert [row["id"] for row in report["scenarios"]] == list(SCENARIOS)
    for row in report["scenarios"]:
        _assert_row(row, SCENARIOS[row["id"]], ui=ui)
    supplementary = report["supplementary"]
    assert supplementary["reverse_override"]["id"] == "reverse_override"
    _assert_row(supplementary["reverse_override"], (True, True, False), ui=ui)
    if not ui:
        extra = supplementary["nonidentity_publication"]
        assert [row["id"] for row in extra] == ["nonidentity-false", "nonidentity-true"]
        for row, flag in zip(extra, (False, True), strict=True):
            _assert_row(row, (flag, flag, flag), hole=True)


def _application_publication(source, parameters, flag, *, hole=False):
    app = ij.Application()
    identity = app.create(ij.Dataset(source, axes="YX"))
    before = app.snapshot(identity)
    assert app.history(identity).undo_steps == app.history(identity).redo_steps == 0
    target = app.resolve_target(operation="binary_erode", document_id=identity)
    prepared = app.prepare("binary_erode", target, parameters)
    observed_parameters = dict(prepared.parameters)
    assert observed_parameters == parameters
    preview_pixels = _pixels(prepared.preview.dataset.plane(), flag, hole=hole)
    assert app.snapshot(identity).shares_data(before) and not before.dirty
    assert_array_equal(before.dataset.plane(), source)
    published = app.publish(prepared)
    after = app.snapshot(identity)
    assert published.document_id == identity and published.created is False
    assert not prepared.active and after.id == before.id and after.metadata == before.metadata
    assert after.dataset.axis_sizes == before.dataset.axis_sizes
    assert after.revisions.metadata == before.revisions.metadata
    assert after.revisions.selection == before.revisions.selection
    observed_pixels = _pixels(after.dataset.plane(), flag, hole=hole)
    history_steps = app.history(identity).undo_steps
    expected_steps = 1 if hole or not flag else 0
    assert history_steps == expected_steps and app.history(identity).redo_steps == 0
    assert after.dirty is bool(expected_steps)
    assert after.revisions.pixels == before.revisions.pixels + expected_steps
    api_inputs = [{"method": "Application.create", "axes": "YX", "document_id": identity},
                  {"method": "Application.resolve_target", "operation": "binary_erode"},
                  {"method": "Application.prepare", "parameters": parameters},
                  {"method": "Application.publish", "document_id": published.document_id}]
    if history_steps:
        app.undo(identity)
        api_inputs.append({"method": "Application.undo", "document_id": identity})
        assert app.history(identity).undo_steps == 0 and app.history(identity).redo_steps == 1
    restored = app.snapshot(identity)
    assert_array_equal(restored.dataset.plane(), source)
    assert not restored.dirty
    assert_array_equal(before.dataset.plane(), source)
    return {"history_steps": history_steps, "undo_performed": bool(history_steps),
            "identity_preserved_history": not bool(history_steps), "undo_restored_source": True,
            "publication": {"document_id": published.document_id, "created": published.created,
                            "in_place": published.document_id == identity and not published.created,
                            "prepared_pad_edges": observed_parameters["pad_edges"],
                            "parameters": observed_parameters, "preview_pixels": preview_pixels["pixels"],
                            "published_pixels": observed_pixels["pixels"],
                            "dirty": after.dirty, "pixel_revision": after.revisions.pixels,
                            "restored_pixels": restored.dataset.plane().ravel().tolist(),
                            "undo_steps_after": app.history(identity).undo_steps,
                            "redo_steps_after": app.history(identity).redo_steps},
            "api_inputs": api_inputs}


def _api_row(name, settings, captured, *, override=None, persistence=None, hole=False):
    source = _source(hole=hole)
    original = source.copy()
    before = _options(captured)
    current = settings["binary_pad_edges"]
    effective = captured.pad_edges if override is None else override
    arguments = {} if override is None else {"pad_edges": override}
    result = ij.binary_erode(source, options=captured, **arguments)
    observed = _pixels(result, effective, hole=hole)
    assert_array_equal(source, original)
    assert not np.shares_memory(source, result)
    assert _options(captured) == before and settings["binary_pad_edges"] is current
    parameters = {**before, "pad_edges": effective}
    publication = _application_publication(source, parameters, effective, hole=hole)
    row = {"id": name, "setting_value": before["pad_edges"], "current_setting_value": current,
           "effective_parameter_value": effective, **observed,
           "source_pixels": original.ravel().tolist(),
           "source_preserved": bool(np.array_equal(source, original)),
           "captured_options": before, "options_after": _options(captured),
           "execution_parameters": publication["publication"]["parameters"],
           "options_preserved": _options(captured) == before, **publication,
           "direct_api_input": {"method": "binary_erode", "options": before, "overrides": arguments}}
    if override is not None:
        row["override_value"] = override
    if name == "frozen":
        row["captured_scenario"] = "default"
    if persistence is not None:
        row["persistence_file"] = persistence
    return row


def test_binary_pad_edges_python_api_consumer(tmp_path):
    _fixtures()
    directory = _directory(tmp_path, "python_api")
    settings = ij.Settings()
    captured_default = settings.binary_options()
    rows = [_api_row("default", settings, captured_default)]
    settings["binary_pad_edges"] = True
    rows.append(_api_row("changed", settings, settings.binary_options()))
    rows.append(_api_row("frozen", settings, captured_default))
    settings["binary_pad_edges"] = False
    captured = settings.binary_options()
    rows.append(_api_row("override", settings, captured, override=True))
    path = directory / "preferences.json"
    preferences = ij.PreferencesDocument()
    preferences.settings["binary_pad_edges"] = True
    assert preferences.save(path) is True
    immediate_bytes = path.read_bytes()
    reopened = ij.PreferencesDocument.load(path, recover=False)
    settings = reopened.settings
    assert settings["binary_pad_edges"] is True and path.read_bytes() == immediate_bytes
    rows.append(_api_row("persisted", settings, settings.binary_options(),
                         persistence=_persistence(path, settings["binary_pad_edges"])))
    reverse = _api_row("reverse_override", settings, settings.binary_options(), override=False)
    nonidentity = []
    for flag in (False, True):
        selected = ij.Settings({"binary_pad_edges": flag})
        nonidentity.append(_api_row(f"nonidentity-{str(flag).lower()}", selected,
                                   selected.binary_options(), hole=True))
    report = _write_report(directory, "python_api", rows,
                           {"reverse_override": reverse, "nonidentity_publication": nonidentity})
    _assert_report(report, "python_api")


@pytest.mark.parametrize("invalid", [0, 1, None, "true", "false", [], {}, np.bool_(True)])
def test_binary_pad_edges_requires_boolean_without_mutating_snapshot_or_source(invalid):
    settings = ij.Settings({"binary_pad_edges": True})
    options = settings.binary_options()
    original_options = _options(options)
    original_settings = settings.to_dict()
    source = _source()
    original = source.copy()
    with pytest.raises((ValueError, TypeError)):
        settings["binary_pad_edges"] = invalid
    assert settings.to_dict() == original_settings
    with pytest.raises((ValueError, TypeError)):
        options.pad_edges = invalid
    assert _options(options) == original_options
    with pytest.raises((ValueError, TypeError)):
        ij.BinaryMorphologyOptions(pad_edges=invalid)
    if invalid is not None:  # None is the public adapter's no-override sentinel.
        with pytest.raises((ValueError, TypeError)):
            ij.binary_erode(source, options=options, pad_edges=invalid)
    assert_array_equal(source, original)
    assert _options(options) == original_options


def test_binary_pad_edges_none_and_explicit_override_leave_detached_defaults_intact():
    source = _source()
    settings = ij.Settings()
    captured = settings.binary_options()
    settings["binary_pad_edges"] = True
    _pixels(ij.binary_erode(source, options=captured, pad_edges=None), False)
    _pixels(ij.binary_erode(source, options=captured, pad_edges=True), True)
    assert captured.pad_edges is False and settings["binary_pad_edges"] is True
    true_options = settings.binary_options()
    _pixels(ij.binary_erode(source, options=true_options, pad_edges=None), True)
    _pixels(ij.binary_erode(source, options=true_options, pad_edges=False), False)
    assert true_options.pad_edges is True
    _pixels(ij.binary_erode(source, pad_edges=None), False)
    assert_array_equal(source, _source())


def _click(point):
    assert len(point) == 2 and all(map(math.isfinite, point)) and min(point) >= 0
    raster = [math.floor(coordinate) for coordinate in point]
    return [{"type": "position", "point": raster, "space": "screen"},
            {"type": "button", "name": "left", "down": True},
            {"type": "button", "name": "left", "down": False}]


def _deliver(editor, requests, recorded, observations, label):
    rendered = editor.render_headless(events=requests)
    # Retain actual input observations before checking their effect.
    observations.append({"label": label, "requests": requests, "rendered": rendered})
    assert len(rendered["input_trace"]) == len(requests)
    for request, observed in zip(requests, rendered["input_trace"], strict=True):
        assert observed["type"] == request["type"]
        assert request["type"] in ("position", "button")
        actual_point = observed["mouse_screen"]
        assert len(actual_point) == 2 and all(map(math.isfinite, actual_point))
        if request["type"] == "position":
            assert actual_point == request["point"]
        recorded.append({"type": "pointer", "position": actual_point,
                         "button": request.get("name", "left"), "down": request.get("down", False)})
    assert not editor.error
    return rendered


def _boolean_panel(rendered):
    panel = rendered["preferences_panel"]
    assert panel["open"] and not panel["error"]
    assert panel["controls"] == ["binary_pad_edges"]
    assert panel["toggle_name"] == "binary_pad_edges"
    assert type(panel["toggle_value"]) is bool
    return panel


def _attach(editor, source, inputs):
    owner = ij.Document(ij.Dataset(source, axes="YX"))
    live = editor.add_native_document(owner)
    assert live.id == owner.id and live.info["undo_steps"] == live.info["redo_steps"] == 0
    assert_array_equal(live.native_snapshot().dataset.plane(), source)
    inputs.append({"method": "add_native_document", "document_id": owner.id,
                   "axes": "YX", "source_pixels": source.ravel().tolist()})
    return live


def _ui_scenario(name, directory):
    from imagejxx.ui.app import Editor

    path = directory / f"{name}-preferences.json"
    source = _source()
    original = source.copy()
    editor = Editor(preferences=Preferences(path))
    api_inputs = [{"method": "Editor", "preferences_path": str(path)}]
    live = _attach(editor, source, api_inputs)
    events, observations = [], []
    persistence = None
    assert editor.settings["binary_pad_edges"] is False
    if name in ("changed", "persisted", "reverse_override"):
        editor.action("show_preferences", search="binary_pad_edges")
        api_inputs.append({"method": "action", "name": "show_preferences", "parameters": {"search": "binary_pad_edges"}})
        rendered = editor.render_headless()
        panel = _boolean_panel(rendered)
        preferences_before = panel
        assert panel["toggle_value"] is False
        rendered = _deliver(editor, _click(panel["toggle_point"]), events, observations, "Preferences.binary_pad_edges")
        panel = _boolean_panel(rendered)
        preferences_draft = panel
        assert panel["toggle_value"] is True and editor.settings["binary_pad_edges"] is False
        assert not path.exists(), "The draft must not persist before actual Apply"
        rendered = _deliver(editor, _click(panel["apply_point"]), events, observations, "Preferences.Apply")
        preferences_applied = _boolean_panel(rendered)
        assert preferences_applied["toggle_value"] is True and editor.settings["binary_pad_edges"] is True
        immediate_bytes = path.read_bytes()  # Before close/destruction/fresh owner.
        fresh_settings = ij.PreferencesDocument.load(path, recover=False).settings
        persistence = _persistence(path, fresh_settings["binary_pad_edges"])
        assert persistence["envelope"]["values"] == editor.settings.to_dict()
        if name == "persisted":
            first_owner = live.id
            editor = Editor(preferences=Preferences(path))
            api_inputs.append({"method": "Editor", "preferences_path": str(path), "fresh_owner": True})
            live = _attach(editor, source, api_inputs)
            assert live.id != first_owner and editor.settings["binary_pad_edges"] is True
            editor.action("show_preferences", search="binary_pad_edges")
            api_inputs.append({"method": "action", "name": "show_preferences", "parameters": {"search": "binary_pad_edges"}})
            rendered = editor.render_headless()
            displayed = _boolean_panel(rendered)["toggle_value"]
            assert displayed is True and path.read_bytes() == immediate_bytes
            persistence = _persistence(path, editor.settings["binary_pad_edges"], displayed_value=displayed)
            observations.append({"label": "Preferences.fresh_reopen", "rendered": rendered})
        observations.append({"label": "Preferences.immediate_save", "before": preferences_before,
                             "changed_draft": preferences_draft, "applied": preferences_applied,
                             "persistence_file": persistence})
    captured = editor.settings.binary_options()
    captured_options = _options(captured)
    editor.action("show_operation", operation="binary_erode")
    api_inputs.append({"method": "action", "name": "show_operation", "parameters": {"operation": "binary_erode"}})
    rendered = editor.render_headless()
    dialog = rendered["operation_dialog"]
    parameters = dict(dialog["parameters"])
    assert dialog["open"] and dialog["id"] == "binary_erode" and parameters == captured_options
    observations.append({"label": "Operation.captured_draft", "rendered": rendered})
    if name == "frozen":
        assert captured.pad_edges is False
        editor.update_settings(binary_pad_edges=True, persist=False)
        api_inputs.append({"method": "update_settings", "values": {"binary_pad_edges": True}, "persist": False})
        rendered = editor.render_headless()
        dialog = rendered["operation_dialog"]
        assert dialog["open"] and dialog["parameters"] == parameters
        assert editor.settings["binary_pad_edges"] is True and _options(captured) == captured_options
        observations.append({"label": "Operation.frozen_after_API_global_change", "rendered": rendered})
    if name in ("override", "reverse_override"):
        assert dialog["controls"]["pad_edges"]["kind"] == "bool"
        rendered = _deliver(editor, _click(dialog["controls"]["pad_edges"]["point"]), events,
                            observations, "Operation.pad_edges.override")
        dialog = rendered["operation_dialog"]
        assert dialog["parameters"]["pad_edges"] is (name == "override")
        assert editor.settings["binary_pad_edges"] is captured_options["pad_edges"]
    effective = dialog["parameters"]["pad_edges"]
    execution_parameters = dict(dialog["parameters"])
    assert type(effective) is bool
    assert_array_equal(editor.document.data, original)
    original_snapshot = live.native_snapshot()
    before = live.info
    editor.recording = True
    api_inputs.append({"method": "recording", "value": True})
    _deliver(editor, _click(dialog["apply_point"]), events, observations, "Operation.Apply")
    editor.wait()
    assert not editor.busy and not editor.error
    pixels = _pixels(editor.document.data, effective)
    after = live.info
    history_steps = after["undo_steps"]
    workflow = editor.workflow
    assert history_steps == before["undo_steps"] + 1 == 1 and len(workflow) == 1
    assert workflow[0]["operation"] == "binary_erode"
    assert workflow[0]["parameters"] == {**parameters, "pad_edges": effective}
    assert workflow[0]["parameters"]["pad_edges"] is effective
    assert _options(captured) == captured_options
    assert_array_equal(source, original)
    assert_array_equal(original_snapshot.dataset.plane(), original)
    current = editor.settings["binary_pad_edges"]
    editor.action("undo")
    api_inputs.append({"method": "action", "name": "undo", "parameters": {}})
    assert_array_equal(editor.document.data, original)
    assert live.info["undo_steps"] == 0 and live.info["redo_steps"] == 1
    row = {"id": name, "setting_value": captured_options["pad_edges"], "current_setting_value": current,
           "effective_parameter_value": workflow[0]["parameters"]["pad_edges"], **pixels,
           "source_pixels": original.ravel().tolist(), "source_preserved": bool(np.array_equal(source, original)),
           "captured_options": captured_options, "options_after": _options(captured),
           "options_preserved": _options(captured) == captured_options,
           "captured_parameters": parameters, "input_events": events, "api_inputs": api_inputs,
           "execution_parameters": execution_parameters,
           "observations": observations, "before": before, "after_publication": after,
           "history_steps": history_steps, "workflow_pad_edges": workflow[0]["parameters"]["pad_edges"],
           "workflow": workflow, "undo_restored_source": bool(np.array_equal(editor.document.data, original)),
           "restored_pixels": editor.document.data.ravel().tolist(),
           "undo_steps_after": live.info["undo_steps"], "redo_steps_after": live.info["redo_steps"]}
    if name == "frozen":
        row["captured_scenario"] = "default"
    if name in ("override", "reverse_override"):
        row["override_value"] = effective
    if name in ("changed", "persisted"):
        assert path.read_bytes() == persistence["bytes_utf8"].encode("utf-8")
        row["persistence_file"] = persistence
    return row


def test_binary_pad_edges_python_ui_consumer(tmp_path):
    pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    _fixtures()
    directory = _directory(tmp_path, "imgui_python")
    rows = [_ui_scenario(name, directory) for name in SCENARIOS]
    reverse = _ui_scenario("reverse_override", directory)
    report = _write_report(directory, "imgui_python", rows, {"reverse_override": reverse})
    _assert_report(report, "imgui_python", ui=True)


@pytest.mark.skipif(not os.environ.get("IMAGEJXX_CPP_EXE"), reason="Set IMAGEJXX_CPP_EXE to installed standalone")
def test_binary_pad_edges_cpp_ui_consumer(tmp_path):
    _fixtures()
    directory = _directory(tmp_path, "imgui_cpp", create=False)
    executable = Path(os.environ["IMAGEJXX_CPP_EXE"]).resolve(strict=True)
    result = subprocess.run([str(executable), "--binary-pad-edges-consumer-test", "--consumer-dir", str(directory)],
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    if directory.is_dir():
        (directory / "stdout.txt").write_text(result.stdout, encoding="utf-8")
        (directory / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + "\n" + result.stderr
    _assert_report(json.loads((directory / "imgui-cpp.json").read_text(encoding="utf-8")), "imgui_cpp", ui=True)


@pytest.mark.skipif(not os.environ.get("IMAGEJXX_PAD_EDGES_CPP_REPORT"), reason="Set explicit installed SDK report")
def test_binary_pad_edges_installed_cpp_api_report():
    _fixtures()
    path = Path(os.environ["IMAGEJXX_PAD_EDGES_CPP_REPORT"]).resolve(strict=True)
    _assert_report(json.loads(path.read_text(encoding="utf-8")), "cpp_api")
