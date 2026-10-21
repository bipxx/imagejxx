"""Independent group18 observations from both installed shared ImGui launchers.

This source-only reader does not certify upstream dialog/runtime behavior. Its
pixel oracles are unmodified, pinned ImageJ numeric observations and a separate
project-owned analytic input. Native job/owner observations establish replay
execution; declared PASS text does not supply any scientific expected value.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path

import pytest


GROUP_ID = "P02a.shared_ui.18.record-and-replay"
VARIANTS = [GROUP_ID + ".compatible-explicit-rebind",
            GROUP_ID + ".incompatible-replay-atomic"]
CONTEXTS = ["imgui_cpp", "imgui_python"]
SHAPE = [17, 23]
SAMPLES = 17 * 23
UI_CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
NUMERIC_CONTRACT_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
INPUT_SHA = "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4"
ORACLE_HASHES = {
    "u8-full-s0p5": "c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53",
    "u8-full-s2": "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b",
    "u8-scaled-s2-px0p5-py2-run-scaled-harness":
        "9c5c2ba837d156d0b25f8f73ddb7f7da3291037cd522f74b8d9b56f8f0e253ad",
}
CALIBRATION = {"spacing": [0.5, 2, 1], "origin": [0, 0, 0],
               "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "um",
               "frame_interval": 1, "time_origin": 0, "time_unit": "s"}
PIXEL_CALIBRATION = {**CALIBRATION, "spacing": [1, 1, 1], "unit": "px"}
INCOMPATIBLE_CALIBRATION = {**CALIBRATION, "spacing": [1, 1, 1], "unit": "mm"}
EXPLICIT_REQUEST = {"scope": "current_plane", "roi": "whole_plane",
                    "mode": "planes", "destination": "in_place"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixture_bytes():
    return bytes(((37*x + 61*y + 13*x*y + 19*(x % 3) + 23*(y % 5)) % 256
                  + (47 if x == 0 else 0) + (83 if y == 16 else 0)) % 256
                 for y in range(17) for x in range(23))


def integer(value, *, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def no_constant(value):
    raise ValueError(f"Nonfinite JSON constant: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def decode(data):
    result = json.loads(data, parse_constant=no_constant, object_pairs_hook=unique_object)
    def finite(value):
        if type(value) is float:
            assert math.isfinite(value)
        elif type(value) is dict:
            for item in value.values():
                finite(item)
        elif type(value) is list:
            for item in value:
                finite(item)
    finite(result)
    return result


def workspace():
    for parent in Path(__file__).resolve().parents:
        if (parent / "src/desktop/window.cpp").is_file() and (parent / "docs/PLAN.md").is_file():
            return parent
    pytest.fail("Cannot locate adopted shared UI contract; configure its explicit path")


def configured_pin(path_name, hash_name, expected):
    path = os.environ.get(path_name)
    digest = os.environ.get(hash_name)
    assert path and digest, f"Required replay evidence missing: {path_name}/{hash_name}"
    assert digest == expected, f"Replay gate must pin the adopted {path_name} input"
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    assert sha(data) == digest
    return path, data


@pytest.fixture(scope="module")
def replay_oracles():
    assert os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0") in {"0", "1"}
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") != "1" and not os.environ.get(
            "IMAGEJXX_GAUSSIAN_REFERENCE_JSONL"):
        pytest.skip("Group18 installed UI is optional outside a required desktop gate")
    _, reference = configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                                  "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", REFERENCE_SHA)
    _, contract_raw = configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                     "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", NUMERIC_CONTRACT_SHA)
    numeric = decode(contract_raw)
    assert numeric["schema_version"] == 1
    configured = os.environ.get("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON")
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        assert configured, "Required installed replay gate must supply IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON"
    ui_path = Path(configured) if configured else workspace() / (
        "build/plan-implementation/p02a-gaussian-command-001/shared-ui-case-contract-002.json")
    ui_raw = ui_path.resolve(strict=True).read_bytes()
    assert sha(ui_raw) == UI_CONTRACT_SHA
    ui = decode(ui_raw)
    group = next(item for item in ui["cases"] if item["id"] == GROUP_ID)
    assert [item["id"] for item in group["variants"]] == VARIANTS
    assert sum(len(item["steps"]) for item in group["variants"]) == 64
    original = fixture_bytes()
    assert len(original) == SAMPLES and sha(original) == INPUT_SHA
    selected = {}
    for line in reference.split(b"\n"):
        if not line.strip():
            continue
        record = decode(line)
        if record["id"] not in ORACLE_HASHES:
            continue
        assert record["id"] not in selected
        assert record["shape"] == SHAPE and record["dtype"] == "U8"
        assert record["input_formula"] == "owned-asymmetric-analytic-v1"
        assert bytes.fromhex(record["input_raw_hex"]) == bytes(record["input"]) == original
        actual = bytes.fromhex(record["output_raw_hex"])
        assert bytes(record["output"]) == actual and len(actual) == SAMPLES
        assert sha(actual) == record["output_sha256"] == ORACLE_HASHES[record["id"]]
        assert record["raw_reference_output_unmodified"] is True
        assert record["outside_changed_pixels"] == 0 and record["outside_changes"] == []
        assert record["roi"]["processor_bounds"] == [0, 0, 23, 17]
        assert record["roi"]["kind"] == "none" and record["roi"]["is_area"] is False
        assert record["full_plugin_runner_executed"] is False
        assert record["dialog_executed"] is False
        selected[record["id"]] = actual
    assert set(selected) == set(ORACLE_HASHES)
    return {"input": original, "outputs": selected,
            "variants": {item["id"]: item for item in group["variants"]}}


def raw(member, directory, *, shape=SHAPE):
    root = directory.resolve(strict=True)
    assert type(member["member_id"]) is str and member["member_id"]
    member_path = (root / member["member_id"]).resolve(strict=True)
    path = Path(member["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path == member_path and path.is_relative_to(root) and path.is_file()
    size = math.prod(shape)
    assert member["shape"] == shape and member["dtype"] == "uint8"
    assert member["encoding"] == "UInt8" and member["sample_count"] == size
    assert member["byte_count"] == size
    data = path.read_bytes()
    assert len(data) == size and sha(data) == member["sha256"]
    return data


def member_path(directory, name):
    root = directory.resolve(strict=True)
    assert type(name) is str and name
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file()
    return path


def usage(value, *, empty=False):
    assert set(value) == {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]
    if empty:
        assert not any(value.values())


def jobs(value):
    assert type(value) is list
    result = {}
    for job in value:
        job_id = integer(job["id"], positive=True)
        assert job_id not in result
        integer(job["document_id"], positive=True)
        assert type(job["label"]) is str and type(job["error"]) is str
        assert type(job["cancel_requested"]) is bool
        assert job["state"] in {"QUEUED", "RUNNING", "READY", "PUBLISHING",
                                "COMPLETED", "FAILED", "CANCELED"}
        for field in ("resource_bytes", "completed", "total"):
            integer(job[field])
        assert type(job["stage"]) is str
        result[job_id] = job
    return result


def document(value, directory, expected):
    integer(value["document_id"], positive=True)
    integer(value["data_identity"])
    assert value["shape_tcz_yxs"] == [1, 1, 1, 17, 23, 1] and value["dtype"] == "uint8"
    assert type(value["dirty"]) is bool
    integer(value["undo_steps"])
    integer(value["redo_steps"])
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for item in value["revisions"].values():
        integer(item)
    assert set(value["calibration"]) == set(CALIBRATION)
    assert value["history"]["undo_steps"] == value["undo_steps"]
    assert value["history"]["redo_steps"] == value["redo_steps"]
    assert value["history"]["pixels"]["exact"] is True
    for key, item in value["history"]["pixels"].items():
        if key != "exact":
            integer(item)
    assert len(value["planes"]) == 1 and value["planes"][0]["position"] == [0, 0, 0]
    plane = value["planes"][0]
    assert raw(plane["raw"], directory) == expected
    assert all(type(plane[key]) is str and int(plane[key], 16) > 0
               for key in ("image_address", "buffer_address"))
    info = value["document_info"]
    assert info["id"] == value["document_id"] and info["dtype"] == "uint8"
    assert info["dirty"] is value["dirty"]
    assert info["undo_steps"] == value["undo_steps"] and info["redo_steps"] == value["redo_steps"]


def seeded(value, directory, original, calibration):
    document(value, directory, original)
    assert value["calibration"] == calibration
    assert value["dirty"] is False and value["undo_steps"] == value["redo_steps"] == 0
    assert value["selection"] is value["previous_selection"] is None
    assert value["rois"] == value["overlays"] == [] and value["spectrum_present"] is False
    assert value["display"]["properties"]["display_range"] == [0, 255]
    assert value["display"]["t"] == value["display"]["c"] == 0
    assert value["display"]["crosshair"] == [11, 8, 0]
    assert value["display"]["orthogonal"] is False


def preserved(value, baseline):
    for field in ("document_id", "shape_tcz_yxs", "dtype", "calibration", "selection",
                  "previous_selection", "rois", "overlays", "spectrum_present", "metadata", "display"):
        assert value[field] == baseline[field], f"Typed replay changed preserved native {field}"
    for field in ("metadata", "selection", "display"):
        assert value["revisions"][field] == baseline["revisions"][field]


def unchanged_document(value, baseline, directory, expected):
    document(value, directory, expected)
    assert set(value) == set(baseline)
    for key in baseline:
        if key != "planes":
            assert value[key] == baseline[key], f"Pre-admission rejection changed {key}"
    for actual, before in zip(value["planes"], baseline["planes"]):
        assert {key: item for key, item in actual.items() if key != "raw"} == {
            key: item for key, item in before.items() if key != "raw"}


def global_state(value):
    integer(value["active_document_id"], positive=True)
    assert type(value["recording"]) is bool
    for field in ("application_document_ids", "desktop_document_ids"):
        assert type(value[field]) is list and len(value[field]) == len(set(value[field]))
        for item in value[field]:
            integer(item, positive=True)
    assert set(value["application_document_ids"]) == set(value["desktop_document_ids"])
    assert {item["document_id"] for item in value["documents"].values()} == set(value["application_document_ids"])
    assert value["active_document_id"] in value["application_document_ids"]
    assert type(value["workflow"]) is list and type(value["results"]) is list


def actual_control(rendered, selector, document_id=None):
    if selector.startswith("gaussian_panel.controls."):
        name = selector.removeprefix("gaussian_panel.controls.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    if selector == "workflow.record_edits":
        return rendered["workflow_controls"]["record"]
    if selector == "workflow.run_on_image":
        return rendered["workflow_controls"]["run"]
    if selector.startswith("document_tabs."):
        matches = [item for item in rendered["document_tabs"] if item["document_id"] == document_id]
        assert len(matches) == 1
        return matches[0]
    if selector.startswith("replay."):
        name = selector.removeprefix("replay.")
        name = {"process": "processing_scope", "process.choice.Current plane": "processing_scope.choice.Current plane",
                "region": "region", "region.choice.Whole plane": "region.choice.Whole plane",
                "consent": "use_destination_calibration", "ok": "apply"}.get(name, name)
        return rendered["dialog"]["controls"][name]
    assert selector == "error.ok", f"Unreviewed replay physical selector: {selector}"
    return rendered["error_modal"]["controls"]["ok"]


def physical_frames(row, directory, adopted):
    frames = row["frames"]
    assert frames and [item["ordinal"] for item in frames] == list(range(1, len(frames)+1))
    sessions = []
    for frame in frames:
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        jobs(frame["actual_native_jobs"])
        rendered = frame["rendered"]
        assert rendered["workflow_replay_observation_failed"] is False
        session = rendered["session"]
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        sessions.append(session)
        actual = rendered["input_frame"]
        assert actual["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "keys", "text"}
            if event["type"] == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert actual["mouse_position"] == [math.floor(item) for item in event["point"]]
            elif event["type"] == "button":
                assert event["name"] == "left" and type(event["down"]) is bool
                assert actual["mouse_down"][0] is event["down"]
            elif event["type"] == "keys":
                for atom in event["keys"]:
                    assert set(atom) == {"name", "down"} and type(atom["down"]) is bool
                    assert (atom["name"] in actual["keys_down"]) is atom["down"]
            else:
                assert type(event["value"]) is str
    assert len({item["session_id"] for item in sessions}) == len({item["context_id"] for item in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"]+1 and after["imgui_time"] > before["imgui_time"]
    steps = row["steps"]
    assert [item["ordinal"] for item in steps] == list(range(1, 33))
    assert [item["adopted_step"] for item in steps] == adopted["steps"]
    previous_last = 0
    for step in steps:
        first, last = step["first_frame_ordinal"], step["last_frame_ordinal"]
        integer(first)
        integer(last)
        assert previous_last <= first <= last <= len(frames)
        previous_last = last
        kind = step["adopted_step"]["op"]
        assert step["completed"] is True and step["before_frame_ordinal"] == first
        if kind == "observe":
            assert first == last, "Observe must not inject a frame or acknowledge a popup"
        elif kind in {"click", "select_choice", "replace_sigma"}:
            assert last > first
        for frame in frames[first:last]:
            assert frame["step_ordinal"] == step["ordinal"]
    for observation in row["control_observations"]:
        prior = integer(observation["before_frame_ordinal"], positive=True)
        assert prior+3 <= len(frames)
        control = observation["control"]
        integer(control["id"], positive=True)
        assert control["enabled"] is True
        assert type(control["active"]) is type(control["focused"]) is bool
        assert control == actual_control(frames[prior-1]["rendered"], observation["selector"],
                                         control.get("document_id"))
        x, y = observation["position"]
        rect = control["rect"]
        assert len(rect) == 4 and all(type(item) in {int, float} and math.isfinite(item) for item in rect)
        assert x == math.floor(x) and y == math.floor(y)
        assert rect[0] < x < rect[2] and rect[1] < y < rect[3]
        assert [item["requested_events"] for item in frames[prior:prior+3]] == [
            [{"type": "position", "space": "screen", "point": [x, y]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        positioned = actual_control(frames[prior]["rendered"], observation["selector"], control.get("document_id"))
        assert positioned["id"] == control["id"] and positioned["enabled"] is True
        current_rect = positioned["rect"]
        assert current_rect[0] < x < current_rect[2] and current_rect[1] < y < current_rect[3]
    return frames


def physical_steps(row, frames, adopted):
    observations = row["control_observations"]
    used = set()
    api = [item for item in row["api_inputs"]
           if item["purpose"] != "bounded final/failure native job cleanup only"]
    source_preps = [item for item in api if item["step_ordinal"] == 0]
    assert len(source_preps) == 1 and source_preps[0]["role"] == "source"
    assert source_preps[0]["purpose"] == "explicit fresh UNBLURRED fixture/calibration preparation"
    assert source_preps[0]["methods"] == ["addNativeDocument", "setNativeDisplay", "mark_saved"]
    assert source_preps[0]["calibration"] == CALIBRATION
    assert source_preps[0]["tested_changes_use_API_setters"] is False
    cleanups = [item for item in row["api_inputs"]
                if item["purpose"] == "bounded final/failure native job cleanup only"]
    assert cleanups == [{"step_ordinal": 32, "purpose": "bounded final/failure native job cleanup only",
                         "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"]}]
    allowed_api_steps = {0} | {item["ordinal"] for item in row["steps"]
                               if item["adopted_step"]["op"] in {"open_command", "api_preparation"}}
    assert all(item["step_ordinal"] in allowed_api_steps for item in api)
    for step in row["steps"]:
        declaration = step["adopted_step"]
        operation = declaration["op"]
        ordinal = step["ordinal"]
        actual_frames = frames[step["first_frame_ordinal"]:step["last_frame_ordinal"]]
        clicked = [item for item in observations if item["step_ordinal"] == ordinal]
        prep = [item for item in api if item["step_ordinal"] == ordinal]
        if operation == "click":
            assert len(clicked) == 1 and clicked[0]["selector"] == declaration["selector"]
            used.add(observations.index(clicked[0]))
            assert not prep
            if declaration["selector"] == "workflow.record_edits":
                before = frames[step["first_frame_ordinal"]-1]["rendered"]["workflow_controls"]["record"]["value"]
                after = frames[step["last_frame_ordinal"]-1]["rendered"]["workflow_controls"]["record"]["value"]
                assert type(before) is type(after) is bool and after is not before
        elif operation == "select_choice":
            assert [item["selector"] for item in clicked] == [
                declaration["selector"], declaration["selector"]+".choice."+declaration["choice"]]
            used.update(observations.index(item) for item in clicked)
            assert not prep
        elif operation == "replace_sigma":
            assert len(clicked) == 1 and clicked[0]["selector"] == "gaussian_panel.controls.sigma"
            used.add(observations.index(clicked[0]))
            input_id = clicked[0]["control"]["id"]
            assert not prep
            key_frames = [item for item in actual_frames if item["requested_events"] and
                          item["requested_events"][0]["type"] == "keys"]
            assert [item["requested_events"] for item in key_frames] == [
                [{"type": "keys", "keys": [{"name": "Ctrl", "down": True}, {"name": "A", "down": True}]}],
                [{"type": "keys", "keys": [{"name": "Ctrl", "down": False}, {"name": "A", "down": False}]}],
            ]
            text_frames = [item for item in actual_frames if item["requested_events"] and
                           item["requested_events"][0]["type"] == "text"]
            assert len(text_frames) == 1
            text_frame = text_frames[0]
            assert text_frame["requested_events"] == [{"type": "text", "value": declaration["text"]}]
            text_input = text_frame["rendered"]["input_frame"]
            assert text_input["focus"]["active_id"] == text_input["input_text"]["id"] == input_id
            settled = actual_frames[-1]
            assert settled["requested_events"] == []
            panel = settled["rendered"]["gaussian_panel"]
            assert panel["sigma_text"] == declaration["text"] and panel["sigma"] == float(declaration["text"])
            assert panel["draft_valid"] is True and panel["preview"] is False
            assert not settled["rendered"]["input_frame"]["keys_down"]
        elif operation == "open_command":
            assert not clicked and len(prep) == 1
            assert prep[0] == {"step_ordinal": ordinal,
                               "purpose": "adopted labelled_action_preparation opening only",
                               "action": "show_operation",
                               "parameters": {"operation": "gaussian_blur_imagej"}}
            panel = actual_frames[-1]["rendered"]["gaussian_panel"]
            assert panel["open"] is True and panel["preview"] is False
            if ordinal == 2:
                assert panel["sigma"] == 2 and panel["scaled_units"] is False
                assert panel["scaled_available"] is True
            elif ordinal == 12:
                assert panel["scaled_available"] is False and panel["scaled_units"] is False
                assert "scaled" not in panel["controls"]
        elif operation == "api_preparation":
            assert not clicked and len(prep) == 1
            item = prep[0]
            assert item["purpose"] == "explicit fresh UNBLURRED fixture/calibration preparation"
            assert item["methods"] == ["addNativeDocument", "setNativeDisplay", "mark_saved"]
            assert item["tested_changes_use_API_setters"] is False
            integer(item["document_id"], positive=True)
            role = "throwaway" if declaration["name"] == "create_throwaway_document" else "replay"
            assert item["role"] == role
            assert item["calibration"] == declaration.get("calibration", PIXEL_CALIBRATION)
        else:
            assert operation in {"observe", "await_native"} and not clicked and not prep
            assert not any(item["requested_events"] for item in actual_frames)
        if "expected_dialog_values" in declaration:
            assert frames[step["last_frame_ordinal"]-1]["rendered"]["dialog"]["values"] == declaration["expected_dialog_values"]
    assert used == set(range(len(observations))), "Unreviewed physical controls were substituted or added"
    assert [item["adopted_step"] for item in row["steps"]] == adopted["steps"]


def record_equal(member, record, directory):
    path = member_path(directory, member["member_id"])
    configured = Path(member["path"])
    if not configured.is_absolute():
        configured = directory / configured
    assert configured.resolve(strict=True) == path
    data = path.read_bytes()
    assert data == member["canonical_utf8"].encode("utf-8")
    assert sha(data) == member["sha256"]
    assert decode(data) == member["record"] == record
    assert member["canonicalization"] == (
        "nlohmann default JSON compact UTF-8 dump with lexical object keys; hash actual bytes")
    # Parse and fully compare the actual compact serialization. Python and C++
    # differ in finite number spelling, so its exact digest belongs to the
    # observed bytes, while every semantic field is independently checked below.
    assert b"\n" not in data and b"\r" not in data


def expected_record(record, source_before, *, calibration=CALIBRATION, sigma_xy=(4, 1),
                    owner=None, roi="active_selection", revisions=None):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": 2, "scaled_units": True}
    assert type(record["options"]["scaled_units"]) is bool
    assert record["calibration"] == calibration and record["effective_sigma_xy"] == list(sigma_xy)
    target = record["target"]
    owner = source_before["document_id"] if owner is None else owner
    revisions = ([source_before["revisions"][key] for key in ("pixels", "metadata", "selection")]
                 if revisions is None else revisions)
    assert target == {"schema_version": 1, "document_id": owner,
                      "source_revisions": revisions, "source_shape": [1, 1, 1, 17, 23, 1],
                      "scope": "ranges", "ranges": [[0, 1, 1], [0, 1, 1], [0, 1, 1]],
                      "positions": [], "roi_policy": roi, "execution_mode": "planes", "destination": "in_place"}
    assert type(target["schema_version"]) is int
    integer(target["document_id"], positive=True)
    for values in [target["source_revisions"], target["source_shape"], *target["ranges"]]:
        for value in values:
            integer(value)
    for values in (record["effective_sigma_xy"], calibration["spacing"], calibration["origin"], calibration["direction"]):
        assert all(type(value) in {int, float} and math.isfinite(value) for value in values)
    assert type(record["options"]["sigma"]) in {int, float}


def prompt(checkpoint, native_id, *, whole=False, consent=False, controls=True):
    rendered = checkpoint["rendered"]
    value = rendered["dialog"]
    assert value["open"] is True and value["is_operation"] is False
    assert value["title"] == "Run workflow" and value["opened_document_id"] == native_id
    assert value["opened_document_id_semantics"] == "observed_at_ask_not_a_captured_execution_source"
    integer(value["generation"], positive=True)
    assert value["values"] == {"processing_scope": "Current plane",
                               "region": "Whole plane" if whole else "Active selection",
                               "use_destination_calibration": consent}
    assert type(value["values"]["use_destination_calibration"]) is bool
    assert value["schema"]
    if controls:
        for key in ("processing_scope", "region", "use_destination_calibration", "apply"):
            assert value["controls"][key]["enabled"] is True
            integer(value["controls"][key]["id"], positive=True)
    return value


def no_new_job(checkpoint, baseline):
    assert set(jobs(checkpoint["actual_native_jobs"])) == set(jobs(baseline["actual_native_jobs"]))
    assert len(checkpoint["actual_native_jobs"]) == len(baseline["actual_native_jobs"])


def replay_lineage(checkpoint, before, record, *, incompatible, terminal):
    value = checkpoint["rendered"]["workflow_replay"]
    assert value["schema_version"] == 1 and value["observation_failed"] is False
    assert value["ownership"] == "scalar_and_record_values_only_no_result_aliases"
    native_id = before["native_state"]["documents"]["replay"]["document_id"]
    assert value["live_destination_id"] == native_id
    assert value["original_steps"] == [record] and value["explicit_request"] == EXPLICIT_REQUEST
    job_id = integer(value["job_id"], positive=True)
    baseline_jobs = jobs(before["actual_native_jobs"])
    current_jobs = jobs(checkpoint["actual_native_jobs"])
    assert job_id not in baseline_jobs and job_id in current_jobs
    assert set(current_jobs)-set(baseline_jobs) == {job_id}
    actual = current_jobs[job_id]
    assert actual["label"] == "Workflow replay" and actual["document_id"] == native_id
    events = value["events"]
    assert [event["sequence"] for event in events] == list(range(1, len(events)+1))
    assert 0 <= len(events) <= 3
    if terminal:
        assert [event["phase"] for event in events] == ["working_copy_created", "command_resolved", "scratch_published"]
    else:
        assert [event["phase"] for event in events] == ["working_copy_created", "command_resolved", "scratch_published"][:len(events)]
    owners = None
    for event in events:
        assert event["live_destination_id"] == native_id and event["step_index"] == 0
        captured = integer(event["captured_snapshot_owner_id"], positive=True)
        working = integer(event["working_copy_id"], positive=True)
        assert len({captured, working, native_id}) == 3
        assert working != record["target"]["document_id"] and captured != record["target"]["document_id"]
        if owners is None:
            owners = (captured, working)
        assert owners == (captured, working)
        if event["phase"] == "working_copy_created":
            assert "resolved_command_record" not in event and "original_record" not in event
            assert "resolved_command_parameters" not in event
        else:
            assert event["original_record"] == record
            assert event["original_record_source_id"] == record["target"]["document_id"]
            assert event["original_record_target"] == record["target"]
            expected_record(event["resolved_command_record"], before["native_state"]["documents"]["replay"],
                            calibration=INCOMPATIBLE_CALIBRATION if incompatible else CALIBRATION,
                            sigma_xy=(2, 2) if incompatible else (4, 1), owner=working,
                            roi="whole_plane", revisions=[0, 0, 0])
            assert event["resolved_command_parameters"] == {"sigma": [2, 2] if incompatible else [1, 4]}
            assert event["resolved_command_parameter_shapes"] == {"sigma": [2]}
            if event["phase"] == "scratch_published":
                assert event["scratch_publication_document_id"] == working
            else:
                assert "scratch_publication_document_id" not in event
    if terminal:
        assert actual["state"] == "COMPLETED" and actual["error"] == "" and actual["cancel_requested"] is False
        final = value["terminal_job"]
        assert final["id"] == job_id and final["document_id"] == native_id
        assert final["label"] == "Workflow replay" and final["state"] == "completed"
        assert final["error"] == "" and final["cancel_requested"] is False
        integer(final["resource_bytes"])
        assert value["publication_succeeded"] is True and value["final_publication_document_id"] == native_id
    if value["terminal_job"] is not None and value["terminal_job"]["state"] in {"failed", "canceled"}:
        assert value["publication_succeeded"] is False and value["final_publication_document_id"] is None
    return job_id


def cpu_readback(checkpoint, directory, expected):
    ordinary = checkpoint["rendered"]["ordinary_pixels"]
    assert ordinary["pixels_source"] == "live" and ordinary["contrast_source"] == "live"
    readback = ordinary["readback"]
    assert readback["available"] is True
    source = readback["source"]
    assert source["dtype"] == "uint8" and source["shape"] == [17, 23, 1]
    assert "pixels" not in source and "no GPU readback" in source["observation_scope"]
    assert raw(source["raw"], directory, shape=[17, 23, 1]) == expected
    rgb = readback["rendered_rgb"]
    assert rgb["dtype"] == "uint8" and rgb["shape"] == [17, 23, 3]
    assert "pixels" not in rgb and "no GPU readback" in rgb["observation_scope"]
    assert raw(rgb["raw"], directory, shape=[17, 23, 3]) == bytes(
        channel for sample in expected for channel in (sample, sample, sample))


def assert_row(row, variant, context, directory, evidence):
    adopted = evidence["variants"][variant]
    incompatible = variant == VARIANTS[1]
    assert type(row["schema_version"]) is int and row["schema_version"] == 1
    assert row["group_id"] == GROUP_ID and row["case_id"] == variant and row["context"] == context
    assert row["contract_sha256"] == UI_CONTRACT_SHA and row["adopted_variant"] == adopted
    assert row["status"] == "passed" and row["acceptance_claim"] is False
    assert all(row[field] is False for field in ("full61_verified", "full122_verified", "family_complete", "release_accepted"))
    assert decode(member_path(directory, "case.json").read_bytes()) == row
    original = evidence["input"]
    assert raw(row["fixture_input_raw"], directory) == original
    frames = physical_frames(row, directory, adopted)
    physical_steps(row, frames, adopted)
    checkpoints = {}
    for checkpoint in row["checkpoints"]:
        name = checkpoint["name"]
        assert name not in checkpoints
        checkpoints[name] = checkpoint
        assert decode(member_path(directory, f"checkpoint-{name}.json").read_bytes()) == checkpoint
        integer(checkpoint["frame_ordinal"], positive=True)
        assert checkpoint["rendered"] == frames[checkpoint["frame_ordinal"]-1]["rendered"]
        usage(checkpoint["usage"])
        jobs(checkpoint["actual_native_jobs"])
        global_state(checkpoint["native_state"])
        if name not in {"baseline", "cleanup"}:
            step = row["steps"][checkpoint["step_ordinal"]-1]
            assert step["adopted_step"]["op"] == "observe"
            assert step["checkpoint_name"] == name
            assert checkpoint["frame_ordinal"] == step["first_frame_ordinal"] == step["last_frame_ordinal"]
    expected_names = (["baseline", "source_applied", "before_replay", "run_prompt_open", "run_prompt_choices",
                       "replay_rejected", "replay_error_acknowledged", "run_prompt_explicit_consent",
                       "recalibration_replay_admitted", "recalibrated_replayed", "remembered_after_replay", "cleanup"]
                      if incompatible else ["baseline", "source_applied", "recorded", "throwaway_applied", "before_replay",
                                            "run_prompt_open", "run_prompt_choices", "replay_admitted", "replayed",
                                            "remembered_after_replay", "cleanup"])
    assert list(checkpoints) == expected_names
    baseline = checkpoints["baseline"]
    baseline_state = baseline["native_state"]
    assert set(baseline_state["documents"]) == {"source"}
    source_before = baseline_state["documents"]["source"]
    seeded(source_before, directory, original, CALIBRATION)
    assert source_before["metadata"] == {"title": "Owned Gaussian replay source", "properties": {},
                                         "extensions": {}, "record_layout": {}}
    source_prep = next(item for item in row["api_inputs"] if item["step_ordinal"] == 0)
    assert source_prep["document_id"] == source_before["document_id"]
    assert baseline_state["active_document_id"] == source_before["document_id"]
    assert baseline_state["workflow"] == baseline_state["results"] == [] and baseline_state["recording"] is False
    applied = checkpoints["source_applied"]
    assert applied["label"] == "applied" and applied["occurrence"] == 1
    source_applied = applied["native_state"]["documents"]["source"]
    scaled = evidence["outputs"]["u8-scaled-s2-px0p5-py2-run-scaled-harness"]
    document(source_applied, directory, scaled)
    preserved(source_applied, source_before)
    assert source_applied["undo_steps"] == source_before["undo_steps"]+1 and source_applied["redo_steps"] == 0
    assert source_applied["dirty"] is True and source_applied["data_identity"] != source_before["data_identity"]
    assert source_applied["revisions"]["pixels"] == source_before["revisions"]["pixels"]+1
    assert applied["native_state"]["recording"] is True
    record = row["original_record"]["record"]
    expected_record(record, source_before)
    record_equal(row["original_record"], record, directory)
    assert row["initial_apply_command_record"] == record
    assert applied["rendered"]["gaussian_jobs"]["apply"]["command_record"] == record
    assert applied["native_state"]["workflow"] == [record]
    cpu_readback(applied, directory, scaled)
    if not incompatible:
        recorded = checkpoints["recorded"]
        assert recorded["native_state"]["workflow"] == [record] and recorded["native_state"]["recording"] is True
        assert recorded["rendered"]["gaussian_jobs"]["apply"]["command_record"] == record
        throwaway_applied = checkpoints["throwaway_applied"]
        assert throwaway_applied["label"] == "applied" and throwaway_applied["occurrence"] == 2
        throwaway = throwaway_applied["native_state"]["documents"]["throwaway"]
        half = evidence["outputs"]["u8-full-s0p5"]
        document(throwaway, directory, half)
        assert throwaway["calibration"] == PIXEL_CALIBRATION
        assert throwaway["undo_steps"] == 1 and throwaway["redo_steps"] == 0 and throwaway["dirty"] is True
        assert throwaway_applied["native_state"]["recording"] is False
        assert throwaway_applied["native_state"]["workflow"] == [record]
        assert throwaway["document_id"] != source_before["document_id"]
        cpu_readback(throwaway_applied, directory, half)
    before = checkpoints["before_replay"]
    before_state = before["native_state"]
    destination = before_state["documents"]["replay"]
    seeded(destination, directory, original, INCOMPATIBLE_CALIBRATION if incompatible else CALIBRATION)
    assert destination["metadata"] == {"title": "Owned Gaussian replay replay", "properties": {},
                                       "extensions": {}, "record_layout": {}}
    for item in row["api_inputs"]:
        if item["purpose"] == "explicit fresh UNBLURRED fixture/calibration preparation":
            assert item["document_id"] == before_state["documents"][item["role"]]["document_id"]
    for step in row["steps"]:
        selector = step["adopted_step"].get("selector", "")
        if selector.startswith("document_tabs."):
            role = selector.removeprefix("document_tabs.")
            target_id = before_state["documents"][role]["document_id"]
            actual_tabs = frames[step["last_frame_ordinal"]-1]["rendered"]["document_tabs"]
            assert next(item for item in actual_tabs if item["document_id"] == target_id)["selected"] is True
            tab_observation = next(item for item in row["control_observations"] if item["step_ordinal"] == step["ordinal"])
            assert tab_observation["control"]["document_id"] == target_id
    assert before_state["active_document_id"] == destination["document_id"]
    assert destination["document_id"] != source_before["document_id"]
    assert before_state["workflow"] == [record] and before_state["recording"] is False
    assert before_state["results"] == baseline_state["results"]
    expected_remembered = {"sigma": 2 if incompatible else 0.5, "scaled_units": incompatible}
    assert before["rendered"]["gaussian_panel"]["remembered"] == expected_remembered
    cpu_readback(before, directory, original)
    opening = checkpoints["run_prompt_open"]
    initial_prompt = prompt(opening, destination["document_id"])
    no_new_job(opening, before)
    choices = checkpoints["run_prompt_choices"]
    chosen_prompt = prompt(choices, destination["document_id"], whole=True)
    no_new_job(choices, before)
    for field in ("generation", "schema", "opened_document_id"):
        assert initial_prompt[field] == chosen_prompt[field]
    if incompatible:
        rejected = checkpoints["replay_rejected"]
        rejected_state = rejected["native_state"]
        assert rejected["step_ordinal"] == 19 and rejected["label"] == "replay_rejected"
        assert row["rejection_evaluated_at_frame"] == rejected["frame_ordinal"]
        no_new_job(rejected, before)
        assert rejected_state.keys() == before_state.keys()
        for field in before_state:
            if field != "documents":
                assert rejected_state[field] == before_state[field]
        for role, old in before_state["documents"].items():
            expected = original if role == "replay" else scaled
            unchanged_document(rejected_state["documents"][role], old, directory, expected)
        rejected_prompt = prompt(rejected, destination["document_id"], whole=True, controls=False)
        for field in ("generation", "schema", "opened_document_id", "values"):
            assert rejected_prompt[field] == chosen_prompt[field]
        error = rejected["rendered"]["error_modal"]
        assert error["open"] is True and error["message"] == rejected["rendered"]["window_error"]
        assert "calibration" in error["message"].lower()
        assert "Use this image's calibration" in error["message"]
        integer(error["controls"]["ok"]["id"], positive=True)
        assert rejected["rendered"]["gaussian_panel"]["remembered"] == expected_remembered
        # Historical lineage can remain visible. Actual scheduler sets and its
        # immutable old scalar lineage both demonstrate no new admission.
        assert rejected["rendered"]["workflow_replay"] == before["rendered"]["workflow_replay"]
        acknowledged = checkpoints["replay_error_acknowledged"]
        no_new_job(acknowledged, before)
        ack_step, wait_step = row["steps"][19], row["steps"][20]
        assert ack_step["adopted_step"] == {"op": "click", "selector": "error.ok"}
        assert rejected["frame_ordinal"] <= ack_step["first_frame_ordinal"] < ack_step["last_frame_ordinal"]
        assert wait_step["first_frame_ordinal"] == ack_step["last_frame_ordinal"]
        assert wait_step["last_frame_ordinal"] > wait_step["first_frame_ordinal"]
        waited = frames[wait_step["first_frame_ordinal"]:wait_step["last_frame_ordinal"]]
        assert waited and all(frame["requested_events"] == [] for frame in waited)
        recovered = prompt(acknowledged, destination["document_id"], whole=True)
        assert acknowledged["rendered"]["error_modal"]["open"] is False
        assert acknowledged["rendered"]["window_error"] == ""
        for field in ("generation", "schema", "opened_document_id", "values"):
            assert recovered[field] == chosen_prompt[field]
        for role, old in before_state["documents"].items():
            unchanged_document(acknowledged["native_state"]["documents"][role], old, directory,
                               original if role == "replay" else scaled)
        consent = checkpoints["run_prompt_explicit_consent"]
        no_new_job(consent, before)
        consent_prompt = prompt(consent, destination["document_id"], whole=True, consent=True)
        for field in ("generation", "schema", "opened_document_id"):
            assert consent_prompt[field] == chosen_prompt[field]
    admitted = checkpoints["recalibration_replay_admitted" if incompatible else "replay_admitted"]
    job_id = replay_lineage(admitted, before, record, incompatible=incompatible, terminal=False)
    actual_ok_step = row["steps"][24]
    assert actual_ok_step["adopted_step"] == {"op": "click", "selector": "replay.ok"}
    assert actual_ok_step["last_frame_ordinal"] <= admitted["frame_ordinal"]
    assert any(job_id in jobs(frame["actual_native_jobs"])
               for frame in frames[actual_ok_step["first_frame_ordinal"]:admitted["frame_ordinal"]])
    assert all(job_id not in jobs(frame["actual_native_jobs"])
               for frame in frames[:actual_ok_step["first_frame_ordinal"]])
    final = checkpoints["recalibrated_replayed" if incompatible else "replayed"]
    assert replay_lineage(final, before, record, incompatible=incompatible, terminal=True) == job_id
    final_state = final["native_state"]
    target = final_state["documents"]["replay"]
    expected = evidence["outputs"]["u8-full-s2"] if incompatible else scaled
    document(target, directory, expected)
    preserved(target, destination)
    assert target["undo_steps"] == destination["undo_steps"]+1 and target["redo_steps"] == destination["redo_steps"]
    assert target["data_identity"] != destination["data_identity"] and target["dirty"] is True
    assert target["revisions"]["pixels"] == destination["revisions"]["pixels"]+1
    assert final_state["active_document_id"] == destination["document_id"]
    assert final_state["application_document_ids"] == before_state["application_document_ids"]
    assert final_state["desktop_document_ids"] == before_state["desktop_document_ids"]
    assert final_state["workflow"] == [record] and final_state["recording"] is False
    assert final_state["results"] == before_state["results"]
    assert final["rendered"]["gaussian_panel"]["remembered"] == expected_remembered
    cpu_readback(final, directory, expected)
    for checkpoint in row["checkpoints"]:
        if checkpoint["frame_ordinal"] >= before["frame_ordinal"]:
            state = checkpoint["native_state"]
            assert state["workflow"] == [record] and state["recording"] is False
            unchanged_document(state["documents"]["source"], source_applied, directory, scaled)
            if not incompatible:
                unchanged_document(state["documents"]["throwaway"], throwaway, directory, half)
            assert checkpoint["rendered"]["gaussian_panel"]["remembered"] == expected_remembered
    reopened = checkpoints["remembered_after_replay"]["rendered"]["gaussian_panel"]
    assert reopened["open"] is True and reopened["sigma"] == expected_remembered["sigma"]
    assert reopened["scaled_units"] is expected_remembered["scaled_units"]
    assert reopened["source_id"] == destination["document_id"] and reopened["preview"] is False
    assert reopened["draft_valid"] is True
    assert row["original_record"]["record"] == final_state["workflow"][0]
    record_equal(row["original_record"], final_state["workflow"][0], directory)
    for actual in (row["after"], checkpoints["cleanup"]["native_state"]):
        global_state(actual)
        for field in final_state:
            if field != "documents":
                assert actual[field] == final_state[field]
        for role, old in final_state["documents"].items():
            unchanged_document(actual["documents"][role], old, directory,
                               expected if role == "replay" else half if role == "throwaway" else scaled)
    assert checkpoints["cleanup"]["rendered"]["gaussian_panel"]["open"] is False
    assert set(row["original_after_raw"]) == ({"source", "replay"} if incompatible else {"source", "throwaway", "replay"})
    for member in row["original_after_raw"].values():
        assert raw(member, directory) == original
    usage(row["final_cleanup"]["usage"], empty=True)
    final_jobs = jobs(row["final_cleanup"]["actual_native_jobs"])
    assert all(value["state"] in {"COMPLETED", "FAILED", "CANCELED"} for value in final_jobs.values())
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "before_close", "closed"]
    opened, closing, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is closing["open"] is True and closed["open"] is False
    assert opened["session_id"] == closing["session_id"] == closed["session_id"]
    assert opened["context_id"] == closing["context_id"] == frames[0]["rendered"]["session"]["context_id"] > 0
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert row["final_cleanup"]["lifecycle"] == closed


@pytest.fixture(scope="module")
def replay_cpp_report():
    configured = os.environ.get("IMAGEJXX_REPLAY_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed standalone replay report missing: IMAGEJXX_REPLAY_CPP_REPORT")
        pytest.skip("Installed standalone replay report is optional outside the required desktop gate")
    path = Path(configured).resolve(strict=True)
    report = decode(path.read_bytes())
    assert type(report["schema_version"]) is int and report["schema_version"] == 1
    assert report["group_id"] == GROUP_ID and report["context"] == "imgui_cpp"
    assert report["status"] == "passed" and report["acceptance_claim"] is False
    assert report["contract_sha256"] == UI_CONTRACT_SHA
    assert all(report[field] is False for field in ("full61_verified", "full122_verified", "family_complete", "release_accepted"))
    assert [row["case_id"] for row in report["cases"]] == VARIANTS
    return report, path.parent


def python_row(variant, directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    assert Path(desktop.__file__).suffix in {".pyd", ".so"}
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return decode(editor._gaussian_replay_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("variant", VARIANTS)
def test_installed_gaussian_typed_replay_ui(variant, context, request, tmp_path, replay_oracles):
    if context == "imgui_cpp":
        report, report_root = request.getfixturevalue("replay_cpp_report")
        row = next(item for item in report["cases"] if item["case_id"] == variant)
        directory = report_root / variant.removeprefix(GROUP_ID+".")
    else:
        # tmp_path already identifies this variant/context. Avoid duplicating
        # its long name before native checkpoint and atomic-write suffixes.
        directory = tmp_path / "run"
        assert not directory.exists()
        row = python_row(variant, directory)
    assert_row(row, variant, context, directory, replay_oracles)
