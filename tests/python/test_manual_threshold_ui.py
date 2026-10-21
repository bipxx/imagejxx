"""Small installed physical-input gate for both shared threshold launches.

All membership, histogram and Apply expectations use the fixed input recipe
below. The native producer records actual controls/pixels and has no oracle.
This gate qualifies headless Dear ImGui interaction, not real GPU platforms.
"""

import base64
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import subprocess
import sysconfig
import time

import pytest


def _stream_sha256(stream):
    """Hash remaining binary stream bytes with bounded memory on Python 3.10+."""
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


CONTRACT = "p05k.manual-threshold-ui.001"
CASES = tuple(f"T{i:02d}" for i in range(1, 10))
CONTEXTS = ("imgui_cpp", "imgui_python")
MAX_REPORT_BYTES = 16 * 1024 * 1024
DTYPES = {"T02": "float32", "T03": "float64", "T04": "uint16",
          "T05": "float64", "T08": "uint16"}
BOUNDS = {"T02": (-3.5, .5), "T03": (1, 1), "T04": (11101, 11303),
          "T05": (-1.5, 1.5), "T08": (1101, 1505)}
ROI_ROWS = ("000000000", "011111000", "010001000", "010101000", "010001000",
            "011111000", "000000010", "000000000", "000000000")
PIXEL_FIELDS = ("document_id", "data_identity", "dirty", "undo_steps", "redo_steps",
                "dtype", "shape", "pixels", "selection", "previous_selection", "metadata")


def _unique(pairs):
    value = {}
    for key, item in pairs:
        assert key not in value, f"Duplicate observation key {key}"
        value[key] = item
    return value


def _constant(value):
    raise AssertionError(f"Nonfinite observation JSON {value}")


def _decode(raw):
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    assert isinstance(raw, bytes) and 0 < len(raw) <= MAX_REPORT_BYTES
    return json.loads(raw, object_pairs_hook=_unique, parse_constant=_constant)


def _read(path):
    assert path.is_file() and not path.is_symlink()
    assert 0 < path.stat().st_size <= MAX_REPORT_BYTES
    return _decode(path.read_bytes())


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_CPP_EXE to the current installed desktop")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed desktop")
    path = Path(configured).resolve(strict=True)
    assert path.is_file() and path.name.lower() in {"imagejxx.exe", "imagejxx"}
    assert path.parent.name.lower() == "bin", "Use the installed desktop"
    expected = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if expected:
        with path.open("rb") as stream:
            assert _stream_sha256(stream) == expected
    return path


def _observe(context, case_id, directory, request):
    assert not directory.exists()
    started = time.monotonic()
    if context == "imgui_cpp":
        executable = request.getfixturevalue("installed_cpp_executable")
        result = subprocess.run(
            [str(executable), "--manual-threshold-ui-case", case_id, str(directory)],
            capture_output=True, timeout=60, check=False,
        )
        assert len(result.stdout) + len(result.stderr) <= MAX_REPORT_BYTES
        assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", "replace")
        row = _read(directory / "case.json")
    else:
        desktop = (importlib.import_module("imagejxx._desktop") if _required()
                   else pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError))
        module = Path(desktop.__file__).resolve(strict=True)
        assert module.suffix in {".pyd", ".so"}
        if _required():
            roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx"
                     for key in ("platlib", "purelib")}
            target_site = os.environ.get("IMAGEJXX_INSTALLED_SITE")
            if target_site:
                target = Path(target_site).resolve(strict=True)
                assert (target / "imagejxx-0.2.0.dist-info" / "METADATA").is_file()
                roots.add(target / "imagejxx")
            assert any(module.is_relative_to(root) for root in roots), "Checkout UI import"
        editor = desktop.Editor(preferences_path=str(directory.parent / "preferences.json"))
        row = _decode(editor._manual_threshold_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row


def _pixels(case_id, *, other=False):
    depth = 2 if case_id == "T04" and not other else 1
    result = []
    for z in range(depth):
        plane = []
        for y in range(9):
            for x in range(9):
                if other:
                    value = 177
                elif case_id == "T02":
                    value = (x - 4) * 1.25 + (y - 4) * .5
                elif case_id == "T03":
                    value = int(max(abs(x - 4), abs(y - 4)) == 2 or (x, y) == (0, 0))
                elif case_id == "T05":
                    value = (x - 4) * .75 + (y - 4) * .125
                elif case_id in {"T04", "T08"}:
                    value = 1000 + 10000 * z + 100 * y + x
                else:
                    value = (31 * x + 17 * y + 7 * x * y) % 256
                plane.append(value)
        result.append(plane)
    return result


def _integer(value, positive=False):
    assert type(value) is int and value >= int(positive)
    return value


def _frame(row, label):
    matches = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(matches) == 1, f"Missing/duplicate actual frame {label}"
    return matches[0]


def _panel(row, label):
    return _frame(row, label)["rendered"]["threshold_panel"]


def _state(row, label):
    return row["states"][label]["source"]


def _actual_control(rendered, selector):
    if selector.startswith("threshold."):
        name = selector.removeprefix("threshold.")
        panel = rendered["threshold_panel"]
        return {**panel["control_states"][name], "rect": panel["control_rects"][name],
                "point": panel["controls"][name]}
    if selector.startswith("document."):
        doc_id = int(selector.removeprefix("document."))
        matches = [tab for tab in rendered["document_tabs"] if tab["document_id"] == doc_id]
        assert len(matches) == 1
        return matches[0]
    return rendered["host_controls"][selector]


def _physical(row):
    frames = row["frames"]
    assert 1 <= len(frames) == row["frame_count"] <= 180
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    held, mouse, position = set(), [False] * 3, None
    identities = []
    for frame in frames:
        rendered = frame["rendered"]
        session = rendered["session"]
        assert session["open"] is True
        _integer(session["session_id"], True)
        _integer(session["context_id"], True)
        assert session["current_context_id"] == session["context_id"]
        identities.append(session)
        observed = rendered["input_frame"]
        assert observed["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            kind = event["type"]
            if kind == "position":
                assert event["space"] == "screen"
                position = event["point"]
            elif kind == "button":
                assert type(event["down"]) is bool and event["name"] == "left"
                mouse[0] = event["down"]
            elif kind == "keys":
                for atom in event["keys"]:
                    assert atom["name"] in {"Ctrl", "A", "Z"} and type(atom["down"]) is bool
                    (held.add if atom["down"] else held.discard)(atom["name"])
            else:
                assert kind == "text" and isinstance(event["value"], str)
        if position is not None:
            assert observed["mouse_position"] == position
        assert observed["mouse_down"] == mouse and set(observed["keys_down"]) == held
        assert observed["modifiers"] == {"ctrl": "Ctrl" in held, "shift": False,
                                          "alt": False, "super": False}
    assert not held and mouse == [False] * 3
    assert len({item["session_id"] for item in identities}) == 1
    assert len({item["context_id"] for item in identities}) == 1
    for first, second in zip(identities, identities[1:]):
        assert second["imgui_frame"] == first["imgui_frame"] + 1
        assert second["imgui_time"] > first["imgui_time"]
    for observation in row["control_observations"]:
        ordinal = _integer(observation["before_frame_ordinal"], True)
        actual = _actual_control(frames[ordinal - 1]["rendered"], observation["selector"])
        assert actual == observation["control"]
        _integer(actual["id"], True)
        assert type(actual["enabled"]) is bool
        assert actual["enabled"] is not observation["disabled_attempt"]
        rect, point = actual["rect"], observation["position"]
        assert all(type(v) in {int, float} and math.isfinite(v) for v in rect + point)
        assert point == [math.floor(v) for v in actual["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        if observation["selector"].startswith("document."):
            pressed = _actual_control(frames[ordinal + 1]["rendered"], observation["selector"])
            assert pressed["active"] is True, "Document tab pointer was intercepted"
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == identities[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert closed["imgui_frame"] == identities[-1]["imgui_frame"]
    selectors = [item["selector"] for item in row["control_observations"]]
    assert {"menu.Process", "menu.manual_threshold", "threshold.lower", "threshold.upper",
            "threshold.set"} <= set(selectors)


def _same_pixels(before, after, *, selection=True):
    for name in PIXEL_FIELDS:
        if selection or name not in {"selection", "previous_selection"}:
            assert after[name] == before[name], name
    for name in ("pixels", "metadata") + (("selection",) if selection else ()):
        assert after["revisions"][name] == before["revisions"][name], name


def _histogram(row, case_id):
    histogram = _panel(row, "opened")["histogram"]
    source = _pixels(case_id)[1 if case_id == "T04" else 0]
    assert histogram["minimum"] == min(source) and histogram["maximum"] == max(source)
    assert histogram["sample_count"] == 81 and histogram["bins"] == 256
    if DTYPES.get(case_id, "uint8") == "uint8":
        edges = list(range(257))
    elif DTYPES[case_id] == "uint16":
        edges = [256 * i for i in range(257)]
    else:
        low, high = min(source), max(source)
        edges = [low + (high - low) * i / 256 for i in range(257)]
    assert histogram["edges"] == edges
    counts = [0] * 256
    for value in source:
        index = math.floor((value - edges[0]) / (edges[-1] - edges[0]) * 256)
        counts[min(255, max(0, index))] += 1
    assert histogram["counts"] == counts
    assert _panel(row, "overlay_set")["histogram"] == histogram


def _range_state(state, lower, upper, z=0):
    assert state["display"]["properties"]["manual_threshold"] == {
        "version": 1, "lower": lower, "upper": upper, "t": 0, "c": 0, "z": z,
    }
    assert state["display"]["properties"]["lut"] == "Grays"


def _membership(selection, expected):
    assert selection["kind"] == "mask" and selection["shape"] == [9, 9]
    assert selection["points"] == [] and selection["width"] == 1
    assert selection["observed_membership"] == [255 if item else 0 for item in expected]
    packed = base64.b64decode(selection["mask"], validate=True)
    assert len(packed) == 11 and packed[-1] & 127 == 0
    assert [bool(packed[i // 8] & (1 << (7 - i % 8))) for i in range(81)] == expected


def _assert_case(row, case_id, context):
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert row["limits"] == {"max_frames": 180, "max_seconds": 15,
                              "max_report_bytes": MAX_REPORT_BYTES, "max_shape_zyx": [2, 9, 9]}
    assert 0 <= row["elapsed_seconds"] <= 15 and row["window_error"] == ""
    _physical(row)
    before, overlay, final = (_state(row, key) for key in ("before", "overlay_set", "final"))
    assert before["pixels"] == _pixels(case_id)
    assert before["dtype"] == DTYPES.get(case_id, "uint8")
    assert before["shape"] == [1, 1, 2 if case_id == "T04" else 1, 9, 9, 1]
    assert before["undo_steps"] == before["redo_steps"] == 0 and before["dirty"] is False
    for state in row["states"].values():
        assert state["workflow"] == state["results"] == []
        assert state["source"]["metadata"] == before["metadata"]
        assert state["source"]["display"]["properties"]["lut"] == "Grays"
        _integer(state["source"]["document_id"], True)
    _same_pixels(before, overlay)
    lower, upper = BOUNDS.get(case_id, (60, 140))
    _range_state(overlay, lower, upper, int(case_id == "T04"))
    panel = _panel(row, "overlay_set")
    assert panel["draft_valid"] is True and panel["source_valid"] is True
    assert panel["lower"] == lower and panel["upper"] == upper
    assert panel["error"] == panel["action_error"] == ""
    assert panel["source_id"] == before["document_id"]
    assert panel["source_position"] == {"t": 0, "c": 0, "z": int(case_id == "T04")}
    _histogram(row, case_id)
    for field, value in (("lower", lower), ("upper", upper)):
        text_frame = _frame(row, field + "_edited.text")
        assert text_frame["requested_events"][0]["type"] == "text"
        assert float(text_frame["requested_events"][0]["value"]) == value
        active = text_frame["rendered"]["threshold_panel"]["control_states"][field]
        assert active["active"] is True
        assert active["id"] == text_frame["rendered"]["input_frame"]["focus"]["active_id"]

    if case_id == "T01":
        readback = _frame(row, "overlay_set")["rendered"]["ordinary_pixels"]["readback"]
        assert readback["available"] is True
        source = _pixels(case_id)[0]
        assert readback["source"]["pixels"] == source
        expected = [component for value in source for component in
                    ([255, value // 2, value // 2] if lower <= value <= upper else [value] * 3)]
        assert readback["rendered_rgb"]["pixels"] == expected
        reset = _state(row, "reset")
        _same_pixels(before, reset)
        assert "manual_threshold" not in reset["display"]["properties"]
        assert _panel(row, "reset")["lower"] == min(source)
        assert _panel(row, "reset")["upper"] == max(source)
    elif case_id == "T02":
        assert _panel(row, "opened")["lower"] == -2 and _panel(row, "opened")["upper"] == 2
        _same_pixels(before, final)
        assert final["display"]["properties"] == before["display"]["properties"]
    elif case_id == "T03":
        selected, repeated = _state(row, "selected"), _state(row, "selected_again")
        expected = [bool(value) for value in _pixels(case_id)[0]]
        _membership(selected["selection"], expected)
        assert expected[0] and not expected[4 * 9 + 4] and not expected[1]
        _same_pixels(before, selected, selection=False)
        assert selected["previous_selection"] is None
        assert selected["revisions"]["selection"] == before["revisions"]["selection"] + 1
        assert repeated == selected, "Repeated To Selection must not publish again"
        assert _panel(row, "selected_again")["source_valid"] is True
        _membership(final["selection"], expected)
    elif case_id in {"T04", "T05", "T08"}:
        applied, undone = _state(row, "applied"), _state(row, "undone")
        target_z = int(case_id == "T04")
        expected = _pixels(case_id)
        for i, value in enumerate(expected[target_z]):
            if case_id != "T08" or ROI_ROWS[i // 9][i % 9] == "1":
                expected[target_z][i] = 255 if lower <= value <= upper else 0
        assert applied["pixels"] == expected
        assert applied["dtype"] == ("uint8" if case_id == "T05" else "uint16")
        assert _panel(row, "before_apply")["apply_output_type"] == applied["dtype"]
        assert _panel(row, "before_apply")["apply_extent"]
        assert applied["undo_steps"] == 1 and applied["redo_steps"] == 0
        assert applied["dirty"] is True and applied["data_identity"] != before["data_identity"]
        assert applied["revisions"]["pixels"] == before["revisions"]["pixels"] + 1
        assert applied["revisions"]["metadata"] == before["revisions"]["metadata"]
        assert applied["selection"] == before["selection"]
        assert "manual_threshold" not in applied["display"]["properties"]
        assert undone["pixels"] == before["pixels"] and undone["dtype"] == before["dtype"]
        assert undone["undo_steps"] == 0 and undone["redo_steps"] == 1 and undone["dirty"] is False
        assert _panel(row, "applied")["exit_reason"] == "apply"
        if case_id == "T04":
            other_id = row["states"]["other_active"]["other"]["document_id"]
            assert row["states"]["other_active"]["active_document_id"] == other_id
            assert row["states"]["applied"]["active_document_id"] == other_id
            assert row["states"]["final"]["other"] == row["states"]["before"]["other"]
            assert applied["pixels"][0] == before["pixels"][0]
        elif case_id == "T08":
            _membership(applied["selection"], [bit == "1" for line in ROI_ROWS for bit in line])
    elif case_id == "T06":
        for label in ("invalid_guard", "order_guard"):
            invalid = _panel(row, label)
            assert invalid["draft_valid"] is False and invalid["error"]
            assert invalid["control_states"]["apply"]["enabled"] is False
            _same_pixels(before, _state(row, label))
        for label in ("stale", "stale_guard"):
            stale = _panel(row, label)
            assert stale["source_valid"] is False and stale["source_error"]
            assert all(stale["control_states"][name]["enabled"] is False
                       for name in ("set", "reset", "to_selection", "apply"))
        assert _state(row, "stale_guard") == _state(row, "stale")
        expected = _pixels(case_id)
        expected[0][0] = 7
        assert final["pixels"] == expected and final["undo_steps"] == 1
    elif case_id == "T07":
        assert _state(row, "closed_source")["available"] is False
        assert _panel(row, "closed_guard")["source_valid"] is False
        assert _state(row, "closed_guard") == _state(row, "closed_source")
        _same_pixels(before, final)
    else:
        for label in ("busy", "busy_guard", "busy_canceled_panel"):
            assert row["states"][label]["busy"] is True
            _same_pixels(before, _state(row, label))
        assert _panel(row, "busy")["busy"] is True
        assert _panel(row, "busy")["control_states"]["apply"]["enabled"] is False
        assert _panel(row, "busy")["control_states"]["cancel"]["enabled"] is True
        assert _panel(row, "busy_canceled_panel")["open"] is False
        assert row["states"]["busy_released"]["busy"] is False
        _same_pixels(before, final)
    assert _panel(row, "final")["open"] is False
    assert row["states"]["final"]["busy"] is False


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_manual_threshold_physical_ui(case_id, context, request, tmp_path):
    row = _observe(context, case_id, tmp_path / "observations", request)
    _assert_case(row, case_id, context)
