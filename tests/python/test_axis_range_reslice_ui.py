"""Actual installed ImGui range consumers; expectations never come from target output."""

import hashlib
import importlib
import json
import math
import os
from functools import lru_cache
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sysconfig
import time

import pytest

CONTRACT = 'p05g.axis-range-reslice-ui.001'
CASE_IDS = tuple(f'U{i:02d}' for i in range(1, 22))
CONTEXTS = ('imgui_python', 'imgui_cpp')
MAX_JSON_BYTES = 64 * 1024 * 1024
AXES = {'XY': (0, 1, 2), 'XZ': (0, 2, 1), 'YZ': (1, 2, 0)}
EXTENTS = (32, 17, 9)
CAPTURED_XYZ = (7, 5, 4)
SPACING, ORIGIN = (.4, 1.2, 3.), (11., -7., 19.)
DIRECTION = ((1., .25, -.5), (.5, -1., .125), (0., .75, 1.5))
PROFILES = {
    'u8': ('uint8', 'UInt8', 'B', 1), 'u16': ('uint16', 'UInt16LE', 'H', 1),
    'f32': ('float32', 'Float32LE', 'f', 1), 'f64': ('float64', 'Float64LE', 'd', 1),
    'rgb': ('uint8', 'UInt8', 'B', 3), 'rgba': ('uint8', 'UInt8', 'B', 4),
    'f32_nan': ('float32', 'Float32LE', 'f', 1),
}
CASES = {
    'U01': ('u8', 'XY', 0, 8, 1), 'U02': ('u16', 'XZ', 0, 16, 1),
    'U03': ('f32', 'YZ', 0, 31, 1), 'U04': ('u8', 'XY', 1, 7, 3),
    'U05': ('u16', 'XZ', 1, 15, 3), 'U06': ('f32', 'YZ', 1, 30, 3),
    'U07': ('f64', 'XZ', 16, 16, 3), 'U08': ('rgb', 'XY', 1, 7, 3),
    'U09': ('u8', 'XZ', 1, 15, 3), 'U10': ('u8', 'XZ', -1, 16, 1),
    'U11': ('u16', 'XZ', 0, 17, 1), 'U12': ('f32', 'XZ', 4, 3, 1),
    'U13': ('u8', 'XZ', 0, 16, 0), 'U14': ('u16', 'XZ', 0, 16, -2),
    'U15': ('f32', 'XZ', 1, 15, 3), 'U16': ('u8', 'XZ', 0, 16, 1),
    'U17': ('u8', 'XZ', 0, 16, 1), 'U18': ('u16', 'YZ', 0, 31, 1),
    'U19': ('f32', 'XZ', 0, 16, 1), 'U20': ('rgba', 'XZ', 1, 15, 3),
    'U21': ('f32_nan', 'XY', 0, 8, 1),
}
SUCCESS_IDS = set(CASE_IDS[:8]) | {'U15', 'U20', 'U21'}
KEYS = {
    'Escape', 'Delete', 'Left', 'Right', 'Up', 'Down', 'PageUp', 'PageDown',
    'Home', 'End', 'Space', 'Shift', 'Ctrl', 'Alt', 'Super', 'A', 'W',
    'Enter', 'Tab', 'Backspace',
}
USAGE_FIELDS = {'running', 'queued', 'running_bytes', 'retained_bytes', 'reserved_bytes'}
REFERENCE_SHA = '8ddde981534e0ccf55840316485c29c3dbcf61eef03dacee291b83477b4a8347'

# Generic physical-frame, snapshot, safe-member and origin checks are inserted
# from the unchanged singleton reader. The range oracle below is independent.
def _unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate observation field: {key}"
        result[key] = value
    return result


def _constant(value):
    raise AssertionError(f"Nonfinite observation JSON: {value}")


def _decode(raw):
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    assert isinstance(raw, bytes) and len(raw) <= MAX_JSON_BYTES
    return json.loads(raw, object_pairs_hook=_unique, parse_constant=_constant)


def _read_json(path):
    assert path.is_file() and not path.is_symlink()
    assert path.stat().st_size <= MAX_JSON_BYTES
    return _decode(path.read_bytes())


def _integer(value, *, positive=False):
    assert type(value) is int and value >= int(positive)
    return value


def _number(value):
    assert type(value) in {int, float} and math.isfinite(value)
    return value


def _close(actual, expected):
    if isinstance(expected, (tuple, list)):
        assert isinstance(actual, list) and len(actual) == len(expected)
        for a, e in zip(actual, expected):
            _close(a, e)
    else:
        assert math.isclose(_number(actual), expected, rel_tol=1e-12, abs_tol=1e-12)


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


def _member(root, name):
    assert isinstance(name, str) and name and "\\" not in name and ":" not in name
    relative = PurePosixPath(name)
    assert not relative.is_absolute() and str(relative) == name
    assert all(part not in {"", ".", ".."} for part in relative.parts)
    path = root.joinpath(*relative.parts)
    assert not path.is_symlink()
    resolved = path.resolve(strict=True)
    assert resolved.is_relative_to(root.resolve(strict=True)) and resolved.is_file()
    return resolved


def _raw(member, root, profile, shape, expected):
    dtype, encoding, code, samples = PROFILES[profile]
    assert isinstance(member, dict)
    assert member["dtype"] == dtype and member["encoding"] == encoding
    assert member["endian"] == "little" and member["axes"] == "TCZYXS"
    assert member["shape"] == list(shape) and shape[-1] == samples
    assert member["sample_count"] == math.prod(shape)
    assert member["byte_count"] == math.prod(shape) * struct.calcsize(code) == len(expected)
    assert isinstance(member["member_id"], str) and member["member_id"]
    path = _member(root, member["path"])
    assert path.name == member["member_id"] and path.stat().st_size == len(expected)
    assert re.fullmatch(r"[0-9a-f]{64}", member["sha256"])
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == member["sha256"]
    assert raw == expected, "Actual typed samples differ from the independent literal oracle"
    return raw


def _frame(row, label):
    matches = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(matches) == 1, f"Missing or duplicate actual frame checkpoint: {label}"
    return matches[0]


def _control(rendered, selector):
    if selector.startswith("dialog."):
        return rendered["dialog"]["controls"][selector.removeprefix("dialog.")]
    if selector == "error_modal.ok":
        return rendered["error_modal"]["controls"]["ok"]
    if selector.startswith("document."):
        document_id = int(selector.removeprefix("document."))
        matches = [tab for tab in rendered["document_tabs"]
                   if tab["document_id"] == document_id]
        assert len(matches) == 1
        return matches[0]
    return rendered["host_controls"][selector]


def _actual_control(control, *, tab=False):
    assert isinstance(control, dict)
    _integer(control["id"], positive=True)
    assert type(control["enabled"]) is bool
    if tab:
        assert type(control["selected"]) is bool
    else:
        assert type(control["active"]) is type(control["focused"]) is bool
    rect, point = control["rect"], control["point"]
    assert isinstance(rect, list) and len(rect) == 4
    assert isinstance(point, list) and len(point) == 2
    for value in rect + point:
        _number(value)
    assert rect[0] < rect[2] and rect[1] < rect[3]
    assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]


def _physical_observations(row, directory):
    frames = row["frames"]
    assert 1 <= len(frames) <= 160
    assert row.get("frame_count", len(frames)) == len(frames)
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    assert len({frame["member_id"] for frame in frames}) == len(frames)
    sessions, held_keys = [], set()
    buttons = [False, False, False]
    mouse = None
    for frame in frames:
        assert _read_json(_member(directory, frame["member_id"])) == frame
        assert isinstance(frame["label"], str) and frame["label"]
        rendered = frame["rendered"]
        _collection(frame["native_state"], row["profile"])
        tab_ids = [tab["document_id"] for tab in rendered["document_tabs"]]
        facade_ids = frame["native_state"]["facade_ids"]
        if tab_ids != facade_ids:
            # The modal runs after the tabs in this render. A successful Submit
            # mouse-up may therefore observe the old drawn tabs and the newly
            # attached native owner. The next explicit checkpoint must draw it.
            assert frame["label"] == "dialog.apply.up"
            assert frame["native_state"]["output"] is not None
            assert tab_ids == facade_ids[:-1]
            assert frame["native_state"]["output"]["document_id"] == facade_ids[-1]
        session = rendered["session"]
        assert session["open"] is True
        _integer(session["session_id"], positive=True)
        _integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        _integer(session["imgui_frame"], positive=True)
        _number(session["imgui_time"])
        sessions.append(session)
        actual, events = rendered["input_frame"], frame["requested_events"]
        assert isinstance(events, list) and len(events) <= 120
        assert actual["raw_inputs"] == events
        atoms = 0
        for event in events:
            assert isinstance(event, dict)
            kind = event["type"]
            if kind == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert len(event["point"]) == 2
                mouse = [_number(value) for value in event["point"]]
            elif kind == "button":
                assert set(event) == {"type", "name", "down"}
                assert event["name"] in {"left", "right", "middle"}
                assert type(event["down"]) is bool
                buttons[("left", "right", "middle").index(event["name"])] = event["down"]
            elif kind in {"key", "keys"}:
                if kind == "key":
                    assert set(event) == {"type", "name", "down"}
                    batch = [{"name": event["name"], "down": event["down"]}]
                else:
                    assert set(event) == {"type", "keys"}
                    batch = event["keys"]
                    assert isinstance(batch, list) and 1 <= len(batch) <= 16
                    assert len({atom["name"] for atom in batch}) == len(batch)
                for atom in batch:
                    assert set(atom) == {"name", "down"} and atom["name"] in KEYS
                    assert type(atom["down"]) is bool
                    if atom["down"]:
                        held_keys.add(atom["name"])
                    else:
                        held_keys.discard(atom["name"])
                atoms += len(batch) - 1
            elif kind == "text":
                assert set(event) == {"type", "value"}
                assert isinstance(event["value"], str) and "\x00" not in event["value"]
                assert len(event["value"].encode("utf-8")) <= 65536
            elif kind == "wheel":
                assert set(event) == {"type", "x", "y"}
                _number(event["x"])
                _number(event["y"])
            else:
                raise AssertionError(f"Undeclared physical event: {kind}")
            atoms += 1
        assert atoms <= 120
        if mouse is not None:
            assert actual["mouse_position"] == mouse
        assert actual["mouse_down"] == buttons
        assert isinstance(actual["keys_down"], list)
        assert len(set(actual["keys_down"])) == len(actual["keys_down"])
        assert set(actual["keys_down"]) == held_keys
        assert actual["modifiers"] == {
            "ctrl": "Ctrl" in held_keys, "shift": "Shift" in held_keys,
            "alt": "Alt" in held_keys, "super": "Super" in held_keys,
        }
        assert isinstance(actual["popup_stack"], list)
        for popup in actual["popup_stack"]:
            _integer(popup["id"], positive=True)
            assert isinstance(popup["window"], str)
        for control in rendered["dialog"]["controls"].values():
            _actual_control(control)
    assert not held_keys and buttons == [False, False, False]
    assert len({session["session_id"] for session in sessions}) == 1
    assert len({session["context_id"] for session in sessions}) == 1
    for previous, current in zip(sessions, sessions[1:]):
        assert current["imgui_frame"] == previous["imgui_frame"] + 1
        assert current["imgui_time"] > previous["imgui_time"]
    covered_click_frames = set()
    for observation in row["control_observations"]:
        ordinal = _integer(observation["before_frame_ordinal"], positive=True)
        assert ordinal + 3 <= len(frames)
        selector, control = observation["selector"], observation["control"]
        assert control == _control(frames[ordinal - 1]["rendered"], selector)
        _actual_control(control, tab=selector.startswith("document."))
        point = observation["position"]
        assert point == [math.floor(value) for value in control["point"]]
        rect = control["rect"]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        assert not covered_click_frames.intersection(range(ordinal + 1, ordinal + 4))
        covered_click_frames.update(range(ordinal + 1, ordinal + 4))
        if ".choice." in selector:
            assert frames[ordinal - 1]["rendered"]["input_frame"]["popup_stack"]
    for frame in frames:
        if any(event["type"] == "button" for event in frame["requested_events"]):
            assert frame["ordinal"] in covered_click_frames
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "closed"]
    opened, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert opened["context_id"] == sessions[0]["context_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert closed["imgui_frame"] == sessions[-1]["imgui_frame"]


def _selection(value):
    if value is None:
        return
    assert isinstance(value, dict)
    assert isinstance(value["kind"], str) and value["kind"]
    assert isinstance(value["points"], list)
    for point in value["points"]:
        assert isinstance(point, list) and len(point) == 2
        for coordinate in point:
            _number(coordinate)
    assert _number(value["width"]) > 0
    assert isinstance(value["attributes"], dict)


def _snapshot(value, profile):
    assert isinstance(value, dict)
    assert {
        "document_id", "data_identity", "dirty", "undo_steps", "redo_steps",
        "history_limit", "history_pixel_budget", "revisions", "selection",
        "previous_selection", "rois", "metadata", "record_layout", "overlays",
        "spectrum", "display", "dataset",
    } <= value.keys()
    _integer(value["document_id"], positive=True)
    for field in ("data_identity", "undo_steps", "redo_steps", "history_pixel_budget"):
        _integer(value[field])
    _integer(value["history_limit"], positive=True)
    assert type(value["dirty"]) is bool
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for revision in value["revisions"].values():
        _integer(revision)
    _selection(value["selection"])
    _selection(value["previous_selection"])
    assert isinstance(value["rois"], list) and isinstance(value["overlays"], list)
    for roi in value["rois"]:
        assert isinstance(roi["name"], str) and isinstance(roi["attributes"], dict)
        _selection(roi["selection"])
        for axis in ("t", "c", "z"):
            if roi[axis] is not None:
                _integer(roi[axis])
    metadata = value["metadata"]
    assert set(metadata) == {"title", "properties", "extensions"}
    assert isinstance(metadata["title"], str)
    assert isinstance(metadata["properties"], dict) and isinstance(metadata["extensions"], dict)
    assert isinstance(value["record_layout"], dict)
    display = value["display"]
    assert {
        "t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom",
        "plane_zoom", "pan", "properties", "revision",
    } <= display.keys()
    for field in ("t", "c", "revision"):
        _integer(display[field])
    assert len(display["crosshair"]) == 3
    for coordinate in display["crosshair"]:
        _integer(coordinate)
    assert type(display["orthogonal"]) is type(display["linked_zoom"]) is bool
    _number(display["zoom"])
    assert len(display["plane_zoom"]) == len(display["pan"]) == 3
    for zoom in display["plane_zoom"]:
        _number(zoom)
    for point in display["pan"]:
        assert len(point) == 2
        for coordinate in point:
            _number(coordinate)
    assert isinstance(display["properties"], dict)
    dataset = value["dataset"]
    dtype, encoding, _, samples = PROFILES[profile]
    assert dataset["dtype"] == dtype
    assert dataset["pixel_type"] == encoding.removesuffix("LE")
    assert dataset["axes"] == "TCZYXS"
    assert isinstance(dataset["shape"], list) and len(dataset["shape"]) == 6
    for extent in dataset["shape"]:
        _integer(extent, positive=True)
    assert dataset["shape"][-1] == samples
    assert dataset["axis_sizes"] == dict(zip("TCZYXS", dataset["shape"]))
    assert dataset["sample_policy"] == (
        "PreserveQuietNaNFloat32" if profile == "f32_nan" else "FiniteOnly")
    assert set(dataset["calibration"]) == {
        "spacing", "origin", "direction", "unit", "time_unit", "frame_interval", "time_origin"}
    return value


def _usage(value, *, idle=False):
    assert isinstance(value, dict) and set(value) == USAGE_FIELDS
    for count in value.values():
        _integer(count)
        if idle:
            assert count == 0


def _collection(value, profile):
    assert isinstance(value, dict) and {
        "source", "other", "output", "native_member_ids", "facade_ids",
        "active_document_id", "busy", "scheduler_usage", "workflow", "results",
    } <= value.keys()
    for name in ("source", "other", "output"):
        if value[name] is not None:
            _snapshot(value[name], profile)
    assert value["source"] is not None
    if value["other"] is not None:
        assert value["source"]["document_id"] != value["other"]["document_id"]
    for name in ("native_member_ids", "facade_ids"):
        assert isinstance(value[name], list) and len(value[name]) == len(set(value[name]))
        for document_id in value[name]:
            _integer(document_id, positive=True)
    assert set(value["native_member_ids"]) == set(value["facade_ids"])
    represented_ids = {value[name]["document_id"] for name in ("source", "other", "output")
                       if value[name] is not None}
    assert set(value["native_member_ids"]) <= represented_ids, "Unobserved native document leaked into membership"
    for name in ("other", "output"):
        if value[name] is not None:
            assert value[name]["document_id"] in value["native_member_ids"]
    _integer(value["active_document_id"], positive=True)
    assert value["active_document_id"] in value["native_member_ids"]
    assert type(value["busy"]) is bool
    _usage(value["scheduler_usage"])
    assert isinstance(value["workflow"], list) and isinstance(value["results"], list)
    return value


def _semantic(value):
    if isinstance(value, dict):
        return {key: _semantic(item) for key, item in value.items()
                if key not in {"raw_member", "world_points"}}
    if isinstance(value, list):
        return [_semantic(item) for item in value]
    return value


def _same(before, after):
    assert json.dumps(_semantic(before), sort_keys=True, separators=(",", ":")) == json.dumps(
        _semantic(after), sort_keys=True, separators=(",", ":")), (
            "Rejected/canceled export changed actual state or its JSON value types")


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Required installed desktop missing: IMAGEJXX_CPP_EXE")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed C++ desktop")
    executable = Path(configured).resolve(strict=True)
    assert executable.is_file() and executable.name.lower() in {"imagejxx.exe", "imagejxx"}
    assert executable.parent.name.lower() == "bin", "Use the installed desktop bin directory"
    expected = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if _required():
        assert expected, "Required C++ UI gate must pin its installed executable"
    if expected:
        assert re.fullmatch(r"[0-9a-f]{64}", expected)
        assert hashlib.sha256(executable.read_bytes()).hexdigest() == expected
    return executable


def _artifact_tree(row, directory):
    tree = row["artifact_tree"]
    assert tree["excluded_members"] == ["case.json"]
    members = []
    for path in directory.rglob("*"):
        assert not path.is_symlink()
        if path.is_file() and path.relative_to(directory).as_posix() != "case.json":
            relative = path.relative_to(directory).as_posix()
            _member(directory, relative)
            assert path.stat().st_size <= MAX_JSON_BYTES
            members.append([relative, path.stat().st_size, hashlib.sha256(path.read_bytes()).hexdigest()])
    members.sort(key=lambda member: member[0])
    assert 1 <= len(members) <= 1000
    assert sum(member[1] for member in members) <= 512 * 1024 * 1024
    assert tree["members"] == members
    assert tree["member_count"] == len(members)
    assert tree["byte_count"] == sum(member[1] for member in members)
    canonical = json.dumps(members, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    assert tree["sha256"] == hashlib.sha256(canonical).hexdigest()
    raw_members = row["raw_members"]
    assert isinstance(raw_members, list)
    assert len({member["path"] for member in raw_members}) == len(raw_members)
    wanted = {frame["member_id"] for frame in row["frames"]} | {
        member["path"] for member in raw_members}
    assert {member[0] for member in members} == wanted, "Unaccounted case artifact member"
    identities = {member[0]: member[1:] for member in members}
    for raw in raw_members:
        assert identities[raw["path"]] == [raw["byte_count"], raw["sha256"]]


def _public_stimuli(row):
    entries = row["public_stimuli"]
    assert isinstance(entries, list)
    assert [entry["ordinal"] for entry in entries] == list(range(1, len(entries) + 1))
    assert len({entry["label"] for entry in entries}) == len(entries)
    for entry in entries:
        assert set(entry) == {"ordinal", "label", "method", "arguments", "classification",
                              "before", "after", "exception"}
        assert entry["classification"] == "public_state_stimulus_no_physical_credit"
        assert isinstance(entry["method"], str) and entry["method"]
        assert isinstance(entry["arguments"], dict)
        _collection(entry["before"], row["profile"])
        _collection(entry["after"], row["profile"])
        if entry["exception"] is not None:
            assert set(entry["exception"]) == {"type", "message"}
            assert isinstance(entry["exception"]["message"], str) and entry["exception"]["message"]
            _same(entry["before"], entry["after"])
    return {entry["label"]: entry for entry in entries}


def _mutation(before, after, changed_fields, revision):
    for field in before:
        if field not in set(changed_fields) | {"revisions", "raw_member", "world_points"}:
            _same(before[field], after[field])
    for name in ("pixels", "metadata", "selection", "display"):
        assert after["revisions"][name] == before["revisions"][name] + int(name == revision)


def _final(row):
    final = row["states"]["final_idle"]
    _collection(final, row["profile"])
    _usage(final["scheduler_usage"], idle=True)
    assert final["busy"] is False and row["final_busy"] is False
    assert row["window_error"] == ""
    assert row["final_context"] == {"current_context_id": 0}
    assert _frame(row, "final_idle")["rendered"]["dialog"]["open"] is False
    assert _frame(row, "final_idle")["rendered"]["error_modal"]["open"] is False
    last = row["frames"][-1]["rendered"]["input_frame"]
    assert last["keys_down"] == [] and last["mouse_down"] == [False, False, False]


def _initial(row, directory):
    profile = row["profile"]
    ready = row["states"]["source_ready"]
    _collection(ready, profile)
    source, other = ready["source"], ready["other"]
    assert ready["output"] is None and ready["busy"] is False
    _usage(ready["scheduler_usage"], idle=True)
    assert ready["workflow"] == ready["results"] == []
    assert (other is not None) == (row["case_id"] in set(CASE_IDS[7:19]))
    ids = [source["document_id"]] + ([other["document_id"]] if other else [])
    assert ready["native_member_ids"] == ready["facade_ids"] == ids
    assert ready["active_document_id"] == source["document_id"]
    assert source["metadata"] == {
        "title": "Range Source", "properties": {"source_only": "retained property", "revision_probe": 7},
        "extensions": {"source_extension": {"retained": True}},
    }
    assert source["record_layout"] == {}
    assert source["metadata"]["extensions"]["source_extension"]["retained"] is True
    assert source["dirty"] is True
    assert source["undo_steps"] == source["data_identity"] == 1 and source["redo_steps"] == 0
    assert source["history_limit"] == 3 and source["history_pixel_budget"] == 4096
    assert source["revisions"] == {"pixels": 0, "metadata": 1, "selection": 0, "display": 2}
    _calibration(source["dataset"]["calibration"])
    if other:
        assert other["metadata"] == {"title": "Other", "properties": {}, "extensions": {}}
        assert other["dirty"] is False
        assert other["undo_steps"] == other["redo_steps"] == other["data_identity"] == 0
        assert other["history_limit"] == 5 and other["history_pixel_budget"] == 8 * 1024 * 1024
        assert other["revisions"] == {"pixels": 0, "metadata": 0, "selection": 0, "display": 1}
        assert other["selection"] is other["previous_selection"] is other["spectrum"] is None
        assert other["rois"] == other["overlays"] == [] and other["record_layout"] == {}
        _calibration(other["dataset"]["calibration"])
    assert source["display"]["t"] == source["display"]["c"] == 0
    assert source["display"]["crosshair"] == list(CAPTURED_XYZ)
    _literal_selection(source["selection"], "rectangle", [[2, 3], [7, 9]], {"tag": "transient"})
    _literal_selection(source["previous_selection"], "ellipse", [[1, 1], [4, 5]], {"tag": "previous"})
    assert len(source["rois"]) == 2
    for roi, name, kind, points, tag, z, attribute in zip(
            source["rois"], ("Saved rectangle", "Saved second"), ("rectangle", "ellipse"),
            ([[1, 2], [5, 6]], [[10, 3], [15, 7]]), ("saved_first", "saved_second"), (2, 4),
            ("first", "second")):
        assert roi["name"] == name and roi["t"] is roi["c"] is None and roi["z"] == z
        assert roi["attributes"] == {"source_only": attribute}
        _literal_selection(roi["selection"], kind, points, {"tag": tag})
    assert source["overlays"] == [{"source_overlay": "retained"}]
    display = source["display"]
    assert display["orthogonal"] is False and display["linked_zoom"] is True
    assert display["zoom"] == 2 and display["revision"] == 2
    assert display["plane_zoom"] == [0, 0, 0] and display["pan"] == [[0, 0], [0, 0], [0, 0]]
    assert display["properties"] == {"display_range": [0, 255], "lut": "Ice"}
    _source_raw(ready, directory, profile)
    _source_raw(ready, directory, profile, other=True)
    shape = (1, 1, 9, 17, 32, PROFILES[profile][3])
    _raw(row["source_before_raw"], directory, profile, shape, _source_bytes(profile))
    _raw(row["source_after_raw"], directory, profile, shape,
         _source_bytes(profile, edited=row["case_id"] == "U17"))
    if row["case_id"] != "U07":
        assert source["spectrum"] is None
    return ready


def _literal_selection(value, kind, points, attributes):
    _selection(value)
    assert value["kind"] == kind and value["points"] == points
    assert value["width"] == 1 and value["attributes"] == attributes
    for field in ("mask_width", "mask_height"):
        if field in value:
            assert value[field] == 0
    if "packed_mask" in value:
        assert value["packed_mask"] == []


def _stimulus(entry, method, arguments):
    assert entry["method"] == method and entry["arguments"] == arguments
    assert entry["exception"] is None


def _only_source_changed(entry):
    for field in entry["before"]:
        if field != "source":
            _same(entry["before"][field], entry["after"][field])


def _display_change(entry, xyz, *, zoom=None, properties=None):
    before, after = entry["before"]["source"], entry["after"]["source"]
    _only_source_changed(entry)
    _mutation(before, after, {"display"}, "display")
    expected = dict(before["display"])
    expected["crosshair"] = xyz
    expected["revision"] += 1
    if zoom is not None:
        expected["zoom"] = zoom
    if properties is not None:
        expected["properties"] = properties
    assert after["display"] == expected


def _tab_switch(entry, other_id):
    _stimulus(entry, "setActiveIndex", {"document_id": other_id, "index": 1})
    before, after = entry["before"], entry["after"]
    assert before["active_document_id"] == before["source"]["document_id"]
    assert after["active_document_id"] == other_id
    for field in before:
        if field != "active_document_id":
            _same(before[field], after[field])


def _spectrum(value, directory):
    assert isinstance(value, dict) and set(value) == {"width", "height", "original_shape", "raw_member"}
    assert value["width"] == value["height"] == 32 and value["original_shape"] == [17, 32]
    raw = value["raw_member"]
    assert raw["dtype"] == "complex128" and raw["encoding"] == "ComplexFloat64LE"
    assert raw["axes"] == "YX" and raw["shape"] == [32, 32]
    assert raw["sample_count"] == 1024 and raw["byte_count"] == 16384
    assert raw["endian"] == "little"
    path = _member(directory, raw["path"])
    assert path.name == raw["member_id"]
    observed = path.read_bytes()
    assert len(observed) == 16384 and observed == b"\x00" * 16384
    assert raw["sha256"] == hashlib.sha256(observed).hexdigest()


def _sample_bytes(profile, x, y, z, sample):
    if profile == 'f32_nan' and (x, y, z) == (3, 2, 0):
        return struct.pack('<I', 0x7FC12345)
    if profile == 'f64' and (x, y, z) == (13, 7, 4):
        return struct.pack('<d', -0.)
    if profile == 'u8':
        value = (37*x+61*y+13*x*y+43*z) % 251
    elif profile == 'u16':
        value = 6000*z+32*y+x
    elif profile in {'f32', 'f64', 'f32_nan'}:
        value = 10000*z+100*y+x+.25
    else:
        assert profile in {'rgb', 'rgba'}
        value = ((17*x+31*y+47*z) % 256, (71*x+11*y+29*z) % 256,
                 (13*x+61*y+23*z) % 256, (19*x+53*y+7*z+127) % 256)[sample]
    return struct.pack('<'+PROFILES[profile][2], value)

@lru_cache(maxsize=32)
def _source_bytes(profile, *, edited=False, other=False):
    return b''.join(
        struct.pack('<'+PROFILES[profile][2], 187) if other else
        b'\x07' if edited and (x, y, z, s) == (0, 0, 0, 0) else
        _sample_bytes(profile, x, y, z, s)
        for z in range(9) for y in range(17) for x in range(32)
        for s in range(PROFILES[profile][3]))

@lru_cache(maxsize=64)
def _range_bytes(profile, plane, first, last, step):
    a, b, f = AXES[plane]
    chunks = []
    for fixed in range(first, last+1, step):
        for v in range(EXTENTS[b]):
            for u in range(EXTENTS[a]):
                xyz = [0, 0, 0]
                xyz[a], xyz[b], xyz[f] = u, v, fixed
                chunks.extend(_sample_bytes(profile, *xyz, s) for s in range(PROFILES[profile][3]))
    return b''.join(chunks)

def _world(xyz, spacing=SPACING):
    return tuple(ORIGIN[r]+sum(DIRECTION[r][a]*spacing[a]*xyz[a] for a in range(3)) for r in range(3))

def _calibration(value, plane=None, coordinate=0, *, step=1, changed_spacing=False):
    spacing = (.8, 1.2, 3.) if changed_spacing else SPACING
    axes = AXES[plane] if plane else (0, 1, 2)
    xyz = [0, 0, 0]
    if plane:
        xyz[axes[2]] = coordinate
    expected = {'spacing': [spacing[a]*(step if i == 2 and plane else 1)
                            for i, a in enumerate(axes)],
                'origin': list(_world(xyz, spacing)),
                'direction': [DIRECTION[r][a] for r in range(3) for a in axes],
                'unit': 'um', 'time_unit': 's', 'frame_interval': .75, 'time_origin': 2.25}
    assert set(value) == set(expected)
    for k, v in expected.items():
        if isinstance(v, str):
            assert value[k] == v
        else:
            _close(value[k], v)

@pytest.fixture(scope='module')
def range_oracles():
    configured = os.environ.get('IMAGEJXX_AXIS_RANGE_REFERENCE')
    if configured is not None:
        assert configured, 'Empty original range reference path'
        path = Path(__file__).resolve().parents[2] / configured
        assert path.is_file() and not path.is_symlink()
    else:
        candidates = [parent / 'build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001/capture-001/corpus.json'
                      for parent in tuple(Path(__file__).resolve().parents)[:8]]
        found = [p for p in candidates if p.is_file()]
        assert len(found) == 1, 'Supply the actual pinned ImageJ range reference corpus'
        path = found[0]
    raw = path.read_bytes()
    assert len(raw) == 7_876_991 and hashlib.sha256(raw).hexdigest() == REFERENCE_SHA
    corpus = _decode(raw)
    assert corpus['imagej_version'] == '1.54r'
    assert corpus['imagej_commit'] == '62db90a269cccc1ac79f658e4602043214ed055b'
    expected_ids = [f'{p}.{a}.{r}' for p in ('u8', 'u16', 'f32', 'rgb')
                    for a in ('xy', 'xz', 'yz') for r in ('lower', 'full', 'interior_stride', 'upper_stride')]
    assert corpus['exact_ordered_case_ids'] == expected_ids
    assert [c['case_id'] for c in corpus['cases']] == expected_ids
    for row in corpus['cases']:
        profile, plane = row['dtype'], row['plane'].upper()
        assert bytes.fromhex(row['source_before']['raw_hex']) == _source_bytes(profile)
        assert row['source_after']['raw_hex'] == row['source_before']['raw_hex']
        assert bytes.fromhex(row['output']['raw_hex']) == _range_bytes(
            profile, plane, row['first_zero_based'], row['last_inclusive_zero_based'], row['positive_step'])
        assert row['ui_executed'] is False and row['selection_calibration_was_adjusted'] is False
    return {r['case_id']: r for r in corpus['cases']}

def _nonalias(path):
    assert path.is_absolute(), 'Indexed observations require absolute origin paths'
    for component in (path, *path.parents):
        assert not component.is_symlink(), component
        assert not getattr(component, 'is_junction', lambda: False)(), component
    assert path.resolve(strict=True) == path, 'Indexed origin must be canonical and unaliased'
    return path


class _IndexedCaseRows:
    """Load one fully asserted case at a time, retaining only small descriptors."""
    def __init__(self, root, entries):
        self.root = _nonalias(root)
        assert [entry['case_id'] for entry in entries] == list(CASE_IDS)
        assert len({entry['path'] for entry in entries}) == len(CASE_IDS)
        self.entries = {}
        for entry in entries:
            assert set(entry) == {'case_id', 'context', 'path', 'bytes', 'sha256'}
            assert entry['context'] == 'imgui_cpp'
            assert entry['path'] == entry['case_id']+'/case.json'
            assert type(entry['bytes']) is int and 0 < entry['bytes'] <= MAX_JSON_BYTES
            assert re.fullmatch(r'[0-9a-f]{64}', entry['sha256'])
            path = _member(root, entry['path'])
            _nonalias(path)
            assert path.stat().st_size == entry['bytes']
            self.entries[entry['case_id']] = entry

    def __getitem__(self, case_id):
        entry = self.entries[case_id]
        path = _member(self.root, entry['path'])
        _nonalias(path)
        raw = path.read_bytes()
        assert len(raw) == entry['bytes'] <= MAX_JSON_BYTES
        assert hashlib.sha256(raw).hexdigest() == entry['sha256']
        row = _decode(raw)
        assert row['schema_version'] == 1 and row['contract_id'] == CONTRACT
        assert row['case_id'] == case_id and row['context'] == 'imgui_cpp'
        assert row['observation_state'] == 'observed' and row['acceptance_claim'] is False
        return row


def _cpp_report_rows(report, root):
    assert report['schema_version'] == 1 and report['contract_id'] == CONTRACT
    assert report['context'] == 'imgui_cpp' and report['observation_state'] == 'observed'
    assert report['acceptance_claim'] is False and report['case_count'] == 21
    assert report['limits'] == {'max_seconds': 180, 'max_case_frames': 160, 'max_case_seconds': 15}
    assert 0 <= _number(report['elapsed_seconds']) <= 180
    assert [r['case_id'] for r in report['cases']] == list(CASE_IDS)
    if report.get('case_format') == 'indexed_case_files':
        return _IndexedCaseRows(root, report['cases'])
    # Preserve bounded original inline reports. The unchanged _read_json cap
    # still rejects Source002's oversized aggregate; cold indexing is explicit.
    assert 'case_format' not in report
    assert all(r['context'] == 'imgui_cpp' and r['contract_id'] == CONTRACT for r in report['cases'])
    return {r['case_id']: r for r in report['cases']}


@pytest.fixture(scope='module')
def cpp_ui_rows(installed_cpp_executable, tmp_path_factory):
    cold_report = os.environ.get('IMAGEJXX_AXIS_RANGE_CPP_REPORT')
    if cold_report:
        # An explicitly pinned retained observation index is read-only. The
        # executable origin fixture above still verifies the original binary,
        # while no GUI process or target processing is invoked on this path.
        report_path = Path(cold_report)
        _nonalias(report_path)
        expected = os.environ.get('IMAGEJXX_AXIS_RANGE_CPP_REPORT_SHA256')
        assert expected and re.fullmatch(r'[0-9a-f]{64}', expected)
        assert hashlib.sha256(report_path.read_bytes()).hexdigest() == expected
        configured_root = os.environ.get('IMAGEJXX_AXIS_RANGE_CPP_ROOT')
        assert configured_root, 'Cold range index requires its explicit original observation root'
        root = Path(configured_root)
        _nonalias(root)
        return root, _cpp_report_rows(_read_json(report_path), root)
    assert not os.environ.get('IMAGEJXX_AXIS_RANGE_CPP_ROOT')
    assert not os.environ.get('IMAGEJXX_AXIS_RANGE_CPP_REPORT_SHA256')
    root = tmp_path_factory.mktemp('axis-range-reslice-cpp') / 'run'
    started = time.monotonic()
    completed = subprocess.run([str(installed_cpp_executable), '--axis-range-reslice-ui-consumer-test',
                                '--consumer-dir', str(root)], capture_output=True, timeout=180, check=False)
    assert time.monotonic()-started <= 180
    assert len(completed.stdout)+len(completed.stderr) <= 16*1024*1024
    assert completed.returncode == 0, completed.stdout.decode('utf-8', 'replace')+completed.stderr.decode('utf-8', 'replace')
    report = _read_json(root/'axis-range-reslice-ui.json')
    return root, _cpp_report_rows(report, root)

@pytest.fixture(scope='module')
def python_ui_rows(tmp_path_factory):
    desktop = importlib.import_module('imagejxx._desktop') if _required() else pytest.importorskip(
        'imagejxx._desktop', exc_type=ModuleNotFoundError)
    module = Path(desktop.__file__).resolve(strict=True)
    assert module.suffix.lower() in {'.pyd', '.so'}
    origins = {Path(sysconfig.get_path(k)).resolve()/'imagejxx' for k in ('platlib', 'purelib')}
    assert any(module.is_relative_to(p) for p in origins), 'UI reader imported checkout code'
    expected = os.environ.get('IMAGEJXX_DESKTOP_MODULE_SHA256')
    if _required():
        assert expected, 'Required installed UI gate must pin the desktop module'
    if expected:
        assert re.fullmatch(r'[0-9a-f]{64}', expected) and hashlib.sha256(module.read_bytes()).hexdigest() == expected
    root = tmp_path_factory.mktemp('axis-range-reslice-python')
    rows, started = {}, time.monotonic()
    for case_id in CASE_IDS:
        editor = desktop.Editor(preferences_path=str(root/f'{case_id}-preferences.json'))
        case_started = time.monotonic()
        row = _decode(editor._axis_range_reslice_ui_case(case_id=case_id, context='imgui_python', directory=str(root/case_id)))
        assert time.monotonic()-case_started <= 15 and time.monotonic()-started <= 180
        rows[case_id] = row
        del editor
    assert tuple(rows) == CASE_IDS
    return root, rows

def _source_raw(state, directory, profile, *, edited=False, other=False):
    source = state['other' if other else 'source']
    if source is None:
        assert other
        return
    _raw(source['raw_member'], directory, profile, (1, 1, 9, 17, 32, PROFILES[profile][3]),
         _source_bytes(profile, edited=edited, other=other))

def _output(snapshot, row, directory, oracles):
    profile, plane, first, last, step = CASES[row['case_id']]
    _snapshot(snapshot, profile)
    a, b, f = AXES[plane]
    count = 1+(last-first)//step
    shape = (1, 1, count, EXTENTS[b], EXTENTS[a], PROFILES[profile][3])
    expected = _range_bytes(profile, plane, first, last, step)
    _raw(snapshot['raw_member'], directory, profile, shape, expected)
    reference_id = {'U01': 'u8.xy.full', 'U02': 'u16.xz.full', 'U03': 'f32.yz.full',
                    'U04': 'u8.xy.interior_stride', 'U05': 'u16.xz.interior_stride',
                    'U06': 'f32.yz.interior_stride', 'U08': 'rgb.xy.interior_stride',
                    'U15': 'f32.xz.interior_stride'}.get(row['case_id'])
    if reference_id:
        assert bytes.fromhex(oracles[reference_id]['output']['raw_hex']) == expected
    _calibration(snapshot['dataset']['calibration'], plane, first, step=step)
    points = snapshot['world_points']
    assert len(points) == count*shape[3]*shape[4]
    for p, (k, v, u) in zip(points, ((k, v, u) for k in range(count)
                                   for v in range(shape[3]) for u in range(shape[4]))):
        assert p['voxel_xyz'] == [u, v, k]
        xyz = [0, 0, 0]
        xyz[a], xyz[b], xyz[f] = u, v, first+k*step
        _close(p['world_xyz'], _world(xyz))
    assert snapshot['selection'] is snapshot['previous_selection'] is snapshot['spectrum'] is None
    assert snapshot['rois'] == snapshot['overlays'] == [] and snapshot['record_layout'] == {}
    source_id = row['states']['source_ready']['source']['document_id']
    assert snapshot['metadata'] == {
        'title': f'Range Source {plane} {first+1}-{last+1} step {step}', 'properties': {},
        'extensions': {'axis_range_reslice': {'schema_version': 1, 'source_document_id': source_id,
            'plane': plane, 'first': first, 'last': last, 'step': step, 'count': count, 't': 0, 'c': 0,
            'horizontal_axis': 'XYZ'[a], 'vertical_axis': 'XYZ'[b], 'fixed_axis': 'XYZ'[f],
            'index_convention': 'zero_based_voxel_centers'}}}
    assert snapshot['dirty'] is False and snapshot['data_identity'] == snapshot['undo_steps'] == snapshot['redo_steps'] == 0
    assert snapshot['history_limit'] == 5 and snapshot['history_pixel_budget'] == 8*1024*1024
    display = snapshot['display']
    assert display['t'] == display['c'] == 0 and display['properties'] == {}
    assert display['orthogonal'] is False and display['linked_zoom'] is True
    assert display['plane_zoom'] == [0, 0, 0] and display['pan'] == [[0, 0], [0, 0], [0, 0]]
    _close(display['zoom'], 1.25/(SPACING[a]*math.sqrt(sum(DIRECTION[r][a]**2 for r in range(3)))))
    if display['revision'] == 1:
        assert display['crosshair'] == [shape[4]//2, shape[3]//2, 0]
    else:
        assert row['case_id'] == 'U21' and display['revision'] == 2 and display['crosshair'] == [3, 2, 8]
    assert snapshot['revisions'] == {'pixels': 0, 'metadata': 0, 'selection': 0, 'display': display['revision']}

def _dialog(row, label, plane, first, last, step, source_id):
    d = _frame(row, label)['rendered']['dialog']
    assert d['open'] is True and d['title'] == 'Reslice range' and d['is_operation'] is False
    assert d['opened_document_id'] == source_id and d['opened_document_id_semantics'] == 'captured_native_execution_source'
    assert d['retained_source'] is True and d['values']['plane'] == plane
    assert d['text'] == {'plane': plane, 'first': str(first), 'last': str(last), 'step': str(step)}
    assert d['fixed_axis_label'] == f'{"XYZ"[AXES[plane][2]]} indices: 1 to {EXTENTS[AXES[plane][2]]}'
    valid = 1 <= first <= last <= EXTENTS[AXES[plane][2]] and step > 0
    assert d['emitted_plane_count'] == (1+(last-first)//step if valid else None)
    schema = {f['name']: f for f in d['schema']}
    assert set(schema) == {'plane', 'first', 'last', 'step'}
    assert schema['plane']['choices'] == ['XY', 'XZ', 'YZ'] and schema['plane']['label'] == 'Plane'
    for field, text in (('first', 'First (1-based)'), ('last', 'Last (inclusive, 1-based)'), ('step', 'Step')):
        assert schema[field]['kind'] == 'int' and schema[field]['label'] == text
    return d

def _typed_inputs(row):
    for frame in row['frames']:
        texts = [e['value'] for e in frame['requested_events'] if e['type'] == 'text']
        if not texts:
            continue
        assert len(texts) == 1
        ordinal = frame['ordinal']
        down, up = row['frames'][ordinal-3:ordinal-1]
        for f, value in ((down, True), (up, False)):
            assert f['requested_events'] == [{'type': 'keys', 'keys': [{'name': 'Ctrl', 'down': value}, {'name': 'A', 'down': value}]}]
        rendered = frame['rendered']
        field = frame['label'].removesuffix('.text').rsplit('.', 1)[-1]
        field = field if field in {'first', 'last', 'step'} else field.removeprefix('invalid_')
        assert field in {'first', 'last', 'step'}
        control, actual = rendered['dialog']['controls'][field], rendered['input_frame']['input_text']
        assert control['active'] is True and actual['id'] == control['id'] == rendered['input_frame']['focus']['active_id']
        assert rendered['dialog']['text'][field] == texts[0]
        assert actual['length'] == len(texts[0].encode('utf-8')) and actual['cursor'] == actual['length']
        assert actual['selection_start'] == actual['selection_end']
        selected = down['rendered']['input_frame']['input_text']
        assert selected['id'] == control['id'] and selected['length'] > 0
        assert abs(selected['selection_end']-selected['selection_start']) == selected['length']
        assert any(o['selector'] == 'dialog.'+field and o['before_frame_ordinal']+3 < down['ordinal']
                   for o in row['control_observations'])

def _publication(row, directory, oracles):
    observations = row['publication_observations']
    if row['case_id'] not in SUCCESS_IDS:
        assert observations == [] and row['states']['final_idle']['output'] is None
        assert 'output_raw' not in row
        return
    assert [o['phase'] for o in observations] == ['before_native_publish', 'after_attach']
    before, after = observations
    profile, plane, first, last, step = CASES[row['case_id']]
    source = row['states']['source_ready']['source']
    for observation in observations:
        assert set(observation) == {'phase', 'captured_source', 'request', 'candidate', 'state'}
        _same(observation['captured_source'], source)
        request = observation['request']
        assert {k: request[k] for k in ('plane', 'first', 'last', 'step', 't', 'c')} == {
            'plane': plane, 'first': first, 'last': last, 'step': step, 't': 0, 'c': 0}
        assert set(request) == {'plane', 'first', 'last', 'step', 't', 'c', 'max_working_bytes'}
        _integer(request['max_working_bytes'], positive=True)
        _output(observation['candidate'], row, directory, oracles)
    candidate = before['candidate']['document_id']
    assert candidate not in before['state']['native_member_ids'] and before['state']['output'] is None
    _same(before['candidate'], after['candidate'])
    assert after['state']['native_member_ids'] == before['state']['native_member_ids']+[candidate]
    assert after['state']['facade_ids'] == before['state']['facade_ids']+[candidate]
    assert after['state']['active_document_id'] == candidate
    _same(after['candidate'], after['state']['output'])
    _same(row['states']['before_submit'], before['state'])
    _same(row['states']['after_submit'], after['state'])
    for field in ('source', 'other', 'busy', 'scheduler_usage', 'workflow', 'results'):
        _same(before['state'][field], after['state'][field])
    output = row['states']['final_idle']['output']
    _output(output, row, directory, oracles)
    _raw(row['output_raw'], directory, profile, tuple(output['dataset']['shape']), _range_bytes(profile, plane, first, last, step))

def _scenario(row):
    case, states = row['case_id'], row['states']
    source_id = states['source_ready']['source']['document_id']
    _dialog(row, 'dialog_opened', 'XZ', 1, 17, 1, source_id)
    selectors = [c['selector'] for c in row['control_observations']]
    assert selectors[:3] == ['menu.Image', 'menu.Image.Stacks', 'menu.action.show_axis_range_reslice']
    assert selectors.count('menu.action.show_axis_range_reslice') == 1
    _typed_inputs(row)
    stimuli = _public_stimuli(row)
    labels = {'U08': ['navigation_1', 'tab_switch'], 'U09': ['tab_switch'],
        'U10': ['tab_switch'], 'U11': ['tab_switch'], 'U12': ['tab_switch'], 'U13': ['tab_switch'],
        'U14': ['tab_switch'], 'U15': ['tab_switch'],
        'U16': ['held_job_start', 'tab_switch', 'busy_action', 'held_job_cancel', 'held_job_release', 'held_job_wait', 'held_job_drain'],
        'U17': ['pixel_edit', 'tab_switch'], 'U18': ['calibration_edit', 'tab_switch'],
        'U19': ['source_close'], 'U20': ['selection_edit', 'display_edit'], 'U21': ['output_navigation']}.get(case, [])
    assert list(stimuli) == labels
    if 'tab_switch' in stimuli:
        _tab_switch(stimuli['tab_switch'], states['source_ready']['other']['document_id'])
        assert not any(s.startswith('document.') for s in selectors)
    if case in SUCCESS_IDS:
        _, plane, first, last, step = CASES[case]
        _dialog(row, 'before_submit', plane, first+1, last+1, step, source_id)
        assert _frame(row, 'after_submit')['rendered']['dialog']['open'] is False
        assert selectors.count('dialog.apply') == (2 if case == 'U15' else 1)
    if case == 'U08':
        for label, plane, first, last, step in (
            ('xz_range_edited', 'XZ', 2, 16, 3), ('after_navigation_1', 'XZ', 2, 16, 3),
            ('same_axis_retained', 'XZ', 2, 16, 3), ('yz_axis_reset', 'YZ', 1, 32, 1),
            ('yz_range_edited', 'YZ', 2, 31, 3), ('captured_draft_retained', 'YZ', 2, 31, 3),
            ('xy_axis_reset', 'XY', 1, 9, 1), ('xy_range_edited', 'XY', 2, 8, 3)):
            _dialog(row, label, plane, first, last, step, source_id)
        _display_change(stimuli['navigation_1'], [3, 2, 1])
    if case == 'U09':
        _same(states['before_cancel'], states['after_cancel'])
        assert 'dialog.apply' not in selectors and 'dialog.cancel' in selectors
    rejected_cases = {'U10', 'U11', 'U12', 'U13', 'U14', 'U15', 'U17', 'U18', 'U19'}
    if case in rejected_cases:
        assert len(row['rejections']) == 1
        rejected = row['rejections'][0]
        _same(rejected['before'], rejected['after'])
        assert rejected['before']['output'] is None and rejected['captured_document_id'] == source_id
        assert rejected['captured_revisions'] == states['source_ready']['source']['revisions']
        assert rejected['error_message'] and rejected['error_message'] == _frame(row, 'after_rejection')['rendered']['window_error']
        error = _frame(row, 'after_rejection')['rendered']['error_modal']
        assert error['open'] is True and error['message'] == rejected['error_message']
        assert _frame(row, 'error_acknowledged')['rendered']['error_modal']['open'] is False
        assert _frame(row, 'error_acknowledged')['rendered']['window_error'] == ''
        assert 'error_modal.ok' in selectors
        if case in {'U10', 'U11', 'U12', 'U13', 'U14', 'U15'}:
            _, plane, first, last, step = CASES[case if case != 'U15' else 'U10']
            assert rejected['request'] == {'plane': plane, 'first': first, 'last': last, 'step': step, 't': 0, 'c': 0}
            for label in ('after_rejection', 'draft_restored'):
                _dialog(row, label, plane, first+1, last+1, step, source_id)
        elif case == 'U17':
            change = stimuli['pixel_edit']
            _mutation(change['before']['source'], change['after']['source'], {'data_identity', 'undo_steps', 'redo_steps', 'dirty'}, 'pixels')
            assert change['after']['source']['data_identity'] == 2 and change['after']['source']['undo_steps'] == 0
        elif case == 'U18':
            change = stimuli['calibration_edit']
            _mutation(change['before']['source'], change['after']['source'], {'dataset', 'data_identity', 'undo_steps', 'dirty'}, 'metadata')
            _calibration(change['after']['source']['dataset']['calibration'], changed_spacing=True)
        else:
            change = stimuli['source_close']
            assert source_id not in change['after']['native_member_ids']
            _same(change['before']['source'], change['after']['source'])
    elif case != 'U16':
        assert row['rejections'] == []
    if case == 'U16':
        start, terminal = row['job_before_cleanup'], row['job_after_cleanup']
        assert start['state'] == 'running' and start['busy'] is True and start['worker_entered'] is True
        assert terminal['id'] == start['id'] and terminal['state'] == 'canceled' and terminal['cancel_requested'] is True
        assert terminal['busy'] is False
        _usage(terminal['usage'], idle=True)
        for label in ('before_disabled_submit', 'after_disabled_submit'):
            assert _frame(row, label)['rendered']['dialog']['controls']['apply']['enabled'] is False
        _same(states['before_disabled_submit'], states['after_disabled_submit'])
        rejected = row['rejections'][0]
        assert len(row['rejections']) == 1 and rejected['exception']['type'] == 'invalid_argument'
        _same(rejected['before'], rejected['after'])
        action = stimuli['busy_action']
        assert action['method'] == 'action.reslice_axis_range'
        assert action['arguments'] == {'document_id': source_id, 'plane': 'XZ', 'first': 0, 'last': 16, 'step': 1}
        assert action['exception'] == rejected['exception']
        assert 'dialog.cancel' in selectors
    if case == 'U20':
        change = stimuli['selection_edit']
        _mutation(change['before']['source'], change['after']['source'], {'selection', 'previous_selection'}, 'selection')
        _literal_selection(change['after']['source']['selection'], 'ellipse', [[8, 4], [13, 8]], {})
        _same(change['after']['source']['previous_selection'], change['before']['source']['selection'])
        _display_change(stimuli['display_edit'], [3, 2, 1], zoom=3, properties={'display_range': [0, 255], 'lut': 'Ice'})

def _all_raw(row, directory, oracles):
    profile = row['profile']
    referenced = {}
    def walk(value):
        if isinstance(value, dict):
            if {'member_id', 'path', 'dtype', 'encoding', 'sha256', 'byte_count'} <= value.keys():
                assert value['path'] not in referenced or referenced[value['path']] == value
                referenced[value['path']] = value
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
    walk({k: v for k, v in row.items() if k not in {'raw_members', 'artifact_tree'}})
    assert {m['path']: m for m in row['raw_members']} == referenced
    states = list(row['states'].values())
    for e in row['public_stimuli']+row['rejections']:
        states.extend((e['before'], e['after']))
    states.extend(o['state'] for o in row['publication_observations'])
    for state in states:
        _collection(state, profile)
        edited = row['case_id'] == 'U17' and state['source']['revisions']['pixels'] > 0
        _source_raw(state, directory, profile, edited=edited)
        _source_raw(state, directory, profile, other=True)
        if state['output']:
            _output(state['output'], row, directory, oracles)
        if row['case_id'] == 'U07':
            _spectrum(state['source']['spectrum'], directory)
    for o in row['publication_observations']:
        _raw(o['captured_source']['raw_member'], directory, profile, (1, 1, 9, 17, 32, PROFILES[profile][3]), _source_bytes(profile))
        if row['case_id'] == 'U07':
            _spectrum(o['captured_source']['spectrum'], directory)
    if row['case_id'] == 'U21':
        shown = row['displayed_output']
        output = row['states']['final_idle']['output']
        assert shown['document_id'] == output['document_id'] and shown['slice'] == 8
        assert shown['probe_voxel_xyz'] == [3, 2, 8]
        _close(shown['probe_world_xyz'], _world((3, 2, 8)))
        _raw(shown['frame_raw'], directory, profile, (1, 1, 1, 17, 32, 1), _range_bytes(profile, 'XY', 8, 8, 1))
    else:
        assert 'displayed_output' not in row

def _preserved(row):
    states = row['states']
    ready, final = states['source_ready'], states['final_idle']
    latest = ready['source']
    for entry in row['public_stimuli']:
        _same(latest, entry['before']['source'])
        if entry['label'] in {'navigation_1', 'pixel_edit', 'calibration_edit', 'selection_edit', 'display_edit'}:
            latest = entry['after']['source']
        else:
            _same(entry['before']['source'], entry['after']['source'])
    _same(latest, final['source'])
    _same(ready['other'], final['other'])
    assert final['workflow'] == ready['workflow'] == [] and final['results'] == ready['results'] == []
    initial_ids = ready['native_member_ids']
    expected_ids = [i for i in initial_ids if row['case_id'] != 'U19' or i != ready['source']['document_id']]
    if row['case_id'] in SUCCESS_IDS:
        expected_ids.append(final['output']['document_id'])
    assert final['native_member_ids'] == final['facade_ids'] == expected_ids

@pytest.mark.parametrize('case_id,context', [(c, ctx) for c in CASE_IDS for ctx in CONTEXTS],
                         ids=[f'{c}-{ctx}' for c in CASE_IDS for ctx in CONTEXTS])
def test_installed_axis_range_reslice_ui(case_id, context, range_oracles, request):
    parent, rows = request.getfixturevalue('cpp_ui_rows' if context == 'imgui_cpp' else 'python_ui_rows')
    directory, row = parent/case_id, rows[case_id]
    assert row['schema_version'] == 1 and row['contract_id'] == CONTRACT
    assert row['case_id'] == case_id and row['context'] == context and row['profile'] == CASES[case_id][0]
    assert row['observation_state'] == 'observed' and row['acceptance_claim'] is False
    assert row['limits'] == {'max_frames': 160, 'max_seconds': 15} and 0 <= _number(row['elapsed_seconds']) <= 15
    assert _read_json(directory/'case.json') == row
    _initial(row, directory)
    _physical_observations(row, directory)
    _scenario(row)
    _publication(row, directory, range_oracles)
    _all_raw(row, directory, range_oracles)
    _preserved(row)
    _final(row)
    _artifact_tree(row, directory)
