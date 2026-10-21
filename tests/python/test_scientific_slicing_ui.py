"""Independent physical P06e readers for both installed Dear ImGui launches.

Samples, affine/time mappings, command derivations and session payloads are
computed from literal fixture indices below. No product extractor, serializer,
command resolver or producer success label supplies an expected value.
"""

import base64
from functools import lru_cache
import hashlib
import importlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import struct
import subprocess
import sysconfig
import time
import zlib

import pytest


CONTRACT = "p06e.scientific-slicing-ui.001"
CASES = tuple(f"E{index:02d}" for index in range(1, 11))
CONTEXTS = ("imgui_cpp", "imgui_python")
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 32 * 1024 * 1024
CAP = 512 * 1024 * 1024
SHAPE = [2, 3, 4, 5, 7, 1]
AXES = {"XY": (0, 1, 2), "XZ": (0, 2, 1), "YZ": (1, 2, 0)}
CALIBRATION = {
    "spacing": [.5, 1.25, 2.5], "origin": [11., -7., 19.],
    "direction": [-1., .25, .5, 0., 1., .5, .5, 0., 1.],
    "unit": "um", "frame_interval": .75, "time_origin": 2.25, "time_unit": "s",
}
REBOUND_CALIBRATION = dict(CALIBRATION, origin=[-13., 5., 7.],
                          spacing=[2., 3., 4.], time_origin=31.)
TYPE_CODES = {"uint8": "B", "uint16": "H", "float32": "f", "float64": "d"}
ZERO_USAGE = dict(running=0, queued=0, running_bytes=0, retained_bytes=0, reserved_bytes=0)
PHYSICAL_KEYS = {"Ctrl", "Shift", "Alt", "Super", "A", "W", "Enter", "Escape", "Tab",
                 "Left", "Right", "Up", "Down", "Backspace", "Delete", "Space",
                 "Home", "End", "PageUp", "PageDown"}

# Literal successful choices, independent of producer request/command echoes.
# (operation, plane, first/coordinate, last, step, T, C, dtype, S)
EXPECTED_OUTPUTS = {
    "E01": {"plane.xy": ("plane", "XY", 2, 2, 1, 1, 2, "float32", 1)},
    "E02": {"plane.xz": ("plane", "XZ", 2, 2, 1, 1, 2, "uint16", 3),
            "plane.yz": ("plane", "YZ", 4, 4, 1, 1, 2, "float64", 4)},
    "E03": {"range.xy": ("range", "XY", 0, 3, 2, 1, 2, "float32", 1),
            "range.xz": ("range", "XZ", 1, 4, 2, 1, 2, "float32", 1),
            "range.yz": ("range", "YZ", 1, 6, 2, 1, 2, "float32", 1)},
    "E04": {"captured.xy": ("plane", "XY", 2, 2, 1, 1, 2, "float32", 1)},
    "E05": {"recovered.range": ("range", "XZ", 1, 4, 2, 1, 2, "float32", 1)},
    "E06": {},
    "E07": {"committed.plane": ("plane", "XY", 2, 2, 1, 1, 2, "float32", 1),
            "committed.range": ("range", "XZ", 1, 4, 2, 1, 2, "float32", 1)},
    "E08": {"saved.plane": ("plane", "XY", 2, 2, 1, 1, 2, "float32", 1),
            "eager.range": ("range", "XZ", 1, 4, 2, 0, 0, "uint16", 1)},
    "E09": {"original.plane": ("plane", "XY", 2, 2, 1, 1, 2, "float32", 1),
            "original.range": ("range", "XZ", 1, 4, 2, 1, 2, "float32", 1),
            "replay.plane": ("plane", "XY", 2, 2, 1, 1, 2, "float32", 1),
            "replay.range": ("range", "XZ", 1, 4, 2, 1, 2, "float32", 1)},
    "E10": {"rebind.plane": ("plane", "YZ", 3, 3, 1, 1, 1, "float32", 1),
            "rebind.range": ("range", "XY", 0, 3, 2, 1, 1, "float32", 1)},
}


def _unique(pairs):
    result = {}
    for name, value in pairs:
        assert name not in result, f"Duplicate observation/record key: {name}"
        result[name] = value
    return result


def _nonfinite(value):
    raise AssertionError(f"Nonfinite observation JSON constant: {value}")


def _decode(raw, maximum=MAX_REPORT):
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    assert isinstance(raw, bytes) and 0 < len(raw) <= maximum
    return json.loads(raw, object_pairs_hook=_unique, parse_constant=_nonfinite)


def _read(path, maximum=MAX_REPORT):
    assert path.is_file() and not path.is_symlink()
    assert 0 < path.stat().st_size <= maximum
    return _decode(path.read_bytes(), maximum)


def _integer(value, *, minimum=0):
    assert type(value) is int and value >= minimum
    return value


def _number(value):
    assert type(value) in {int, float} and math.isfinite(value)
    return value


def _typed(value):
    """Compare unchanged state without hiding Boolean/int or signed-zero drift."""
    if isinstance(value, dict):
        return {name: _typed(item) for name, item in value.items()}
    if isinstance(value, list):
        return [_typed(item) for item in value]
    if type(value) is float:
        return ("float64", struct.pack("<d", value).hex())
    return (type(value).__name__, value)


def _close(actual, expected):
    if isinstance(expected, (list, tuple)):
        assert isinstance(actual, list) and len(actual) == len(expected)
        for value, target in zip(actual, expected):
            _close(value, target)
    elif isinstance(expected, str):
        assert actual == expected
    else:
        assert math.isclose(_number(actual), expected, rel_tol=1e-12, abs_tol=1e-12)


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


def _member(root, name):
    assert isinstance(name, str) and name and "\\" not in name and ":" not in name
    relative = PurePosixPath(name)
    assert not relative.is_absolute() and relative.as_posix() == name
    assert all(part not in {"", ".", ".."} for part in relative.parts)
    path = root.joinpath(*relative.parts)
    assert not path.is_symlink()
    resolved = path.resolve(strict=True)
    assert resolved.is_relative_to(root.resolve(strict=True)) and resolved.is_file()
    return resolved


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_CPP_EXE to the matching installed desktop")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed desktop")
    path = Path(configured).resolve(strict=True)
    assert path.is_file() and path.parent.name.lower() == "bin"
    assert path.name.lower() in {"imagejxx.exe", "imagejxx"}
    expected = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if _required():
        assert expected, "Required paired gate must pin IMAGEJXX_CPP_EXE_SHA256"
    if expected:
        assert len(expected) == 64
        with path.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == expected
    return path


@pytest.fixture(scope="module")
def standalone_cases(installed_cpp_executable):
    configured = os.environ.get("IMAGEJXX_SCIENTIFIC_SLICING_CPP_REPORT")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_SCIENTIFIC_SLICING_CPP_REPORT to the path-only installed case index")
        return None
    index_path = Path(configured).resolve(strict=True)
    index = _read(index_path)
    assert index["contract_id"] == CONTRACT and index["context"] == "imgui_cpp"
    entries = index["cases"]
    assert isinstance(entries, list) and len(entries) == len(CASES)
    paths = {}
    for entry in entries:
        case_id = entry["case_id"]
        assert case_id in CASES and case_id not in paths
        assert entry["id"] == case_id
        paths[case_id] = _member(index_path.parent, entry["relative_path"])
    assert set(paths) == set(CASES)
    return paths


def _observe(case_id, context, directory, request):
    assert not directory.exists()
    started = time.monotonic()
    if context == "imgui_cpp":
        retained = request.getfixturevalue("standalone_cases")
        if retained is not None:
            path = retained[case_id]
            return _read(path), path.parent
        completed = subprocess.run(
            [str(request.getfixturevalue("installed_cpp_executable")),
             "--scientific-slicing-ui-case", case_id, str(directory)],
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
                     for key in ("purelib", "platlib")}
            assert any(module.is_relative_to(root) for root in roots), "Checkout desktop import"
        editor = desktop.Editor(preferences_path=str(directory.parent / "preferences.json"))
        row = _decode(editor._scientific_slicing_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row, directory


def _frame(row, label):
    frames = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(frames) == 1, f"Missing/duplicate physical frame: {label}"
    return frames[0]


def _control(rendered, selector):
    if selector.startswith("dialog."):
        return rendered["dialog"]["controls"][selector.removeprefix("dialog.")]
    if selector.startswith("document."):
        matches = [tab for tab in rendered["document_tabs"]
                   if tab["document_id"] == int(selector.removeprefix("document."))]
        assert len(matches) == 1
        return matches[0]
    if selector.startswith("gaussian."):
        key = selector.removeprefix("gaussian.")
        panel = rendered["gaussian_panel"]
        return dict(panel["control_states"][key], point=panel["controls"][key],
                    rect=panel["control_rects"][key])
    if selector == "error_modal.ok":
        return rendered["error_modal"]["controls"]["ok"]
    return rendered["host_controls"][selector]


def _physical(row):
    frames = row["frames"]
    assert 1 <= row["frame_count"] == len(frames) <= 180
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    sessions, held, buttons, position = [], set(), [False] * 3, None
    for frame in frames:
        rendered = frame["rendered"]
        session, actual = rendered["session"], rendered["input_frame"]
        assert session["open"] is True and _integer(session["context_id"], minimum=1)
        assert session["context_id"] == session["current_context_id"]
        sessions.append(session)
        assert actual["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            kind = event["type"]
            if kind == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert len(event["point"]) == 2
                position = [_number(value) for value in event["point"]]
            elif kind == "button":
                assert set(event) == {"type", "name", "down"}
                assert event["name"] in {"left", "middle", "right"} and type(event["down"]) is bool
                buttons[("left", "right", "middle").index(event["name"])] = event["down"]
            elif kind in {"key", "keys"}:
                atoms = [dict(name=event["name"], down=event["down"])] if kind == "key" else event["keys"]
                assert atoms and len({atom["name"] for atom in atoms}) == len(atoms)
                for atom in atoms:
                    assert set(atom) == {"name", "down"} and atom["name"] in PHYSICAL_KEYS
                    assert type(atom["down"]) is bool
                    (held.add if atom["down"] else held.discard)(atom["name"])
            elif kind == "text":
                assert set(event) == {"type", "value"} and isinstance(event["value"], str)
                assert "\x00" not in event["value"]
                assert actual["input_text"]["id"] == actual["focus"]["active_id"] != 0
            elif kind == "wheel":
                assert set(event) == {"type", "x", "y"}
                _number(event["x"])
                _number(event["y"])
            else:
                raise AssertionError(f"Undeclared physical event: {kind}")
        if position is not None:
            assert actual["mouse_position"] == position
        assert actual["mouse_down"] == buttons and set(actual["keys_down"]) == held
        assert actual["modifiers"] == {"ctrl": "Ctrl" in held, "shift": "Shift" in held,
                                       "alt": "Alt" in held, "super": "Super" in held}
    assert not held and buttons == [False] * 3
    assert len({value["context_id"] for value in sessions}) == 1
    assert len({value["session_id"] for value in sessions}) == 1
    for previous, current in zip(sessions, sessions[1:]):
        assert current["imgui_frame"] == previous["imgui_frame"] + 1
        assert current["imgui_time"] > previous["imgui_time"]
    click_frames = set()
    for observed in row["control_observations"]:
        ordinal = _integer(observed["before_frame_ordinal"], minimum=1)
        assert ordinal + 3 <= len(frames)
        control = _control(frames[ordinal - 1]["rendered"], observed["selector"])
        assert control == observed["control"] and _integer(control["id"], minimum=1)
        assert type(control["enabled"]) is bool
        assert control["enabled"] is (not observed["disabled_attempt"])
        point, rect = observed["position"], control["rect"]
        assert len(rect) == 4 and len(point) == 2
        for value in rect + point:
            _number(value)
        assert point == [math.floor(value) for value in control["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        affected = set(range(ordinal + 1, ordinal + 4))
        assert not click_frames.intersection(affected)
        click_frames.update(affected)
    for frame in frames:
        if any(event["type"] == "button" for event in frame["requested_events"]):
            assert frame["ordinal"] in click_frames
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0


def _state(row, label):
    state = row["states"][label]
    frame = row["frames"][state["frame_ordinal"] - 1]
    assert frame["label"] == label
    assert state["document_ids"] == [document["document_id"] for document in state["documents"]]
    assert len(set(state["document_ids"])) == len(state["document_ids"]) <= 16
    assert state["active_document_id"] in state["document_ids"] or state["active_document_id"] == 0
    assert type(state["recording"]) is bool
    return state, frame["rendered"]


def _owner(state, document_id):
    matches = [document for document in state["documents"] if document["document_id"] == document_id]
    assert len(matches) == 1
    return matches[0]


def _plane_rows(document):
    shape, dtype = document["shape"], document["dtype"]
    assert len(shape) == 6 and all(type(value) is int and value > 0 for value in shape)
    assert shape[-1] in {1, 3, 4} and dtype in TYPE_CODES
    expected_positions = [(t, c, z) for t in range(shape[0])
                          for c in range(shape[1]) for z in range(shape[2])]
    assert len(document["planes"]) == len(expected_positions)
    rows = {}
    count = math.prod(shape[3:])
    for expected, plane in zip(expected_positions, document["planes"]):
        assert plane["position"] == list(expected)
        assert len(bytes.fromhex(plane["storage_f64_hex"])) == count * 8
        assert len(bytes.fromhex(plane["declared_hex"])) == count * struct.calcsize(TYPE_CODES[dtype])
        for name in ("image_address", "buffer_address"):
            assert isinstance(plane[name], str) and plane[name].startswith("0x") and int(plane[name], 16) > 0
        rows[expected] = plane
    return rows


def _sample_bytes(t, c, z, y, x, sample, dtype, policy):
    if policy == "preserve_quiet_nan_float32" and (t, c, z, y, x) == (1, 2, 2, 2, 3):
        assert dtype == "float32" and sample == 0
        return struct.pack("<I", 0x7FC12345), struct.pack("<Q", 0x7FF82468A0000000)
    if policy == "preserve_quiet_nan_float32" and (t, c, z, y, x) == (1, 2, 2, 3, 4):
        assert dtype == "float32" and sample == 0
        return struct.pack("<I", 0x80000000), struct.pack("<Q", 0x8000000000000000)
    value = 64*t + 24*c + 8*z + 3*y + x + 2*sample
    return struct.pack("<" + TYPE_CODES[dtype], value), struct.pack("<d", value)


@lru_cache(maxsize=128)
def _source_plane(t, c, z, y_size, x_size, samples, dtype, policy):
    typed, backing = [], []
    for y in range(y_size):
        for x in range(x_size):
            for sample in range(samples):
                a, b = _sample_bytes(t, c, z, y, x, sample, dtype, policy)
                typed.append(a)
                backing.append(b)
    return b"".join(typed), b"".join(backing)


def _literal(document, *, calibration=CALIBRATION):
    shape, dtype, policy = document["shape"], document["dtype"], document["sample_policy"]
    assert shape[:5] in ([2, 3, 4, 5, 7], [1, 1, 4, 5, 7])
    assert policy in {"finite_only", "preserve_quiet_nan_float32"}
    if policy != "finite_only":
        assert dtype == "float32" and shape[-1] == 1
    assert document["calibration"] == calibration
    for position, plane in _plane_rows(document).items():
        typed, backing = _source_plane(*position, *shape[3:], dtype, policy)
        assert plane["declared_hex"] == typed.hex()
        assert plane["storage_f64_hex"] == backing.hex()


def _unchanged(before, after, *, display=False):
    for name in ("document_id", "shape", "dtype", "sample_policy", "calibration", "metadata",
                 "selection", "previous_selection", "rois", "overlays", "spectrum_present",
                 "data_identity", "dirty", "undo_steps", "redo_steps", "history", "planes"):
        assert _typed(after[name]) == _typed(before[name]), name
    for name in ("pixels", "metadata", "selection"):
        assert after["revisions"][name] == before["revisions"][name]
    if display:
        assert _typed(after["display"]) == _typed(before["display"])
        assert after["revisions"]["display"] == before["revisions"]["display"]
    else:
        for name in ("zoom", "linked_zoom", "plane_zoom", "pan", "orthogonal", "properties"):
            assert _typed(after["display"][name]) == _typed(before["display"][name]), name


def _world(calibration, xyz):
    return [calibration["origin"][row] + sum(
        calibration["direction"][3*row + axis] * calibration["spacing"][axis] * xyz[axis]
        for axis in range(3)) for row in range(3)]


def _derived(source, request, operation):
    plane = request["plane"]
    axes, shape, original = AXES[plane], source["shape"], source["calibration"]
    extents = (shape[4], shape[3], shape[2])
    first = request["coordinate"] if operation == "axis_plane_export" else request["first"]
    step = 1 if operation == "axis_plane_export" else request["step"]
    count = 1 if operation == "axis_plane_export" else 1 + (request["last"] - first) // step
    expected_shape = [1, 1, count, extents[axes[1]], extents[axes[0]], shape[5]]
    fixed = [0, 0, 0]
    fixed[axes[2]] = first
    calibration = {
        "spacing": [original["spacing"][axes[0]], original["spacing"][axes[1]],
                    original["spacing"][axes[2]]*step],
        "origin": _world(original, fixed),
        "direction": [original["direction"][3*row + axis] for row in range(3) for axis in axes],
        "unit": original["unit"], "time_unit": original["time_unit"],
        "frame_interval": original["frame_interval"],
        "time_origin": original["time_origin"] + request["t"]*original["frame_interval"],
    }
    geometry = {
        "origin": _world(original, fixed),
        "horizontal_step": [original["direction"][3*row + axes[0]]*original["spacing"][axes[0]] for row in range(3)],
        "vertical_step": [original["direction"][3*row + axes[1]]*original["spacing"][axes[1]] for row in range(3)],
        "horizontal_axis": "XYZ"[axes[0]], "vertical_axis": "XYZ"[axes[1]], "unit": original["unit"],
    }
    working = math.prod(expected_shape)*8 + shape[3]*shape[4]*shape[5]*8
    if plane != "XY":
        working += expected_shape[4]*shape[5]*8
    return expected_shape, calibration, geometry, axes, working


def _request(spec):
    kind, plane, first, last, step, t, c, _, _ = spec
    result = dict(plane=plane, t=t, c=c, max_working_bytes=CAP)
    if kind == "plane":
        result["coordinate"] = first
    else:
        result.update(first=first, last=last, step=step)
    return result


def _command(record, source, request, operation):
    shape, calibration, geometry, axes, working = _derived(source, request, operation)
    assert set(record) == {"schema", "version", "operation", "source", "request", "derived", "destination"}
    assert record["schema"] == "imagejxx.axis-extraction-command" and type(record["version"]) is int
    assert record["version"] == 1 and record["operation"] == operation
    assert record["destination"] == "new_document"
    assert record["request"] == request
    assert all(type(value) is int for name, value in record["request"].items() if name != "plane")
    assert record["source"] == {
        "document_id": source["document_id"], "shape": source["shape"],
        "pixel_type": source["dtype"], "sample_policy": source["sample_policy"],
        "revisions": {name: source["revisions"][name] for name in ("pixels", "metadata")},
        "calibration": source["calibration"],
    }
    expected = dict(shape=shape, calibration=calibration, geometry=geometry,
                    source_axes=list(axes), plane_name=request["plane"], working_bytes=working)
    assert set(record["derived"]) == set(expected)
    assert all(type(value) is int for value in record["derived"]["shape"])
    assert all(type(value) is int for value in record["derived"]["source_axes"])
    assert type(record["derived"]["working_bytes"]) is int
    for name, value in expected.items():
        if name in {"calibration", "geometry"}:
            assert set(record["derived"][name]) == set(value)
            for key, item in value.items():
                _close(record["derived"][name][key], item)
        else:
            assert record["derived"][name] == value


def _output(row, entry, spec, *, physical=True):
    source, output = entry["source_before"], entry["output"]
    kind, plane, first, last, step, t, c, dtype, samples = spec
    operation = "axis_plane_export" if kind == "plane" else "axis_range_reslice"
    request = _request(spec)
    assert entry["operation"] == operation and entry["request"] == request
    assert entry["trigger"] == ("physical" if physical else "api")
    assert 0 < _integer(entry["effective_max_working_bytes"], minimum=1) <= CAP
    calibration = REBOUND_CALIBRATION if row["case_id"] == "E10" else CALIBRATION
    _literal(source, calibration=calibration)
    assert source["dtype"] == dtype and source["shape"][-1] == samples
    _unchanged(source, entry["source_after"])
    expected_shape, expected_calibration, _, axes, _ = _derived(source, request, operation)
    assert output["shape"] == expected_shape and output["dtype"] == dtype
    assert output["sample_policy"] == source["sample_policy"]
    assert output["document_id"] != source["document_id"]
    assert output["data_identity"] != source["data_identity"]
    for key, expected in expected_calibration.items():
        _close(output["calibration"][key], expected)
    assert set(output["calibration"]) == set(expected_calibration)
    for (ot, oc, oz), observed in _plane_rows(output).items():
        assert ot == oc == 0
        typed, backing = [], []
        for v in range(expected_shape[3]):
            for u in range(expected_shape[4]):
                xyz = [0, 0, 0]
                xyz[axes[0]], xyz[axes[1]], xyz[axes[2]] = u, v, first + oz*step
                for sample in range(samples):
                    a, b = _sample_bytes(t, c, xyz[2], xyz[1], xyz[0], sample, dtype, source["sample_policy"])
                    typed.append(a)
                    backing.append(b)
                # Independently evaluate source and output affine landmarks.
                _close(_world(output["calibration"], [u, v, oz]), _world(calibration, xyz))
        assert observed["declared_hex"] == b"".join(typed).hex()
        assert observed["storage_f64_hex"] == b"".join(backing).hex()
    assert output["selection"] is None and output["previous_selection"] is None
    assert output["rois"] == output["overlays"] == [] and output["spectrum_present"] is False
    assert output["dirty"] is False and output["undo_steps"] == output["redo_steps"] == 0
    assert output["history"]["undo_steps"] == output["history"]["redo_steps"] == 0
    assert all(output["revisions"][name] == 0 for name in ("pixels", "metadata", "selection"))
    assert output["display"]["t"] == output["display"]["c"] == 0
    assert all(0 <= coordinate < extent for coordinate, extent in
               zip(output["display"]["crosshair"], (expected_shape[4], expected_shape[3], expected_shape[2])))
    assert output["metadata"]["properties"] == output["metadata"]["record_layout"] == {}
    assert set(output["metadata"]["extensions"]) == {operation}
    provenance = output["metadata"]["extensions"][operation]
    assert provenance["schema_version"] == 1 and provenance["source_document_id"] == source["document_id"]
    assert provenance["plane"] == plane and provenance["t"] == t and provenance["c"] == c
    assert provenance["horizontal_axis"] == "XYZ"[axes[0]]
    assert provenance["vertical_axis"] == "XYZ"[axes[1]]
    assert provenance["fixed_axis"] == "XYZ"[axes[2]]
    assert provenance["index_convention"] == "zero_based_voxel_centers"
    if kind == "plane":
        assert provenance["coordinate"] == first
        assert output["metadata"]["title"] == source["metadata"]["title"] + f" {plane} {first+1}"
    else:
        for key, value in dict(first=first, last=last, step=step, count=expected_shape[2]).items():
            assert provenance[key] == value
        assert output["metadata"]["title"] == source["metadata"]["title"] + f" {plane} {first+1}-{last+1} step {step}"
    record = entry["command_record"]
    _command(record, source, request, operation)
    before, after = entry["state_before"], entry["state_after"]
    assert after["document_ids"] == before["document_ids"] + [output["document_id"]]
    assert before["facade_ids"] == before["document_ids"]
    assert after["facade_ids"] == after["document_ids"]
    assert after["active_document_id"] == output["document_id"]
    assert after["results"] == before["results"] and after["recording"] is before["recording"]
    expected_workflow = before["workflow"] + ([record] if before["recording"] else [])
    assert _typed(after["workflow"]) == _typed(expected_workflow)
    _unchanged(_owner(before, source["document_id"]), _owner(after, source["document_id"]), display=True)
    if physical:
        ordinal = _integer(entry["confirm_frame_ordinal"], minimum=1)
        assert ordinal <= len(row["frames"])
        confirms = [item for item in row["control_observations"]
                    if item["before_frame_ordinal"] + 3 == ordinal]
        assert len(confirms) == 1 and confirms[0]["disabled_attempt"] is False
        selector = confirms[0]["selector"]
        assert selector == "dialog.apply" or selector.endswith(".replay")
        assert row["frames"][ordinal - 1]["native_state"]["active_document_id"] == output["document_id"]
    else:
        assert entry["confirm_frame_ordinal"] is None
        assert entry["api_label"] == "recording_off_strict_replay"
    return output


def _session(row, directory, key):
    descriptor = row["files"][key]
    assert Path(descriptor["member"]).name == descriptor["member"]
    raw = _member(directory, descriptor["member"]).read_bytes()
    assert 0 < len(raw) == descriptor["byte_count"] <= MAX_ARCHIVE
    assert raw.hex() == descriptor["hex"] and raw.startswith(b"IMAGEJXX3\n")
    declared, = struct.unpack("<Q", raw[10:18])
    assert 0 < declared <= MAX_ARCHIVE
    stream = zlib.decompressobj()
    plain = stream.decompress(raw[18:], declared + 1)
    assert len(plain) == declared and stream.eof and not stream.unused_data and not stream.unconsumed_tail
    value = _decode(plain, MAX_ARCHIVE)
    assert set(value) == {"format", "version", "documents", "results", "workflow", "extensions"}
    assert value["format"] == "imagejxx-session" and value["version"] == 3
    return value


def _session_selection_expected(observed):
    """Map the declared desktop selection representation to native V3 fields.

    SESSION_FORMAT.md requires a nested attributes object, including {}. The
    desktop observation flattens those same attributes beside geometry. Retain
    every observed extension/style field in that object; never discard unknown
    fields or normalize geometry, packed masks or nonempty attribute values.
    """
    if observed is None:
        return None
    assert isinstance(observed, dict)
    geometry = {"kind", "points", "width"}
    if observed["kind"] == "mask":
        geometry |= {"shape", "mask"}
    assert geometry <= observed.keys()
    expected = {name: observed[name] for name in geometry}
    expected["attributes"] = {name: value for name, value in observed.items()
                              if name not in geometry}
    return expected


def _session_document(record, document):
    assert record["axes"] == "TCZYXS" and record["shape"] == document["shape"]
    assert record["pixel_type"] == document["dtype"] and record["sample_policy"] == document["sample_policy"]
    assert record["storage"] == "Float64LE"
    expected = [bytes.fromhex(plane["storage_f64_hex"]) for plane in _plane_rows(document).values()]
    assert len(record["planes"]) == len(expected)
    assert [base64.b64decode(value, validate=True) for value in record["planes"]] == expected
    for key in ("calibration", "metadata", "overlays"):
        assert _typed(record[key]) == _typed(document[key])
    for key in ("selection", "previous_selection"):
        assert _typed(record[key]) == _typed(_session_selection_expected(document[key]))
    expected_rois = [dict(roi, selection=_session_selection_expected(roi["selection"]))
                     for roi in document["rois"]]
    assert _typed(record["rois"]) == _typed(expected_rois)
    assert record["fourier"] is None
    display = {key: value for key, value in document["display"].items() if key != "revision"}
    assert _typed(record["display"]) == _typed(display)


def _selectors(row, *selectors):
    actual = {item["selector"] for item in row["control_observations"] if not item["disabled_attempt"]}
    assert set(selectors) <= actual


def _api(row, name):
    matches = [item for item in row["api_observations"] if item["name"] == name]
    assert len(matches) == 1, f"Missing/duplicate explicitly labeled API observation: {name}"
    value = matches[0]
    assert "API" in value["role"] and "not physical" in value["role"]
    return value


def _refused(row, name):
    value = _api(row, name)
    assert value["accepted"] is False and value["error"] and value["error_type"]
    before, after = value["before"], value["after"]
    for key in ("document_ids", "active_document_id", "workflow", "results", "recording"):
        assert _typed(after[key]) == _typed(before[key]), key
    for document in before["documents"]:
        _unchanged(document, _owner(after, document["document_id"]), display=True)
    return value


def _publications(row):
    observations = row["publication_observations"]
    assert len(observations) == 2 * len(row["outputs"])
    for entry in row["outputs"]:
        pair = [value for value in observations if value["label"] == entry["label"]]
        assert [value["phase"] for value in pair] == ["before_native_publish", "after_attach"]
        before, after = pair
        assert before["request"] == after["request"] == entry["request"]
        assert _typed(before["source"]) == _typed(after["source"]) == _typed(entry["source_before"])
        candidate, output = before["candidate"], entry["output"]
        assert candidate["document_id"] == output["document_id"]
        assert candidate["document_id"] not in entry["state_before"]["document_ids"]
        assert candidate["document_id"] not in entry["state_before"]["facade_ids"]
        for key in ("shape", "dtype", "sample_policy", "calibration", "metadata", "selection",
                    "previous_selection", "rois", "overlays", "spectrum_present", "planes", "data_identity"):
            assert _typed(candidate[key]) == _typed(output[key]), key
        assert _typed(after["candidate"]) == _typed(output)
        if entry["trigger"] == "physical":
            assert before["frame_ordinal"] == after["frame_ordinal"] == entry["confirm_frame_ordinal"]


def _entry(row, label):
    matches = [entry for entry in row["outputs"] if entry["label"] == label]
    assert len(matches) == 1
    return matches[0]


def _draft(row, label, *, rebind=False):
    entry = _entry(row, label)
    state, rendered = _state(row, label + ".draft")
    source, request = entry["source_before"], entry["request"]
    dialog = rendered["dialog"]
    assert dialog["open"] is True and dialog["is_operation"] is False
    assert dialog["opened_document_id"] == source["document_id"] == state["active_document_id"]
    assert dialog["opened_document_id_semantics"] == "captured_native_execution_source"
    assert type(dialog["captured_t"]) is type(dialog["captured_c"]) is int
    assert dialog["captured_t"] == request["t"] and dialog["captured_c"] == request["c"]
    assert dialog["captured_channel_label"] == f"Channel {request['c']+1} / {source['shape'][1]}"
    assert dialog["captured_time_label"] == f"Time point {request['t']+1} / {source['shape'][0]}"
    assert dialog["is_extraction_rebind"] is rebind
    assert dialog["values"]["plane"] == request["plane"]
    if entry["operation"] == "axis_plane_export":
        assert dialog["title"] == ("Rebind plane export" if rebind else "Export plane")
        assert dialog["text"]["index"] == str(request["coordinate"] + 1)
        assert {field["name"] for field in dialog["schema"]} == {"plane", "index"}
        axis = "XYZ"[AXES[request["plane"]][2]]
        extent = (source["shape"][4], source["shape"][3], source["shape"][2])[AXES[request["plane"]][2]]
        assert dialog["axis_index_label"] == f"{axis} index: 1 to {extent}"
    else:
        assert dialog["title"] == ("Rebind reslice range" if rebind else "Reslice range")
        assert {field["name"] for field in dialog["schema"]} == {"plane", "first", "last", "step"}
        assert dialog["text"]["first"] == str(request["first"] + 1)
        assert dialog["text"]["last"] == str(request["last"] + 1)
        assert dialog["text"]["step"] == str(request["step"])
        assert dialog["emitted_plane_count"] == 1 + (request["last"] - request["first"]) // request["step"]
        assert dialog["retained_source"] is True
    assert dialog["controls"]["apply"]["enabled"] is True
    assert rendered["input_frame"]["popup_stack"]
    if rebind:
        original = row["original_records"][0 if entry["operation"] == "axis_plane_export" else 1]
        assert _typed(dialog["rebind_record"]) == _typed(original)
    else:
        assert dialog["rebind_record"] is None
    return state, rendered


def _fixture_sources(row):
    initial, _ = _state(row, "initial")
    assert initial["recording"] is True and initial["workflow"] == initial["results"] == []
    assert initial["facade_ids"] == initial["document_ids"]
    for label, document in row["fixture_sources"].items():
        _literal(document)
        assert document["metadata"]["title"] == f"Scientific slicing {row['case_id']} {label}"
        properties = document["metadata"]["properties"]
        assert properties["source_only"] == {"nested": [None, True, 7, "retained"]}
        assert properties["revision_probe"] == 7 and "history_probe" not in properties
        assert document["metadata"]["record_layout"] == {"source_presence": True}
        assert document["metadata"]["extensions"] == {"future_annotation": {"version": 37, "opaque": [3, 1, 4]}}
        assert document["selection"] == {"kind": "rectangle", "points": [[1, 1], [4, 3]], "width": 1}
        assert document["previous_selection"] == {"kind": "line", "points": [[0, 0], [2, 2]], "width": 1}
        assert document["overlays"] == [{"name": "source overlay", "kind": "point", "points": [[6., 4.]]}]
        assert document["spectrum_present"] is False
        assert document["dirty"] is False and document["undo_steps"] == document["redo_steps"] == 1
        if document["shape"][0] > 1:
            assert document["rois"] == [
                {"name": "explicit zero", "selection": {"kind": "rectangle", "points": [[1, 1], [3, 3]], "width": 1},
                 "t": 0, "c": 0, "z": 0, "attributes": {}},
                {"name": "wildcard", "selection": {"kind": "point", "points": [[2, 2]], "width": 1},
                 "t": None, "c": None, "z": 1, "attributes": {}},
            ]
        else:
            assert document["rois"] == []
        _unchanged(document, _owner(initial, document["document_id"]), display=True)
    assert row["fixture_sources"]["source"]["document_id"] == row["source_document_id"]


def _navigation(row, label="navigated", *, t=1, c=2, document_id=None):
    state, _ = _state(row, label)
    document_id = document_id or row["source_document_id"]
    document = _owner(state, document_id)
    assert state["active_document_id"] == document_id
    assert document["display"]["t"] == t and document["display"]["c"] == c
    _selectors(row, "navigation.channel", "navigation.time")
    return state, document


def _no_publication(first, second, *, display=True):
    for key in ("document_ids", "facade_ids", "active_document_id", "results", "workflow", "recording"):
        assert _typed(second[key]) == _typed(first[key]), key
    for source in first["documents"]:
        _unchanged(source, _owner(second, source["document_id"]), display=display)


def _ready_preview(row, label):
    state, rendered = _state(row, label)
    observations = [value for value in row["preview_observations"] if value["label"] == label]
    assert len(observations) == 1
    observation = observations[0]
    jobs = rendered["gaussian_jobs"]
    assert observation["gaussian_jobs"]["active_installed_generation"] == jobs["active_installed_generation"]
    assert type(jobs["active_installed_generation"]) is int and jobs["active_installed_generation"] > 0
    assert jobs["preview"]["owns_job"] is True and jobs["preview"]["source_id"] == row["source_document_id"]
    assert jobs["preview"]["job"]["state"] == "ready"
    assert jobs["preview"]["job"]["id"] == observation["job_id"]
    assert state["busy"] is False and state["usage"]["retained_bytes"] > 0
    assert state["usage"]["reserved_bytes"] >= state["usage"]["retained_bytes"]
    assert observation["shape"] == SHAPE and observation["calibration"] == CALIBRATION
    live = _owner(state, row["source_document_id"])
    _literal(live)
    committed = {tuple(value["position"]): value for value in observation["committed_planes"]}
    staged = {tuple(value["position"]): value for value in observation["planes"]}
    assert committed.keys() == staged.keys() and committed
    assert all(position[:2] == (1, 2) for position in committed)
    live_rows = _plane_rows(live)
    changed = set()
    for position, plane in staged.items():
        assert committed[position] == live_rows[position]
        assert len(bytes.fromhex(plane["storage_f64_hex"])) == 5*7*8
        values = struct.unpack("<35d", bytes.fromhex(plane["storage_f64_hex"]))
        assert all(math.isfinite(value) for value in values)
        assert plane["declared_hex"] == b"".join(struct.pack("<f", value) for value in values).hex()
        original = struct.unpack("<35d", bytes.fromhex(committed[position]["storage_f64_hex"]))
        for offset, (actual, expected) in enumerate(zip(values, original)):
            if actual != expected:
                changed.add((*position, offset // 7, offset % 7))
    assert changed, "READY label alone does not prove an actual nontrivial staged preview"
    return state, jobs, changed


def _assert_case(row, case_id, context, directory):
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert row["limits"]["max_frames"] == 180 and row["limits"]["max_seconds"] == 15
    assert row["limits"]["max_report_bytes"] == MAX_REPORT
    assert row["limits"]["max_archive_bytes"] == MAX_ARCHIVE
    assert row["limits"]["max_documents"] == 16
    assert 0 <= _number(row["elapsed_seconds"]) <= 15
    _physical(row)
    _fixture_sources(row)
    _integer(row["source_document_id"], minimum=1)
    outputs = [entry for entry in row["outputs"] if entry["trigger"] == "physical"]
    assert [entry["label"] for entry in outputs] == list(EXPECTED_OUTPUTS[case_id])
    for entry in outputs:
        _output(row, entry, EXPECTED_OUTPUTS[case_id][entry["label"]])
    api_outputs = [entry for entry in row["outputs"] if entry["trigger"] != "physical"]
    if case_id == "E10":
        assert len(api_outputs) == 1 and api_outputs[0]["label"] == "recording_off.api_replay"
        _output(row, api_outputs[0], EXPECTED_OUTPUTS[case_id]["rebind.plane"], physical=False)
    else:
        assert api_outputs == []
    _publications(row)
    _scenario(row, case_id, directory)
    assert row["window_error"] == "" and row["final_usage"] == ZERO_USAGE


def _scenario(row, case_id, directory):
    initial, _ = _state(row, "initial")
    source_id = row["source_document_id"]
    if case_id != "E10":
        navigated, source = _navigation(row)
        _unchanged(_owner(initial, source_id), source)
        assert source["display"]["crosshair"][2] > 0
        _selectors(row, "image.slice")
    for label in EXPECTED_OUTPUTS[case_id]:
        if not label.startswith("replay."):
            _draft(row, label, rebind=label.startswith("rebind."))
    if case_id == "E01":
        _selectors(row, "menu.action.show_axis_plane_export", "dialog.index", "dialog.apply")
    elif case_id == "E02":
        _selectors(row, "dialog.plane.choice.XZ", "dialog.plane.choice.YZ", "dialog.index")
        assert _entry(row, "plane.xz")["source_before"]["document_id"] != _entry(row, "plane.yz")["source_before"]["document_id"]
    elif case_id == "E03":
        _selectors(row, "dialog.plane.choice.XY", "dialog.plane.choice.XZ", "dialog.plane.choice.YZ",
                   "dialog.first", "dialog.last", "dialog.step", "dialog.apply")
        for entry in row["outputs"]:
            request = entry["request"]
            assert (request["last"] - request["first"]) % request["step"] != 0
    elif case_id == "E04":
        before, opened = _state(row, "captured.opened")
        assert opened["dialog"]["captured_t"] == 1 and opened["dialog"]["captured_c"] == 2
        stimulus = _api(row, "display_navigation_while_modal")
        assert stimulus["accepted"] is True and stimulus["inputs"]["t"] == stimulus["inputs"]["c"] == stimulus["inputs"]["z"] == 0
        _no_publication(stimulus["before"], stimulus["after"], display=False)
        blocked, rendered = _state(row, "captured.modal_blocked")
        live = _owner(blocked, source_id)
        assert [live["display"]["t"], live["display"]["c"], live["display"]["crosshair"][2]] == [0, 0, 0]
        assert rendered["dialog"]["open"] is True and blocked["active_document_id"] == source_id
        assert rendered["input_frame"]["popup_stack"]
        assert rendered["input_frame"]["focus"]["window"] == rendered["dialog"]["title"]
        assert rendered["dialog"]["captured_t"] == 1 and rendered["dialog"]["captured_c"] == 2
        _unchanged(_owner(before, source_id), live)
    elif case_id == "E05":
        for label, field, text in (("invalid.partial", "index", "-"), ("invalid.index", "index", "99"),
                                   ("invalid.range", "step", "0")):
            state, rendered = _state(row, label)
            assert rendered["dialog"]["open"] is True and rendered["dialog"]["text"][field] == text
            assert rendered["dialog"]["controls"]["apply"]["enabled"] is False
            _no_publication(navigated, state)
        cancelled, rendered = _state(row, "invalid.plane_cancelled")
        assert rendered["dialog"]["open"] is False
        _no_publication(navigated, cancelled)
        blur, rendered = _state(row, "recovered.blur")
        assert rendered["dialog"]["text"]["first"] == "2"
        assert rendered["dialog"]["controls"]["apply"]["enabled"] is True
        assert rendered["input_frame"]["focus"]["active_id"] != rendered["dialog"]["controls"]["first"]["id"]
        assert not any(frame["label"].startswith("recovered.first.commit") for frame in row["frames"])
        blur_text = _frame(row, "recovered.first.text")
        assert blur_text["requested_events"] == [{"type": "text", "value": "2"}]
        assert any(item["selector"] == "dialog.apply" and item["disabled_attempt"]
                   for item in row["control_observations"])
        _no_publication(navigated, blur)
    elif case_id == "E06":
        eager_id = row["fixture_sources"]["eager"]["document_id"]
        blocked, rendered = _state(row, "lifetime.modal_blocked")
        assert blocked["active_document_id"] == source_id and rendered["dialog"]["open"] is True
        assert rendered["input_frame"]["popup_stack"]
        assert rendered["input_frame"]["focus"]["window"] == rendered["dialog"]["title"]
        _no_publication(navigated, blocked)
        cancelled, rendered = _state(row, "lifetime.cancelled")
        assert rendered["dialog"]["open"] is False
        _no_publication(navigated, cancelled)
        setup = _api(row, "stale_source_setup")
        assert setup["accepted"] is True
        a, b = _owner(setup["before"], source_id), _owner(setup["after"], source_id)
        assert b["metadata"]["properties"]["stale_fixture"] is True
        assert b["revisions"]["metadata"] > a["revisions"]["metadata"]
        assert b["planes"] == a["planes"]
        _refused(row, "stale_capture_refusal")
        switched, _ = _state(row, "lifetime.switched")
        assert switched["active_document_id"] == eager_id
        _refused(row, "inactive_source_refusal")
        saved = _api(row, "mark_source_saved_for_physical_close")
        assert saved["accepted"] is True and _owner(saved["after"], source_id)["dirty"] is False
        closed, _ = _state(row, "lifetime.closed")
        assert source_id not in closed["document_ids"] and eager_id in closed["document_ids"]
        _unchanged(_owner(saved["after"], source_id), row["closed_source"], display=True)
        _refused(row, "closed_source_refusal")
        _selectors(row, f"document.{eager_id}", f"document.{source_id}", "dialog.cancel", "menu.File.Close_image")
    elif case_id == "E07":
        ready, jobs, changed = _ready_preview(row, "plane.ready")
        assert any(position[:3] == (1, 2, 2) for position in changed), "Preview must overlap the exported XY plane"
        for label in ("preview.cancelled", "preview.refused"):
            state, rendered = _state(row, label)
            _no_publication(ready, state)
            current = rendered["gaussian_jobs"]
            assert current["preview"]["job"]["id"] == jobs["preview"]["job"]["id"]
            assert current["preview"]["target"] == jobs["preview"]["target"]
            assert current["active_installed_generation"] == jobs["active_installed_generation"]
            assert state["usage"] == ready["usage"]
        _refused(row, "malformed_preview_refusal")
        second, _, changed = _ready_preview(row, "range.ready")
        assert any(position[3] in {1, 3} for position in changed), "Preview must overlap exported XZ samples"
        assert second["active_document_id"] == source_id
        released, _ = _state(row, "preview.output_active")
        assert released["usage"] == ZERO_USAGE and released["busy"] is False
        assert released["active_document_id"] == _entry(row, "committed.range")["output"]["document_id"]
        _selectors(row, "gaussian.preview", "dialog.cancel", "dialog.apply")
    elif case_id == "E08":
        saved = _api(row, "save_derived_native")
        reopened = _api(row, "open_derived_native")
        assert saved["accepted"] is reopened["accepted"] is True
        output = _entry(row, "saved.plane")["output"]
        assert output["sample_policy"] == "preserve_quiet_nan_float32"
        root = _session(row, directory, "derived")
        assert len(root["documents"]) == 1
        _session_document(root["documents"][0], output)
        assert root["workflow"] == saved["before"]["workflow"] and root["results"] == saved["before"]["results"]
        state, _ = _state(row, "derived.reopened")
        new_ids = set(reopened["after"]["document_ids"]) - set(reopened["before"]["document_ids"])
        assert len(new_ids) == 1
        fresh = _owner(state, new_ids.pop())
        assert fresh["document_id"] != output["document_id"] and fresh["dirty"] is False
        assert fresh["undo_steps"] == fresh["redo_steps"] == 0
        _session_document(root["documents"][0], fresh)
        _refused(row, "guard_legacy_reslice")
        for action in ("show_axis_range_orientation", "show_axis_range_spacing", "show_axis_range_slicer_resize"):
            opened, rendered = _state(row, action + ".opened")
            assert rendered["dialog"]["open"] and not opened["busy"]
            canceled, rendered = _state(row, action + ".canceled")
            assert not rendered["dialog"]["open"]
            _no_publication(opened, canceled)
            _selectors(row, "menu.action." + action, "dialog.cancel")
    elif case_id == "E09":
        original = [_entry(row, label)["command_record"] for label in ("original.plane", "original.range")]
        assert _typed(row["original_records"]) == _typed(original)
        state, source = _navigation(row, "replay.navigated", t=0, c=0)
        assert state["workflow"] == original
        for label, record in zip(("replay.plane", "replay.range"), original):
            entry = _entry(row, label)
            assert entry["source_before"]["display"]["t"] == entry["source_before"]["display"]["c"] == 0
            assert _typed(entry["command_record"]) == _typed(record)
            assert _typed(entry["state_after"]["workflow"][:2]) == _typed(original)
        _refused(row, "new_owner_strict_refusal")
        setup = _api(row, "stale_replay_setup")
        assert setup["accepted"] is True
        assert _owner(setup["after"], source_id)["metadata"]["properties"]["stale_replay_fixture"] is True
        assert _owner(setup["after"], source_id)["revisions"]["metadata"] > source["revisions"]["metadata"]
        _refused(row, "stale_strict_refusal")
        _selectors(row, "workflow.extraction.0.replay", "workflow.extraction.1.replay")
    else:
        old = row["original_owner_id"]
        reopened_id = row["reopened_owner_id"]
        assert old == source_id and reopened_id != old
        prepared = _api(row, "original_records_metadata_preparation")
        assert prepared["accepted"] is True and prepared["before"]["document_ids"] == prepared["after"]["document_ids"]
        original_source = _owner(prepared["after"], old)
        records = row["original_records"]
        assert len(records) == 2
        _command(records[0], original_source, _request(("plane", "XZ", 2, 2, 1, 1, 2, "float32", 1)), "axis_plane_export")
        _command(records[1], original_source, _request(("range", "YZ", 1, 6, 2, 1, 2, "float32", 1)), "axis_range_reslice")
        root = _session(row, directory, "source")
        assert len(root["documents"]) == 1 and _typed(root["workflow"]) == _typed(records)
        _session_document(root["documents"][0], original_source)
        assert _api(row, "save_rebind_source_native")["accepted"] is True
        assert _api(row, "open_rebind_source_native")["accepted"] is True
        changed = _api(row, "new_calibration_preparation")
        assert changed["accepted"] is True
        _literal(_owner(changed["after"], reopened_id), calibration=REBOUND_CALIBRATION)
        destination, source = _navigation(row, "rebind.destination", t=1, c=1, document_id=reopened_id)
        _literal(source, calibration=REBOUND_CALIBRATION)
        assert _typed(destination["workflow"]) == _typed(records)
        for label in ("rebind.plane", "rebind.range"):
            entry = _entry(row, label)
            assert entry["source_before"]["document_id"] == reopened_id
            assert _typed(entry["state_after"]["workflow"][:2]) == _typed(records)
        off = _api(row, "recording_off_strict_replay")
        assert off["accepted"] is True and off["after"]["recording"] is False
        assert _typed(off["after"]["workflow"]) == _typed(off["before"]["workflow"])
        _refused(row, "malformed_original_rebind_refusal")
        final, _ = _state(row, "rebind.final")
        assert _typed(final["workflow"][:2]) == _typed(records)
        _unchanged(original_source, _owner(final, old), display=True)
        _selectors(row, "workflow.extraction.0.rebind", "workflow.extraction.1.rebind")


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_scientific_slicing(case_id, context, request, tmp_path):
    row, directory = _observe(case_id, context, tmp_path / "observations", request)
    _assert_case(row, case_id, context, directory)
