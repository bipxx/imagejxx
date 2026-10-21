"""Independent H1 race assertions for installed C++ and Python desktop hosts.

These are cancellation/admission/publication integration observations. Source
bytes have an independent integer Float32LE oracle; changed Gaussian output is
compared with the producer's separately captured public primitive preparation,
without claiming an additional upstream numerical or actual-input UI proof.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct
import threading

import pytest


CASES = [f"H1-{number:02d}" for number in range(1, 13)]
CONTEXTS = ["imgui_cpp", "imgui_python"]
FIXTURE_ID = "H1-owned-f32-173x101"
COPY_STAGE = "ImageJ Gaussian source copy"
SAMPLE_COUNT = 173 * 101
USAGE_FIELDS = ["running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"]
TERMINAL_STATES = {"Ready", "Completed", "Failed", "Canceled"}


def original_bytes():
    return b"".join(
        struct.pack("<f", (37 * x + 61 * y + 13 * x * y) / 8 - 0.125)
        for y in range(101) for x in range(173)
    )


def integer(value, *, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def raw(member, root):
    root = root.resolve()
    path = Path(member["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path.is_relative_to(root), "H1 raw artifact escapes its producer directory"
    assert path.is_file()
    assert member["encoding"] == "IEEE754 Float32LE"
    assert member["dtype"] == "float32" and member["shape"] == [101, 173]
    assert member["sample_count"] == SAMPLE_COUNT
    assert member["byte_count"] == SAMPLE_COUNT * 4
    data = path.read_bytes()
    assert len(data) == SAMPLE_COUNT * 4
    assert hashlib.sha256(data).hexdigest() == member["sha256"]
    return data


def usage(value):
    for field in USAGE_FIELDS:
        integer(value[field])
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]


def job(value, source_id):
    integer(value["id"], positive=True)
    assert value["document_id"] == source_id
    integer(value["resource_bytes"], positive=True)
    assert value["state"] in {"Queued", "Running", "Ready", "Publishing", "Completed", "Failed", "Canceled"}
    assert type(value["cancel_requested"]) is bool
    assert type(value["error"]) is str and type(value["stage"]) is str
    for field in ["completed", "total", "checkpoints", "stage_completed", "stage_total"]:
        integer(value[field])


def unique_event(events, phase):
    matched = [event for event in events if event["phase"] == phase]
    assert len(matched) == 1, f"H1 requires one actual {phase} event"
    return matched[0]


def event_job(event, identifier):
    matches = [value for value in event["jobs"] if value["id"] == identifier]
    assert len(matches) == 1, "Actual scheduler observation lacks its declared job ID"
    return matches[0]


def actor_events(row):
    events = row["ordered_probe_events"]
    assert [event["sequence"] for event in events] == list(range(len(events)))
    assert all(type(event["phase"]) is str and event["phase"] for event in events)
    assert all(type(event["thread_id"]) is str and event["thread_id"] for event in events)
    assert not any(event["phase"] == "timeout" for event in events)
    point = unique_event(events, "cancel_point")
    entered = unique_event(events, "off_thread_window_cancel_entered")
    returned = unique_event(events, "off_thread_window_cancel_returned")
    assert point["sequence"] < entered["sequence"] < returned["sequence"]
    assert entered["role"] == returned["role"] == "canceller"
    assert entered["thread_id"] == returned["thread_id"]
    assert point["thread_id"] != entered["thread_id"]
    for event in [entered, returned]:
        usage(event["usage"])
    return events, entered, returned


def find_event(events, phase, *, name=None, request_index=None, lane=None):
    matched = [event for event in events if event["phase"] == phase
               and (name is None or event.get("name") == name)
               and (request_index is None or event.get("request_index") == request_index)
               and (lane is None or event.get("lane") == lane)]
    assert len(matched) == 1, f"Missing or repeated H1 event: {phase}/{name}/{request_index}/{lane}"
    return matched[0]


def checkpoint(row, collection, name, request_index=0):
    matched = [value for value in row[collection]
               if value["name"] == name and value["request_index"] == request_index]
    assert len(matched) == 1, f"Missing or repeated H1 checkpoint: {collection}/{name}/{request_index}"
    return matched[0]


def source_state(snapshot, root):
    integer(snapshot["document_id"], positive=True)
    integer(snapshot["data_identity"])
    assert type(snapshot["dirty"]) is bool
    assert set(snapshot["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for value in snapshot["revisions"].values():
        integer(value)
    integer(snapshot["undo_steps"])
    integer(snapshot["redo_steps"])
    info = snapshot["document_info"]
    assert info["id"] == snapshot["document_id"]
    assert info["dirty"] == snapshot["dirty"]
    assert info["revisions"] == snapshot["revisions"]
    assert info["undo_steps"] == snapshot["undo_steps"]
    assert info["redo_steps"] == snapshot["redo_steps"]
    assert info["dtype"] == "float32"
    assert info["axis_sizes"] == {"T": 1, "C": 1, "Z": 1, "Y": 101, "X": 173, "S": 1}
    assert info["slice_index"] == 0 and info["selection"] is None
    assert type(snapshot["workflow"]) is list and type(snapshot["results"]) is list
    return raw(snapshot["source_raw"], root)


def same_source_state(before, after, root):
    assert source_state(after, root) == source_state(before, root)
    for field in ["document_id", "data_identity", "dirty", "revisions", "undo_steps",
                  "redo_steps", "document_info", "workflow", "results"]:
        assert after[field] == before[field], f"H1 changed source state: {field}"


def host_state(host):
    for field in ["admission_epoch", "current_epoch", "drained_epoch"]:
        integer(host[field])
    for field in ["mirrored_ids", "actual_lane_ids"]:
        assert len(host[field]) == 2
        for value in host[field]:
            integer(value)
    controller = host["gaussian_jobs"]
    integer(controller["apply_callback_count"])
    for field in ["preview", "apply"]:
        assert type(controller[field]["owns_job"]) is bool


def assert_h1_row(row, case_id, context, root):
    number = int(case_id[-2:])
    preview = number in {1, 3, 5, 7}
    slot = 0 if preview else 1
    winner = number in {11, 12}
    assert row["schema_version"] == 1 and row["contract_id"] == "p02a.gaussian.h1-races.001"
    assert row["case_id"] == case_id and row["context"] == context
    assert row["fixture_id"] == FIXTURE_ID
    assert row["input_events"] == [] and row["timeouts_or_failure"] == []
    before, after = row["before"], row["after"]
    expected_input = original_bytes()
    assert source_state(before, root) == expected_input
    assert before["source_raw"]["sha256"] == hashlib.sha256(expected_input).hexdigest()
    assert before["undo_steps"] == before["redo_steps"] == 0
    assert before["workflow"] == before["results"] == []
    source_id = before["document_id"]
    original_after = raw(row["original_after_raw"], root)
    assert original_after == expected_input, "H1 work modified retained original source buffers"

    record, target = row["command_record"], row["target"]
    assert record["schema_version"] == 1 and record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": 2.0, "scaled_units": False}
    assert record["effective_sigma_xy"] == [2.0, 2.0] and record["target"] == target
    assert target["schema_version"] == 1 and target["document_id"] == source_id
    assert target["source_shape"] == [1, 1, 1, 101, 173, 1]
    assert target["source_revisions"] == [before["revisions"][name]
                                          for name in ["pixels", "metadata", "selection"]]
    assert target["scope"] == "ranges" and target["ranges"] == [[0, 1, 1]] * 3
    assert target["positions"] == [] and target["roi_policy"] == "whole_plane"
    assert target["execution_mode"] == "planes" and target["destination"] == "in_place"

    session = row["session"]
    assert session["case_id"] == case_id and session["context"] == context
    assert session["cancel_calls"] == 1 and session["timeouts"] == []
    assert session["cancel_point_reached"] is True and session["cancel_entered"] is True
    assert session["cancel_returned"] is True and session["aborted"] is False
    events, entered, returned = actor_events(row)
    assert events == session["ordered_probe_events"]
    assert entered["thread_id"] == session["canceller_thread"]
    assert session["creator_thread"] != session["canceller_thread"]
    assert row["host_and_canceller_thread_roles"] == {
        "host": session["creator_thread"], "canceller": session["canceller_thread"], "context": context}
    for event in events:
        if event.get("role") == "host":
            assert event["thread_id"] == session["creator_thread"]
        if event["phase"] in {"worker_entered", "stage_progress"}:
            assert event["role"] == "native_worker" and event["lane"] in {"blocker", "window"}
            assert event["operation"] == "gaussian_blur_imagej" and event["source_id"] == source_id
            assert event["thread_id"] not in {session["creator_thread"], session["canceller_thread"]}
            integer(event["completed"])
            integer(event["total"])
            if event["phase"] == "worker_entered":
                assert event["stage"] == "" and event["completed"] == event["total"] == 0
            if event["stage"] == COPY_STAGE:
                assert event["total"] == SAMPLE_COUNT and event["completed"] <= SAMPLE_COUNT

    actual = row["actual_native_jobs"]
    ids = [value["id"] for value in actual]
    assert len(ids) == len(set(ids))
    for value in actual:
        job(value, source_id)
    first_id = integer(row["first_native_job_id"], positive=True)
    assert first_id in ids and row["first_terminal_job"]["id"] == first_id
    job(row["first_terminal_job"], source_id)
    job(row["terminal_job"], source_id)
    assert row["terminal_job"]["state"] == ("Completed" if winner else "Canceled")
    assert row["publication_callback_count"] == (1 if number >= 10 else 0)
    assert row["second_poll_callback_count"] == row["publication_callback_count"]
    assert type(row["native_publication_succeeded"]) is bool
    assert row["native_publication_succeeded"] is winner
    assert all(value["state"] in TERMINAL_STATES for value in actual)

    paired = [(value["name"], value["request_index"]) for value in row["resource_checkpoints"]]
    assert paired == [(value["name"], value["request_index"]) for value in row["host_checkpoints"]]
    assert len(paired) == len(set(paired))
    for value in row["resource_checkpoints"]:
        usage(value["usage"])
    for value in row["host_checkpoints"]:
        host_state(value["host"])
    initial = checkpoint(row, "host_checkpoints", "initial")["host"]
    final = row["final_cleanup"]
    host_state(final["host"])
    usage(final["usage"])
    assert checkpoint(row, "resource_checkpoints", "initial")["usage"] == dict.fromkeys(USAGE_FIELDS, 0)
    assert initial["mirrored_ids"] == initial["actual_lane_ids"] == [0, 0]
    assert initial["current_epoch"] == initial["drained_epoch"]
    assert final["host"]["current_epoch"] == initial["current_epoch"] + 1
    assert final["host"]["drained_epoch"] == final["host"]["current_epoch"]
    assert final["host"]["mirrored_ids"] == final["host"]["actual_lane_ids"] == [0, 0]
    assert final["host"]["gaussian_jobs"]["active_installed_generation"] is None
    assert final["host"]["gaussian_jobs"]["preview"]["owns_job"] is False
    assert final["host"]["gaussian_jobs"]["apply"]["owns_job"] is False
    assert final["usage"] == dict.fromkeys(USAGE_FIELDS, 0)
    assert final["jobs"] == actual and final["output_aliases_dropped"] is True
    end_index = 1 if number == 12 else 0
    final_event = find_event(events, "resource_observation", name="after_host_drain_and_output_alias_drop",
                             request_index=end_index)
    dropped = [event for event in events if event["phase"] == "output_aliases_dropped"]
    assert dropped and dropped[-1]["sequence"] < final_event["sequence"]
    assert final_event["usage"] == final["usage"]

    requests = [value["parameters"] for value in row["api_inputs"] if value["method"] == "request_gaussian"]
    assert requests == [{"lane": "preview" if preview else "apply", "generation": 1 if preview else None,
                         "request_index": index} for index in range(2 if number == 12 else 1)]
    limits = [value["parameters"] for value in row["api_inputs"] if value["method"] == "set_scheduler_limits"]
    assert len(limits) == 1 and limits[0]["workers"] == 1 and limits[0]["capacity"] >= 2
    before_admission = find_event(events, "admission_before_submit", request_index=0)
    after_admission = find_event(events, "admission_after_submit", request_index=0)
    assert before_admission["sequence"] < after_admission["sequence"]
    assert before_admission["admission_epoch"] == initial["current_epoch"]
    assert after_admission["admission_epoch"] == before_admission["admission_epoch"]
    assert after_admission["actual_lane_ids"][slot] == first_id
    assert after_admission["mirrored_ids"] == [0, 0]

    if number <= 2:
        blocker_id = integer(row["blocker_job_id"], positive=True)
        assert set(ids) == {first_id, blocker_id} and blocker_id != first_id
        blocker_event = find_event(events, "job_observation", name="blocker_real_worker_entered")
        assert blocker_event["job"]["id"] == blocker_id and blocker_event["job"]["state"] == "Running"
        assert blocker_event["sequence"] < before_admission["sequence"]
        assert event_job(entered, first_id)["state"] == "Queued"
        assert event_job(returned, first_id)["state"] == "Canceled"
        assert event_job(entered, blocker_id)["state"] == event_job(returned, blocker_id)["state"] == "Running"
        assert entered["usage"]["running"] == 1 and entered["usage"]["queued"] == 1
        assert returned["usage"]["running"] == 1 and returned["usage"]["queued"] == 0
        assert returned["usage"]["running_bytes"] == event_job(returned, blocker_id)["resource_bytes"]
        assert entered["usage"]["retained_bytes"] == returned["usage"]["retained_bytes"] == 0
        assert not any(event["phase"] == "worker_entered" and event.get("lane") == "window" for event in events)
        canceled_blocker = unique_event(events, "blocker_cancel_requested")
        assert returned["sequence"] < canceled_blocker["sequence"] < unique_event(events, "all_gates_released")["sequence"]
        assert canceled_blocker["accepted"] is True and all(value["state"] == "Canceled" for value in actual)
    elif number <= 4:
        assert ids == [first_id]
        positive = [event for event in events if event["phase"] == "stage_progress" and event["stage"] == COPY_STAGE
                    and event["completed"] == 1024 and event["lane"] == "window"]
        assert len(positive) == 1 and positive[0]["total"] == SAMPLE_COUNT
        mirrored = find_event(events, "resource_observation", name="after_request_and_mirror", request_index=0)
        assert mirrored["host"]["mirrored_ids"][slot] == first_id
        assert mirrored["sequence"] < positive[0]["sequence"] < entered["sequence"]
        for event in [entered, returned]:
            value = event_job(event, first_id)
            assert value["state"] == "Running" and value["stage"] == COPY_STAGE
            assert value["stage_completed"] == 1024 and value["stage_total"] == SAMPLE_COUNT
            assert event["usage"]["running"] == 1 and event["usage"]["queued"] == 0
            assert event["usage"]["running_bytes"] == value["resource_bytes"]
            assert event["usage"]["retained_bytes"] == 0
        assert event_job(entered, first_id)["cancel_requested"] is False
        assert event_job(returned, first_id)["cancel_requested"] is True
        assert returned["sequence"] < unique_event(events, "all_gates_released")["sequence"]
    elif number in {5, 6, 7, 8, 12}:
        boundary = "admission_before_submit" if number in {5, 6, 12} else "admission_after_submit"
        host_gate = find_event(events, boundary, request_index=0)
        released = unique_event(events, "host_gate_released")
        assert released["boundary"] == boundary
        assert host_gate["sequence"] < entered["sequence"] < returned["sequence"] < released["sequence"]
        assert host_gate["current_epoch"] == host_gate["drained_epoch"] == initial["current_epoch"]
        assert host_gate["mirrored_ids"] == [0, 0]
        if boundary == "admission_before_submit":
            assert entered["jobs"] == returned["jobs"] == []
            assert entered["usage"] == returned["usage"] == dict.fromkeys(USAGE_FIELDS, 0)
            assert host_gate["actual_lane_ids"] == [0, 0]
            assert released["sequence"] < after_admission["sequence"]
            assert after_admission["current_epoch"] == initial["current_epoch"] + 1
        else:
            assert host_gate["actual_lane_ids"][slot] == first_id
            for event in [entered, returned]:
                value = event_job(event, first_id)
                assert value["state"] in {"Queued", "Running", "Ready"} and value["cancel_requested"] is False
        crossed = checkpoint(row, "host_checkpoints", "after_request_and_mirror")["host"]
        assert crossed["current_epoch"] == crossed["drained_epoch"] == initial["current_epoch"] + 1
        assert crossed["mirrored_ids"] == crossed["actual_lane_ids"] == [0, 0]
        assert row["first_terminal_job"]["state"] == "Canceled"
    elif number in {9, 10}:
        assert ids == [first_id]
        assert event_job(entered, first_id)["state"] == "Ready"
        assert event_job(returned, first_id)["state"] == "Canceled"
        assert entered["usage"]["running_bytes"] == 0 and entered["usage"]["running"] == 0
        assert entered["usage"]["retained_bytes"] == event_job(entered, first_id)["resource_bytes"] > 0
        if number == 9:
            assert returned["usage"] == dict.fromkeys(USAGE_FIELDS, 0)
        else:
            publish = unique_event(events, "before_native_publish")
            released = unique_event(events, "host_gate_released")
            assert publish["sequence"] < entered["sequence"] < returned["sequence"] < released["sequence"]
            assert released["boundary"] == "before_native_publish"
    else:
        assert number == 11 and ids == [first_id]
        registered = unique_event(events, "document_validator_registered_disarmed")
        armed = unique_event(events, "document_validator_armed_for_changed_dataset")
        validator = unique_event(events, "validator_gate_entered")
        released = unique_event(events, "validator_gate_released")
        assert registered["sequence"] < armed["sequence"] < before_admission["sequence"]
        assert validator["sequence"] < entered["sequence"] < returned["sequence"] < released["sequence"]
        for event in [entered, returned]:
            assert event_job(event, first_id)["state"] == "Publishing"
            assert event_job(event, first_id)["cancel_requested"] is False
            assert event["usage"]["retained_bytes"] == event_job(event, first_id)["resource_bytes"] > 0
        assert row["terminal_job"]["cancel_requested"] is False

    if winner:
        changed = source_state(after, root)
        assert changed != expected_input and changed == raw(row["expected_changed_raw"], root)
        assert all(math.isfinite(value[0]) for value in struct.iter_unpack("<f", changed))
        assert after["document_id"] == before["document_id"] and after["data_identity"] != before["data_identity"]
        assert after["revisions"]["pixels"] == before["revisions"]["pixels"] + 1
        for field in ["metadata", "selection", "display"]:
            assert after["revisions"][field] == before["revisions"][field]
        assert after["undo_steps"] == 1 and after["redo_steps"] == 0 and after["dirty"] is True
        assert after["workflow"] == [record] and after["results"] == before["results"]
        assert source_state(row["undo"], root) == expected_input
        assert row["undo"]["undo_steps"] == 0 and row["undo"]["redo_steps"] == 1
        assert source_state(row["redo"], root) == changed
        assert row["redo"]["undo_steps"] == 1 and row["redo"]["redo_steps"] == 0
        if number == 12:
            second_id = integer(row["second_native_job_id"], positive=True)
            assert second_id != first_id and set(ids) == {first_id, second_id}
            assert row["terminal_job"]["id"] == second_id
            same_source_state(before, row["before_second_admission"], root)
            first_cleanup = checkpoint(row, "host_checkpoints", "first_cleanup")["host"]
            assert first_cleanup["current_epoch"] == first_cleanup["drained_epoch"] == initial["current_epoch"] + 1
            assert checkpoint(row, "resource_checkpoints", "first_cleanup")["usage"] == dict.fromkeys(USAGE_FIELDS, 0)
            second = find_event(events, "admission_before_submit", request_index=1)
            assert second["admission_epoch"] == second["current_epoch"] == second["drained_epoch"] == first_cleanup["current_epoch"]
            assert returned["sequence"] < second["sequence"]
    else:
        same_source_state(before, after, root)
        assert row["first_terminal_job"]["state"] == "Canceled"
        assert row["terminal_job"]["id"] == first_id
    same_source_state(after, row["second_poll"], root)


@pytest.fixture(scope="module")
def cpp_report():
    configured = os.environ.get("IMAGEJXX_H1_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed H1 C++ report is missing: IMAGEJXX_H1_CPP_REPORT")
        pytest.skip("Installed H1 C++ report is optional outside the mandatory desktop gate")
    path = Path(configured).resolve(strict=True)
    report = json.loads(path.read_text(encoding="utf-8"))
    rows = report["cases"]
    assert len(rows) == 12 and sorted(row["case_id"] for row in rows) == CASES
    assert all(row["context"] == "imgui_cpp" for row in rows)
    return {row["case_id"]: row for row in rows}, path.parent


def desktop_module():
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        return importlib.import_module("imagejxx._desktop")
    return pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)


def python_row(case_id, directory):
    desktop = desktop_module()
    editor = desktop.Editor(preferences_path=str(directory.parent / f"{case_id}-preferences.json"))
    session = desktop._H1CancelSession()
    errors = []

    def cancel_on_python_thread():
        try:
            assert session.wait_for_cancel_point(timeout_ms=5000)
            session.cancel_entered()
            editor.cancel()
            session.cancel_returned()
        except BaseException as error:
            errors.append(error)
            session.abort()

    actor = threading.Thread(target=cancel_on_python_thread, name=f"{case_id}-python-canceller")
    actor.start()
    try:
        row = json.loads(editor._h1_case(
            case_id=case_id, context="imgui_python", session=session, directory=str(directory)))
        observation = json.loads(session.diagnostics())
    finally:
        session.abort()
        actor.join(timeout=5)
        assert not actor.is_alive(), "H1 Python cancellation actor did not join after abort/release"
    if errors:
        raise AssertionError("Actual Python Editor.cancel actor failed") from errors[0]
    assert observation["cancel_point_reached"] is True
    assert observation["cancel_entered"] is True and observation["cancel_returned"] is True
    assert observation["cancel_calls"] == 1 and observation["timeouts"] == []
    assert observation["creator_thread"] != observation["canceller_thread"]
    assert observation["ordered_probe_events"] == row["ordered_probe_events"]
    return row


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_h1_actual_off_thread_cancellation(case_id, context, request, tmp_path):
    if context == "imgui_cpp":
        rows, root = request.getfixturevalue("cpp_report")
        row = rows[case_id]
    else:
        root = tmp_path / case_id
        assert not root.exists()
        row = python_row(case_id, root)
    assert_h1_row(row, case_id, context, root)
