"""Independent installed readers for the 21 shared ImGui axis-export cases.

This is a prospective source packet. Pixel expectations use literal formulas
and pinned reference bytes; the producer supplies observations only. Full
affine, captured-owner, fresh-output and state rules are independently owned.
"""

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


CONTRACT = "p05f.axis-plane-export-ui.001"
CASE_IDS = tuple(f"E{index:02d}" for index in range(1, 22))
CONTEXTS = ("imgui_python", "imgui_cpp")
FIXTURE_SHA = "05cb09b95b2dcfd2e1e492150d3883497c0bb5a203c8dde3b5b5b1830f3b5d08"
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_INDEX_BYTES = 64 * 1024
AXES = {"XY": (0, 1, 2), "XZ": (0, 2, 1), "YZ": (1, 2, 0)}
EXTENTS = (32, 17, 9)
CAPTURED_XYZ = (7, 5, 4)
SPACING = (0.4, 1.2, 3.0)
ORIGIN = (11.0, -7.0, 19.0)
DIRECTION = ((1.0, 0.25, -0.5), (0.5, -1.0, 0.125), (0.0, 0.75, 1.5))
PROFILES = {
    "u8": ("uint8", "UInt8", "B", 1),
    "u16": ("uint16", "UInt16LE", "H", 1),
    "f32": ("float32", "Float32LE", "f", 1),
    "f64": ("float64", "Float64LE", "d", 1),
    "rgb": ("uint8", "UInt8", "B", 3),
    "rgba": ("uint8", "UInt8", "B", 4),
    "f32_nan": ("float32", "Float32LE", "f", 1),
}
CASES = {
    "E01": ("u8", "XY", 1, "u8.xy.lower"),
    "E02": ("u16", "XY", 5, "u16.xy.interior"),
    "E03": ("f32", "XY", 9, "f32.xy.upper"),
    "E04": ("u8", "XZ", 1, "u8.xz.lower"),
    "E05": ("u16", "XZ", 6, "u16.xz.interior"),
    "E06": ("f32", "XZ", 17, "f32.xz.upper"),
    "E07": ("u8", "YZ", 1, "u8.yz.lower"),
    "E08": ("u16", "YZ", 8, "u16.yz.interior"),
    "E09": ("f32", "YZ", 32, "f32.yz.upper"),
    "E10": ("f64", "XY", 5, None),
    "E11": ("rgb", "YZ", 8, "rgb.yz.interior"),
    "E12": ("u8", "XZ", 7, None),
    "E13": ("u8", "XY", 1, "u8.xy.lower"),
    "E14": ("u16", "XZ", 18, None),
    "E15": ("f32", "YZ", -1, None),
    "E16": ("u8", "XY", 5, None),
    "E17": ("u8", "XZ", 6, None),
    "E18": ("u16", "YZ", 8, None),
    "E19": ("f32", "XY", 5, None),
    "E20": ("rgba", "XZ", 6, None),
    "E21": ("f32_nan", "XY", 1, None),
}
SUCCESS_IDS = set(CASE_IDS[:11]) | {"E13", "E20", "E21"}
REFERENCE_IDS = (
    "u8.xy.lower", "u8.xz.lower", "u8.yz.lower",
    "u16.xy.interior", "u16.xz.interior", "u16.yz.interior",
    "f32.xy.upper", "f32.xz.upper", "f32.yz.upper",
    "rgb.xy.interior", "rgb.xz.interior", "rgb.yz.interior",
)
KEYS = {
    "Escape", "Delete", "Left", "Right", "Up", "Down", "PageUp", "PageDown",
    "Home", "End", "Space", "Shift", "Ctrl", "Alt", "Super", "A", "W",
    "Enter", "Tab", "Backspace",
}
USAGE_FIELDS = {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}


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


def _sample_bytes(profile, x, y, z, sample):
    if profile == "f32_nan" and (x, y, z) == (3, 2, 0):
        return struct.pack("<I", 0x7FC12345)
    if profile == "u8":
        value = (37 * x + 61 * y + 13 * x * y + 43 * z) % 251
    elif profile == "u16":
        value = 6000 * z + 32 * y + x
    elif profile in {"f32", "f64", "f32_nan"}:
        value = 10000 * z + 100 * y + x + 0.25
    else:
        assert profile in {"rgb", "rgba"}
        value = (
            (17 * x + 31 * y + 47 * z) % 256,
            (71 * x + 11 * y + 29 * z) % 256,
            (13 * x + 61 * y + 23 * z) % 256,
            (19 * x + 53 * y + 7 * z + 127) % 256,
        )[sample]
    return struct.pack("<" + PROFILES[profile][2], value)


@lru_cache(maxsize=32)
def _source_bytes(profile, *, edited=False, other=False):
    chunks = []
    for z in range(9):
        for y in range(17):
            for x in range(32):
                for sample in range(PROFILES[profile][3]):
                    if other:
                        chunk = struct.pack("<" + PROFILES[profile][2], 187)
                    elif edited and (x, y, z, sample) == (0, 0, 0, 0):
                        assert profile == "u8"
                        chunk = b"\x07"
                    else:
                        chunk = _sample_bytes(profile, x, y, z, sample)
                    chunks.append(chunk)
    return b"".join(chunks)


@lru_cache(maxsize=32)
def _plane_bytes(profile, plane, coordinate):
    axes = AXES[plane]
    chunks = []
    for v in range(EXTENTS[axes[1]]):
        for u in range(EXTENTS[axes[0]]):
            xyz = [0, 0, 0]
            xyz[axes[0]], xyz[axes[1]], xyz[axes[2]] = u, v, coordinate
            chunks.extend(_sample_bytes(profile, *xyz, sample)
                          for sample in range(PROFILES[profile][3]))
    return b"".join(chunks)


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


@pytest.fixture(scope="module")
def axis_oracles():
    paths = [parent / "tests/fixtures/axis-plane-imagej154r.json"
             for parent in tuple(Path(__file__).resolve().parents)[:8]]
    paths = [path for path in paths if path.is_file()]
    assert len(paths) == 1
    raw = paths[0].read_bytes()
    assert len(raw) == 1_307_528 and hashlib.sha256(raw).hexdigest() == FIXTURE_SHA
    fixture = _decode(raw)
    assert fixture["schema_version"] == 1 and fixture["imagej_version"] == "1.54r"
    assert fixture["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert tuple(fixture["exact_ordered_case_ids"]) == REFERENCE_IDS
    assert tuple(row["case_id"] for row in fixture["cases"]) == REFERENCE_IDS
    for row in fixture["cases"]:
        profile = row["dtype"]
        source = _source_bytes(profile)
        assert bytes.fromhex(row["source_before"]["raw_hex"]) == source
        assert row["source_after"]["raw_hex"] == row["source_before"]["raw_hex"]
        assert bytes.fromhex(row["output"]["raw_hex"]) == _plane_bytes(
            profile, row["plane"].upper(), row["coordinate_zero_based"])
    return {row["case_id"]: row for row in fixture["cases"]}


def _world(xyz, spacing=SPACING):
    return tuple(ORIGIN[row] + sum(DIRECTION[row][axis] * spacing[axis] * xyz[axis]
                                  for axis in range(3)) for row in range(3))


def _calibration(value, plane=None, coordinate=0, *, changed_spacing=False):
    spacing = (0.8, 1.2, 3.0) if changed_spacing else SPACING
    axes = AXES[plane] if plane else (0, 1, 2)
    xyz = [0, 0, 0]
    if plane:
        xyz[axes[2]] = coordinate
    expected = {
        "spacing": [spacing[a] for a in axes],
        "origin": list(_world(xyz, spacing)),
        "direction": [DIRECTION[row][a] for row in range(3) for a in axes],
        "unit": "um", "time_unit": "s", "frame_interval": 0.75, "time_origin": 2.25,
    }
    assert set(value) == set(expected)
    for key, e in expected.items():
        if isinstance(e, str):
            assert value[key] == e
        else:
            _close(value[key], e)
    if plane:
        for v in range(EXTENTS[axes[1]]):
            for u in range(EXTENTS[axes[0]]):
                xyz[axes[0]], xyz[axes[1]] = u, v
                actual = [value["origin"][row] + value["direction"][row * 3] *
                          value["spacing"][0] * u + value["direction"][row * 3 + 1] *
                          value["spacing"][1] * v for row in range(3)]
                _close(actual, _world(xyz, spacing))
    return expected


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


def _source_raw(state, directory, profile, *, edited=False, other=False):
    snapshot = state["other" if other else "source"]
    if snapshot is None:
        assert other
        return None
    shape = (1, 1, 9, 17, 32, PROFILES[profile][3])
    assert snapshot["dataset"]["shape"] == list(shape)
    return _raw(snapshot["raw_member"], directory, profile, shape,
                _source_bytes(profile, edited=edited, other=other))


def _output(snapshot, profile, plane, index, source_id, directory, oracles, reference_id):
    _snapshot(snapshot, profile)
    axes = AXES[plane]
    shape = (1, 1, 1, EXTENTS[axes[1]], EXTENTS[axes[0]], PROFILES[profile][3])
    assert snapshot["dataset"]["shape"] == list(shape)
    expected_raw = _plane_bytes(profile, plane, index - 1)
    _raw(snapshot["raw_member"], directory, profile, shape, expected_raw)
    if reference_id:
        assert bytes.fromhex(oracles[reference_id]["output"]["raw_hex"]) == expected_raw
    _calibration(snapshot["dataset"]["calibration"], plane, index - 1)
    points = snapshot["world_points"]
    assert isinstance(points, list) and len(points) == shape[3] * shape[4]
    for observed, (v, u) in zip(points, ((v, u) for v in range(shape[3]) for u in range(shape[4]))):
        assert set(observed) == {"voxel_xyz", "world_xyz"}
        assert observed["voxel_xyz"] == [u, v, 0]
        xyz = [0, 0, 0]
        xyz[axes[0]], xyz[axes[1]], xyz[axes[2]] = u, v, index - 1
        _close(observed["world_xyz"], _world(xyz))
    assert snapshot["selection"] is snapshot["previous_selection"] is snapshot["spectrum"] is None
    assert snapshot["rois"] == snapshot["overlays"] == []
    assert snapshot["record_layout"] == {}
    assert snapshot["metadata"] == {
        "title": f"Export Source {plane} {index}", "properties": {},
        "extensions": {"axis_plane_export": {
            "schema_version": 1, "source_document_id": source_id,
            "plane": plane, "coordinate": index - 1, "t": 0, "c": 0,
            "horizontal_axis": "XYZ"[axes[0]], "vertical_axis": "XYZ"[axes[1]],
            "fixed_axis": "XYZ"[axes[2]], "index_convention": "zero_based_voxel_centers",
        }},
    }
    provenance = snapshot["metadata"]["extensions"]["axis_plane_export"]
    for field in ("schema_version", "source_document_id", "coordinate", "t", "c"):
        _integer(provenance[field])
    assert snapshot["dirty"] is False
    assert snapshot["undo_steps"] == snapshot["redo_steps"] == snapshot["data_identity"] == 0
    assert snapshot["history_limit"] == 5 and snapshot["history_pixel_budget"] == 8 * 1024 * 1024
    assert snapshot["revisions"] == {"pixels": 0, "metadata": 0, "selection": 0, "display": 1}
    display = snapshot["display"]
    assert display["t"] == display["c"] == 0
    assert display["crosshair"] == [shape[4] // 2, shape[3] // 2, 0]
    assert display["orthogonal"] is False and display["linked_zoom"] is True
    assert display["plane_zoom"] == [0, 0, 0] and display["pan"] == [[0, 0], [0, 0], [0, 0]]
    assert display["properties"] == {} and display["revision"] == 1
    _close(display["zoom"], 1.25 / (SPACING[axes[0]] * math.sqrt(
        sum(DIRECTION[row][axes[0]] ** 2 for row in range(3)))))


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


@pytest.fixture(scope="module")
def cpp_ui_rows(installed_cpp_executable, tmp_path_factory):
    root = tmp_path_factory.mktemp("axis-plane-export-cpp") / "run"
    assert not root.exists()
    started = time.monotonic()
    completed = subprocess.run(
        [str(installed_cpp_executable), "--axis-plane-export-ui-consumer-test",
         "--consumer-dir", str(root)],
        capture_output=True, timeout=180, check=False,
    )
    assert time.monotonic() - started <= 180
    assert len(completed.stdout) + len(completed.stderr) <= 16 * 1024 * 1024
    assert completed.returncode == 0, (
        completed.stdout.decode("utf-8", "replace") + completed.stderr.decode("utf-8", "replace"))
    report = _read_json(root / "axis-plane-export-ui.json")
    if report["schema_version"] == 2:
        assert (root / "axis-plane-export-ui.json").stat().st_size <= MAX_INDEX_BYTES
    return root, _cpp_report_rows(report, root)


def _nonalias(path):
    assert path.is_absolute(), "Indexed observations require absolute origin paths"
    for component in (path, *path.parents):
        assert not component.is_symlink(), component
        assert not getattr(component, "is_junction", lambda: False)(), component
    assert path.resolve(strict=True) == path, "Indexed origin must be canonical and unaliased"
    return path


class _IndexedCaseRows:
    """Retain only descriptors; load and validate one full case at a time."""

    def __init__(self, root, entries):
        self.root = _nonalias(root)
        assert isinstance(entries, list) and len(entries) == len(CASE_IDS)
        assert [entry["case_id"] for entry in entries] == list(CASE_IDS)
        assert len({entry["case_id"] for entry in entries}) == len(CASE_IDS)
        assert len({entry["path"] for entry in entries}) == len(CASE_IDS)
        self.entries = {}
        for entry in entries:
            assert set(entry) == {"case_id", "context", "path", "bytes", "sha256"}
            assert entry["context"] == "imgui_cpp"
            assert entry["path"] == entry["case_id"] + "/case.json"
            assert type(entry["bytes"]) is int and 0 < entry["bytes"] <= MAX_JSON_BYTES
            assert re.fullmatch(r"[0-9a-f]{64}", entry["sha256"])
            path = _nonalias(root / entry["path"])
            assert _member(root, entry["path"]) == path
            assert path.stat().st_size == entry["bytes"]
            self.entries[entry["case_id"]] = entry

    def __getitem__(self, case_id):
        entry = self.entries[case_id]
        path = _nonalias(self.root / entry["path"])
        assert _member(self.root, entry["path"]) == path
        raw = path.read_bytes()
        assert 0 < len(raw) == entry["bytes"] <= MAX_JSON_BYTES
        assert hashlib.sha256(raw).hexdigest() == entry["sha256"]
        row = _decode(raw)
        assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
        assert row["case_id"] == case_id and row["context"] == "imgui_cpp"
        assert row["observation_state"] == "observed" and row["acceptance_claim"] is False
        return row


def _cpp_report_rows(report, root):
    assert type(report["schema_version"]) is int and report["schema_version"] in {1, 2}
    assert report["contract_id"] == CONTRACT
    assert report["context"] == "imgui_cpp"
    assert report["observation_state"] == "observed" and report["acceptance_claim"] is False
    assert report["limits"] == {
        "max_seconds": 180, "max_case_frames": 160, "max_case_seconds": 15,
    }
    assert 0 <= _number(report["elapsed_seconds"]) <= 180
    assert type(report["case_count"]) is int and report["case_count"] == 21
    assert isinstance(report["cases"], list) and len(report["cases"]) == len(CASE_IDS)
    assert [row["case_id"] for row in report["cases"]] == list(CASE_IDS)
    if report["schema_version"] == 2:
        assert set(report) == {"schema_version", "contract_id", "context", "observation_state",
                               "acceptance_claim", "case_count", "case_format", "limits",
                               "cases", "elapsed_seconds"}
        assert report["case_format"] == "indexed_case_files"
        assert len(json.dumps(report, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) <= MAX_INDEX_BYTES
        return _IndexedCaseRows(root, report["cases"])
    # Preserve bounded original inline parents. The unchanged _read_json limit
    # still rejects the retained oversized parent; it is never silently relaxed.
    assert "case_format" not in report
    assert all(row["schema_version"] == 1 and row["contract_id"] == CONTRACT and
               row["context"] == "imgui_cpp" for row in report["cases"])
    return {row["case_id"]: row for row in report["cases"]}


@pytest.fixture(scope="module")
def python_ui_rows(tmp_path_factory):
    if _required():
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    module = Path(desktop.__file__).resolve(strict=True)
    assert module.suffix.lower() in {".pyd", ".so"}
    roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx"
             for key in ("platlib", "purelib")}
    assert any(module.is_relative_to(root) for root in roots), "UI gate imported a checkout module"
    expected = os.environ.get("IMAGEJXX_DESKTOP_MODULE_SHA256")
    if expected:
        assert re.fullmatch(r"[0-9a-f]{64}", expected)
        assert hashlib.sha256(module.read_bytes()).hexdigest() == expected
    root = tmp_path_factory.mktemp("axis-plane-export-python")
    rows = {}
    started = time.monotonic()
    for case_id in CASE_IDS:
        directory = root / case_id
        assert not directory.exists()
        editor = desktop.Editor(preferences_path=str(root / f"{case_id}-preferences.json"))
        case_started = time.monotonic()
        row = _decode(editor._axis_plane_export_ui_case(
            case_id=case_id, context="imgui_python", directory=str(directory)))
        assert time.monotonic() - case_started <= 15
        assert time.monotonic() - started <= 180
        assert row["case_id"] == case_id and row["context"] == "imgui_python"
        rows[case_id] = row
        del editor
    assert tuple(rows) == CASE_IDS
    return root, rows


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


def _dialog(row, label, plane, index, source_id, *, open=True):
    rendered = _frame(row, label)["rendered"]
    dialog = rendered["dialog"]
    assert dialog["open"] is open and dialog["title"] == "Export plane"
    assert dialog["is_operation"] is False
    assert dialog["opened_document_id"] == source_id
    assert dialog["opened_document_id_semantics"] == "captured_native_execution_source"
    _integer(dialog["generation"], positive=True)
    assert dialog["values"]["plane"] == plane
    assert dialog["text"]["index"] == str(index)
    assert dialog["axis_index_label"] == f"{'XYZ'[AXES[plane][2]]} index: 1 to {EXTENTS[AXES[plane][2]]}"
    schema = {field["name"]: field for field in dialog["schema"]}
    assert set(schema) == {"plane", "index"}
    assert schema["plane"]["label"] == "Plane" and schema["plane"]["choices"] == ["XY", "XZ", "YZ"]
    assert schema["index"]["label"] == "Index (1-based)" and schema["index"]["kind"] == "int"
    return dialog


def _typed_index_inputs(row):
    text_frames = [frame for frame in row["frames"]
                   if any(event["type"] == "text" for event in frame["requested_events"])]
    if not text_frames:
        assert row["case_id"] in {"E11", "E16", "E17", "E18", "E19", "E20"}
    for frame in text_frames:
        ordinal = frame["ordinal"]
        assert ordinal >= 3
        down, up = row["frames"][ordinal - 3:ordinal - 1]
        assert down["requested_events"] == [{"type": "keys", "keys": [
            {"name": "Ctrl", "down": True}, {"name": "A", "down": True}]}]
        assert up["requested_events"] == [{"type": "keys", "keys": [
            {"name": "Ctrl", "down": False}, {"name": "A", "down": False}]}]
        rendered = frame["rendered"]
        control = rendered["dialog"]["controls"]["index"]
        assert control["active"] is True
        actual_text = rendered["input_frame"]["input_text"]
        assert actual_text["id"] == control["id"] == rendered["input_frame"]["focus"]["active_id"]
        texts = [event["value"] for event in frame["requested_events"] if event["type"] == "text"]
        assert len(texts) == 1
        assert rendered["dialog"]["text"]["index"] == texts[0]
        assert actual_text["length"] == len(texts[0].encode("utf-8"))
        assert actual_text["cursor"] == actual_text["length"]
        assert actual_text["selection_start"] == actual_text["selection_end"]
        selected = down["rendered"]["input_frame"]["input_text"]
        assert selected["id"] == control["id"]
        assert abs(selected["selection_end"] - selected["selection_start"]) == selected["length"]
        assert selected["length"] > 0
        clicks = [observation for observation in row["control_observations"]
                  if observation["selector"] == "dialog.index"
                  and observation["before_frame_ordinal"] + 3 < down["ordinal"]]
        assert clicks, "Typed draft had no preceding actual index-control pointer focus"


def _publication(row, directory, oracles):
    case_id = row["case_id"]
    observations = row["publication_observations"]
    if case_id not in SUCCESS_IDS:
        assert observations == []
        assert all(state["output"] is None for state in row["states"].values())
        return
    assert [observation["phase"] for observation in observations] == [
        "before_native_publish", "after_attach"]
    before, attached = observations
    profile, plane, index, reference_id = CASES[case_id]
    source_ready = row["states"]["source_ready"]
    before_submit, after_submit = row["states"]["before_submit"], row["states"]["after_submit"]
    assert before_submit["native_member_ids"] == source_ready["native_member_ids"]
    assert before_submit["facade_ids"] == source_ready["facade_ids"]
    source_id = source_ready["source"]["document_id"]
    for observation in observations:
        assert set(observation) == {"phase", "captured_source", "request", "candidate", "state"}
        _snapshot(observation["captured_source"], profile)
        _same(source_ready["source"], observation["captured_source"])
        request = observation["request"]
        assert set(request) == {"plane", "coordinate", "t", "c", "max_working_bytes"}
        assert {key: request[key] for key in ("plane", "coordinate", "t", "c")} == {
            "plane": plane, "coordinate": index - 1, "t": 0, "c": 0,
        }
        _integer(request["max_working_bytes"], positive=True)
        _collection(observation["state"], profile)
        _output(observation["candidate"], profile, plane, index, source_id,
                directory, oracles, reference_id)
    candidate_id = before["candidate"]["document_id"]
    assert candidate_id not in before["state"]["native_member_ids"]
    assert candidate_id not in before["state"]["facade_ids"]
    assert before["state"]["output"] is None
    _same(before_submit, before["state"])
    _same(after_submit, attached["state"])
    _same(before["candidate"], attached["candidate"])
    assert attached["candidate"]["document_id"] == attached["state"]["output"]["document_id"] == candidate_id
    _same(attached["candidate"], attached["state"]["output"])
    assert attached["state"]["native_member_ids"] == before["state"]["native_member_ids"] + [candidate_id]
    assert attached["state"]["facade_ids"] == before["state"]["facade_ids"] + [candidate_id]
    assert attached["state"]["active_document_id"] == candidate_id
    for field in ("source", "other", "scheduler_usage", "workflow", "results", "busy"):
        _same(before["state"][field], attached["state"][field])
    output = after_submit["output"]
    _output(output, profile, plane, index, source_id, directory, oracles, reference_id)
    _raw(row["output_raw"], directory, profile, tuple(output["dataset"]["shape"]),
         _plane_bytes(profile, plane, index - 1))


def _rejection(row, directory, label, *, exception=None):
    matches = [rejected for rejected in row["rejections"] if rejected["label"] == label]
    assert len(matches) == 1, f"Missing or duplicate observed rejection: {label}"
    rejected = matches[0]
    profile = row["profile"]
    _collection(rejected["before"], profile)
    _collection(rejected["after"], profile)
    _same(rejected["before"], rejected["after"])
    assert rejected["before"]["output"] is None
    source_id = row["states"]["source_ready"]["source"]["document_id"]
    assert rejected["captured_document_id"] == source_id
    assert rejected["captured_revisions"] == row["states"]["source_ready"]["source"]["revisions"]
    assert rejected["before"]["active_document_id"] == rejected["before"]["other"]["document_id"]
    assert set(rejected["request"]) == {"plane", "coordinate", "t", "c"}
    assert rejected["request"]["t"] == rejected["request"]["c"] == 0
    if exception:
        assert rejected["exception"]["type"] == exception
        assert isinstance(rejected["exception"]["message"], str) and rejected["exception"]["message"]
    else:
        assert isinstance(rejected["error_message"], str) and rejected["error_message"]
        rendered = _frame(row, "after_rejection")["rendered"]
        assert rejected["error_message"] == rendered["window_error"]
        assert rendered["error_modal"]["open"] is True
        assert rendered["error_modal"]["message"] == rejected["error_message"]
        assert rejected["dialog_after"] == rendered["dialog"]
        assert any(item["selector"] == "error_modal.ok" for item in row["control_observations"])
        acknowledged = _frame(row, "error_acknowledged")["rendered"]
        assert acknowledged["error_modal"]["open"] is False and acknowledged["window_error"] == ""
    for state in (rejected["before"], rejected["after"]):
        _source_raw(state, directory, profile, edited=row["case_id"] == "E17")
        _source_raw(state, directory, profile, other=True)
    return rejected


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
    assert (other is not None) == (row["case_id"] in set(CASE_IDS[10:19]))
    ids = [source["document_id"]] + ([other["document_id"]] if other else [])
    assert ready["native_member_ids"] == ready["facade_ids"] == ids
    assert ready["active_document_id"] == source["document_id"]
    assert source["metadata"] == {
        "title": "Export Source", "properties": {"source_only": "retained property", "revision_probe": 7},
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
         _source_bytes(profile, edited=row["case_id"] == "E17"))
    if row["case_id"] != "E10":
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


def _checkpoint_raw(row, directory):
    profile = row["profile"]
    edited_paths = set()
    if row["case_id"] == "E17":
        edited_paths.update(row["states"][label]["source"]["raw_member"]["path"]
                            for label in ("after_pixel_edit", "before_rejection", "after_rejection",
                                          "error_acknowledged", "after_cancel", "final_idle"))
    for label, state in row["states"].items():
        _collection(state, profile)
        if label != "source_ready":
            _same(state, _frame(row, label)["native_state"])
        _source_raw(state, directory, profile,
                    edited=state["source"]["raw_member"]["path"] in edited_paths)
        _source_raw(state, directory, profile, other=True)
    for frame in row["frames"]:
        state = frame["native_state"]
        assert "raw_member" not in state["source"]
        if state["other"] is not None:
            assert "raw_member" not in state["other"]


def _case_preservation(row):
    states = row["states"]
    case_id = row["case_id"]
    initial = states["source_ready"]
    final = states["final_idle"]
    _same(initial["other"], final["other"])
    if case_id not in {"E10", "E17", "E18", "E20"}:
        _same(initial["source"], final["source"])
    if case_id in SUCCESS_IDS:
        _same(states["before_submit"]["source"], states["after_submit"]["source"])
        _same(states["after_submit"], final)
    else:
        assert final["output"] is None
        assert "output_raw" not in row or row["output_raw"] is None
        expected_ids = initial["native_member_ids"]
        if case_id == "E19":
            expected_ids = [initial["other"]["document_id"]]
        assert final["native_member_ids"] == final["facade_ids"] == expected_ids
        assert final["active_document_id"] == initial["other"]["document_id"]
    assert final["workflow"] == initial["workflow"]
    assert final["results"] == initial["results"]


def _case_dialogs(row):
    case_id = row["case_id"]
    source_id = row["states"]["source_ready"]["source"]["document_id"]
    opened = _dialog(row, "dialog_opened", "XY", 5, source_id)
    generation = opened["generation"]
    for frame in row["frames"]:
        dialog = frame["rendered"]["dialog"]
        if dialog["title"] == "Export plane":
            assert dialog["generation"] == generation
            assert dialog["opened_document_id"] == source_id
    selectors = [item["selector"] for item in row["control_observations"]]
    assert selectors[:3] == ["menu.Image", "menu.Image.Stacks", "menu.action.show_axis_plane_export"]
    assert selectors.count("menu.action.show_axis_plane_export") == 1
    if case_id not in {"E11", "E16", "E17", "E18", "E19", "E20"}:
        assert "dialog.index" in selectors
    if case_id in SUCCESS_IDS:
        profile, plane, index, _ = CASES[case_id]
        _dialog(row, "before_submit", plane, index, source_id)
        assert _frame(row, "after_submit")["rendered"]["dialog"]["open"] is False
        assert selectors.count("dialog.apply") == (2 if case_id == "E13" else 1)
    elif case_id != "E16":
        assert "dialog.cancel" in selectors or case_id == "E19"
    if case_id not in {"E10", "E11", "E13", "E14", "E15", "E16", "E17", "E18", "E19", "E20"}:
        _, plane, index, _ = CASES[case_id]
        _dialog(row, "index_edited", plane, index, source_id)
    if case_id in {"E04", "E05", "E06", "E07", "E08", "E09", "E11", "E12",
                   "E14", "E15", "E17", "E18", "E20"}:
        plane = CASES[case_id][1]
        assert f"dialog.plane.choice.{plane}" in selectors
        _dialog(row, "plane_selected", plane, CAPTURED_XYZ[AXES[plane][2]] + 1, source_id)
    for plane in ("XY", "XZ", "YZ"):
        for item in row["control_observations"]:
            if item["selector"] == f"dialog.plane.choice.{plane}":
                assert item["control"]["enabled"] is True
    _typed_index_inputs(row)


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


def _scenario(row, directory):
    case_id = row["case_id"]
    states = row["states"]
    source_id = states["source_ready"]["source"]["document_id"]
    other = states["source_ready"]["other"]
    other_id = other["document_id"] if other else None
    stimuli = _public_stimuli(row)
    expected_labels = {
        "E10": ["navigation_1", "navigation_2"],
        "E11": ["tab_switch"], "E12": ["tab_switch"], "E13": ["tab_switch"],
        "E14": ["tab_switch"], "E15": ["tab_switch"],
        "E16": ["held_job_start", "tab_switch", "busy_action", "held_job_cancel",
                "held_job_release", "held_job_wait", "held_job_drain"],
        "E17": ["pixel_edit", "tab_switch"], "E18": ["calibration_edit", "tab_switch"],
        "E19": ["source_close"], "E20": ["selection_edit", "display_edit"],
    }.get(case_id, [])
    assert list(stimuli) == expected_labels, "Undeclared or missing public state stimulus"
    if "tab_switch" in stimuli:
        _tab_switch(stimuli["tab_switch"], other_id)
        assert not any(item["selector"].startswith("document.")
                       for item in row["control_observations"]), "Modal API tab switch received pointer credit"
        label = "after_tab_switch" if case_id in {"E11", "E12"} else (
            "before_disabled_submit" if case_id == "E16" else "before_rejection")
        assert states[label]["active_document_id"] == other_id
    if case_id == "E10":
        for entry, label, xyz in ((stimuli["navigation_1"], "after_navigation_1", [3, 2, 1]),
                                   (stimuli["navigation_2"], "after_navigation_2", [1, 1, 8])):
            _stimulus(entry, "setNativeDisplay", {"document_id": source_id, "crosshair": xyz})
            _display_change(entry, xyz)
            _same(entry["after"], states[label])
        for label, plane, index in (
                ("xy_index_edited", "XY", 2), ("after_navigation_1", "XY", 2),
                ("xz_axis_reset", "XZ", 6), ("xz_index_edited", "XZ", 10),
                ("after_navigation_2", "XZ", 10), ("yz_axis_reset", "YZ", 8),
                ("yz_index_edited", "YZ", 12), ("xy_axis_reset", "XY", 5)):
            _dialog(row, label, plane, index, source_id)
        assert [item["selector"] for item in row["control_observations"]
                if ".choice." in item["selector"]] == [
                    "dialog.plane.choice.XZ", "dialog.plane.choice.YZ", "dialog.plane.choice.XY"]
        _same(stimuli["navigation_1"]["before"]["source"], states["source_ready"]["source"])
        _same(stimuli["navigation_1"]["after"]["source"], stimuli["navigation_2"]["before"]["source"])
        _same(stimuli["navigation_2"]["after"]["source"], states["final_idle"]["source"])
    elif case_id == "E11":
        _same(states["before_tab_switch"], stimuli["tab_switch"]["before"])
        _same(states["after_tab_switch"], stimuli["tab_switch"]["after"])
        assert states["before_submit"]["active_document_id"] == other_id
    elif case_id == "E12":
        _dialog(row, "index_edited", "XZ", 7, source_id)
        _same(states["before_cancel"], states["after_cancel"])
        assert _frame(row, "after_cancel")["rendered"]["dialog"]["open"] is False
        assert not any(item["selector"] == "dialog.apply" for item in row["control_observations"])
    elif case_id in {"E13", "E14", "E15", "E17", "E18", "E19"}:
        assert len(row["rejections"]) == 1
        rejected = _rejection(row, directory, row["rejections"][0]["label"])
        _same(states["before_rejection"], rejected["before"])
        _same(states["after_rejection"], rejected["after"])
        if case_id in {"E13", "E14", "E15"}:
            plane = CASES[case_id][1]
            invalid = {"E13": 0, "E14": 18, "E15": -1}[case_id]
            assert rejected["request"] == {"plane": plane, "coordinate": invalid - 1, "t": 0, "c": 0}
            for label in ("invalid_index_edited", "after_rejection", "draft_restored"):
                _dialog(row, label, plane, invalid, source_id)
            if case_id == "E13":
                _dialog(row, "corrected_index", "XY", 1, source_id)
                assert states["after_rejection"]["output"] is None
            else:
                _same(states["before_rejection"], states["after_cancel"])
        elif case_id == "E17":
            edit = stimuli["pixel_edit"]
            _stimulus(edit, "commitEdit.replace_plane", {
                "document_id": source_id, "t": 0, "c": 0, "z": 0, "x": 0, "y": 0, "s": 0, "value": 7})
            _only_source_changed(edit)
            before, after = edit["before"]["source"], edit["after"]["source"]
            _mutation(before, after, {"data_identity", "undo_steps", "redo_steps", "dirty"}, "pixels")
            assert after["data_identity"] == before["data_identity"] + 1 == 2
            assert after["dirty"] is True and after["redo_steps"] == 0
            # One replaced plane retains 544 doubles (4352 bytes). The literal
            # 4096-byte source history budget prunes every entry using it.
            assert after["undo_steps"] == 0
            _same(edit["after"], states["after_pixel_edit"])
            _same(after, states["final_idle"]["source"])
            assert rejected["before"]["source"]["revisions"]["pixels"] == rejected["captured_revisions"]["pixels"] + 1
            assert rejected["request"] == {"plane": "XZ", "coordinate": 5, "t": 0, "c": 0}
        elif case_id == "E18":
            edit = stimuli["calibration_edit"]
            _stimulus(edit, "commitEdit.set_calibration", {"document_id": source_id, "spacing": [0.8, 1.2, 3.0]})
            _only_source_changed(edit)
            before, after = edit["before"]["source"], edit["after"]["source"]
            _mutation(before, after, {"dataset", "data_identity", "undo_steps", "dirty"}, "metadata")
            assert after["data_identity"] == before["data_identity"] + 1 == 2
            assert after["undo_steps"] == before["undo_steps"] + 1 == 2 and after["dirty"] is True
            for field in before["dataset"]:
                if field != "calibration":
                    assert before["dataset"][field] == after["dataset"][field]
            _calibration(after["dataset"]["calibration"], changed_spacing=True)
            _same(edit["after"], states["after_calibration_edit"])
            _same(after, states["final_idle"]["source"])
            assert rejected["before"]["source"]["revisions"]["metadata"] == rejected["captured_revisions"]["metadata"] + 1
            assert rejected["request"] == {"plane": "YZ", "coordinate": 7, "t": 0, "c": 0}
        else:
            close = stimuli["source_close"]
            _stimulus(close, "closeDocument", {"document_id": source_id, "discard": True})
            before, after = close["before"], close["after"]
            _same(states["before_source_close"], before)
            _same(states["after_source_close"], after)
            assert after["native_member_ids"] == after["facade_ids"] == [other_id]
            assert after["active_document_id"] == other_id
            for field in ("source", "other", "output", "busy", "scheduler_usage", "workflow", "results"):
                _same(before[field], after[field])
            assert rejected["request"] == {"plane": "XY", "coordinate": 4, "t": 0, "c": 0}
            assert source_id not in rejected["before"]["native_member_ids"]
    elif case_id == "E16":
        _busy_scenario(row, stimuli, source_id, other_id)
    elif case_id == "E20":
        selection = stimuli["selection_edit"]
        literal = {"kind": "ellipse", "points": [[8, 4], [13, 8]], "width": 1, "attributes": {}}
        _stimulus(selection, "commitEdit.select", {"document_id": source_id, "selection": literal})
        _only_source_changed(selection)
        before, after = selection["before"]["source"], selection["after"]["source"]
        _mutation(before, after, {"selection", "previous_selection"}, "selection")
        _literal_selection(after["selection"], "ellipse", [[8, 4], [13, 8]], {})
        _same(after["previous_selection"], before["selection"])
        _same(selection["after"], states["after_selection_edit"])
        display = stimuli["display_edit"]
        properties = {"display_range": [0, 255], "lut": "Ice"}
        _stimulus(display, "setNativeDisplay", {
            "document_id": source_id, "crosshair": [3, 2, 1], "zoom": 3, "properties": properties})
        _display_change(display, [3, 2, 1], zoom=3, properties=properties)
        _same(display["before"]["source"], after)
        _same(display["after"], states["after_display_edit"])
        _same(display["after"]["source"], states["final_idle"]["source"])
        _dialog(row, "before_submit", "XZ", 6, source_id)
    if case_id not in {"E13", "E14", "E15", "E16", "E17", "E18", "E19"}:
        assert row["rejections"] == []


def _busy_scenario(row, stimuli, source_id, other_id):
    start = stimuli["held_job_start"]
    _stimulus(start, "startBusyPreparation", {
        "operation": "add", "value": 0, "asynchronous": True, "document_id": source_id})
    assert start["before"]["busy"] is False and start["after"]["busy"] is True
    _usage(start["before"]["scheduler_usage"], idle=True)
    assert start["after"]["scheduler_usage"]["running"] > 0
    for field in ("source", "other", "output", "native_member_ids", "facade_ids",
                  "active_document_id", "workflow", "results"):
        _same(start["before"][field], start["after"][field])
    assert len(row["rejections"]) == 1
    rejected = row["rejections"][0]
    assert rejected["exception"]["type"] == "invalid_argument"
    assert isinstance(rejected["exception"]["message"], str) and rejected["exception"]["message"]
    _same(rejected["before"], rejected["after"])
    assert rejected["captured_document_id"] == source_id
    assert rejected["request"] == {"plane": "XY", "coordinate": 4, "t": 0, "c": 0}
    action = stimuli["busy_action"]
    assert action["method"] == "action.export_axis_plane"
    assert action["arguments"] == {"document_id": source_id, "plane": "XY", "coordinate": 4}
    assert action["exception"] == rejected["exception"]
    _same(action["before"], rejected["before"])
    _same(action["after"], rejected["after"])
    assert action["before"]["active_document_id"] == other_id
    for label in ("before_disabled_submit", "after_disabled_submit"):
        state = row["states"][label]
        assert state["busy"] is True and state["active_document_id"] == other_id
        control = _frame(row, label)["rendered"]["dialog"]["controls"]["apply"]
        assert control["enabled"] is False
    _same(row["states"]["before_disabled_submit"], row["states"]["after_disabled_submit"])
    disabled_clicks = [item for item in row["control_observations"]
                       if item["selector"] == "dialog.apply"]
    assert len(disabled_clicks) == 1 and disabled_clicks[0]["control"]["enabled"] is False
    held, terminal = row["job_before_cleanup"], row["job_after_cleanup"]
    for job in (held, terminal):
        assert set(job) == {"busy", "id", "document_id", "state", "cancel_requested", "usage", "worker_entered"}
        _integer(job["id"], positive=True)
        assert job["document_id"] == source_id and job["worker_entered"] is True
        assert type(job["busy"]) is type(job["cancel_requested"]) is bool
        _usage(job["usage"])
    assert held["busy"] is True and held["state"] == "running" and held["cancel_requested"] is False
    assert held["usage"]["running"] > 0 and held["usage"]["running_bytes"] > 0
    assert terminal["id"] == held["id"] and terminal["state"] == "canceled"
    assert terminal["busy"] is False and terminal["cancel_requested"] is True
    _usage(terminal["usage"], idle=True)
    job_id = held["id"]
    _stimulus(stimuli["held_job_cancel"], "cancelJob", {"job_id": job_id})
    _stimulus(stimuli["held_job_release"], "diagnosticBarrier.release", {"job_id": job_id})
    _stimulus(stimuli["held_job_wait"], "Job.wait_for", {"job_id": job_id, "timeout_ms": 2000})
    _stimulus(stimuli["held_job_drain"], "wait", {})
    for name in ("held_job_cancel", "held_job_release", "held_job_wait", "held_job_drain"):
        entry = stimuli[name]
        for field in ("source", "other", "output", "native_member_ids", "facade_ids",
                      "active_document_id", "workflow", "results"):
            _same(entry["before"][field], entry["after"][field])
    _usage(stimuli["held_job_drain"]["after"]["scheduler_usage"], idle=True)
    assert stimuli["held_job_drain"]["after"]["busy"] is False
    assert row["states"]["held_job_canceled"]["busy"] is False
    assert any(item["selector"] == "dialog.cancel" and item["control"]["enabled"] is True
               for item in row["control_observations"])


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


def _all_raw_observations(row, directory, oracles):
    profile = row["profile"]
    collections = [(state, label in {
        "after_pixel_edit", "before_rejection", "after_rejection", "error_acknowledged",
        "after_cancel", "final_idle"}) for label, state in row["states"].items()]
    for entry in row["public_stimuli"]:
        before_edited = row["case_id"] == "E17" and entry["label"] == "tab_switch"
        after_edited = row["case_id"] == "E17"
        collections.extend(((entry["before"], before_edited), (entry["after"], after_edited)))
    for rejected in row["rejections"]:
        collections.extend((state, row["case_id"] == "E17")
                           for state in (rejected["before"], rejected["after"]))
    for observation in row["publication_observations"]:
        collections.append((observation["state"], False))
    for state, edited in collections:
        _source_raw(state, directory, profile, edited=edited and row["case_id"] == "E17")
        _source_raw(state, directory, profile, other=True)
        if row["case_id"] == "E10":
            _spectrum(state["source"]["spectrum"], directory)
        if state["output"]:
            _, plane, index, reference_id = CASES[row["case_id"]]
            _output(state["output"], profile, plane, index, state["source"]["document_id"],
                    directory, oracles, reference_id)
    for observation in row["publication_observations"]:
        captured = observation["captured_source"]
        _raw(captured["raw_member"], directory, profile,
             (1, 1, 9, 17, 32, PROFILES[profile][3]), _source_bytes(profile))
        if row["case_id"] == "E10":
            _spectrum(captured["spectrum"], directory)
    referenced = {}

    def visit(value):
        if isinstance(value, dict):
            if {"member_id", "path", "dtype", "encoding", "sha256", "byte_count"} <= value.keys():
                if value["path"] in referenced:
                    assert referenced[value["path"]] == value
                referenced[value["path"]] = value
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit({key: value for key, value in row.items() if key not in {"raw_members", "artifact_tree"}})
    assert {member["path"]: member for member in row["raw_members"]} == referenced


@pytest.mark.parametrize(
    "case_id,context",
    [(case_id, context) for case_id in CASE_IDS for context in CONTEXTS],
    ids=[f"{case_id}-{context}" for case_id in CASE_IDS for context in CONTEXTS],
)
def test_installed_axis_plane_export_ui(case_id, context, axis_oracles, request):
    parent, rows = request.getfixturevalue("cpp_ui_rows" if context == "imgui_cpp" else "python_ui_rows")
    directory, row = parent / case_id, rows[case_id]
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["profile"] == CASES[case_id][0]
    assert row["limits"] == {"max_frames": 160, "max_seconds": 15}
    assert 0 <= _number(row["elapsed_seconds"]) <= 15
    assert _read_json(directory / "case.json") == row
    _initial(row, directory)
    _physical_observations(row, directory)
    _case_dialogs(row)
    _scenario(row, directory)
    _publication(row, directory, axis_oracles)
    _checkpoint_raw(row, directory)
    _all_raw_observations(row, directory, axis_oracles)
    _case_preservation(row)
    _final(row)
    _artifact_tree(row, directory)
