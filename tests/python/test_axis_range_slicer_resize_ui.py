"""Prospective68 x2 installed Dear ImGui slicer-resize checks.

Actual pointer/key/text delivery and independently derived pixels/nominal affine.
Direct core stimuli carry no physical UI credit. No target calls at collection.
Eachcase160frames/15seconds; eachlaunch300seconds,64MiB/file,2GiB/tree.
"""
import hashlib
import importlib
import json
import math
import os
from functools import lru_cache
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import subprocess
import sysconfig
import time
import pytest
CONTRACT='p05j.axis-range-slicer-resize-ui.001'
CASE_IDS=tuple(f'B{i:02d}' for i in range(1,69))
CONTEXTS=('imgui_cpp','imgui_python')
MAX_JSON_BYTES=64*1024*1024
MAX_TREE_BYTES=2*1024*1024*1024
USAGE_FIELDS={'running','queued','running_bytes','retained_bytes','reserved_bytes'}
KEYS={'Escape','Delete','Left','Right','Up','Down','PageUp','PageDown','Home','End','Space','Shift','Ctrl','Alt','Super','A','W','Enter','Tab','Backspace'}
PROFILES={'u8':('uint8','UInt8','B',1),'u16':('uint16','UInt16LE','H',1),'f32':('float32','Float32LE','f',1),'f64':('float64','Float64LE','d',1),'rgb_u8':('uint8','UInt8','B',3)}
AXES={'XZ':(0,2,1),'YZ':(1,2,0)}
ORIGIN=(11.,-7.,19.)
DIRECTION=((-1.,.25,1.5),(.5,1.,0.),(0.,.75,2.))
CASE_TABLE=(
 ('B01', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, False, False, False, 'submit', 64),
 ('B02', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, False, False, True, 'submit', 64),
 ('B03', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, False, True, False, 'submit', 64),
 ('B04', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, False, True, True, 'submit', 64),
 ('B05', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, True, False, False, 'submit', 64),
 ('B06', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, True, False, True, 'submit', 64),
 ('B07', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, True, True, False, 'submit', 64),
 ('B08', 'u8', 7, 5, 9, 1.0, 'XZ', 2, 5, 2, True, True, True, 'submit', 64),
 ('B09', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, False, False, False, 'submit', 64),
 ('B10', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, False, False, True, 'submit', 64),
 ('B11', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, False, True, False, 'submit', 64),
 ('B12', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, False, True, True, 'submit', 64),
 ('B13', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, True, False, False, 'submit', 64),
 ('B14', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, True, False, True, 'submit', 64),
 ('B15', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, True, True, False, 'submit', 64),
 ('B16', 'u16', 7, 5, 9, 1.125, 'XZ', 2, 5, 2, True, True, True, 'submit', 64),
 ('B17', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, False, False, False, 'submit', 64),
 ('B18', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, False, False, True, 'submit', 64),
 ('B19', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, False, True, False, 'submit', 64),
 ('B20', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, False, True, True, 'submit', 64),
 ('B21', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, False, False, 'submit', 64),
 ('B22', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, False, True, 'submit', 64),
 ('B23', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, False, 'submit', 64),
 ('B24', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'submit', 64),
 ('B25', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, False, False, False, 'submit', 64),
 ('B26', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, False, False, True, 'submit', 64),
 ('B27', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, False, True, False, 'submit', 64),
 ('B28', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, False, True, True, 'submit', 64),
 ('B29', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, True, False, False, 'submit', 64),
 ('B30', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, True, False, True, 'submit', 64),
 ('B31', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, True, True, False, 'submit', 64),
 ('B32', 'u8', 7, 5, 9, 1.0, 'YZ', 2, 9, 2, True, True, True, 'submit', 64),
 ('B33', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, False, False, False, 'submit', 64),
 ('B34', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, False, False, True, 'submit', 64),
 ('B35', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, False, True, False, 'submit', 64),
 ('B36', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, False, True, True, 'submit', 64),
 ('B37', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, True, False, False, 'submit', 64),
 ('B38', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, True, False, True, 'submit', 64),
 ('B39', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, True, True, False, 'submit', 64),
 ('B40', 'u16', 7, 5, 9, 1.125, 'YZ', 2, 9, 2, True, True, True, 'submit', 64),
 ('B41', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, False, False, False, 'submit', 64),
 ('B42', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, False, False, True, 'submit', 64),
 ('B43', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, False, True, False, 'submit', 64),
 ('B44', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, False, True, True, 'submit', 64),
 ('B45', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, True, False, False, 'submit', 64),
 ('B46', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, True, False, True, 'submit', 64),
 ('B47', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, True, True, False, 'submit', 64),
 ('B48', 'f32', 7, 5, 9, 1.5, 'YZ', 2, 9, 2, True, True, True, 'submit', 64),
 ('B49', 'u8', 7, 5, 9, 4.0, 'XZ', 2, 5, 2, False, False, False, 'submit', 64),
 ('B50', 'u16', 7, 5, 9, 4.0, 'XZ', 2, 5, 2, False, False, False, 'submit', 64),
 ('B51', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'cancel', 64),
 ('B52', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'active_other', 64),
 ('B53', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'stale_pixels', 64),
 ('B54', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'stale_calibration', 64),
 ('B55', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'closed_source', 64),
 ('B56', 'f32', 7, 5, 9, 1.5, 'XZ', 0, 5, 2, True, True, True, 'invalid_first', 64),
 ('B57', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 6, 2, True, True, True, 'invalid_last', 64),
 ('B58', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 0, True, True, True, 'invalid_step', 64),
 ('B59', 'f64', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'invalid_type', 64),
 ('B60', 'rgb_u8', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'invalid_samples', 64),
 ('B61', 'u8', 2, 2, 4097, 1.0, 'XZ', 2, 2, 2, True, True, True, 'render_limit', 64),
 ('B62', 'u8', 257, 33, 257, 1.0, 'XZ', 2, 33, 2, True, True, True, 'memory_limit', 1),
 ('B63', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'busy', 64),
 ('B64', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'fractional_direct', 64),
 ('B65', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'choice_changes', 64),
 ('B66', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'allowed_edits', 64),
 ('B67', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'focused_escape', 64),
 ('B68', 'f32', 7, 5, 9, 1.5, 'XZ', 2, 5, 2, True, True, True, 'navigate_source', 64),
)
CASES={r[0]:dict(zip(('profile','z','y','x','depth','plane','first','last','step','reverse','flip','transpose','scenario','memory_mb'),r[1:])) for r in CASE_TABLE}
SUCCESS_IDS=frozenset(CASE_IDS[:50]+('B52','B65','B66','B68'))
DISABLED_IDS=frozenset(('B53','B54','B55','B56','B57','B58','B59','B60','B61','B62','B63'))
CANCEL_IDS=frozenset(('B51','B67'))


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
        _integer(frame['ordinal'],positive=True)
        assert frame['member_id'] == f"frame-{frame['ordinal']:04d}.json"
        assert _read_json(_member(directory, frame["member_id"])) == frame
        assert isinstance(frame["label"], str) and frame["label"]
        rendered = frame["rendered"]
        _collection(frame["native_state"], row["profile"], held_job=_busy_frame(row,frame))
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
            if control.get('read_only') is True:
                assert control['value']=='um' and control['enabled'] is False
                assert type(control['id']) is int and control['id']>=0
                assert type(control['active']) is type(control['focused']) is bool
                rect=control['rect'];assert len(rect)==4 and rect[0]<rect[2] and rect[1]<rect[3]
            else:
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
        assert control['enabled'] is (not (row['case_id'] in DISABLED_IDS and selector=='dialog.apply'))
        point = observation["position"]
        assert point == [math.floor(value) for value in control["point"]]
        rect = control["rect"]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        assert [frame['label'] for frame in frames[ordinal:ordinal+3]] == [
            selector+'.position',selector+'.down',selector+'.up']
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
    assert closed['imgui_time'] == sessions[-1]['imgui_time']

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
    assert value["owner_id"] == value["document_id"]
    assert type(value["native_available"]) is bool
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
        "PreserveQuietNaNFloat32" if profile == "quiet_f32" else "FiniteOnly")
    assert set(dataset["calibration"]) == {
        "spacing", "origin", "direction", "unit", "time_unit", "frame_interval", "time_origin"}
    return value

def _usage(value, *, idle=False):
    assert isinstance(value, dict) and set(value) == USAGE_FIELDS
    for count in value.values():
        _integer(count)
        if idle:
            assert count == 0

def _literal_selection(value, kind, points, attributes):
    _selection(value)
    assert value["kind"] == kind and value["points"] == points
    assert value["width"] == 1 and value["attributes"] == attributes
    for field in ("mask_width", "mask_height"):
        if field in value:
            assert value[field] == 0
    if "packed_mask" in value:
        assert value["packed_mask"] == []

def _finite_json(value):
    if isinstance(value, dict):
        for item in value.values():
            _finite_json(item)
    elif isinstance(value, list):
        for item in value:
            _finite_json(item)
    elif isinstance(value, float):
        assert math.isfinite(value), 'Overflow/nonfinite observation number'

def _namespace(path, *, file=False, allow_hardlink=False):
    """Fresh lstat namespace checks; never resolve an alias before checking it."""
    assert isinstance(path, Path) and path.is_absolute()
    assert Path(os.path.abspath(path)) == path
    chain = list(reversed((path, *path.parents)))
    for component in chain:
        observed = os.lstat(component)
        assert not stat.S_ISLNK(observed.st_mode)
        assert not getattr(observed, 'st_file_attributes', 0) & 0x400, component
        assert stat.S_ISDIR(observed.st_mode) or (file and component == path and stat.S_ISREG(observed.st_mode))
    if file:
        assert observed.st_nlink >= 1 if allow_hardlink else observed.st_nlink == 1, 'Observation member has a hardlink alias'
    return observed

def _identity(value):
    return (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)

def _path_identity(value):
    return (_identity(value), value.st_ctime_ns, getattr(value, 'st_birthtime_ns', None))

def _safe_bytes(path, *, maximum=MAX_JSON_BYTES, allow_hardlink=False):
    before = _namespace(path, file=True, allow_hardlink=allow_hardlink)
    before_namespace = _path_identity(before)
    assert 0 < before.st_size <= maximum
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    descriptor = os.open(path, flags)
    try:
        opened = os.fstat(descriptor)
        assert _identity(opened) == _identity(before)
        assert opened.st_nlink >= 1 if allow_hardlink else opened.st_nlink == 1
        chunks, count = [], 0
        while True:
            chunk = os.read(descriptor, min(1024 * 1024, maximum + 1 - count))
            if not chunk:
                break
            count += len(chunk)
            assert count <= maximum
            chunks.append(chunk)
        assert count == before.st_size
        assert _identity(os.fstat(descriptor)) == _identity(before)
    finally:
        os.close(descriptor)
    assert _path_identity(_namespace(path, file=True, allow_hardlink=allow_hardlink)) == before_namespace
    return b''.join(chunks)

def _read_json(path):
    value = _decode(_safe_bytes(path))
    _finite_json(value)
    return value

def _member(root, name):
    assert isinstance(name, str) and name and '\\' not in name and ':' not in name
    relative = PurePosixPath(name)
    assert not relative.is_absolute() and relative.as_posix() == name
    assert all(part not in {'', '.', '..'} for part in relative.parts)
    path = root.joinpath(*relative.parts)
    _namespace(path, file=True)
    return path

def _inventory(directory):
    _namespace(directory)
    members, identities = [], set()
    for entry in os.scandir(directory):
        path = directory / entry.name
        observed = _namespace(path, file=True)
        identity = (observed.st_dev, observed.st_ino)
        assert identity not in identities, 'Two physical members alias the same file'
        identities.add(identity)
        raw = _safe_bytes(path)
        members.append([entry.name, len(raw), hashlib.sha256(raw).hexdigest()])
    members.sort(key=lambda item: item[0])
    assert 1 <= len(members) <= 1000
    assert sum(item[1] for item in members) <= MAX_TREE_BYTES
    return members

class _CaseFiles:
    def __init__(self, directory, row):
        self.directory, self.row = directory, row
        self.before = _inventory(directory)
        self.pins = {name: (size, digest) for name, size, digest in self.before}
        self.raw_cache = {}
        self.checked_raw = set()
        tree = row['artifact_tree']
        assert set(tree) == {'members','member_count','byte_count','sha256','excluded_members'}
        assert tree['excluded_members'] == ['case.json']
        members = [member for member in self.before if member[0] != 'case.json']
        assert tree['members'] == members and tree['member_count'] == len(members)
        assert tree['byte_count'] == sum(item[1] for item in members)
        canonical = json.dumps(members, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
        assert tree['sha256'] == hashlib.sha256(canonical).hexdigest()
        raw_members = row['raw_members']
        assert isinstance(raw_members, list) and 2 <= len(raw_members) <= 10
        assert len({member['path'] for member in raw_members}) == len(raw_members)
        self.raw_descriptors = {member['path']: member for member in raw_members}
        wanted = {frame['member_id'] for frame in row['frames']} | set(self.raw_descriptors)
        assert {member[0] for member in members} == wanted, 'Orphan or omitted actual artifact'
        for member in raw_members:
            assert self.pins[member['path']] == (member['byte_count'], member['sha256'])

    def raw(self, member, profile, shape, expected, *, storage):
        dtype, encoding, code, samples = PROFILES[profile]
        if storage:
            dtype, encoding, code = 'float64', 'Float64LE', 'd'
        assert set(member) == {'member_id','path','encoding','dtype','axes','shape','sample_count',
                               'byte_count','sha256','endian','transport'}
        assert member['path'] == member['member_id'] and '/' not in member['path']
        assert re.fullmatch(r'storage-f64-\d{4}\.bin' if storage else r'typed-\d{4}\.bin', member['path'])
        assert member == self.raw_descriptors[member['path']]
        assert (member['dtype'], member['encoding'], member['axes'], member['endian']) == (dtype,encoding,'TCZYXS','little')
        assert member['shape'] == list(shape) and shape[-1] == samples
        for extent in member['shape']:
            _integer(extent,positive=True)
        _integer(member['sample_count'],positive=True)
        _integer(member['byte_count'],positive=True)
        assert member['sample_count'] == math.prod(shape)
        assert member['byte_count'] == len(expected) == math.prod(shape) * struct.calcsize(code)
        assert member['transport'] == ('immutable_core_float64_storage_bits' if storage else 'exact_declared_pixel_type_bits')
        assert re.fullmatch('[0-9a-f]{64}', member['sha256'])
        name = member['path']
        if name not in self.raw_cache:
            raw = _safe_bytes(_member(self.directory, name))
            assert (len(raw), hashlib.sha256(raw).hexdigest()) == self.pins[name]
            self.raw_cache[name] = raw
        assert self.raw_cache[name] == expected, 'Actual samples differ from independent Slicer processor oracle'
        self.checked_raw.add(name)

    def finish(self):
        assert self.checked_raw == set(self.raw_descriptors), 'Raw descriptor escaped scientific validation'
        assert _inventory(self.directory) == self.before, 'Observation tree changed during cold assertions'

def _busy_frame(row, frame):
    if row['case_id'] != 'B63':
        return None
    first = _frame(row,'held_job_entered')['ordinal']
    last = _frame(row,'held_job_canceled')['ordinal']
    assert first < last
    return row['job_before_cleanup'] if first <= frame['ordinal'] < last else None


def _collection_activity(value, source_id, held_job):
    assert value['busy'] is (held_job is not None)
    _usage(value['scheduler_usage'], idle=held_job is None)
    if held_job is not None:
        _integer(held_job['id'],positive=True)
        assert held_job['document_id']==source_id
        assert held_job['state']=='running' and held_job['busy'] is True and held_job['cancel_requested'] is False
        usage=held_job['usage'];_usage(usage)
        assert usage['running']==1 and usage['queued']==usage['retained_bytes']==0
        assert usage['reserved_bytes']==usage['running_bytes'] and usage['running_bytes']>0
        _exact(value['scheduler_usage'],usage)


def _collection(value, profile, *, held_job=None):
    assert isinstance(value, dict) and set(value) == {
        'source','other','output','outputs','facade_ids','document_info','active_index',
        'native_member_ids','active_document_id','facade_active_document_id','busy',
        'scheduler_usage','workflow','results'}
    source = _snapshot(value['source'], profile)
    documents = [source]
    if value['other'] is not None:
        documents.append(_snapshot(value['other'], profile))
    assert isinstance(value['outputs'], list) and len(value['outputs']) <= 1
    documents.extend(_snapshot(output, profile) for output in value['outputs'])
    assert value['output'] == (value['outputs'][0] if len(value['outputs']) == 1 else None)
    ids = [doc['document_id'] for doc in documents]
    assert len(set(ids)) == len(ids), 'Source/other/output ownership alias'
    for name in ('native_member_ids','facade_ids'):
        assert isinstance(value[name], list) and len(set(value[name])) == len(value[name])
        for document_id in value[name]:
            _integer(document_id, positive=True)
    assert set(value['native_member_ids']) == set(value['facade_ids'])
    available = [doc['document_id'] for doc in documents if doc['native_available']]
    assert set(value['native_member_ids']) == set(available), 'Membership has unobserved or unavailable owner'
    _integer(value['active_document_id'], positive=True)
    assert value['active_document_id'] == value['facade_active_document_id']
    assert value['active_document_id'] in value['native_member_ids']
    _integer(value['active_index'])
    assert value['facade_ids'][value['active_index']] == value['active_document_id']
    _collection_activity(value,source['document_id'],held_job)
    assert value['workflow'] == value['results'] == []
    assert len(value['document_info']) == len(value['facade_ids'])
    by_id = {doc['document_id']: doc for doc in documents}
    for info, document_id in zip(value['document_info'], value['facade_ids']):
        doc = by_id[document_id]
        assert info['id'] == document_id and info['title'] == doc['metadata']['title']
        assert info['dtype'] == doc['dataset']['dtype']
        assert info['axis_sizes'] == doc['dataset']['axis_sizes'] and info['revisions'] == doc['revisions']
        assert info['dirty'] is doc['dirty']
        assert info['undo_steps'] == doc['undo_steps'] and info['redo_steps'] == doc['redo_steps']
    return value


def _exact(actual, expected):
    if type(expected) is float:
        assert type(actual) in (int,float) and math.isfinite(actual)
        assert struct.pack('<d',actual)==struct.pack('<d',expected)
    else:
        assert type(actual) is type(expected)
        if isinstance(expected,dict):
            assert set(actual)==set(expected)
            for key,value in expected.items():
                _exact(actual[key],value)
        elif isinstance(expected,list):
            assert len(actual)==len(expected)
            for a,e in zip(actual,expected):
                _exact(a,e)
        else:
            assert actual==expected


def _source_shape(spec):
    return (1,1,spec['z'],spec['y'],spec['x'],PROFILES[spec['profile']][3])


def _calibration(spec,changed=False):
    return {'spacing':[2. if changed else 1.,2.25,spec['depth']],
            'origin':list(ORIGIN),'direction':[v for row in DIRECTION for v in row],
            'unit':'um','time_unit':'s','frame_interval':.75,'time_origin':2.25}


def _sample(spec,x,y,z,s=0,other=False,edited=False):
    if other:
        return 187.
    if edited and (x,y,z,s)==(0,0,0,0):
        return 7.
    edge=x==spec['x']-1 or y==spec['y']-1 or z==spec['z']-1
    parity=(x+y+z)&1
    profile=spec['profile']
    if profile=='u8':
        return float(255 if edge else parity)
    if profile=='u16':
        return float(65535 if edge else 32768+parity)
    if profile=='f32':
        value=(1. if parity==0 else -1.)*(1000000+13*x+29*y+47*z+.125) if edge else (
            13*x-29*y+47*z+19*x*y-311)*128+.125
        return struct.unpack('<f',struct.pack('<f',value))[0]
    if profile=='rgb_u8':
        return float((17*x+31*y+47*z+53*s)%256)
    assert profile=='f64'
    return 10000*z+100*y+x+.25


def _typed(value,profile):
    code=PROFILES[profile][2]
    return struct.pack('<'+code,value if code in ('f','d') else int(value))


@lru_cache(maxsize=8)
def _source_buffers(profile,z,y,x,other=False,edited=False):
    spec={'profile':profile,'z':z,'y':y,'x':x}
    stored,typed=bytearray(),bytearray()
    for iz in range(z):
        for iy in range(y):
            for ix in range(x):
                for s in range(PROFILES[profile][3]):
                    value=_sample(spec,ix,iy,iz,s,other,edited)
                    stored.extend(struct.pack('<d',value));typed.extend(_typed(value,profile))
    return bytes(stored),bytes(typed)


def _axis(n,m,index):
    scale=float(m)/float(n)
    center=float(m)/2.
    if n!=m:
        center=center+scale/4.
    nominal=(float(index)-center)/scale+float(n)/2.
    sampled=nominal
    if sampled<0.:
        sampled=0.
    if sampled>=float(n)-1.:
        sampled=float(n)-1.001
    return nominal,sampled


def _grid(spec):
    a,_,f=AXES[spec['plane']]
    ratio=spec['depth']/1.
    m=math.floor(float(spec['z'])*ratio)
    first,last=spec['first']-1,spec['last']-1
    count=1+(last-first)//spec['step']
    selected=first+(count-1)*spec['step']
    branch='no_resize' if ratio==1. else 'equal_dimension_crop' if m==spec['z'] else 'bilinear_resize'
    delta=1./(float(m)/float(spec['z'])) if branch=='bilinear_resize' else 1.
    z0=_axis(spec['z'],m,0)[0] if branch=='bilinear_resize' else 0.
    sizes=(spec['x'],spec['y'],spec['z'])
    width,height=(m,sizes[a]) if spec['transpose'] else (sizes[a],m)
    return {'shape':(1,1,count,height,width,1),'axes':(2,a,f) if spec['transpose'] else (a,2,f),
            'ratio':ratio,'m':m,'delta':delta,'z0':z0,'branch':branch,'count':count,
            'last_selected':selected,'effective_first':selected if spec['reverse'] else first,
            'effective_last':first if spec['reverse'] else selected}


@lru_cache(maxsize=4)
def _output_buffers(case_id):
    spec=CASES[case_id];grid=_grid(spec);a,_,f=AXES[spec['plane']]
    sizes=(spec['x'],spec['y'],spec['z']);profile=spec['profile']
    width,height=grid['shape'][4],grid['shape'][3]
    pw,ph=(spec['z'],sizes[a]) if spec['transpose'] else (sizes[a],spec['z'])
    stored,typed=bytearray(),bytearray()
    for k in range(grid['count']):
        fixed=grid['last_selected']-k*spec['step'] if spec['reverse'] else spec['first']-1+k*spec['step']
        def sample(px,py):
            horizontal,z=(py,px) if spec['transpose'] else (px,py)
            if spec['flip']:
                z=spec['z']-1-z
            xyz=[0,0,0];xyz[a],xyz[f],xyz[2]=horizontal,fixed,z
            return _sample(spec,*xyz)
        for v in range(height):
            for u in range(width):
                if grid['branch']!='bilinear_resize':
                    value=sample(u,v)
                else:
                    x,y=_axis(pw,width,u)[1],_axis(ph,height,v)[1]
                    xb,yb=int(x),int(y);xf,yf=x-float(xb),y-float(yb)
                    upper_left,upper_right=sample(xb,yb+1),sample(xb+1,yb+1)
                    lower_left,lower_right=sample(xb,yb),sample(xb+1,yb)
                    upper=upper_left+xf*(upper_right-upper_left)
                    lower=lower_left+xf*(lower_right-lower_left)
                    value=lower+yf*(upper-lower)
                    if profile=='f32':
                        value=struct.unpack('<f',struct.pack('<f',value))[0]
                    else:
                        value=float(int(value+.5)&(255 if profile=='u8' else 65535))
                stored.extend(struct.pack('<d',value));typed.extend(_typed(value,profile))
    return bytes(stored),bytes(typed)


def _world(xyz,spec):
    spacing=(1.,2.25,spec['depth'])
    return [ORIGIN[r]+sum(DIRECTION[r][a]*spacing[a]*xyz[a] for a in range(3)) for r in range(3)]


def _output_calibration(spec):
    grid=_grid(spec);a,_,f=AXES[spec['plane']]
    xyz=[0.,0.,0.];xyz[f]=grid['effective_first']
    xyz[2]=float(spec['z']-1)-grid['z0'] if spec['flip'] else grid['z0']
    original=(1.,2.25,spec['depth'])
    spacing=[original[a],original[2]*grid['delta'],original[f]*spec['step']]
    columns=[a,2,f];signs=[1.,-1. if spec['flip'] else 1.,-1. if spec['reverse'] else 1.]
    if spec['transpose']:
        spacing[0],spacing[1]=spacing[1],spacing[0]
        columns[0],columns[1]=columns[1],columns[0]
        signs[0],signs[1]=signs[1],signs[0]
    return {'spacing':spacing,'origin':_world(xyz,spec),
            'direction':[DIRECTION[r][a]*signs[c] for r in range(3) for c,a in enumerate(columns)],
            'unit':'um','time_unit':'s','frame_interval':.75,'time_origin':2.25}


def _scalar_spacing(spec):
    a,_,f=AXES[spec['plane']];spacing=(1.,2.25,spec['depth'])
    value=[spacing[a],spacing[2]/(spacing[2]/spacing[0]),spacing[f]*1.]
    if spec['transpose']:
        value[0],value[1]=value[1],value[0]
    return value


def _provenance(spec,source_id):
    g=_grid(spec)
    return {'schema_version':1,'source_document_id':source_id,'plane':spec['plane'],
      'first':spec['first']-1,'last':spec['last']-1,'step':spec['step'],'count':g['count'],
      'last_selected':g['last_selected'],'t':0,'c':0,'reverse_order':spec['reverse'],
      'flip_vertical':spec['flip'],'transpose':spec['transpose'],'horizontal_axis':'XYZ'[g['axes'][0]],
      'vertical_axis':'XYZ'[g['axes'][1]],'fixed_axis':'XYZ'[g['axes'][2]],'index_convention':'zero_based_voxel_centers',
      'source_vertical_count':spec['z'],'vertical_count':g['m'],'z_spacing_ratio':g['ratio'],
      'processor_vertical_origin':g['z0'],'processor_vertical_step':g['delta'],'resize_branch':g['branch'],
      'scalar_spacing':_scalar_spacing(spec),'geometry_policy':'nominal_unclamped_processor_sampling_affine',
      'scalar_spacing_policy':'imagej_slicer_positive_scalar_spacing_public_stride_copy','sampling':'imagej_bilinear_source_order',
      'border_policy':'both_axes_last_center_minus_0_001','quantization_policy':'canonical_typed_bilinear_or_exact_crop'}


def _same(actual,expected,ignore=()):
    assert set(actual)==set(expected)
    for key in actual:
        if key not in ignore:
            _exact(actual[key],expected[key])


def _literal_input(spec):
    return {'source':{'profile':spec['profile'],'shape':list(_source_shape(spec)),
        'axes':'TCZYXS','calibration':_calibration(spec)},'form_1based':{
        'plane':spec['plane'],'first':spec['first'],'last':spec['last'],'step':spec['step'],
        'reverse_order':spec['reverse'],'flip_vertical':spec['flip'],'transpose':spec['transpose']},
        'scenario':spec['scenario'],'processing_memory_mb':spec['memory_mb']}


def _check_calibration(actual,expected):
    assert set(actual)==set(expected)
    for key in actual:
        if key=='origin':
            assert len(actual[key])==3
            for a,e in zip(actual[key],expected[key]):
                assert math.isclose(_number(a),e,rel_tol=2e-12,abs_tol=2e-12)
        else:
            _exact(actual[key],expected[key])


def _initial(row):
    spec=CASES[row['case_id']];ready=_collection(row['states']['source_ready'],spec['profile'])
    source=ready['source'];source_id=source['document_id'];other=ready['other']
    assert (other is not None)==(row['case_id'] in {'B52','B55'})
    assert ready['facade_ids']==ready['native_member_ids']==[source_id]+([other['document_id']] if other else [])
    assert ready['active_document_id']==source_id and ready['active_index']==0
    assert ready['output'] is None and ready['outputs']==[]
    _exact(row['source_before'],source);_exact(row['captured_source'],source)
    assert source['metadata']=={'title':'Slicer Resize Source','properties':{'source_only':'retained property','revision_probe':7},
        'extensions':{'source_extension':{'retained':True}}}
    assert source['data_identity']==1 and source['dirty'] is True
    assert source['undo_steps']==1 and source['redo_steps']==0
    assert source['history_limit']==3 and source['history_pixel_budget']==4096
    assert source['revisions']=={'pixels':0,'metadata':1,'selection':0,'display':2}
    assert source['record_layout']=={} and source['spectrum'] is None
    assert len(source['rois'])==2 and source['overlays']==[{'source_overlay':'retained'}]
    _literal_selection(source['selection'],'rectangle',[[0,0],[1,1]],{'tag':'transient'})
    _literal_selection(source['previous_selection'],'ellipse',[[0,0],[1,1]],{'tag':'previous'})
    _exact(source['dataset']['calibration'],_calibration(spec))
    assert source['dataset']['shape']==list(_source_shape(spec))
    assert source['display']['crosshair']==[min(7,spec['x']-1),min(5,spec['y']-1),min(4,spec['z']-1)]
    assert source['display']['zoom']==2.
    return ready


def _dialog_identity(dialog):
    assert dialog['title']=='Reslice with ImageJ bilinear' and dialog['is_operation'] is False
    _exact(dialog['schema'],[
        {'choices':['XZ','YZ'],'kind':'text','label':'Plane','name':'plane'},
        {'kind':'int','label':'First (1-based)','name':'first'},
        {'kind':'int','label':'Last (inclusive, 1-based)','name':'last'},
        {'kind':'int','label':'Step','name':'step'},
        {'kind':'bool','label':'Reverse selected planes','name':'reverse_order'},
        {'kind':'bool','label':'Flip source Z before transpose','name':'flip_vertical'},
        {'kind':'bool','label':'Transpose before bilinear sampling','name':'transpose'}])


def _configuration(row):
    spec=CASES[row['case_id']];opened=_frame(row,'dialog_opened')['rendered']['dialog']
    assert opened['open'] is True
    _dialog_identity(opened)
    assert opened['retained_source'] is True
    assert opened['opened_document_id']==row['captured_source']['document_id']
    assert opened['opened_document_id_semantics']=='captured_native_execution_source'
    wanted={'plane':spec['plane'],'first':spec['first'],'last':spec['last'],'step':spec['step'],
        'reverse_order':spec['reverse'],'flip_vertical':spec['flip'],'transpose':spec['transpose']}
    configured=_frame(row,'draft_configured')['rendered']['dialog']
    _exact(configured['values'],wanted)
    for field in ('first','last','step'):
        frame=_frame(row,'range_edited.'+field+'.text')
        assert frame['requested_events']==[{'type':'text','value':str(spec[field])}]
        assert frame['rendered']['input_frame']['raw_inputs']==frame['requested_events']
    selectors=[item['selector'] for item in row['control_observations']]
    assert selectors[:3]==['menu.Image','menu.Image.Stacks','menu.action.show_axis_range_slicer_resize']
    for field,value in (('reverse_order',spec['reverse']),('flip_vertical',spec['flip']),('transpose',spec['transpose'])):
        assert selectors.count('dialog.'+field)==(1 if value else 2)
    assert any(selector=='dialog.plane.choice.'+spec['plane'] for selector in selectors)
    if spec['scenario']=='choice_changes':
        for label,plane,last in (('yz_axis_reset','YZ',spec['x']),('xz_axis_reset','XZ',spec['y'])):
            values=_frame(row,label)['rendered']['dialog']['values']
            assert (values['plane'],values['first'],values['last'],values['step'])==(plane,1,last,1)
    return configured


def _preview(row,dialog):
    spec=CASES[row['case_id']];preview=dialog['slicer_resize_preview']
    assert preview['source_unit']=='um'
    assert preview['geometry_policy']=='nominal_unclamped_processor_sampling_affine'
    assert preview['scalar_spacing_policy']=='imagej_slicer_positive_scalar_spacing_public_stride_copy'
    assert preview['canonicality']=='checked_on_selected_samples_at_execution'
    assert type(preview['source_available']) is type(preview['source_current']) is type(preview['busy']) is type(preview['can_apply']) is bool
    if row['case_id'] in {'B53','B54','B55'}:
        assert preview['source_current'] is False and preview['can_apply'] is False
        assert bool(preview['source_available'])==(row['case_id']!='B55')
        assert preview['error']
        return
    assert preview['source_available'] is preview['source_current'] is True
    assert preview['busy'] is (row['case_id']=='B63')
    if row['case_id'] in {'B56','B57','B58','B59','B60','B62'}:
        assert preview['can_apply'] is False and preview['error']
        return
    g=_grid(spec)
    expected={'width':g['shape'][4],'height':g['shape'][3],'samples':1,'emitted_plane_count':g['count'],
      'source_vertical_count':spec['z'],'vertical_count':g['m'],'z_spacing_ratio':g['ratio'],
      'processor_vertical_origin':g['z0'],'processor_vertical_step':g['delta'],'resize_branch':g['branch'],
      'requested_last_1based':spec['last'],'last_selected_1based':g['last_selected']+1,
      'effective_first_selected_1based':g['effective_first']+1,'effective_last_selected_1based':g['effective_last']+1,
      'output_axis_order':['XYZ'[axis] for axis in g['axes']],'nominal_spacing':_output_calibration(spec)['spacing'],
      'scalar_spacing':_scalar_spacing(spec),'max_working_bytes':spec['memory_mb']*1024*1024,
      'render_limit':4096,'render_limit_source':'headless_frontend_4096'}
    for key,value in expected.items():
        _exact(preview[key],value)
    abi=os.environ.get('IMAGEJXX_AXIS_RANGE_SLICER_RESIZE_ABI_SIZES','')
    assert re.fullmatch(r'[1-9][0-9]{0,3},[1-9][0-9]{0,3}',abi),'Pin measured Image/shared_ptr sizes'
    image,pointer=map(int,abi.split(','));a,_,_=AXES[spec['plane']]
    horizontal=(spec['x'],spec['y'],spec['z'])[a]
    expected_bytes=8*(math.prod(g['shape'])+horizontal*spec['z']+spec['x']*spec['y']+horizontal)+g['count']*(2*image+pointer+256)+2*image+256
    assert type(preview['working_bytes']) is int and preview['working_bytes']==expected_bytes<=preview['max_working_bytes']
    assert preview['can_apply'] is (row['case_id'] not in DISABLED_IDS)
    if row['case_id'] in {'B61','B63'}:
        assert preview['error']


def _check_output(snapshot,row,files):
    spec=CASES[row['case_id']];g=_grid(spec);source_id=row['source_before']['document_id']
    assert snapshot['document_id']!=source_id and snapshot['owner_id']==snapshot['document_id']
    assert snapshot['native_available'] is True and snapshot['dirty'] is False
    assert snapshot['data_identity']==0 and snapshot['undo_steps']==snapshot['redo_steps']==0
    assert snapshot['history_limit']==5 and snapshot['history_pixel_budget']==8*1024*1024
    assert snapshot['revisions']=={'pixels':0,'metadata':0,'selection':0,'display':1}
    assert snapshot['selection'] is snapshot['previous_selection'] is snapshot['spectrum'] is None
    assert snapshot['rois']==snapshot['overlays']==[] and snapshot['record_layout']=={}
    flags=f"R{int(spec['reverse'])} F{int(spec['flip'])} T{int(spec['transpose'])}"
    title=f"Slicer Resize Source {spec['plane']} {spec['first']}-{spec['last']} step {spec['step']} ImageJ bilinear [{flags}]"
    assert snapshot['metadata']['title']==title and snapshot['metadata']['properties']=={}
    _exact(snapshot['metadata']['extensions'],{'axis_range_slicer_resize':_provenance(spec,source_id)})
    expected_cal=_output_calibration(spec)
    basis=[expected_cal['direction'][r*3]*expected_cal['spacing'][0] for r in range(3)]
    assert math.isclose(snapshot['display']['zoom'],1.25/math.hypot(*basis),rel_tol=2e-12,abs_tol=2e-12)
    assert snapshot['dataset']['shape']==list(g['shape'])
    _check_calibration(snapshot['dataset']['calibration'],_output_calibration(spec))
    if 'world_points' in snapshot:
        points=snapshot['world_points']
        _,_,nz,ny,nx,_=g['shape'];voxels={(0.,0.,0.),(1.,0.,0.),(0.,1.,0.),(0.,0.,1.)}
        voxels.update((float(x),float(y),float(z)) for x in (0,nx-1) for y in (0,ny-1) for z in (0,nz-1))
        assert [point['voxel_xyz'] for point in points]==[list(v) for v in sorted(voxels)]
        assert snapshot['world_points_scope']=='observed_affine_origin_unit_basis_and_corners'
        for point in points:
            assert len(point['voxel_xyz'])==len(point['world_xyz'])==3
            u,v,k=point['voxel_xyz'];a,_,f=AXES[spec['plane']]
            horizontal,j=(v,u) if spec['transpose'] else (u,v)
            xyz=[0.,0.,0.];xyz[a]=horizontal
            xyz[f]=g['effective_first']+(-1 if spec['reverse'] else 1)*spec['step']*k
            z=g['z0']+j*g['delta'];xyz[2]=float(spec['z']-1)-z if spec['flip'] else z
            for actual,expected in zip(point['world_xyz'],_world(xyz,spec)):
                assert math.isclose(_number(actual),expected,rel_tol=2e-12,abs_tol=2e-12)
    if 'raw_member' in snapshot:
        stored,typed=_output_buffers(row['case_id'])
        files.raw(snapshot['stored_raw_member'],spec['profile'],g['shape'],stored,storage=True)
        files.raw(snapshot['raw_member'],spec['profile'],g['shape'],typed,storage=False)


def assert_axis_range_slicer_resize_observation(row,directory):
    case_id=row['case_id'];assert case_id in CASES
    spec=CASES[case_id]
    assert row['schema_version']==1 and row['contract_id']==CONTRACT and row['context'] in CONTEXTS
    assert row['profile']==spec['profile'] and row['observation_state']=='observed' and row['acceptance_claim'] is False
    assert row['limits']=={'max_frames':160,'max_seconds':15,'max_file_bytes':MAX_JSON_BYTES,
                          'max_tree_bytes':MAX_TREE_BYTES,'max_world_observations_per_document':12}
    assert 0<=_number(row['elapsed_seconds'])<=15
    _exact(row['literal_input'],_literal_input(spec))
    files=_CaseFiles(directory,row);assert _read_json(directory/'case.json')==row
    ready=_initial(row);_physical_observations(row,directory);_configuration(row)
    assert row['source_before_raw']==row['source_before']['raw_member']
    assert row['source_before_stored_raw']==row['source_before']['stored_raw_member']
    assert row['source_after_raw']==row['source_after']['raw_member']
    assert row['source_after_stored_raw']==row['source_after']['stored_raw_member']
    final=_collection(row['states']['final_idle'],spec['profile'])
    assert row['final_busy'] is False and row['final_context']['current_context_id']==0
    _usage(final['scheduler_usage'],idle=True)
    assert len(row['outputs'])==(1 if case_id in SUCCESS_IDS else 0)
    assert final['outputs']==row['outputs']
    if case_id in SUCCESS_IDS:
        output=row['outputs'][0];_check_output(output,row,files)
        assert final['facade_ids']==ready['facade_ids']+[output['document_id']]
        assert final['active_document_id']==final['facade_active_document_id']==output['document_id']
        assert row['window_error']==''
        submissions=row['submission_observations'];assert len(submissions)==1
        assert submissions[0]['before']['outputs']==[] and submissions[0]['actual_output_ids']==[output['document_id']]
        assert submissions[0]['dialog_after']['open'] is False
    else:
        assert final['outputs']==[] and final['output'] is None
        assert final['facade_ids']==(ready['facade_ids'][1:] if case_id=='B55' else ready['facade_ids'])
    if case_id in DISABLED_IDS:
        before=_frame(row,'before_submit')['rendered']['dialog'];_preview(row,before)
        submission=row['submission_observations'];assert len(submission)==1
        item=submission[0];_exact(item['before'],item['after'])
        assert item['actual_output_ids']==[] and item['dialog_after']['open'] is True
    elif case_id not in {'B51','B64','B67'}:
        _preview(row,_frame(row,'before_submit')['rendered']['dialog'])
    stimuli=row['core_stimuli']
    selectors=[item['selector'] for item in row['control_observations']]
    assert selectors.count('dialog.apply')==int(case_id in SUCCESS_IDS|DISABLED_IDS)
    if case_id=='B67':
        for down,label in ((True,'focused_escape.down'),(False,'focused_escape.up')):
            assert _frame(row,label)['requested_events']==[{'type':'key','name':'Escape','down':down}]
        assert _frame(row,'before_focused_escape')['rendered']['dialog']['controls']['first']['focused'] is True
    for stimulus in stimuli:
        assert stimulus['classification']=='declared_core_state_stimulus_no_physical_credit'
        if stimulus['label']=='direct_rejection':
            assert case_id in DISABLED_IDS|{'B64'}
            expected='revision_conflict' if case_id in {'B53','B54'} else 'document_unavailable' if case_id=='B55' else 'out_of_range' if case_id=='B57' else 'invalid_argument'
            assert stimulus['exception']['type']==expected and stimulus['exception']['message']
            args={'document_id':row['captured_source']['document_id'],'plane':spec['plane'],
                  'first':.5 if case_id=='B64' else spec['first']-1,'last':spec['last']-1,'step':spec['step'],
                  'reverse_order':spec['reverse'],'flip_vertical':spec['flip'],'transpose':spec['transpose']}
            _exact(stimulus['arguments'],args)
            assert stimulus['method']==('resliceSlicerAxisRange.captured_snapshot' if case_id in {'B53','B54','B55'} else 'action.reslice_slicer_axis_range')
            _exact(stimulus['before'],stimulus['after'])
        else:
            assert stimulus['exception'] is None
    assert len([s for s in stimuli if s['label']=='direct_rejection'])==int(case_id in DISABLED_IDS|{'B64'})
    if case_id=='B63':
        before,after=row['job_before_cleanup'],row['job_after_cleanup']
        assert before['id']==after['id'] and before['state']=='running' and before['busy'] is True
        assert before['usage']['running']==1 and before['cancel_requested'] is False
        assert after['state']=='canceled' and after['busy'] is False and after['cancel_requested'] is True
        _usage(after['usage'],idle=True)
    expected_source=dict(ready['source'])
    captured_after=row['captured_source_after']
    _same(captured_after,row['captured_source'],{'native_available','undo_steps','redo_steps','history_limit','history_pixel_budget'})
    assert captured_after['native_available'] is (case_id!='B55')
    assert captured_after['undo_steps']==row['source_after']['undo_steps']
    if case_id not in {'B53','B54','B55','B66','B68'}:
        _exact(row['source_after'],expected_source)
    else:
        after=row['source_after'];changed={'native_available'} if case_id=='B55' else {'data_identity','dirty','undo_steps','revisions','stored_raw_member','raw_member'} if case_id=='B53' else {'data_identity','dirty','undo_steps','revisions','dataset'} if case_id=='B54' else {'revisions','selection','previous_selection','display'} if case_id=='B66' else {'revisions','display'}
        _same(after,expected_source,changed)
        if case_id=='B53':
            assert after['revisions']=={'pixels':1,'metadata':1,'selection':0,'display':2}
            assert after['data_identity']==2 and after['undo_steps']==2
        if case_id=='B54':
            assert after['revisions']=={'pixels':0,'metadata':2,'selection':0,'display':2}
            assert after['data_identity']==2 and after['undo_steps']==2
            _exact(after['dataset']['calibration'],_calibration(spec,changed=True))
        if case_id=='B55':
            assert after['native_available'] is False
        if case_id=='B66':
            assert after['revisions']=={'pixels':0,'metadata':1,'selection':1,'display':3}
            _exact(after['previous_selection'],expected_source['selection'])
            _literal_selection(after['selection'],'point',[[0,1]],{})
            _same(after['display'],expected_source['display'],{'zoom','revision'})
            assert after['display']['zoom']==3. and after['display']['revision']==3
        if case_id=='B68':
            assert after['revisions']=={'pixels':0,'metadata':1,'selection':0,'display':3}
            assert after['display']['crosshair']==[0,0,0]
    source_buffers=_source_buffers(spec['profile'],spec['z'],spec['y'],spec['x'])
    edited_buffers=_source_buffers(spec['profile'],spec['z'],spec['y'],spec['x'],edited=True) if case_id=='B53' else source_buffers
    other_buffers=_source_buffers(spec['profile'],spec['z'],spec['y'],spec['x'],other=True) if ready['other'] else None
    def check_collection(value,held_job=None):
        _collection(value,spec['profile'],held_job=held_job)
        for role in ('source','other'):
            snapshot=value[role]
            if snapshot is None or 'raw_member' not in snapshot:
                continue
            buffers=other_buffers if role=='other' else edited_buffers if snapshot['revisions']['pixels']==1 else source_buffers
            files.raw(snapshot['stored_raw_member'],spec['profile'],_source_shape(spec),buffers[0],storage=True)
            files.raw(snapshot['raw_member'],spec['profile'],_source_shape(spec),buffers[1],storage=False)
        for output in value['outputs']:
            _check_output(output,row,files)
    for label,state in row['states'].items():
        held_job=row['job_before_cleanup'] if case_id=='B63' and label in {
            'held_job_entered','before_submit','after_submit','after_direct_rejection'} else None
        check_collection(state,held_job)
    for frame in row['frames']:
        check_collection(frame['native_state'],_busy_frame(row,frame))
    for stimulus in stimuli:
        held_job=row['job_before_cleanup'] if case_id=='B63' and stimulus['label']=='direct_rejection' else None
        check_collection(stimulus['before'],held_job);check_collection(stimulus['after'],held_job)
    for item in row['submission_observations']:
        held_job=row['job_before_cleanup'] if case_id=='B63' else None
        check_collection(item['before'],held_job);check_collection(item['after'],held_job)
    files.raw(captured_after['stored_raw_member'],spec['profile'],_source_shape(spec),source_buffers[0],storage=True)
    files.raw(captured_after['raw_member'],spec['profile'],_source_shape(spec),source_buffers[1],storage=False)
    files.finish()
    return {'case_id':case_id,'context':row['context'],'physical_frames':len(row['frames']),
            'actual_control_clicks':len(row['control_observations']),'output_owners':[o['document_id'] for o in row['outputs']],
            'source_stored_sha256':hashlib.sha256(source_buffers[0]).hexdigest(),
            'output_stored_sha256':hashlib.sha256(_output_buffers(case_id)[0]).hexdigest() if case_id in SUCCESS_IDS else None}


def _whole_tree(root, *, cpp):
    _namespace(root)
    names = {entry.name for entry in os.scandir(root)}
    assert names == set(CASE_IDS) | ({'axis-range-slicer-resize-ui.json'} if cpp else set())
    total, identities = 0, set()
    for case_id in CASE_IDS:
        for member in _inventory(root/case_id):
            path = root/case_id/member[0]
            st = _namespace(path,file=True)
            identity = (st.st_dev,st.st_ino)
            assert identity not in identities, 'Launch members have cross-case file aliases'
            identities.add(identity)
            total += member[1]
    if cpp:
        total += len(_safe_bytes(root/'axis-range-slicer-resize-ui.json'))
    assert total <= MAX_TREE_BYTES
    return total

class _IndexedCaseRows:
    def __init__(self, root, entries, context):
        _namespace(root)
        assert [entry['case_id'] for entry in entries] == list(CASE_IDS)
        self.root, self.context = root, context
        self.entries = {}
        for entry in entries:
            assert set(entry) == {'case_id','context','path','bytes','sha256'}
            assert entry['context'] == context and entry['path'] == entry['case_id']+'/case.json'
            assert type(entry['bytes']) is int and 0 < entry['bytes'] <= MAX_JSON_BYTES
            assert re.fullmatch('[0-9a-f]{64}',entry['sha256'])
            self.entries[entry['case_id']] = entry
        assert len(self.entries) == 68

    def __getitem__(self, case_id):
        entry = self.entries[case_id]
        raw = _safe_bytes(_member(self.root,entry['path']))
        assert len(raw) == entry['bytes'] and hashlib.sha256(raw).hexdigest() == entry['sha256']
        row = _decode(raw)
        _finite_json(row)
        assert row['case_id'] == case_id and row['context'] == self.context
        return row

def _cpp_report_rows(report, root):
    assert report['schema_version'] == 1 and report['contract_id'] == CONTRACT
    assert type(report['schema_version']) is int
    assert report['context'] == 'imgui_cpp' and report['observation_state'] == 'observed'
    assert report['acceptance_claim'] is False and report['case_count'] == 68
    assert report['case_format'] == 'indexed_case_files'
    assert report['limits'] == {'max_seconds':300,'max_case_frames':160,'max_case_seconds':15,
                               'max_file_bytes':MAX_JSON_BYTES,'max_tree_bytes':MAX_TREE_BYTES}
    assert 0 <= _number(report['elapsed_seconds']) <= 300
    _whole_tree(root,cpp=True)
    return _IndexedCaseRows(root,report['cases'],'imgui_cpp')

@pytest.fixture(scope='module')
def installed_cpp_executable():
    configured = os.environ.get('IMAGEJXX_CPP_EXE')
    if not configured:
        if _required():
            pytest.fail('Required installed desktop missing: IMAGEJXX_CPP_EXE')
        pytest.skip('Set IMAGEJXX_CPP_EXE to the qualified installed desktop')
    executable = Path(configured)
    _namespace(executable,file=True)
    assert executable.name.lower() in {'imagejxx.exe','imagejxx'} and executable.parent.name.lower() == 'bin'
    expected = os.environ.get('IMAGEJXX_CPP_EXE_SHA256')
    assert expected and re.fullmatch('[0-9a-f]{64}',expected), 'Pin qualified standalone binary'
    assert hashlib.sha256(_safe_bytes(executable)).hexdigest() == expected
    return executable

@pytest.fixture(scope='module')
def cpp_ui_rows(installed_cpp_executable, tmp_path_factory):
    root = tmp_path_factory.mktemp('axis-range-slicer-resize-cpp')/'run'
    started = time.monotonic()
    completed = subprocess.run([str(installed_cpp_executable),'--axis-range-slicer-resize-ui-consumer-test',
        '--consumer-dir',str(root)],capture_output=True,timeout=300,check=False)
    assert time.monotonic()-started <= 300
    assert len(completed.stdout)+len(completed.stderr) <= 16*1024*1024
    assert completed.returncode == 0, completed.stdout.decode('utf-8','replace')+completed.stderr.decode('utf-8','replace')
    report = _read_json(root/'axis-range-slicer-resize-ui.json')
    yield root,_cpp_report_rows(report,root)
    _whole_tree(root,cpp=True)
    assert hashlib.sha256(_safe_bytes(installed_cpp_executable)).hexdigest() == os.environ['IMAGEJXX_CPP_EXE_SHA256']

@pytest.fixture(scope='module')
def python_ui_rows(tmp_path_factory):
    desktop = importlib.import_module('imagejxx._desktop') if _required() else pytest.importorskip(
        'imagejxx._desktop',exc_type=ModuleNotFoundError)
    module = Path(desktop.__file__)
    assert module.suffix.lower() in {'.pyd','.so'}
    origins = {Path(os.path.abspath(sysconfig.get_path(key)))/'imagejxx' for key in ('platlib','purelib')}
    assert any(module.is_relative_to(origin) for origin in origins), 'Imported checkout desktop'
    expected = os.environ.get('IMAGEJXX_DESKTOP_MODULE_SHA256')
    assert expected and re.fullmatch('[0-9a-f]{64}',expected), 'Pin installed compiled desktop module'
    # Qualified wheel installers may retain a hardlink to their immutable cache.
    # This exception is confined to the checked installed compiled origin.
    _namespace(module,file=True,allow_hardlink=True)
    assert hashlib.sha256(_safe_bytes(module,allow_hardlink=True)).hexdigest() == expected
    uiapp=importlib.import_module('imagejxx.ui.app')
    Preferences=importlib.import_module('imagejxx.ui.persistence').Preferences
    app_module=Path(uiapp.__file__)
    assert any(app_module.is_relative_to(origin) for origin in origins)
    app_pin=os.environ.get('IMAGEJXX_UI_APP_MODULE_SHA256','')
    assert re.fullmatch('[0-9a-f]{64}',app_pin), 'Pin installed public Editor/LiveDocument source'
    assert hashlib.sha256(_safe_bytes(app_module,allow_hardlink=True)).hexdigest()==app_pin
    root = tmp_path_factory.mktemp('axis-range-slicer-resize-python')
    entries, started = [], time.monotonic()
    total = 0
    for case_id in CASE_IDS:
        editor = uiapp.Editor(preferences=Preferences(root/f'{case_id}-preferences.json'))
        case_started = time.monotonic()
        returned = _decode(editor._editor._axis_range_slicer_resize_ui_case(case_id=case_id,context='imgui_python',directory=str(root/case_id)))
        assert time.monotonic()-case_started <= 15 and time.monotonic()-started <= 300
        assert [info['id'] for info in editor.document_infos]==returned['states']['final_idle']['facade_ids']
        for info in editor.document_infos:
            handle=editor.live_document(info['id'])
            assert isinstance(handle,uiapp.LiveDocument) and handle.id==info['id']
            assert handle.native_owner.id==handle.native_snapshot().id==handle.id
            assert handle.info['axis_sizes']==info['axis_sizes'] and handle.info['title']==info['title']
        if case_id in SUCCESS_IDS:
            output=editor.live_document(returned['outputs'][0]['document_id'])
            expected_typed=_output_buffers(case_id)[1]
            plane_bytes=math.prod(_grid(CASES[case_id])['shape'][3:])*struct.calcsize(PROFILES[CASES[case_id]['profile']][2])
            assert output.plane('xy',0).tobytes(order='C')==expected_typed[:plane_bytes]
        del editor
        raw = _safe_bytes(root/case_id/'case.json')
        assert _decode(raw) == returned
        entries.append({'case_id':case_id,'context':'imgui_python','path':case_id+'/case.json',
                        'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()})
        total += sum(member[1] for member in _inventory(root/case_id))
        assert total <= MAX_TREE_BYTES
    assert time.monotonic()-started <= 300
    _whole_tree(root,cpp=False)
    yield root,_IndexedCaseRows(root,entries,'imgui_python')
    _whole_tree(root,cpp=False)
    assert hashlib.sha256(_safe_bytes(module,allow_hardlink=True)).hexdigest() == expected

@pytest.mark.parametrize('case_id,context',[(case,context) for case in CASE_IDS for context in CONTEXTS],
                         ids=[f'{case}-{context}' for case in CASE_IDS for context in CONTEXTS])
def test_installed_axis_range_slicer_resize_ui(case_id,context,request):
    root,rows=request.getfixturevalue('cpp_ui_rows' if context=='imgui_cpp' else 'python_ui_rows')
    row=rows[case_id]
    assert row['case_id']==case_id and row['context']==context
    assert_axis_range_slicer_resize_observation(row,root/case_id)


@pytest.mark.parametrize('dtype,expected',(('uint8',1.),('uint16',32769.),('float32',.5)),
                         ids=('u8','u16','f32'))
def test_installed_live_document_slicer_resize(dtype,expected,tmp_path,python_ui_rows):
    """Public direct wrapper scope; this test grants no pointer/UI credit."""
    import numpy as np
    import imagejxx as ij
    from imagejxx.ui.app import Editor,LiveDocument
    from imagejxx.ui.persistence import Preferences
    # The origin-qualified physical fixture above already qualifies this launch.
    assert python_ui_rows[0].is_dir()
    low=32768 if dtype=='uint16' else 0
    pixels=np.array([[[low,low],[low,low]],[[low+2,low+2],[low+2,low+2]]],dtype=dtype).reshape(1,1,2,2,2,1)
    dataset=ij.Dataset(pixels,axes='TCZYXS',calibration=ij.SpatialCalibration(spacing=(1.,2.25,4.),unit='um'))
    source,other=ij.Document(dataset,'Live Source'),ij.Document(dataset,'Other')
    editor=Editor(preferences=Preferences(tmp_path/'preferences.json'))
    handle=editor.add_native_document(source);editor.add_native_document(other)
    before=source.snapshot();other_before=other.snapshot();ids=[item['id'] for item in editor.document_infos]
    assert editor.live_document().id==other.id
    output=handle.reslice_slicer_range('xz',0,1)
    assert isinstance(output,LiveDocument) and output.id not in ids
    assert output.native_owner.id==output.native_snapshot().id==output.id
    assert [item['id'] for item in editor.document_infos]==ids+[output.id]
    assert editor.live_document().id==output.id
    observed=output.plane('xy',0)
    assert observed.shape==(8,2) and observed.dtype==np.dtype(dtype)
    assert float(observed[2,0])==expected
    assert source.snapshot().shares_data(before) and other.snapshot().shares_data(other_before)
    assert source.snapshot().data_identity==before.data_identity
    assert source.snapshot().dirty==before.dirty
    assert output.native_owner.undo_steps==output.native_owner.redo_steps==0
    assert output.native_snapshot().dirty is False
