"""Independent assertions for the installed persistent Dear ImGui renderer.

The same native physical-input driver runs in the standalone executable and the
optional Python launch. These tests inspect actual context, item, input, source,
history and resource observations, rather than trusting producer PASS flags.
This finite prerequisite does not certify the full Gaussian UI matrix.
"""

import importlib
import json
import math
import os
from pathlib import Path

import pytest


CASES = [f"S{number:02d}" for number in range(1, 9)]
CONTEXTS = ["imgui_cpp", "imgui_python"]
USAGE_FIELDS = {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
SOURCE_FIELDS = ["document_id", "data_identity", "dirty", "undo_steps", "redo_steps",
                 "revisions", "document_info", "workflow", "results"]


def fixture_bytes(planes=1):
    return bytes((37*x + 61*y + 13*x*y + 43*z) % 251
                 for z in range(planes) for y in range(17) for x in range(23))


def integer(value, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def member_path(root, name):
    root = root.resolve(strict=True)
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file(), "Session artifact escaped its case directory"
    return path


def raw(member, root, planes):
    path = Path(member["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path.is_relative_to(root.resolve(strict=True)) and path.is_file()
    assert path == member_path(root, member["member_id"])
    assert member["encoding"] == "UInt8" and member["dtype"] == "uint8"
    assert member["shape"] == [planes, 17, 23]
    assert member["sample_count"] == member["byte_count"] == planes * 17 * 23
    data = path.read_bytes()
    assert len(data) == member["byte_count"]
    return data


def usage(value, *, empty=False):
    assert set(value) == USAGE_FIELDS
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]
    if empty:
        assert not any(value.values())


def lifecycle(value, *, opened):
    assert type(value["open"]) is bool and value["open"] is opened
    integer(value["session_id"], positive=True)
    integer(value["context_id"], positive=opened)
    integer(value["current_context_id"], positive=opened)
    assert value["context_id"] == value["current_context_id"]
    if not opened:
        assert value["context_id"] == 0
    assert type(value["imgui_frame"]) is int
    assert type(value["imgui_time"]) in {int, float} and math.isfinite(value["imgui_time"])


def label(row, name):
    matched = [frame for frame in row["frames"] if frame["label"] == name]
    assert matched, f"No actual session frame for {name}"
    return matched[-1]


def rendered(row, name):
    return label(row, name)["rendered"]


def source(value, root, planes):
    integer(value["document_id"], positive=True)
    integer(value["data_identity"])
    assert type(value["dirty"]) is bool
    for field in ["undo_steps", "redo_steps"]:
        integer(value[field])
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for item in value["revisions"].values():
        integer(item)
    info = value["document_info"]
    assert info["id"] == value["document_id"] and info["dtype"] == "uint8"
    assert info["revisions"] == value["revisions"]
    assert info["undo_steps"] == value["undo_steps"] and info["redo_steps"] == value["redo_steps"]
    assert info["dirty"] == value["dirty"]
    assert info["axis_sizes"] == {"T": 1, "C": 1, "Z": planes, "Y": 17, "X": 23, "S": 1}
    assert value["workflow"] == [] and value["results"] == []
    return raw(value["source_raw"], root, planes)


def physical_frames(row, root):
    frames = row["frames"]
    assert frames and [frame["ordinal"] for frame in frames] == list(range(1, len(frames)+1))
    grouped = {}
    for frame in frames:
        assert json.loads(member_path(root, frame["member_id"]).read_text(encoding="utf-8")) == frame
        value = frame["rendered"]
        identity = value["session"]
        lifecycle(identity, opened=True)
        grouped.setdefault(identity["session_id"], []).append(identity)
        usage(frame["usage"])
        observed = value["input_frame"]
        assert observed["raw_inputs"] == frame["requested_events"]
        assert set(observed["focus"]) == {"nav_id", "active_id", "window"}
        integer(observed["focus"]["nav_id"])
        integer(observed["focus"]["active_id"])
        assert type(observed["focus"]["window"]) is str
        assert len(observed["mouse_down"]) == 3 and all(type(item) is bool for item in observed["mouse_down"])
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "key", "keys", "text", "wheel"}
            if event["type"] == "position":
                assert event["space"] == "screen"
                assert observed["mouse_position"] == [math.floor(item) for item in event["point"]]
            elif event["type"] == "button":
                assert event["name"] == "left" and observed["mouse_down"][0] is event["down"]
            elif event["type"] in {"key", "keys"}:
                atoms = event["keys"] if event["type"] == "keys" else [event]
                for atom in atoms:
                    assert (atom["name"] in observed["keys_down"]) is atom["down"]
        for popup in observed["popup_stack"]:
            integer(popup["id"], positive=True)
            assert type(popup["opened_frame"]) is int and popup["opened_frame"] <= identity["imgui_frame"]
            assert type(popup["window"]) is str
    for identities in grouped.values():
        assert len({value["context_id"] for value in identities}) == 1
        for earlier, later in zip(identities, identities[1:]):
            assert later["imgui_frame"] == earlier["imgui_frame"] + 1
            assert later["imgui_time"] > earlier["imgui_time"]
    for observation in row["control_observations"]:
        actual = observation["control"]
        integer(actual["id"], positive=True)
        assert actual["enabled"] is True
        x, y = observation["position"]
        x0, y0, x1, y1 = actual["rect"]
        assert x0 < x < x1 and y0 < y < y1 and x == math.floor(x) and y == math.floor(y)
        first = frames[observation["before_frame_ordinal"]]
        assert first["label"] == observation["selector"] + ".position"
        assert first["requested_events"] == [{"type": "position", "space": "screen", "point": [x, y]}]
    return grouped


def menu_and_text(row, *, text):
    selectors = [value["selector"] for value in row["control_observations"]]
    assert all(name in selectors for name in ["menu.Process", "menu.Process.Filters",
                                              "menu.command.gaussian_blur_imagej"])
    for name in ["process_popup_retained", "filters_popup_retained"]:
        actual = label(row, name)
        assert actual["requested_events"] == []
        assert actual["rendered"]["input_frame"]["popup_stack"]
    if text:
        initial = rendered(row, "gaussian_initial_focus.2")
        control = initial["gaussian_panel"]["control_states"]["sigma"]
        focus = initial["input_frame"]["focus"]
        state = initial["input_frame"]["input_text"]
        assert control["active"] is True and control["id"] == focus["active_id"] == state["id"]
        assert state["length"] > 0 and abs(state["selection_end"] - state["selection_start"]) == state["length"]
        after = rendered(row, "sigma.text_retained")
        assert after["gaussian_panel"]["sigma_text"] == "0.5"
        assert after["gaussian_panel"]["sigma"] == 0.5 and after["gaussian_panel"]["draft_valid"] is True
        assert after["input_frame"]["focus"]["active_id"] == control["id"]
        assert label(row, "sigma.text")["requested_events"] == [{"type": "text", "value": "0.5"}]
        for name, down in [("sigma.ctrl_a.down", True), ("sigma.ctrl_a.up", False)]:
            assert label(row, name)["requested_events"] == [{"type": "keys", "keys": [
                {"name": "Ctrl", "down": down}, {"name": "A", "down": down}]}]


def focus_control(row, frame_name, name):
    value = rendered(row, frame_name)
    control = value["gaussian_panel"]["control_states"][name]
    assert control["focused"] is True and control["id"] == value["input_frame"]["focus"]["nav_id"]


def readback(value, expected, width, height):
    assert value["available"] is True
    assert value["source"]["shape"] == [height, width, 1] and value["source"]["dtype"] == "uint8"
    assert value["source"]["pixels"] == list(expected)
    assert value["rendered_rgb"]["shape"] == [height, width, 3] and value["rendered_rgb"]["dtype"] == "uint8"
    assert value["rendered_rgb"]["pixels"] == [item for item in expected for _ in range(3)]


def assert_row(row, case_id, context, root):
    assert row["schema_version"] == 1 and row["contract_id"] == "p02a.headless-session.001"
    assert row["case_id"] == case_id and row["context"] == context
    assert row["status"] == "passed" and row["acceptance_claim"] is False
    assert row["fixture_id"] == "session-owned-u8-23x17" and row["window_error"] == ""
    assert json.loads(member_path(root, "case.json").read_text(encoding="utf-8")) == row
    assert len(row["member_ids"]) == len(set(row["member_ids"]))
    for member in row["member_ids"]:
        member_path(root, member)
    unverified = json.loads(member_path(root, "case-unverified.json").read_text(encoding="utf-8"))
    assert unverified["status"] == "unverified" and unverified["frames"] == row["frames"]
    groups = physical_frames(row, root)
    for collection in ["lifecycle", "recovery"]:
        for observed in row[collection]:
            lifecycle(observed["observed"], opened=observed["observed"]["open"])
    assert row["final_context"]["current_context_id"] == 0
    usage(row["final_usage"], empty=True)
    for checkpoint in row["resource_checkpoints"]:
        usage(checkpoint["usage"])
    planes = 3 if case_id == "S04" else 1
    before, after = row["before"], row["after"]
    original = fixture_bytes(planes)
    assert source(before, root, planes) == original
    assert raw(row["original_after_raw"], root, planes) == original
    changed = source(after, root, planes)
    if case_id == "S04":
        assert changed[:391] == original[:391] and changed[782:] == original[782:]
        assert changed[391:782] != original[391:782]
        assert after["document_id"] == before["document_id"] and after["data_identity"] != before["data_identity"]
        assert after["dirty"] is True and after["undo_steps"] == 1 and after["redo_steps"] == 0
        assert after["revisions"]["pixels"] == before["revisions"]["pixels"] + 1
        for field in ["metadata", "selection", "display"]:
            assert after["revisions"][field] == before["revisions"][field]
    else:
        assert changed == original
        for field in SOURCE_FIELDS:
            assert after[field] == before[field], f"Session-only input changed native {field}"
    if case_id not in {"S06"}:
        menu_and_text(row, text=case_id in {"S01", "S05", "S07", "S08"})
    if case_id in {"S01", "S02", "S03"}:
        assert len(groups) == 1 and row["recovery"] == []
    if case_id == "S02":
        focus_control(row, "cancel_focused", "cancel")
        back = rendered(row, "shift_tab.up")
        assert back["input_frame"]["focus"]["nav_id"] != back["gaussian_panel"]["control_states"]["cancel"]["id"]
        assert rendered(row, "key.Enter.up")["gaussian_panel"]["exit_reason"] == "cancel"
    elif case_id in {"S03", "S08"}:
        ready = label(row, "preview_ready")
        assert ready["usage"]["running_bytes"] == 0 and ready["usage"]["retained_bytes"] > 0
        assert ready["usage"]["reserved_bytes"] == ready["usage"]["retained_bytes"]
        assert ready["native_state"]["undo_steps"] == 0
        assert ready["rendered"]["ordinary_pixels"]["pixels_source"] == "preview"
    elif case_id == "S04":
        for name in ["stack_prompt_retained.1", "stack_prompt_retained.2"]:
            retained = label(row, name)
            assert retained["requested_events"] == [] and retained["rendered"]["gaussian_panel"]["stack_prompt"] is True
            assert retained["native_state"]["undo_steps"] == 0
            usage(retained["usage"], empty=True)
        focus_control(row, "stack_default_yes_focus", "stack_yes")
        focus_control(row, "stack_no_focused", "stack_no")
        assert rendered(row, "stack_apply_completed")["gaussian_jobs"]["apply"]["exit"] == "published"
    elif case_id == "S05":
        for name in ["legacy_default_before", "legacy_default_after"]:
            assert len(row[name]["input_frames"]) == 3 and row[name]["input_trace"] == []
        assert [value["label"] for value in row["exceptions"]] == ["unknown_field", "unknown_key", "nonfinite_pointer"]
        for value in row["exceptions"]:
            assert value["exception"]["type"] == "invalid_argument"
            assert value["before"] == value["after"] and value["after"]["open"] is True
        supplement = row["legacy_readback_compatibility"]
        assert raw(supplement["source_before_raw"], root, 1) == fixture_bytes()
        assert raw(supplement["source_after_raw"], root, 1) == fixture_bytes()
        assert supplement["data_identity_before"] == supplement["data_identity_after"]
        assert supplement["pixels_revision_before"] == supplement["pixels_revision_after"]
        assert supplement["undo_steps"] == supplement["redo_steps"] == 0 and supplement["dirty"] is False
        assert supplement["api_inputs"][0]["input_events"] == [{"type": "action", "name": "show_operation",
            "parameters": {"operation": "gaussian_blur_imagej"}}]
        assert supplement["opened_during_call"]["gaussian_panel"]["open"] is True
        for name in ["opened_during_call", "subsequent_ordinary"]:
            ordinary = supplement[name]["ordinary_pixels"]
            assert ordinary["pixels_source"] == "live"
            readback(ordinary["readback"], original, 23, 17)
        xyz = supplement["subsequent_xyz"]["orthogonal_planes"]
        assert len(xyz) == 3
        expected = [list(original), [original[5*23+x] for x in range(23)], [original[y*23+7] for y in range(17)]]
        for index, plane in enumerate(xyz):
            assert plane["visible"] is True and plane["pixels_source"] == "live_document"
            readback(plane["readback"], expected[index], 17 if index == 2 else 23, 17 if index == 0 else 1)
    elif case_id == "S06":
        thread = row["thread_rejections"]
        assert thread["actor_language"] == "cpp" and thread["thread_id"] != row["creator_thread"]
        assert thread["before"] == thread["after"]
        assert [item["operation"] for item in thread["results"]] == ["frame", "close"]
        assert all(item["exception"]["type"] == "runtime_error" for item in thread["results"])
        assert [value["label"] for value in row["exceptions"]] == ["same_window_begin", "other_window_begin", "legacy_render"]
        for value in row["exceptions"]:
            assert value["exception"]["type"] == "runtime_error" and value["before"] == value["after"]
        other = row["other_window_recovery"]
        lifecycle(other["opened"], opened=True)
        lifecycle(other["frame"]["session"], opened=True)
        lifecycle(other["closed"], opened=False)
        assert other["opened"]["session_id"] not in groups
    if case_id in {"S07", "S08"}:
        assert row["probes"] and all(value["thread_id"] == row["creator_thread"] for value in row["probes"])
        assert all(value["actual_context"]["current_context_id"] > 0 for value in row["probes"])
        assert row["recovery"] and all(value["recovery"] for value in row["frames"] if value["label"] == "sigma.text_retained")
        recovered = [value for value in row["recovery"] if value["label"] == "opened"]
        assert len(recovered) == 1
        if case_id == "S07":
            failure = row["initialization_failure"]
            assert failure["before"]["current_context_id"] == failure["after"]["current_context_id"] == 0
            assert failure["exception"]["message"] == "S07 renderer initialization probe"
            assert row["probes"][0]["phase"] == "after_context_creation"
        else:
            failure = row["frame_failure"]
            lifecycle(failure["before"], opened=True)
            lifecycle(failure["after"], opened=False)
            assert failure["before"]["session_id"] == failure["after"]["session_id"]
            assert failure["exception"]["message"] == "S08 renderer frame probe"
            assert failure["usage_before"]["retained_bytes"] == failure["usage_after_renderer_close"]["retained_bytes"] > 0
            assert failure["actual_context_after"]["current_context_id"] == 0
            assert recovered[0]["observed"]["session_id"] != failure["before"]["session_id"]
            checkpoints = {item["name"]: item["usage"] for item in row["resource_checkpoints"]}
            assert checkpoints["renderer_closed_before_driver_job_cleanup"]["retained_bytes"] > 0
            usage(checkpoints["driver_jobs_canceled_after_renderer_failure"], empty=True)


@pytest.fixture(scope="module")
def cpp_report():
    configured = os.environ.get("IMAGEJXX_SESSION_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed session report missing: IMAGEJXX_SESSION_CPP_REPORT")
        pytest.skip("Installed C++ session report is optional outside the mandatory desktop gate")
    path = Path(configured).resolve(strict=True)
    report = json.loads(path.read_text(encoding="utf-8"))
    assert report["schema_version"] == 1 and report["contract_id"] == "p02a.headless-session.001"
    assert report["context"] == "imgui_cpp" and report["status"] == "passed"
    rows = report["cases"]
    assert len(rows) == 8 and sorted(row["case_id"] for row in rows) == CASES
    assert all(row["context"] == "imgui_cpp" for row in rows)
    return {row["case_id"]: row for row in rows}, path.parent


def python_row(case_id, directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    editor = desktop.Editor(preferences_path=str(directory.parent / f"{case_id}-preferences.json"))
    return json.loads(editor._headless_session_case(case_id=case_id, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_persistent_headless_input_session(case_id, context, request, tmp_path):
    if context == "imgui_cpp":
        rows, parent = request.getfixturevalue("cpp_report")
        row, root = rows[case_id], parent / case_id
    else:
        root = tmp_path / case_id
        assert not root.exists()
        row = python_row(case_id, root)
    assert_row(row, case_id, context, root)
