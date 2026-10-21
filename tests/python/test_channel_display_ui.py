"""Independent P06c physical observations for both installed ImGui launches.

Expected source bits, display states, processor mapping, RGB, records and V3
payloads are derived here from declared fixture literals. Product mapping,
extraction, range-resolution and operation helpers are never expectation oracles.
The producer supplies actual events and readbacks, not acceptance decisions.
"""

import base64
import copy
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


CONTRACT = "p06c.channel-display-ui.001"
CASES = ("C01A", "C01B", "C01C", "C02A", "C02B", "C03A", "C03B",
         "C04A", "C04B", "C04C", "C05A", "C05B", "C05C",
         "C06A", "C06B", "C06C", "C06D", "C06E")
CONTEXTS = ("imgui_cpp", "imgui_python")
EXPECTED_COLLECTION_COUNT = 36
EXPECTED_COLLECTION_IDS = tuple(
    f"test_installed_channel_display[{case_id}-{context}]"
    for case_id in CASES for context in CONTEXTS
)
EXPECTED_CHECKPOINTS = {
    "C01A": ("enabled", "color.ordinary", "color.linked", "grayscale.ordinary", "grayscale.linked",
             "composite.ordinary", "composite.linked", "channel.changed", "disabled"),
    "C01B": ("enabled", "color.ordinary", "color.linked", "grayscale.ordinary", "grayscale.linked",
             "composite.ordinary", "composite.linked", "disabled"),
    "C01C": ("color.ordinary", "color.linked", "grayscale.ordinary", "grayscale.linked", "composite.ordinary", "composite.linked"),
    "C02A": ("draft.partial", "draft.disabled_apply", "draft.failed_action", "draft.unrelated", "draft.focus_return", "draft.applied"),
    "C02B": ("range.c1", "range.c0", "range.navigated", "range.reset_current", "range.reset_all"),
    "C03A": ("visible.ordinary", "visible.linked", "visible.hidden_active", "visible.hidden_all", "visible.color",
             "visible.grayscale", "visible.composite_restored"),
    "C03B": ("lut.loaded", "lut.linked", "lut.exported"),
    "C04A": ("preview.ready", "preview.nonactive_ordinary", "preview.nonactive_linked", "preview.live_reset"),
    "C04B": ("preview.ready", "preview.threshold_linked", "preview.away", "preview.back", "preview.hidden",
             "preview.color_hidden", "preview.grayscale_hidden"),
    "C04C": ("preview.ready", "preview.before_tab", "preview.other_owner", "preview.returned", "preview.panel_reopened"),
    "C05A": ("saved.range", "saved.edited", "saved", "reopened"),
    "C05B": ("record.original", "record.navigated", "record.replayed", "record.rebind_opened", "record.rebound", "record.recording_off_replayed"),
    "C05C": ("opaque.baseline", "opaque_37.refused", "opaque_1.refused", "legacy.refused"),
    "C06A": ("busy.disabled", "guards.complete"),
    "C06B": ("owner.partial", "owner.switched", "owner.returned", "owner.none"),
    "C06C": ("count.singleton_ordinary", "count.singleton_linked", "count.eager_range", "count.eager_converted",
             "count.ninth_selected", "count.fallback_ordinary", "count.fallback_linked"),
    "C06D": ("nan.reset", "nan.equal", "nan.linked", "nan.inverted", "nan.disabled_apply"),
    "C06E": ("float64.legacy", "rgb.legacy", "rgba.legacy"),
}
EXPECTED_API_OBSERVATIONS = {
    "C02A": ("malformed_workflow_precondition", "restore_workflow"),
    "C04B": ("threshold_overlay_preparation",),
    "C05B": ("recording_off",),
    "C05C": ("opaque_37_native_roundtrip", "opaque_1_native_roundtrip", "legacy_v2_refusal", "detached_v2_refusal"),
    "C06A": ("queued_apply_preparation", "busy_apply_refusal", "busy_enable_refusal", "malformed_rebind_refusal",
             "foreign_owner_refusal", "stale_metadata_preparation", "stale_record_refusal", "close_owner_preparation", "closed_source_refusal"),
    "C06B": ("close_current_preparation", "closed_panel_source_refusal", "close_remaining_preparation"),
    "C06C": ("eager_display_range_conversion", "scientific_conversion_refusal"),
    "C06E": ("float64_unsupported_set", "rgb_unsupported_set", "rgba_unsupported_set"),
}
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 32 * 1024 * 1024
SHAPE = [2, 3, 4, 3, 5, 1]
CALIBRATION = {
    "spacing": [.5, 1.25, 2.5], "origin": [11., -7., 19.],
    "direction": [-1., .25, .5, 0., 1., .5, .5, 0., 1.], "unit": "um",
    "frame_interval": .75, "time_origin": 2.25, "time_unit": "s",
}
TYPE_CODES = {"uint8": "B", "uint16": "H", "float32": "f", "float64": "d"}
PALETTE = ("Red", "Green", "Blue", "White", "Cyan", "Magenta", "Yellow", "White")
MASKS = {"Grays": (1, 1, 1), "White": (1, 1, 1), "Red": (1, 0, 0),
         "Green": (0, 1, 0), "Blue": (0, 0, 1), "Cyan": (0, 1, 1),
         "Magenta": (1, 0, 1), "Yellow": (1, 1, 0)}
ZERO_USAGE = dict(running=0, queued=0, running_bytes=0, retained_bytes=0, reserved_bytes=0)
PHYSICAL_KEYS = {"Ctrl", "Shift", "Alt", "Super", "A", "W", "Enter", "Escape", "Tab",
                 "Left", "Right", "Up", "Down", "Backspace", "Delete", "Space",
                 "Home", "End", "PageUp", "PageDown"}
# Reuse only the small-sigma raw-sample sanity comparison from P06d1's
# _analytic_convolve/_changed, tests/python/test_scientific_operation_targets_ui.py
# SHA256 483c9cdfb9b0abe69ae730b97f83882ecbc8baed98e168a88dd6d467545eea33.
# build/p06d1-001/source-inputs-006.json pins those bytes; local-exit-001.json
# binds the passing installed-all-006.xml gate. This is not a new Gaussian
# parity tolerance. Compositor expectations below use actual READY input bits
# and the independent captured processor mapping, with exact RGB comparison.
PRIOR_GAUSSIAN_SANITY_MAX_ABS_ERROR = .02


def _unique(pairs):
    result = {}
    for name, value in pairs:
        assert name not in result, f"Duplicate JSON key: {name}"
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
    if isinstance(value, dict):
        return {name: _typed(item) for name, item in value.items()}
    if isinstance(value, list):
        return [_typed(item) for item in value]
    if type(value) is float:
        return ("float64", struct.pack("<d", value).hex())
    return (type(value).__name__, value)


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
    configured = os.environ.get("IMAGEJXX_CHANNEL_DISPLAY_CPP_REPORT")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_CHANNEL_DISPLAY_CPP_REPORT to the path-only installed case index")
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
        # A case index transports paths only; inline frames/pixels are forbidden.
        assert set(entry) <= {"case_id", "id", "relative_path", "byte_count", "sha256"}
        path = _member(index_path.parent, entry["relative_path"])
        assert 0 < path.stat().st_size <= MAX_REPORT
        if "byte_count" in entry:
            assert path.stat().st_size == _integer(entry["byte_count"], minimum=1)
        if "sha256" in entry:
            assert hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"]
        paths[case_id] = path
    assert set(paths) == set(CASES) and len(set(paths.values())) == len(CASES)
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
             "--channel-display-ui-case", case_id, str(directory)],
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
        row = _decode(editor._channel_display_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row, directory


def _frame(row, label):
    values = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(values) == 1, f"Missing/duplicate physical frame: {label}"
    return values[0]


def _control(rendered, selector):
    if selector == "gaussian.window_drag":
        return rendered["gaussian_panel"]["window_drag"]
    if selector.startswith("channel_display."):
        return rendered["channel_display_panel"]["control_states"][selector]
    if selector.startswith("dialog."):
        return rendered["dialog"]["controls"][selector.removeprefix("dialog.")]
    if selector.startswith("document."):
        values = [tab for tab in rendered["document_tabs"]
                  if tab["document_id"] == int(selector.removeprefix("document."))]
        assert len(values) == 1
        return values[0]
    if selector.startswith("gaussian."):
        panel, key = rendered["gaussian_panel"], selector.removeprefix("gaussian.")
        return dict(panel["control_states"][key], point=panel["controls"][key],
                    rect=panel["control_rects"][key])
    if selector.startswith("target."):
        panel, key = rendered["gaussian_panel"]["operation_target"], selector.removeprefix("target.")
        return dict(panel["control_states"][key], point=panel["controls"][key],
                    rect=panel["control_rects"][key])
    if selector.startswith("error_modal."):
        return rendered["error_modal"]["controls"][selector.removeprefix("error_modal.")]
    if selector == "orthogonal.toggle":
        navigation = rendered["orthogonal_navigation"]
        return {"id": navigation["toggle_id"], "point": navigation["toggle_point"],
                "rect": None, "enabled": True, "focused": navigation["toggle_focused"],
                "geometry_source": "actual_toggle_point"}
    if selector == "orthogonal.slice.XY":
        planes = [plane for plane in rendered["orthogonal_planes"] if plane["plane"] == "XY"]
        assert len(planes) == 1
        plane = planes[0]
        return {"id": plane["slice_id"], "point": plane["slice_point"], "rect": None,
                "enabled": True, "focused": plane["slice_focused"],
                "geometry_source": "actual_slice_point"}
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
        assert observed["selector"] != "gaussian.window_drag", "Window moves require the drag ledger"
        ordinal = _integer(observed["before_frame_ordinal"], minimum=1)
        assert ordinal + 3 <= len(frames)
        control = _control(frames[ordinal - 1]["rendered"], observed["selector"])
        assert control == observed["control"] and _integer(control["id"], minimum=1)
        assert type(control["enabled"]) is bool
        assert control["enabled"] is (not observed["disabled_attempt"])
        point, rect = observed["position"], control["rect"]
        assert len(point) == 2
        assert rect is not None or observed["selector"] in {"orthogonal.toggle", "orthogonal.slice.XY"}
        for value in (rect or []) + point:
            _number(value)
        assert point == [math.floor(value) for value in control["point"]]
        if rect is not None:
            assert len(rect) == 4 and rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        affected = set(range(ordinal + 1, ordinal + 4))
        assert not click_frames.intersection(affected)
        click_frames.update(affected)
    drags = row["drag_observations"]
    assert isinstance(drags, list) and len(drags) == (1 if row["case_id"] == "C04B" else 0)
    drag_frames = set()
    for observed in drags:
        assert set(observed) == {"selector", "control", "start", "end", "before_frame_ordinal"}
        assert observed["selector"] == "gaussian.window_drag"
        ordinal = _integer(observed["before_frame_ordinal"], minimum=1)
        assert ordinal+5 <= len(frames)
        before = frames[ordinal-1]
        control = _control(before["rendered"], observed["selector"])
        assert control == observed["control"]
        assert set(control) == {"id", "enabled", "visible", "point", "rect", "window_rect", "viewport_work_rect"}
        move_id = _integer(control["id"], minimum=1)
        assert control["enabled"] is True and control["visible"] is True
        title, window, viewport = (control[name] for name in ("rect", "window_rect", "viewport_work_rect"))
        for rect in (title, window, viewport):
            assert isinstance(rect, list) and len(rect) == 4
            assert all(math.isfinite(_number(value)) for value in rect)
            assert rect[0] < rect[2] and rect[1] < rect[3]
        assert window[0] <= title[0] < title[2] <= window[2]
        assert window[1] <= title[1] < title[3] <= window[3]
        assert control["point"] == [(title[0]+title[2])/2, (title[1]+title[3])/2]
        start, end = observed["start"], observed["end"]
        assert all(isinstance(point, list) and len(point) == 2 and
                   all(_number(value) == math.floor(value) for value in point)
                   for point in (start, end))
        assert start == [math.floor(value) for value in control["point"]]
        assert title[0] <= start[0] < title[2] and title[1] <= start[1] < title[3]
        desired_shift = viewport[2]-(window[2]-window[0])-20-window[0]
        assert end == [math.floor(control["point"][0]+desired_shift), start[1]]
        delta = [end[0]-start[0], end[1]-start[1]]
        assert delta[0] != 0 and delta[1] == 0
        moved = frames[ordinal:ordinal+5]
        assert [frame["label"] for frame in moved] == [
            "preview.gaussian_moved."+phase for phase in ("position", "down", "move", "up", "settled")]
        assert [frame["requested_events"] for frame in moved] == [
            [{"type": "position", "space": "screen", "point": start}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "position", "space": "screen", "point": end}],
            [{"type": "button", "name": "left", "down": False}],
            [],
        ]
        affected = set(range(ordinal+1, ordinal+6))
        assert not affected.intersection(click_frames) and not affected.intersection(drag_frames)
        drag_frames.update(affected)
        for frame in moved[1:3]:
            actual = frame["rendered"]["input_frame"]
            assert actual["mouse_down"] == [True, False, False]
            assert actual["focus"]["active_id"] == move_id
        for frame in moved[3:]:
            actual = frame["rendered"]["input_frame"]
            assert actual["mouse_down"] == [False]*3
            assert actual["focus"]["active_id"] != move_id
        settled = moved[-1]
        final_control = _control(settled["rendered"], observed["selector"])
        assert set(final_control) == set(control)
        for name in ("id", "enabled", "visible", "viewport_work_rect"):
            assert final_control[name] == control[name]
        for name in ("rect", "window_rect"):
            rect = control[name]
            assert final_control[name] == [rect[0]+delta[0], rect[1]+delta[1],
                                           rect[2]+delta[0], rect[3]+delta[1]]
        assert final_control["point"] == [control["point"][axis]+delta[axis] for axis in range(2)]
        final_window = final_control["window_rect"]
        assert viewport[0] <= final_window[0] < final_window[2] <= viewport[2]
        assert viewport[1] <= final_window[1] < final_window[3] <= viewport[3]
        assert before["rendered"]["gaussian_jobs"] == settled["rendered"]["gaussian_jobs"]
        jobs = settled["rendered"]["gaussian_jobs"]
        assert jobs["preview"]["owns_job"] is True and jobs["preview"]["job"]["state"] == "ready"
        assert _integer(jobs["active_installed_generation"], minimum=1)
        for name in ("document_ids", "facade_ids", "active_document_id", "busy", "workflow", "results", "recording", "usage"):
            assert _typed(settled["native_state"][name]) == _typed(before["native_state"][name]), name
        for document in before["native_state"]["documents"]:
            _summary_unchanged(document, _owner(settled["native_state"], document["document_id"]), display=True)
        assert [plane["plane"] for plane in before["rendered"]["orthogonal_planes"]] == [
            plane["plane"] for plane in settled["rendered"]["orthogonal_planes"]] == ["XY", "XZ", "YZ"]
        for original, shifted in zip(before["rendered"]["orthogonal_planes"], settled["rendered"]["orthogonal_planes"]):
            assert original["plane"] == shifted["plane"]
            assert original["readback"] == shifted["readback"]
            assert original["channel_display"] == shifted["channel_display"]
    for frame in frames:
        if any(event["type"] == "button" for event in frame["requested_events"]):
            assert frame["ordinal"] in click_frames or frame["ordinal"] in drag_frames
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0


def _state(row, label):
    state = row["states"][label]
    frame = row["frames"][state["frame_ordinal"] - 1]
    assert frame["label"] == label
    assert state["document_ids"] == [document["document_id"] for document in state["documents"]]
    assert state["facade_ids"] == state["document_ids"]
    assert len(set(state["document_ids"])) == len(state["document_ids"]) <= 16
    assert state["active_document_id"] in state["document_ids"] or state["active_document_id"] == 0
    assert type(state["recording"]) is bool
    return state, frame["rendered"]


def _owner(state, document_id):
    values = [document for document in state["documents"] if document["document_id"] == document_id]
    assert len(values) == 1
    return values[0]


def _plane_rows(document):
    shape, dtype = document["shape"], document["dtype"]
    assert len(shape) == 6 and all(type(value) is int and value > 0 for value in shape)
    assert shape[-1] in {1, 3, 4} and dtype in TYPE_CODES
    positions = [(t, c, z) for t in range(shape[0])
                 for c in range(shape[1]) for z in range(shape[2])]
    assert len(document["planes"]) == len(positions)
    rows, count = {}, math.prod(shape[3:])
    for position, plane in zip(positions, document["planes"]):
        assert plane["position"] == list(position)
        assert len(bytes.fromhex(plane["storage_f64_hex"])) == count * 8
        assert len(bytes.fromhex(plane["declared_hex"])) == count * struct.calcsize(TYPE_CODES[dtype])
        for name in ("image_address", "buffer_address"):
            assert isinstance(plane[name], str) and plane[name].startswith("0x") and int(plane[name], 16) > 0
        rows[position] = plane
    return rows


def _source_value(t, c, z, y, x, sample=0):
    return 40*t + 30*c + 5*z + 2*y + x + 2*sample


@lru_cache(maxsize=256)
def _source_plane(t, c, z, y_size, x_size, samples, dtype, variant):
    declared, storage = [], []
    for y in range(y_size):
        for x in range(x_size):
            for sample in range(samples):
                if variant == "quiet" and (t, c, z, y, x) == (1, 1, 2, 1, 2):
                    assert dtype == "float32" and samples == 1
                    declared.append(struct.pack("<I", 0x7FC12345))
                    storage.append(struct.pack("<Q", 0x7FF82468A0000000))
                elif variant == "quiet" and (t, c, z, y, x) == (1, 1, 2, 2, 3):
                    assert dtype == "float32" and samples == 1
                    declared.append(struct.pack("<I", 0x80000000))
                    storage.append(struct.pack("<Q", 0x8000000000000000))
                elif variant == "all_nan" and (t, c, z) == (1, 1, 2):
                    assert dtype == "float32" and samples == 1
                    declared.append(struct.pack("<I", 0x7FC12345))
                    storage.append(struct.pack("<Q", 0x7FF82468A0000000))
                else:
                    value = _source_value(t, c, z, y, x, sample)
                    # Above-eight U8 variants must declare their own finite
                    # representation rather than relying on implicit wrapping.
                    declared.append(struct.pack("<" + TYPE_CODES[dtype], value))
                    storage.append(struct.pack("<d", value))
    return b"".join(declared), b"".join(storage)


def _literal(document, *, variant="finite"):
    shape, dtype, policy = document["shape"], document["dtype"], document["sample_policy"]
    assert shape[2:5] == [4, 3, 5]
    assert shape[0] in {1, 2} and shape[1] in {1, 3, 9}
    assert policy == ("finite_only" if variant == "finite" else "preserve_quiet_nan_float32")
    assert document["calibration"] == CALIBRATION
    for position, plane in _plane_rows(document).items():
        typed, backing = _source_plane(*position, *shape[3:], dtype, variant)
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


def _no_publication(before, after):
    for name in ("document_ids", "facade_ids", "active_document_id", "workflow", "results", "recording"):
        assert _typed(after[name]) == _typed(before[name]), name
    for document in before["documents"]:
        _unchanged(document, _owner(after, document["document_id"]), display=True)


def _display_state(count=3, mode="composite", bounds=(0., 255.)):
    return {"schema": "imagejxx.channel-display", "version": 1, "mode": mode,
            "channels": [{"visible": True, "range": list(bounds),
                          "lut": {"name": PALETTE[min(c, 7)]}} for c in range(count)]}


def _table():
    return [[(3*i+7) % 256, (5*i+11) % 256, (7*i+13) % 256] for i in range(256)]


def _stored(document):
    return document["display"]["properties"].get("channel_display")


def _assert_stored(document, expected):
    actual = _stored(document)
    assert actual == expected
    if actual is None:
        assert "channel_display" not in document["display"]["properties"]
        return
    assert set(actual) == {"schema", "version", "mode", "channels"}
    assert type(actual["version"]) is int
    assert actual["mode"] in {"color", "grayscale", "composite"}
    assert len(actual["channels"]) == document["shape"][1]
    for channel in actual["channels"]:
        assert set(channel) == {"visible", "range", "lut"}
        assert type(channel["visible"]) is bool
        low, high = channel["range"]
        assert _number(low) <= _number(high)
        lut = channel["lut"]
        assert set(lut) in ({"name"}, {"table"})
        if "table" in lut:
            assert len(lut["table"]) == 256
            assert all(len(row) == 3 and all(type(v) is int and 0 <= v <= 255 for v in row)
                       for row in lut["table"])
        else:
            assert lut["name"] in MASKS


def _index(value, dtype, bounds):
    """Frozen processor rules from the independent 63+9 pinned captures."""
    if math.isnan(value):
        return 0
    low, high = bounds
    if dtype == "uint8":
        origin, width = math.floor(low+.5), high-low
        scaled = ((value-origin)*256/width if width else (255 if value > origin else 0))
        return max(0, min(255, math.floor(scaled)))
    if dtype == "uint16":
        lo, hi = math.floor(low+.5), math.floor(high+.5)
        if lo == hi:
            return 255 if value > lo else 0
        # Pinned ShortProcessor.create8BitImage stores scale before rounding.
        scale = 256.0/(hi-lo+1)
        return max(0, min(255, math.floor((value-lo)*scale+.5)))
    assert dtype == "float32"
    rounded = [struct.unpack("<f", struct.pack("<f", bound))[0] for bound in bounds]
    lo, hi = rounded
    if lo == hi:
        return 255 if value > lo else 0
    # Pinned FloatProcessor.create8BitImage requires this binary64 order.
    scale = 255.0/(hi-lo)
    return max(0, min(255, math.floor((value-lo)*scale+.5)))


def _lut(index, lut):
    if "table" in lut:
        return lut["table"][index]
    return [index*component for component in MASKS[lut["name"]]]


def _selectors(row, *selectors):
    actual = {item["selector"] for item in row["control_observations"] if not item["disabled_attempt"]}
    assert set(selectors) <= actual


def _text(row, value):
    assert any(frame["requested_events"] == [{"type": "text", "value": value}]
               for frame in row["frames"]), f"Missing physical text input {value!r}"


def _api(row, name):
    values = [value for value in row["api_observations"] if value["name"] == name]
    assert len(values) == 1, f"Missing/duplicate API observation {name}"
    value = values[0]
    assert "API" in value["role"] and "not physical" in value["role"]
    return value


def _refused(row, name):
    value = _api(row, name)
    assert value["accepted"] is False and value["error_type"] and value["error"]
    _no_publication(value["before"], value["after"])
    return value


def _snapshot(row, label, expected=None, *, document_id=None):
    state, rendered = _state(row, label)
    document_id = document_id or row["source_document_id"]
    document = _owner(state, document_id)
    if expected is not None:
        _assert_stored(document, expected)
    return state, rendered, document


@lru_cache(maxsize=8)
def _gaussian_sanity_plane(t=1, c=1, z=2):
    """Apply P06d1's prior analytic sanity comparison to this fixture only.

    That qualified reader compares separable normalized Gaussian weights with
    radius ceil(7*sigma), clamped source edges, 0 < sigma <= 4, and selected
    writes within .02 raw units; unselected writes remain bit-exact. P06d1
    owns Gaussian numerical/reference qualification. This auxiliary check is
    never used as compositor input or as an expected RGB value.
    """
    values = [_source_value(t, c, z, y, x) for y in range(3) for x in range(5)]
    sigma, radius = .85, math.ceil(7*.85)
    offsets = range(-radius, radius+1)
    weights = [math.exp(-.5*(offset/sigma)**2) for offset in offsets]
    divisor = math.fsum(weights)
    weights = [value/divisor for value in weights]
    for axis in (0, 1):
        values = [math.fsum(weight*values[(min(2, max(0, y+offset)) if axis else y)*5
                                       + (x if axis else min(4, max(0, x+offset)))]
                            for offset, weight in zip(offsets, weights))
                  for y in range(3) for x in range(5)]
    # The physically opened Gaussian panel captures the fixture's selection:
    # rectangle [1,0]..[4,3]. Its mask writes only x=1,2,3; the convolution
    # independently reads the full live plane around those selected pixels.
    return tuple(value if 1 <= index % 5 < 4 else
                 _source_value(t, c, z, index//5, index % 5)
                 for index, value in enumerate(values))


def _preview(row, label):
    state, rendered, source = _snapshot(row, label)
    values = [item for item in row["preview_observations"] if item["label"] == label]
    assert len(values) == 1
    value, jobs = values[0], rendered["gaussian_jobs"]
    generation = _integer(jobs["active_installed_generation"], minimum=1)
    assert value["gaussian_jobs"]["active_installed_generation"] == generation
    assert jobs["preview"]["owns_job"] is True
    assert jobs["preview"]["source_id"] == source["document_id"]
    assert jobs["preview"]["job"]["state"] == "ready"
    assert jobs["preview"]["job"]["id"] == value["job_id"]
    assert state["busy"] is False and state["usage"]["retained_bytes"] > 0
    assert state["usage"]["reserved_bytes"] >= state["usage"]["retained_bytes"]
    assert value["shape"] == SHAPE and value["calibration"] == CALIBRATION
    assert source["dtype"] == "float32" and source["sample_policy"] == "finite_only"
    assert source["selection"] == {"kind": "rectangle", "points": [[1, 0], [4, 3]], "width": 1}
    _literal(source)
    observed_ordinal = _integer(value["frame_ordinal"], minimum=1)
    assert state["frame_ordinal"] == observed_ordinal+1
    captured_jobs = row["frames"][observed_ordinal-1]["rendered"]["gaussian_jobs"]
    assert captured_jobs == value["gaussian_jobs"]
    assert captured_jobs["preview"]["job"]["state"] == "ready"
    assert captured_jobs["preview"]["job"]["id"] == value["job_id"]
    staged = {tuple(plane["position"]): plane for plane in value["planes"]}
    committed = {tuple(plane["position"]): plane for plane in value["committed_planes"]}
    assert len(value["planes"]) == len(value["committed_planes"]) == 1
    assert staged.keys() == committed.keys() == {(1, 1, 2)}
    assert committed[(1, 1, 2)] == _plane_rows(source)[(1, 1, 2)]
    for key in ("image_address", "buffer_address"):
        assert int(staged[(1, 1, 2)][key], 16) > 0
        assert staged[(1, 1, 2)][key] != committed[(1, 1, 2)][key]
    actual = struct.unpack("<15d", bytes.fromhex(staged[(1, 1, 2)]["storage_f64_hex"]))
    assert all(math.isfinite(sample) for sample in actual)
    assert staged[(1, 1, 2)]["declared_hex"] == b"".join(struct.pack("<f", sample) for sample in actual).hex()
    assert staged[(1, 1, 2)]["storage_f64_hex"] != committed[(1, 1, 2)]["storage_f64_hex"]
    # Prepared samples are actual inputs to the compositor. Keep their exact
    # Float32-backed double values, independently of any rendered readback.
    assert all(struct.pack("<d", sample) == struct.pack("<d", struct.unpack("<f", struct.pack("<f", sample))[0])
               for sample in actual)
    committed_bits = bytes.fromhex(committed[(1, 1, 2)]["storage_f64_hex"])
    literal_declared, literal_backing = _source_plane(1, 1, 2, 3, 5, 1, "float32", "finite")
    assert committed_bits == literal_backing
    assert committed[(1, 1, 2)]["declared_hex"] == literal_declared.hex()
    staged_bits = bytes.fromhex(staged[(1, 1, 2)]["storage_f64_hex"])
    sanity = _gaussian_sanity_plane()
    for index, (sample, expected) in enumerate(zip(actual, sanity)):
        if 1 <= index % 5 < 4:
            assert abs(sample-expected) <= PRIOR_GAUSSIAN_SANITY_MAX_ABS_ERROR
        else:
            assert staged_bits[index*8:(index+1)*8] == literal_backing[index*8:(index+1)*8]
    record = jobs["preview"]["command_record"]
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": .85, "scaled_units": False}
    assert record["calibration"] == CALIBRATION and record["effective_sigma_xy"] == [.85, .85]
    target = record["target"]
    assert target == value["target"]
    assert target["schema_version"] == 1 and target["document_id"] == source["document_id"]
    assert target["source_shape"] == SHAPE and target["roi_policy"] == "active_selection"
    assert target["source_revisions"] == [source["revisions"][name] for name in ("pixels", "metadata", "selection")]
    assert target["execution_mode"] == "planes" and target["destination"] == "in_place"
    if target["scope"] == "positions":
        assert target["positions"] == [[1, 1, 2]] and target["ranges"] == []
    else:
        assert target["scope"] == "ranges" and target["positions"] == []
        assert target["ranges"] == [[1, 2, 1], [1, 2, 1], [2, 3, 1]]
    _selectors(row, "gaussian.sigma", "gaussian.preview")
    _text(row, ".85")
    return generation, actual


def _xyz(plane, fixed, u, v):
    return {"XY": (u, v, fixed), "XZ": (u, fixed, v), "YZ": (fixed, u, v)}[plane]


def _rgb(pixels, source, expected_state, plane, fixed, *, preview=False, generation=0,
         preview_inputs=None, variant="finite", threshold=False):
    display, shape, dtype = source["display"], source["shape"], source["dtype"]
    t, active, gate_z = display["t"], display["c"], display["crosshair"][2]
    channels, mode = expected_state["channels"], expected_state["mode"]
    if preview:
        assert isinstance(preview_inputs, tuple) and len(preview_inputs) == 15
        assert all(type(sample) is float and math.isfinite(sample) for sample in preview_inputs)
    contributing = ([c for c, channel in enumerate(channels) if channel["visible"]]
                    if mode == "composite" else [active])
    height, width = (shape[3], shape[4]) if plane == "XY" else (
        shape[2], shape[4] if plane == "XZ" else shape[3])
    observed = pixels["readback"]
    assert observed["available"] is True
    assert pixels["current_tcz"] == pixels["range_tcz"] == observed["source_tcz"] == [t, active, gate_z]
    image = observed["rendered_rgb"]
    assert image["shape"] == [height, width, 3] and image["dtype"] == "uint8"
    assert len(image["pixels"]) == height*width*3
    expected_rgb = []
    for v in range(height):
        for u in range(width):
            x, y, z = _xyz(plane, fixed, u, v)
            rgb = [0, 0, 0]
            for c in contributing:
                value = float(_source_value(t, c, z, y, x))
                if variant == "all_nan" and (t, c, z) == (1, 1, 2):
                    value = math.nan
                if variant == "quiet" and (t, c, z, y, x) == (1, 1, 2, 1, 2):
                    value = math.nan
                if variant == "quiet" and (t, c, z, y, x) == (1, 1, 2, 2, 3):
                    value = -0.
                staged = preview and t == 1 and c == 1 and gate_z == 2 and z == 2
                if staged:
                    value = preview_inputs[y*5+x]
                channel = channels[c]
                index = _index(value, dtype, channel["range"])
                mapped = ([index]*3 if mode == "grayscale" or
                          (mode == "composite" and shape[1] == 1)
                          else _lut(index, channel["lut"]))
                rgb = [min(255, old+new) for old, new in zip(rgb, mapped)]
            if (threshold and plane == "XY" and active in contributing
                    and (t, active, gate_z) == (1, 1, 2)
                    and 83 <= _source_value(t, active, z, y, x) <= 85):
                rgb = [255, rgb[1]//2, rgb[2]//2]
            expected_rgb.extend(rgb)
    assert image["pixels"] == expected_rgb
    assert all(0 <= _number(value) <= 255 and value == math.floor(value) for value in image["pixels"])
    diagnostic = pixels["channel_display"]
    assert diagnostic["state"] == expected_state
    assert [item["c"] for item in diagnostic["contributors"]] == contributing
    for c, item in zip(contributing, diagnostic["contributors"]):
        staged = preview and t == 1 and c == 1 and gate_z == 2
        assert item["preview"] is staged
        assert item["generation"] == (generation if staged else 0)
        assert item["range"] == channels[c]["range"]
        assert item["preview_z"] == ([2] if staged else [])
        assert _integer(item["data_identity"], minimum=1)
    live_identities = {item["data_identity"] for item in diagnostic["contributors"] if not item["preview"]}
    staged_identities = {item["data_identity"] for item in diagnostic["contributors"] if item["preview"]}
    assert len(live_identities) <= 1 and len(staged_identities) <= 1
    assert not live_identities.intersection(staged_identities)
    assert pixels["source_document_id"] == source["document_id"]
    assert pixels["range"] == channels[active]["range"]
    assert pixels["range_tcz"] == [t, active, gate_z]
    raw_source = observed["source"]
    assert raw_source["shape"] == [height, width, 1] and raw_source["dtype"] == dtype
    expected_source = []
    for v in range(height):
        for u in range(width):
            x, y, z = _xyz(plane, fixed, u, v)
            if variant == "all_nan" and (t, active, z) == (1, 1, 2):
                expected_source.append(struct.pack("<I", 0x7FC12345))
            elif variant == "quiet" and (t, active, z, y, x) == (1, 1, 2, 1, 2):
                expected_source.append(struct.pack("<I", 0x7FC12345))
            elif variant == "quiet" and (t, active, z, y, x) == (1, 1, 2, 2, 3):
                expected_source.append(struct.pack("<I", 0x80000000))
            elif variant != "finite":
                expected_source.append(struct.pack("<f", _source_value(t, active, z, y, x)))
            else:
                expected_source.append(preview_inputs[y*5+x]
                                       if preview and (t, active, gate_z, z) == (1, 1, 2, 2)
                                       else _source_value(t, active, z, y, x))
    if variant != "finite":
        assert raw_source["encoding"] == "IEEE754 Float32LE"
        assert bytes(raw_source["bytes"]) == b"".join(expected_source)
    else:
        assert len(raw_source["pixels"]) == len(expected_source)
        for actual, expected in zip(raw_source["pixels"], expected_source):
            assert struct.pack("<d", _number(actual)) == struct.pack("<d", expected)
    return image["pixels"]


def _view(row, label, expected, *, linked=False, preview=False, generation=0,
          preview_inputs=None, variant="finite", threshold=False, document_id=None):
    state, rendered, source = _snapshot(row, label, expected, document_id=document_id)
    assert state["active_document_id"] == source["document_id"]
    assert source["display"]["orthogonal"] is linked
    x, y, z = source["display"]["crosshair"]
    if linked:
        planes = rendered["orthogonal_planes"]
        assert [pixels["plane"] for pixels in planes] == ["XY", "XZ", "YZ"]
        for pixels, fixed in zip(planes, (z, y, x)):
            assert pixels["coordinate"] == fixed
            _rgb(pixels, source, expected, pixels["plane"], fixed, preview=preview,
                 generation=generation, preview_inputs=preview_inputs, variant=variant, threshold=threshold)
    else:
        _rgb(rendered["ordinary_pixels"], source, expected, "XY", z, preview=preview,
             generation=generation, preview_inputs=preview_inputs, variant=variant, threshold=threshold)
    return state, rendered, source


def _record(record, source, expected, action="set", channels=(), empty=()):
    assert set(record) == {"schema", "version", "action", "source", "position", "state", "reset"}
    assert record["schema"] == "imagejxx.channel-display-command" and type(record["version"]) is int
    assert record["version"] == 1 and record["action"] == action
    assert record["source"] == {
        "id": source["document_id"], "pixels_revision": source["revisions"]["pixels"],
        "metadata_revision": source["revisions"]["metadata"], "shape": source["shape"],
        "type": source["dtype"], "sample_policy": source["sample_policy"],
    }
    assert all(type(value) is int for name, value in record["source"].items()
               if name in {"id", "pixels_revision", "metadata_revision"})
    assert record["position"] == {"t": source["display"]["t"], "c": source["display"]["c"],
                                   "z": source["display"]["crosshair"][2]}
    assert all(type(value) is int for value in record["position"].values())
    assert record["state"] == expected
    assert record["reset"] == {"channels": list(channels), "empty_finite": list(empty)}
    assert all(type(value) is int for value in record["reset"]["channels"])
    assert all(type(value) is bool for value in record["reset"]["empty_finite"])


def _display_change(before, after, document_id, expected, *, action="set", channels=(), empty=(),
                    record_source=None):
    for name in ("document_ids", "facade_ids", "active_document_id", "results", "recording"):
        assert _typed(after[name]) == _typed(before[name]), name
    original, changed = _owner(before, document_id), _owner(after, document_id)
    for document in before["documents"]:
        _unchanged(document, _owner(after, document["document_id"]),
                   display=document["document_id"] != document_id)
    _assert_stored(changed, expected)
    assert changed["display"]["revision"] > original["display"]["revision"]
    assert changed["revisions"]["display"] > original["revisions"]["display"]
    old_properties = original["display"]["properties"]
    assert {name: value for name, value in changed["display"]["properties"].items()
            if name != "channel_display"} == {name: value for name, value in old_properties.items()
                                               if name != "channel_display"}
    for name in ("t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan"):
        assert _typed(changed["display"][name]) == _typed(original["display"][name]), name
    if before["recording"]:
        assert len(after["workflow"]) == len(before["workflow"])+1
        assert _typed(after["workflow"][:-1]) == _typed(before["workflow"])
        _record(after["workflow"][-1], record_source or original, expected, action, channels, empty)
    else:
        assert _typed(after["workflow"]) == _typed(before["workflow"])
    return changed


def _summary_unchanged(before, after, *, display=False):
    for name in ("document_id", "shape", "dtype", "sample_policy", "data_identity", "dirty",
                 "undo_steps", "redo_steps"):
        assert _typed(after[name]) == _typed(before[name]), name
    for name in ("pixels", "metadata", "selection"):
        assert after["revisions"][name] == before["revisions"][name]
    if display:
        assert _typed(after["display"]) == _typed(before["display"])
        assert after["revisions"]["display"] == before["revisions"]["display"]


def _reset_bounds(source, c, case_id):
    if source["dtype"] == "uint8":
        return [0., 255.], False
    view = source["display"]
    t, z = view["t"], view["crosshair"][2]
    if case_id == "C06D" and (t, c, z) == (1, 1, 2):
        return [0., 1.], True
    values = [_source_value(t, c, z, y, x) for y in range(3) for x in range(5)]
    if case_id == "C03B" and (t, c, z) == (1, 1, 2):
        values.remove(_source_value(t, c, z, 1, 2))
        values.remove(_source_value(t, c, z, 2, 3))
        values.append(-0.)
    return [float(min(values)), float(max(values))], False


def _physical_display_commands(row):
    """Derive every panel publication from its actual clicked control.

    This compares native before/release snapshots and the complete normalized
    record, including refusal and recording-off paths. It does not trust a
    producer request echo or a resolved expected state.
    """
    for observation in row["control_observations"]:
        selector = observation["selector"]
        action, selected, empty = "set", [], []
        if not (selector in {"channel_display.enable", "channel_display.disable", "channel_display.apply",
                              "channel_display.reset_current", "channel_display.reset_all"}
                or selector.startswith("channel_display.mode.")
                or selector.startswith("channel_display.lut.")
                or selector.startswith("channel_display.visible.")):
            continue
        ordinal = observation["before_frame_ordinal"]
        before_frame, after_frame = row["frames"][ordinal-1], row["frames"][ordinal+2]
        before, after = before_frame["native_state"], after_frame["native_state"]
        panel = before_frame["rendered"]["channel_display_panel"]
        document_id = panel["source_id"]
        if not document_id:
            assert observation["disabled_attempt"] is True
            assert _typed(after) == _typed(before)
            continue
        source = _owner(before, document_id)
        expected = copy.deepcopy(_stored(source))
        if observation["disabled_attempt"]:
            for name in ("document_ids", "facade_ids", "active_document_id", "workflow", "results", "recording"):
                assert _typed(after[name]) == _typed(before[name])
            for document in before["documents"]:
                _summary_unchanged(document, _owner(after, document["document_id"]), display=True)
            continue
        if selector == "channel_display.enable":
            assert expected is None
            bounds = _reset_bounds(source, 0, row["case_id"])[0]
            expected = _display_state(source["shape"][1], bounds=bounds)
            properties = source["display"]["properties"]
            for channel in expected["channels"]:
                if "display_range" in properties:
                    channel["range"] = copy.deepcopy(properties["display_range"])
                if "custom_lut" in properties:
                    channel["lut"] = {"table": copy.deepcopy(properties["custom_lut"])}
                elif "lut" in properties:
                    assert properties["lut"] in MASKS, "Unqualified legacy conversion must refuse"
                    channel["lut"] = {"name": properties["lut"]}
        elif selector == "channel_display.disable":
            expected, action = None, "disable"
        elif selector.startswith("channel_display.mode."):
            requested = selector.removeprefix("channel_display.mode.")
            if requested == expected["mode"]:
                continue
            expected["mode"] = "color" if requested == "composite" and source["shape"][1] > 8 else requested
            if requested == "composite":
                for channel in expected["channels"]:
                    channel["visible"] = True
        elif selector.startswith("channel_display.lut."):
            lut = {"name": selector.removeprefix("channel_display.lut.")}
            if expected["channels"][source["display"]["c"]]["lut"] == lut:
                continue
            expected["channels"][source["display"]["c"]]["lut"] = lut
        elif selector.startswith("channel_display.visible."):
            c = int(selector.removeprefix("channel_display.visible."))
            expected["channels"][c]["visible"] = not expected["channels"][c]["visible"]
        elif selector == "channel_display.apply":
            # These are the observed input-widget buffers; scenario assertions
            # separately pin their authored physical text and retained drafts.
            bounds = [float(panel["minimum_text"]), float(panel["maximum_text"])]
            assert all(math.isfinite(value) for value in bounds) and bounds[0] <= bounds[1]
            expected["channels"][source["display"]["c"]]["range"] = bounds
        else:
            action = "reset_range"
            selected = ([source["display"]["c"]] if selector == "channel_display.reset_current"
                        else list(range(source["shape"][1])))
            for c in selected:
                bounds, is_empty = _reset_bounds(source, c, row["case_id"])
                expected["channels"][c]["range"] = bounds
                empty.append(is_empty)
        failed_preflight = before["recording"] and not isinstance(before["workflow"], list)
        for name in ("document_ids", "facade_ids", "active_document_id", "results", "recording"):
            assert _typed(after[name]) == _typed(before[name]), name
        for document in before["documents"]:
            _summary_unchanged(document, _owner(after, document["document_id"]),
                               display=failed_preflight or document["document_id"] != document_id)
        if failed_preflight:
            assert _typed(after["workflow"]) == _typed(before["workflow"])
            continue
        changed = _owner(after, document_id)
        _assert_stored(changed, expected)
        for name in ("t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan"):
            assert _typed(changed["display"][name]) == _typed(source["display"][name]), name
        assert {name: value for name, value in changed["display"]["properties"].items() if name != "channel_display"} == {
            name: value for name, value in source["display"]["properties"].items() if name != "channel_display"}
        if before["recording"]:
            assert len(after["workflow"]) == len(before["workflow"])+1
            assert _typed(after["workflow"][:-1]) == _typed(before["workflow"])
            _record(after["workflow"][-1], source, expected, action, selected, empty)
        else:
            assert _typed(after["workflow"]) == _typed(before["workflow"])


def _file(row, directory, key):
    descriptor = row["files"][key]
    assert set(descriptor) == {"member", "byte_count", "hex"}
    assert Path(descriptor["member"]).name == descriptor["member"]
    raw = _member(directory, descriptor["member"]).read_bytes()
    assert 0 < len(raw) == _integer(descriptor["byte_count"], minimum=1) <= MAX_ARCHIVE
    assert raw.hex() == descriptor["hex"]
    return raw


def _session(row, directory, key):
    raw = _file(row, directory, key)
    assert raw.startswith(b"IMAGEJXX3\n") and len(raw) > 18
    declared, = struct.unpack("<Q", raw[10:18])
    assert 0 < declared <= MAX_ARCHIVE
    stream = zlib.decompressobj()
    plain = stream.decompress(raw[18:], declared+1)
    assert len(plain) == declared and stream.eof and not stream.unused_data and not stream.unconsumed_tail
    value = _decode(plain, MAX_ARCHIVE)
    assert set(value) == {"format", "version", "documents", "results", "workflow", "extensions"}
    assert value["format"] == "imagejxx-session" and value["version"] == 3
    return value


def _session_selection(observed):
    if observed is None:
        return None
    geometry = {"kind", "points", "width"}
    if observed["kind"] == "mask":
        geometry |= {"shape", "mask"}
    return {**{name: observed[name] for name in geometry},
            "attributes": {name: value for name, value in observed.items() if name not in geometry}}


def _session_document(record, document):
    assert record["axes"] == "TCZYXS" and record["shape"] == document["shape"]
    assert record["pixel_type"] == document["dtype"] and record["sample_policy"] == document["sample_policy"]
    assert record["storage"] == "Float64LE"
    expected = [bytes.fromhex(plane["storage_f64_hex"]) for plane in _plane_rows(document).values()]
    assert [base64.b64decode(value, validate=True) for value in record["planes"]] == expected
    for name in ("calibration", "metadata", "overlays"):
        assert _typed(record[name]) == _typed(document[name])
    for name in ("selection", "previous_selection"):
        assert _typed(record[name]) == _typed(_session_selection(document[name]))
    assert _typed(record["rois"]) == _typed([
        dict(roi, selection=_session_selection(roi["selection"])) for roi in document["rois"]])
    assert record["fourier"] is None
    assert _typed(record["display"]) == _typed({name: value for name, value in document["display"].items()
                                               if name != "revision"})


def _fixtures(row, case_id):
    initial, _ = _state(row, "initial")
    fixtures = row["fixture_sources"]
    labels = {"source"}
    if case_id in {"C04C", "C05B", "C06A", "C06B"}:
        labels.add("other")
    if case_id == "C06C":
        labels.add("many")
    if case_id == "C06E":
        labels |= {"rgb", "rgba"}
    assert set(fixtures) == labels
    assert fixtures["source"]["document_id"] == row["source_document_id"]
    assert initial["recording"] is True and initial["workflow"] == initial["results"] == []
    for label, document in fixtures.items():
        variant = ("quiet" if case_id == "C03B" else "all_nan" if case_id == "C06D" else "finite")
        _literal(document, variant=variant)
        expected_shape = SHAPE
        expected_dtype = "uint16" if case_id in {"C01B", "C05A"} else "uint8" if case_id == "C01A" else "float32"
        if case_id in {"C05C", "C06C"} and label == "source":
            expected_shape = [1, 1, 4, 3, 5, 1]
        if case_id == "C06C" and label == "many":
            expected_shape, expected_dtype = [2, 9, 4, 3, 5, 1], "uint16"
        if case_id == "C06E":
            expected_shape = [1, 1, 4, 3, 5, {"source": 1, "rgb": 3, "rgba": 4}[label]]
            expected_dtype = {"source": "float64", "rgb": "uint8", "rgba": "float32"}[label]
        assert document["shape"] == expected_shape and document["dtype"] == expected_dtype
        assert document["metadata"]["properties"] == {"source_only": {"nested": [True, 7, "retained"]},
                                                       "revision_probe": 7}
        assert document["metadata"]["extensions"] == {"future_annotation": {"version": 37, "opaque": [3, 1, 4]}}
        assert document["metadata"]["record_layout"] == {"source_presence": True}
        assert document["selection"] == {"kind": "rectangle", "points": [[1, 0], [4, 3]], "width": 1}
        assert document["previous_selection"] == {"kind": "line", "points": [[0, 0], [2, 2]], "width": 1}
        assert document["overlays"] == [] and document["spectrum_present"] is False
        assert document["dirty"] is False and document["undo_steps"] == document["redo_steps"] == 1
        if expected_shape[0] > 1 and expected_shape[1] > 1:
            assert document["rois"] == [
                {"name": "explicit zero", "selection": {"kind": "rectangle", "points": [[1, 0], [3, 2]], "width": 1},
                 "t": 0, "c": 0, "z": 0, "attributes": {}},
                {"name": "wildcard", "selection": {"kind": "point", "points": [[2, 1]], "width": 1},
                 "t": None, "c": None, "z": 1, "attributes": {}},
            ]
        else:
            assert document["rois"] == []
        properties = document["display"]["properties"]
        expected = _display_state(expected_shape[1])
        if case_id in {"C01A", "C01B", "C06E"}:
            expected = None
        elif case_id == "C03A":
            expected = _display_state(bounds=(0., 100.))
            expected["channels"][1]["lut"] = {"name": "Red"}
        elif case_id == "C03B":
            expected = _display_state(bounds=(7., 19.))
        elif case_id == "C06D":
            expected["channels"][1]["lut"] = {"table": _table()}
        elif case_id == "C06C" and label == "many":
            expected = _display_state(9, "color", (0., 511.))
            for c, channel in enumerate(expected["channels"]):
                channel["lut"] = {"name": ("Red", "Green", "Blue")[c % 3]}
        _assert_stored(document, expected)
        if case_id == "C01B":
            assert properties == {"display_range": [0., 255.], "lut": "Inverted",
                                  "custom_lut": [[255-i, i, 0] for i in range(256)]}
        else:
            assert set(properties) == ({"channel_display"} if expected is not None else set())
        _unchanged(document, _owner(initial, document["document_id"]), display=True)
    return initial


def _assert_case(row, case_id, context, directory):
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert not ({"pass", "passed", "expected", "expected_rgb", "expected_state"} & row.keys())
    assert row["limits"]["max_frames"] == 180 and row["limits"]["max_seconds"] == 15
    assert row["limits"]["max_report_bytes"] == MAX_REPORT
    assert row["limits"]["max_archive_bytes"] == MAX_ARCHIVE and row["limits"]["max_documents"] == 16
    assert row["limits"]["max_shape_tczyxs"] == [2, 9, 4, 3, 5, 4]
    assert 0 <= _number(row["elapsed_seconds"]) <= 15
    assert row["fixture_recipe"] == "40*t+30*c+5*z+2*y+x+2*s"
    _integer(row["source_document_id"], minimum=1)
    _physical(row)
    _physical_display_commands(row)
    _fixtures(row, case_id)
    checkpoints = ("initial", *EXPECTED_CHECKPOINTS[case_id], "final")
    assert set(row["states"]) == set(checkpoints)
    assert [row["states"][name]["frame_ordinal"] for name in checkpoints] == sorted(
        {row["states"][name]["frame_ordinal"] for name in checkpoints})
    assert [value["name"] for value in row["api_observations"]] == [
        "fixed_fixture_preparation", *EXPECTED_API_OBSERVATIONS.get(case_id, ())]
    assert [value["label"] for value in row["preview_observations"]] == (
        ["preview.ready"] if case_id.startswith("C04") else [])
    assert set(row["files"]) == ({"lut_input", "lut_export"} if case_id == "C03B" else
                                 {"native_session"} if case_id == "C05A" else
                                 {"opaque_37", "opaque_1"} if case_id == "C05C" else set())
    for name in row["states"]:
        _state(row, name)
    for key in row["files"]:
        _file(row, directory, key)
    _scenario(row, case_id, directory)
    final, _ = _state(row, "final")
    assert row["window_error"] == "" and row["final_usage"] == ZERO_USAGE
    assert final["busy"] is False


def _modes(row, case_id):
    initial, _ = _state(row, "initial")
    source_id = row["source_document_id"]
    original = _owner(initial, source_id)
    expected = _display_state()
    if case_id == "C01B":
        for channel in expected["channels"]:
            channel["lut"] = {"table": [[255-i, i, 0] for i in range(256)]}
    for mode in ("color", "grayscale", "composite"):
        expected["mode"] = mode
        ordinary = _view(row, mode+".ordinary", expected)
        linked = _view(row, mode+".linked", expected, linked=True)
        for state, rendered, source in (ordinary, linked):
            assert [source["display"]["t"], source["display"]["c"], source["display"]["crosshair"][2]] == [1, 2, 1]
            _unchanged(original, source)
            assert {key: value for key, value in source["display"]["properties"].items()
                    if key != "channel_display"} == {key: value for key, value in original["display"]["properties"].items()
                                                      if key != "channel_display"}
        assert ordinary[1]["ordinary_pixels"]["readback"]["rendered_rgb"] == linked[1]["orthogonal_planes"][0]["readback"]["rendered_rgb"]
    _selectors(row, "menu.action.show_channel_display", "channel_display.mode",
               "channel_display.mode.color", "channel_display.mode.grayscale", "channel_display.mode.composite",
               "orthogonal.toggle")
    if case_id in {"C01A", "C01B"}:
        _selectors(row, "channel_display.enable", "channel_display.disable")
        disabled, _, source = _snapshot(row, "disabled")
        _assert_stored(source, None)
        assert _typed(source["display"]["properties"]) == _typed(original["display"]["properties"])
        _unchanged(original, source)
        assert disabled["workflow"][-1]["action"] == "disable"
        _record(disabled["workflow"][-1], source, None, "disable")
    if case_id == "C01A":
        _selectors(row, "channel_display.channel", "channel_display.channel.1")
        _, _, changed = _snapshot(row, "channel.changed", expected)
        assert changed["display"]["c"] == 1
        _unchanged(original, changed)
    else:
        assert not any(item["selector"] == "channel_display.channel.1"
                       for item in row["control_observations"])


def _drafts(row):
    initial, _ = _state(row, "initial")
    source_id = row["source_document_id"]
    for label in ("draft.partial", "draft.disabled_apply", "draft.failed_action", "draft.unrelated", "draft.focus_return"):
        state, rendered, source = _snapshot(row, label)
        panel = rendered["channel_display_panel"]
        assert panel["open"] is True and panel["source_id"] == source_id
        assert panel["minimum_text"] == "-" and panel["maximum_text"] == "255"
        assert panel["draft_valid"] is False and panel["draft_range"] is None and panel["range_error"]
        assert panel["control_states"]["channel_display.apply"]["enabled"] is False
        _unchanged(_owner(initial, source_id), source)
    _no_publication(initial, _state(row, "draft.disabled_apply")[0])
    failed, rendered = _state(row, "draft.failed_action")
    assert rendered["error_modal"]["open"] is True
    assert _stored(_owner(failed, source_id)) == _display_state()
    malformed = _api(row, "malformed_workflow_precondition")
    assert malformed["accepted"] is True and malformed["after"]["workflow"] == {"malformed": True}
    restored = _api(row, "restore_workflow")
    assert restored["accepted"] is True and restored["after"]["workflow"] == []
    expected = _display_state(mode="color")
    expected["channels"][1]["lut"] = {"name": "Blue"}
    expected["channels"][0]["visible"] = False
    _assert_stored(_snapshot(row, "draft.unrelated")[2], expected)
    _assert_stored(_snapshot(row, "draft.focus_return")[2], expected)
    expected["channels"][1]["range"] = [10., 200.]
    _, rendered, source = _snapshot(row, "draft.applied", expected)
    panel = rendered["channel_display_panel"]
    assert panel["minimum_text"] == "10" and panel["maximum_text"] == "200"
    assert panel["draft_valid"] is True and panel["draft_range"] == [10., 200.]
    _unchanged(_owner(initial, source_id), source)
    _selectors(row, "channel_display.minimum", "channel_display.maximum", "channel_display.apply",
               "channel_display.mode.color", "channel_display.lut.Blue", "channel_display.visible.0",
               "channel_display.close", "error_modal.ok")
    for value in ("-", "10", "200"):
        _text(row, value)
    assert any(item["selector"] == "channel_display.apply" and item["disabled_attempt"]
               for item in row["control_observations"])


def _ranges(row):
    source_id = row["source_document_id"]
    expected = _display_state()
    expected["channels"][1]["range"] = [11., 91.]
    _snapshot(row, "range.c1", expected)
    expected["channels"][0]["range"] = [5., 101.]
    _snapshot(row, "range.c0", expected)
    navigated, _, source = _snapshot(row, "range.navigated", expected)
    assert [source["display"]["t"], source["display"]["c"], source["display"]["crosshair"][2]] == [0, 1, 3]
    expected["channels"][1]["range"] = [45., 53.]
    reset, rendered, _ = _snapshot(row, "range.reset_current", expected)
    _display_change(navigated, reset, source_id, expected, action="reset_range", channels=[1], empty=[False])
    assert rendered["channel_display_panel"]["minimum_text"] == "45"
    assert rendered["channel_display_panel"]["maximum_text"] == "53"
    for c, channel in enumerate(expected["channels"]):
        channel["range"] = [15.+30*c, 23.+30*c]
    all_reset, rendered, _ = _snapshot(row, "range.reset_all", expected)
    _display_change(reset, all_reset, source_id, expected, action="reset_range",
                    channels=[0, 1, 2], empty=[False]*3)
    assert rendered["channel_display_panel"]["draft_range"] == [45., 53.]
    _selectors(row, "channel_display.channel.0", "navigation.time", "navigation.channel", "image.slice",
               "channel_display.reset_current", "channel_display.reset_all")
    for value in ("11", "91", "5", "101", "1", "2", "4"):
        _text(row, value)


def _visibility(row):
    expected = _display_state(bounds=(0., 100.))
    expected["channels"][1]["lut"] = {"name": "Red"}
    _view(row, "visible.ordinary", expected)
    _view(row, "visible.linked", expected, linked=True)
    expected["channels"][1]["visible"] = False
    _view(row, "visible.hidden_active", expected, linked=True)
    for channel in expected["channels"]:
        channel["visible"] = False
    _, rendered, _ = _view(row, "visible.hidden_all", expected, linked=True)
    assert all(value == 0 for plane in rendered["orthogonal_planes"]
               for value in plane["readback"]["rendered_rgb"]["pixels"])
    expected["mode"] = "color"
    _view(row, "visible.color", expected, linked=True)
    expected["mode"] = "grayscale"
    _view(row, "visible.grayscale", expected, linked=True)
    expected["mode"] = "composite"
    for channel in expected["channels"]:
        channel["visible"] = True
    _view(row, "visible.composite_restored", expected, linked=True)
    _selectors(row, "channel_display.visible.0", "channel_display.visible.1", "channel_display.visible.2",
               "channel_display.mode.color", "channel_display.mode.grayscale", "channel_display.mode.composite")


def _lut_files(row, directory):
    expected = _display_state(bounds=(7., 19.))
    expected["channels"][1]["lut"] = {"table": _table()}
    before, _ = _state(row, "initial")
    loaded = _view(row, "lut.loaded", expected, variant="quiet")
    linked = _view(row, "lut.linked", expected, linked=True, variant="quiet")
    exported = _view(row, "lut.exported", expected, linked=True, variant="quiet")
    for state, _, source in (loaded, linked, exported):
        _literal(source, variant="quiet")
        _unchanged(_owner(before, source["document_id"]), source)
    assert _typed(exported[0]["workflow"]) == _typed(linked[0]["workflow"])
    for key in ("lut_input", "lut_export"):
        value = _decode(_file(row, directory, key))
        assert value == {"format": "imagejxx-lut", "lut": _table()}
    assert _file(row, directory, "lut_input") != b""
    _selectors(row, "channel_display.load_lut", "browser.path", "browser.submit", "menu.Image.Lookup_table",
               "menu.path.save_lut")
    for key in ("lut_input", "lut_export"):
        _text(row, str(_member(directory, row["files"][key]["member"])))


def _provenance(rendered, expected, *, linked, active, preview, generation):
    images = rendered["orthogonal_planes"] if linked else [rendered["ordinary_pixels"]]
    contributors = ([c for c, channel in enumerate(expected["channels"]) if channel["visible"]]
                    if expected["mode"] == "composite" else [active])
    for image in images:
        diagnostic = image["channel_display"]
        assert diagnostic["state"] == expected
        assert [item["c"] for item in diagnostic["contributors"]] == contributors
        for c, item in zip(contributors, diagnostic["contributors"]):
            staged = preview and c == 1
            assert item["preview"] is staged and item["generation"] == (generation if staged else 0)
            assert item["range"] == expected["channels"][c]["range"]
            assert item["preview_z"] == ([2] if staged else [])


def _cache_changed(before, after):
    assert [plane["plane"] for plane in before["orthogonal_planes"]] == ["XY", "XZ", "YZ"]
    for old, new in zip(before["orthogonal_planes"], after["orthogonal_planes"]):
        assert _integer(new["builds"], minimum=1) > _integer(old["builds"], minimum=1)


def _previews(row, case_id):
    generation, preview_inputs = _preview(row, "preview.ready")
    expected = _display_state()
    initial, _ = _state(row, "initial")
    _view(row, "preview.ready", expected, preview=True, generation=generation,
          preview_inputs=preview_inputs, threshold=case_id == "C04B")
    if case_id == "C04A":
        ordinary = _view(row, "preview.nonactive_ordinary", expected, preview=True, generation=generation,
                         preview_inputs=preview_inputs)
        linked = _view(row, "preview.nonactive_linked", expected, linked=True, preview=True, generation=generation,
                       preview_inputs=preview_inputs)
        assert ordinary[2]["display"]["c"] == linked[2]["display"]["c"] == 0
        assert ordinary[1]["ordinary_pixels"]["pixels_source"] == "live"
        assert ordinary[1]["ordinary_pixels"]["preview_generation"] == 0
        for c, channel in enumerate(expected["channels"]):
            channel["range"] = [50.+30*c, 58.+30*c]
        reset, rendered, source = _view(row, "preview.live_reset", expected, linked=True, preview=True,
                                       generation=generation, preview_inputs=preview_inputs)
        _display_change(linked[0], reset, source["document_id"], expected,
                        action="reset_range", channels=[0, 1, 2], empty=[False]*3)
        _provenance(rendered, expected, linked=True, active=0, preview=True, generation=generation)
        _cache_changed(linked[1], rendered)
        _unchanged(_owner(initial, source["document_id"]), source)
        assert reset["usage"] == linked[0]["usage"]
    elif case_id == "C04B":
        preparation = _api(row, "threshold_overlay_preparation")
        assert preparation["accepted"] is True
        overlay = {"kind": "roi", "selection": {"kind": "rectangle", "points": [[1, 0], [4, 3]], "width": 1,
                    "imagejxx.roi_style": {"schema_version": 1, "fill_color": [18, 52, 86, 128], "stroke_color": None}},
                   "t": 1, "c": 1, "z": 2}
        assert preparation["overlay"] == overlay
        prepared = _owner(preparation["after"], row["source_document_id"])
        assert prepared["overlays"] == [overlay]
        assert prepared["display"]["properties"]["manual_threshold"] == {
            "version": 1, "lower": 83., "upper": 85., "t": 1, "c": 1, "z": 2}
        linked = _view(row, "preview.threshold_linked", expected, linked=True, preview=True,
                       generation=generation, preview_inputs=preview_inputs, threshold=True)
        away = _view(row, "preview.away", expected, linked=True)
        assert away[2]["display"]["crosshair"][2] == 3
        back = _view(row, "preview.back", expected, linked=True, preview=True,
                     generation=generation, preview_inputs=preview_inputs, threshold=True)
        assert linked[1]["orthogonal_planes"][0]["readback"]["rendered_rgb"] == back[1]["orthogonal_planes"][0]["readback"]["rendered_rgb"]
        _cache_changed(linked[1], away[1])
        _cache_changed(away[1], back[1])
        for current in (linked, back):
            xy, xz, yz = current[1]["orthogonal_planes"]
            assert len(xy["overlay_layers"]) == 1
            assert xz["overlay_layers"] == yz["overlay_layers"] == []
            _unchanged(prepared, current[2])
        assert all(plane["overlay_layers"] == [] for plane in away[1]["orthogonal_planes"])
        expected["channels"][1]["visible"] = False
        hidden = _view(row, "preview.hidden", expected, linked=True, preview=True, generation=generation,
                       preview_inputs=preview_inputs, threshold=True)
        _cache_changed(back[1], hidden[1])
        expected["mode"] = "color"
        _view(row, "preview.color_hidden", expected, linked=True, preview=True, generation=generation,
              preview_inputs=preview_inputs, threshold=True)
        expected["mode"] = "grayscale"
        grayscale = _view(row, "preview.grayscale_hidden", expected, linked=True, preview=True,
                          generation=generation, preview_inputs=preview_inputs, threshold=True)
        assert grayscale[2]["display"]["properties"]["manual_threshold"] == prepared["display"]["properties"]["manual_threshold"]
        _selectors(row, "orthogonal.slice.XY", "channel_display.visible.1")
    else:
        before = _view(row, "preview.before_tab", expected, preview=True, generation=generation,
                       preview_inputs=preview_inputs)
        other_id = row["fixture_sources"]["other"]["document_id"]
        other = _view(row, "preview.other_owner", expected, document_id=other_id)
        returned = _view(row, "preview.returned", expected)
        reopened = _view(row, "preview.panel_reopened", expected)
        assert other[0]["active_document_id"] == other_id
        assert reopened[1]["channel_display_panel"]["open"] is True
        assert other[0]["usage"] == returned[0]["usage"] == reopened[0]["usage"] == ZERO_USAGE
        assert before[1]["ordinary_pixels"]["readback"]["rendered_rgb"] != returned[1]["ordinary_pixels"]["readback"]["rendered_rgb"]
        assert returned[1]["ordinary_pixels"]["readback"] == reopened[1]["ordinary_pixels"]["readback"]
        before_jobs = before[1]["gaussian_jobs"]
        canceled_jobs = other[1]["gaussian_jobs"]
        assert before_jobs["preview"]["exit"] == "installed"
        assert before_jobs["preview"]["job"]["state"] == "ready"
        assert before[1]["gaussian_panel"]["open"] is True
        assert before[1]["gaussian_panel"]["exit_reason"] == ""
        for current in (other, returned, reopened):
            jobs = current[1]["gaussian_jobs"]
            assert jobs["active_installed_generation"] is None and jobs["preview"]["owns_job"] is False
            # The qualified panel source-change policy explicitly cancels on
            # tab-away: GaussianPanel::draw closes with source_changed, then
            # GaussianJobs::cancel_preview records canceled. Poll's distinct
            # source_invalid/reject_generation path is not this UI transition.
            assert current[1]["gaussian_panel"]["open"] is False
            assert current[1]["gaussian_panel"]["exit_reason"] == "source_changed"
            assert jobs["preview"]["exit"] == "canceled"
            assert jobs["preview"]["error"] == "Gaussian preview was canceled"
            assert jobs["preview"]["job"]["state"] == "canceled"
            assert jobs["preview"]["job"]["cancel_requested"] is True
            assert jobs["preview"]["job"]["id"] == before_jobs["preview"]["job"]["id"]
            assert jobs["requested_generation"] == generation
            assert jobs["rejected_generations"] == before_jobs["rejected_generations"] == []
            assert jobs["rejected_count"] == before_jobs["rejected_count"] == 0
            assert _typed(jobs) == _typed(canceled_jobs)
            _unchanged(_owner(initial, row["source_document_id"]), _owner(current[0], row["source_document_id"]))
        _selectors(row, f"document.{other_id}", f"document.{row['source_document_id']}", "channel_display.close")
    final, final_rendered, _ = _view(row, "final", expected, linked=case_id != "C04C", preview=case_id != "C04C",
                                    generation=generation, preview_inputs=preview_inputs, threshold=case_id == "C04B")
    assert final["gaussian_host"]["gaussian_jobs"]["preview"]["job"]["state"] == (
        "canceled" if case_id == "C04C" else "ready")
    if case_id == "C04C":
        assert _typed(final_rendered["gaussian_jobs"]) == _typed(canceled_jobs)
        assert final_rendered["gaussian_panel"]["open"] is False
        assert final_rendered["gaussian_panel"]["exit_reason"] == "source_changed"
        assert final["usage"] == ZERO_USAGE


def _persistence(row, directory):
    expected = _display_state()
    expected["channels"][1]["range"] = [11., 91.]
    range_state, _, source_range = _snapshot(row, "saved.range", expected)
    expected["channels"][1]["lut"] = {"name": "Blue"}
    edited, _, source = _snapshot(row, "saved.edited", expected)
    _display_change(range_state, edited, source["document_id"], expected)
    saved, _, saved_source = _snapshot(row, "saved", expected)
    _unchanged(source, saved_source, display=True)
    assert row["original_owner_id"] == source["document_id"] == row["source_document_id"]
    records = row["original_records"]
    assert records == saved["workflow"] and len(records) == 2
    _record(records[0], source_range, _stored(source_range))
    _record(records[1], source, expected)
    root = _session(row, directory, "native_session")
    assert len(root["documents"]) == 1 and root["results"] == saved["results"]
    assert _typed(root["workflow"]) == _typed(records)
    _session_document(root["documents"][0], saved_source)
    reopened_id = _integer(row["reopened_owner_id"], minimum=1)
    assert reopened_id != source["document_id"]
    reopened, _, opened = _snapshot(row, "reopened", expected, document_id=reopened_id)
    assert reopened["active_document_id"] == reopened_id
    _literal(opened)
    for name in ("shape", "dtype", "sample_policy", "calibration", "metadata", "selection",
                 "previous_selection", "rois", "overlays", "spectrum_present"):
        assert _typed(opened[name]) == _typed(saved_source[name]), name
    assert _typed(opened["display"]["properties"]) == _typed(saved_source["display"]["properties"])
    assert _typed(reopened["workflow"]) == _typed(records)
    assert all(record["source"]["id"] == source["document_id"] for record in reopened["workflow"])
    assert opened["data_identity"] != saved_source["data_identity"]
    assert [plane["storage_f64_hex"] for plane in opened["planes"]] == [plane["storage_f64_hex"] for plane in saved_source["planes"]]
    _selectors(row, "channel_display.lut.Blue", "menu.path.save_document", "menu.file.open", "browser.path", "browser.submit")
    _text(row, str(_member(directory, row["files"]["native_session"]["member"])))


def _replay(row):
    source_id, other_id = row["source_document_id"], row["fixture_sources"]["other"]["document_id"]
    expected = _display_state()
    expected["channels"][1]["range"] = [80., 88.]
    original, _, source = _snapshot(row, "record.original", expected)
    assert row["original_owner_id"] == source_id
    assert len(row["original_records"]) == 1 and row["original_records"] == original["workflow"]
    record = row["original_records"][0]
    _record(record, source, expected, "reset_range", [1], [False])
    navigated, _, current = _snapshot(row, "record.navigated", expected)
    assert [current["display"]["t"], current["display"]["c"], current["display"]["crosshair"][2]] == [0, 0, 0]
    replayed, _, replay_source = _snapshot(row, "record.replayed", expected)
    assert replayed["workflow"] == [record, record]
    _unchanged(current, replay_source)
    for name in ("t", "c", "crosshair", "zoom", "linked_zoom", "pan", "plane_zoom", "orthogonal"):
        assert _typed(replay_source["display"][name]) == _typed(current["display"][name])
    # Strict replay must retain the resolved T1/C1/Z2 range after T0/C0/Z0
    # navigation; a current-plane recomputation would instead yield [30,38].
    assert replay_source["display"]["properties"]["channel_display"]["channels"][1]["range"] == [80., 88.]
    opened, rendered, other = _snapshot(row, "record.rebind_opened", document_id=other_id)
    assert rendered["channel_display_rebind"] == {"record": record, "document_id": other_id}
    assert rendered["channel_display_panel"]["source_id"] == other_id
    assert opened["workflow"] == [record, record]
    assert row["rebind_owner_id"] == other_id
    rebound_state = _display_state()
    rebound_state["channels"][1]["range"] = [7., 11.]
    rebound, _, rebound_source = _snapshot(row, "record.rebound", rebound_state, document_id=other_id)
    assert len(rebound["workflow"]) == 3 and rebound["workflow"][:2] == [record, record]
    rebound_record = row["rebound_record"]
    assert rebound_record == rebound["workflow"][2]
    _record(rebound_record, other, rebound_state)
    assert rebound_record["source"]["id"] == other_id != record["source"]["id"]
    _unchanged(other, rebound_source)
    off = _api(row, "recording_off")
    assert off["accepted"] is True and off["after"]["recording"] is False
    assert off["after"]["workflow"] == off["before"]["workflow"]
    after, _, replay_other = _snapshot(row, "record.recording_off_replayed", rebound_state, document_id=other_id)
    assert after["recording"] is False and after["workflow"] == rebound["workflow"]
    _unchanged(rebound_source, replay_other)
    _unchanged(replay_source, _owner(after, source_id), display=True)
    _selectors(row, "channel_display.reset_current", "workflow.channel_display.0.replay",
               "workflow.channel_display.0.rebind", "workflow.channel_display.2.replay")
    _text(row, "7")
    _text(row, "11")


def _opaque(row, directory):
    baseline, _, source = _snapshot(row, "opaque.baseline", _display_state(1))
    for version, schema in ((37, "imagejxx.channel-display"), (1, "opaque.application")):
        key = f"opaque_{version}"
        value = _api(row, key+"_native_roundtrip")
        assert value["accepted"] is True
        before, decoded = value["before_source"], value["decoded_source"]
        expected = {"schema": schema, "version": version,
                    "payload": [True, 9007199254740993, "retained"]}
        assert _stored(before) == _stored(decoded) == expected
        assert type(_stored(decoded)["payload"][1]) is int
        _literal(before)
        _literal(decoded)
        root = _session(row, directory, key)
        assert len(root["documents"]) == 1
        _session_document(root["documents"][0], before)
        assert root["documents"][0]["display"]["properties"]["channel_display"] == expected
        refused, rendered = _state(row, key+".refused")
        _no_publication(baseline, refused)
        assert rendered["error_modal"]["open"] is True
        assert refused["window_error"]
    _refused(row, "legacy_v2_refusal")
    _refused(row, "detached_v2_refusal")
    assert row["forbidden_v2_exists"] is False
    assert not (directory / "forbidden-v2.ijx").exists()
    legacy, _ = _state(row, "legacy.refused")
    _no_publication(baseline, legacy)
    _selectors(row, "menu.file.open", "browser.path", "browser.submit", "error_modal.ok")


def _refusals(row):
    source_id, other_id = row["source_document_id"], row["fixture_sources"]["other"]["document_id"]
    prepared = _api(row, "queued_apply_preparation")
    assert prepared["accepted"] is True
    busy, rendered, source = _snapshot(row, "busy.disabled", _display_state(mode="color"))
    assert busy["busy"] is True and rendered["channel_display_panel"]["busy"] is True
    jobs = prepared["jobs"]["gaussian_jobs"]
    assert jobs["apply"]["owns_job"] is True and jobs["apply"]["job"]["state"] == "queued"
    assert jobs["apply"]["source_id"] == source_id
    for selector in ("channel_display.apply", "channel_display.reset_current"):
        assert rendered["channel_display_panel"]["control_states"][selector]["enabled"] is False
        assert any(item["selector"] == selector and item["disabled_attempt"] for item in row["control_observations"])
    _record(row["guard_record"], source, _display_state(mode="color"))
    for name in ("busy_apply_refusal", "busy_enable_refusal"):
        refusal = _refused(row, name)
        assert refusal["before"]["busy"] is refusal["after"]["busy"] is True
        assert refusal["after"]["gaussian_host"] == refusal["before"]["gaussian_host"]
        assert refusal["after"]["usage"] == refusal["before"]["usage"]
    malformed = _refused(row, "malformed_rebind_refusal")
    assert malformed["inputs"]["record"] == {**row["guard_record"], "unexpected": True}
    foreign = _refused(row, "foreign_owner_refusal")
    assert _owner(foreign["before"], other_id)["document_id"] != row["guard_record"]["source"]["id"]
    stale = _api(row, "stale_metadata_preparation")
    assert stale["accepted"] is True
    a, b = _owner(stale["before"], source_id), _owner(stale["after"], source_id)
    assert b["metadata"]["properties"]["stale"] is True
    assert b["revisions"]["metadata"] > a["revisions"]["metadata"]
    assert b["revisions"]["pixels"] == a["revisions"]["pixels"] and b["planes"] == a["planes"]
    _refused(row, "stale_record_refusal")
    closed = _api(row, "close_owner_preparation")
    assert closed["accepted"] is True and other_id not in closed["after"]["document_ids"]
    _unchanged(_owner(closed["before"], other_id), row["detached_sources"]["closed_other"], display=True)
    _refused(row, "closed_source_refusal")
    complete, _ = _state(row, "guards.complete")
    assert complete["busy"] is False and complete["usage"] == ZERO_USAGE


def _owners(row):
    source_id, other_id = row["source_document_id"], row["fixture_sources"]["other"]["document_id"]
    partial, rendered, source = _snapshot(row, "owner.partial", _display_state())
    assert rendered["channel_display_panel"]["minimum_text"] == "-"
    assert rendered["channel_display_panel"]["draft_valid"] is False
    switched, rendered, _ = _snapshot(row, "owner.switched", _display_state(), document_id=other_id)
    assert switched["active_document_id"] == rendered["channel_display_panel"]["source_id"] == other_id
    assert rendered["channel_display_panel"]["minimum_text"] == "0"
    returned, rendered, restored_source = _snapshot(row, "owner.returned", _display_state())
    assert returned["active_document_id"] == rendered["channel_display_panel"]["source_id"] == source_id
    assert rendered["channel_display_panel"]["minimum_text"] == "0"
    assert rendered["channel_display_panel"]["maximum_text"] == "255"
    assert rendered["channel_display_panel"]["draft_valid"] is True
    _unchanged(source, restored_source, display=True)
    _refused(row, "closed_panel_source_refusal")
    closed = _api(row, "close_current_preparation")
    assert closed["accepted"] is True and source_id not in closed["after"]["document_ids"]
    _unchanged(restored_source, row["detached_sources"]["closed_source"], display=True)
    none, rendered = _state(row, "owner.none")
    assert none["document_ids"] == [] and none["active_document_id"] == 0
    panel = rendered["channel_display_panel"]
    assert panel["open"] is True and panel["source_valid"] is False and panel["source_id"] == 0
    assert panel["control_states"]["channel_display.enable"]["enabled"] is False
    assert none["workflow"] == []
    _selectors(row, f"document.{other_id}", f"document.{source_id}")


def _counts(row):
    expected = _display_state(1)
    _view(row, "count.singleton_ordinary", expected)
    _view(row, "count.singleton_linked", expected, linked=True)
    before, _, source = _snapshot(row, "count.eager_range", expected)
    conversion = _api(row, "eager_display_range_conversion")
    assert conversion["accepted"] is True
    after, _, converted = _snapshot(row, "count.eager_converted", expected)
    _literal(converted)
    assert source["dtype"] == "float32" and converted["dtype"] == "uint8"
    assert converted["shape"] == [1, 1, 4, 3, 5, 1]
    assert converted["data_identity"] != source["data_identity"]
    assert converted["revisions"]["pixels"] > source["revisions"]["pixels"]
    assert converted["undo_steps"] == source["undo_steps"]+1 and converted["redo_steps"] == 0
    assert converted["dirty"] is True
    for name in ("calibration", "selection", "previous_selection", "rois", "overlays"):
        assert _typed(converted[name]) == _typed(source[name])
    many_id = row["fixture_sources"]["many"]["document_id"]
    expected = _display_state(9, "color", (0., 511.))
    for c, channel in enumerate(expected["channels"]):
        channel["lut"] = {"name": ("Red", "Green", "Blue")[c % 3]}
    _, _, many = _snapshot(row, "count.ninth_selected", expected, document_id=many_id)
    assert many["display"]["c"] == 8
    _view(row, "count.fallback_ordinary", expected, document_id=many_id)
    _view(row, "count.fallback_linked", expected, linked=True, document_id=many_id)
    assert any(frame["requested_events"] == [{"type": "wheel", "x": 0, "y": -6}] for frame in row["frames"])
    _selectors(row, "channel_display.channel.8", "channel_display.mode.composite")
    _refused(row, "scientific_conversion_refusal")


def _nan_ranges(row):
    source_id = row["source_document_id"]
    expected = _display_state()
    expected["channels"][1]["lut"] = {"table": _table()}
    expected["channels"][1]["range"] = [0., 1.]
    reset = _view(row, "nan.reset", expected, variant="all_nan")
    _display_change(_state(row, "initial")[0], reset[0], source_id, expected,
                    action="reset_range", channels=[1], empty=[True])
    expected["channels"][1]["range"] = [0., 0.]
    _view(row, "nan.equal", expected, variant="all_nan")
    _view(row, "nan.linked", expected, linked=True, variant="all_nan")
    for label in ("nan.inverted", "nan.disabled_apply"):
        state, rendered, source = _snapshot(row, label, expected)
        panel = rendered["channel_display_panel"]
        assert panel["minimum_text"] == "100" and panel["maximum_text"] == "0"
        assert panel["draft_valid"] is False and panel["draft_range"] is None
        assert panel["range_error"] and panel["control_states"]["channel_display.apply"]["enabled"] is False
        _literal(source, variant="all_nan")
    _no_publication(_state(row, "nan.inverted")[0], _state(row, "nan.disabled_apply")[0])
    _selectors(row, "channel_display.reset_current", "channel_display.minimum", "channel_display.maximum")
    _text(row, "0")
    _text(row, "100")


def _legacy_view(rendered, source):
    pixels = rendered["ordinary_pixels"]
    assert pixels["source_document_id"] == source["document_id"] and pixels["pixels_source"] == "live"
    assert pixels["preview_generation"] == 0 and pixels["readback"]["available"] is True
    t, c, z = source["display"]["t"], source["display"]["c"], source["display"]["crosshair"][2]
    samples, dtype = source["shape"][-1], source["dtype"]
    observed = pixels["readback"]["source"]
    expected_source = [_source_value(t, c, z, y, x, sample)
                       for y in range(3) for x in range(5) for sample in range(samples)]
    assert observed == {"shape": [3, 5, samples], "dtype": dtype, "pixels": expected_source}
    if dtype == "uint8":
        low, high = 0., 255.
    else:
        low, high = float(min(expected_source)), float(max(expected_source))
    expected_rgb = []
    scale, offset = 255/(high-low), -low*255/(high-low)
    for y in range(3):
        for x in range(5):
            mapped = [math.floor(max(0., min(255., scale*_source_value(t, c, z, y, x, sample)+offset))+.5)
                      for sample in range(3 if samples > 1 else 1)]
            expected_rgb.extend(mapped if samples > 1 else mapped*3)
    assert pixels["readback"]["rendered_rgb"] == {"shape": [3, 5, 3], "dtype": "uint8", "pixels": expected_rgb}


def _unsupported(row):
    for profile, label in (("float64", "source"), ("rgb", "rgb"), ("rgba", "rgba")):
        document_id = row["fixture_sources"][label]["document_id"]
        state, rendered, source = _snapshot(row, profile+".legacy", document_id=document_id)
        _assert_stored(source, None)
        _literal(source)
        _legacy_view(rendered, source)
        panel = rendered["channel_display_panel"]
        assert panel["open"] is True and panel["supported"] is False and panel["source_error"]
        assert panel["control_states"]["channel_display.enable"]["enabled"] is False
        _refused(row, profile+"_unsupported_set")
        _unchanged(row["fixture_sources"][label], source, display=True)
        assert state["workflow"] == []
    assert len([item for item in row["control_observations"]
                if item["selector"] == "channel_display.enable" and item["disabled_attempt"]]) == 3


def _scenario(row, case_id, directory):
    if case_id.startswith("C01"):
        _modes(row, case_id)
    elif case_id == "C02A":
        _drafts(row)
    elif case_id == "C02B":
        _ranges(row)
    elif case_id == "C03A":
        _visibility(row)
    elif case_id == "C03B":
        _lut_files(row, directory)
    elif case_id.startswith("C04"):
        _previews(row, case_id)
    elif case_id == "C05A":
        _persistence(row, directory)
    elif case_id == "C05B":
        _replay(row)
    elif case_id == "C05C":
        _opaque(row, directory)
    elif case_id == "C06A":
        _refusals(row)
    elif case_id == "C06B":
        _owners(row)
    elif case_id == "C06C":
        _counts(row)
    elif case_id == "C06D":
        _nan_ranges(row)
    else:
        assert case_id == "C06E"
        _unsupported(row)


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_channel_display(case_id, context, request, tmp_path):
    row, directory = _observe(case_id, context, tmp_path / "observations", request)
    _assert_case(row, case_id, context, directory)
