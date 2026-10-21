"""Independent installed-ImGui scientific Gaussian target observations.

Source samples, TCZ membership, masks and session decoding are specified here.
Small-sigma full-plane pixels are checked against a direct mathematical Gaussian
with nearest-edge extension (0.02 absolute tolerance for the held Float32
profile). Exact ImageJ kernel/reference comparisons remain in the independent
API reference matrix; producer labels and status never establish correctness.
"""

import base64
from functools import lru_cache
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


CONTRACT = "p06d1.scientific-operation-targets-ui.001"
CASES = ("D01", "D02", "D03", "D03M", "D03E", "D04", "D04X", "D05", "D05T", "D06", "D07")
CONTEXTS = ("imgui_cpp", "imgui_python")
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 32 * 1024 * 1024
SHAPE = [2, 3, 3, 17, 23, 1]
POSITIONS = [(t, c, z) for t in range(2) for c in range(3) for z in range(3)]
CALIBRATION = {
    "spacing": [.5, 1.25, 2.5], "origin": [11, -7, 19],
    "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "um",
    "time_unit": "s", "frame_interval": .75, "time_origin": 2.25,
}
CAMERAS = {"zoom": 1.25, "linked_zoom": False,
           "plane_zoom": [1.25, 1.75, 2.25], "pan": [[3, -2], [1, 4], [-5, 2]]}


def _unique(pairs):
    result = {}
    for name, value in pairs:
        assert name not in result, f"Duplicate observation key {name}"
        result[name] = value
    return result


def _nonfinite(value):
    raise AssertionError(f"Nonfinite JSON constant {value}")


def _decode(raw, maximum=MAX_REPORT):
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    assert 0 < len(raw) <= maximum
    return json.loads(raw, object_pairs_hook=_unique, parse_constant=_nonfinite)


def _read(path, maximum=MAX_REPORT):
    assert path.is_file() and not path.is_symlink()
    assert 0 < path.stat().st_size <= maximum
    return _decode(path.read_bytes(), maximum)


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_CPP_EXE to the byte-checked installed desktop")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed desktop")
    executable = Path(configured).resolve(strict=True)
    assert executable.is_file() and executable.parent.name.lower() == "bin"
    assert executable.name.lower() in {"imagejxx.exe", "imagejxx"}
    expected = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if _required():
        assert expected, "Required paired gate must pin IMAGEJXX_CPP_EXE_SHA256"
    if expected:
        assert len(expected) == 64
        with executable.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == expected
    return executable


def _owned_path(root, member):
    path = Path(member)
    assert not path.is_absolute() and ".." not in path.parts
    resolved = (root / path).resolve(strict=True)
    assert resolved.is_relative_to(root.resolve(strict=True))
    assert resolved.is_file() and not (root / path).is_symlink()
    return resolved


@pytest.fixture(scope="module")
def standalone_cases(installed_cpp_executable):
    configured = os.environ.get("IMAGEJXX_SCIENTIFIC_TARGETS_CPP_REPORT")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_SCIENTIFIC_TARGETS_CPP_REPORT to the retained standalone case index")
        return None
    index_path = Path(configured).resolve(strict=True)
    index = _read(index_path)
    assert index["contract_id"] == CONTRACT and index["context"] == "imgui_cpp"
    entries = index["cases"]
    assert isinstance(entries, list) and len(entries) == len(CASES)
    rows = {}
    for entry in entries:
        case_id = entry["case_id"]
        assert case_id in CASES and case_id not in rows
        assert entry["id"] == case_id
        path = _owned_path(index_path.parent, entry["relative_path"])
        rows[case_id] = path
    assert set(rows) == set(CASES)
    return rows


def _observe(case_id, context, directory, request):
    assert not directory.exists()
    started = time.monotonic()
    if context == "imgui_cpp":
        existing = request.getfixturevalue("standalone_cases")
        if existing is not None:
            return _read(existing[case_id]), existing[case_id].parent
        executable = request.getfixturevalue("installed_cpp_executable")
        completed = subprocess.run(
            [str(executable), "--scientific-operation-targets-ui-case", case_id, str(directory)],
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
            roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("platlib", "purelib")}
            assert any(module.is_relative_to(root) for root in roots), "Checkout desktop import"
        editor = desktop.Editor(preferences_path=str(directory.parent / "preferences.json"))
        row = _decode(editor._scientific_operation_targets_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row, directory


def _frame(row, label):
    matches = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(matches) == 1, f"Missing or duplicate physical frame {label}"
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
        return dict(panel["control_states"][key], point=panel["controls"][key], rect=panel["control_rects"][key])
    if selector.startswith("target."):
        key = selector.removeprefix("target.")
        panel = rendered["gaussian_panel"]["operation_target"]
        return dict(panel["control_states"][key], point=panel["controls"][key], rect=panel["control_rects"][key])
    if selector == "review.cancel":
        return rendered["native_workflow"]["controls"]["cancel"]
    if selector.startswith("threshold."):
        key = selector.removeprefix("threshold."); panel = rendered["threshold_panel"]
        return dict(panel["control_states"][key], point=panel["controls"][key], rect=panel["control_rects"][key])
    if selector == "error_modal.ok":
        return rendered["error_modal"]["controls"]["ok"]
    return rendered["host_controls"][selector]


def _physical(row):
    frames = row["frames"]
    assert 1 <= row["frame_count"] == len(frames) <= 180
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    held, buttons, position, sessions = set(), [False] * 3, None, []
    allowed_keys = {"Ctrl", "A", "Enter", "Escape", "Tab", "Left", "Right", "Up", "Down", "Shift"}
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
                assert actual["input_text"]["id"] == actual["focus"]["active_id"] != 0
        if position is not None:
            assert actual["mouse_position"] == position
        assert actual["mouse_down"] == buttons and set(actual["keys_down"]) == held
        assert actual["modifiers"] == {"ctrl": "Ctrl" in held, "shift": "Shift" in held,
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
        point, rect = observation["position"], actual["rect"]
        assert all(type(value) in {int, float} and math.isfinite(value) for value in point + rect)
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


def _source(t, c, z, y, x):
    return (40*t + 15*c + 7*z + .25*x + .5*y + 3*((x + 2*y + z + c + 3*t) % 5)
            + (70 + 11*z if x == 3 + 2*c and y == 4 + 3*t else 0)
            + (35 if x == 17-z and y == 12-c else 0))


def _source_plane(position):
    return [_source(*position, y, x) for y in range(17) for x in range(23)]


def _pack(values, dtype):
    return b"".join(struct.pack(dtype, value) for value in values)


def _planes(document):
    assert document["shape"] in (SHAPE, [1, 1, 3, 17, 23, 1]) and document["dtype"] == "float32"
    positions = [(t, c, z) for t in range(document["shape"][0])
                 for c in range(document["shape"][1]) for z in range(3)]
    rows = document["planes"]
    assert len(rows) == len(positions)
    result = {}
    for position, row in zip(positions, rows):
        assert row["position"] == list(position)
        raw = bytes.fromhex(row["storage_f64_hex"])
        typed = bytes.fromhex(row["declared_f32_hex"])
        assert len(raw) == 17 * 23 * 8 and len(typed) == 17 * 23 * 4
        values = list(struct.unpack("<391d", raw))
        assert all(math.isfinite(value) for value in values)
        assert typed == _pack(values, "<f")
        assert list(struct.unpack("<391f", typed)) == values, "Noncanonical native Float32 fixture/result"
        assert isinstance(row["image_address"], str) and row["image_address"].startswith("0x")
        assert isinstance(row["buffer_address"], str) and row["buffer_address"].startswith("0x")
        result[position] = (row, values)
    return result


def _literal(document):
    planes = _planes(document)
    for position, (row, values) in planes.items():
        expected = _source_plane(position)
        assert values == expected
        assert row["storage_f64_hex"] == _pack(expected, "<d").hex()
        assert row["declared_f32_hex"] == _pack(expected, "<f").hex()
    assert document["calibration"] == CALIBRATION
    return planes


def _document(state, document_id):
    matches = [document for document in state["documents"] if document["document_id"] == document_id]
    assert len(matches) == 1
    return matches[0]


def _position(document):
    display = document["display"]
    return (display["t"], display["c"], display["crosshair"][2])


def _same_pixels(before, after, *, identities=True):
    a, b = _planes(before), _planes(after)
    assert a.keys() == b.keys()
    for position in a:
        assert b[position][0]["storage_f64_hex"] == a[position][0]["storage_f64_hex"]
        assert b[position][0]["declared_f32_hex"] == a[position][0]["declared_f32_hex"]
        if identities:
            assert b[position][0]["image_address"] == a[position][0]["image_address"]
            assert b[position][0]["buffer_address"] == a[position][0]["buffer_address"]


def _preserved(before, after):
    for key in ("shape", "dtype", "calibration", "metadata", "selection", "previous_selection", "rois", "overlays"):
        assert after[key] == before[key], key


def _target(record, selected, source, *, roi="active_selection"):
    assert record["document_id"] == source["document_id"]
    assert record["source_shape"] == source["shape"] and record["roi_policy"] == roi
    assert record["source_revisions"] == [source["revisions"][key] for key in ("pixels", "metadata", "selection")]
    assert record["schema_version"] == 1
    assert record["execution_mode"] == "planes" and record["destination"] == "in_place"
    if record["scope"] == "positions":
        assert record["ranges"] == []
        positions = [tuple(position) for position in record["positions"]]
    else:
        assert record["scope"] == "ranges" and record["positions"] == []
        ranges = record["ranges"]
        assert len(ranges) == 3
        for values, extent in zip(ranges, source["shape"][:3]):
            assert len(values) == 3 and all(type(value) is int for value in values)
            assert 0 <= values[0] < values[1] <= extent and values[2] > 0
        positions = [(t, c, z) for t in range(*ranges[0]) for c in range(*ranges[1]) for z in range(*ranges[2])]
    assert positions == list(selected) and len(positions) == len(set(positions))


def _command(record, selected, source, *, sigma=1.0, scaled=False, roi="active_selection", calibration=CALIBRATION):
    assert record["schema_version"] == 1 and record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": sigma, "scaled_units": scaled}
    assert record["calibration"] == calibration
    assert record["effective_sigma_xy"] == ([sigma/calibration["spacing"][0], sigma/calibration["spacing"][1]] if scaled else [sigma, sigma])
    _target(record["target"], selected, source, roi=roi)


@lru_cache(maxsize=128)
def _analytic_convolve(source, sigma_x, sigma_y):
    """Direct normalized mathematical convolution; no product kernel helpers."""
    values = list(source)
    for sigma, axis in ((sigma_x, 0), (sigma_y, 1)):
        if sigma == 0:
            continue
        assert 0 < sigma <= 4, "Use held reference evidence for resampling/large sigma"
        radius = math.ceil(7*sigma)
        weights = [math.exp(-.5*(offset/sigma)**2) for offset in range(-radius, radius+1)]
        divisor = math.fsum(weights)
        weights = [weight/divisor for weight in weights]
        output = []
        for y in range(17):
            for x in range(23):
                output.append(math.fsum(weight * values[(min(16, max(0, y+offset)) if axis else y)*23
                                                        + (x if axis else min(22, max(0, x+offset)))]
                                        for offset, weight in zip(range(-radius, radius+1), weights)))
        values = output
    return tuple(values)


def _changed(before, after, selected, *, sigma=1.0, scaled=False, masks=None):
    _preserved(before, after)
    old, new = _planes(before), _planes(after)
    selected = set(selected)
    assert old.keys() == new.keys()
    for position in old:
        a, b = old[position], new[position]
        mask = masks.get(position, set()) if masks is not None else set(range(391))
        affected = position in selected and bool(mask) and sigma != 0
        if not affected:
            assert b[0]["storage_f64_hex"] == a[0]["storage_f64_hex"]
            assert b[0]["image_address"] == a[0]["image_address"]
            assert b[0]["buffer_address"] == a[0]["buffer_address"]
            continue
        assert b[0]["storage_f64_hex"] != a[0]["storage_f64_hex"], position
        assert b[0]["buffer_address"] != a[0]["buffer_address"]
        spacing = before["calibration"]["spacing"]
        sx, sy = (sigma/spacing[0], sigma/spacing[1]) if scaled else (sigma, sigma)
        expected = _analytic_convolve(tuple(a[1]), sx, sy)
        for index, (source, actual) in enumerate(zip(a[1], b[1])):
            if index not in mask:
                assert struct.pack("<d", actual) == struct.pack("<d", source)
            else:
                assert abs(actual-expected[index]) <= .02, (position, index, actual, expected[index])


def _session(path):
    assert path.is_file() and not path.is_symlink() and 18 < path.stat().st_size <= MAX_ARCHIVE
    raw = path.read_bytes()
    assert raw[:10] == b"IMAGEJXX3\n"
    declared, = struct.unpack("<Q", raw[10:18])
    assert 0 < declared <= MAX_ARCHIVE
    stream = zlib.decompressobj()
    data = stream.decompress(raw[18:], declared+1)
    assert len(data) == declared and stream.eof and not stream.unused_data and not stream.unconsumed_tail
    root = _decode(data, MAX_ARCHIVE)
    assert root["format"] == "imagejxx-session" and root["version"] == 3
    return root


def _rectangle(x0, y0, x1, y1):
    return {y*23+x for y in range(y0, y1) for x in range(x0, x1)}


def _roi_masks(kind, case_id):
    if kind == "whole_plane":
        return None
    if kind == "active_selection":
        if case_id not in {"D03", "D03M"}:
            return None  # Captured absence means whole plane, as declared by UI.
        mask = (_rectangle(3, 2, 19, 15) - _rectangle(8, 6, 12, 10)
                if case_id == "D03M" else _rectangle(3, 2, 18, 14))
        return {position: mask for position in POSITIONS}
    assert kind == "positioned_rois"
    result = {position: set() for position in POSITIONS}
    result[0, 0, 0] |= _rectangle(2, 2, 9, 10)
    if case_id == "D03E":
        return result
    for position in POSITIONS:
        t, c, z = position
        if c == 2:
            result[position] |= _rectangle(6, 5, 14, 14)
        if position == (1, 2, 1):
            result[position] |= _rectangle(10, 3, 20, 12)
        if t == 1 and c == 1:
            result[position] |= _rectangle(4, 3, 17, 14) - _rectangle(8, 6, 12, 10)
        if position == (1, 0, 2):
            result[position] = set(range(391))  # Existing Gaussian non-area rule.
    return result


def _state(row, label):
    state = row["states"][label]
    frame = row["frames"][state["frame_ordinal"] - 1]
    assert frame["label"] == label
    assert state["document_ids"] == [document["document_id"] for document in state["documents"]]
    assert state["active_document_id"] in state["document_ids"] or state["active_document_id"] == 0
    assert type(state["recording"]) is bool
    return state, frame["rendered"]


def _owner(row, label):
    return _document(row["states"][label], row["source_document_id"])


def _history(document, undo, redo=0, *, dirty):
    assert document["history"]["undo_steps"] == undo
    assert document["history"]["redo_steps"] == redo
    assert document["dirty"] is dirty


def _unchanged(before, after, *, display=True, history=True):
    _preserved(before, after)
    _same_pixels(before, after)
    assert after["data_identity"] == before["data_identity"]
    for key in ("pixels", "metadata", "selection"):
        assert after["revisions"][key] == before["revisions"][key]
    if display:
        assert after["display"] == before["display"]
        assert after["revisions"]["display"] == before["revisions"]["display"]
    if history:
        assert after["history"] == before["history"] and after["dirty"] == before["dirty"]


def _no_resources(state):
    assert state["busy"] is False
    usage = state["usage"]
    assert all(usage[key] == 0 for key in ("running", "queued", "reserved_bytes", "running_bytes", "retained_bytes"))


def _selectors(row):
    return [observation["selector"] for observation in row["control_observations"]]


def _require_selectors(row, *names):
    selectors = _selectors(row)
    for name in names:
        assert name in selectors, f"Missing actual control interaction {name}"


def _panel(rendered):
    panel = rendered["gaussian_panel"]
    assert panel["open"] is True and panel["scientific_targets"] is True
    assert panel["stack_prompt"] is False
    return panel


def _draft(row, label, selected, *, sigma=1.0, scaled=False, roi="active_selection"):
    state, rendered = _state(row, label)
    panel = _panel(rendered)
    assert panel["draft_valid"] is True
    assert panel["sigma"] == sigma and panel["scaled_units"] is scaled
    assert panel["source_id"] == row["source_document_id"]
    assert panel["operation_target"]["selected_plane_count"] == len(selected)
    assert panel["operation_target"]["captured_position"] == [1, 2, 1]
    assert panel["operation_target"]["samples"] == 1
    _target(panel["target"], selected, _document(state, row["source_document_id"]), roi=roi)
    preview_position = tuple(panel["preview_position"])
    assert preview_position in selected
    _target(panel["preview_target"], [preview_position], _document(state, row["source_document_id"]), roi=roi)
    return panel, state


def _shown(row, label, *, position, preview=False, preview_position=None, sigma=1.0,
           scaled=False, roi="active_selection", case_id=None, orthogonal=False):
    state, rendered = _state(row, label)
    source = _document(state, row["source_document_id"])
    planes = _planes(source)
    assert _position(source) == tuple(position)
    t, c, z = position
    effective = [sigma/.5, sigma/1.25] if scaled else [sigma, sigma]
    masks = _roi_masks(roi, case_id) if roi != "whole_plane" else None
    displayed = {key: values for key, (_, values) in planes.items()}
    if preview:
        preview_position = tuple(preview_position or position)
        assert preview_position == tuple(position)
        _command(rendered["gaussian_jobs"]["preview"]["command_record"], [preview_position],
                 source, sigma=sigma, scaled=scaled, roi=roi)
        original = displayed[preview_position]
        blurred = _analytic_convolve(tuple(original), *effective)
        mask = masks.get(preview_position, set()) if masks is not None else set(range(391))
        displayed[preview_position] = [blurred[index] if index in mask else sample for index, sample in enumerate(original)]
    live_range = [min(planes[t, c, z][1]), max(planes[t, c, z][1])]
    if orthogonal:
        x, y, _ = source["display"]["crosshair"]
        specs = [("XY", z, [17, 23, 1], displayed[t, c, z]),
                 ("XZ", y, [3, 23, 1], [displayed[t, c, iz][y*23+ix] for iz in range(3) for ix in range(23)]),
                 ("YZ", x, [3, 17, 1], [displayed[t, c, iz][iy*23+x] for iz in range(3) for iy in range(17)])]
        actual = rendered["orthogonal_planes"]
        assert len(actual) == 3
    else:
        specs = [(None, None, [17, 23, 1], displayed[t, c, z])]
        actual = [rendered["ordinary_pixels"]]
    for plane, (name, coordinate, shape, expected) in zip(actual, specs):
        assert plane["source_document_id"] == row["source_document_id"]
        assert plane["current_tcz"] == plane["range_tcz"] == list(position)
        assert plane["range"] == live_range
        assert plane["pixels_source"] == ("transient" if orthogonal and preview else "live_document" if orthogonal else "preview" if preview else "live")
        assert (plane["preview_generation"] > 0) is preview
        if name is not None:
            assert plane["plane"] == name and plane["coordinate"] == coordinate
            assert plane["range_policy"] == "live_document"
        else:
            assert plane["contrast_source"] == "live"
        readback = plane["readback"]
        assert readback["available"] is True and readback["source_tcz"] == list(position)
        image = readback["source"]
        assert image["shape"] == shape and image["dtype"] == "float32"
        assert len(image["pixels"]) == len(expected)
        if preview:
            assert all(abs(a-b) <= .02 for a, b in zip(image["pixels"], expected))
        else:
            assert image["pixels"] == expected
    return rendered


def _accepted(row, before_label, after_label, selected, *, sigma=1.0, scaled=False,
              roi="active_selection", case_id=None, undo, dirty=True):
    before, after = _owner(row, before_label), _owner(row, after_label)
    _changed(before, after, selected, sigma=sigma, scaled=scaled,
             masks=_roi_masks(roi, case_id) if roi != "whole_plane" else None)
    _history(after, undo, dirty=dirty)
    state, _ = _state(row, after_label)
    assert state["results"] == []
    record = state["workflow"][-1]
    _command(record, selected, before, sigma=sigma, scaled=scaled, roi=roi)
    assert after["display"] == before["display"]
    _no_resources(state)
    return record


def _initial(row, case_id):
    state, _ = _state(row, "initial")
    source = _document(state, row["source_document_id"])
    _literal(source)
    _history(source, 0, dirty=False)
    assert _position(source) == (1, 2, 1)
    assert source["display"]["crosshair"] == [9, 7, 1]
    for key, value in CAMERAS.items():
        assert source["display"][key] == value
    assert source["metadata"] == {
        "title": "Scientific operation targets " + case_id,
        "properties": {"fixture": case_id, "fixture_unknown": "preserve"},
        "extensions": {"fixture_extension": "preserve"}, "record_layout": {},
    }
    assert source["display"]["properties"] == {"lut": "Grays"}
    assert source["previous_selection"] is None and source["spectrum_present"] is False
    assert source["overlays"] == [{"kind": "point", "points": [[21, 15]], "fixture_overlay_unknown": "preserve"}]
    _initial_rois(source, case_id)
    assert state["workflow"] == state["results"] == []
    assert state["recording"] is (case_id not in {"D05", "D05T"})
    return source


def _initial_rois(source, case_id):
    if case_id not in {"D03", "D03M", "D03E"}:
        assert source["selection"] is None and source["rois"] == []
        return
    if case_id == "D03E":
        assert source["selection"] is None
    elif case_id == "D03":
        assert source["selection"] == {"kind": "rectangle", "points": [[3, 2], [18, 14]], "width": 1}
    else:
        _mask_selection(source["selection"], _rectangle(3, 2, 19, 15) - _rectangle(8, 6, 12, 10))
    recipes = [("exact_zero", 0, 0, 0, "rectangle", [[2, 2], [9, 10]])]
    if case_id != "D03E":
        recipes += [("channel_wildcards", None, 2, None, "rectangle", [[6, 5], [14, 14]]),
                    ("overlap", 1, 2, 1, "rectangle", [[10, 3], [20, 12]]),
                    ("holed", 1, 1, None, "mask", None),
                    ("non_area", 1, 0, 2, "line", [[2, 2], [8, 9]])]
    assert len(source["rois"]) == len(recipes)
    for roi, (name, t, c, z, kind, points) in zip(source["rois"], recipes):
        assert (roi["name"], roi["t"], roi["c"], roi["z"]) == (name, t, c, z)
        assert roi["attributes"] == {"fixture_roi_unknown": "preserve"}
        if kind == "mask":
            _mask_selection(roi["selection"], _rectangle(4, 3, 17, 14) - _rectangle(8, 6, 12, 10))
        else:
            assert roi["selection"] == {"kind": kind, "points": points, "width": 1}


def _mask_selection(selection, expected):
    assert selection["kind"] == "mask" and selection["shape"] == [17, 23]
    assert selection["fixture_shape_unknown"] == "preserve"
    raw = base64.b64decode(selection["mask"], validate=True)
    assert len(raw) == math.ceil(391/8)
    actual = {index for index in range(391) if raw[index//8] & (1 << (7-index % 8))}
    assert actual == expected
    assert raw[-1] & 1 == 0  # The single unused packed tail bit remains zero.


def _current(row):
    selected = [(1, 2, 1)]
    _draft(row, "captured_preview", selected, sigma=.75)
    _shown(row, "captured_preview", position=(1, 2, 1), preview=True, sigma=.75)
    before = _owner(row, "captured_preview")
    navigated = _owner(row, "navigated")
    _unchanged(before, navigated, display=False)
    assert _position(navigated) == (0, 0, 2)
    for key, value in CAMERAS.items():
        assert navigated["display"][key] == value
    _draft(row, "navigated", selected, sigma=.75)
    _shown(row, "navigated", position=(0, 0, 2))
    record = _accepted(row, "navigated", "applied", selected, sigma=.75, undo=1)
    preview_values = _frame(row, "captured_preview")["rendered"]["ordinary_pixels"]["readback"]["source"]["pixels"]
    assert _planes(_owner(row, "applied"))[1, 2, 1][1] == preview_values
    undone, redone = _owner(row, "undone"), _owner(row, "redone")
    _same_pixels(navigated, undone)
    _preserved(navigated, undone)
    _history(undone, 0, 1, dirty=False)
    _same_pixels(_owner(row, "applied"), redone)
    _history(redone, 1, dirty=True)
    assert undone["display"] == redone["display"] == navigated["display"]
    assert row["states"]["undone"]["workflow"] == row["states"]["redone"]["workflow"] == [record]
    _require_selectors(row, "gaussian.sigma", "gaussian.preview", "gaussian.ok",
                       "navigation.channel", "navigation.time", "menu.Edit.Undo", "menu.Edit.Redo")


def _mixed(row):
    recipes = [
        ("mixed_a", [(1, c, z) for c in (0, 2) for z in range(3)]),
        ("mixed_b", [(0, c, 1) for c in range(3)]),
        ("mixed_c", [(t, 2, z) for t in range(2) for z in (0, 2)]),
    ]
    for undo, (label, selected) in enumerate(recipes, 1):
        _draft(row, label + ".draft", selected)
        _accepted(row, label + ".draft", label + ".applied", selected, undo=undo)
        assert len(row["states"][label + ".applied"]["workflow"]) == undo
    _require_selectors(row, "target.c.range", "target.c.step", "target.z.all",
                       "target.t.range", "target.c.all", "target.t.all", "target.z.range", "target.z.step")


def _roi(row, case_id):
    if case_id == "D03":
        recipes = [("whole", "whole_plane"), ("active", "active_selection"), ("positioned", "positioned_rois")]
    elif case_id == "D03M":
        recipes = [("mask", "active_selection")]
    else:
        recipes = [("empty", "positioned_rois")]
    for number, (label, policy) in enumerate(recipes, 1):
        selected = [(1, 2, 1)] if case_id == "D03E" else POSITIONS
        draft_label = "empty.preview" if case_id == "D03E" else label + ".draft"
        _draft(row, draft_label, selected, roi=policy)
        if case_id == "D03E":
            _shown(row, draft_label, position=(1, 2, 1), preview=True, roi=policy, case_id=case_id)
        _accepted(row, draft_label, label + ".applied", selected, roi=policy, case_id=case_id,
                  undo=0 if case_id == "D03E" else 1, dirty=case_id != "D03E")
        if case_id == "D03":
            assert len(row["states"][label + ".applied"]["workflow"]) == number
            if label != "positioned":
                undone = _owner(row, label + ".undone")
                _same_pixels(_owner(row, "initial"), undone)
                _history(undone, 0, 1, dirty=False)
    _require_selectors(row, "target.roi." + recipes[-1][1], "gaussian.ok")
    if case_id != "D03E":
        _require_selectors(row, "target.t.all", "target.c.all", "target.z.all")


def _drafts(row):
    _draft(row, "valid_one", [(1, 2, 1)], sigma=1)
    _shown(row, "valid_one", position=(1, 2, 1), preview=True, sigma=1)
    for label in ("sigma_partial", "first_uncommitted", "range_uncommitted", "range_zero",
                  "preview_partial", "preview_partial_committed", "preview_invalid"):
        state, rendered = _state(row, label)
        panel = _panel(rendered)
        assert panel["draft_valid"] is False and panel["control_states"]["ok"]["enabled"] is False
        assert panel["error"]
        assert state["gaussian_host"]["gaussian_jobs"]["preview"]["owns_job"] is False
        _unchanged(_owner(row, "initial"), _document(state, row["source_document_id"]), display=False)
        assert state["workflow"] == state["results"] == []
    _draft(row, "valid_two", [(1, 2, 1)], sigma=1.25)
    _shown(row, "valid_two", position=(1, 2, 1), preview=True, sigma=1.25)
    first_pending = _panel(_state(row, "first_uncommitted")[1])["operation_target"]
    assert first_pending["axes"]["c"]["first"] == {"text": "2", "pending": True}
    for label, selected in (("first_blur", [(1, 1, 1), (1, 2, 1)]),
                             ("first_restored", [(1, c, 1) for c in range(3)])):
        panel, _ = _draft(row, label, selected, sigma=1.25)
        assert panel["operation_target"]["axes"]["c"]["first"]["pending"] is False
        _shown(row, label, position=(1, 2, 1), preview=True, sigma=1.25)
    assert _panel(_state(row, "preview_partial")[1])["operation_target"]["preview_pending"] is True
    assert _panel(_state(row, "preview_partial_committed")[1])["operation_target"]["preview_pending"] is False
    assert any(frame["requested_events"] == [{"type": "text", "value": "2x"}] for frame in row["frames"])
    outside, _ = _state(row, "outside_target")
    _draft(row, "outside_target", [(1, 0, 1)], sigma=1.25)
    _shown(row, "outside_target", position=(1, 2, 1))
    assert outside["workflow"] == []
    _draft(row, "show_selected", [(1, 0, 1)], sigma=1.25)
    _shown(row, "show_selected", position=(1, 0, 1), preview=True, sigma=1.25)
    for label in ("range_corrected", "preview_corrected"):
        _draft(row, label, [(1, 0, 1)], sigma=1.25)
    _accepted(row, "preview_corrected", "applied", [(1, 0, 1)], sigma=1.25, undo=1)
    assert any(frame["requested_events"] == [{"type": "text", "value": "1e"}] for frame in row["frames"])
    _require_selectors(row, "target.c.range", "target.c.first", "target.c.last", "target.c.step",
                       "target.preview.ordinal", "target.preview.show", "gaussian.sigma", "gaussian.ok")


def _xyz(row):
    selected = [(t, c, z) for t in range(2) for c in (0, 2) for z in range(3)]
    _draft(row, "selected_xyz", selected, sigma=1.5, scaled=True)
    _shown(row, "selected_xyz", position=(1, 2, 1), preview=True, sigma=1.5, scaled=True, orthogonal=True)
    _shown(row, "outside_xyz", position=(0, 1, 1), orthogonal=True)
    _shown(row, "other_selected_live", position=(0, 2, 1), orthogonal=True)
    _shown(row, "returned_xyz", position=(1, 2, 1), preview=True, sigma=1.5, scaled=True, orthogonal=True)
    for label in ("outside_xyz", "other_selected_live", "returned_xyz"):
        _draft(row, label, selected, sigma=1.5, scaled=True)
        _unchanged(_owner(row, "initial"), _owner(row, label), display=False)
        for key, value in CAMERAS.items():
            assert _owner(row, label)["display"][key] == value
    _accepted(row, "returned_xyz", "applied", selected, sigma=1.5, scaled=True, undo=1)
    _require_selectors(row, "gaussian.scaled", "target.t.all", "target.c.range", "target.c.step",
                       "target.z.all", "navigation.channel", "navigation.time", "target.preview.show")


def _api(row, name):
    matches = [observation for observation in row["api_observations"] if observation["name"] == name]
    assert len(matches) == 1, f"Missing or duplicate labeled API observation {name}"
    return matches[0]


def _refused_api(row, name):
    observation = _api(row, name)
    assert observation["accepted"] is False and observation["error"]
    assert observation["error_type"] in {"invalid_argument", "exception"}
    before, after = observation["before"], observation["after"]
    for key in ("documents", "document_ids", "active_document_id", "results", "workflow", "recording", "busy",
                "usage", "job_limits", "gaussian_host", "jobs"):
        assert after[key] == before[key], (name, key)
    return observation


def _failure(row):
    for label in ("held_preview", "after_malformed", "canceled", "failed_admission", "recovered", "final_canceled"):
        _unchanged(_owner(row, "initial"), _owner(row, label))
        state, _ = _state(row, label)
        assert state["workflow"] == state["results"] == [] and state["recording"] is False
    _shown(row, "held_preview", position=(1, 2, 1), preview=True)
    _shown(row, "after_malformed", position=(1, 2, 1), preview=True)
    _refused_api(row, "malformed_target_preserves_preview")
    for label in ("canceled", "failed_admission", "final_canceled"):
        state, rendered = _state(row, label)
        _no_resources(state)
        assert state["gaussian_host"]["gaussian_jobs"]["active_installed_generation"] is None
        _shown(row, label, position=(1, 2, 1))
        if label == "failed_admission":
            assert rendered["gaussian_panel"]["preview_error"]
            assert state["gaussian_host"]["gaussian_jobs"]["request_error"]
            assert state["job_limits"]["memory_budget"] == 1
    _shown(row, "recovered", position=(1, 2, 1), preview=True, sigma=1.25)
    recovered, rendered = _state(row, "recovered")
    assert rendered["gaussian_panel"]["preview_error"] == ""
    assert recovered["job_limits"] == row["states"]["initial"]["job_limits"]
    for name in ("scheduler_budget_one_byte", "restore_scheduler_budget"):
        assert _api(row, name)["accepted"] is True
    _require_selectors(row, "gaussian.preview", "gaussian.cancel", "gaussian.sigma")


def _source_lifetime(row):
    for label in ("first_preview", "following_eager", "second_preview"):
        _unchanged(_owner(row, "initial"), _owner(row, label))
    _shown(row, "first_preview", position=(1, 2, 1), preview=True, sigma=2)
    _shown(row, "second_preview", position=(1, 2, 1), preview=True, sigma=2)
    eager_id = row["eager_document_id"]
    following, rendered = _state(row, "following_eager")
    assert following["active_document_id"] == eager_id
    assert rendered["gaussian_panel"]["open"] is False
    _no_resources(following)
    closed, rendered = _state(row, "closed")
    assert row["source_document_id"] not in closed["document_ids"]
    assert closed["document_ids"] == [eager_id] and closed["active_document_id"] == eager_id
    assert rendered["gaussian_panel"]["open"] is False
    _no_resources(closed)
    _unchanged(_owner(row, "initial"), row["closed_owner"])
    for label in ("following_eager", "closed"):
        _unchanged(_document(row["states"]["initial"], eager_id), _document(row["states"][label], eager_id))
    _require_selectors(row, "document." + str(eager_id), "document." + str(row["source_document_id"]),
                       "menu.File", "menu.File.Close_image")


def _replay(row, directory):
    selected = [(1, c, z) for c in range(3) for z in (0, 2)]
    _draft(row, "accepted_draft", selected, sigma=.75, scaled=True)
    original, applied = _owner(row, "accepted_draft"), _owner(row, "applied")
    _changed(original, applied, selected, sigma=.75, scaled=True)
    _history(applied, 1, dirty=True)
    assert _position(applied) == (0, 0, 2)
    for key, value in CAMERAS.items():
        assert applied["display"][key] == value
    record = row["accepted_command_record"]
    _command(record, selected, original, sigma=.75, scaled=True)
    assert row["states"]["applied"]["workflow"] == [record]
    decoded = _api(row, "navigation_tolerant_strict_decode")
    assert decoded["accepted"] is True and decoded["resolved_record"] == record
    for name in ("stale_strict_replay", "malformed_original_rebind"):
        _refused_api(row, name)
    saved = _owner(row, "saved")
    _same_pixels(applied, saved)
    _preserved(applied, saved)
    _history(saved, 1, dirty=False)
    assert saved["display"] == applied["display"]
    file = row["files"]["edited"]
    assert file["member"] == "edited.ijx"
    path = _owned_path(directory, file["member"])
    raw = path.read_bytes()
    assert raw.hex() == file["hex"] and len(raw) == file["byte_count"]
    wire = _session(path)
    assert wire["workflow"] == [record] and wire["results"] == [] and wire["extensions"] == {"imagejxx_workspace": {"version": 1, "active_index": 0}}
    encoded, = wire["documents"]
    assert encoded["axes"] == "TCZYXS" and encoded["shape"] == SHAPE
    assert encoded["pixel_type"] == "float32" and encoded["storage"] == "Float64LE"
    assert [base64.b64decode(value, validate=True).hex() for value in encoded["planes"]] == [plane["storage_f64_hex"] for plane in saved["planes"]]
    for key in ("metadata", "calibration", "selection", "previous_selection", "rois", "overlays"):
        assert encoded[key] == saved[key]
    assert encoded["display"] == {key: value for key, value in saved["display"].items() if key != "revision"}
    loaded, _ = _state(row, "loaded")
    assert len(loaded["document_ids"]) == 2 and loaded["active_document_id"] != row["source_document_id"]
    destination_id = loaded["active_document_id"]
    destination = _document(loaded, destination_id)
    _same_pixels(saved, destination, identities=False)
    _preserved(saved, destination)
    _history(destination, 0, dirty=False)
    assert {key: value for key, value in destination["display"].items() if key != "revision"} == encoded["display"]
    calibrated = _document(row["states"]["rebound_calibration"], destination_id)
    expected_calibration = dict(CALIBRATION, spacing=[.25, 1.5, 2.5])
    assert calibrated["calibration"] == expected_calibration
    _same_pixels(destination, calibrated)
    _history(calibrated, 1, dirty=True)
    rebound = _document(row["states"]["rebound"], destination_id)
    _changed(calibrated, rebound, [(0, 1, 0), (0, 1, 2)], sigma=.75, scaled=True)
    _history(rebound, 2, dirty=True)
    assert row["states"]["rebound"]["recording"] is False
    assert row["states"]["rebound"]["workflow"] == [record]
    _unchanged(saved, _document(row["states"]["rebound"], row["source_document_id"]))
    rebind = _api(row, "explicit_reopened_rebind")
    assert rebind["accepted"] is True and rebind["inputs"]["record"] == record
    assert record["target"]["document_id"] != destination_id
    _require_selectors(row, "gaussian.scaled", "target.c.all", "target.z.range", "target.z.step", "gaussian.ok")


def _eager(row):
    for label in ("generic_review", "generic_canceled", "scientific_large_preview", "supported_threshold"):
        _unchanged(_owner(row, "initial"), _owner(row, label))
    for label in ("threshold_canceled", "scientific_guards", "scientific_tab"):
        before, after = _owner(row, "initial"), _owner(row, label)
        _unchanged(before, after, display=False)
        assert {k:v for k,v in after["display"].items() if k != "revision"} == {k:v for k,v in before["display"].items() if k != "revision"}
        assert after["revisions"]["display"] == after["display"]["revision"] == before["revisions"]["display"] + 1
    state, rendered = _state(row, "scientific_large_preview")
    panel = _panel(rendered)
    assert panel["sigma"] == 12 and panel["draft_valid"] is True
    _target(panel["preview_target"], [(1, 2, 1)], _owner(row, "initial"))
    _command(rendered["gaussian_jobs"]["preview"]["command_record"], [(1, 2, 1)],
             _owner(row, "initial"), sigma=12)
    pixels = rendered["ordinary_pixels"]
    assert pixels["pixels_source"] == "preview" and pixels["current_tcz"] == [1, 2, 1]
    large = pixels["readback"]["source"]
    source = _source_plane((1, 2, 1))
    assert large["shape"] == [17, 23, 1] and large["dtype"] == "float32"
    assert len(large["pixels"]) == 391 and large["pixels"] != source
    assert all(math.isfinite(value) and min(source)-.02 <= value <= max(source)+.02 for value in large["pixels"])
    # Large-sigma ImageJ resampling is qualified by pinned API reference cases;
    # this UI assertion checks actual routing/readback, not a new numerical oracle.
    for name in ("guard_fill", "guard_run_workflow", "guard_generic_operation"):
        _refused_api(row, name)
    _, generic = _state(row, "generic_review")
    review = generic["native_workflow"]
    assert review["visible"] and not review["ready"] and not review["error"]
    assert len(review["entries"]) == 1 and not review["entries"][0]["confirmed"]
    assert not _state(row, "generic_canceled")[1]["native_workflow"]["visible"]
    assert _api(row, "open_supported_threshold")["accepted"] is True
    _, threshold = _state(row, "supported_threshold")
    assert threshold["threshold_panel"]["open"] and not threshold["threshold_panel"]["error"]
    assert not _state(row, "threshold_canceled")[1]["threshold_panel"]["open"]
    _require_selectors(row, "review.cancel", "threshold.cancel")
    eager_id = row["eager_document_id"]
    original = _document(row["states"]["initial"], eager_id)
    _literal(original)
    assert original["shape"] == [1, 1, 3, 17, 23, 1]
    for label in ("eager_current_prompt", "eager_stack_prompt"):
        _, frame = _state(row, label)
        assert frame["gaussian_panel"]["stack_prompt"] is True
        assert frame["gaussian_panel"]["scientific_targets"] is False
    current = _document(row["states"]["eager_current_applied"], eager_id)
    _changed(original, current, [(0, 0, 1)], sigma=.5)
    _history(current, 1, dirty=True)
    stacked = _document(row["states"]["eager_stack_applied"], eager_id)
    _changed(current, stacked, [(0, 0, z) for z in range(3)], sigma=1)
    _history(stacked, 2, dirty=True)
    assert len(row["states"]["eager_current_applied"]["workflow"]) == 1
    assert len(row["states"]["eager_stack_applied"]["workflow"]) == 2
    for phase, sigma, selected, before in (("eager_current_applied", .5, [(0, 0, 1)], original),
                                           ("eager_stack_applied", 1, [(0, 0, z) for z in range(3)], current)):
        _command(row["states"][phase]["workflow"][-1], selected, before, sigma=sigma)
    for label in ("eager.current_prompt.focus_settled", "eager.stack_prompt.focus_settled"):
        rendered = _frame(row, label)["rendered"]
        yes = _control(rendered, "gaussian.stack_yes")
        assert yes["focused"] is True and yes["enabled"] is True
        assert rendered["input_frame"]["focus"]["nav_id"] == yes["id"]
    tab_frames = [frame for frame in row["frames"] if frame["label"].startswith("eager.current_choose_no.")]
    assert len(tab_frames) % 3 == 0 and 1 <= len(tab_frames)//3 <= 6
    assert [frame["label"] for frame in tab_frames] == [
        f"eager.current_choose_no.{attempt}.{phase}"
        for attempt in range(len(tab_frames)//3) for phase in ("down", "up", "settled")]
    settled_yes = _frame(row, "eager.current_prompt.focus_settled")
    assert [frame["ordinal"] for frame in tab_frames] == list(
        range(settled_yes["ordinal"] + 1, settled_yes["ordinal"] + 1 + len(tab_frames)))
    no_id = _control(settled_yes["rendered"], "gaussian.stack_no")["id"]
    for index in range(0, len(tab_frames), 3):
        down, up, settled = tab_frames[index:index + 3]
        assert down["requested_events"] == [{"type": "keys", "keys": [{"name": "Tab", "down": True}]}]
        assert up["requested_events"] == [{"type": "keys", "keys": [{"name": "Tab", "down": False}]}]
        assert settled["requested_events"] == []
        for frame in (down, up, settled):
            no = _control(frame["rendered"], "gaussian.stack_no")
            assert no["id"] == no_id and no["enabled"] is True
        no = _control(settled["rendered"], "gaussian.stack_no")
        focused = no["focused"] and settled["rendered"]["input_frame"]["focus"]["nav_id"] == no_id
        assert focused is (index + 3 == len(tab_frames)), "Physical Tab must stop at the first observed No focus"
    focus_no = _frame(row, "eager.current_no_focused")
    assert focus_no["ordinal"] == tab_frames[-1]["ordinal"] + 1 and focus_no["requested_events"] == []
    no = _control(focus_no["rendered"], "gaussian.stack_no")
    assert no["focused"] is True and no["enabled"] is True and no["id"] == no_id
    assert focus_no["rendered"]["input_frame"]["focus"]["nav_id"] == no_id
    for label in ("eager.current_no", "eager.stack_yes"):
        frame = _frame(row, label + ".down")
        assert frame["requested_events"] == [{"type": "keys", "keys": [{"name": "Enter", "down": True}]}]
        assert _frame(row, label + ".up")["requested_events"] == [
            {"type": "keys", "keys": [{"name": "Enter", "down": False}]}]
    assert _frame(row, "eager.current_no.down")["ordinal"] == focus_no["ordinal"] + 1
    final, rendered = _state(row, "scientific_tab")
    assert final["active_document_id"] == row["source_document_id"]
    assert rendered["gaussian_panel"]["open"] is False
    _same_pixels(stacked, _document(final, eager_id))
    _no_resources(final)
    _require_selectors(row, "menu.command.gaussian_blur_imagej", "menu.command.gaussian_blur", "menu.action.fill",
                       "tools.tool.brush", "document." + str(eager_id), "document." + str(row["source_document_id"]))
    for observation in row["control_observations"]:
        if observation["selector"] in {"menu.action.fill", "tools.tool.brush"}:
            assert observation["disabled_attempt"] is True


def _assert_case(row, case_id, context, directory):
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert row["limits"] == {"max_frames": 180, "max_seconds": 15, "max_report_bytes": MAX_REPORT,
                              "max_archive_bytes": MAX_ARCHIVE, "max_documents": 16, "max_shape_tczyxs": SHAPE}
    assert 0 <= row["elapsed_seconds"] <= 15
    assert type(row["source_document_id"]) is int and row["source_document_id"] > 0
    _physical(row)
    _initial(row, case_id)
    if case_id == "D01":
        _current(row)
        _refused_api(row, "inactive_explicit_owner")
    elif case_id == "D02":
        _mixed(row)
    elif case_id in {"D03", "D03M", "D03E"}:
        _roi(row, case_id)
    elif case_id == "D04":
        _drafts(row)
    elif case_id == "D04X":
        _xyz(row)
    elif case_id == "D05":
        _failure(row)
    elif case_id == "D05T":
        _source_lifetime(row)
    elif case_id == "D06":
        _replay(row, directory)
    else:
        _eager(row)
    assert row["window_error"] == ""
    assert row["final_usage"] == {"running": 0, "queued": 0, "running_bytes": 0, "retained_bytes": 0, "reserved_bytes": 0}


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_scientific_operation_targets(case_id, context, request, tmp_path):
    row, directory = _observe(case_id, context, tmp_path / "observations", request)
    _assert_case(row, case_id, context, directory)
