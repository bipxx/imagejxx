"""Installed Save/Open interaction assertions for the shared native codec.

The producer supplies fixed source recipes and actual frames. Expected pixels,
camera/metadata values and V2/V3 wire decoding below are independent stdlib code.
Headless ImGui input is exercised in each real installed launch mode.
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

CONTRACT = "p06a.scientific-session-ui.002"
CASES = ("S01", "S02", "S03")
CONTEXTS = ("imgui_cpp", "imgui_python")
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 4 * 1024 * 1024
CONTENT = ("shape", "dtype", "pixels", "storage_f64_hex", "calibration", "metadata",
           "selection", "previous_selection", "rois", "overlays", "fourier")
OPAQUE = {"opaque_ui_root": {"unsigned": 2**64-1, "negative_zero": -0.0,
                           "nested": ["retained", {"flag": True}]},
          "future_list": [None, 17, "Omega"]}


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


def _typed(value):
    if isinstance(value, dict):
        return {key: _typed(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_typed(item) for item in value]
    if type(value) is float:
        assert math.isfinite(value)
        return ("float", struct.pack("<d", value).hex())
    return (type(value).__name__, value)


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
            [str(executable), "--scientific-session-ui-case", case_id, str(directory)],
            capture_output=True, timeout=60, check=False,
        )
        assert len(completed.stdout) + len(completed.stderr) <= MAX_REPORT
        assert completed.returncode == 0, (completed.stdout+completed.stderr).decode("utf-8", "replace")
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
        row = _decode(editor._scientific_session_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row


def _frame(row, label):
    result = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(result) == 1, f"Missing/duplicate actual frame {label}"
    return result[0]


def _control(rendered, selector):
    if selector.startswith("browser."):
        return rendered["file_browser"]["controls"][selector.removeprefix("browser.")]
    if selector == "error_modal.ok":
        return rendered["error_modal"]["controls"]["ok"]
    return rendered["host_controls"][selector]


def _physical(row):
    frames = row["frames"]
    assert 1 <= row["frame_count"] == len(frames) <= 180
    assert [f["ordinal"] for f in frames] == list(range(1, len(frames)+1))
    sessions, held, buttons, position = [], set(), [False]*3, None
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
                    assert atom["name"] in {"Ctrl", "A"} and type(atom["down"]) is bool
                    (held.add if atom["down"] else held.discard)(atom["name"])
            else:
                assert event["type"] == "text" and isinstance(event["value"], str)
        if position is not None:
            assert actual["mouse_position"] == position
        assert actual["mouse_down"] == buttons and set(actual["keys_down"]) == held
        assert actual["modifiers"] == {"ctrl": "Ctrl" in held, "shift": False, "alt": False, "super": False}
    assert not held and buttons == [False]*3
    assert len({s["context_id"] for s in sessions}) == len({s["session_id"] for s in sessions}) == 1
    for first, second in zip(sessions, sessions[1:]):
        assert second["imgui_frame"] == first["imgui_frame"]+1
        assert second["imgui_time"] > first["imgui_time"]
    for observation in row["control_observations"]:
        ordinal = observation["before_frame_ordinal"]
        assert type(ordinal) is int and ordinal > 0
        actual = _control(frames[ordinal-1]["rendered"], observation["selector"])
        assert actual == observation["control"] and actual["id"] > 0 and actual["enabled"] is True
        rect, point = actual["rect"], observation["position"]
        assert all(type(v) in {int, float} and math.isfinite(v) for v in rect+point)
        assert point == [math.floor(v) for v in actual["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [f["requested_events"] for f in frames[ordinal:ordinal+3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0
    selectors = [o["selector"] for o in row["control_observations"]]
    assert "menu.File" in selectors and "menu.file.open" in selectors
    assert "browser.path" in selectors and "browser.submit" in selectors
    if row["case_id"] != "S02":
        assert "menu.path.save_workspace" in selectors


def _pixels(depth):
    return [[100*z+10*y+x+.125 for y in range(5) for x in range(7)] for z in range(depth)]


def _f64(values):
    return b"".join(struct.pack("<d", value) for value in values)


def _literal_source(state, case_id):
    depth = 2 if case_id == "S01" else 1
    assert state["shape"] == [1, 1, depth, 5, 7, 1] and state["dtype"] == "float32"
    expected = _pixels(depth)
    assert state["pixels"] == expected
    assert state["storage_f64_hex"] == [_f64(plane).hex() for plane in expected]
    assert state["calibration"] == {"spacing": [.5, 1.25, 2.5], "origin": [11, -7, 19],
        "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "um", "time_unit": "s",
        "frame_interval": .75, "time_origin": 2.25}
    metadata = state["metadata"]
    assert metadata["title"] == "Scientific session "+case_id
    assert _typed(metadata["properties"]) == _typed({"dirty_marker": "saved", "session_property": {
        "signed_zero": -0.0, "unsigned": 2**64-1, "tag": "kept"}})
    assert metadata["extensions"] == {"opaque_metadata": {"tag": "keep"}}
    assert metadata["record_layout"] == {}
    assert state["selection"] == {"kind": "rectangle", "points": [[1, 1], [6, 4]],
        "width": 2, "name": "area", "selection_tag": "keep"}
    assert state["previous_selection"] == {"kind": "rectangle", "points": [[12, 12], [14, 15]], "width": 1}
    ellipse = {"kind": "ellipse", "points": [[2, 1], [5, 4]], "width": 1}
    assert state["rois"] == [{"name": "saved ROI", "selection": ellipse, "t": None,
        "c": None, "z": depth-1, "attributes": {"roi_tag": "keep"}}]
    assert state["overlays"] == [{"kind": "roi", "selection": ellipse, "color": "#00ff00ff",
        "width": 1, "slice_index": depth-1, "overlay_tag": "keep"}]
    display = state["display"]
    assert display["zoom"] == 0 and display["crosshair"] == [3, 2, depth-1]
    assert display["t"] == display["c"] == 0
    assert display["plane_zoom"] == [0, 1.5, 2.5] and display["pan"] == [[3, -2], [1, 4], [-5, 2]]
    assert display["linked_zoom"] is True and display["orthogonal"] is False
    assert display["properties"]["lut"] == "Grays" and display["properties"]["display_range"] == [0, 255]
    assert display["properties"]["manual_threshold"] == {"version": 1, "lower": 100*(depth-1)+12.125,
        "upper": 100*(depth-1)+34.125, "t": 0, "c": 0, "z": depth-1}
    if case_id == "S01":
        fourier = state["fourier"]
        expected_pairs = [[i+.25, -i-.5] for i in range(64)]
        assert fourier["shape"] == [8, 8] and fourier["original_shape"] == [5, 7]
        assert fourier["pixels"] == expected_pairs
        assert fourier["storage_f64_hex"] == _f64([v for pair in expected_pairs for v in pair]).hex()
    else:
        assert state["fourier"] is None


def _same_content(first, second):
    for key in CONTENT:
        assert _typed(first[key]) == _typed(second[key]), key
    first_display = {k: v for k, v in first["display"].items() if k != "revision"}
    second_display = {k: v for k, v in second["display"].items() if k != "revision"}
    assert _typed(first_display) == _typed(second_display)


def _fresh(state):
    assert state["dirty"] is False and state["undo_steps"] == state["redo_steps"] == 0
    assert state["revisions"] == {"pixels": 0, "metadata": 0, "selection": 0, "display": 0}
    assert state["display"]["revision"] == 0


def _file(row, directory, key):
    member = row["files"][key]
    assert Path(member["member"]).name == member["member"]
    path = (directory / member["member"]).resolve(strict=True)
    assert path.parent == directory.resolve(strict=True) and path.is_file() and not path.is_symlink()
    raw = path.read_bytes()
    assert 0 < len(raw) == member["byte_count"] <= MAX_ARCHIVE
    assert raw.hex() == member["hex"]
    if raw.startswith(b"IMAGEJXX3\n"):
        declared, = struct.unpack("<Q", raw[10:18])
        offset, version = 18, 3
    else:
        assert raw.startswith(b"IMAGEJXX2\n")
        declared, = struct.unpack(">I", raw[10:14])
        offset, version = 14, 2
    assert 0 < declared <= MAX_ARCHIVE
    stream = zlib.decompressobj()
    data = stream.decompress(raw[offset:], declared+1)
    assert len(data) == declared and stream.eof and not stream.unused_data and not stream.unconsumed_tail
    root = _decode(data)
    assert root["format"] == "imagejxx-session" and root["version"] == version
    return root


def _path_text(row, label, path):
    frame = _frame(row, label+".path_text")
    assert frame["requested_events"] == [{"type": "text", "value": str(path.resolve())}]
    browser = frame["rendered"]["file_browser"]
    assert browser["open"] is True and browser["path"] == str(path.resolve())
    control = browser["controls"]["path"]
    assert control["active"] is True and control["id"] == frame["rendered"]["input_frame"]["focus"]["active_id"]


def _assert_case(row, case_id, context, directory):
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert row["limits"] == {"max_frames": 180, "max_seconds": 15, "max_report_bytes": MAX_REPORT,
                             "max_archive_bytes": MAX_ARCHIVE, "max_shape_zyx": [2, 9, 9]}
    assert 0 <= row["elapsed_seconds"] <= 15
    _physical(row)
    before = row["states"]["before_io"]
    assert len(before["documents"]) == 1 and before["active_document_id"] == before["document_ids"][0]
    original = before["documents"][0]
    _literal_source(original, case_id)
    assert original["dirty"] is True and original["undo_steps"] == 1
    assert before["preferences"]["values"]["default_zoom"] == 2
    assert _typed(before["results"]) == _typed([{"Image": "recorded", "mean": 12.5, "negative_zero": -0.0}])
    assert before["workflow"] == [{"operation": "add", "parameters": {"value": 2}, "all_slices": False}]
    for state in row["states"].values():
        assert state["busy"] is False
        assert state["document_ids"] == [doc["document_id"] for doc in state["documents"]]
    if case_id == "S01":
        saved, loaded = row["states"]["saved"], row["states"]["loaded"]
        _same_content(original, saved["documents"][0])
        assert saved["documents"][0]["dirty"] is False and saved["documents"][0]["undo_steps"] == 1
        assert len(loaded["documents"]) == 2 and loaded["document_ids"][0] == original["document_id"]
        restored = loaded["documents"][1]
        assert restored["document_id"] != original["document_id"] and loaded["active_document_id"] == restored["document_id"]
        _literal_source(restored, case_id)
        _same_content(original, restored)
        _fresh(restored)
        assert _typed(loaded["results"]) == _typed(before["results"])
        assert _typed(loaded["workflow"]) == _typed(before["workflow"])
        wire = _file(row, directory, "saved")
        assert wire["version"] == 3 and len(wire["documents"]) == 1
        document = wire["documents"][0]
        assert document["axes"] == "TCZYXS" and document["shape"] == [1, 1, 2, 5, 7, 1]
        assert document["pixel_type"] == "float32" and document["storage"] == "Float64LE"
        assert _typed(document["metadata"]) == _typed(original["metadata"])
        assert document["calibration"] == original["calibration"]
        assert _typed(document["display"]) == _typed({k: v for k, v in original["display"].items() if k != "revision"})
        assert [base64.b64decode(plane, validate=True) for plane in document["planes"]] == [_f64(plane) for plane in _pixels(2)]
        assert base64.b64decode(document["fourier"]["pixels"], validate=True) == _f64([v for i in range(64) for v in (i+.25, -i-.5)])
        _path_text(row, "save", directory / "fresh.ijx")
        _path_text(row, "open", directory / "fresh.ijx")
    elif case_id == "S02":
        rejected = row["states"]["rejected"]
        # P06c recognizes channel display. This preserved opaque payload still
        # lacks its strict envelope and must fail before mixed-open publication.
        assert rejected["window_error"] == "Channel display has unexpected fields"
        assert _typed({k: v for k, v in rejected.items() if k != "window_error"}) == _typed({k: v for k, v in before.items() if k != "window_error"})
        wire = _file(row, directory, "mixed")
        assert wire["version"] == 3 and len(wire["documents"]) == 2
        assert wire["documents"][0]["shape"] == [1, 1, 1, 5, 7, 1]
        assert wire["documents"][1]["shape"] == [2, 2, 1, 3, 4, 1]
        assert wire["documents"][1]["display"]["properties"] == {
            "channel_display": {"version": 37, "future": "retained"}}
        assert wire["documents"][1]["metadata"]["title"] == "Unsupported future channel display owner"
        assert wire["results"] == [{"incoming": "must not publish"}]
        assert wire["workflow"] == [{"operation": "incoming"}]
        assert wire["extensions"] == {"incoming_extension": "must not publish"}
        _path_text(row, "reject", directory / "mixed.ijx")
    else:
        legacy, upgraded = row["states"]["legacy_loaded"], row["states"]["upgraded"]
        assert len(legacy["documents"]) == 2
        _same_content(original, legacy["documents"][1])
        _fresh(legacy["documents"][1])
        assert _typed(legacy["session_extensions"]) == _typed(OPAQUE)
        assert _typed(upgraded["session_extensions"]) == _typed({**OPAQUE, "imagejxx_workspace": {"version": 1, "active_index": 1}})
        legacy_original = _file(row, directory, "legacy_original")
        legacy_extended = _file(row, directory, "legacy_extended")
        assert legacy_original["version"] == legacy_extended["version"] == 2
        assert "opaque_ui_root" not in legacy_original
        assert _typed({k: legacy_extended[k] for k in OPAQUE}) == _typed(OPAQUE)
        wire = _file(row, directory, "upgraded")
        assert wire["version"] == 3 and len(wire["documents"]) == 2
        assert _typed(wire["extensions"]) == _typed({**OPAQUE, "imagejxx_workspace": {"version": 1, "active_index": 1}})
        assert _typed(wire["results"]) == _typed(before["results"])
        assert _typed(wire["workflow"]) == _typed(before["workflow"])
        assert all(doc["dirty"] is False for doc in upgraded["documents"])
        _path_text(row, "open", directory / "legacy-extended.ijx")
        _path_text(row, "save", directory / "upgraded.ijx")
    assert row["states"]["final"]["busy"] is False
    assert row["window_error"] == ""


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_scientific_session_save_open(case_id, context, request, tmp_path):
    directory = tmp_path / "observations"
    row = _observe(case_id, context, directory, request)
    _assert_case(row, case_id, context, directory)
