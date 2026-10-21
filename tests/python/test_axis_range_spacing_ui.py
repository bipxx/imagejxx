"""Installed physical-spacing ImGui observations; independent literal byte/grid oracle.

Source-only prospective 20 x 2 gate. No target processing at collection time.
Every numerical expectation below is derived from the fixed source/request
constants, never a producer status, preview, reported match or output sample.
The headless frontend cap is 4096; this does not qualify a real GL platform.
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

CONTRACT = 'p05i.axis-range-spacing-ui.001'
CASE_IDS = tuple(f'S{i:02d}' for i in range(1, 21))
CONTEXTS = ('imgui_cpp', 'imgui_python')
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_TREE_BYTES = 2 * 1024 * 1024 * 1024
AXES = {'XZ': (0, 2, 1), 'YZ': (1, 2, 0)}
SPACING, ORIGIN = (.5, 1.25, 2.), (11., -7., 19.)
PROFILES = {
    'u8': ('uint8', 'UInt8', 'B', 1), 'u16': ('uint16', 'UInt16LE', 'H', 1),
    'rgb_u8': ('uint8', 'UInt8', 'B', 3), 'rgba_f32': ('float32', 'Float32LE', 'f', 4),
    'quiet_f32': ('float32', 'Float32LE', 'f', 1),
}
# profile, Z,Y,X, calibrationB, plane, first1,last1,step, h UTF8,
# sampling, reverse,flip,transpose, scenario, processing_memory_mb.
CASE_TABLE = (
 ('S01','u16',9,17,32,False,'XZ',2,16,3,'2','Nearest',False,False,False,'submit',64),
 ('S02','u16',9,17,32,True,'YZ',2,31,3,'7.5','Linear',True,True,True,'submit',64),
 ('S03','rgb_u8',9,17,32,False,'XZ',2,16,3,'2','Linear',False,False,False,'submit',64),
 ('S04','rgba_f32',9,17,32,False,'YZ',2,31,3,'2','Nearest',False,False,False,'submit',64),
 ('S05','quiet_f32',9,17,32,False,'XZ',3,3,1,'2','Nearest',False,False,False,'submit',64),
 ('S06','u16',9,17,32,False,'XZ',2,16,3,'4','Nearest',True,False,False,'submit',64),
 ('S07','rgba_f32',9,17,32,True,'YZ',2,31,3,'7','Linear',False,False,False,'choice_changes',64),
 ('S08','u16',9,17,32,False,'XZ',2,16,3,'2','Linear',False,False,False,'cancel',64),
 ('S09','u16',9,17,32,False,'XZ',2,16,3,'2','Nearest',False,False,False,'focused_escape',64),
 ('S10','u16',9,17,32,False,'XZ',2,16,3,'0','Nearest',False,False,False,'submit_cleanup',64),
 ('S11','u16',9,17,32,False,'XZ',2,16,3,'-1','Nearest',False,False,False,'submit_cleanup',64),
 ('S12','u16',9,17,32,False,'XZ',2,16,3,'9.0949470177292824e-13','Nearest',False,False,False,'submit_cleanup',64),
 ('S13','u16',9,17,32,False,'XZ',2,16,3,'1000000000','Nearest',False,True,True,'submit',64),
 ('S14','u16',9,17,32,False,'XZ',0,16,3,'2','Nearest',False,False,False,'submit_cleanup',64),
 ('S15','u8',257,33,257,False,'XZ',1,33,1,'4','Nearest',False,False,False,'submit_cleanup',1),
 ('S16','u16',3,2,4097,False,'XZ',1,2,1,'4','Nearest',False,False,False,'submit_cleanup',64),
 ('S17','u16',9,17,32,False,'XZ',2,16,3,'2','Nearest',False,False,False,'stale_pixels',64),
 ('S18','u16',9,17,32,False,'XZ',2,16,3,'2','Nearest',False,False,False,'stale_calibration',64),
 ('S19','u16',9,17,32,False,'XZ',2,16,3,'2','Nearest',False,False,False,'closed_source',64),
 ('S20','u16',9,17,32,False,'XZ',2,16,3,'2','Nearest',False,False,False,'active_other',64),
)
CASES = {r[0]: dict(zip(('profile','z','y','x','tail','plane','first','last','step','h_text','sampling',
                        'reverse','flip','transpose','scenario','memory_mb'), r[1:])) for r in CASE_TABLE}
SUCCESS_IDS = frozenset(('S01','S02','S03','S04','S05','S06','S07','S13','S20'))
CANCEL_IDS = frozenset(('S08','S09'))
KEYS = {'Escape','Delete','Left','Right','Up','Down','PageUp','PageDown','Home','End','Space',
        'Shift','Ctrl','Alt','Super','A','W','Enter','Tab','Backspace'}
USAGE_FIELDS = {'running','queued','running_bytes','retained_bytes','reserved_bytes'}


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
        assert control['enabled'] is True
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
        assert self.raw_cache[name] == expected, 'Actual samples differ from independent endpoint-grid oracle'
        self.checked_raw.add(name)

    def finish(self):
        assert self.checked_raw == set(self.raw_descriptors), 'Raw descriptor escaped scientific validation'
        assert _inventory(self.directory) == self.before, 'Observation tree changed during cold assertions'


def _semantic(value):
    if isinstance(value, dict):
        return {key: _semantic(item) for key, item in value.items()
                if key not in {'raw_member','stored_raw_member','world_points','world_points_scope'}}
    if isinstance(value, list):
        return [_semantic(item) for item in value]
    return value


def _same(before, after):
    assert json.dumps(_semantic(before), sort_keys=True, separators=(',', ':'), allow_nan=False) == json.dumps(
        _semantic(after), sort_keys=True, separators=(',', ':'), allow_nan=False), 'Actual state or JSON types changed'


def _collection(value, profile):
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
    assert value['busy'] is False
    _usage(value['scheduler_usage'], idle=True)
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


def _direction(spec):
    return ((-1., .25, 1.5 if spec['tail'] else 0.), (.5, 1., 0.), (0., .75, 2.))


def _calibration(spec, *, changed=False):
    return {'spacing': [1.,1.25,2.] if changed else list(SPACING), 'origin': list(ORIGIN),
            'direction': [v for row in _direction(spec) for v in row], 'unit': 'um',
            'time_unit': 's','frame_interval': .75,'time_origin': 2.25}


def _source_shape(spec):
    return (1,1,spec['z'],spec['y'],spec['x'],PROFILES[spec['profile']][3])


def _world(xyz, spec):
    direction = _direction(spec)
    return [ORIGIN[r] + sum(direction[r][a] * SPACING[a] * xyz[a] for a in range(3)) for r in range(3)]


def _sample_bits(profile, x, y, z, s, *, other=False, edited=False):
    if other:
        value = 187.
    elif edited and (x,y,z,s) == (0,0,0,0):
        value = 7.
    elif profile == 'u8':
        value = float((37*x + 61*y + 13*x*y + 43*z) % 251)
    elif profile == 'u16':
        value = float(6000*z + 32*y + x)
    elif profile in {'rgb_u8','rgba_f32'}:
        value = float(((17*x+31*y+47*z)%256, (71*x+11*y+29*z)%256,
                       (13*x+61*y+23*z)%256, (19*x+53*y+7*z+127)%256)[s])
        if profile == 'rgba_f32':
            value += .25
    else:
        if (x,y,z) == (3,2,0):
            # Explicit binary32-origin sign/exponent/payload field placement.
            return struct.pack('<Q', 0x7ff0000000000000 | ((0x7fc12345 & 0x007fffff) << 29))
        value = -0. if (x,y,z) == (5,2,4) else 0. if (x,y,z) == (5,2,2) else 10000*z+100*y+x+.25
    return struct.pack('<d', value)


def _typed_bits(stored, profile):
    code = PROFILES[profile][2]
    if code == 'f':
        bits = struct.unpack('<Q', stored)[0]
        if bits & 0x7ff0000000000000 == 0x7ff0000000000000:
            assert bits & 0x0008000000000000 and bits & ((1 << 29)-1) == 0
            return struct.pack('<I', ((bits >> 32) & 0x80000000) | 0x7f800000 | ((bits & 0xfffffffffffff) >> 29))
        return struct.pack('<f', struct.unpack('<d', stored)[0])
    value = struct.unpack('<d', stored)[0]
    assert math.isfinite(value) and value == math.floor(value)
    return struct.pack('<'+code, int(value))


@lru_cache(maxsize=4)
def _source_buffers(case_id, other=False, edited=False):
    spec = CASES[case_id]
    profile, samples = spec['profile'], PROFILES[spec['profile']][3]
    stored, typed = bytearray(), bytearray()
    for z in range(spec['z']):
        for y in range(spec['y']):
            for x in range(spec['x']):
                for s in range(samples):
                    bits = _sample_bits(profile,x,y,z,s,other=other,edited=edited)
                    stored.extend(bits)
                    typed.extend(_typed_bits(bits,profile))
    return bytes(stored), bytes(typed)


def _grid(spec):
    a, _, f = AXES[spec['plane']]
    first, last, step = spec['first']-1, spec['last']-1, spec['step']
    length = 5. if spec['tail'] else 4.
    delta = float(spec['h_text']) / length
    vertical = 1 + math.floor((spec['z']-1) / delta)
    count = 1 + (last-first)//step
    last_selected = first + (count-1)*step
    effective_first = last_selected if spec['reverse'] else first
    effective_last = first if spec['reverse'] else last_selected
    spatial = (spec['x'],spec['y'],spec['z'])
    horizontal = spatial[a]
    axes = (2,a,f) if spec['transpose'] else (a,2,f)
    width, height = (vertical,horizontal) if spec['transpose'] else (horizontal,vertical)
    return {'shape': (1,1,count,height,width,PROFILES[spec['profile']][3]), 'axes': axes,
            'length': length, 'delta': delta, 'vertical': vertical, 'count': count,
            'last_selected': last_selected,'effective_first':effective_first,'effective_last':effective_last}


def _linear(left, right, weight, profile):
    if weight == 0. or left == right:
        return left  # Bitwise copy includes equal signed zeros and endpoints.
    a, b = struct.unpack('<d',left)[0], struct.unpack('<d',right)[0]
    if a == b:
        return left
    difference = b - a
    product = weight * difference
    result = a + product  # Separate binary64 operations; no fused multiply/add.
    assert math.isfinite(result)
    if profile in {'u8','rgb_u8','u16'}:
        maximum = 65535. if profile == 'u16' else 255.
        result = float(math.floor(min(max(result,0.),maximum) + .5))
    else:
        assert profile == 'rgba_f32'
        result = float(struct.unpack('<f', struct.pack('<f',result))[0])
    return struct.pack('<d',result)


@lru_cache(maxsize=4)
def _output_buffers(case_id):
    spec, grid = CASES[case_id], _grid(CASES[case_id])
    profile = spec['profile']
    a, _, f = AXES[spec['plane']]
    _, _, count, height, width, samples = grid['shape']
    stored, typed = bytearray(), bytearray()
    for k in range(count):
        fixed = grid['last_selected'] - k*spec['step'] if spec['reverse'] else spec['first']-1+k*spec['step']
        for v in range(height):
            for u in range(width):
                horizontal, j = (v,u) if spec['transpose'] else (u,v)
                product = float(j) * grid['delta']
                q = min(max(product,0.),float(spec['z']-1))
                if spec['flip']:
                    q = float(spec['z']-1) - q
                xyz = [0,0,0]
                xyz[a], xyz[f] = horizontal, fixed
                for s in range(samples):
                    if spec['sampling'] == 'Nearest':
                        xyz[2] = min(max(math.floor(q+.5),0),spec['z']-1)
                        bits = _sample_bits(profile,*xyz,s)
                    else:
                        lower = math.floor(q)
                        upper = min(lower+1,spec['z']-1)
                        weight = q-float(lower)
                        xyz[2] = lower
                        left = _sample_bits(profile,*xyz,s)
                        xyz[2] = upper
                        right = _sample_bits(profile,*xyz,s)
                        bits = left if lower == upper or weight == 0. else _linear(left,right,weight,profile)
                    stored.extend(bits)
                    typed.extend(_typed_bits(bits,profile))
    return bytes(stored), bytes(typed)


def _output_calibration(spec):
    grid = _grid(spec)
    a, _, f = AXES[spec['plane']]
    q0 = [0,0,0]
    q0[2], q0[f] = spec['z']-1 if spec['flip'] else 0, grid['effective_first']
    spacing = [SPACING[a],SPACING[2]*grid['delta'],SPACING[f]*spec['step']]
    columns = [a,2,f]
    signs = [1.,-1. if spec['flip'] else 1.,-1. if spec['reverse'] else 1.]
    if spec['transpose']:
        spacing[0], spacing[1] = spacing[1], spacing[0]
        columns[0], columns[1] = columns[1], columns[0]
        signs[0], signs[1] = signs[1], signs[0]
    direction = _direction(spec)
    return {'spacing':spacing,'origin':_world(q0,spec),
            'direction':[direction[r][axis]*signs[c] for r in range(3) for c,axis in enumerate(columns)],
            'unit':'um','time_unit':'s','frame_interval':.75,'time_origin':2.25}


def _exact_calibration(actual, expected):
    assert set(actual) == set(expected)
    for key, value in expected.items():
        if isinstance(value, str):
            assert actual[key] == value
        else:
            _close(actual[key], value)


def _provenance(spec, source_id):
    grid = _grid(spec)
    return {'schema_version':1,'source_document_id':source_id,'plane':spec['plane'],
            'first':spec['first']-1,'last':spec['last']-1,'step':spec['step'],'count':grid['count'],
            'last_selected':grid['last_selected'],'t':0,'c':0,'reverse_order':spec['reverse'],
            'flip_vertical':spec['flip'],'transpose':spec['transpose'],
            'horizontal_axis':'XYZ'[grid['axes'][0]],'vertical_axis':'XYZ'[grid['axes'][1]],
            'fixed_axis':'XYZ'[grid['axes'][2]],'index_convention':'zero_based_voxel_centers',
            'sampling':spec['sampling'].lower(),'physical_vertical_spacing':float(spec['h_text']),
            'source_vertical_spacing':grid['length'],'voxel_step':grid['delta'],'vertical_count':grid['vertical'],
            'grid_policy':'source_endpoint_centers_physical_z','border_policy':'clamp_endpoint_roundoff',
            'quantization_policy':'stored_double_copy_else_declared_type_linear'}


def _typed_equal(actual, expected):
    assert type(actual) is type(expected) or (type(expected) is float and type(actual) in {int,float})
    if isinstance(expected, dict):
        assert set(actual) == set(expected)
        for key, value in expected.items():
            _typed_equal(actual[key], value)
    elif isinstance(expected, list):
        assert len(actual) == len(expected)
        for a, e in zip(actual, expected):
            _typed_equal(a,e)
    elif type(expected) is float:
        _close(actual, expected)
    else:
        assert actual == expected


def _literal_input(spec):
    return {'source':{'profile':spec['profile'],'shape':list(_source_shape(spec)),
                      'axes':'TCZYXS','calibration':_calibration(spec)},
            'form_1based':{'plane':spec['plane'],'first':spec['first'],'last':spec['last'],'step':spec['step'],
                          'physical_vertical_spacing_text':spec['h_text'],'sampling':spec['sampling'],
                          'reverse_order':spec['reverse'],'flip_vertical':spec['flip'],'transpose':spec['transpose']},
            'scenario':spec['scenario'],'processing_memory_mb':spec['memory_mb']}


def _initial(row, files):
    spec = CASES[row['case_id']]
    ready = _collection(row['states']['source_ready'],spec['profile'])
    source, other = ready['source'], ready['other']
    assert (other is not None) == (row['case_id'] in {'S19','S20'})
    ids = [source['document_id']] + ([other['document_id']] if other else [])
    assert ready['native_member_ids'] == ready['facade_ids'] == ids
    assert ready['active_document_id'] == source['document_id'] and ready['active_index'] == 0
    assert ready['output'] is None and ready['outputs'] == []
    _same(row['source_before'],source)
    _same(row['captured_source'],source)
    assert row['source_before_raw'] == source['raw_member']
    assert row['source_before_stored_raw'] == source['stored_raw_member']
    assert source['metadata'] == {'title':'Physical Spacing Source',
        'properties':{'source_only':'retained property','revision_probe':7},
        'extensions':{'source_extension':{'retained':True}}}
    assert source['dirty'] is True and source['undo_steps'] == source['data_identity'] == 1 and source['redo_steps'] == 0
    assert source['history_limit'] == 3 and source['history_pixel_budget'] == 4096
    assert source['native_available'] is True
    assert source['revisions'] == {'pixels':0,'metadata':1,'selection':0,'display':2}
    assert source['dataset']['shape'] == list(_source_shape(spec))
    _exact_calibration(source['dataset']['calibration'],_calibration(spec))
    assert source['record_layout'] == {} and source['spectrum'] is None
    _literal_selection(source['selection'],'rectangle',[[0,0],[1,1]],{'tag':'transient'})
    _literal_selection(source['previous_selection'],'ellipse',[[0,0],[1,1]],{'tag':'previous'})
    assert len(source['rois']) == 2
    for roi, name, kind, tag, z, attr in zip(source['rois'],('Saved rectangle','Saved second'),
            ('rectangle','ellipse'),('saved_first','saved_second'),(0,1),('first','second')):
        assert roi['name'] == name and roi['t'] is roi['c'] is None and roi['z'] == z
        assert roi['attributes'] == {'source_only':attr}
        _literal_selection(roi['selection'],kind,[[0,0],[1,1]],{'tag':tag})
    assert source['overlays'] == [{'source_overlay':'retained'}]
    display = source['display']
    assert display == {'t':0,'c':0,'crosshair':[min(7,spec['x']-1),min(5,spec['y']-1),min(4,spec['z']-1)],
        'orthogonal':False,'linked_zoom':True,'zoom':2.,'plane_zoom':[0.,0.,0.],
        'pan':[[0.,0.],[0.,0.],[0.,0.]],'properties':{'display_range':[0,255],'lut':'Ice'},'revision':2}
    if other:
        assert other['metadata'] == {'title':'Other','properties':{},'extensions':{}}
        assert other['dirty'] is False and other['native_available'] is True
        assert other['undo_steps'] == other['redo_steps'] == other['data_identity'] == 0
        assert other['history_limit'] == 5 and other['history_pixel_budget'] == 8*1024*1024
        assert other['revisions'] == {'pixels':0,'metadata':0,'selection':0,'display':1}
        assert other['selection'] is other['previous_selection'] is other['spectrum'] is None
        assert other['rois'] == other['overlays'] == [] and other['record_layout'] == {}
        assert other['dataset']['shape'] == list(_source_shape(spec))
        _exact_calibration(other['dataset']['calibration'],_calibration(spec))
        d = other['display']
        assert d['crosshair'] == [spec['x']//2,spec['y']//2,0]
        assert d['t'] == d['c'] == 0 and d['revision'] == 1
        assert d['orthogonal'] is False and d['linked_zoom'] is True and d['properties'] == {}
        assert d['plane_zoom'] == [0,0,0] and d['pan'] == [[0,0],[0,0],[0,0]]
        _close(d['zoom'],1.25/math.hypot(-.5,.25,0.))
    return ready


def _output(snapshot, row):
    spec = CASES[row['case_id']]
    grid = _grid(spec)
    _snapshot(snapshot,spec['profile'])
    assert snapshot['dataset']['shape'] == list(grid['shape'])
    expected_cal = _output_calibration(spec)
    _exact_calibration(snapshot['dataset']['calibration'],expected_cal)
    assert snapshot['selection'] is snapshot['previous_selection'] is snapshot['spectrum'] is None
    assert snapshot['rois'] == snapshot['overlays'] == [] and snapshot['record_layout'] == {}
    assert snapshot['metadata']['properties'] == {} and set(snapshot['metadata']['extensions']) == {'axis_range_spacing'}
    provenance = snapshot['metadata']['extensions']['axis_range_spacing']
    assert len(provenance) == 25
    _typed_equal(provenance,_provenance(spec,row['source_before']['document_id']))
    title_h = '1e+09' if spec['h_text'] == '1000000000' else spec['h_text']
    assert snapshot['metadata']['title'] == (
        f"Physical Spacing Source {spec['plane']} {spec['first']}-{spec['last']} step {spec['step']} "
        f"Z spacing {title_h} um {spec['sampling'].lower()} "
        f"[R{int(spec['reverse'])} F{int(spec['flip'])} T{int(spec['transpose'])}]")
    assert snapshot['native_available'] is True and snapshot['dirty'] is False
    assert snapshot['data_identity'] == snapshot['undo_steps'] == snapshot['redo_steps'] == 0
    assert snapshot['history_limit'] == 5 and snapshot['history_pixel_budget'] == 8*1024*1024
    assert snapshot['revisions'] == {'pixels':0,'metadata':0,'selection':0,'display':1}
    display = snapshot['display']
    assert display['t'] == display['c'] == 0 and display['revision'] == 1
    assert display['crosshair'] == [grid['shape'][4]//2,grid['shape'][3]//2,0]
    assert display['orthogonal'] is False and display['linked_zoom'] is True and display['properties'] == {}
    assert display['plane_zoom'] == [0,0,0] and display['pan'] == [[0,0],[0,0],[0,0]]
    horizontal_basis = [expected_cal['direction'][r*3] * expected_cal['spacing'][0] for r in range(3)]
    _close(display['zoom'],1.25/math.hypot(*horizontal_basis))
    if 'world_points' in snapshot:
        _,_,nz,ny,nx,_ = grid['shape']
        voxels = {(0,0,0),(1,0,0),(0,1,0),(0,0,1)}
        voxels.update((x,y,z) for x in (0,nx-1) for y in (0,ny-1) for z in (0,nz-1))
        assert snapshot['world_points_scope'] == 'observed_affine_origin_unit_basis_and_corners'
        assert 1 <= len(snapshot['world_points']) == len(voxels) <= 12
        for actual, xyz in zip(snapshot['world_points'],sorted(voxels)):
            assert set(actual) == {'voxel_xyz','world_xyz'} and actual['voxel_xyz'] == list(xyz)
            _close(actual['voxel_xyz'],xyz)
            # Full signed affine is checked separately from discrete sampling.
            expected = [expected_cal['origin'][r] + sum(expected_cal['direction'][r*3+a] *
                        expected_cal['spacing'][a] * xyz[a] for a in range(3)) for r in range(3)]
            _close(actual['world_xyz'],expected)


def _all_raw(row, files):
    spec = CASES[row['case_id']]
    source_id = row['source_before']['document_id']
    other = row['states']['source_ready']['other']
    other_id = other['document_id'] if other else None
    descriptors, complete_outputs = {}, set()
    verified = set()
    def walk(value):
        if isinstance(value, dict):
            if {'member_id','path','dtype','encoding','sha256','byte_count'} <= value.keys():
                assert value['path'] not in descriptors or descriptors[value['path']] == value
                descriptors[value['path']] = value
            if {'document_id','owner_id','dataset','revisions'} <= value.keys():
                _snapshot(value,spec['profile'])
                identity = value['document_id']
                if identity == source_id or identity == other_id:
                    assert value['dataset']['shape'] == list(_source_shape(spec))
                    edited = identity == source_id and value['revisions']['pixels'] != 0
                    assert not edited or row['case_id'] == 'S17' and value['revisions']['pixels'] == 1
                    changed = identity == source_id and value['revisions']['metadata'] == 2
                    assert not changed or row['case_id'] == 'S18'
                    _exact_calibration(value['dataset']['calibration'],_calibration(spec,changed=changed))
                    shape = _source_shape(spec)
                    expected = _source_buffers(row['case_id'],identity == other_id,edited)
                else:
                    assert row['case_id'] in SUCCESS_IDS, 'Rejected or canceled case published an owner'
                    _output(value,row)
                    shape, expected = _grid(spec)['shape'], _output_buffers(row['case_id'])
                    if 'world_points' in value:
                        complete_outputs.add(identity)
                has_raw = 'raw_member' in value
                assert has_raw == ('stored_raw_member' in value)
                if has_raw:
                    for storage, key, bits in ((True,'stored_raw_member',expected[0]),(False,'raw_member',expected[1])):
                        member = value[key]
                        request = (member['path'],identity,value['revisions']['pixels'])
                        if request not in verified:
                            files.raw(member,spec['profile'],shape,bits,storage=storage)
                            verified.add(request)
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
    walk({k:v for k,v in row.items() if k not in {'raw_members','artifact_tree'}})
    assert descriptors == files.raw_descriptors
    if row['case_id'] in SUCCESS_IDS:
        assert complete_outputs == {row['outputs'][0]['document_id']}
    else:
        assert complete_outputs == set()
    assert row['source_after_raw'] == row['source_after']['raw_member']
    assert row['source_after_stored_raw'] == row['source_after']['stored_raw_member']
    if row['case_id'] in SUCCESS_IDS:
        assert row['output_raw'] == row['outputs'][0]['raw_member']
        assert row['output_stored_raw'] == row['outputs'][0]['stored_raw_member']
    else:
        assert 'output_raw' not in row and 'output_stored_raw' not in row


def _stimuli(row):
    case_id, spec = row['case_id'], CASES[row['case_id']]
    entries = row['core_stimuli']
    expected_label = {'S17':'pixel_edit','S18':'calibration_edit','S19':'source_close','S20':'activate_other'}.get(case_id)
    assert [e['ordinal'] for e in entries] == list(range(1,len(entries)+1))
    assert [e['label'] for e in entries] == ([expected_label] if expected_label else [])
    ready = row['states']['source_ready']
    if not entries:
        return
    entry = entries[0]
    assert set(entry) == {'ordinal','label','method','arguments','classification','before','after','exception'}
    assert entry['classification'] == 'declared_core_state_stimulus_no_physical_credit' and entry['exception'] is None
    before, after = (_collection(entry[key],spec['profile']) for key in ('before','after'))
    _same(before,ready)
    source_id = ready['source']['document_id']
    if case_id == 'S17':
        assert entry['method'] == 'commitEdit.replace_plane'
        assert entry['arguments'] == {'document_id':source_id,'t':0,'c':0,'z':0,'x':0,'y':0,'s':0,'value':7}
        changed = {'data_identity','undo_steps','revisions','raw_member','stored_raw_member'}
        assert after['source']['revisions'] == {'pixels':1,'metadata':1,'selection':0,'display':2}
        assert after['source']['data_identity'] == 2 and after['source']['undo_steps'] == 0
    elif case_id == 'S18':
        assert entry['method'] == 'commitEdit.set_calibration'
        assert entry['arguments'] == {'document_id':source_id,'spacing':[1.,1.25,2.]}
        changed = {'data_identity','undo_steps','revisions','dataset'}
        assert after['source']['revisions'] == {'pixels':0,'metadata':2,'selection':0,'display':2}
        assert after['source']['data_identity'] == 2 and after['source']['undo_steps'] == 2
        _exact_calibration(after['source']['dataset']['calibration'],_calibration(spec,changed=True))
        assert after['source']['raw_member'] == before['source']['raw_member']
        assert after['source']['stored_raw_member'] == before['source']['stored_raw_member']
    elif case_id == 'S19':
        assert entry['method'] == 'closeDocument' and entry['arguments'] == {'document_id':source_id,'discard':True}
        changed = {'native_available'}
        assert before['source']['native_available'] is True and after['source']['native_available'] is False
        assert source_id not in after['native_member_ids']
        assert after['native_member_ids'] == after['facade_ids'] == [ready['other']['document_id']]
        assert after['active_document_id'] == ready['other']['document_id'] and after['active_index'] == 0
    else:
        assert entry['method'] == 'setActiveIndex'
        assert entry['arguments'] == {'document_id':ready['other']['document_id'],'index':1}
        changed = set()
        assert after['active_document_id'] == ready['other']['document_id'] and after['active_index'] == 1
        assert after['native_member_ids'] == before['native_member_ids']
    for field in before['source']:
        if field not in changed:
            _same(before['source'][field],after['source'][field])
    if case_id == 'S18':
        for field in before['source']['dataset']:
            if field != 'calibration':
                _same(before['source']['dataset'][field],after['source']['dataset'][field])
    _same(before['other'],after['other'])
    assert after['outputs'] == before['outputs'] == []
    checkpoint = {'S17':'after_pixel_edit','S18':'after_calibration_edit','S19':'after_source_close','S20':'after_activate_other'}[case_id]
    _same(after,row['states'][checkpoint])
    _same(after,row['states']['before_submit'])


def _abi_sizes():
    value = os.environ.get('IMAGEJXX_AXIS_RANGE_SPACING_ABI_SIZES')
    assert value, 'Pin measured native sizeof(Image),sizeof(shared_ptr<const Image>) for estimate checks'
    assert re.fullmatch(r'[1-9][0-9]{0,3},[1-9][0-9]{0,3}',value)
    image, pointer = (int(v) for v in value.split(','))
    assert image >= 16 and pointer >= 2*struct.calcsize('P')
    return image, pointer


def _working_bytes(spec, grid):
    a, _, _ = AXES[spec['plane']]
    spatial = (spec['x'],spec['y'],spec['z'])
    horizontal_samples = spatial[a] * PROFILES[spec['profile']][3]
    output = math.prod(grid['shape'])
    input_samples = horizontal_samples * spec['z']
    fallback = spec['x'] * spec['y'] * PROFILES[spec['profile']][3]
    image, pointer = _abi_sizes()
    return 8*(output+input_samples+fallback+horizontal_samples) + grid['count']*(2*image+pointer+256) + 2*image+256


def _preview(dialog, spec):
    values = dialog['values']
    request = dict(spec,plane=values['plane'],first=values['first'],last=values['last'],step=values['step'],
                   h_text=str(values['physical_vertical_spacing']),sampling=values['sampling'],
                   reverse=values['reverse_order'],flip=values['flip_vertical'],transpose=values['transpose'])
    assert 'render_error' not in dialog['spacing_preview']
    try:
        assert float(request['h_text']) > 0 and 1 <= request['first'] <= request['last']
        extent = request['y'] if request['plane'] == 'XZ' else request['x']
        assert request['last'] <= extent and request['step'] > 0
        grid = _grid(request)
        assert grid['vertical'] <= 2147483647 - 8194
        working = _working_bytes(request,grid)
        assert working <= spec['memory_mb']*1024*1024
        assert not (spec['profile'] == 'quiet_f32' and request['sampling'] == 'Linear')
        admitted = True
    except (AssertionError,ZeroDivisionError,OverflowError,ValueError):
        admitted = False
    preview = dialog['spacing_preview']
    if not admitted:
        assert set(preview) == {'error'} and isinstance(preview['error'],str) and preview['error']
        assert dialog['emitted_plane_count'] is None
        return
    expected = {'width':grid['shape'][4],'height':grid['shape'][3],'samples':grid['shape'][5],
        'emitted_plane_count':grid['count'],'vertical_count':grid['vertical'],
        'last_selected_1based':grid['last_selected']+1,'effective_first_selected_1based':grid['effective_first']+1,
        'effective_last_selected_1based':grid['effective_last']+1,'output_axis_order':['XYZ'[a] for a in grid['axes']],
        'source_vertical_spacing':grid['length'],'voxel_step':grid['delta'],'working_bytes':working,
        'render_limit':4096,'render_limit_source':'headless_frontend_4096'}
    render_admitted = expected['width'] <= 4096 and expected['height'] <= 4096
    assert set(preview) == set(expected) | (set() if render_admitted else {'error'})
    _typed_equal({key:preview[key] for key in expected},expected)
    assert dialog['emitted_plane_count'] == grid['count']
    if not render_admitted:
        assert isinstance(preview['error'],str) and preview['error']


def _dialog(row, label, *, plane, first, last, step, h, sampling, flags=(False,False,False)):
    spec = CASES[row['case_id']]
    dialog = _frame(row,label)['rendered']['dialog']
    assert dialog['open'] is True and dialog['title'] == 'Reslice physical spacing' and dialog['is_operation'] is False
    assert dialog['opened_document_id'] == row['source_before']['document_id']
    assert dialog['opened_document_id_semantics'] == 'captured_native_execution_source' and dialog['retained_source'] is True
    expected = {'plane':plane,'first':first,'last':last,'step':step,'physical_vertical_spacing':float(h),
                'sampling':sampling,'reverse_order':flags[0],'flip_vertical':flags[1],'transpose':flags[2]}
    _typed_equal(dialog['values'],expected)
    for field, value in (('plane',plane),('first',str(first)),('last',str(last)),('step',str(step)),('sampling',sampling)):
        assert dialog['text'][field] == value
    assert float(dialog['text']['physical_vertical_spacing']) == float(h)
    schema = {field['name']:field for field in dialog['schema']}
    assert len(dialog['schema']) == len(schema) == 9 and set(schema) == set(expected)
    assert schema['plane'] == {'name':'plane','kind':'text','label':'Plane','choices':['XZ','YZ']}
    assert schema['sampling'] == {'name':'sampling','kind':'text','label':'Sampling','choices':['Nearest','Linear']}
    for field, kind, text in (
            ('first','int','First (1-based)'),('last','int','Last (inclusive, 1-based)'),('step','int','Step'),
            ('physical_vertical_spacing','float','Physical Z spacing before transpose (um)'),
            ('reverse_order','bool','Reverse selected planes'),
            ('flip_vertical','bool','Flip source Z extent before sampling'),('transpose','bool','Transpose output axes')):
        assert schema[field] == {'name':field,'kind':kind,'label':text}
    assert not {'unit','physical_unit'} & set(schema)
    fields = ['plane','first','last','step','physical_vertical_spacing','sampling','reverse_order','flip_vertical','transpose']
    ids = []
    for field in fields:
        control = dialog['controls'][field]
        _actual_control(control)
        assert control['enabled'] is True
        ids.append(control['id'])
        if field in {'first','last','step','physical_vertical_spacing'}:
            name = 'InputDouble' if field == 'physical_vertical_spacing' else 'InputInt'
            assert control['identity_source'] == f'current_scoped_{name}_text_label_id'
            assert control['geometry_source'] == f'actual_{name}_group_item_rect'
            assert control['rect_kind'] == ('group_including_label' if name == 'InputDouble' else 'group_including_step_buttons_and_label')
    assert len(set(ids)) == len(ids), 'Fields share an ImGui identity'
    extent = spec['y'] if plane == 'XZ' else spec['x']
    assert dialog['fixed_axis_label'] == f"{'Y' if plane == 'XZ' else 'X'} indices: 1 to {extent}"
    _preview(dialog,spec)
    return dialog


def _typed_inputs(row):
    for frame in row['frames']:
        texts = [event['value'] for event in frame['requested_events'] if event['type'] == 'text']
        if not texts:
            continue
        assert len(texts) == 1
        ordinal = frame['ordinal']
        down, up = row['frames'][ordinal-3:ordinal-1]
        assert down['label'] == frame['label'].removesuffix('.text')+'.select_all.down'
        assert up['label'] == frame['label'].removesuffix('.text')+'.select_all.up'
        for actual, value in ((down,True),(up,False)):
            assert actual['requested_events'] == [{'type':'keys','keys':[{'name':'Ctrl','down':value},{'name':'A','down':value}]}]
        field = 'physical_vertical_spacing' if frame['label'] == 'spacing_edited.text' else frame['label'].removesuffix('.text').rsplit('.',1)[-1]
        assert field in {'first','last','step','physical_vertical_spacing'}
        rendered = frame['rendered']
        control, actual = rendered['dialog']['controls'][field], rendered['input_frame']['input_text']
        assert control['active'] is True
        assert actual['id'] == control['id'] == rendered['input_frame']['focus']['active_id']
        assert actual['length'] == actual['cursor'] == len(texts[0].encode('utf-8'))
        assert actual['selection_start'] == actual['selection_end']
        if field == 'physical_vertical_spacing':
            assert texts[0] == CASES[row['case_id']]['h_text']
            assert float(rendered['dialog']['text'][field]) == float(texts[0])
        else:
            assert rendered['dialog']['text'][field] == texts[0]
        selected = down['rendered']['input_frame']['input_text']
        assert selected['id'] == control['id'] and selected['length'] > 0
        assert abs(selected['selection_end']-selected['selection_start']) == selected['length']
        assert any(observation['selector'] == 'dialog.'+field and
                   observation['before_frame_ordinal']+3 < down['ordinal'] for observation in row['control_observations'])


def _configuration(row):
    spec = CASES[row['case_id']]
    length = 5. if spec['tail'] else 4.
    _dialog(row,'dialog_opened',plane='XZ',first=1,last=spec['y'],step=1,h=length,sampling='Nearest')
    if spec['scenario'] == 'choice_changes':
        _dialog(row,'initial_xz_range',plane='XZ',first=2,last=16,step=3,h=length,sampling='Nearest')
        _dialog(row,'spacing_edited',plane='XZ',first=2,last=16,step=3,h=7,sampling='Nearest')
        for label,plane,sampling in (('yz_axis_reset.1','YZ','Nearest'),('linear_selected.1','YZ','Linear'),
                ('xz_axis_reset','XZ','Linear'),('nearest_selected','XZ','Nearest'),
                ('yz_axis_reset.2','YZ','Nearest'),('linear_selected.2','YZ','Linear')):
            _dialog(row,label,plane=plane,first=1,last=spec['y'] if plane == 'XZ' else spec['x'],
                    step=1,h=7,sampling=sampling)
        expected_choices = ['dialog.plane.choice.YZ','dialog.sampling.choice.Linear',
            'dialog.plane.choice.XZ','dialog.sampling.choice.Nearest',
            'dialog.plane.choice.YZ','dialog.sampling.choice.Linear']
        _dialog(row,'range_edited',plane='YZ',first=2,last=31,step=3,h=7,sampling='Linear')
    else:
        extent = spec['y'] if spec['plane'] == 'XZ' else spec['x']
        _dialog(row,'plane_selected',plane=spec['plane'],first=1,last=extent,step=1,h=length,sampling='Nearest')
        _dialog(row,'sampling_selected',plane=spec['plane'],first=1,last=extent,step=1,h=length,sampling=spec['sampling'])
        _dialog(row,'range_edited',plane=spec['plane'],first=spec['first'],last=spec['last'],step=spec['step'],
                h=length,sampling=spec['sampling'])
        _dialog(row,'spacing_edited',plane=spec['plane'],first=spec['first'],last=spec['last'],step=spec['step'],
                h=spec['h_text'],sampling=spec['sampling'])
        expected_choices = ['dialog.plane.choice.'+spec['plane'],'dialog.sampling.choice.'+spec['sampling']]
    expected_flags = (spec['reverse'],spec['flip'],spec['transpose'])
    flags = [False,False,False]
    for index, field in enumerate(('reverse_order','flip_vertical','transpose')):
        flags[index] = expected_flags[index]
        _dialog(row,'flag.'+field+'.set',plane=spec['plane'],first=spec['first'],last=spec['last'],step=spec['step'],
                h=spec['h_text'],sampling=spec['sampling'],flags=tuple(flags))
    _dialog(row,'draft_configured',plane=spec['plane'],first=spec['first'],last=spec['last'],step=spec['step'],
            h=spec['h_text'],sampling=spec['sampling'],flags=expected_flags)
    selectors = [observation['selector'] for observation in row['control_observations']]
    assert selectors[:3] == ['menu.Image','menu.Image.Stacks','menu.action.show_axis_range_spacing']
    assert [selector for selector in selectors if '.choice.' in selector] == expected_choices
    for field, final in zip(('reverse_order','flip_vertical','transpose'),expected_flags):
        observations = [o for o in row['control_observations'] if o['selector'] == 'dialog.'+field]
        assert len(observations) == (1 if final else 2)
        for index, observation in enumerate(observations):
            ordinal = observation['before_frame_ordinal']
            before = row['frames'][ordinal-1]['rendered']['dialog']['values'][field]
            after = row['frames'][ordinal+2]['rendered']['dialog']['values'][field]
            assert before is bool(index) and after is (not bool(index))
    assert selectors.count('dialog.apply') == (0 if row['case_id'] in CANCEL_IDS else 1)
    expected_states = {'source_ready','dialog_opened','range_edited.first','range_edited.last',
        'range_edited.step','range_edited','spacing_edited','flag.reverse_order.set','flag.flip_vertical.set',
        'flag.transpose.set','draft_configured','final_idle'}
    if spec['scenario'] == 'choice_changes':
        expected_states |= {'initial_xz_range.first','initial_xz_range.last','initial_xz_range.step','initial_xz_range',
            'yz_axis_reset.1','linear_selected.1','xz_axis_reset','nearest_selected','yz_axis_reset.2','linear_selected.2'}
    else:
        expected_states |= {'plane_selected','sampling_selected'}
    if row['case_id'] == 'S08':
        expected_states |= {'requested_cancel.before','requested_cancel.after'}
    elif row['case_id'] == 'S09':
        expected_states |= {'before_focused_escape','after_focused_escape'}
        if row['escape_observation']['dialog_after']['open']:
            expected_states |= {'escape_cleanup_cancel.before','escape_cleanup_cancel.after'}
    else:
        expected_states |= {'before_submit','after_submit'}
        if row['case_id'] not in SUCCESS_IDS:
            expected_states |= {'error_acknowledged','cleanup_cancel.before','cleanup_cancel.after'}
    stimulus_state = {'S17':'after_pixel_edit','S18':'after_calibration_edit',
                      'S19':'after_source_close','S20':'after_activate_other'}.get(row['case_id'])
    if stimulus_state:
        expected_states.add(stimulus_state)
    assert set(row['states']) == expected_states, 'Missing, overwritten or unexpected controller checkpoint'
    assert set(selectors) <= {'menu.Image','menu.Image.Stacks','menu.action.show_axis_range_spacing',
        'dialog.plane','dialog.sampling','dialog.first','dialog.last','dialog.step','dialog.physical_vertical_spacing',
        'dialog.reverse_order','dialog.flip_vertical','dialog.transpose','dialog.apply','dialog.cancel','error_modal.ok',
        'dialog.plane.choice.XZ','dialog.plane.choice.YZ','dialog.sampling.choice.Nearest','dialog.sampling.choice.Linear'}
    # State checkpoints correspond to the real controller after each render.
    for label,state in row['states'].items():
        _collection(state,spec['profile'])
        if label != 'source_ready':
            _same(state,_frame(row,label)['native_state'])


def _submission(row):
    spec = CASES[row['case_id']]
    entries = row['submission_observations']
    if row['case_id'] in CANCEL_IDS:
        assert entries == [] and 'before_submit' not in row['states']
        return
    assert len(entries) == 1
    entry = entries[0]
    assert set(entry) == {'draft','before','after','dialog_after','error_modal_after','window_error',
                          'actual_output_ids','exception','exception_scope'}
    assert entry['exception'] is None
    assert entry['exception_scope'] == 'UI guard reports actual errors through modal and window_error'
    before, after = (_collection(entry[key],spec['profile']) for key in ('before','after'))
    _same(before,row['states']['before_submit'])
    _same(after,row['states']['after_submit'])
    rendered = _frame(row,'before_submit')['rendered']['dialog']
    _typed_equal(entry['draft'],{'form_values':rendered['values'],'form_text':rendered['text'],
        'opened_document_id':row['source_before']['document_id'],
        'opened_document_id_semantics':'captured_native_execution_source',
        'captured_document_id':row['source_before']['document_id'],
        'captured_revisions':{'pixels':0,'metadata':1,'selection':0,'display':2},
        'spacing_preview':rendered['spacing_preview']})
    assert entry['dialog_after'] == _frame(row,'after_submit')['rendered']['dialog']
    assert entry['error_modal_after'] == _frame(row,'after_submit')['rendered']['error_modal']
    _same(before['source'],after['source'])
    _same(before['other'],after['other'])
    if row['case_id'] in SUCCESS_IDS:
        output = after['output']
        assert output is not None and len(after['outputs']) == 1
        output_id = output['document_id']
        assert output_id not in before['native_member_ids']
        assert output_id not in {row['source_before']['document_id'],before['other']['document_id'] if before['other'] else None}
        assert entry['actual_output_ids'] == [output_id]
        assert after['native_member_ids'] == before['native_member_ids']+[output_id]
        assert after['facade_ids'] == before['facade_ids']+[output_id]
        assert after['active_document_id'] == output_id and after['active_index'] == len(after['facade_ids'])-1
        assert entry['window_error'] == '' and entry['error_modal_after']['open'] is False
        assert entry['dialog_after']['open'] is False and entry['dialog_after']['retained_source'] is False
        assert entry['dialog_after']['spacing_preview'] == {} and entry['dialog_after']['emitted_plane_count'] is None
        _output(output,row)
    else:
        assert entry['actual_output_ids'] == [] and after['output'] is None and after['outputs'] == []
        _same(before,after)
        assert isinstance(entry['window_error'],str) and entry['window_error']
        assert entry['dialog_after']['open'] is True and entry['dialog_after']['retained_source'] is True
        _same(entry['draft']['form_values'],entry['dialog_after']['values'])
        _same(entry['draft']['form_text'],entry['dialog_after']['text'])
        settled = _frame(row,'error_modal.settled.2')['rendered']['error_modal']
        assert settled['open'] is True and settled['message'] == entry['window_error']
        assert settled['acknowledged'] is False
        assert any(o['selector'] == 'error_modal.ok' for o in row['control_observations'])
        acknowledged = _frame(row,'error_acknowledged')['rendered']['error_modal']
        assert acknowledged['open'] is False
        _same(row['states']['after_submit'],row['states']['error_acknowledged'])
        assert any(o['selector'] == 'dialog.cancel' for o in row['control_observations'])
        _same(row['states']['cleanup_cancel.before'],row['states']['cleanup_cancel.after'])
        # Each failure is independently tied to its actual guard and request.
        error_tokens = {'S10':('positive',),'S11':('positive',),'S12':('vertical count',),
            'S14':('first',),'S15':('working-memory',),'S16':('limit',),
            'S17':('document changed since the edit began',),
            'S18':('document changed since the edit began',),'S19':('document',)}
        assert any(token in entry['window_error'].lower() for token in error_tokens[row['case_id']])


def _cancel_and_escape(row):
    if row['case_id'] == 'S08':
        _same(row['states']['requested_cancel.before'],row['states']['requested_cancel.after'])
        assert _frame(row,'requested_cancel.before')['rendered']['dialog']['open'] is True
        assert _frame(row,'requested_cancel.after')['rendered']['dialog']['open'] is False
    if row['case_id'] == 'S09':
        before = _frame(row,'before_focused_escape')['rendered']
        control = before['dialog']['controls']['physical_vertical_spacing']
        assert control['active'] is True and before['input_frame']['input_text']['id'] == control['id']
        for label, down in (('focused_escape.down',True),('focused_escape.up',False)):
            assert _frame(row,label)['requested_events'] == [{'type':'key','name':'Escape','down':down}]
        _same(row['states']['before_focused_escape'],row['states']['after_focused_escape'])
        assert row['escape_observation']['dialog_after'] == _frame(row,'after_focused_escape')['rendered']['dialog']
        _same(row['escape_observation']['state_after'],row['states']['after_focused_escape'])
        if row['escape_observation']['dialog_after']['open']:
            _same(row['states']['escape_cleanup_cancel.before'],row['states']['escape_cleanup_cancel.after'])
            assert _frame(row,'escape_cleanup_cancel.after')['rendered']['dialog']['open'] is False
        else:
            assert 'escape_cleanup_cancel.before' not in row['states']
    if row['case_id'] in CANCEL_IDS:
        assert row['outputs'] == []
        assert not any(o['selector'] in {'dialog.apply','error_modal.ok'} for o in row['control_observations'])


def _preserved(row):
    ready, final = row['states']['source_ready'], row['states']['final_idle']
    latest = row['core_stimuli'][-1]['after'] if row['core_stimuli'] else ready
    _same(latest['source'],final['source'])
    _same(ready['other'],final['other'])
    _same(row['source_after'],final['source'])
    assert row['outputs'] == final['outputs']
    initial_ids = ready['native_member_ids']
    expected_ids = [i for i in initial_ids if row['case_id'] != 'S19' or i != ready['source']['document_id']]
    if row['case_id'] in SUCCESS_IDS:
        expected_ids.append(final['output']['document_id'])
    assert final['native_member_ids'] == final['facade_ids'] == expected_ids
    frozen_before, frozen_after = row['captured_source'], row['captured_source_after']
    live_owner_fields = {'undo_steps','redo_steps','history_limit','history_pixel_budget','native_available'}
    for field in frozen_before:
        if field not in live_owner_fields:
            _same(frozen_before[field],frozen_after[field])
        else:
            assert frozen_after[field] == final['source'][field]
    assert frozen_after['data_identity'] == 1 and frozen_after['revisions'] == {'pixels':0,'metadata':1,'selection':0,'display':2}
    assert frozen_after['raw_member'] == frozen_before['raw_member']
    assert frozen_after['stored_raw_member'] == frozen_before['stored_raw_member']
    assert final['busy'] is False and row['final_busy'] is False and row['window_error'] == ''
    _usage(final['scheduler_usage'],idle=True)
    assert row['final_context'] == {'current_context_id':0}
    rendered = _frame(row,'final_idle')['rendered']
    assert rendered['dialog']['open'] is False and rendered['dialog']['retained_source'] is False
    assert rendered['dialog']['spacing_preview'] == {} and rendered['dialog']['emitted_plane_count'] is None
    assert rendered['error_modal']['open'] is False
    assert rendered['input_frame']['keys_down'] == [] and rendered['input_frame']['mouse_down'] == [False,False,False]
    if row['case_id'] == 'S20':
        assert row['states']['before_submit']['active_document_id'] == ready['other']['document_id']
        assert final['output']['metadata']['extensions']['axis_range_spacing']['source_document_id'] == ready['source']['document_id']


def _frame_preservation(row):
    """No transient source/history/owner mutation can hide between checkpoints."""
    ready = row['states']['source_ready']
    entry = row['core_stimuli'][0] if row['core_stimuli'] else None
    stimulus_label = {'S17':'after_pixel_edit','S18':'after_calibration_edit',
                      'S19':'after_source_close','S20':'after_activate_other'}.get(row['case_id'])
    changed_ordinal = _frame(row,stimulus_label)['ordinal'] if stimulus_label else len(row['frames'])+1
    published_id = row['outputs'][0]['document_id'] if row['case_id'] in SUCCESS_IDS else None
    published_ordinal = next((frame['ordinal'] for frame in row['frames']
                              if frame['native_state']['outputs']),len(row['frames'])+1)
    if published_id is not None:
        assert row['frames'][published_ordinal-1]['label'] == 'dialog.apply.up'
    for frame in row['frames']:
        state = frame['native_state']
        expected = entry['after'] if entry and frame['ordinal'] >= changed_ordinal else ready
        _same(state['source'],expected['source'])
        _same(state['other'],ready['other'])
        expected_ids = list(expected['facade_ids'])
        active = expected['active_document_id']
        if frame['ordinal'] >= published_ordinal:
            assert published_id is not None and len(state['outputs']) == 1
            assert state['outputs'][0]['document_id'] == published_id
            expected_ids.append(published_id)
            active = published_id
        else:
            assert state['outputs'] == [] and state['output'] is None
        assert state['facade_ids'] == state['native_member_ids'] == expected_ids
        assert state['active_document_id'] == state['facade_active_document_id'] == active
        assert state['active_index'] == expected_ids.index(active)


def assert_axis_range_spacing_observation(row, directory):
    """Cold strict reader; accepts observed files, never imports a product module."""
    case_id = row['case_id']
    assert case_id in CASES and row['context'] in CONTEXTS
    spec = CASES[case_id]
    assert row['schema_version'] == 1 and row['contract_id'] == CONTRACT
    assert type(row['schema_version']) is int
    assert row['profile'] == spec['profile']
    assert row['observation_state'] == 'observed' and row['acceptance_claim'] is False
    assert row['limits'] == {'max_frames':160,'max_seconds':15,'max_file_bytes':MAX_JSON_BYTES,
                            'max_tree_bytes':MAX_TREE_BYTES,'max_world_observations_per_document':12}
    assert 0 <= _number(row['elapsed_seconds']) <= 15
    _typed_equal(row['literal_input'],_literal_input(spec))
    _abi_sizes()  # Admission/preview cannot silently infer its native ABI.
    files = _CaseFiles(directory,row)
    assert _read_json(directory/'case.json') == row
    _initial(row,files)
    _physical_observations(row,directory)
    _typed_inputs(row)
    _configuration(row)
    _stimuli(row)
    _submission(row)
    _cancel_and_escape(row)
    _preserved(row)
    _frame_preservation(row)
    _all_raw(row,files)
    files.finish()
    source_stored, source_typed = _source_buffers(case_id)
    output_stored, output_typed = _output_buffers(case_id) if case_id in SUCCESS_IDS else (b'',b'')
    return {'case_id':case_id,'context':row['context'],'physical_frames':len(row['frames']),
        'actual_control_clicks':len(row['control_observations']),'raw_member_count':len(files.raw_descriptors),
        'source_samples':math.prod(_source_shape(spec)),
        'output_samples':len(output_stored)//8,'source_stored_sha256':hashlib.sha256(source_stored).hexdigest(),
        'source_typed_sha256':hashlib.sha256(source_typed).hexdigest(),
        'output_stored_sha256':hashlib.sha256(output_stored).hexdigest(),
        'output_typed_sha256':hashlib.sha256(output_typed).hexdigest(),
        'observed_output_owner_ids':[output['document_id'] for output in row['outputs']],
        'artifact_bytes':sum(member[1] for member in files.before)}


def _whole_tree(root, *, cpp):
    _namespace(root)
    names = {entry.name for entry in os.scandir(root)}
    assert names == set(CASE_IDS) | ({'axis-range-spacing-ui.json'} if cpp else set())
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
        total += len(_safe_bytes(root/'axis-range-spacing-ui.json'))
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
        assert len(self.entries) == 20

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
    assert report['acceptance_claim'] is False and report['case_count'] == 20
    assert report['case_format'] == 'indexed_case_files'
    assert report['limits'] == {'max_seconds':180,'max_case_frames':160,'max_case_seconds':15,
                               'max_file_bytes':MAX_JSON_BYTES,'max_tree_bytes':MAX_TREE_BYTES}
    assert 0 <= _number(report['elapsed_seconds']) <= 180
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
    root = tmp_path_factory.mktemp('axis-range-spacing-cpp')/'run'
    started = time.monotonic()
    completed = subprocess.run([str(installed_cpp_executable),'--axis-range-spacing-ui-consumer-test',
        '--consumer-dir',str(root)],capture_output=True,timeout=180,check=False)
    assert time.monotonic()-started <= 180
    assert len(completed.stdout)+len(completed.stderr) <= 16*1024*1024
    assert completed.returncode == 0, completed.stdout.decode('utf-8','replace')+completed.stderr.decode('utf-8','replace')
    report = _read_json(root/'axis-range-spacing-ui.json')
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
    root = tmp_path_factory.mktemp('axis-range-spacing-python')
    entries, started = [], time.monotonic()
    total = 0
    for case_id in CASE_IDS:
        editor = desktop.Editor(preferences_path=str(root/f'{case_id}-preferences.json'))
        case_started = time.monotonic()
        returned = _decode(editor._axis_range_spacing_ui_case(case_id=case_id,context='imgui_python',directory=str(root/case_id)))
        assert time.monotonic()-case_started <= 15 and time.monotonic()-started <= 180
        del editor
        raw = _safe_bytes(root/case_id/'case.json')
        assert _decode(raw) == returned
        entries.append({'case_id':case_id,'context':'imgui_python','path':case_id+'/case.json',
                        'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()})
        total += sum(member[1] for member in _inventory(root/case_id))
        assert total <= MAX_TREE_BYTES
    assert time.monotonic()-started <= 180
    _whole_tree(root,cpp=False)
    yield root,_IndexedCaseRows(root,entries,'imgui_python')
    _whole_tree(root,cpp=False)
    assert hashlib.sha256(_safe_bytes(module,allow_hardlink=True)).hexdigest() == expected


@pytest.mark.parametrize('case_id,context',[(case,context) for case in CASE_IDS for context in CONTEXTS],
                         ids=[f'{case}-{context}' for case in CASE_IDS for context in CONTEXTS])
def test_installed_axis_range_spacing_ui(case_id, context, request):
    root, rows = request.getfixturevalue('cpp_ui_rows' if context == 'imgui_cpp' else 'python_ui_rows')
    row = rows[case_id]
    assert row['case_id'] == case_id and row['context'] == context
    assert_axis_range_spacing_observation(row,root/case_id)
