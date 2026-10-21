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

CONTRACT = 'p05h.axis-range-orientation-ui.001'
CASE_IDS = tuple(f'O{i:02d}' for i in range(1, 20))
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
    **{f'O{i:02d}': ('u16','XZ',1,15,3) for i in range(1,9)},
    'O09':('rgb','YZ',1,30,3),'O10':('f64','XY',1,7,3),'O11':('u8','XZ',16,16,3),
    'O12':('u16','XY',1,7,3),'O13':('u8','XZ',1,15,3),'O14':('f32','XZ',1,15,3),
    'O15':('u8','XZ',0,16,1),'O16':('u16','YZ',0,31,1),'O17':('u8','XZ',0,16,1),
    'O18':('f32','XZ',0,16,1),'O19':('rgba','XZ',1,15,3),
}
FLAGS = {case: (bool((i-1)&4),bool((i-1)&2),bool((i-1)&1)) if i<=8 else (True,True,True)
         for i,case in enumerate(CASE_IDS,1)}
SUCCESS_IDS = set(CASE_IDS[:12]) | {'O14','O19'}
KEYS = {
    'Escape', 'Delete', 'Left', 'Right', 'Up', 'Down', 'PageUp', 'PageDown',
    'Home', 'End', 'Space', 'Shift', 'Ctrl', 'Alt', 'Super', 'A', 'W',
    'Enter', 'Tab', 'Backspace',
}
USAGE_FIELDS = {'running', 'queued', 'running_bytes', 'retained_bytes', 'reserved_bytes'}


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
    assert (other is not None) == (row["case_id"] in set(CASE_IDS[11:19]))
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
         _source_bytes(profile, edited=row["case_id"] == "O15"))
    if row["case_id"] != "O10":
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
def _oriented_bytes(profile,plane,first,last,step,reverse,flip,transpose):
    a,b,f=AXES[plane]
    selected=list(range(first,last+1,step))
    if reverse:selected.reverse()
    H,W=(EXTENTS[a],EXTENTS[b]) if transpose else (EXTENTS[b],EXTENTS[a])
    chunks=[]
    for fixed in selected:
        for v in range(H):
            for u in range(W):
                p,j=(v,u) if transpose else (u,v)
                xyz=[0,0,0];xyz[a]=p;xyz[b]=EXTENTS[b]-1-j if flip else j;xyz[f]=fixed
                chunks.extend(_sample_bytes(profile,*xyz,k) for k in range(PROFILES[profile][3]))
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
    assert report['acceptance_claim'] is False and report['case_count'] == 19
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
    cold_report = os.environ.get('IMAGEJXX_AXIS_RANGE_ORIENTATION_CPP_REPORT')
    if cold_report:
        # An explicitly pinned retained observation index is read-only. The
        # executable origin fixture above still verifies the original binary,
        # while no GUI process or target processing is invoked on this path.
        report_path = Path(cold_report)
        _nonalias(report_path)
        expected = os.environ.get('IMAGEJXX_AXIS_RANGE_ORIENTATION_CPP_REPORT_SHA256')
        assert expected and re.fullmatch(r'[0-9a-f]{64}', expected)
        assert hashlib.sha256(report_path.read_bytes()).hexdigest() == expected
        configured_root = os.environ.get('IMAGEJXX_AXIS_RANGE_ORIENTATION_CPP_ROOT')
        assert configured_root, 'Cold range index requires its explicit original observation root'
        root = Path(configured_root)
        _nonalias(root)
        return root, _cpp_report_rows(_read_json(report_path), root)
    assert not os.environ.get('IMAGEJXX_AXIS_RANGE_ORIENTATION_CPP_ROOT')
    assert not os.environ.get('IMAGEJXX_AXIS_RANGE_ORIENTATION_CPP_REPORT_SHA256')
    root = tmp_path_factory.mktemp('axis-range-orientation-cpp') / 'run'
    started = time.monotonic()
    completed = subprocess.run([str(installed_cpp_executable), '--axis-range-orientation-ui-consumer-test',
                                '--consumer-dir', str(root)], capture_output=True, timeout=180, check=False)
    assert time.monotonic()-started <= 180
    assert len(completed.stdout)+len(completed.stderr) <= 16*1024*1024
    assert completed.returncode == 0, completed.stdout.decode('utf-8', 'replace')+completed.stderr.decode('utf-8', 'replace')
    report = _read_json(root/'axis-range-orientation-ui.json')
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
    root = tmp_path_factory.mktemp('axis-range-orientation-python')
    rows, started = {}, time.monotonic()
    for case_id in CASE_IDS:
        editor = desktop.Editor(preferences_path=str(root/f'{case_id}-preferences.json'))
        case_started = time.monotonic()
        row = _decode(editor._axis_range_orientation_ui_case(case_id=case_id, context='imgui_python', directory=str(root/case_id)))
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

def _output(snapshot,row,directory):
    profile,plane,first,last,step=CASES[row['case_id']]
    reverse,flip,transpose=FLAGS[row['case_id']]
    _snapshot(snapshot,profile)
    a,b,f=AXES[plane];count=1+(last-first)//step;last_selected=first+(count-1)*step
    selected_first=last_selected if reverse else first
    axes=(b,a,f) if transpose else (a,b,f)
    signs=((-1 if flip else 1),1,(-1 if reverse else 1)) if transpose else (1,(-1 if flip else 1),(-1 if reverse else 1))
    shape=(1,1,count,EXTENTS[axes[1]],EXTENTS[axes[0]],PROFILES[profile][3])
    expected=_oriented_bytes(profile,plane,first,last,step,reverse,flip,transpose)
    _raw(snapshot['raw_member'],directory,profile,shape,expected)
    q0=[0,0,0];q0[b]=EXTENTS[b]-1 if flip else 0;q0[f]=selected_first
    cal=snapshot['dataset']['calibration']
    assert set(cal)=={'spacing','origin','direction','unit','time_unit','frame_interval','time_origin'}
    _close(cal['spacing'],[SPACING[axes[0]],SPACING[axes[1]],SPACING[f]*step])
    _close(cal['origin'],_world(q0))
    _close(cal['direction'],[DIRECTION[r][axis]*signs[i] for r in range(3) for i,axis in enumerate(axes)])
    assert (cal['unit'],cal['time_unit'],cal['frame_interval'],cal['time_origin'])==('um','s',.75,2.25)
    points=snapshot['world_points'];assert len(points)==count*shape[3]*shape[4]
    for observed,(k,v,u) in zip(points,((k,v,u) for k in range(count) for v in range(shape[3]) for u in range(shape[4]))):
        assert observed['voxel_xyz']==[u,v,k]
        p,j=(v,u) if transpose else (u,v)
        q=[0,0,0];q[a]=p;q[b]=EXTENTS[b]-1-j if flip else j
        q[f]=last_selected-k*step if reverse else first+k*step
        _close(observed['world_xyz'],_world(q))
    assert snapshot['selection'] is snapshot['previous_selection'] is snapshot['spectrum'] is None
    assert snapshot['rois']==snapshot['overlays']==[] and snapshot['record_layout']=={}
    metadata=snapshot['metadata'];assert metadata['properties']=={} and set(metadata['extensions'])=={'axis_range_orientation'}
    assert metadata['title']==f'Range Source {plane} {first+1}-{last+1} step {step} [R{int(reverse)} F{int(flip)} T{int(transpose)}]'
    provenance=metadata['extensions']['axis_range_orientation']
    expected_provenance={'schema_version':1,'source_document_id':row['states']['source_ready']['source']['document_id'],
        'plane':plane,'first':first,'last':last,'step':step,'count':count,'last_selected':last_selected,'t':0,'c':0,
        'reverse_order':reverse,'flip_vertical':flip,'transpose':transpose,
        'horizontal_axis':'XYZ'[axes[0]],'vertical_axis':'XYZ'[axes[1]],'fixed_axis':'XYZ'[f],
        'index_convention':'zero_based_voxel_centers'}
    assert provenance==expected_provenance
    assert snapshot['dirty'] is False and snapshot['data_identity']==snapshot['undo_steps']==snapshot['redo_steps']==0
    assert snapshot['history_limit']==5 and snapshot['history_pixel_budget']==8*1024*1024
    display=snapshot['display']
    assert display['t']==display['c']==0 and display['properties']=={}
    assert display['orthogonal'] is False and display['linked_zoom'] is True
    assert display['plane_zoom']==[0,0,0] and display['pan']==[[0,0],[0,0],[0,0]]
    _close(display['zoom'],1.25/(SPACING[axes[0]]*math.sqrt(sum(DIRECTION[r][axes[0]]**2 for r in range(3)))))
    assert display['crosshair']==[shape[4]//2,shape[3]//2,0] and display['revision']==1
    assert snapshot['revisions']=={'pixels':0,'metadata':0,'selection':0,'display':1}

def _dialog(row,label,plane,first,last,step,source_id,flags=None):
    flags=FLAGS[row['case_id']] if flags is None else flags
    d=_frame(row,label)['rendered']['dialog']
    assert d['open'] is True and d['title']=='Reslice oriented range' and d['is_operation'] is False
    assert d['opened_document_id']==source_id and d['opened_document_id_semantics']=='captured_native_execution_source'
    assert d['retained_source'] is True
    assert {k:d['values'][k] for k in ('plane','first','last','step')}=={'plane':plane,'first':first,'last':last,'step':step}
    assert tuple(d['values'][k] for k in ('reverse_order','flip_vertical','transpose'))==flags
    assert all(type(d['values'][k]) is bool for k in ('reverse_order','flip_vertical','transpose'))
    assert {k:d['text'][k] for k in ('plane','first','last','step')}=={'plane':plane,'first':str(first),'last':str(last),'step':str(step)}
    schema={r['name']:r for r in d['schema']}
    assert set(schema)=={'plane','first','last','step','reverse_order','flip_vertical','transpose'}
    assert schema['plane']['choices']==['XY','XZ','YZ']
    for key,label_text in (('first','First (1-based)'),('last','Last (inclusive, 1-based)'),('step','Step')):
        assert schema[key]['kind']=='int' and schema[key]['label']==label_text
        control=d['controls'][key];_actual_control(control)
        assert control['identity_source']=='current_scoped_InputText_text_label_id'
        assert control['geometry_source']=='actual_InputText_item_rect'
    for key,label_text in (('reverse_order','Reverse selected planes'),('flip_vertical','Flip input vertical axis'),('transpose','Transpose axes (ImageJ Rotate90 output)')):
        assert schema[key]['kind']=='bool' and schema[key]['label']==label_text
        _actual_control(d['controls'][key])
    a,b,f=AXES[plane];reverse,flip,transpose=flags
    assert d['fixed_axis_label']==f'{"XYZ"[f]} indices: 1 to {EXTENTS[f]}'
    valid=1<=first<=last<=EXTENTS[f] and step>0
    assert d['emitted_plane_count']==(1+(last-first)//step if valid else None)
    if valid:
        count=1+(last-first)//step;selected_last=first+(count-1)*step
        axes=(b,a,f) if transpose else (a,b,f)
        preview={'effective_first_selected_1based':selected_last if reverse else first,
            'effective_last_selected_1based':first if reverse else selected_last,
            'last_selected_1based':selected_last,'requested_last_1based':last,
            'output_axis_order':['XYZ'[axis] for axis in axes],
            'output_axis_signs':[-1 if transpose and flip else 1,-1 if not transpose and flip else 1,-1 if reverse else 1],
            'emitted_plane_count':count,'flip_before_transpose':True}
        actual = d['orientation_preview']
        assert set(actual) == set(preview) | {'can_apply', 'working_bytes', 'max_working_bytes'}
        assert {k:v for k,v in actual.items() if k in preview} == preview
        assert actual['can_apply'] is True and actual['max_working_bytes'] == 512*1024*1024
        _integer(actual['working_bytes'], positive=True)
        minimum = count * EXTENTS[a] * EXTENTS[b] * PROFILES[row['profile']][3] * 8
        assert minimum <= actual['working_bytes'] <= actual['max_working_bytes']
        assert all(d[k]==v for k,v in preview.items())
    else:
        assert d['orientation_preview'] == {'can_apply': False, 'error': 'Extraction indices and Step must be positive integers'}
        assert d['can_apply'] is False

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
        field = field if field in {'first', 'last', 'step'} else field.removeprefix('invalid_').removeprefix('corrected_')
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

def _publication(row,directory):
    observations=row['publication_observations']
    if row['case_id'] not in SUCCESS_IDS:
        assert observations==[] and row['states']['final_idle']['output'] is None and 'output_raw' not in row
        return
    assert [o['phase'] for o in observations]==['before_native_publish','after_attach']
    before,after=observations;profile,plane,first,last,step=CASES[row['case_id']]
    flags=FLAGS[row['case_id']];source=row['states']['source_ready']['source']
    for o in observations:
        assert set(o)=={'phase','captured_source','request','candidate','state'}
        _same(o['captured_source'],source)
        request=o['request'];assert set(request)=={'range','reverse_order','flip_vertical','transpose'}
        assert tuple(request[k] for k in ('reverse_order','flip_vertical','transpose'))==flags
        assert all(type(request[k]) is bool for k in ('reverse_order','flip_vertical','transpose'))
        r=request['range'];assert set(r)=={'plane','first','last','step','t','c','max_working_bytes'}
        assert {k:r[k] for k in ('plane','first','last','step','t','c')}=={'plane':plane,'first':first,'last':last,'step':step,'t':0,'c':0}
        _integer(r['max_working_bytes'],positive=True);_output(o['candidate'],row,directory)
    candidate=before['candidate']['document_id'];_integer(candidate,positive=True)
    assert candidate!=source['document_id'] and candidate not in before['state']['native_member_ids']
    assert before['state']['output'] is None
    _same(before['candidate'],after['candidate'])
    assert after['state']['native_member_ids']==before['state']['native_member_ids']+[candidate]
    assert after['state']['facade_ids']==before['state']['facade_ids']+[candidate]
    assert after['state']['active_document_id']==candidate
    _same(after['candidate'],after['state']['output'])
    _same(row['states']['before_submit'],before['state']);_same(row['states']['after_submit'],after['state'])
    for field in ('source','other','busy','scheduler_usage','workflow','results'):_same(before['state'][field],after['state'][field])
    output=row['states']['final_idle']['output'];_output(output,row,directory)
    _raw(row['output_raw'],directory,profile,tuple(output['dataset']['shape']),_oriented_bytes(profile,plane,first,last,step,*flags))

def _scenario(row):
    case,states=row['case_id'],row['states'];source_id=states['source_ready']['source']['document_id']
    _dialog(row,'dialog_opened','XZ',1,17,1,source_id,(False,False,False))
    selectors=[o['selector'] for o in row['control_observations']]
    assert selectors[:3]==['menu.Image','menu.Image.Stacks','menu.action.show_axis_range_orientation']
    assert selectors.count('menu.action.show_axis_range_orientation')==1
    for i,flag in enumerate(('reverse_order','flip_vertical','transpose')):
        count=1 if FLAGS[case][i] else 2
        assert selectors.count('dialog.'+flag)==count
        observed=_frame(row,'flag.'+flag+'.set')['rendered']['dialog']['values'][flag]
        assert type(observed) is bool and observed is FLAGS[case][i]
    _dialog(row,'flags_selected','XZ',1,17,1,source_id)
    _typed_inputs(row);stimuli=_public_stimuli(row)
    expected={'O12':['navigation_1','tab_switch'],'O13':['tab_switch'],'O14':['tab_switch'],
        'O15':['pixel_edit','tab_switch'],'O16':['calibration_edit','tab_switch'],
        'O17':['held_job_start','tab_switch','busy_action','held_job_cancel','held_job_release','held_job_wait','held_job_drain'],
        'O18':['source_close'],'O19':['selection_edit','display_edit']}.get(case,[])
    assert list(stimuli)==expected
    if 'tab_switch' in stimuli:
        _tab_switch(stimuli['tab_switch'],states['source_ready']['other']['document_id'])
        assert not any(selector.startswith('document.') for selector in selectors)
    if case in SUCCESS_IDS:
        _,plane,first,last,step=CASES[case]
        _dialog(row,'before_submit',plane,first+1,last+1,step,source_id)
        assert _frame(row,'after_submit')['rendered']['dialog']['open'] is False
        assert selectors.count('dialog.apply')==(3 if case=='O14' else 1)
    if case=='O12':
        for label,plane,first,last,step in (('xz_range_edited','XZ',2,16,3),('after_navigation_1','XZ',2,16,3),
            ('same_axis_retained','XZ',2,16,3),('yz_axis_reset','YZ',1,32,1),('yz_range_edited','YZ',2,31,3),
            ('captured_draft_retained','YZ',2,31,3),('xy_axis_reset','XY',1,9,1),('xy_range_edited','XY',2,8,3)):
            _dialog(row,label,plane,first,last,step,source_id)
        _display_change(stimuli['navigation_1'],[3,2,1])
    if case=='O13':
        _same(states['before_cancel'],states['after_cancel'])
        assert 'dialog.apply' not in selectors and selectors.count('dialog.cancel')==1
    rejections=row['rejections'];assert len(rejections)==(2 if case=='O14' else 1 if case in {'O15','O16','O17','O18'} else 0)
    for rejected in rejections:
        _same(rejected['before'],rejected['after'])
        assert rejected['before']['output'] is None and rejected['captured_document_id']==source_id
        assert rejected['captured_revisions']==states['source_ready']['source']['revisions']
        assert tuple(rejected['request'][k] for k in ('reverse_order','flip_vertical','transpose'))==FLAGS[case]
        if case not in {'O14','O17'}:
            assert rejected['error_message'] and rejected['dialog_after']['open'] is True
            assert rejected['dialog_after']['opened_document_id']==source_id
            acknowledged=rejected['acknowledged']
            assert set(acknowledged)==set(rejected['before'])==set(rejected['after'])
            _collection(acknowledged,row['profile'])
            _same(rejected['after'],acknowledged)
            assert acknowledged['output'] is None and acknowledged['busy'] is False
            _usage(acknowledged['scheduler_usage'],idle=True)
            assert acknowledged['active_document_id']==rejected['before']['active_document_id']
            _integer(rejected['acknowledged_frame_ordinal'],positive=True)
        else:
            assert 'acknowledged' not in rejected and 'acknowledged_frame_ordinal' not in rejected
    error_frames=[f for f in row['frames'] if f['label']=='after_rejection' and case!='O14']
    ack_frames=[f for f in row['frames'] if f['label']=='error_acknowledged']
    modal_count=len(rejections) if case not in {'O14','O17'} else 0
    assert len(error_frames)==len(ack_frames)==selectors.count('error_modal.ok')==modal_count
    for rejected,error,ack in zip(rejections,error_frames,ack_frames):
        assert error['rendered']['error_modal']['open'] is True
        assert error['rendered']['error_modal']['message']==rejected['error_message']==error['rendered']['window_error']
        assert ack['rendered']['error_modal']['open'] is False and ack['rendered']['window_error']==''
        assert rejected['before']['active_document_id']==rejected['after']['active_document_id']
        assert rejected['acknowledged_frame_ordinal']==ack['ordinal']
        _same(rejected['acknowledged'],ack['native_state'])
        dialog=ack['rendered']['dialog']
        assert dialog['open'] is True and dialog['retained_source'] is True
        assert dialog['opened_document_id']==source_id
        assert dialog['generation']==rejected['dialog_after']['generation']
        assert dialog['values']==rejected['dialog_after']['values']
        assert dialog['text']==rejected['dialog_after']['text']
    if case=='O14':
        _dialog(row,'draft_restored.first','XZ',0,17,1,source_id)
        _dialog(row,'draft_restored.step','XZ',2,17,0,source_id)
        assert [r['request']['range'] for r in rejections]==[
            {'plane':'XZ','first':-1,'last':16,'step':1,'t':0,'c':0},
            {'plane':'XZ','first':1,'last':16,'step':0,'t':0,'c':0}]
        for rejected in rejections:
            assert rejected['rejection_stage'] == 'preflight' and rejected['error_message']
            frame = row['frames'][rejected['observed_frame_ordinal']-1]
            dialog = frame['rendered']['dialog']
            assert not dialog['can_apply'] and not dialog['controls']['apply']['enabled']
            assert dialog['error'] == rejected['error_message']
            assert not frame['rendered']['window_error'] and not frame['rendered']['error_modal']['open']
        assert rejections[0]['observed_frame_ordinal'] < rejections[1]['observed_frame_ordinal']
        _same(rejections[0]['after'],rejections[1]['before'])
        _same(rejections[1]['after'],states['before_submit'])
    if case=='O15':
        change=stimuli['pixel_edit'];_mutation(change['before']['source'],change['after']['source'],{'data_identity','undo_steps','redo_steps','dirty'},'pixels')
        assert change['after']['source']['data_identity']==2 and change['after']['source']['undo_steps']==0
    if case=='O16':
        change=stimuli['calibration_edit'];_mutation(change['before']['source'],change['after']['source'],{'dataset','data_identity','undo_steps','dirty'},'metadata')
        _calibration(change['after']['source']['dataset']['calibration'],changed_spacing=True)
    if case=='O18':
        change=stimuli['source_close'];assert source_id not in change['after']['native_member_ids']
        _same(change['before']['source'],change['after']['source'])
    if case=='O17':
        start,end=row['job_before_cleanup'],row['job_after_cleanup']
        assert start['state']=='running' and start['busy'] is start['worker_entered'] is True
        assert end['id']==start['id'] and end['state']=='canceled' and end['cancel_requested'] is True and end['busy'] is False
        _usage(end['usage'],idle=True)
        for label in ('before_disabled_submit','after_disabled_submit'):
            assert _frame(row,label)['rendered']['dialog']['controls']['submit']['enabled'] is False
        _same(states['before_disabled_submit'],states['after_disabled_submit'])
        rejected=rejections[0];assert rejected['exception']['type']=='invalid_argument'
        action=stimuli['busy_action'];assert action['method']=='action.reslice_oriented_axis_range'
        assert action['arguments']=={'document_id':source_id,'plane':'XZ','first':0,'last':16,'step':1,
            'reverse_order':True,'flip_vertical':True,'transpose':True}
        assert action['exception']==rejected['exception'] and 'dialog.cancel' in selectors
    if case=='O19':
        change=stimuli['selection_edit'];_mutation(change['before']['source'],change['after']['source'],{'selection','previous_selection'},'selection')
        _literal_selection(change['after']['source']['selection'],'ellipse',[[8,4],[13,8]],{})
        _same(change['after']['source']['previous_selection'],change['before']['source']['selection'])
        _display_change(stimuli['display_edit'],[3,2,1],zoom=3,properties={'display_range':[0,255],'lut':'Ice'})

def _all_raw(row,directory):
    profile=row['profile'];referenced={}
    def walk(value):
        if isinstance(value,dict):
            if {'member_id','path','dtype','encoding','sha256','byte_count'}<=value.keys():
                assert value['path'] not in referenced or referenced[value['path']]==value
                referenced[value['path']]=value
            for child in value.values():walk(child)
        elif isinstance(value,list):
            for child in value:walk(child)
    walk({k:v for k,v in row.items() if k not in {'raw_members','artifact_tree'}})
    assert {m['path']:m for m in row['raw_members']}==referenced
    states=list(row['states'].values())
    for e in row['public_stimuli']+row['rejections']:states.extend((e['before'],e['after']))
    states.extend(e['acknowledged'] for e in row['rejections'] if 'acknowledged' in e)
    states.extend(o['state'] for o in row['publication_observations'])
    for state in states:
        _collection(state,profile)
        _source_raw(state,directory,profile,edited=row['case_id']=='O15' and state['source']['revisions']['pixels']>0)
        _source_raw(state,directory,profile,other=True)
        if state['output']:_output(state['output'],row,directory)
        if row['case_id']=='O10':_spectrum(state['source']['spectrum'],directory)
    for o in row['publication_observations']:
        _raw(o['captured_source']['raw_member'],directory,profile,(1,1,9,17,32,PROFILES[profile][3]),_source_bytes(profile))
        if row['case_id']=='O10':_spectrum(o['captured_source']['spectrum'],directory)
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
    expected_ids = [i for i in initial_ids if row['case_id'] != 'O18' or i != ready['source']['document_id']]
    if row['case_id'] in SUCCESS_IDS:
        expected_ids.append(final['output']['document_id'])
    assert final['native_member_ids'] == final['facade_ids'] == expected_ids

@pytest.mark.parametrize('case_id,context', [(c, ctx) for c in CASE_IDS for ctx in CONTEXTS],
                         ids=[f'{c}-{ctx}' for c in CASE_IDS for ctx in CONTEXTS])
def test_installed_axis_range_orientation_ui(case_id, context, request):
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
    _publication(row, directory)
    _all_raw(row, directory)
    _preserved(row)
    _final(row)
    _artifact_tree(row, directory)
