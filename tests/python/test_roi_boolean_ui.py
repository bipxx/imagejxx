"""Independent installed readers for the 16 shared ImGui ROI-manager cases."""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import subprocess
import sysconfig
import time

import pytest


CONTRACT = "p05e.roi-boolean-ui.001"
CASE_IDS = tuple(f"R{index:02d}" for index in range(1, 17))
CONTEXTS = ("imgui_python", "imgui_cpp")
ORACLE_SHA = "407a52c8804adf0b63e9efb2951cd701f985714b696a109db0a1a174e95e4488"
MAX_JSON_BYTES = 64 * 1024 * 1024
ORACLES = {
    "R01": "overlap_rectangles__union",
    "R02": "overlap_rectangles__intersection",
    "R03": "overlap_rectangles__subtraction",
    "R04": "overlap_rectangles__xor",
    "R05": "ring_and_inner_island__union",
    "R06": "disconnected_rectangle_ellipse__union",
    "R07": "entirely_outside__intersection",
    "R08": "duplicate_polygon__xor",
}
STATE_FIELDS = (
    "document_id", "data_identity", "dirty", "undo_steps", "redo_steps",
    "revisions", "selection", "previous_selection", "rois", "metadata",
    "record_layout", "overlays", "display", "workflow", "results", "dataset",
)
PRESERVED_BOOLEAN_FIELDS = tuple(
    name for name in STATE_FIELDS
    if name not in {"revisions", "selection", "previous_selection"}
)


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
    assert path.is_file() and path.stat().st_size <= MAX_JSON_BYTES
    return _decode(path.read_bytes())


def _integer(value, *, positive=False):
    assert type(value) is int and value >= int(positive)
    return value


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


def _member(root, name):
    assert isinstance(name, str) and name
    relative = PurePosixPath(name)
    assert not relative.is_absolute() and ".." not in relative.parts and "\\" not in name
    resolved = root.joinpath(*relative.parts).resolve(strict=True)
    assert resolved.is_relative_to(root.resolve(strict=True)) and resolved.is_file()
    return resolved


def _source_bytes():
    return bytes(
        (37 * x + 61 * y + 13 * x * y + 43 * z) % 251
        for z in range(2) for y in range(9) for x in range(13)
    )


def _source_member(member, root):
    assert member["dtype"] == "uint8" and member["encoding"] == "UInt8"
    assert member["shape"] == [2, 9, 13]
    assert member["sample_count"] == member["byte_count"] == 234
    assert isinstance(member["member_id"], str) and member["member_id"]
    declared = Path(member["path"])
    if declared.is_absolute():
        path = declared.resolve(strict=True)
        assert path.is_file() and path.is_relative_to(root.resolve(strict=True))
    else:
        path = _member(root, member["path"])
    assert path.name == member["member_id"]
    assert path.stat().st_size == 234
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == member["sha256"]
    assert raw == _source_bytes(), "Selection-only action changed original source bytes"


@pytest.fixture(scope="module")
def boolean_oracles():
    path = Path(__file__).resolve().parents[1] / "fixtures/roi-boolean-imagej154r.json"
    raw = path.read_bytes()
    assert len(raw) == 88018 and hashlib.sha256(raw).hexdigest() == ORACLE_SHA
    fixture = _decode(raw)
    assert fixture["imagej_version"] == "1.54r"
    assert fixture["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    cases = fixture["cases"]
    assert len(cases) == 72 and len({case["id"] for case in cases}) == 72
    return {case["id"]: case for case in cases}


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Required installed desktop missing: IMAGEJXX_CPP_EXE")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed C++ desktop")
    executable = Path(configured).resolve(strict=True)
    assert executable.is_file()
    assert executable.name.lower() in {"imagejxx.exe", "imagejxx"}
    assert executable.parent.name.lower() == "bin", "Use the installed desktop bin directory"
    expected = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if expected:
        with executable.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == expected
    return executable


@pytest.fixture(scope="module")
def cpp_ui_rows(installed_cpp_executable, tmp_path_factory):
    root = tmp_path_factory.mktemp("roi-boolean-cpp") / "run"
    assert not root.exists()
    started = time.monotonic()
    completed = subprocess.run(
        [str(installed_cpp_executable), "--roi-boolean-ui-consumer-test",
         "--consumer-dir", str(root)],
        capture_output=True, timeout=180, check=False,
    )
    assert time.monotonic() - started <= 180
    assert len(completed.stdout) + len(completed.stderr) <= 16 * 1024 * 1024
    assert completed.returncode == 0, (
        completed.stdout.decode("utf-8", "replace")
        + completed.stderr.decode("utf-8", "replace")
    )
    report = _read_json(root / "roi-boolean-ui.json")
    assert report["schema_version"] == 1 and report["contract_id"] == CONTRACT
    assert report["context"] == "imgui_cpp" and report["acceptance_claim"] is False
    assert report["limits"] == {
        "max_seconds": 180, "max_case_frames": 160, "max_case_seconds": 15,
    }
    assert type(report["elapsed_seconds"]) in {int, float}
    assert math.isfinite(report["elapsed_seconds"]) and 0 <= report["elapsed_seconds"] <= 180
    assert report["case_count"] == 16
    assert [row["case_id"] for row in report["cases"]] == list(CASE_IDS)
    return root, {row["case_id"]: row for row in report["cases"]}


def _python_row(case_id, directory):
    if _required():
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    module = Path(desktop.__file__).resolve(strict=True)
    assert module.suffix in {".pyd", ".so"}
    if _required():
        roots = {
            Path(sysconfig.get_path(key)).resolve() / "imagejxx"
            for key in ("platlib", "purelib")
        }
        assert any(module.is_relative_to(root) for root in roots), (
            "Required ROI manager gate imported a checkout module"
        )
    assert not directory.exists()
    editor = desktop.Editor(preferences_path=str(directory.parent / f"{case_id}-preferences.json"))
    started = time.monotonic()
    row = _decode(editor._roi_boolean_ui_case(
        case_id=case_id, context="imgui_python", directory=str(directory)
    ))
    assert time.monotonic() - started <= 15
    return row


def _state(value):
    assert isinstance(value, dict) and set(STATE_FIELDS) <= value.keys()
    _integer(value["document_id"], positive=True)
    _integer(value["data_identity"])
    _integer(value["undo_steps"])
    _integer(value["redo_steps"])
    assert type(value["dirty"]) is bool
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for revision in value["revisions"].values():
        _integer(revision)
    assert value["dataset"]["dtype"] == "uint8"
    assert value["dataset"]["axis_sizes"] == {"T": 1, "C": 1, "Z": 2, "Y": 9, "X": 13, "S": 1}
    return value


def _same_state(before, after):
    _state(before)
    _state(after)
    assert after == before, "Rejected/manager-only action changed actual native state"


def _packed_result(selection, rows):
    assert selection is not None and selection["kind"] == "mask"
    assert selection["points"] == [] and selection["width"] == 1
    assert selection["attributes"] == {}
    assert selection["mask_width"] == 13 and selection["mask_height"] == 9
    assert selection["mask_rows"] == rows
    assert len(rows) == 9 and all(len(row) == 13 and set(row) <= {"0", "1"} for row in rows)
    packed = selection["packed_mask"]
    assert len(packed) == 15 and all(type(value) is int and 0 <= value <= 255 for value in packed)
    assert packed[-1] & 7 == 0, "Unused mask tail bits were not cleared"
    bits = [
        "1" if packed[index // 8] & (1 << (7 - index % 8)) else "0"
        for index in range(117)
    ]
    assert "".join(bits) == "".join(rows), "Observed raster and packed storage disagree"


def _selection_change(before, after, rows):
    _state(before)
    _state(after)
    for field in PRESERVED_BOOLEAN_FIELDS:
        assert after[field] == before[field], f"Boolean publication changed {field}"
    for field in ("pixels", "metadata", "display"):
        assert after["revisions"][field] == before["revisions"][field]
    assert after["revisions"]["selection"] == before["revisions"]["selection"] + 1
    assert after["previous_selection"] == before["selection"]
    _packed_result(after["selection"], rows)


def _frame(row, label):
    matches = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(matches) == 1, f"Missing or duplicate actual checkpoint {label}"
    return matches[0]


def _manager(frame, expected):
    manager = frame["rendered"]["roi_manager"]
    assert manager["visible"] is True
    assert manager["selected_indices"] == expected
    assert expected == sorted(set(expected))
    rows = manager["rows"]
    assert [row["index"] for row in rows] == list(range(len(rows)))
    assert [row["index"] for row in rows if row["selected"]] == expected
    assert [row["index"] for row in rows if row["drawn_selected"]] == expected
    for row in rows:
        assert type(row["selected"]) is type(row["drawn_selected"]) is bool
    return manager


def _actual_control(rendered, selector):
    if selector.startswith("document."):
        document_id = int(selector.removeprefix("document."))
        matches = [
            item for item in rendered["document_tabs"]
            if item["document_id"] == document_id
        ]
        assert len(matches) == 1
        return matches[0]
    return rendered["host_controls"][selector]


def _physical_observations(row, directory):
    frames = row["frames"]
    assert 1 <= len(frames) <= 160
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    assert len({frame["member_id"] for frame in frames}) == len(frames)
    sessions = []
    for frame in frames:
        assert _read_json(_member(directory, frame["member_id"])) == frame
        _state(frame["native_state"])
        rendered = frame["rendered"]
        session = rendered["session"]
        assert session["open"] is True
        _integer(session["session_id"], positive=True)
        _integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        sessions.append(session)
        actual = rendered["input_frame"]
        assert actual["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            kind = event["type"]
            assert kind in {"position", "button", "key", "keys", "wait"}
            if kind == "position":
                assert event["space"] == "screen"
                assert actual["mouse_position"] == [math.floor(value) for value in event["point"]]
            elif kind == "button":
                assert event["name"] in {"left", "right", "middle"}
                index = ("left", "right", "middle").index(event["name"])
                assert type(event["down"]) is bool and actual["mouse_down"][index] is event["down"]
            elif kind in {"key", "keys"}:
                atoms = [event] if kind == "key" else event["keys"]
                for atom in atoms:
                    assert type(atom["down"]) is bool
                    assert (atom["name"] in actual["keys_down"]) is atom["down"]
        assert all(type(value) is bool for value in actual["modifiers"].values())
    assert len({session["session_id"] for session in sessions}) == 1
    assert len({session["context_id"] for session in sessions}) == 1
    for previous, current in zip(sessions, sessions[1:]):
        assert current["imgui_frame"] == previous["imgui_frame"] + 1
        assert current["imgui_time"] > previous["imgui_time"]
    for observation in row["control_observations"]:
        ordinal = _integer(observation["before_frame_ordinal"], positive=True)
        assert ordinal + 3 <= len(frames)
        control = observation["control"]
        assert control == _actual_control(frames[ordinal - 1]["rendered"], observation["selector"])
        _integer(control["id"], positive=True)
        if observation["selector"] == "roi_manager.tab":
            assert set(control) == {"id", "rect", "point", "enabled", "selected"}
            assert type(control["enabled"]) is type(control["selected"]) is bool
        else:
            assert all(type(control[field]) is bool for field in ("enabled", "active", "focused"))
        rect, point = control["rect"], observation["position"]
        assert len(rect) == 4 and len(point) == 2
        assert all(type(value) in {int, float} and math.isfinite(value) for value in rect)
        assert point == [math.floor(value) for value in control["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        delivered = [frame["requested_events"] for frame in frames[ordinal:ordinal + 3]]
        assert delivered == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "closed"]
    opened, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert opened["context_id"] == sessions[0]["context_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert closed["imgui_frame"] == sessions[-1]["imgui_frame"]
    for label in ("warmup.1", "warmup.2", "manager_open.1", "manager_open.2"):
        assert _frame(row, label)["requested_events"] == []


def _rejection(row, label):
    matches = [item for item in row["rejections"] if item["label"] == label]
    assert len(matches) == 1
    rejected = matches[0]
    assert isinstance(rejected["parameters"], dict)
    assert rejected["exception"]["type"] in {
        "revision_conflict", "out_of_range", "invalid_argument", "exception",
    }
    assert isinstance(rejected["exception"]["message"], str) and rejected["exception"]["message"]
    _same_state(rejected["before"], rejected["after"])
    return rejected


def _disabled(frame):
    manager = frame["rendered"]["roi_manager"]
    assert manager["can_combine"] is False
    for name in ("union", "intersection", "subtraction", "xor"):
        assert manager["controls"][name]["enabled"] is False


def _scenario(row, case_id, oracles):
    states = row["states"]
    if case_id in ORACLES:
        oracle = oracles[ORACLES[case_id]]
        _manager(_frame(row, "operand0_selected"), [0])
        pair = _manager(_frame(row, "operands_selected"), [0, 1])
        assert pair["area_operands_valid"] is pair["can_combine"] is True
        assert pair["subtraction_base_index"] == 0
        before = states["before_apply"]
        after = states["after_apply"]
        assert len(before["rois"]) == 2
        for index, operand in enumerate(oracle["operands"]):
            assert before["rois"][index]["selection"]["mask_rows"] == operand["mask_rows"]
        assert [(roi["t"], roi["c"], roi["z"]) for roi in before["rois"]] == [
            (None, None, 1), (None, None, 0),
        ]
        _selection_change(before, after, oracle["result"]["mask_rows"])
        _same_state(after, states["after_noop"])
        _packed_result(states["after_noop"]["selection"], oracle["result"]["mask_rows"])
        _manager(_frame(row, "applied"), [0, 1])
        _manager(_frame(row, "noop"), [0, 1])
        selectors = [item["selector"] for item in row["control_observations"]]
        wanted = "roi_manager." + oracle["operator"]
        assert selectors.count(wanted) == 2
        assert "roi_manager.row.0" in selectors and "roi_manager.row.1" in selectors
        assert row["rejections"] == []
    elif case_id == "R09":
        for label, selected in (
            ("modifier_one", [0]), ("modifier_pair", [0, 1]),
            ("modifier_remove", [1]), ("modifier_anchor", [0, 1]),
            ("range_forward", [0, 1, 2, 3]), ("range_shrink", [0, 1]),
            ("range_ctrl_extend", [0, 1, 2, 3]),
        ):
            _manager(_frame(row, label), selected)
        assert _frame(row, "range_forward.modifiers.down")["rendered"]["input_frame"]["modifiers"]["shift"] is True
        modified = _frame(row, "range_ctrl_extend.modifiers.down")["rendered"]["input_frame"]["modifiers"]
        assert modified["ctrl"] is modified["shift"] is True
        _same_state(states["before"], states["after"])
    elif case_id == "R10":
        _manager(_frame(row, "select_all_button"), [0, 1, 2, 3])
        _manager(_frame(row, "select_all_toggle"), [0, 2, 3])
        keyboard = _frame(row, "select_all_keyboard")
        manager = _manager(keyboard, [0, 1, 2, 3])
        assert manager["focused"] is True
        assert any(
            {"Ctrl", "A"} <= set(frame["rendered"]["input_frame"]["keys_down"])
            for frame in row["frames"]
        )
        assert any(item["selector"] == "roi_manager.select_all" for item in row["control_observations"])
        _same_state(states["before"], states["after"])
    elif case_id == "R11":
        original = states["before"]["document_id"]
        other = states["other_before"]["document_id"]
        assert original != other
        assert _manager(_frame(row, "other_document"), [])["document_id"] == other
        assert _manager(_frame(row, "original_document"), [])["document_id"] == original
        selectors = {item["selector"] for item in row["control_observations"]}
        assert {f"document.{other}", f"document.{original}"} <= selectors
        _same_state(states["before"], states["after"])
        _same_state(states["other_before"], states["other_after"])
    elif case_id == "R12":
        undo_controls = [
            item["selector"] for item in row["control_observations"]
            if item["selector"].startswith("menu.Edit")
        ]
        assert undo_controls == ["menu.Edit", "menu.Edit.Undo"]
        popup = _frame(row, "undo_menu_open")
        assert popup["requested_events"] == []
        assert popup["rendered"]["input_frame"]["popup_stack"]
        for label in ("after_replace", "after_undo", "after_import"):
            _manager(_frame(row, label), [])
        before, replaced, undone = states["replace_before"], states["replace_after"], states["undo_after"]
        assert len(before["rois"]) == len(replaced["rois"]) == len(undone["rois"])
        assert replaced["rois"] != before["rois"]
        assert replaced["revisions"]["metadata"] == before["revisions"]["metadata"] + 1
        assert replaced["undo_steps"] == before["undo_steps"] + 1
        assert replaced["dirty"] is True
        assert undone["rois"] == before["rois"] and undone["dirty"] == before["dirty"]
        assert undone["undo_steps"] == before["undo_steps"] and undone["redo_steps"] >= 1
        _same_state(states["import_before"], states["import_after"])
        for value in (replaced, undone):
            for field in ("selection", "previous_selection", "dataset", "display"):
                assert value[field] == before[field]
    elif case_id == "R13":
        labels = ("insufficient", "duplicate", "out_of_range", "operation", "extra_parameter", "nonarea")
        assert [item["label"] for item in row["rejections"]] == list(labels)
        for label in labels:
            _rejection(row, label)
        manager = _manager(_frame(row, "nonarea_disabled"), [0, 1])
        assert manager["area_operands_valid"] is False
        _disabled(_frame(row, "nonarea_disabled"))
    elif case_id == "R14":
        rejected = _rejection(row, "stale_revision")
        assert rejected["exception"]["type"] == "revision_conflict"
        assert rejected["captured_document_id"] == rejected["before"]["document_id"]
        assert rejected["before"]["revisions"]["metadata"] > rejected["captured_revisions"]["metadata"]
        _manager(_frame(row, "stale_invalidated"), [])
    elif case_id == "R15":
        _disabled(_frame(row, "busy_disabled"))
        _rejection(row, "busy")
        _same_state(states["before"], states["after"])
        held = row["job_before_cleanup"]
        _integer(held["id"], positive=True)
        assert held["document_id"] == states["before"]["document_id"]
        assert held["busy"] is True
        assert held["state"].lower() == "running" and held["cancel_requested"] is False
        terminal = row["job_after_cleanup"]
        assert terminal["id"] == held["id"] and terminal["state"].lower() == "canceled"
        assert terminal["document_id"] == held["document_id"] and terminal["busy"] is False
        assert terminal["cancel_requested"] is True
        assert held["usage"]["running"] > 0
        assert all(terminal["usage"][field] == 0 for field in (
            "running", "queued", "running_bytes", "retained_bytes", "reserved_bytes",
        ))
    else:
        assert case_id == "R16"
        rejected = _rejection(row, "closed")
        original = rejected["captured_document_id"]
        assert original == states["before"]["document_id"]
        invalidated = _frame(row, "closed_invalidated")
        manager = _manager(invalidated, [])
        assert manager["document_id"] != original
        assert original not in {item["document_id"] for item in invalidated["rendered"]["document_tabs"]}
        _same_state(states["before"], states["after"])


@pytest.mark.parametrize(
    "case_id,context",
    [(case_id, context) for case_id in CASE_IDS for context in CONTEXTS],
    ids=[f"{case_id}-{context}" for case_id in CASE_IDS for context in CONTEXTS],
)
def test_installed_roi_boolean_manager(case_id, context, boolean_oracles, request, tmp_path):
    if context == "imgui_cpp":
        parent, rows = request.getfixturevalue("cpp_ui_rows")
        directory = parent / case_id
        row = rows[case_id]
    else:
        directory = tmp_path / "run"
        row = _python_row(case_id, directory)
    assert row["schema_version"] == 1 and row["contract_id"] == CONTRACT
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False
    assert row["limits"] == {"max_frames": 160, "max_seconds": 15}
    assert type(row["elapsed_seconds"]) in {int, float}
    assert math.isfinite(row["elapsed_seconds"]) and 0 <= row["elapsed_seconds"] <= 15
    assert _read_json(directory / "case.json") == row
    for field in ("source_before_raw", "source_after_raw", "original_after_raw"):
        _source_member(row[field], directory)
    _state(row["states"]["before"])
    assert row["states"]["before"]["dirty"] is False
    assert row["states"]["before"]["undo_steps"] == row["states"]["before"]["redo_steps"] == 0
    assert row["states"]["before"]["selection"]["kind"] == "point"
    assert row["states"]["before"]["selection"]["points"] == [[2, 8]]
    assert row["states"]["before"]["selection"]["width"] == 2
    assert row["states"]["before"]["selection"]["attributes"] == {"label": "before Boolean"}
    assert row["states"]["before"]["display"]["crosshair"] == [6, 4, 0]
    assert row["final_context"] == {"current_context_id": 0}
    assert row["final_busy"] is False
    _physical_observations(row, directory)
    _scenario(row, case_id, boolean_oracles)
