"""Independent readers for the fixed paired rank editor observations.

Only stdlib code decodes evidence; pytest supplies 24 original and two R13 entries. No
ImageJxx import, native call, processing oracle, launcher, or producer verdict
supplies an expectation. CPU cache/upload inputs are checked, not GPU or OS
input. IMAGEJXX_RANK_UI_CASE_ROOT must contain the 24 original and two R13 case directories.
"""

import base64
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import struct
import zlib

import pytest


CONTRACT = "p06f.rank-ui.001"
CONTEXTS = ("imgui_cpp", "imgui_python")
CASES = tuple(f"R{index:02}" for index in range(1, 13))
PARAMETERS = tuple((context, case) for context in CONTEXTS for case in CASES)
PARAMETER_IDS = tuple(context + "-" + case for context, case in PARAMETERS)
ORIGINAL_EXPECTED_COLLECTION_IDS = tuple("test_fixed_rank_pair[" + name + "]" for name in PARAMETER_IDS)
RANGE_PARAMETER_IDS = tuple(context + "-R13" for context in CONTEXTS)
RANGE_EXPECTED_COLLECTION_IDS = tuple("test_fixed_rank_range_pair[" + name + "]" for name in RANGE_PARAMETER_IDS)
EXPECTED_COLLECTION_IDS = ORIGINAL_EXPECTED_COLLECTION_IDS + RANGE_EXPECTED_COLLECTION_IDS
assert len(ORIGINAL_EXPECTED_COLLECTION_IDS) == 24 and len(RANGE_EXPECTED_COLLECTION_IDS) == 2
assert len(EXPECTED_COLLECTION_IDS) == len(set(EXPECTED_COLLECTION_IDS)) == 26
MAX_JSON = 16 * 1024 * 1024
SPIKE = ((0, 0, 0, 0, 0), (0, 0, 0, 0, 0), (0, 0, 80, 0, 0),
         (0, 0, 0, 0, 0), (0, 0, 0, 0, 0))
LOW = ((0, 0, 0, 0, 0),) * 5
MAXIMUM = ((0, 0, 0, 0, 0), (0, 0, 80, 0, 0), (0, 80, 80, 80, 0),
           (0, 0, 80, 0, 0), (0, 0, 0, 0, 0))
MEAN = ((0, 0, 0, 0, 0), (0, 0, 16, 0, 0), (0, 16, 16, 16, 0),
        (0, 0, 16, 0, 0), (0, 0, 0, 0, 0))
HOLED_MEAN = ((0, 0, 0, 0, 0), (0, 0, 16, 0, 0), (0, 16, 80, 16, 0),
              (0, 0, 16, 0, 0), (0, 0, 0, 0, 0))
DTYPE_CODES = {"uint8": "B", "uint16": "H", "float32": "f"}
SCIENTIFIC = {"R02", "R03", "R04", "R09", "R13"}
DTYPES = {case: ("float32" if case == "R03" else "uint16"
                if case in {"R02", "R04", "R06", "R09", "R10"} else "uint8")
          for case in CASES}
DTYPES["R13"] = "uint16"
CALIBRATION = {"spacing": [1, 1, 1], "origin": [0, 0, 0],
               "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "px",
               "time_unit": "s", "frame_interval": 1, "time_origin": 0}
SCI_CALIBRATION = {**CALIBRATION, "spacing": [.5, 1.25, 2.5],
                   "origin": [11, -7, 19], "unit": "um",
                   "frame_interval": .75, "time_origin": 2.25}
CHECKPOINTS = {
    "R01": ("minimum.preview", "minimum.applied.stack_prompt", "minimum.applied", "minimum.undo", "minimum.redo"),
    "R02": ("maximum.preview", "maximum.nonactive_channel", "maximum.returned", "maximum.applied"),
    "R03": ("median.preview", "median.linked", "median.away", "median.returned", "median.cancelled"),
    "R04": ("mean.target_ready", "mean.preview", "mean.fresh_navigation", "mean.shown", "mean.before_apply", "mean.applied"),
    "R05": ("stack.yes.stack_prompt", "stack.yes"),
    "R06": ("stack.no.stack_prompt", "stack.no"),
    "R07": ("stack.cancel_prompt", "stack.cancelled", "stack.reopened"),
    "R08": ("memory.minimum.accepted", "memory.maximum.accepted", "memory.minimum.reopened",
            "memory.minimum.boundary", "memory.maximum.reopened", "memory.maximum.boundary",
            "memory.mean.default", "memory.mean.boundary", "memory.median.boundary",
            "memory.median.invalid", "memory.median.partial",
            "memory.median.reopened", "memory.destination.draft", "memory.destination.reopened"),
    "R09": ("new.preview", "new.applied", "new.saved", "new.reopened", "new.reopened_closed"),
    "R10": ("replay.before", "replay.strict", "replay.repeat_refused", "session.reopened",
            "replay.reopened_refused", "replay.rebind_opened", "replay.rebind_values", "replay.rebound", "replay.malformed_refused"),
    "R11": ("supersession.rank_ready", "supersession.gaussian_open", "supersession.gaussian_ready",
            "supersession.rank_again", "busy.queued", "busy.disabled", "busy.jobs_open", "busy.cancelled"),
    "R12": ("unchanged.applied.stack_prompt", "unchanged.applied", "empty.initial", "empty.applied"),
    "R13": ("range.last_limits", "range.nonlanding_ready", "range.preview", "range.shown",
            "range.applied", "range.undo", "range.redo"),
}


def _integer(value, minimum=0):
    assert type(value) is int and value >= minimum
    return value


def _unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate JSON key: " + key
        result[key] = value
    return result


def _decode(raw):
    def constant(value):
        raise AssertionError("Nonfinite JSON token: " + value)
    result = json.loads(raw, object_pairs_hook=_unique, parse_constant=constant)
    pending = [(result, 0)]
    nodes = 0
    while pending:
        value, depth = pending.pop()
        nodes += 1
        assert depth <= 128 and nodes <= 2_000_000
        if type(value) is float:
            assert math.isfinite(value)
        elif type(value) is dict:
            pending.extend((child, depth + 1) for child in value.values())
        elif type(value) is list:
            pending.extend((child, depth + 1) for child in value)
    return result


def _read(path, limit=MAX_JSON):
    assert path.is_file() and not path.is_symlink()
    size = path.stat().st_size
    assert 0 < size <= limit
    with path.open("rb") as stream:
        raw = stream.read(limit + 1)
    assert len(raw) == size <= limit
    return raw


@pytest.fixture(scope="module")
def evidence():
    configured = os.environ.get("IMAGEJXX_RANK_UI_CASE_ROOT")
    assert configured, "All 24 original observations require IMAGEJXX_RANK_UI_CASE_ROOT"
    root = Path(configured).resolve(strict=True)
    assert root.is_dir()
    reports = {}
    for context, case in PARAMETERS:
        directory = root / context / case / "run"
        assert directory.resolve(strict=True).is_relative_to(root)
        assert not directory.is_symlink()
        index = _decode(_read(directory / "case-index.json", 64 * 1024))
        assert index["schema_version"] == 1 and type(index["schema_version"]) is int
        assert index["contract"] == CONTRACT and index["context"] == context
        assert type(index["case_count"]) is int and index["case_count"] == 1
        assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
        assert type(index["cases"]) is list and len(index["cases"]) == 1
        descriptor = index["cases"][0]
        assert set(descriptor) == {"id", "member", "byte_count", "sha256"}
        assert descriptor["id"] == case and descriptor["member"] == "case.json"
        raw = _read(directory / "case.json")
        assert _integer(descriptor["byte_count"], 1) == len(raw)
        assert re.fullmatch("[0-9a-f]{64}", descriptor["sha256"])
        assert hashlib.sha256(raw).hexdigest() == descriptor["sha256"]
        row = _decode(raw)
        assert row["contract"] == CONTRACT and row["case_id"] == case and row["context"] == context
        assert type(row["schema_version"]) is int and row["schema_version"] == 1
        assert row["observation_state"] == "observed" and row["acceptance_claim"] is False
        assert "error" not in row and "cleanup_error" not in row
        reports[context, case] = (row, directory)
    assert len(reports) == len(ORIGINAL_EXPECTED_COLLECTION_IDS) == 24
    return reports


@pytest.fixture(scope="module")
def range_evidence():
    configured = os.environ.get("IMAGEJXX_RANK_UI_CASE_ROOT")
    assert configured, "Both R13 observations require IMAGEJXX_RANK_UI_CASE_ROOT"
    root = Path(configured).resolve(strict=True)
    assert root.is_dir()
    reports = {}
    for context in CONTEXTS:
        directory = root / context / "R13" / "run"
        assert directory.resolve(strict=True).is_relative_to(root)
        assert not directory.is_symlink()
        index = _decode(_read(directory / "case-index.json", 64 * 1024))
        assert index["schema_version"] == 1 and type(index["schema_version"]) is int
        assert index["contract"] == CONTRACT and index["context"] == context
        assert type(index["case_count"]) is int and index["case_count"] == 1
        assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
        assert type(index["cases"]) is list and len(index["cases"]) == 1
        descriptor = index["cases"][0]
        assert set(descriptor) == {"id", "member", "byte_count", "sha256"}
        assert descriptor["id"] == "R13" and descriptor["member"] == "case.json"
        raw = _read(directory / "case.json")
        assert _integer(descriptor["byte_count"], 1) == len(raw)
        assert re.fullmatch("[0-9a-f]{64}", descriptor["sha256"])
        assert hashlib.sha256(raw).hexdigest() == descriptor["sha256"]
        row = _decode(raw)
        assert row["contract"] == CONTRACT and row["case_id"] == "R13" and row["context"] == context
        assert type(row["schema_version"]) is int and row["schema_version"] == 1
        assert row["observation_state"] == "observed" and row["acceptance_claim"] is False
        assert "error" not in row and "cleanup_error" not in row
        reports[context] = (row, directory)
    assert len(reports) == len(RANGE_EXPECTED_COLLECTION_IDS) == 2
    return reports


def _flat(matrix, position=(0, 0, 0)):
    t, c, z = position
    return [sample + 10*t + 20*c + 30*z for line in matrix for sample in line]


def _packed(values, dtype):
    if dtype != "float32":
        maximum = 255 if dtype == "uint8" else 65535
        assert all(type(value) in {int, float} and math.isfinite(value) and
                   value == int(value) and 0 <= value <= maximum for value in values)
        values = [int(value) for value in values]
    return struct.pack("<" + DTYPE_CODES[dtype] * len(values), *values)


def _all_positions(shape):
    return tuple(itertools.product(range(shape[0]), range(shape[1]), range(shape[2])))


def _plane_values(document, method=None, changed=(), *, holed=False, constant=False):
    selected = set(changed)
    matrix = HOLED_MEAN if holed else {"minimum": LOW, "median": LOW,
                                      "maximum": MAXIMUM, "mean": MEAN}.get(method, SPIKE)
    return {position: ([7] * 25 if constant else _flat(matrix if position in selected else SPIKE, position))
            for position in _all_positions(document["shape"])}


def _assert_pixels(document, values):
    _integer(document["document_id"], 1)
    assert document["dtype"] in DTYPE_CODES and document["sample_policy"] == "finite_only"
    assert document["shape"][3:] == [5, 5, 1]
    assert len(document["planes"]) == len(values)
    observed = {}
    for plane in document["planes"]:
        position = tuple(plane["position"])
        assert position in values and position not in observed
        expected = values[position]
        assert bytes.fromhex(plane["declared_hex"]) == _packed(expected, document["dtype"])
        assert bytes.fromhex(plane["storage_f64_hex"]) == struct.pack("<25d", *expected)
        for name in ("image_address", "buffer_address"):
            assert re.fullmatch("0x[0-9a-f]+", plane[name]) and int(plane[name], 16) > 0
        observed[position] = plane
    assert set(observed) == set(values)


def _state(row, label):
    state = row["states"][label]
    frame = row["frames"][_integer(state["frame_ordinal"], 1) - 1]
    assert frame["ordinal"] == state["frame_ordinal"] and frame["label"] == label
    assert frame["requested_events"] == []
    assert [doc["document_id"] for doc in state["documents"]] == state["facade_ids"]
    assert sorted(state["document_ids"]) == sorted(state["facade_ids"])
    assert len(state["document_ids"]) == len(set(state["document_ids"]))
    assert state["active_document_id"] in state["document_ids"]
    return state


def _render(row, label):
    return row["frames"][_state(row, label)["frame_ordinal"] - 1]["rendered"]


def _doc(state, document_id=None):
    document_id = state["active_document_id"] if document_id is None else document_id
    matches = [document for document in state["documents"] if document["document_id"] == document_id]
    assert len(matches) == 1
    return matches[0]


def _source(row, label="initial"):
    return _doc(_state(row, label), row["source_document_id"])


def _annotations(before, after, *, new_document=False):
    for field in ("shape", "dtype", "sample_policy", "calibration", "selection", "previous_selection",
                  "rois", "overlays", "spectrum_present"):
        assert after[field] == before[field], field
    wanted = {**before["metadata"]}
    if new_document:
        wanted["title"] += " (rank_filter_imagej)"
    assert after["metadata"] == wanted


def _unchanged(before, after, *, display=False):
    _annotations(before, after)
    for field in ("document_id", "data_identity", "dirty", "history", "undo_steps", "redo_steps", "planes"):
        assert after[field] == before[field], field
    for field in ("pixels", "metadata", "selection"):
        assert after["revisions"][field] == before["revisions"][field]
    if display:
        assert after["display"] == before["display"] and after["revisions"] == before["revisions"]


def _camera(document):
    return {key: value for key, value in document["display"].items() if key != "revision"}


def _changed(before, after, method, positions, *, holed=False, previous_undo=0):
    _annotations(before, after)
    _assert_pixels(after, _plane_values(before, method, positions, holed=holed))
    assert after["document_id"] == before["document_id"]
    assert after["data_identity"] != before["data_identity"] and after["dirty"]
    assert after["undo_steps"] == previous_undo + 1 and after["redo_steps"] == 0
    assert after["history"]["step_limit"] == before["history"]["step_limit"]
    assert after["history"]["pixel_budget"] == before["history"]["pixel_budget"]
    assert after["revisions"]["pixels"] == before["revisions"]["pixels"] + 1
    assert after["revisions"]["metadata"] == before["revisions"]["metadata"]
    assert after["revisions"]["selection"] == before["revisions"]["selection"]


def _positions(target):
    if target["scope"] == "positions":
        assert target["ranges"] == []
        assert all(len(position) == 3 and all(type(value) is int and value >= 0 for value in position)
                   for position in target["positions"])
        return tuple(tuple(position) for position in target["positions"])
    assert target["scope"] == "ranges" and target["positions"] == []
    ranges = target["ranges"]
    assert len(ranges) == 3
    for start, stop, step in ranges:
        assert all(type(value) is int for value in (start, stop, step))
        assert 0 <= start < stop and step > 0
    return tuple(itertools.product(*(range(*axis) for axis in ranges)))


def _target(target, source, positions, *, roi="active_selection", destination="in_place", scope=None):
    assert set(target) == {"schema_version", "document_id", "source_revisions", "source_shape",
                           "scope", "ranges", "positions", "roi_policy", "execution_mode", "destination"}
    assert type(target["schema_version"]) is int and target["schema_version"] == 1
    _integer(target["document_id"], 1)
    assert target["document_id"] == source["document_id"] and target["source_shape"] == source["shape"]
    assert target["source_revisions"] == [source["revisions"][key] for key in ("pixels", "metadata", "selection")]
    assert target["roi_policy"] == roi and target["execution_mode"] == "planes"
    assert target["destination"] == destination
    assert _positions(target) == tuple(positions)
    if scope is not None:
        assert target["scope"] == scope


def _record(record, source, method, positions, *, radius=0, roi="active_selection", destination="in_place", scope=None):
    assert set(record) == {"schema_version", "operation", "profile", "options", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "rank_filter_imagej" and record["profile"] == "imagej_154r"
    assert type(record["options"]["radius"]) in {int, float} and math.isfinite(record["options"]["radius"])
    assert record["options"] == {"method": method, "radius": radius}
    _target(record["target"], source, positions, roi=roi, destination=destination, scope=scope)


def _item(rendered, selector):
    if selector.startswith("rank."):
        panel, name = rendered["rank_panel"], selector[5:]
    elif selector.startswith("gaussian."):
        panel, name = rendered["gaussian_panel"], selector[9:]
    elif selector.startswith("target."):
        panel, name = rendered["rank_panel"]["operation_target"], selector[7:]
    elif selector.startswith("dialog."):
        return rendered["dialog"]["controls"][selector[7:]]
    elif selector.startswith("error."):
        return rendered["error_modal"]["controls"][selector[6:]]
    elif selector.startswith("jobs.cancel."):
        suffix = selector[len("jobs.cancel."):]
        assert suffix.isdecimal() and _integer(int(suffix), 1)
        panel = rendered["jobs_panel"]
        assert panel["visible"]
        jobs = [job for job in panel["jobs"] if job["id"] == int(suffix)]
        assert len(jobs) == 1
        return jobs[0]["cancel"]
    else:
        return rendered["host_controls"][selector]
    return {**panel["control_states"][name], "point": panel["controls"][name],
            "rect": panel["control_rects"][name]}


def _controls(row, required):
    observed = {entry["selector"] for entry in row["control_observations"]}
    assert set(required) <= observed, set(required) - observed


def _common(row, case):
    assert set(row["states"]) == {"initial", "final", "terminal.cleaned", *CHECKPOINTS[case]}
    frames = row["frames"]
    assert _integer(row["frame_count"], 1) == len(frames) <= 180
    assert 0 <= row["elapsed_seconds"] <= 15
    assert row["limits"] == {"max_frames": 180, "max_seconds": 15,
                              "max_report_bytes": MAX_JSON, "max_archive_bytes": 2 * MAX_JSON, "max_documents": 16}
    lifecycle = row["lifecycle"]
    assert [entry["phase"] for entry in lifecycle] == ["opened", "closed"]
    opened, closed = (entry["state"] for entry in lifecycle)
    session = _integer(opened["session_id"], 1)
    context = _integer(opened["context_id"], 1)
    assert opened["open"] and opened["current_context_id"] == context
    assert closed["open"] is False and closed["session_id"] == session
    assert closed["context_id"] == closed["current_context_id"] == 0
    previous_frame, previous_time = opened["imgui_frame"], opened["imgui_time"]
    for ordinal, frame in enumerate(frames, 1):
        assert frame["ordinal"] == ordinal and isinstance(frame["label"], str)
        rendered = frame["rendered"]
        assert not rendered["window_error"] and not frame["native_state"]["window_error"]
        assert rendered["error_modal"].get("open", False) is False
        current = rendered["session"]
        assert current["open"] and current["session_id"] == session
        assert current["context_id"] == current["current_context_id"] == context
        assert current["imgui_frame"] == previous_frame + 1 and current["imgui_time"] > previous_time
        previous_frame, previous_time = current["imgui_frame"], current["imgui_time"]
        events = frame["requested_events"]
        assert type(events) is list and len(events) <= 120
        assert rendered["input_frame"]["raw_inputs"] == events
        assert all(event["type"] in {"position", "button", "key", "keys", "text", "wheel"} for event in events)
        assert len(frame["native_state"]["documents"]) <= 16
    assert closed["imgui_frame"] == previous_frame and closed["imgui_time"] == previous_time
    for label in row["states"]:
        _state(row, label)
    assert row["final_usage"] == dict.fromkeys(("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"), 0)
    terminal = _state(row, "terminal.cleaned")
    assert terminal["usage"] == row["final_usage"] and not terminal["busy"]
    assert terminal["results"] == [] and terminal["recording"]
    assert not row["window_error"] and not terminal["window_error"]
    expected_publications = 2 if case == "R12" else 1 if case in {"R01", "R02", "R04", "R05", "R06", "R09", "R10", "R13"} else 0
    assert len(row["publication_observations"]) == expected_publications
    refusals = {"strict_repeat_refusal", "reopened_strict_refusal", "malformed_original_rebind_refusal"} if case == "R10" else set()
    assert {entry["label"] for entry in row["api_observations"] if entry["accepted"] is False} == refusals
    assert all(type(entry["accepted"]) is bool for entry in row["api_observations"])

    for entry in row["control_observations"]:
        before = _integer(entry["before_frame_ordinal"], 1)
        selector, control, point = entry["selector"], entry["control"], entry["position"]
        prior = frames[before - 1]["rendered"]
        actual = _item(prior, selector)
        assert _integer(control["id"], 1) == actual["id"]
        assert type(entry["disabled_attempt"]) is bool
        assert control["enabled"] is actual["enabled"] is (not entry["disabled_attempt"])
        assert point == [math.floor(value) for value in actual["point"]]
        assert all(0 <= value < extent for value, extent in zip(point, (1250, 820)))
        if actual["rect"] is not None:
            x0, y0, x1, y1 = actual["rect"]
            assert x0 <= point[0] < x1 and y0 <= point[1] < y1
        position, down, up = frames[before:before + 3]
        assert position["requested_events"] == [{"type": "position", "space": "screen", "point": point}]
        assert down["requested_events"] == [{"type": "button", "name": "left", "down": True}]
        assert up["requested_events"] == [{"type": "button", "name": "left", "down": False}]
        assert down["rendered"]["input_frame"]["mouse_position"] == point
        assert down["rendered"]["input_frame"]["mouse_down"][0] is True
        assert up["rendered"]["input_frame"]["mouse_down"][0] is False
        actual_at_down = _item(position["rendered"], selector)
        assert actual_at_down["id"] == actual["id"] and actual_at_down["enabled"] is actual["enabled"]
        if entry["disabled_attempt"]:
            assert down["rendered"]["input_frame"]["focus"]["active_id"] != actual["id"]
        if selector.startswith("rank.method."):
            assert prior["rank_panel"]["method_popup"]
            assert up["rendered"]["rank_panel"]["method"] == selector.rsplit(".", 1)[1]
        if selector in {"rank.radius", "target.z.first", "target.z.last", "target.z.step", "target.preview.ordinal"}:
            input_frame = up["rendered"]["input_frame"]
            assert input_frame["focus"]["active_id"] == input_frame["input_text"]["id"] == actual["id"]

    initial = _state(row, "initial")
    assert initial["workflow"] == initial["results"] == [] and initial["recording"]
    assert initial["active_document_id"] == row["source_document_id"]
    for name, source in row["fixture_sources"].items():
        scientific = case in SCIENTIFIC if name == "source" else True
        assert source["shape"] == [2 if scientific else 1, 2 if scientific else 1, 3, 5, 5, 1]
        assert source["dtype"] == (DTYPES[case] if name == "source" else "uint8")
        _assert_pixels(source, _plane_values(source, constant=case == "R12" and name == "source"))
        assert source["calibration"] == (SCI_CALIBRATION if scientific else CALIBRATION)
        assert source["metadata"] == {"title": "Owned rank spike80 source", "properties": {
            "rank_source_marker": "retained", "rank_nested": [True, 7, "retained"]},
            "extensions": {}, "record_layout": {}}
        assert source["selection"]["kind"] == "rectangle" and source["selection"]["points"] == [[1, 1], [4, 4]]
        assert source["previous_selection"]["kind"] == "line" and source["previous_selection"]["points"] == [[0, 0], [1, 1]]
        assert source["overlays"] == [{"kind": "rectangle", "points": [[0, 0], [1, 1]],
                                       "t": 0, "c": 0, "z": 2, "slice_index": 2,
                                       "version": 37, "opaque": [3, 1, 4]}]
        assert source["undo_steps"] == 0 and source["redo_steps"] == 1 and not source["dirty"]
        _unchanged(source, _doc(initial, source["document_id"]))


def _readback(observation, values, shape, dtype, *, rgb=None):
    readback = observation["readback"]
    assert readback["available"]
    image = readback["source"]
    assert image["shape"] == [*shape, 1] and image["dtype"] == dtype
    assert image["pixels"] == values
    assert _packed(image["pixels"], dtype) == _packed(values, dtype)
    if rgb is None:
        rgb = [component for sample in values for component in (sample, sample, sample)]
    rendered = readback["rendered_rgb"]
    assert rendered["shape"] == [*shape, 3] and rendered["dtype"] == "uint8"
    assert _packed(rendered["pixels"], "uint8") == _packed(rgb, "uint8")


def _preview(row, label, method, position, *, roi="active_selection", full_positions=None, destination="in_place"):
    source = _source(row)
    state, rendered = _state(row, label), _render(row, label)
    _unchanged(source, _doc(state, source["document_id"]))
    assert state["workflow"] == [] and state["results"] == []
    panel, lanes = rendered["rank_panel"], rendered["gaussian_jobs"]
    assert panel["open"] and panel["draft_valid"] and panel["preview"] and panel["preview_requested"]
    assert not panel["error"] and not panel["preview_error"]
    assert panel["method"] == method and panel["radius"] == 0 and panel["source_id"] == source["document_id"]
    generation = _integer(panel["generation"], 1)
    assert lanes["active_installed_generation"] == generation
    assert lanes["preview"]["job"]["state"] == "ready"
    assert lanes["resource_usage"]["retained_reserved_bytes"] > 0
    assert lanes["installed_preview_storage"] is not None
    assert panel["preview_position"] == list(position)
    _target(panel["preview_target"], source, (position,), roi=roi, scope="positions" if source["shape"][0] > 1 else "ranges")
    _record(lanes["preview"]["command_record"], source, method, (position,), roi=roi)
    _target(panel["target"], source, full_positions or (position,), roi=roi, destination=destination)
    return generation


def _ordinary(row, label, values, position, *, generation=0, source="live", rgb=None, canvas=None):
    rendered = _render(row, label)
    document = _doc(_state(row, label))
    pixels = rendered["ordinary_pixels"]
    assert pixels["source_document_id"] == document["document_id"]
    assert pixels["current_tcz"] == pixels["range_tcz"] == pixels["readback"]["source_tcz"] == list(position)
    assert pixels["range"] == [0, 255] and pixels["contrast_source"] == "live"
    assert pixels["pixels_source"] == source and pixels["preview_generation"] == generation
    _readback(pixels, values, [5, 5], document["dtype"], rgb=rgb)
    # This observes the independent canvas branch's actual local decision.
    selected = rendered["ordinary_canvas"]
    expected_canvas = source if canvas is None else canvas
    assert selected["frame_source"] == expected_canvas
    assert selected["source_document_id"] == document["document_id"] and selected["current_tcz"] == list(position)
    assert selected["preview_generation"] == (generation if expected_canvas == "preview" else 0)


def _linked(row, label, plane_values, position, *, generation=0):
    rendered = _render(row, label)
    document = _doc(_state(row, label))
    t, c, z = position
    x, y, _ = document["display"]["crosshair"]
    planes = {entry["plane"]: entry for entry in rendered["orthogonal_planes"]}
    assert set(planes) == {"XY", "XZ", "YZ"}
    expected = {"XY": (plane_values[t, c, z], [5, 5]),
                "XZ": ([plane_values[t, c, plane_z][y*5 + px] for plane_z in range(3) for px in range(5)], [3, 5]),
                "YZ": ([plane_values[t, c, plane_z][py*5 + x] for plane_z in range(3) for py in range(5)], [3, 5])}
    for name, (values, shape) in expected.items():
        plane = planes[name]
        assert plane["source_document_id"] == plane["range_source_document_id"] == document["document_id"]
        # This cache key includes preview identity although the explicit range
        # itself is still the literal live [0,255] range.
        assert plane["preview_generation"] == plane["range_preview_generation"] == generation
        assert plane["pixels_source"] == ("transient" if generation else "live_document")
        assert plane["range"] == [0, 255] and plane["range_policy"] == "live_document"
        assert plane["range_tcz"] == list(position)
        _readback(plane, values, shape, document["dtype"])


def _publication(row, label, source, method, positions, *, destination="in_place", roi="active_selection"):
    entries = [entry for entry in row["publication_observations"] if entry["label"] == label]
    assert len(entries) == 1
    entry = entries[0]
    assert entry["trigger"] == "physical"
    confirmation = row["frames"][_integer(entry["confirm_frame_ordinal"], 1) - 1]
    assert confirmation["requested_events"] == [{"type": "button", "name": "left", "down": False}]
    assert confirmation["label"] in {"rank.ok.up", "rank.stack_no.up", "rank.stack_yes.up"}
    lane = entry["apply_lane"]
    _record(lane["command_record"], source, method, positions, roi=roi, destination=destination)
    assert lane["job"]["state"] == "completed" and not lane["error"]
    target_id = lane["published_document_id"]
    assert target_id == _state(row, label)["active_document_id"]
    assert entry["state_after"]["workflow"] == _state(row, label)["workflow"]
    assert _state(row, label)["workflow"][-1] == lane["command_record"]
    return lane["command_record"]


def _cancelled(row, label, source=None):
    source = _source(row) if source is None else source
    state, rendered = _state(row, label), _render(row, label)
    _unchanged(source, _doc(state, source["document_id"]))
    assert state["workflow"] == [] and not state["busy"]
    assert not rendered["rank_panel"]["open"]
    assert rendered["gaussian_jobs"]["active_installed_generation"] is None


def _api(row, label, *, accepted):
    entries = [entry for entry in row["api_observations"] if entry["label"] == label]
    assert len(entries) == 1
    entry = entries[0]
    assert entry["accepted"] is accepted
    if not accepted:
        assert entry["error"] and entry["before"] == entry["after"]
    return entry


def _source_preserved_states(row, labels):
    original = _source(row)
    for label in labels:
        _unchanged(original, _source(row, label))
        assert _state(row, label)["workflow"] == []


def _r01(row, directory):
    source, position = _source(row), (0, 0, 1)
    _controls(row, ("rank.method", "rank.method.minimum", "rank.radius", "rank.preview",
                    "rank.ok", "rank.stack_no", "menu.Edit.Undo", "menu.Edit.Redo"))
    generation = _preview(row, "minimum.preview", "minimum", position)
    _ordinary(row, "minimum.preview", _flat(LOW, position), position,
              generation=generation, source="preview")
    prompt = _render(row, "minimum.applied.stack_prompt")["rank_panel"]
    assert prompt["stack_prompt"] and prompt["remembered_radii"]["minimum"] == 0
    applied = _source(row, "minimum.applied")
    _changed(source, applied, "minimum", (position,))
    record = _publication(row, "minimum.applied", source, "minimum", (position,))
    assert _state(row, "minimum.applied")["workflow"] == [record]
    _ordinary(row, "minimum.applied", _flat(LOW, position), position)
    undone, redone = _source(row, "minimum.undo"), _source(row, "minimum.redo")
    _annotations(source, undone)
    _assert_pixels(undone, _plane_values(source))
    assert undone["data_identity"] == source["data_identity"] and not undone["dirty"]
    assert undone["undo_steps"] == 0 and undone["redo_steps"] == 1
    _annotations(source, redone)
    _assert_pixels(redone, _plane_values(source, "minimum", (position,)))
    assert redone["data_identity"] == applied["data_identity"] and redone["dirty"]
    assert redone["undo_steps"] == 1 and redone["redo_steps"] == 0
    assert _state(row, "minimum.undo")["workflow"] == _state(row, "minimum.redo")["workflow"] == [record]
    assert _camera(undone) == _camera(redone) == _camera(applied)


def _r02(row, directory):
    source, position = _source(row), (1, 1, 1)
    _controls(row, ("rank.method.maximum", "rank.radius", "rank.preview", "rank.ok"))
    generation = _preview(row, "maximum.preview", "maximum", position)
    red, green = _flat(SPIKE, (1, 0, 1)), _flat(MAXIMUM, position)
    rgb = [component for r, g in zip(red, green) for component in (r, g, 0)]
    for label, current_c in (("maximum.preview", 1), ("maximum.nonactive_channel", 0), ("maximum.returned", 1)):
        current = (1, current_c, 1)
        raw = green if current_c == 1 else red
        # The summary describes the active raw channel. C0 remains live while
        # the actual composite RGB still contains C1's preview contribution.
        _ordinary(row, label, raw, current, generation=generation if current_c == 1 else 0,
                  source="preview" if current_c == 1 else "live", rgb=rgb,
                  canvas="preview" if current_c == 1 else "live")
        rendered = _render(row, label)
        contributors = rendered["ordinary_pixels"]["channel_display"]["contributors"]
        assert [(entry["c"], entry["preview"], entry["generation"], entry["preview_z"])
                for entry in contributors] == [(0, False, 0, []), (1, True, generation, [1])]
        assert all(entry["range"] == [0, 255] for entry in contributors)
        assert rendered["ordinary_pixels"]["channel_display"]["state"]["mode"] == "composite"
        _unchanged(source, _source(row, label))
    applied = _source(row, "maximum.applied")
    _changed(source, applied, "maximum", (position,))
    assert (applied["display"]["t"], applied["display"]["c"], applied["display"]["crosshair"]) == (1, 0, [2, 2, 1])
    record = _publication(row, "maximum.applied", source, "maximum", (position,))
    assert _state(row, "maximum.applied")["workflow"] == [record]
    _ordinary(row, "maximum.applied", red, (1, 0, 1), rgb=rgb)


def _r03(row, directory):
    source, position = _source(row), (1, 1, 1)
    _controls(row, ("rank.method.median", "rank.radius", "rank.preview", "rank.cancel"))
    generation = _preview(row, "median.preview", "median", position)
    _ordinary(row, "median.preview", _flat(LOW, position), position,
              generation=generation, source="preview")
    transient = _plane_values(source, "median", (position,))
    _linked(row, "median.linked", transient, position, generation=generation)
    _linked(row, "median.away", _plane_values(source), (1, 1, 0))
    _linked(row, "median.returned", transient, position, generation=generation)
    for label in ("median.linked", "median.away", "median.returned"):
        assert _render(row, label)["gaussian_jobs"]["active_installed_generation"] == generation
        _unchanged(source, _source(row, label))
    _cancelled(row, "median.cancelled")
    _linked(row, "median.cancelled", _plane_values(source), position)
    assert row["publication_observations"] == []


def _r04(row, directory):
    source, preview_position = _source(row), (1, 1, 2)
    target = tuple(itertools.product(range(2), range(2), (0, 2)))
    modified = ((1, 0, 2), (1, 1, 2))
    _controls(row, ("rank.method.mean", "rank.radius", "target.t.all", "target.c.all", "target.z.range",
                    "target.z.first", "target.z.last", "target.z.step", "target.roi.positioned_rois",
                    "target.preview.ordinal", "target.preview.show", "rank.preview", "rank.ok"))
    roi, = source["rois"]
    assert (roi["name"], roi["t"], roi["c"], roi["z"], roi["attributes"]) == ("positioned ring", 1, None, 2, {"retained": True})
    assert roi["selection"]["kind"] == "mask" and roi["selection"]["shape"] == [5, 5]
    assert base64.b64decode(roi["selection"]["mask"], validate=True) == bytes.fromhex("0394e000")
    _target(_render(row, "mean.target_ready")["rank_panel"]["target"], source, target, roi="positioned_rois")
    assert row["preview_selection"] == {"resolved_positions": [list(position) for position in target],
                                         "requested_position": [1, 1, 2], "selected_zero_based_ordinal": 7}
    ordinal_events = [event for frame in row["frames"] if frame["label"] == "mean.preview.ordinal.text"
                      for event in frame["requested_events"]]
    assert ordinal_events == [{"type": "text", "value": "8"}]
    generation = _preview(row, "mean.preview", "mean", preview_position,
                           roi="positioned_rois", full_positions=target)
    _ordinary(row, "mean.preview", _flat(SPIKE, (1, 1, 1)), (1, 1, 1))
    _ordinary(row, "mean.fresh_navigation", _flat(SPIKE, (0, 0, 1)), (0, 0, 1))
    _ordinary(row, "mean.shown", _flat(HOLED_MEAN, preview_position), preview_position,
              generation=generation, source="preview")
    _ordinary(row, "mean.before_apply", _flat(SPIKE, (0, 0, 1)), (0, 0, 1))
    for label in ("mean.fresh_navigation", "mean.shown", "mean.before_apply"):
        _unchanged(source, _source(row, label))
        panel = _render(row, label)["rank_panel"]
        _target(panel["target"], source, target, roi="positioned_rois")
        _target(panel["preview_target"], source, (preview_position,), roi="positioned_rois", scope="positions")
    applied = _source(row, "mean.applied")
    _changed(source, applied, "mean", modified, holed=True)
    assert _camera(applied) == _camera(_source(row, "mean.before_apply"))
    record = _publication(row, "mean.applied", source, "mean", target, roi="positioned_rois")
    assert _state(row, "mean.applied")["workflow"] == [record]


def _r05_r06(row, directory):
    source, yes = _source(row), row["case_id"] == "R05"
    label = "stack.yes" if yes else "stack.no"
    positions = ((0, 0, 0), (0, 0, 1), (0, 0, 2)) if yes else ((0, 0, 1),)
    _controls(row, ("rank.method.maximum", "rank.radius", "rank.ok", "rank.stack_yes" if yes else "rank.stack_no"))
    prompt = _render(row, label + ".stack_prompt")["rank_panel"]
    assert prompt["open"] and prompt["stack_prompt"] and not prompt["preview"]
    assert prompt["method"] == "maximum" and prompt["remembered_radii"]["maximum"] == 0
    _unchanged(source, _source(row, label + ".stack_prompt"))
    applied = _source(row, label)
    _changed(source, applied, "maximum", positions)
    assert _camera(applied) == _camera(source)
    record = _publication(row, label, source, "maximum", positions)
    assert _state(row, label)["workflow"] == [record]


def _r07(row, directory):
    _controls(row, ("rank.method.minimum", "rank.radius", "rank.ok", "rank.stack_cancel", "rank.cancel"))
    _source_preserved_states(row, CHECKPOINTS["R07"])
    prompt = _render(row, "stack.cancel_prompt")["rank_panel"]
    assert prompt["stack_prompt"] and prompt["remembered_radii"] == {
        "minimum": 2.25, "maximum": 2, "median": 2, "mean": 2}
    _cancelled(row, "stack.cancelled")
    reopened = _render(row, "stack.reopened")["rank_panel"]
    assert reopened["open"] and reopened["method"] == "minimum" and reopened["radius"] == 2.25
    assert reopened["remembered_radii"] == prompt["remembered_radii"] and not reopened["new_document"]
    assert row["publication_observations"] == []


def _r08(row, directory):
    _controls(row, ("rank.method.minimum", "rank.method.maximum", "rank.method.mean", "rank.method.median",
                    "rank.radius", "rank.ok", "rank.stack_cancel", "rank.new_document", "rank.cancel"))
    _source_preserved_states(row, CHECKPOINTS["R08"])
    minimum = _render(row, "memory.minimum.accepted")["rank_panel"]
    assert minimum["remembered_radii"] == {"minimum": 2.25, "maximum": 2, "median": 2, "mean": 2}
    remembered = {"minimum": 2.25, "maximum": 3.5, "median": 2, "mean": 2}
    assert _render(row, "memory.maximum.accepted")["rank_panel"]["remembered_radii"] == remembered
    for label, method, radius in (("memory.minimum.reopened", "minimum", 2.25),
                                  ("memory.maximum.reopened", "maximum", 3.5),
                                  ("memory.mean.default", "mean", 2),
                                  ("memory.median.reopened", "median", 2),
                                  ("memory.destination.reopened", "median", 2)):
        panel = _render(row, label)["rank_panel"]
        assert panel["open"] and panel["draft_valid"] and not panel["error"]
        assert panel["method"] == method and panel["radius"] == radius
        assert panel["remembered_radii"] == remembered
    for method, radius in (("minimum", 1000), ("maximum", 1000), ("mean", 1000), ("median", 100)):
        label = "memory." + method + ".boundary"
        panel = _render(row, label)["rank_panel"]
        assert panel["open"] and panel["draft_valid"] and not panel["error"]
        assert panel["method"] == method and panel["radius"] == radius and panel["radius_text"] == str(radius)
        assert panel["control_states"]["ok"]["enabled"] and panel["control_states"]["preview"]["enabled"]
        assert panel["remembered_radii"] == remembered
        assert not panel["preview"] and not panel["preview_requested"] and not panel["apply_busy"]
        assert _state(row, label)["jobs"] == [] and not _state(row, label)["busy"]
    for label, text in (("memory.median.invalid", "101"), ("memory.median.partial", "-")):
        panel = _render(row, label)["rank_panel"]
        assert panel["open"] and not panel["draft_valid"] and panel["error"]
        assert panel["radius_text"] == text and panel["remembered_radii"] == remembered
        assert not panel["control_states"]["ok"]["enabled"] and not panel["control_states"]["preview"]["enabled"]
    assert any(entry["selector"] == "rank.ok" and entry["disabled_attempt"] for entry in row["control_observations"])
    escape_presses = [event for frame in row["frames"] for event in frame["requested_events"]
                      if event == {"type": "key", "name": "Escape", "down": True}]
    assert len(escape_presses) == 2
    draft = _render(row, "memory.destination.draft")["rank_panel"]
    assert draft["new_document"] and draft["target"]["destination"] == "new_document"
    assert draft["preview_target"]["destination"] == "in_place" and not draft["preview"]
    assert not _render(row, "memory.destination.reopened")["rank_panel"]["new_document"]
    assert row["publication_observations"] == []


def _r09(row, directory):
    source, position = _source(row), (1, 1, 1)
    _controls(row, ("rank.method.median", "rank.radius", "rank.new_document", "rank.preview", "rank.ok"))
    generation = _preview(row, "new.preview", "median", position, destination="new_document")
    _ordinary(row, "new.preview", _flat(LOW, position), position, generation=generation, source="preview")
    applied = _state(row, "new.applied")
    assert len(applied["documents"]) == 2 and len(applied["document_ids"]) == len(applied["facade_ids"]) == 2
    _unchanged(source, _doc(applied, source["document_id"]))
    output = _doc(applied)
    assert output["document_id"] != source["document_id"]
    _annotations(source, output, new_document=True)
    _assert_pixels(output, _plane_values(source, "median", (position,)))
    assert output["dirty"] and output["undo_steps"] == 1 and output["redo_steps"] == 0
    assert output["history"]["step_limit"] == source["history"]["step_limit"]
    assert output["history"]["pixel_budget"] == source["history"]["pixel_budget"]
    assert _camera(output) == _camera(source)
    record = _publication(row, "new.applied", source, "median", (position,), destination="new_document")
    assert applied["workflow"] == [record]
    output_id = _integer(row["new_output_owner_id"], 1)
    assert output_id == output["document_id"]
    saved_state = _state(row, "new.saved")
    saved = _doc(saved_state, output_id)
    _unchanged({**output, "dirty": False}, saved, display=True)
    assert saved_state["document_ids"] == applied["document_ids"]
    assert saved_state["active_document_id"] == output_id and saved_state["workflow"] == [record]
    _native_session(row, directory, saved, [record],
                    file_key="new_document_session", member="rank-new-document.ijx")
    save = _api(row, "new.native_save", accepted=True)
    reopen = _api(row, "new.native_reopen", accepted=True)
    close = _api(row, "new.close_reopened", accepted=True)
    assert save["saved_owner_id"] == output_id
    assert save["path"] == reopen["path"] == "rank-new-document.ijx"
    for action in (save, reopen, close):
        assert action["role"] == "explicit API/action preparation or refusal; no widget-input credit"
    reopened_id = _integer(row["new_reopened_owner_id"], 1)
    assert reopened_id not in applied["document_ids"]
    assert reopen["reopened_owner_id"] == close["closed_owner_id"] == reopened_id
    reopened_state = _state(row, "new.reopened")
    assert len(reopened_state["document_ids"]) == 3 and reopened_state["active_document_id"] == reopened_id
    assert set(reopened_state["document_ids"]) == {*applied["document_ids"], reopened_id}
    reopened = _doc(reopened_state)
    _annotations(saved, reopened)
    _assert_pixels(reopened, _plane_values(source, "median", (position,)))
    assert reopened["data_identity"] != saved["data_identity"]
    assert not reopened["dirty"] and reopened["undo_steps"] == reopened["redo_steps"] == 0
    assert _camera(reopened) == _camera(saved) and reopened_state["workflow"] == [record]
    for label in ("new.saved", "new.reopened", "new.reopened_closed", "final", "terminal.cleaned"):
        state = _state(row, label)
        _unchanged(_source(row, "new.applied"), _source(row, label), display=True)
        _unchanged(saved, _doc(state, output_id), display=True)
        assert state["workflow"] == [record]
    closed_state = _state(row, "new.reopened_closed")
    assert set(closed_state["document_ids"]) == set(applied["document_ids"])
    assert closed_state["active_document_id"] == output_id


def _native_selection_projection(wire):
    # NativeV3 nests attributes; the CPU document diagnostic flattens them.
    # Retain every geometry/mask field and attribute in that declared projection.
    assert type(wire) is dict and type(wire["attributes"]) is dict
    fields = {"kind", "points", "width"}
    if wire["kind"] == "mask":
        fields |= {"shape", "mask"}
        base64.b64decode(wire["mask"], validate=True)
    assert set(wire) == fields | {"attributes"}
    return {**wire["attributes"], **{field: wire[field] for field in fields}}


def _native_session(row, directory, saved, workflow, *, file_key="native_session", member="rank-session.ijx"):
    file = row["files"][file_key]
    assert set(file) == {"member", "byte_count", "sha256", "hex"}
    assert file["member"] == member
    raw = _read(directory / file["member"])
    assert len(raw) == _integer(file["byte_count"], 1)
    assert hashlib.sha256(raw).hexdigest() == file["sha256"] and bytes.fromhex(file["hex"]) == raw
    assert raw[:10] == b"IMAGEJXX3\n" and len(raw) > 18
    declared, = struct.unpack("<Q", raw[10:18])
    assert 0 < declared <= 8 * 1024 * 1024
    inflater = zlib.decompressobj()
    decoded = inflater.decompress(raw[18:], declared + 1)
    assert inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail
    assert len(decoded) == declared
    capture = _decode(decoded)
    assert capture["format"] == "imagejxx-session" and type(capture["version"]) is int and capture["version"] == 3
    assert capture["workflow"] == workflow and capture["results"] == []
    document, = capture["documents"]
    assert document["axes"] == "TCZYXS" and document["shape"] == saved["shape"]
    assert document["pixel_type"] == saved["dtype"] and document["sample_policy"] == "finite_only"
    assert document["storage"] == "Float64LE" and document["calibration"] == saved["calibration"]
    assert document["metadata"] == saved["metadata"] and document["fourier"] is None
    # V3 persists cameras/properties, not the process-local display revision.
    assert document["display"] == _camera(saved)
    assert len(document["planes"]) == len(saved["planes"])
    for plane, original in zip(document["planes"], saved["planes"]):
        assert base64.b64decode(plane, validate=True) == bytes.fromhex(original["storage_f64_hex"])
    assert document["selection"]["kind"] == "rectangle" and document["selection"]["points"] == [[1, 1], [4, 4]]
    assert document["previous_selection"]["kind"] == "line"
    assert _native_selection_projection(document["selection"]) == saved["selection"]
    assert _native_selection_projection(document["previous_selection"]) == saved["previous_selection"]
    assert [{**roi, "selection": _native_selection_projection(roi["selection"])}
            for roi in document["rois"]] == saved["rois"]
    assert document["overlays"] == saved["overlays"]


def _r10(row, directory):
    source, position = _source(row), (0, 0, 1)
    assert source["shape"] == [1, 1, 3, 5, 5, 1]
    assert row["original_owner_id"] == source["document_id"]
    original = row["original_record"]
    _record(original, source, "median", (position,))
    _unchanged(source, _source(row, "replay.before"))
    assert _source(row, "replay.before")["display"]["crosshair"] == [2, 2, 0]
    strict = _source(row, "replay.strict")
    _changed(source, strict, "median", (position,))
    assert strict["display"]["crosshair"] == [2, 2, 0]
    assert _state(row, "replay.strict")["workflow"] == [original]
    _api(row, "strict_replay", accepted=True)
    _api(row, "strict_repeat_refusal", accepted=False)
    _unchanged(strict, _source(row, "replay.repeat_refused"), display=True)
    _native_session(row, directory, _source(row, "replay.repeat_refused"), [original])
    reopened_id = _integer(row["reopened_owner_id"], 1)
    assert reopened_id != source["document_id"]
    reopened_state = _state(row, "session.reopened")
    assert len(reopened_state["documents"]) == 2 and reopened_state["active_document_id"] == reopened_id
    reopened = _doc(reopened_state)
    source_saved = _source(row, "session.reopened")
    # Successful native save changes only the source's saved/dirty marker.
    _unchanged({**strict, "dirty": False}, source_saved, display=True)
    _annotations(strict, reopened)
    _assert_pixels(reopened, _plane_values(source, "median", (position,)))
    assert not reopened["dirty"] and reopened["undo_steps"] == reopened["redo_steps"] == 0
    assert _camera(reopened) == _camera(strict) and reopened_state["workflow"] == [original]
    _api(row, "reopened_strict_refusal", accepted=False)
    _unchanged(reopened, _doc(_state(row, "replay.reopened_refused")), display=True)
    _controls(row, ("workflow.run", "dialog.processing_scope", "dialog.processing_scope.choice.All Z slices",
                    "dialog.region", "dialog.region.choice.Whole plane", "dialog.apply"))
    form = _render(row, "replay.rebind_values")["dialog"]
    assert form["open"] and form["values"] == {"processing_scope": "All Z slices", "region": "Whole plane"}
    rebound = _doc(_state(row, "replay.rebound"))
    all_z = ((0, 0, 0), (0, 0, 1), (0, 0, 2))
    _changed(reopened, rebound, "median", all_z)
    assert _state(row, "replay.rebound")["workflow"] == [original]
    assert _camera(rebound) == _camera(reopened)
    lineage = _render(row, "replay.rebound")["workflow_replay"]
    assert not lineage["observation_failed"] and lineage["publication_succeeded"]
    assert lineage["live_destination_id"] == lineage["final_publication_document_id"] == reopened_id
    assert lineage["original_steps"] == [original]
    assert lineage["explicit_request"] == {"scope": "all_z", "roi": "whole_plane", "mode": "planes", "destination": "in_place"}
    events = lineage["events"]
    assert [event["phase"] for event in events] == ["working_copy_created", "command_resolved", "scratch_published"]
    working = _integer(events[0]["working_copy_id"], 1)
    captured = _integer(events[0]["captured_snapshot_owner_id"], 1)
    # Window first captures its detached worker facade; runWorkflow then owns
    # a separate scratch facade. Neither is an attached destination owner.
    assert len({source["document_id"], reopened_id, captured, working}) == 4
    for sequence, event in enumerate(events, 1):
        assert event["sequence"] == sequence and event["live_destination_id"] == reopened_id
        assert event["captured_snapshot_owner_id"] == captured and event["working_copy_id"] == working
    resolved = events[1]
    assert resolved["original_record"] == original and resolved["original_record_source_id"] == source["document_id"]
    assert resolved["resolved_command_parameters"] == {"method": "median", "radius": 0}
    assert resolved["resolved_command_parameter_shapes"] == {}
    scratch_source = {**reopened, "document_id": working,
                      "revisions": {"pixels": 0, "metadata": 0, "selection": 0}}
    _record(resolved["resolved_command_record"], scratch_source, "median", all_z, roi="whole_plane")
    assert events[2]["scratch_publication_document_id"] == working
    assert lineage["terminal_job"]["state"] == "completed" and lineage["terminal_job"]["id"] == lineage["job_id"]
    publication, = row["publication_observations"]
    assert publication["label"] == "replay.rebound" and publication["trigger"] == "physical"
    confirm = row["frames"][publication["confirm_frame_ordinal"] - 1]
    assert confirm["label"] == "dialog.apply.up" and publication["workflow_replay"] == lineage
    _api(row, "malformed_original_rebind_refusal", accepted=False)
    _unchanged(rebound, _doc(_state(row, "replay.malformed_refused")), display=True)
    for label in ("session.reopened", "replay.reopened_refused", "replay.rebound", "replay.malformed_refused"):
        _unchanged(source_saved, _source(row, label), display=True)


def _r11(row, directory):
    source, position = _source(row), (0, 0, 1)
    first = _preview(row, "supersession.rank_ready", "minimum", position)
    _ordinary(row, "supersession.rank_ready", _flat(LOW, position), position,
              generation=first, source="preview")
    gaussian_open = _render(row, "supersession.gaussian_open")
    assert not gaussian_open["rank_panel"]["open"] and gaussian_open["gaussian_panel"]["open"]
    assert gaussian_open["gaussian_jobs"]["active_installed_generation"] is None
    gaussian_ready = _render(row, "supersession.gaussian_ready")
    gaussian_lane = gaussian_ready["gaussian_jobs"]
    assert gaussian_lane["preview"]["command_record"]["operation"] == "gaussian_blur_imagej"
    assert gaussian_lane["preview"]["job"]["state"] == "ready"
    assert gaussian_lane["active_installed_generation"] == gaussian_ready["gaussian_panel"]["generation"]
    second = _preview(row, "supersession.rank_again", "minimum", position)
    assert second > first and not _render(row, "supersession.rank_again")["gaussian_panel"]["open"]
    job_ids = [_render(row, label)["gaussian_jobs"]["preview"]["job"]["id"]
               for label in ("supersession.rank_ready", "supersession.gaussian_ready", "supersession.rank_again")]
    assert len(set(job_ids)) == 3
    queued = _state(row, "busy.queued")
    lane = _render(row, "busy.queued")["gaussian_jobs"]["apply"]
    job_id = _integer(row["queued_apply_job_id"], 1)
    assert lane["job"]["id"] == job_id and lane["job"]["state"] == "queued"
    _record(lane["command_record"], source, "minimum", (position,))
    assert queued["busy"] and queued["usage"]["running"] == 1 and queued["usage"]["queued"] >= 1
    assert _render(row, "busy.queued")["gaussian_jobs"]["active_installed_generation"] is None
    _controls(row, ("menu.command.gaussian_blur_imagej", "menu.command.rank_filter_imagej", "gaussian.preview",
                    "rank.stack_no", "menu.Process", "jobs.cancel." + str(job_id)))
    assert any(entry["selector"] == "menu.Process" and entry["disabled_attempt"] for entry in row["control_observations"])
    assert _render(row, "busy.disabled")["gaussian_jobs"]["apply"]["job"]["id"] == job_id
    visible_jobs = _render(row, "busy.jobs_open")["jobs_panel"]
    assert visible_jobs["visible"] and job_id in visible_jobs["job_ids"]
    cancel_rows = [job for job in visible_jobs["jobs"] if job["id"] == job_id]
    assert len(cancel_rows) == 1 and cancel_rows[0]["state"] == "Queued"
    assert cancel_rows[0]["cancel"]["enabled"] and _integer(cancel_rows[0]["cancel"]["id"], 1)
    prepared = _api(row, "busy_worker_preparation", accepted=True)
    released = _api(row, "busy_worker_release", accepted=True)
    blocker_before, blocker_after = prepared["blocker_job"], released["blocker_job"]
    blocker_id = _integer(blocker_before["id"], 1)
    assert blocker_after["id"] == blocker_id and blocker_id not in (*job_ids, job_id)
    # A generic void scheduler task becomes READY when its worker returns.
    # This blocker has no publication; COMPLETED belongs to Job.complete().
    assert blocker_before["state_code"] == 1 and blocker_after["state_code"] == 2
    for blocker in (blocker_before, blocker_after):
        assert blocker["document_id"] == 0 and blocker["label"] == "owned rank UI blocker"
        assert blocker["error"] == "" and blocker["cancel_requested"] is False
        assert blocker["resource_bytes"] == 1 and blocker["total"] == 1
    assert blocker_before["completed"] == 0 and blocker_after["completed"] == 1
    cancelled = _render(row, "busy.cancelled")["gaussian_jobs"]["apply"]["job"]
    assert cancelled["id"] == job_id and cancelled["state"] == "canceled" and cancelled["cancel_requested"]
    _source_preserved_states(row, CHECKPOINTS["R11"])
    assert row["publication_observations"] == []


def _r12(row, directory):
    source = _source(row)
    _controls(row, ("rank.method.minimum", "rank.radius", "rank.ok", "rank.stack_no", "target.roi.positioned_rois"))
    _unchanged(source, _source(row, "unchanged.applied"))
    assert _state(row, "unchanged.applied")["workflow"] == []
    empty = row["fixture_sources"]["empty"]
    for label in ("empty.initial", "empty.applied"):
        state = _state(row, label)
        assert state["active_document_id"] == empty["document_id"]
        _unchanged(empty, _doc(state))
        _unchanged(source, _source(row, label))
        assert state["workflow"] == []
    assert len(row["publication_observations"]) == 2
    for label, document, roi in (("unchanged.applied", source, "active_selection"),
                                  ("empty.applied", empty, "positioned_rois")):
        entries = [entry for entry in row["publication_observations"] if entry["label"] == label]
        entry, = entries
        assert entry["trigger"] == "physical"
        _record(entry["apply_lane"]["command_record"], document, "minimum",
                 ((1, 1, 1),) if roi == "positioned_rois" else ((0, 0, 1),), roi=roi)
        assert entry["apply_lane"]["job"]["state"] == "completed"
        assert entry["state_before"]["workflow"] == entry["state_after"]["workflow"] == []
    roi, = empty["rois"]
    assert (roi["t"], roi["c"], roi["z"]) == (1, None, 2)
    assert _doc(_state(row, "empty.applied"))["display"]["crosshair"][2] == 1


def _r13_drag(row):
    drag, = row["drag_observations"]
    fields = {"label", "kind", "performed", "move_id", "before", "after",
              "before_frame_ordinal", "after_frame_ordinal", "input_frame_ordinals",
              "source_before", "source_after"}
    assert type(drag["performed"]) is bool
    assert set(drag) == fields | ({"position", "target_position"} if drag["performed"] else set())
    assert drag["label"] == "range.panel_viewport" and drag["kind"] == "rank_title_bar"
    before_ordinal = _integer(drag["before_frame_ordinal"], 1)
    after_ordinal = _integer(drag["after_frame_ordinal"], 1)
    before_frame, after_frame = row["frames"][before_ordinal - 1], row["frames"][after_ordinal - 1]
    assert drag["before"] == before_frame["rendered"]["rank_panel"]["window_drag"]
    assert drag["after"] == after_frame["rendered"]["rank_panel"]["window_drag"]
    assert drag["source_before"] == before_frame["native_state"]
    assert drag["source_after"] == after_frame["native_state"] == drag["source_before"]
    before, after = drag["before"], drag["after"]
    assert _integer(drag["move_id"], 1) == before["id"] == after["id"]
    for geometry in (before, after):
        assert geometry["enabled"] is True and geometry["visible"] is True
        assert set(geometry) == {"id", "enabled", "visible", "point", "rect", "window_rect", "viewport_work_rect"}
        for field, length in (("point", 2), ("rect", 4), ("window_rect", 4), ("viewport_work_rect", 4)):
            assert len(geometry[field]) == length
            assert all(type(value) in {int, float} and math.isfinite(value) for value in geometry[field])
    work = before["viewport_work_rect"]
    assert after["viewport_work_rect"] == work

    def inside(rect):
        return (type(rect) is list and len(rect) == 4 and
                work[0] <= rect[0] < rect[2] <= work[2] and work[1] <= rect[1] < rect[3] <= work[3])

    before_actions = before_frame["rendered"]["rank_panel"]["control_rects"]
    after_actions = after_frame["rendered"]["rank_panel"]["control_rects"]
    assert all(inside(after_actions[name]) for name in ("preview", "ok", "cancel"))
    if not drag["performed"]:
        assert drag["input_frame_ordinals"] == {} and before_ordinal == after_ordinal
        assert before == after and all(inside(before_actions[name]) for name in ("preview", "ok", "cancel"))
        return
    assert not all(inside(before_actions[name]) for name in ("preview", "ok", "cancel"))
    order = ("position", "down", "move", "up", "settled")
    assert set(drag["input_frame_ordinals"]) == set(order)
    ordinals = [drag["input_frame_ordinals"][name] for name in order]
    assert ordinals == list(range(before_ordinal + 1, before_ordinal + 6)) and after_ordinal == ordinals[-1]
    x, y = (math.floor(value) for value in before["point"])
    delta = math.floor(work[1] + 8) - before["window_rect"][1]
    assert delta < 0 and drag["position"] == [x, y] and drag["target_position"] == [x, y + delta]
    assert inside(before["rect"]) and before["rect"][0] <= x < before["rect"][2]
    assert before["rect"][1] <= y < before["rect"][3]
    for field in ("rect", "window_rect"):
        for index in range(4):
            assert abs(after[field][index] - before[field][index] - (delta if index % 2 else 0)) <= 1
    events = (
        [{"type": "position", "space": "screen", "point": [x, y]}],
        [{"type": "button", "name": "left", "down": True}],
        [{"type": "position", "space": "screen", "point": [x, y + delta]}],
        [{"type": "button", "name": "left", "down": False}], [],
    )
    for name, ordinal, requested in zip(order, ordinals, events):
        frame = row["frames"][ordinal - 1]
        assert frame["label"] == "range.panel_viewport." + name and frame["requested_events"] == requested
        input_frame = frame["rendered"]["input_frame"]
        if name in {"down", "move"}:
            assert input_frame["mouse_down"][0] is True
            assert input_frame["focus"]["active_id"] == drag["move_id"]
        elif name in {"up", "settled"}:
            assert input_frame["mouse_down"][0] is False
    assert after_frame["rendered"]["input_frame"]["focus"]["active_id"] != drag["move_id"]


def _r13(row, directory):
    source = _source(row)
    assert source["shape"] == [2, 2, 3, 5, 5, 1] and source["dtype"] == "uint16"
    assert set(row["fixture_sources"]) == {"source"}
    assert [entry["label"] for entry in row["api_observations"]] == [
        "fixed_fixture_preparation", "rank.open_preparation"]
    assert all(entry["accepted"] is True for entry in row["api_observations"])
    radius_text, = [frame for frame in row["frames"] if frame["label"] == "rank.radius.text"]
    assert radius_text["requested_events"] == [{"type": "text", "value": "0"}]
    selected = ((0, 0, 0),)
    excluded = ((0, 0, 1), (0, 0, 2), (0, 1, 0), (0, 1, 1), (0, 1, 2),
                (1, 0, 0), (1, 0, 1), (1, 0, 2), (1, 1, 0), (1, 1, 1), (1, 1, 2))
    assert set(selected).isdisjoint(excluded) and len(selected) + len(excluded) == 12
    _controls(row, ("rank.method.minimum", "rank.radius", "rank.preview", "rank.ok",
                    "target.preview.show", "menu.Edit.Undo", "menu.Edit.Redo",
                    "target.t.range", "target.c.range", "target.z.range", "target.roi.whole_plane",
                    "target.t.first", "target.t.last", "target.t.step",
                    "target.c.first", "target.c.last", "target.c.step",
                    "target.z.first", "target.z.last", "target.z.step"))
    _r13_drag(row)

    limited = _render(row, "range.last_limits")["rank_panel"]
    _target(limited["target"], source, ((0, 0, 0), (0, 0, 1)), roi="whole_plane", scope="ranges")
    assert limited["target"]["ranges"] == [[0, 1, 1], [0, 1, 1], [0, 2, 1]]
    nonlanding = _render(row, "range.nonlanding_ready")["rank_panel"]
    _target(nonlanding["target"], source, selected, roi="whole_plane", scope="ranges")
    assert nonlanding["target"]["ranges"] == [[0, 2, 2], [0, 2, 2], [0, 2, 2]]
    stages = row["range_input_observations"]
    assert type(stages) is list and len(stages) == 2
    stage_literals = (
        ("range.last_limits", [[0, 0, 0], [0, 0, 1]], ("1", "1", "2"), "1"),
        ("range.nonlanding_ready", [[0, 0, 0]], ("2", "2", "2"), "2"),
    )
    for observation, (label, positions, last_text, step_text) in zip(stages, stage_literals):
        assert set(observation) == {"stage", "frame_ordinal", "panel_target", "operation_target", "resolved_positions"}
        assert observation["stage"] == label
        assert observation["frame_ordinal"] == _state(row, label)["frame_ordinal"]
        panel = _render(row, label)["rank_panel"]
        assert observation["panel_target"] == panel["target"]
        assert observation["operation_target"] == panel["operation_target"]
        assert observation["resolved_positions"] == positions
        operation_target = observation["operation_target"]
        assert operation_target["captured"] and operation_target["has_active_selection"]
        assert operation_target["roi_policy"] == "whole_plane" and operation_target["samples"] == 1
        assert operation_target["captured_position"] == operation_target["canvas_position"] == [1, 1, 1]
        assert operation_target["target"] == observation["panel_target"]
        assert operation_target["selected_plane_count"] == len(positions)
        assert operation_target["preview_position"] == [0, 0, 0]
        assert operation_target["preview_ordinal"] == "1" and operation_target["preview_pending"] is False
        assert operation_target["preview_requested"] is False and operation_target["preview_visible"] is False
        assert set(operation_target["axes"]) == {"t", "c", "z"}
        for axis, extent, last in zip(("t", "c", "z"), (2, 2, 3), last_text):
            draft = operation_target["axes"][axis]
            assert draft["mode"] == "range" and draft["extent"] == extent and draft["captured"] == 1
            assert draft["first"] == {"text": "1", "pending": False}
            assert draft["last"] == {"text": last, "pending": False}
            assert draft["step"] == {"text": step_text, "pending": False}

    typed_literals = (
        ("range.t.first", "t", "first", "1"),
        ("range.t.last_limit", "t", "last", "1"),
        ("range.c.first", "c", "first", "1"),
        ("range.c.last_limit", "c", "last", "1"),
        ("range.z.first", "z", "first", "1"),
        ("range.z.last_limit", "z", "last", "2"),
        ("range.t.last_nonlanding", "t", "last", "2"),
        ("range.c.last_nonlanding", "c", "last", "2"),
        ("range.t.step_nonlanding", "t", "step", "2"),
        ("range.c.step_nonlanding", "c", "step", "2"),
        ("range.z.step_nonlanding", "z", "step", "2"),
    )
    text_frames = [frame for frame in row["frames"]
                   if frame["label"].startswith("range.") and frame["label"].endswith(".text")]
    assert [(frame["label"], frame["requested_events"]) for frame in text_frames] == [
        (label + ".text", [{"type": "text", "value": value}])
        for label, axis, field, value in typed_literals]
    for text_frame, (label, axis, field, value) in zip(text_frames, typed_literals):
        selector = "target." + axis + "." + field
        # Each fixed nine-frame edit consists of the independently validated
        # real widget click, Ctrl+A, literal text, Enter and a settled frame.
        start = text_frame["ordinal"] - 6
        controls = [entry for entry in row["control_observations"]
                    if entry["before_frame_ordinal"] == start and entry["selector"] == selector]
        control, = controls
        identity = _integer(control["control"]["id"], 1)
        edit = row["frames"][start:start + 9]
        assert len(edit) == 9
        for index, down in ((3, True), (4, False)):
            assert edit[index]["label"] == label + (".select_all.down" if down else ".select_all.up")
            assert edit[index]["requested_events"] == [{"type": "keys", "keys": [
                {"name": "Ctrl", "down": down}, {"name": "A", "down": down}]}]
        assert edit[5] == text_frame
        input_frame = text_frame["rendered"]["input_frame"]
        assert input_frame["focus"]["active_id"] == input_frame["input_text"]["id"] == identity
        for index, down in ((6, True), (7, False)):
            assert edit[index]["label"] == label + (".commit.down" if down else ".commit.up")
            assert edit[index]["requested_events"] == [{"type": "key", "name": "Enter", "down": down}]
        assert edit[8]["label"] == label + ".settled" and edit[8]["requested_events"] == []
        assert edit[8]["rendered"]["rank_panel"]["operation_target"]["axes"][axis][field] == {
            "text": value, "pending": False}
    assert text_frames[5]["ordinal"] < stages[0]["frame_ordinal"] < text_frames[6]["ordinal"]
    assert text_frames[-1]["ordinal"] < stages[1]["frame_ordinal"]

    for label in ("range.last_limits", "range.nonlanding_ready", "range.preview", "range.shown"):
        state = _state(row, label)
        _unchanged(source, _source(row, label))
        assert state["workflow"] == state["results"] == []
        panel = _render(row, label)["rank_panel"]
        assert panel["method"] == "minimum" and panel["radius"] == 0
        assert panel["draft_valid"] and not panel["error"] and not panel["preview_error"]

    generation = _preview(row, "range.preview", "minimum", (0, 0, 0),
                           roi="whole_plane", full_positions=selected)
    _ordinary(row, "range.preview", _flat(SPIKE, (1, 1, 1)), (1, 1, 1))
    _ordinary(row, "range.shown", [0] * 25, (0, 0, 0), generation=generation, source="preview")
    assert _render(row, "range.shown")["rank_panel"]["generation"] == generation
    _target(_render(row, "range.shown")["rank_panel"]["target"], source, selected,
            roi="whole_plane", scope="ranges")
    preview_job = _render(row, "range.preview")["gaussian_jobs"]["preview"]["job"]
    applied_state = _state(row, "range.applied")
    applied = _source(row, "range.applied")
    _changed(source, applied, "minimum", selected)
    assert applied_state["document_ids"] == applied_state["facade_ids"] == [source["document_id"]]
    assert applied_state["active_document_id"] == source["document_id"]
    assert _camera(applied) == _camera(_source(row, "range.shown"))
    record = _publication(row, "range.applied", source, "minimum", selected, roi="whole_plane")
    assert record["target"]["ranges"] == [[0, 2, 2], [0, 2, 2], [0, 2, 2]]
    assert applied_state["workflow"] == [record] and applied_state["results"] == []
    publication, = row["publication_observations"]
    before_publication = _doc(publication["state_before"], source["document_id"])
    assert set(before_publication) == {"document_id", "shape", "dtype", "data_identity", "dirty",
                                       "undo_steps", "redo_steps", "display", "revisions"}
    for field in ("document_id", "shape", "dtype", "data_identity", "dirty", "undo_steps", "redo_steps"):
        assert before_publication[field] == source[field]
    for field in ("display", "revisions"):
        assert before_publication[field] == _source(row, "range.shown")[field]
    assert publication["state_before"]["workflow"] == publication["state_before"]["results"] == []
    job = publication["apply_lane"]["job"]
    assert _integer(job["id"], 1) != preview_job["id"]
    assert job["document_id"] == source["document_id"] and job["cancel_requested"] is False
    assert job["completed"] == job["total"] == 1
    assert job["reservation_acquired"] is True
    assert _integer(job["resource_bytes"], 1) == job["peak_reserved_bytes"]

    original_planes = {tuple(plane["position"]): plane for plane in source["planes"]}
    applied_planes = {tuple(plane["position"]): plane for plane in applied["planes"]}
    assert set(original_planes) == set(applied_planes) == set(selected) | set(excluded)
    for position in excluded:
        # The explicit eleven-plane complement checks untouched samples and
        # storage identity; it is not derived from a product-resolved target.
        for field in ("declared_hex", "storage_f64_hex", "image_address", "buffer_address"):
            assert applied_planes[position][field] == original_planes[position][field]
    for field in ("image_address", "buffer_address"):
        assert applied_planes[0, 0, 0][field] != original_planes[0, 0, 0][field]
    assert bytes.fromhex(applied_planes[0, 0, 0]["declared_hex"]) == struct.pack("<25H", *([0] * 25))
    assert bytes.fromhex(applied_planes[0, 0, 0]["storage_f64_hex"]) == struct.pack("<25d", *([0] * 25))

    undone, redone = _source(row, "range.undo"), _source(row, "range.redo")
    _annotations(source, undone)
    _assert_pixels(undone, _plane_values(source))
    assert undone["data_identity"] == source["data_identity"] and not undone["dirty"]
    assert undone["undo_steps"] == 0 and undone["redo_steps"] == 1
    _annotations(source, redone)
    _assert_pixels(redone, _plane_values(source, "minimum", selected))
    assert redone["data_identity"] == applied["data_identity"] and redone["dirty"]
    assert redone["undo_steps"] == 1 and redone["redo_steps"] == 0
    assert _camera(undone) == _camera(redone) == _camera(applied)
    for label in ("range.undo", "range.redo", "final", "terminal.cleaned"):
        state = _state(row, label)
        assert state["document_ids"] == state["facade_ids"] == [source["document_id"]]
        assert state["active_document_id"] == source["document_id"]
        assert state["workflow"] == [record] and state["results"] == []
    for label in ("final", "terminal.cleaned"):
        _unchanged(redone, _source(row, label), display=True)
    assert _state(row, "terminal.cleaned")["usage"] == row["final_usage"] == {
        "running": 0, "queued": 0, "running_bytes": 0, "retained_bytes": 0, "reserved_bytes": 0}


READERS = {"R01": _r01, "R02": _r02, "R03": _r03, "R04": _r04,
           "R05": _r05_r06, "R06": _r05_r06, "R07": _r07, "R08": _r08,
           "R09": _r09, "R10": _r10, "R11": _r11, "R12": _r12, "R13": _r13}


@pytest.mark.parametrize("context,case", PARAMETERS, ids=PARAMETER_IDS)
def test_fixed_rank_pair(evidence, context, case):
    row, directory = evidence[context, case]
    _common(row, case)
    READERS[case](row, directory)


@pytest.mark.parametrize("context", CONTEXTS, ids=RANGE_PARAMETER_IDS)
def test_fixed_rank_range_pair(range_evidence, context):
    row, directory = range_evidence[context]
    _common(row, "R13")
    _r13(row, directory)

