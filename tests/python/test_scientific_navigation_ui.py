"""Independent assertions for installed shared-ImGui scientific navigation.

Expected samples, display bytes, positioned annotations and session records are
specified here from the fixed fixture recipe, without invoking product helpers.
The producer records real pointer/key/text frames in both installed launches.
"""
import base64
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sysconfig
import time
import zlib

import pytest


CONTRACT = "p06b.scientific-navigation-ui.002"
CASES = ("N01", "N02", "N03", "N04", "N05", "N06")
CONTEXTS = ("imgui_cpp", "imgui_python")
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 4 * 1024 * 1024
SHAPE = [2, 3, 4, 5, 7, 1]
CALIBRATION = {
    "spacing": [.5, 1.25, 2.5], "origin": [11, -7, 19],
    "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "um",
    "time_unit": "s", "frame_interval": .75, "time_origin": 2.25,
}
CAMERAS = {"crosshair": [3, 2, 1], "zoom": 1.25, "linked_zoom": False,
           "plane_zoom": [1.25, 1.75, 2.25], "pan": [[3, -2], [1, 4], [-5, 2]]}
ROI_NAMES = ["global", "time 2", "channel 3", "position", "explicit zero"]
ROI_COLORS = [[255, 0, 0, 255], [0, 255, 0, 255], [0, 0, 255, 255],
              [255, 255, 0, 255], [0, 255, 255, 255]]
ROI_POSITIONS = [(None, None, None), (1, None, None), (None, 2, None),
                 (1, 2, 2), (0, 0, 1)]


def _unique(pairs):
    result = {}
    for name, value in pairs:
        assert name not in result, f"Duplicate observation/record key {name}"
        result[name] = value
    return result


def _nonfinite(value):
    raise AssertionError(f"Nonfinite observation/record constant {value}")


def _decode(raw):
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    assert 0 < len(raw) <= MAX_REPORT
    return json.loads(raw, object_pairs_hook=_unique, parse_constant=_nonfinite)


def _read(path):
    assert path.is_file() and not path.is_symlink()
    assert 0 < path.stat().st_size <= MAX_REPORT
    return _decode(path.read_bytes())


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_CPP_EXE to the matching installed desktop")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed desktop")
    path = Path(configured).resolve(strict=True)
    assert path.is_file() and path.name.lower() in {"imagejxx.exe", "imagejxx"}
    assert path.parent.name.lower() == "bin"
    expected = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if expected:
        with path.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == expected
    return path


def _observe(case_id, context, directory, request):
    assert not directory.exists()
    started = time.monotonic()
    if context == "imgui_cpp":
        executable = request.getfixturevalue("installed_cpp_executable")
        completed = subprocess.run(
            [str(executable), "--scientific-navigation-ui-case", case_id, str(directory)],
            capture_output=True, timeout=60, check=False,
        )
        assert len(completed.stdout) + len(completed.stderr) <= MAX_REPORT
        assert completed.returncode == 0, (completed.stdout + completed.stderr).decode("utf-8", "replace")
        row = _read(directory / "case.json")
    else:
        desktop = (importlib.import_module("imagejxx._desktop") if _required()
                   else pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError))
        module = Path(desktop.__file__).resolve(strict=True)
        assert module.suffix in {".pyd", ".so"}
        if _required():
            roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx"
                     for key in ("platlib", "purelib")}
            assert any(module.is_relative_to(root) for root in roots), "Checkout desktop import"
        editor = desktop.Editor(preferences_path=str(directory.parent / "preferences.json"))
        row = _decode(editor._scientific_navigation_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row


def _frame(row, label):
    matches = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(matches) == 1, f"Missing/duplicate actual frame {label}"
    return matches[0]


def _control(rendered, selector):
    if selector.startswith("document."):
        document_id = int(selector.removeprefix("document."))
        matches = [tab for tab in rendered["document_tabs"] if tab["document_id"] == document_id]
        assert len(matches) == 1
        return matches[0]
    if selector.startswith("browser."):
        return rendered["file_browser"]["controls"][selector.removeprefix("browser.")]
    if selector.startswith("gaussian."):
        key = selector.removeprefix("gaussian.")
        panel = rendered["gaussian_panel"]
        return dict(panel["control_states"][key], point=panel["controls"][key],
                    rect=panel["control_rects"][key])
    return rendered["host_controls"][selector]


def _physical(row):
    frames = row["frames"]
    assert 1 <= row["frame_count"] == len(frames) <= 180
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    sessions, held, buttons, position = [], set(), [False] * 3, None
    allowed_keys = {"Ctrl", "A", "Enter", "Up", "Escape"}
    for frame in frames:
        rendered = frame["rendered"]
        session = rendered["session"]
        assert session["open"] is True and session["context_id"] > 0
        assert session["context_id"] == session["current_context_id"]
        sessions.append(session)
        actual = rendered["input_frame"]
        assert actual["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            if event["type"] == "position":
                assert event["space"] == "screen"
                position = event["point"]
            elif event["type"] == "button":
                assert event["name"] == "left" and type(event["down"]) is bool
                buttons[0] = event["down"]
            elif event["type"] == "keys":
                for atom in event["keys"]:
                    assert atom["name"] in allowed_keys and type(atom["down"]) is bool
                    (held.add if atom["down"] else held.discard)(atom["name"])
            else:
                assert event["type"] == "text" and isinstance(event["value"], str)
        if position is not None:
            assert actual["mouse_position"] == position
        assert actual["mouse_down"] == buttons and set(actual["keys_down"]) == held
        assert actual["modifiers"] == {"ctrl": "Ctrl" in held, "shift": False,
                                       "alt": False, "super": False}
    assert not held and buttons == [False] * 3
    assert len({session["context_id"] for session in sessions}) == 1
    assert len({session["session_id"] for session in sessions}) == 1
    for first, second in zip(sessions, sessions[1:]):
        assert second["imgui_frame"] == first["imgui_frame"] + 1
        assert second["imgui_time"] > first["imgui_time"]
    for observation in row["control_observations"]:
        ordinal = observation["before_frame_ordinal"]
        assert type(ordinal) is int and ordinal > 0
        actual = _control(frames[ordinal - 1]["rendered"], observation["selector"])
        assert actual == observation["control"] and actual["id"] > 0
        assert actual["enabled"] is (not observation["disabled_attempt"])
        rect, point = actual["rect"], observation["position"]
        assert all(type(value) in {int, float} and math.isfinite(value) for value in rect + point)
        assert point == [math.floor(value) for value in actual["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0


def _sample(t, c, z, y, x, changed=False):
    return 99999 if changed and (t, c, z, y, x) == (1, 2, 1, 0, 0) else 10000*t + 1000*c + 100*z + 10*y + x + .125


def _planes(changed=False):
    return [[_sample(t, c, z, y, x, changed) for y in range(5) for x in range(7)]
            for t in range(2) for c in range(3) for z in range(4)]


def _f64(values):
    return b"".join(struct.pack("<d", value) for value in values)


def _document(state, document_id):
    matches = [document for document in state["documents"] if document["document_id"] == document_id]
    assert len(matches) == 1
    return matches[0]


def _literal_source(document, case_id, *, changed=False):
    assert document["shape"] == SHAPE and document["dtype"] == "float32"
    expected = _planes(changed)
    assert document["pixels"] == expected
    assert document["storage_f64_hex"] == [_f64(plane).hex() for plane in expected]
    assert document["calibration"] == CALIBRATION
    properties = {"fixture": case_id}
    if case_id == "N05":
        properties["fixture_history"] = 1
    assert document["metadata"] == {"title": "Scientific navigation " + case_id,
        "properties": properties, "extensions": {}, "record_layout": {}}
    assert document["display"]["properties"] == {"lut": "Grays"}
    if case_id != "N03":
        assert document["selection"] is None and document["rois"] == []
    else:
        assert document["selection"] == {"kind": "rectangle", "points": [[1, 1], [5, 4]], "width": 1}
        assert [roi["name"] for roi in document["rois"]] == ROI_NAMES
        for index, roi in enumerate(document["rois"]):
            assert (roi["t"], roi["c"], roi["z"]) == ROI_POSITIONS[index]
            selection = roi["selection"]
            assert selection["kind"] == "rectangle" and selection["width"] == 1
            assert selection["points"] == [[index, 0], [index + 1, 1]]


def _position(document, t, c, z=1, *, orthogonal=None):
    display = document["display"]
    assert (display["t"], display["c"], display["crosshair"][2]) == (t, c, z)
    assert display["crosshair"][:2] == [3, 2]
    for key in ("zoom", "linked_zoom", "plane_zoom", "pan"):
        assert display[key] == CAMERAS[key]
    if orthogonal is not None:
        assert display["orthogonal"] is orthogonal


def _navigation_preserved(before, after):
    for key in ("document_ids", "active_document_id", "results", "workflow", "busy", "window_error"):
        assert after[key] == before[key], key
    assert len(after["documents"]) == len(before["documents"])
    for original, current in zip(before["documents"], after["documents"]):
        assert {key: value for key, value in current.items() if key not in {"display", "revisions"}} == {
            key: value for key, value in original.items() if key not in {"display", "revisions"}}
        assert {key: value for key, value in current["revisions"].items() if key != "display"} == {
            key: value for key, value in original["revisions"].items() if key != "display"}
        assert {key: value for key, value in current["display"].items() if key not in {"t", "c", "crosshair", "orthogonal"}} == {
            key: value for key, value in original["display"].items() if key not in {"t", "c", "crosshair", "orthogonal"}}


def _axis_controls(rendered, document_id, t, c):
    for name, count, index in (("channel", 3, c), ("time", 2, t)):
        control = rendered["host_controls"]["navigation." + name]
        assert control["enabled"] is True and control["id"] > 0
        assert {key: control[key] for key in ("document_id", "min", "max", "count", "value")} == {
            "document_id": document_id, "min": 1, "max": count, "count": count, "value": index + 1}
    info = rendered["document_info"]
    assert info["id"] == document_id and info["dtype"] == "float32"
    assert info["axis_sizes"] == dict(zip("TCZYXS", SHAPE))
    assert info["slice_index"] == rendered["view"]["crosshair"][2]
    assert info["current_tcz"] == [t, c, info["slice_index"]]
    assert info["scientific_read_only"] is False
    assert info["time"] == 2.25 + .75*t and info["time_unit"] == "s"
    assert rendered["view"]["t"] == t and rendered["view"]["c"] == c


def _rgb(values, bounds):
    low, high = bounds
    scale, offset = 255 / (high - low), -low * 255 / (high - low)
    grays = [math.floor(max(0., min(255., scale * value + offset)) + .5) for value in values]
    return [channel for value in grays for channel in (value,) * 3]


def _readback(image, shape, dtype, values):
    assert image["shape"] == shape and image["dtype"] == dtype
    assert image["pixels"] == values


def _pixels(row, label, t, c, z=1, *, orthogonal=False, changed=False, document_id=None):
    rendered = _frame(row, label)["rendered"]
    document_id = row["source_document_id"] if document_id is None else document_id
    _axis_controls(rendered, document_id, t, c)
    xy = [_sample(t, c, z, y, x, changed) for y in range(5) for x in range(7)]
    bounds = [min(xy), max(xy)]
    if not orthogonal:
        ordinary = rendered["ordinary_pixels"]
        assert ordinary["pixels_source"] == ordinary["contrast_source"] == "live"
        assert ordinary["source_document_id"] == document_id
        assert ordinary["preview_generation"] == 0
        assert ordinary["current_tcz"] == ordinary["range_tcz"] == [t, c, z]
        assert ordinary["range"] == bounds
        assert ordinary["readback"]["available"] is True
        assert ordinary["readback"]["source_tcz"] == [t, c, z]
        _readback(ordinary["readback"]["source"], [5, 7, 1], "float32", xy)
        _readback(ordinary["readback"]["rendered_rgb"], [5, 7, 3], "uint8", _rgb(xy, bounds))
        return [ordinary["pixel_builds"]]
    expected = [("XY", z, [5, 7, 1], [.5, 1.25], xy),
                ("XZ", 2, [4, 7, 1], [.5, 2.5],
                 [_sample(t, c, iz, 2, x, changed) for iz in range(4) for x in range(7)]),
                ("YZ", 3, [4, 5, 1], [1.25, 2.5],
                 [_sample(t, c, iz, y, 3, changed) for iz in range(4) for y in range(5)])]
    assert len(rendered["orthogonal_planes"]) == 3
    builds = []
    for plane, (name, coordinate, shape, spacing, values) in zip(rendered["orthogonal_planes"], expected):
        assert plane["plane"] == name and plane["coordinate"] == coordinate
        assert plane["current_tcz"] == plane["range_tcz"] == [t, c, z]
        assert plane["t"] == t and plane["c"] == c and plane["z"] == z
        assert plane["width"] == shape[1] and plane["height"] == shape[0]
        assert plane["spacing"] == spacing
        assert plane["range"] == bounds and plane["range_policy"] == "live_document"
        assert plane["source_document_id"] == plane["range_source_document_id"] == document_id
        assert plane["pixels_source"] == "live_document" and plane["preview_generation"] == 0
        readback = plane["readback"]
        assert readback["available"] is True and readback["source_tcz"] == [t, c, z]
        _readback(readback["source"], shape, "float32", values)
        _readback(readback["rendered_rgb"], [shape[0], shape[1], 3], "uint8", _rgb(values, bounds))
        builds.append(plane["builds"])
    return builds


def _archive(row, directory):
    member = row["files"]["saved"]
    assert member["member"] == "scientific.ijx"
    path = (directory / member["member"]).resolve(strict=True)
    assert path.parent == directory.resolve(strict=True) and path.is_file() and not path.is_symlink()
    raw = path.read_bytes()
    assert 0 < len(raw) == member["byte_count"] <= MAX_ARCHIVE and raw.hex() == member["hex"]
    assert raw.startswith(b"IMAGEJXX3\n")
    declared, = struct.unpack("<Q", raw[10:18])
    assert 0 < declared <= MAX_ARCHIVE
    stream = zlib.decompressobj()
    data = stream.decompress(raw[18:], declared + 1)
    assert len(data) == declared and stream.eof and not stream.unused_data and not stream.unconsumed_tail
    root = _decode(data)
    assert root["format"] == "imagejxx-session" and root["version"] == 3
    assert root["results"] == root["workflow"] == [] and root["extensions"] == {"imagejxx_workspace": {"version": 1, "active_index": 0}}
    assert len(root["documents"]) == 1
    document = root["documents"][0]
    assert document["axes"] == "TCZYXS" and document["shape"] == SHAPE
    assert document["pixel_type"] == "float32" and document["storage"] == "Float64LE"
    assert [base64.b64decode(plane, validate=True) for plane in document["planes"]] == [_f64(plane) for plane in _planes()]
    assert document["calibration"] == CALIBRATION
    assert document["display"] == _document(row["states"]["before_save"], row["source_document_id"])["display"]
    assert document["metadata"] == {"title": "Scientific navigation N04", "properties": {"fixture": "N04"},
                                      "extensions": {}, "record_layout": {}}
    assert document["selection"] is None and document["rois"] == []
    return document


def _path_text(row, label, path):
    frame = _frame(row, label + ".path.text")
    assert frame["requested_events"] == [{"type": "text", "value": str(path.resolve())}]
    browser = frame["rendered"]["file_browser"]
    assert browser["open"] is True and browser["path"] == str(path.resolve())
    control = browser["controls"]["path"]
    assert control["active"] is True
    assert control["id"] == frame["rendered"]["input_frame"]["focus"]["active_id"]


def _assert_case(row, case_id, context, directory):
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert row["limits"] == {"max_frames": 180, "max_seconds": 15, "max_report_bytes": MAX_REPORT,
                             "max_archive_bytes": MAX_ARCHIVE, "max_shape_tczyx": [2, 3, 4, 5, 7]}
    assert 0 <= row["elapsed_seconds"] <= 15
    _physical(row)
    source_id = row["source_document_id"]
    assert type(source_id) is int and source_id > 0
    initial = row["states"]["initial"]
    source = _document(initial, source_id)
    _literal_source(source, case_id)
    _position(source, 0, 0, orthogonal=case_id == "N01")
    assert source["undo_steps"] == source["redo_steps"] == (1 if case_id == "N05" else 0)
    assert source["dirty"] is (case_id == "N05")
    for state in row["states"].values():
        assert state["busy"] is False and state["results"] == state["workflow"] == []
        assert state["document_ids"] == [document["document_id"] for document in state["documents"]]
    if case_id in {"N01", "N02"}:
        previous = _pixels(row, "initial", 0, 0, orthogonal=case_id == "N01")
        for label, t, c in (("selected", 1, 2), ("alternate", 0, 1), ("returned", 1, 2)):
            state = row["states"][label]
            _navigation_preserved(initial, state)
            _literal_source(_document(state, source_id), case_id)
            _position(_document(state, source_id), t, c, orthogonal=case_id == "N01")
            current = _pixels(row, label, t, c, orthogonal=case_id == "N01")
            assert all(after > before for before, after in zip(previous, current)), "C/T switch left stale plane caches"
            previous = current
        if case_id == "N02":
            for label, edit_label, selector, typed, before_t, before_c, t, c in (
                ("channel_blur", "channel_blur_edit", "navigation.time", "2", 1, 2, 1, 1),
                ("time_blur", "time_blur_edit", "navigation.channel", "1", 1, 1, 0, 1),
            ):
                text_frame = _frame(row, edit_label + ".text")
                assert text_frame["requested_events"] == [{"type": "text", "value": typed}]
                _position(_document(text_frame["native_state"], source_id), before_t, before_c)
                end = _frame(row, label)
                clicks = [observation for observation in row["control_observations"]
                          if observation["selector"] == selector
                          and observation["before_frame_ordinal"] == text_frame["ordinal"]]
                assert len(clicks) == 1 and clicks[0]["disabled_attempt"] is False
                for frame in row["frames"][text_frame["ordinal"]:end["ordinal"]]:
                    assert not any(atom["name"] == "Enter"
                                   for event in frame["requested_events"] if event["type"] == "keys"
                                   for atom in event["keys"]), "Blur case substituted Enter"
                focus = end["rendered"]["input_frame"]["focus"]["active_id"]
                assert focus == end["rendered"]["host_controls"][selector]["id"]
                state = row["states"][label]
                _navigation_preserved(initial, state)
                _literal_source(_document(state, source_id), case_id)
                _position(_document(state, source_id), t, c, orthogonal=False)
                current = _pixels(row, label, t, c)
                assert all(after > before for before, after in zip(previous, current)), "Blur left stale current-plane cache"
                previous = current
        if case_id == "N01":
            changed = _document(row["states"]["external_native_edit"], source_id)
            _literal_source(changed, case_id, changed=True)
            assert changed["undo_steps"] == 1 and changed["redo_steps"] == 0 and changed["dirty"] is True
            current = _pixels(row, "external_native_edit", 1, 2, orthogonal=True, changed=True)
            assert all(after > before for before, after in zip(previous, current))
            restored = _document(row["states"]["external_native_undo"], source_id)
            _literal_source(restored, case_id)
            assert restored["undo_steps"] == 0 and restored["redo_steps"] == 1
            after_undo = _pixels(row, "external_native_undo", 1, 2, orthogonal=True)
            assert all(after > before for before, after in zip(current, after_undo))
    elif case_id == "N03":
        expected = [("initial", 0, 0, 1, [0, 4]), ("position_z1", 1, 2, 1, [0, 1, 2]),
                    ("position_z2", 1, 2, 2, [0, 1, 2, 3]), ("time_only", 1, 0, 2, [0, 1]),
                    ("global_only", 0, 0, 2, [0]), ("explicit_zero", 0, 0, 1, [0, 4]),
                    ("xy_annotations_only", 0, 0, 1, [0, 4])]
        for label, t, c, z, indices in expected:
            state = row["states"][label]
            _navigation_preserved(initial, state)
            _literal_source(_document(state, source_id), case_id)
            _position(_document(state, source_id), t, c, z, orthogonal=label == "xy_annotations_only")
            assert [roi["name"] for roi in state["facade_rois"]] == ROI_NAMES
            assert [roi["name"] for roi in state["visible_rois"]] == [ROI_NAMES[index] for index in indices]
            rendered = _frame(row, label)["rendered"]
            _axis_controls(rendered, source_id, t, c)
            layers = (rendered["orthogonal_planes"][0]["style_layers"] if label == "xy_annotations_only"
                      else rendered["ordinary_canvas"]["style_layers"])
            stored = [layer for layer in layers if layer["role"] == "stored_roi"]
            assert [layer["stroke_color"] for layer in stored] == [ROI_COLORS[index] for index in indices]
            assert all(layer["stroke_width"] == 1 and layer["area"] and layer["contour_edges"] > 0 for layer in stored)
            if label == "xy_annotations_only":
                assert not rendered["orthogonal_planes"][1]["style_layers"]
                assert not rendered["orthogonal_planes"][2]["style_layers"]
    elif case_id == "N04":
        before, saved, loaded = (row["states"][label] for label in ("before_save", "saved", "loaded"))
        _navigation_preserved(initial, before)
        assert saved == before
        assert len(loaded["documents"]) == 2 and loaded["document_ids"][0] == source_id
        restored = loaded["documents"][1]
        assert restored["document_id"] != source_id and loaded["active_document_id"] == restored["document_id"]
        _literal_source(restored, case_id)
        _position(restored, 1, 2, orthogonal=False)
        assert restored["dirty"] is False and restored["undo_steps"] == restored["redo_steps"] == 0
        assert restored["revisions"] == {"pixels": 0, "metadata": 0, "selection": 0, "display": 0}
        assert restored["display"] == _document(before, source_id)["display"]
        _pixels(row, "loaded", 1, 2, document_id=restored["document_id"])
        _archive(row, directory)
        _path_text(row, "save", directory / "scientific.ijx")
        _path_text(row, "open", directory / "scientific.ijx")
    elif case_id == "N05":
        _axis_controls(_frame(row, "initial")["rendered"], source_id, 0, 0)
        assert row["states"]["disabled_menu_attempts"] == initial
        assert row["states"]["disabled_tool_attempt"] == initial
        assert row["states"]["dispatch_guards"] == initial
        observations = row["dispatch_observations"]
        assert [observation["name"] for observation in observations] == [
            "fill", "select_all", "delete_slice", "split_channels", "threshold", "operation", "begin_edit"]
        for observation in observations:
            if observation["name"] == "threshold":
                assert observation["accepted"] is True and "error" not in observation
            else:
                assert observation["accepted"] is False and observation["error_type"] == "invalid_argument"
                assert "scientific" in observation["error"].lower() and observation["error"]
            assert observation["before"] == observation["after"] == initial
        assert _frame(row, "dispatch_guards")["rendered"]["threshold_panel"]["open"] is True
        disabled = [observation["selector"] for observation in row["control_observations"] if observation["disabled_attempt"]]
        assert disabled == ["menu.action.fill", "tools.tool.brush"]
        controls = _frame(row, "disabled_tool_attempt")["rendered"]["host_controls"]
        assert controls["tools.tool.hand"]["enabled"] is True
        assert controls["tools.tool.rectangle"]["enabled"] is controls["tools.tool.edit_roi"]["enabled"] is False
    else:
        eager = row["states"]["eager_preview"]
        assert len(eager["documents"]) == 2 and eager["active_document_id"] != source_id
        preview = _frame(row, "eager_preview")["rendered"]["ordinary_pixels"]
        assert preview["pixels_source"] == "preview" and preview["preview_generation"] > 0
        assert preview["source_document_id"] == eager["active_document_id"]
        scientific = row["states"]["scientific_tab"]
        assert scientific["active_document_id"] == source_id
        _position(_document(scientific, source_id), 0, 0)
        _pixels(row, "scientific_tab", 0, 0)
        focused = row["states"]["text_focus"]
        assert _document(focused, source_id)["display"]["crosshair"] == [3, 2, 1]
        text_frame = _frame(row, "text_focus")["rendered"]
        assert text_frame["input_frame"]["focus"]["active_id"] == text_frame["host_controls"]["navigation.channel"]["id"]
        after_text = row["states"]["after_text"]
        _position(_document(after_text, source_id), 1, 2)
        _pixels(row, "after_text", 1, 2)
        modal = row["states"]["modal_focus"]
        assert modal == after_text
        modal_frame = _frame(row, "modal_focus")["rendered"]
        assert modal_frame["file_browser"]["open"] is True
        _position(_document(row["states"]["after_modal"], source_id), 0, 1)
        _pixels(row, "after_modal", 0, 1)
        for state in row["states"].values():
            _literal_source(_document(state, source_id), case_id)
            assert _document(state, source_id)["data_identity"] == source["data_identity"]
            assert _document(state, source_id)["undo_steps"] == _document(state, source_id)["redo_steps"] == 0
        after_switch = _frame(row, "scientific_tab")["ordinal"]
        for frame in row["frames"][after_switch - 1:]:
            if frame["native_state"]["active_document_id"] == source_id:
                ordinary = frame["rendered"]["ordinary_pixels"]
                assert ordinary["pixels_source"] == "live" and ordinary["preview_generation"] == 0
                assert ordinary["source_document_id"] == source_id
    assert row["window_error"] == "" and row["states"]["final"]["busy"] is False


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_scientific_navigation(case_id, context, request, tmp_path):
    directory = tmp_path / "observations"
    row = _observe(case_id, context, directory, request)
    _assert_case(row, case_id, context, directory)
